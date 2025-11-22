/*! @file csv_encoder.c
 * @brief CSV encoder for streaming analog and digital samples.
 *
 * Encodes sample data into a human-readable CSV format optimized for:
 * - Only outputs enabled channels (reduces file size dramatically)
 * - Self-documenting header with device info and column names
 * - Per-channel timestamps for accurate timing analysis
 * - Calibrated millivolt values for immediate use
 *
 * Uses fast integer-to-string conversion to avoid snprintf overhead
 * in the hot path (data rows). Header generation uses snprintf for
 * simplicity since it runs only once per session.
 */

#include "../services/DaqifiPB/DaqifiOutMessage.pb.h"
#include "state/data/BoardData.h"
#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "Util/StringFormatters.h"
#include "Util/Logger.h"  // For LOG_E
#include "encoder.h"
#include "csv_encoder.h"
#include "../HAL/ADC.h"
#include "../HAL/TimerApi/TimerApi.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>  // For INT_MIN
#include <string.h>  // For strlen

// =============================================================================
// Fast Integer Formatting (avoids snprintf overhead in hot path)
// =============================================================================
#define MAX_UINT32_STR_LEN 11  // Max digits for uint32_t (4294967295) + null

/**
 * @brief Converts uint32 to string without snprintf overhead.
 *
 * @param value  The value to convert
 * @param buf    Output buffer pointer
 * @param rem    Remaining space in buffer
 * @return Updated buffer pointer, or NULL if insufficient space
 */
static inline char* uint32_to_str(uint32_t value, char* buf, size_t rem) {
    if (rem == 0) {
        return NULL;  // No space - signal error
    }

    if (value == 0) {
        *buf++ = '0';
        return buf;
    }

    char temp[MAX_UINT32_STR_LEN];
    int len = 0;
    while (value > 0) {
        temp[len++] = '0' + (value % 10);
        value /= 10;
    }

    // Check if we have enough space
    if ((size_t)len > rem) {
        return NULL;  // Not enough space - signal error
    }

    while (len > 0) {
        *buf++ = temp[--len];
    }

    return buf;
}

/**
 * @brief Converts signed int to string, handles INT_MIN edge case.
 *
 * @param value  The value to convert
 * @param buf    Output buffer pointer
 * @param rem    Remaining space in buffer
 * @return Updated buffer pointer, or NULL if insufficient space
 */
static inline char* int_to_str(int value, char* buf, size_t rem) {
    // Handle INT_MIN special case (-2147483648 cannot be negated safely)
    if (value == INT_MIN) {
        const char* str = "-2147483648";
        size_t len = strlen(str);  // Calculate length programmatically
        if (len > rem) {
            return NULL;  // Not enough space - signal error
        }
        while (*str) {
            *buf++ = *str++;
        }
        return buf;
    }

    if (value < 0) {
        if (rem == 0) {
            return NULL;  // No space for minus sign - signal error
        }
        *buf++ = '-';
        rem--;
        value = -value;
    }
    return uint32_to_str((uint32_t)value, buf, rem);  // Can return NULL
}

// Track whether CSV header has been sent (reset when streaming stops)
static bool csvHeaderSent = false;

// =============================================================================
// Pre-computed CSV Header Strings (stored in program memory for fast access)
// =============================================================================
// Avoids snprintf overhead during header generation - critical for minimizing
// file rotation pause at high sample rates (15kHz+).

// Metadata header prefixes
static const char CSV_HEADER_DEVICE_PREFIX[] = "# Device: ";
static const char CSV_HEADER_TICKRATE_PREFIX[] = "# Timestamp Tick Rate: ";
static const char CSV_HEADER_HZ_SUFFIX[] = " Hz\n";

// Channel header strings are now stored in board config (csvChannelHeadersFirst/Subsequent)
// This allows board-specific naming conventions (e.g., "ain" vs "ch" prefix)

// DIO header strings
static const char CSV_DIO_HEADER_FIRST[] = "dio_ts,dio_val\n";
static const char CSV_DIO_HEADER_SUBSEQUENT[] = ",dio_ts,dio_val\n";

/**
 * @brief Fast string copy from flash to RAM (inline for zero call overhead).
 *
 * Optimized for copying static strings from program memory. Avoids strlen()
 * overhead by copying until null terminator.
 *
 * @param dst Destination pointer
 * @param src Source string (typically from flash)
 * @return Updated destination pointer
 */
static inline char* fast_strcpy(char* dst, const char* src) {
    while (*src) {
        *dst++ = *src++;
    }
    return dst;
}

static inline char* fast_strcpy_bounded(char* dst, size_t* prem, const char* src) {
    while (*src && *prem > 0) {
        *dst++ = *src++;
        (*prem)--;
    }
    return dst;
}

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
 * @brief Generate CSV header with device metadata and column names (optimized)
 *
 * Uses pre-computed strings from flash to minimize file rotation latency.
 * Critical for maintaining data continuity at high sample rates (15kHz+).
 */
static size_t generateHeader(char *out, size_t rem, AInRuntimeArray* channelConfig, bool dioEnabled) {
    char *q = out;

    // Get board config early with NULL check
    const tBoardConfig* boardConfig = (const tBoardConfig*)BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    if (!boardConfig) {
        LOG_E("[CSV] Board config is NULL, cannot generate header");
        return 0;  // Cannot generate header without board config
    }

    // Get variant and serial number with NULL checks
    uint8_t variant = 0;
    uint64_t serialNum = 0;
    void* pVar = BoardConfig_Get(BOARDCONFIG_VARIANT, 0);
    void* pSer = BoardConfig_Get(BOARDCONFIG_SERIAL_NUMBER, 0);
    if (pVar) variant = *(uint8_t*)pVar;
    if (pSer) serialNum = *(uint64_t*)pSer;

    if (rem < 2) return 0; // minimal safety

    // Line 1: Device name
    q = fast_strcpy_bounded(q, &rem, CSV_HEADER_DEVICE_PREFIX);
    q = fast_strcpy_bounded(q, &rem, DAQIFI_PRODUCT_NAME);
    if (rem == 0) return 0;
    *q++ = ' '; rem--;
    q = uint32_to_str(variant, q, rem);
    if (q == NULL) return 0;
    if (rem == 0) return 0;
    *q++ = '\n'; rem--;

    // Line 2: Serial number
    int w = snprintf(q, rem, "# Serial Number: %016llX\n", (unsigned long long)serialNum);
    if (w < 0 || (size_t)w >= rem) return 0;
    q += w; rem -= (size_t)w;

    // Line 3: Timestamp tick rate
    uint32_t tickRate = TimerApi_FrequencyGet(boardConfig->StreamingConfig.TSTimerIndex);

    q = fast_strcpy_bounded(q, &rem, CSV_HEADER_TICKRATE_PREFIX);
    q = uint32_to_str(tickRate, q, rem);
    if (q == NULL) return 0;
    q = fast_strcpy_bounded(q, &rem, CSV_HEADER_HZ_SUFFIX);

    // Line 4: Column headers
    const char* const* headerFirst = boardConfig->csvChannelHeadersFirst;
    const char* const* headerSubsequent = boardConfig->csvChannelHeadersSubsequent;
    if (!headerFirst || !headerSubsequent) {
        LOG_E("[CSV] Board config missing CSV column header arrays");
        return 0;  // Safety check - arrays must be initialized
    }

    bool firstCol = true;
    for (int i = 0; i < MAX_AIN_PUBLIC_CHANNELS; i++) {
        if (channelConfig->Data[i].IsEnabled) {
            const char* header = firstCol ? headerFirst[i] : headerSubsequent[i];
            q = fast_strcpy_bounded(q, &rem, header);
            firstCol = false;
        }
    }

    if (dioEnabled) {
        const char* dio_header = firstCol ? CSV_DIO_HEADER_FIRST
                                          : CSV_DIO_HEADER_SUBSEQUENT;
        q = fast_strcpy_bounded(q, &rem, dio_header);
    } else {
        if (rem == 0) return 0;
        *q++ = '\n'; rem--;
    }

    return (size_t)(q - out);
}
/**
 * @brief Attempts to write one data row to the output buffer.
 *
 * Writes timestamp,value pairs for all enabled analog channels and DIO.
 * Uses peek-before-pop strategy: peeks queues first to check data availability,
 * writes the row, then caller pops if successful.
 *
 * @param out           Output buffer
 * @param rem           Remaining space in buffer
 * @param state         Board data state (for DIO samples)
 * @param channelConfig Channel enable configuration
 * @param dioEnabled    Whether DIO output is enabled
 * @param hadAIN        [out] True if analog samples were available
 * @param hadDIO        [out] True if DIO samples were available
 * @return Bytes written (0 if no data or insufficient space)
 */
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

            // Convert to calibrated millivolts (backwards compatible)
            double voltage_mv = ADC_ConvertToVoltage(s) * 1000.0;
            // Clamp to int32_t range for portability
            int32_t mv;
            if (voltage_mv > (double)INT32_MAX) {
                mv = INT32_MAX;
            } else if (voltage_mv < (double)INT32_MIN) {
                mv = INT32_MIN;
            } else {
                // Round to nearest instead of truncation
                mv = (int32_t)(voltage_mv >= 0.0 ? voltage_mv + 0.5 : voltage_mv - 0.5);
            }
            // First field has no leading comma
            char* p = q;
            size_t space = rem;
            if (!firstField) {
                if (space == 0) return 0;  // Buffer exhausted
                *p++ = ',';
                space--;
            }
            p = uint32_to_str(s->Timestamp, p, space);
            if (p == NULL) return 0;  // Buffer exhausted
            size_t used = p - q;
            space = (used < rem) ? rem - used : 0;
            if (space == 0) return 0;  // Buffer exhausted
            *p++ = ',';
            space--;
            p = int_to_str(mv, p, space);
            if (p == NULL) return 0;  // Buffer exhausted
            w = p - q;
            firstField = false;
        } else {
            // No valid sample for this enabled channel: emit empty ts,val pair
            // Always two fields (timestamp and value) to align with header
            char* p = q;
            size_t space = rem;

            // Check space before each write
            if (!firstField) {
                if (space == 0) return 0;  // Buffer exhausted
                *p++ = ',';  // separator before empty ts
                space--;
            }
            if (space < 1) return 0;  // Buffer exhausted
            *p++ = ',';  // separator between empty ts and empty val

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
            // Empty DIO: emit two empty fields (ts,val) to match header columns
            if (firstField) {
                w = snprintf(q, rem, ",");  // [empty_ts],[empty_val] - comma separates the two
                firstField = false;
            } else {
                w = snprintf(q, rem, ",,");  // [sep],[empty_ts],[empty_val]
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


/**
 * @brief Main CSV encoder - encodes all available samples into buffer.
 *
 * Encodes analog and DIO samples into CSV format. On first call, outputs
 * a header with device metadata and column names. Subsequent calls output
 * data rows only.
 *
 * @param state     Board data state (for DIO samples)
 * @param fields    Unused (for API compatibility with other encoders)
 * @param pBuffer   Output buffer
 * @param buffSize  Size of output buffer
 * @return Bytes written (excluding null terminator)
 */
size_t csv_Encode(
    tBoardData*       state,
    NanopbFlagsArray* fields,
    uint8_t*          pBuffer,
    size_t            buffSize
) {
    // Need at least room for '\n' + '\0'
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

