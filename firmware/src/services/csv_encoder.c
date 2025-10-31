
#include "../services/DaqifiPB/DaqifiOutMessage.pb.h"
#include "state/data/BoardData.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "Util/StringFormatters.h"
#include "encoder.h"
#include "csv_encoder.h"
#include "../HAL/ADC.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// Track whether CSV header has been sent (reset when streaming stops)
static bool csvHeaderSent = false;

/*
 * CSV Format (optimized - header + enabled channels only):
 *
 * Header (sent once per session):
 * # Enabled ADC: 0,4,7
 * ch0_ts,ch0_val,ch4_ts,ch4_val,ch7_ts,ch7_val,dio_ts,dio_val
 *
 * Data rows (only enabled channels):
 * 798519461,2773,798519465,2801,798519469,2795,798520000,15
 *
 * Benefits:
 * - Only outputs enabled channels (not all 16)
 * - Header identifies column mapping
 * - Dramatically smaller files (e.g., 1 channel = 2 fields vs 32 fields)
 * - Per-channel timestamps for accuracy
 *
 * Examples:
 *
 * 1. Single channel (ch4) enabled:
 *    Header: # Enabled ADC: 4
 *            ch4_ts,ch4_val,dio_ts,dio_val
 *    Data:   798519461,2773,,
 *
 * 2. Multiple channels (ch0,4,7) and DIO:
 *    Header: # Enabled ADC: 0,4,7
 *            ch0_ts,ch0_val,ch4_ts,ch4_val,ch7_ts,ch7_val,dio_ts,dio_val
 *    Data:   798519461,2773,798519465,2801,798519469,2795,798520000,15
 *
 */

/**
 * @brief Reset CSV encoder state (call when streaming stops)
 */
void csv_ResetEncoder(void) {
    csvHeaderSent = false;
}

/**
 * @brief Generate CSV header with enabled channel information
 */
static size_t generateHeader(char *out, size_t rem, AInRuntimeArray* channelConfig) {
    char *q = out;
    int w;

    // Line 1: Comment listing enabled channels
    w = snprintf(q, rem, "# Enabled ADC:");
    if (w < 0 || (size_t)w >= rem) return 0;
    q += w; rem -= w;

    // List enabled channel IDs
    for (int i = 0; i < MAX_AIN_PUBLIC_CHANNELS; i++) {
        if (channelConfig->Data[i].IsEnabled) {
            w = snprintf(q, rem, " %d", i);
            if (w < 0 || (size_t)w >= rem) return 0;
            q += w; rem -= w;
        }
    }

    w = snprintf(q, rem, "\n");
    if (w < 0 || (size_t)w >= rem) return 0;
    q += w; rem -= w;

    // Line 2: Column headers (only enabled channels)
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

    // Add DIO columns
    w = snprintf(q, rem, ",dio_ts,dio_val\n");
    if (w < 0 || (size_t)w >= rem) return 0;
    q += w; rem -= w;

    return (size_t)(q - out);
}
static size_t tryWriteRow(
    char       *out,
    size_t      rem,
    tBoardData *state,
    AInRuntimeArray* channelConfig,
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

        if (*hadAIN && ainPeek->isSampleValid[i]) {
            AInSample *s = &ainPeek->sampleElement[i];
            int mv = (int)(ADC_ConvertToVoltage(s) * 1000.0);
            // First field has no leading comma
            if (firstField) {
                w = snprintf(q, rem, "%u,%d", s->Timestamp, mv);
                firstField = false;
            } else {
                w = snprintf(q, rem, ",%u,%d", s->Timestamp, mv);
            }
        } else {
            // Empty channel: two commas (or one if first field)
            if (firstField) {
                w = snprintf(q, rem, ",");
                firstField = false;
            } else {
                w = snprintf(q, rem, ",,");
            }
        }
        if (w < 0 || (size_t)w >= rem) return 0;
        q   += w; rem -= w;
    }

    // Write DIO timestamp,value pair
    if (*hadDIO) {
        w = snprintf(q, rem, ",%u,%u", dioPeek.Timestamp, dioPeek.Values);
    } else {
        w = snprintf(q, rem, ",,");
    }
    if (w < 0 || (size_t)w >= rem) return 0;
    q   += w; rem -= w;

  
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

    char   *p     = (char*)pBuffer;
    size_t  rem   = buffSize - 1;  // reserve 1 byte up front for '\0'
    size_t  total = 0;

    // Generate header on first call
    if (!csvHeaderSent) {
        size_t headerLen = generateHeader(p, rem, channelConfig);
        if (headerLen > 0) {
            p += headerLen;
            rem -= headerLen;
            total += headerLen;
            csvHeaderSent = true;
        }
    }

    while (1) {
        bool hadAIN, hadDIO;
        // attempt to write the next row in-place
        size_t rowLen = tryWriteRow(p, rem, state, channelConfig, &hadAIN, &hadDIO);
        if (rowLen == 0 || rowLen > rem) {
            break;  // no data left or row won?t fit
        }

        // now that it?s safely written, consume the queues:
        if (hadAIN) {
            AInPublicSampleList_t *tmp = NULL;
            AInSampleList_PopFront(&tmp);
            vPortFree(tmp);
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

