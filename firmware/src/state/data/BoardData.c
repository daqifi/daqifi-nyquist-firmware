/*! @file BoardConfig.c
 * @brief Implementation of the board data module
 * 
 * j.longares@abluethinginthecloud.com
 * 
 * A Blue Thing In The Cloud S.L.U.
 *   === When Technology becomes art ===
 * www.abluethinginthecloud.com
 *     
 */

#include "BoardData.h"
#include "Util/NullLockProvider.h"
#include "state/board/DIOConfig.h"
#include "../../HAL/ADC.h"
tBoardData __attribute__((coherent)) g_BoardData;

void InitializeBoardData(tBoardData* boardData) {
    // Initialize variable to known state
    memset(&g_BoardData, 0, sizeof (g_BoardData));

    memset(&boardData->DIOLatest, 0, sizeof (DIOSample));
    DIOSampleList_Initialize(&boardData->DIOSamples, MAX_DIO_SAMPLE_COUNT, false, &g_NullLockProvider);
    //    
    memset(&boardData->AInState, 0, sizeof (AInModDataArray));

    memset(&boardData->AInLatest, 0, sizeof (AInSampleArray));
    boardData->AInLatest.Size = MAX_AIN_CHANNEL;
    AInSampleList_Initialize(MAX_AIN_SAMPLE_COUNT, false, &g_NullLockProvider);
    boardData->AInState.Size = MAX_AIN_MOD;
    // Set default battery values for debugging - allows power on without ADC active


    boardData->PowerData.powerState = POWERED_DOWN;
    boardData->PowerData.requestedPowerState = NO_CHANGE;
    boardData->PowerData.battLow = false;
    boardData->PowerData.battVoltage = 0.0;
    boardData->PowerData.chargePct = 0;
    boardData->PowerData.USBSleep = false;
    boardData->PowerData.powerDnAllowed = true;
    boardData->PowerData.externalPowerSource = NO_EXT_POWER;
    boardData->PowerData.BQ24297Data.chargeAllowed = true;


    boardData->UIReadVars.LED1 = false;
    boardData->UIReadVars.LED2 = false;
    boardData->UIReadVars.button = false;
    
    //memcpy(&g_BoardData,boardData,sizeof(g_BoardData));


}

/*! This function is used for getting a board data parameter
 * @param[in] parameter Parameter to be get
 * @param[in] index In case that the parameter is an array, an index can be 
 * specified here for getting a specific member of the array
 * @return Parameter which is part of the global Board Configuration structure
 */
void *BoardData_Get(
        enum eBoardData parameter,
        uint8_t index) {
    void *pRet=NULL;
    switch (parameter) {
        case BOARDDATA_ALL_DATA:
            pRet= &g_BoardData;
            break;
        case BOARDDATA_IN_ISR:
            pRet= &g_BoardData.InISR;
            break;
        case BOARDDATA_DIO_LATEST:
            pRet= &g_BoardData.DIOLatest;
            break;
        case BOARDDATA_DIO_SAMPLES:
            pRet= &g_BoardData.DIOSamples.List;
            break;
        case BOARDATA_AIN_MODULE:
            if (index < g_BoardData.AInState.Size) {
                pRet= &g_BoardData.AInState.Data[ index ];
                break;
            }
            pRet= NULL;
            break;
        case BOARDDATA_AIN_LATEST:
            if (index < g_BoardData.AInLatest.Size) {
                pRet= &g_BoardData.AInLatest.Data[ index ];
                break;
            }
            pRet= NULL;
            break;
        case BOARDDATA_AIN_LATEST_SIZE:
            pRet= &g_BoardData.AInLatest.Size;
            break;
        case BOARDDATA_AIN_LATEST_TIMESTAMP:
            if (index < g_BoardData.AInLatest.Size) {
                pRet= &g_BoardData.AInLatest.Data[ index ].Timestamp;
                break;
            }
            pRet= NULL;
            break;
        case BOARDDATA_AIN_SAMPLES:
            pRet= &g_BoardData.AInSamples;
            break;
        case BOARDATA_POWER_DATA:
            pRet= &g_BoardData.PowerData;
            break;
        case BOARDDATA_UI_VARIABLES:
            pRet= &g_BoardData.UIReadVars;  
            break;
        case BOARDDATA_WIFI_SETTINGS:
            pRet= &g_BoardData.wifiSettings;
            break;
        case BOARDDATA_STREAMING_TIMESTAMP:
            pRet= &g_BoardData.StreamTrigStamp;
            break;
        case BOARDDATA_NUM_OF_FIELDS:
            break;
        default:
            pRet= NULL;
    }
    
    return pRet;
}

/*! This function is used for setting a board data parameter
 * @param[in] parameter Parameter to be set
 * @param[in] index In case that the parameter is an array, an index can be 
 * specified here for setting a specific member of the array
 * @param[in] pSetValue Pointer to the configuration value to be set
 */
void BoardData_Set(
        enum eBoardData parameter,
        uint8_t index,
        const void *pSetValue) {
    switch (parameter) {
        case BOARDDATA_IN_ISR:
            memcpy(
                    &g_BoardData.InISR,
                    pSetValue,
                    sizeof (g_BoardData.InISR));
            break;
        case BOARDDATA_DIO_LATEST:
            memcpy(
                    &g_BoardData.DIOLatest,
                    pSetValue,
                    sizeof (g_BoardData.DIOLatest));
            break;
        case BOARDDATA_DIO_SAMPLES:
            memcpy(
                    &g_BoardData.DIOSamples.List,
                    pSetValue,
                    sizeof (g_BoardData.DIOSamples));
            break;
        case BOARDATA_AIN_MODULE:
            if (index < g_BoardData.AInState.Size) {
                memcpy(
                        &g_BoardData.AInState.Data[ index ].AInTaskState,
                        pSetValue,
                        sizeof (AInTaskState_t));
            }
            break;
        case BOARDDATA_AIN_LATEST:
            if (index < g_BoardData.AInLatest.Size) {
                memcpy(
                        &g_BoardData.AInLatest.Data[ index ],
                        pSetValue,
                        sizeof (AInSample));

            }
            break;
        case BOARDDATA_AIN_LATEST_TIMESTAMP:
            if (index < g_BoardData.AInLatest.Size) {
                g_BoardData.AInLatest.Data[ index ].Timestamp = *(uint32_t*) pSetValue;
            }
            break;
        case BOARDDATA_AIN_SAMPLES:
            memcpy(
                    &g_BoardData.AInSamples.List,
                    pSetValue,
                    sizeof (HeapList));
            break;
        case BOARDATA_POWER_DATA:
            memcpy(
                    &g_BoardData.PowerData,
                    pSetValue,
                    sizeof (g_BoardData.PowerData));
            break;
        case BOARDDATA_UI_VARIABLES:
            memcpy(
                    &g_BoardData.UIReadVars,
                    pSetValue,
                    sizeof (g_BoardData.UIReadVars));
            break;
        case BOARDDATA_WIFI_SETTINGS:
            memcpy(
                    &g_BoardData.wifiSettings,
                    pSetValue,
                    sizeof (g_BoardData.wifiSettings));
            break;
        case BOARDDATA_STREAMING_TIMESTAMP:
            memcpy(
                    &g_BoardData.StreamTrigStamp,
                    pSetValue,
                    sizeof (g_BoardData.StreamTrigStamp));
            break;
        case BOARDDATA_NUM_OF_FIELDS:
        default:
            break;
    }
    
}
