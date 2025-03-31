#include "../services/DaqifiPB/DaqifiOutMessage.pb.h"
#include "state/data/BoardData.h"
#include "Util/StringFormatters.h"
#include "encoder.h"
#include "csv_encoder.h"
#include "../HAL/ADC.h"

#define MIN_BUFFER_SPACE 50 // Ensure there's space for null termination and newlines
#define TEMP_BUFFER_SIZE 270 // Temporary buffer size for each iteration

size_t csv_Encode(tBoardData* state, 
                  NanopbFlagsArray* fields, 
                  uint8_t* pBuffer, size_t buffSize) {
    if (pBuffer == NULL || buffSize < MIN_BUFFER_SPACE) { // Ensure buffer is not NULL and has a reasonable size
        return 0; // Return 0 if buffer is NULL or too small
    }

    char* charBuffer = (char*) pBuffer;
    size_t startIndex = 0;
    int i;
    size_t tempLen;
    char tempBuffer[TEMP_BUFFER_SIZE]; // Temporary buffer for each iteration
    size_t tempIndex;

    // Process data in rows: one AI sample list + DIO data per row
    while ((buffSize - startIndex) > MIN_BUFFER_SPACE) {
        uint32_t qSize = AInSampleList_Size();
        uint32_t dioSize = DIOSampleList_Size(&state->DIOSamples);
        
        if (qSize == 0 && dioSize == 0) {
            break; // No more data available
        }
        
        // Reset temp buffer index
        tempIndex = 0;

        // Stream timestamp as the first element
        tempLen = snprintf(NULL, 0, "%u", state->StreamTrigStamp);
        if (tempIndex + tempLen >= TEMP_BUFFER_SIZE) break;
        tempIndex += snprintf(tempBuffer + tempIndex, TEMP_BUFFER_SIZE - tempIndex, "%u", state->StreamTrigStamp);
        
        // Process one AI sample list
        AInPublicSampleList_t *pPublicSampleList;
        if (AInSampleList_PopFront(&pPublicSampleList) && pPublicSampleList != NULL) {
            for (i = 0; i < MAX_AIN_PUBLIC_CHANNELS; i++) {
                if (pPublicSampleList->isSampleValid[i]) {
                    AInSample data = pPublicSampleList->sampleElement[i];
                    double voltage = ADC_ConvertToVoltage(&data) * 1000; // Convert to mV
                    tempLen = snprintf(NULL, 0, ",%u,%d", data.Timestamp, (int)voltage);
                    if (tempIndex + tempLen >= TEMP_BUFFER_SIZE) {
                        free(pPublicSampleList);
                        break;
                    }
                    tempIndex += snprintf(tempBuffer + tempIndex, TEMP_BUFFER_SIZE - tempIndex, ",%u,%d", data.Timestamp, (int)voltage);
                } else {
                    if (tempIndex + 2 >= TEMP_BUFFER_SIZE) {
                        free(pPublicSampleList);
                        break;
                    }
                    tempIndex += snprintf(tempBuffer + tempIndex, TEMP_BUFFER_SIZE - tempIndex, ",,");
                }
            }
            free(pPublicSampleList);
        } else {
            for (i = 0; i < MAX_AIN_PUBLIC_CHANNELS; i++) {
                if (tempIndex + 2 >= TEMP_BUFFER_SIZE) break;
                tempIndex += snprintf(tempBuffer + tempIndex, TEMP_BUFFER_SIZE - tempIndex, ",,");
            }
        }

        // Process one DIO sample
        DIOSample data;
        if (!DIOSampleList_IsEmpty(&state->DIOSamples)) {
            DIOSampleList_PopFront(&state->DIOSamples, &data);
            tempLen = snprintf(NULL, 0, ",%u,%u", data.Timestamp, data.Values);
            if (tempIndex + tempLen >= TEMP_BUFFER_SIZE) break;
            tempIndex += snprintf(tempBuffer + tempIndex, TEMP_BUFFER_SIZE - tempIndex, ",%u,%u", data.Timestamp, data.Values);
        } else {
            if (tempIndex + 2 >= TEMP_BUFFER_SIZE) break;
            tempIndex += snprintf(tempBuffer + tempIndex, TEMP_BUFFER_SIZE - tempIndex, ",,");
        }
        
        tempLen = snprintf(NULL, 0, "\n");
        if (tempIndex + tempLen >= TEMP_BUFFER_SIZE) break;
        tempIndex += snprintf(tempBuffer + tempIndex, TEMP_BUFFER_SIZE - tempIndex, "\n");
        
        // Check if tempBuffer fits into the main buffer
        if (startIndex + tempIndex < buffSize) {
            memcpy(charBuffer + startIndex, tempBuffer, tempIndex);
            startIndex += tempIndex;
        } else {
            break; // Stop if main buffer is full
        }
    }
    
    if (startIndex >= buffSize) {
        startIndex = buffSize - 1;
    }
    charBuffer[startIndex] = '\0'; // Null-terminate the CSV string
    return startIndex;
}
