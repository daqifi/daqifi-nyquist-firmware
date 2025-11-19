
#include "../services/DaqifiPB/DaqifiOutMessage.pb.h"
#include "state/data/BoardData.h"
#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "Util/StringFormatters.h"
#include "encoder.h"
#include "csv_encoder.h"
#include "../HAL/ADC.h"
#include "../HAL/TimerApi/TimerApi.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>  // For INT_MIN

// Fast integer to string with bounds checking (replaces slow snprintf)
static inline char* uint32_to_str(uint32_t value, char* buf, size_t rem) {
    if (rem == 0) {
        return buf;  // No space
    }

    if (value == 0) {
        *buf++ = '0';
        return buf;
    }

    char temp[11];
    int len = 0;
    while (value > 0) {
        temp[len++] = '0' + (value % 10);
        value /= 10;
    }

    // Check if we have enough space
    if ((size_t)len > rem) {
        return buf;  // Not enough space, return original pointer
    }

    while (len > 0) {
        *buf++ = temp[--len];
    }

    return buf;
}

static inline char* int_to_str(int value, char* buf, size_t rem) {
    // Handle INT_MIN special case (undefined behavior on -value)
    if (value == INT_MIN) {
        const char* str = "-2147483648";
        size_t len = 11;  // Length of INT_MIN string
        if (len > rem) {
            return buf;  // Not enough space
        }
        while (*str) {
            *buf++ = *str++;
        }
        return buf;
    }

    if (value < 0) {
        if (rem == 0) {
            return buf;  // No space for minus sign
        }
        *buf++ = '-';
        rem--;
        value = -value;
    }
    return uint32_to_str((uint32_t)value, buf, rem);
}

// Track whether CSV header has been sent (reset when streaming stops)
static bool csvHeaderSent = false;

/*
 * CSV Format (optimized - header with metadata + enabled channels only):
 *
 * Header (sent once per session):
 * # Device: Nyquist NQ1
 * # Sample Rate: 100 Hz
 * ch0_ts,ch0_val,ch4_ts,ch4_val,ch7_ts,ch7_val,dio_ts,dio_val
 *
 * Data rows (only enabled channels):
 * 798519461,2773,798519465,2801,798519469,2795,798520000,15
 *
 * Benefits:
 * - Only outputs enabled channels (not all 16)
 * - Self-documenting with device info and sample rate
 * - Column names identify channel mapping
 * - Dramatically smaller files (e.g., 1 channel = 4 fields vs 34 fields)
 * - Per-channel timestamps for accuracy
 *
 * Examples:
 *
 * 1. Single channel (ch4) enabled at 100Hz:
 *    # Device: Nyquist NQ1
 *    # Sample Rate: 100 Hz
 *    ch4_ts,ch4_val,dio_ts,dio_val
 *    798519461,2773,,
 *
 * 2. Multiple channels (ch0,4,7) at 5000Hz:
 *    # Device: Nyquist NQ1
 *    # Sample Rate: 5000 Hz
 *    ch0_ts,ch0_val,ch4_ts,ch4_val,ch7_ts,ch7_val,dio_ts,dio_val
 *    798519461,2773,798519465,2801,798519469,2795,798520000,15
 *
 */

/**
 * @brief Reset CSV encoder state (call when streaming stops)
 */
void csv_ResetEncoder(void) {
    csvHeaderSent = false;
}

/**
 * @brief Generate CSV header with device metadata and column names
 */
static size_t generateHeader(char *out, size_t rem, AInRuntimeArray* channelConfig, bool dioEnabled) {
    char *q = out;
    int w;

    // Get variant and serial number with NULL checks
    uint8_t variant = 0;
    uint64_t serialNum = 0;
    void* pVar = BoardConfig_Get(BOARDCONFIG_VARIANT, 0);
    void* pSer = BoardConfig_Get(BOARDCONFIG_SERIAL_NUMBER, 0);
    if (pVar) variant = *(uint8_t*)pVar;
    if (pSer) serialNum = *(uint64_t*)pSer;

    // Line 1: Device name (Product + Variant)
    w = snprintf(q, rem, "# Device: %s %d\n", DAQIFI_PRODUCT_NAME, variant);
    if (w < 0 || (size_t)w >= rem) return 0;
    q += w; rem -= w;

    // Line 2: Serial number
    w = snprintf(q, rem, "# Serial Number: %016llX\n", (unsigned long long)serialNum);
    if (w < 0 || (size_t)w >= rem) return 0;
    q += w; rem -= w;

    // Line 3: Timestamp tick rate (for converting timestamps to seconds)
    // Get actual timer frequency (accounts for prescaler)
    const tBoardConfig* boardConfig = (const tBoardConfig*)BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    uint32_t tickRate = TIMER_CLOCK_FRQ;  // Default fallback
    if (boardConfig) {
        tickRate = TimerApi_FrequencyGet(boardConfig->StreamingConfig.TSTimerIndex);
    }
    w = snprintf(q, rem, "# Timestamp Tick Rate: %u Hz\n", tickRate);
    if (w < 0 || (size_t)w >= rem) return 0;
    q += w; rem -= w;

    // Line 4: Column headers (only enabled channels)
    bool firstCol = true;
    for (int i = 0; i < MAX_AIN_PUBLIC_CHANNELS; i++) {
        if (channelConfig->Data[i].IsEnabled) {
            if (firstCol) {
                w = snprintf(q, rem, "ch%d_ts,ch%d_val", i, i);
                firstCol = false;
            } else {
                w = snprintf(q, rem, ",ch%d_ts,ch%d_val", i, i);
            }
            if (w < 0 || (size_t)w >= rem) return 0;
            q += w; rem -= w;
        }
    }

    // Add DIO columns only if DIO is enabled
    if (dioEnabled) {
        if (firstCol) {
            w = snprintf(q, rem, "dio_ts,dio_val\n");
        } else {
            w = snprintf(q, rem, ",dio_ts,dio_val\n");
        }
        if (w < 0 || (size_t)w >= rem) return 0;
        q += w; rem -= w;
    } else {
        // No DIO columns - just end the header
        w = snprintf(q, rem, "\n");
        if (w < 0 || (size_t)w >= rem) return 0;
        q += w; rem -= w;
    }

    return (size_t)(q - out);
}
static size_t tryWriteRow(
    char       *out,
    size_t      rem,
    tBoardData *state,
    AInRuntimeArray* channelConfig,
    bool        dioEnabled,
    bool       *hadAIN,
    bool       *hadDIO
) {
    // 1) peek queues
    AInPublicSampleList_t *ainPeek = NULL;
    *hadAIN = AInSampleList_PeekFront(&ainPeek);

    DIOSample dioPeek = {0};
    *hadDIO = DIOSampleList_PeekFront(&state->DIOSamples, &dioPeek);

    
    if (!*hadAIN && !*hadDIO) {
        return 0;
    }

    char *q = out;
    int   w;
    bool firstField = true;

    // Write channel timestamp,value pairs (ONLY for enabled channels)
    for (int i = 0; i < MAX_AIN_PUBLIC_CHANNELS; i++) {
        // Skip disabled channels entirely
        if (!channelConfig->Data[i].IsEnabled) {
            continue;
        }

        if (*hadAIN && ainPeek && ainPeek->isSampleValid[i]) {
            AInSample *s = &ainPeek->sampleElement[i];
            // Output raw ADC counts instead of converted voltage (much faster!)
            // User can apply calibration in post-processing if needed
            int rawValue = s->Value;
            // First field has no leading comma
            char* p = q;
            size_t space = rem;
            if (!firstField) {
                if (space == 0) { w = 0; break; }
                *p++ = ',';
                space--;
            }
            p = uint32_to_str(s->Timestamp, p, space);
            size_t used = p - q;
            space = (used < rem) ? rem - used : 0;
            if (space == 0) { w = 0; break; }
            *p++ = ',';
            space--;
            p = int_to_str(rawValue, p, space);
            w = p - q;
            firstField = false;
        } else {
            // No valid sample for this enabled channel: emit empty ts,val pair
            // Always two fields (timestamp and value) to align with header
            char* p = q;
            if (!firstField) {
                *p++ = ',';
            }
            *p++ = ',';
            *p++ = ',';
            w = p - q;
            firstField = false;
        }
        if (w < 0 || (size_t)w >= rem) return 0;
        q += w; rem -= w;
    }

    // Write DIO timestamp,value pair only if DIO is enabled
    if (dioEnabled) {
        if (*hadDIO) {
            if (firstField) {
                w = snprintf(q, rem, "%u,%u", dioPeek.Timestamp, dioPeek.Values);
                firstField = false;
            } else {
                w = snprintf(q, rem, ",%u,%u", dioPeek.Timestamp, dioPeek.Values);
            }
        } else {
            // Empty DIO: always emit two fields (ts,val pair) to match header
            if (firstField) {
                w = snprintf(q, rem, ",,");  // Empty DIO ts,val pair (no leading comma)
                firstField = false;
            } else {
                w = snprintf(q, rem, ",,");  // Empty DIO ts,val pair (with leading comma)
            }
        }
        if (w < 0 || (size_t)w >= rem) return 0;
        q += w; rem -= w;
    }
    // If DIO disabled, don't output DIO columns at all

  
    if (rem < 1) return 0;
    *q++ = '\n';

    return (size_t)(q - out);
}


size_t csv_Encode(
    tBoardData*       state,
    NanopbFlagsArray* fields,
    uint8_t*          pBuffer,
    size_t            buffSize
) {
    // need at least room for '\n' + '\0'
    if (!pBuffer || buffSize < 2) {
        return 0;
    }

    // Get channel enable configuration
    AInRuntimeArray* channelConfig = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_AIN_CHANNELS);
    if (!channelConfig) {
        return 0;
    }

    // Get DIO enable state
    bool* pDioEnable = (bool*)BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_DIO_GLOBAL_ENABLE);
    bool dioEnabled = (pDioEnable && *pDioEnable);

    char   *p     = (char*)pBuffer;
    size_t  rem   = buffSize - 1;  // reserve 1 byte up front for '\0'
    size_t  total = 0;

    // Generate header on first call
    if (!csvHeaderSent) {
        size_t headerLen = generateHeader(p, rem, channelConfig, dioEnabled);
        if (headerLen == 0) {
            // Not enough space to write header; don't emit partial data
            *p = '\0';
            return 0;
        }
        p += headerLen;
        rem -= headerLen;
        total += headerLen;
        csvHeaderSent = true;
    }

    while (1) {
        bool hadAIN, hadDIO;
        // attempt to write the next row in-place
        size_t rowLen = tryWriteRow(p, rem, state, channelConfig, dioEnabled, &hadAIN, &hadDIO);
        if (rowLen == 0 || rowLen > rem) {
            break;  // no data left or row won?t fit
        }

        // now that it?s safely written, consume the queues:
        if (hadAIN) {
            AInPublicSampleList_t *tmp = NULL;
            AInSampleList_PopFront(&tmp);
            AInSampleList_FreeToPool(tmp);  // Use object pool instead of vPortFree
        }
        if (hadDIO) {
            DIOSample tmpD;
            DIOSampleList_PopFront(&state->DIOSamples, &tmpD);
        }

    
        p     += rowLen;
        rem   -= rowLen;
        total += rowLen;
    }

 
    *p = '\0';
    return total;
}

