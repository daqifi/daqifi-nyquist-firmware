
#include "../services/DaqifiPB/DaqifiOutMessage.pb.h"
#include "state/data/BoardData.h"
#include "Util/StringFormatters.h"
#include "encoder.h"
#include "csv_encoder.h"
#include "../HAL/ADC.h"

#define MAX_COLUMNS 16 // Adjust based on max expected channels
#define MIN_BUFFER_SPACE 16 // Ensure there's space for null termination and newlines

size_t csv_Encode( tBoardData* state,                                  
                        NanopbFlagsArray* fields,                           
                        uint8_t* pBuffer, size_t buffSize) {
    if (pBuffer == NULL || buffSize < MIN_BUFFER_SPACE) { // Ensure buffer is not NULL and has a reasonable size
        return 0; // Return 0 if buffer is NULL or too small
    }

    char* charBuffer = (char*) pBuffer;
    size_t startIndex = 0;
    int i;
    
    // Process data in rows: one AI sample list + DIO data per row
    while ((buffSize - startIndex) > MIN_BUFFER_SPACE) {
        uint32_t qSize = AInSampleList_Size();
        uint32_t dioSize = DIOSampleList_Size(&state->DIOSamples);
        
        if (qSize == 0 && dioSize == 0) {
            break; // No more data available
        }
        
        // Stream timestamp as the first element
        startIndex += snprintf(charBuffer + startIndex, buffSize - startIndex, "%u", state->StreamTrigStamp);
        
        // Process one AI sample list
        AInPublicSampleList_t *pPublicSampleList;
        if (AInSampleList_PopFront(&pPublicSampleList) && pPublicSampleList != NULL) {
            for (i = 0; i < MAX_AIN_PUBLIC_CHANNELS; i++) {
                if (pPublicSampleList->isSampleValid[i]) {
                    AInSample data = pPublicSampleList->sampleElement[i];
                    double voltage = ADC_ConvertToVoltage(&data) * 1000; // Convert to mV
                    startIndex += snprintf(charBuffer + startIndex, buffSize - startIndex, ",%u,%d", data.Timestamp, (int)voltage);
                } else {
                    startIndex += snprintf(charBuffer + startIndex, buffSize - startIndex, ",,");
                }
            }
            free(pPublicSampleList);
        } else {
            // No AI data, add empty placeholders
            for (i = 0; i < MAX_AIN_PUBLIC_CHANNELS; i++) {
                startIndex += snprintf(charBuffer + startIndex, buffSize - startIndex, ",,");
            }
        }

        // Process one DIO sample
        DIOSample data;
        if (!DIOSampleList_IsEmpty(&state->DIOSamples)) {
            DIOSampleList_PopFront(&state->DIOSamples, &data);
            startIndex += snprintf(charBuffer + startIndex, buffSize - startIndex, ",%u,%u", data.Timestamp, data.Values);
        } else {
            startIndex += snprintf(charBuffer + startIndex, buffSize - startIndex, ",,");
        }
        
        startIndex += snprintf(charBuffer + startIndex, buffSize - startIndex, "\n");
    }
    
    charBuffer[startIndex] = '\0'; // Null-terminate the CSV string
    return startIndex;
}