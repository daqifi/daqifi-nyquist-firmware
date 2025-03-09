#include "../services/DaqifiPB/DaqifiOutMessage.pb.h"
#include "state/data/BoardData.h"
#include "Util/StringFormatters.h"
#include "encoder.h"
#include "csv_encoder.h"
#include "../HAL/ADC.h"

size_t csv_Encode(tBoardData* state,
        NanopbFlagsArray* fields,
        uint8_t* pBuffer, size_t buffSize) {
    if (pBuffer == NULL || buffSize < 32) { // Ensure buffer is not NULL and has a reasonable size
        return 0; // Return 0 if buffer is NULL or too small
    }

    char* charBuffer = (char*) pBuffer;
    size_t startIndex = 0;
    int i;
    bool encodeADC = false;
    bool encodeDIO = false;

    // Determine which data should be encoded
    for (i = 0; i < fields->Size; ++i) {
        switch (fields->Data[i]) {
            case DaqifiOutMessage_analog_in_data_tag:
                encodeADC = true;
                break;
            case DaqifiOutMessage_digital_data_tag:
                encodeDIO = true;
                break;
            case DaqifiOutMessage_msg_time_stamp_tag:
                // Ensure timestamp is always included
                break;
            default:
                return 0; // Unknown data type, return 0
        }
    }

    // Stream timestamp as the first element
    int bytesNeeded = snprintf(NULL, 0, "%u", state->StreamTrigStamp);
    if (startIndex + bytesNeeded >= buffSize) return 0;
    startIndex += snprintf(charBuffer + startIndex, buffSize - startIndex, "%u", state->StreamTrigStamp);

    // Encode ADC values if required, otherwise insert empty placeholders
    if (encodeADC) {
        AInPublicSampleList_t *pPublicSampleList;
        if (AInSampleList_PopFront(&pPublicSampleList) && pPublicSampleList != NULL) {
            for (i = 0; i < MAX_AIN_PUBLIC_CHANNELS; i++) {
                if (pPublicSampleList->isSampleValid[i]) {
                    AInSample data = pPublicSampleList->sampleElement[i];
                    double voltage = ADC_ConvertToVoltage(&data) * 1000; // Convert to mV
                    bytesNeeded = snprintf(NULL, 0, ",%u,%d", data.Timestamp, (int) voltage);
                    if (startIndex + bytesNeeded >= buffSize) return 0;
                    startIndex += snprintf(charBuffer + startIndex, buffSize - startIndex, ",%u,%d", data.Timestamp, (int) voltage);
                } else {
                    startIndex += snprintf(charBuffer + startIndex, buffSize - startIndex, ",,");
                }
            }
            free(pPublicSampleList);
        } else {
            for (i = 0; i < MAX_AIN_PUBLIC_CHANNELS; i++) {
                startIndex += snprintf(charBuffer + startIndex, buffSize - startIndex, ",,");
            }
        }
    } else {
        for (i = 0; i < MAX_AIN_PUBLIC_CHANNELS; i++) {
            startIndex += snprintf(charBuffer + startIndex, buffSize - startIndex, ",,");
        }
    }

    // Encode packed DIO value if required, otherwise insert empty placeholders
    if (encodeDIO) {
        DIOSample data;
        if (!DIOSampleList_IsEmpty(&state->DIOSamples)) {
            DIOSampleList_PopFront(&state->DIOSamples, &data);
            bytesNeeded = snprintf(NULL, 0, ",%u,%u", data.Timestamp, data.Values);
            if (startIndex + bytesNeeded >= buffSize) return 0;
            startIndex += snprintf(charBuffer + startIndex, buffSize - startIndex, ",%u,%u", data.Timestamp, data.Values);
        } else {
            startIndex += snprintf(charBuffer + startIndex, buffSize - startIndex, ",,");
        }
    } else {
        startIndex += snprintf(charBuffer + startIndex, buffSize - startIndex, ",,");
    }

    if (startIndex + 1 >= buffSize) return 0;
    startIndex += snprintf(charBuffer + startIndex, buffSize - startIndex, "\n");
    charBuffer[startIndex] = '\0'; // Null-terminate the CSV string
    return startIndex;
}
