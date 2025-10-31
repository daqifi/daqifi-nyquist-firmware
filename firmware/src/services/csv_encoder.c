
#include "../services/DaqifiPB/DaqifiOutMessage.pb.h"
#include "state/data/BoardData.h"
#include "Util/StringFormatters.h"
#include "encoder.h"
#include "csv_encoder.h"
#include "../HAL/ADC.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

/*
 * CSV Format (simplified - removed redundant StreamTrigStamp):
 * ch0_ts,ch0_val,ch1_ts,ch1_val, ..., ch15_ts,ch15_val,dio_ts,dio_val\n\0
 *
 * Notes:
 * - Each row represents one data packet.
 * - Per-channel timestamps provide accurate conversion times
 * - If a channel or DIO sample is not valid, its fields appear as empty: ",,"
 * - Each row always ends with \n and is null-terminated (\0).
 *
 * Examples:
 *
 * 1. Only Analog Channel 3 is valid, no DIO sample:
 *  ,,,,,,2938580134,1200,,,,,,,,,,,,,,,,,,,,,
 *
 * 2. Analog Channel 5 and DIO both valid:
 *  ,,,,,,,,,,2938580567,1225,,,,,,,,,,,,,,,2940010001,3
 *
 * 3. All fields invalid (empty row, rare but valid state):
 *  ,,,,,,,,,,,,,,,,,,,,,,,,,,,,,,
 *
 * 4. Analog Channel 0 and 1 valid, DIO also valid:
 *  2938580011,1300,2938580015,1305,,,,,,,,,,,,,,,,,,,,,,,2940010008,15
 *
 */
static size_t tryWriteRow(
    char       *out,
    size_t      rem,
    tBoardData *state,
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

    // Write channel timestamp,value pairs (no leading StreamTrigStamp)
    for (int i = 0; i < MAX_AIN_PUBLIC_CHANNELS; i++) {
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

    char   *p     = (char*)pBuffer;
    size_t  rem   = buffSize - 1;  // reserve 1 byte up front for '\0'
    size_t  total = 0;

    while (1) {
        bool hadAIN, hadDIO;
        // attempt to write the next row in-place
        size_t rowLen = tryWriteRow(p, rem, state, &hadAIN, &hadDIO);
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

