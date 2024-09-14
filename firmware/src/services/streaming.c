/*! @file streaming.c 
 * 
 * This file implements the functions to manage the streaming
 */

#include "streaming.h"

#include "HAL/ADC.h"
#include "HAL/DIO.h"
#include "JSON_Encoder.h"
#include "DaqifiPB/DaqifiOutMessage.pb.h"
#include "DaqifiPB/NanoPB_Encoder.h"
#include "Util/Logger.h"
//#include "TcpServer/TcpServer.h"
#include "Util/CircularBuffer.h"
//#include "commTest.h"
#include "UsbCdc/UsbCdc.h"
#include "../HAL/TimerApi/TimerApi.h"
#include "HAL/ADC/MC12bADC.h"
#include "SDcard/SDCard.h"

//#define TEST_STREAMING

#define UNUSED(x) (void)(x)
#ifndef min
#define min(x,y) ((x) <= (y) ? (x) : (y))
#endif // min

#ifndef max
#define max(x,y) ((x) >= (y) ? (x) : (y))
#endif // max

#define BUFFER_SIZE min(min(USBCDC_WBUFFER_SIZE, WIFI_WBUFFER_SIZE), SD_CARD_CONF_WBUFFER_SIZE)  //2048
uint8_t buffer[BUFFER_SIZE];

//! Pointer to the board configuration data structure to be set in 
//! initialization
//static tStreamingConfig *pConfigStream;
//! Pointer to the board runtime configuration data structure, to be 
//! set in initialization
static StreamingRuntimeConfig *gpRuntimeConfigStream;
static tStreamingConfig* gpStreamingConfig;
//! Indicate if handler is used 
static bool gInTimerHandler = false;
static TaskHandle_t gStreamingInterruptHandle;
#if  defined(TEST_STREAMING)
static void Streaming_StuffDummyData(void);
#endif

void _Streaming_Deferred_Interrupt_Task(void) {


    TickType_t xBlockTime = portMAX_DELAY;
#if  !defined(TEST_STREAMING)
    uint8_t i = 0;
    tBoardData * pBoardData = BoardData_Get(
            BOARDDATA_ALL_DATA,
            0);
    tBoardConfig * pBoardConfig = BoardConfig_Get(
            BOARDCONFIG_ALL_CONFIG,
            0);

    StreamingRuntimeConfig * pRunTimeStreamConf = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);

    AInModRuntimeArray * pRunTimeAInModules = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_AIN_MODULES);
    uint64_t ChannelScanFreqDivCount = 0;
#endif
    while (1) {
        ulTaskNotifyTake(pdFALSE, xBlockTime);
#if  !defined(TEST_STREAMING)
        if (pRunTimeStreamConf->IsEnabled) {
            if (pRunTimeStreamConf->ChannelScanFreqDiv == 1) {
                for (i = 0; i < pRunTimeAInModules->Size; ++i) {
                    ADC_TriggerConversion(&pBoardConfig->AInModules.Data[i], MC12B_ADC_TYPE_ALL);
                }
            } else if (pRunTimeStreamConf->ChannelScanFreqDiv != 0) {
                for (i = 0; i < pRunTimeAInModules->Size; ++i) {
                    ADC_TriggerConversion(&pBoardConfig->AInModules.Data[i], MC12B_ADC_TYPE_DEDICATED);
                    DIO_TIMING_TEST_TOGGLE_STATE();
                }

                if (ChannelScanFreqDivCount >= pRunTimeStreamConf->ChannelScanFreqDiv) {
                    for (i = 0; i < pRunTimeAInModules->Size; ++i) {
                        ADC_TriggerConversion(&pBoardConfig->AInModules.Data[i], MC12B_ADC_TYPE_SHARED);
                    }
                    ChannelScanFreqDivCount = 0;
                }
                ChannelScanFreqDivCount++;
            }
            DIO_StreamingTrigger(&pBoardData->DIOLatest, &pBoardData->DIOSamples);
        }
#else
        Streaming_StuffDummyData();
        DIO_TIMING_TEST_TOGGLE_STATE();
#endif
    }
}

void Streaming_Defer_Interrupt(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(gStreamingInterruptHandle, &xHigherPriorityTaskWoken);
    portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
}

/*!
 *  Function for debugging - fills buffer with dummy data
 */
//static void Streaming_StuffDummyData(void);

static void TSTimerCB(uintptr_t context, uint32_t alarmCount) {
    //DIO_TIMING_TEST_TOGGLE_STATE();
}

/*!
 * Function to manage timer handler
 * @param[in] context    unused
 * @param[in] alarmCount unused
 */
static void Streaming_TimerHandler(uintptr_t context, uint32_t alarmCount) {

    uint32_t valueTMR = TimerApi_CounterGet(gpStreamingConfig->TSTimerIndex);
    BoardData_Set(BOARDDATA_STREAMING_TIMESTAMP, 0, (const void*) &valueTMR);
    if (gInTimerHandler) return;
    gInTimerHandler = true;
    Streaming_Defer_Interrupt();
    gInTimerHandler = false;

}

/*!
 * Starts the streaming timer
 */
static void Streaming_Start(void) {
    if (!gpRuntimeConfigStream->Running) {
        TimerApi_Initialize(gpStreamingConfig->TimerIndex);
        TimerApi_PeriodSet(gpStreamingConfig->TimerIndex, gpRuntimeConfigStream->ClockPeriod);
        TimerApi_CallbackRegister(gpStreamingConfig->TimerIndex, Streaming_TimerHandler, 0);
        TimerApi_InterruptEnable(gpStreamingConfig->TimerIndex);
        TimerApi_Start(gpStreamingConfig->TimerIndex);
        gpRuntimeConfigStream->Running = 1;
    }
}

/*! 
 * Stops the streaming timer
 */
static void Streaming_Stop(void) {
    if (gpRuntimeConfigStream->Running) {
        TimerApi_Stop(gpStreamingConfig->TimerIndex);
        TimerApi_InterruptDisable(gpStreamingConfig->TimerIndex);
        gpRuntimeConfigStream->Running = false;
    }
}

void Streaming_Init(tStreamingConfig* pStreamingConfigInit,
        StreamingRuntimeConfig* pStreamingRuntimeConfigInit) {
    gpStreamingConfig = pStreamingConfigInit;
    gpRuntimeConfigStream = pStreamingRuntimeConfigInit;
    TimestampTimer_Init();
    TimerApi_Stop(gpStreamingConfig->TimerIndex);
    TimerApi_InterruptDisable(gpStreamingConfig->TimerIndex);
    TimerApi_PeriodSet(gpStreamingConfig->TimerIndex, gpRuntimeConfigStream->ClockPeriod);
    gpRuntimeConfigStream->Running = false;
}

void Streaming_UpdateState(void) {
    Streaming_Stop();
    Streaming_Start();
}

void Streaming_Tasks(tBoardRuntimeConfig* runtimeConfig,
        tBoardData* boardData) {
    //Nanopb flag, decide what to write
    NanopbFlagsArray nanopbFlag;
    //! Structures size
    volatile size_t usbSize, wifiSize, sdSize, maxSize;
    //! Boolean to indicate if the system has USB and Wifi actives. 
    volatile bool hasUsb, hasWifi, hasSD;
    //Analog input availability. Digital input/output availability
    bool AINDataAvailable = !AInSampleList_IsEmpty(&boardData->AInSamples);
    bool DIODataAvailable = !DIOSampleList_IsEmpty(&boardData->DIOSamples);
    SDCard_RuntimeConfig_t* pSdRuntimeConf=(SDCard_RuntimeConfig_t*)BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS)
;
    //    UsbCdcData_t * pRunTimeUsbSettings = BoardRunTimeConfig_Get(
    //            BOARDRUNTIME_USB_SETTINGS);
    if(pSdRuntimeConf->enable==true && pSdRuntimeConf->mode==SD_CARD_MODE_READ){
        SDCard_readStatus_t readStatus;
        usbSize = UsbCdc_WriteBuffFreeSize(NULL);
        if (usbSize > BUFFER_SIZE){
            readStatus=SDCard_Read((char*)buffer,(size_t*)&usbSize);
            if(usbSize>0 && readStatus==SD_CARD_READ_STATUS_SUCCESS)
                UsbCdc_WriteToBuffer(NULL, (const char *) buffer, usbSize);
            else if(readStatus==SD_CARD_READ_STATUS_END_OF_FILE){
                sprintf((char*)buffer,"\r\n__END OF FILE__\r\n");
                UsbCdc_WriteToBuffer(NULL, (const char *) buffer, strlen((char*)buffer));
                pSdRuntimeConf->mode=SD_CARD_MODE_NONE;
            }
        }
        return;
        
    }
    if (!runtimeConfig->StreamingConfig.IsEnabled) {
        return;
    }
    
    do {
        AINDataAvailable = !AInSampleList_IsEmpty(&boardData->AInSamples);
        DIODataAvailable = !DIOSampleList_IsEmpty(&boardData->DIOSamples);
        nanopbFlag.Size = 0;
        usbSize = 0;
        hasWifi = false;
        hasUsb = true;
        hasSD = false;
        usbSize = wifiSize = sdSize = 0;
        maxSize = 0;

        if (AINDataAvailable || DIODataAvailable) {
            nanopbFlag.Data[nanopbFlag.Size++] =
                    DaqifiOutMessage_msg_time_stamp_tag;
        } else {
            return;
        }

        // Decide how many samples we can send out
        usbSize = UsbCdc_WriteBuffFreeSize(NULL);
        if (usbSize > BUFFER_SIZE) {
            hasUsb = true;
        } else {
            hasUsb = false;
        }

//        wifiSize = WifiApi_WriteBuffFreeSize();
//        if (wifiSize > BUFFER_SIZE) {
//            hasWifi = true;
//        } else {
//            hasWifi = false;
//        }

        sdSize = SDCard_WriteBuffFreeSize();
        if (sdSize > BUFFER_SIZE) {
            hasSD = true;
        } else {
            hasSD = false;
        }

        if (hasUsb && hasWifi && hasSD) {
            maxSize = min(usbSize, min(wifiSize, sdSize));
        } else if (hasUsb && hasWifi) {
            maxSize = min(usbSize, wifiSize);
        } else if (hasWifi && hasSD) {
            maxSize = min(wifiSize, sdSize);
        } else if (hasUsb && hasSD) {
            maxSize = min(usbSize, sdSize);
        } else if (hasUsb) {
            maxSize = usbSize;
        } else if (hasWifi) {
            maxSize = wifiSize;
        } else if (hasSD) {
            maxSize = sdSize;
        }

        maxSize = min(maxSize, BUFFER_SIZE);

        if (maxSize < 128) {
            return;
        }

        if (AINDataAvailable) {
            nanopbFlag.Data[nanopbFlag.Size++] =
                    DaqifiOutMessage_analog_in_data_tag;
        }

        if (DIODataAvailable) {
            nanopbFlag.Data[nanopbFlag.Size++] =
                    DaqifiOutMessage_digital_data_tag;
            nanopbFlag.Data[nanopbFlag.Size++] =
                    DaqifiOutMessage_digital_port_dir_tag;
        }

        // Generate a packet
        // TODO: ASCII Encoder
        if (nanopbFlag.Size > 0) {
            size_t size = 0;
            if (runtimeConfig->StreamingConfig.Encoding == Streaming_Json) {
                size = Json_Encode(
                        boardData,
                        &nanopbFlag,
                        (uint8_t *) buffer, maxSize);
            } else {

                size = Nanopb_Encode(
                        boardData,
                        &nanopbFlag,
                        (uint8_t *) buffer, maxSize);
            }

            // Write the packet out
            if (size > 0) {
                if (hasUsb) {
                    UsbCdc_WriteToBuffer(NULL, (const char *) buffer, size);
                }
                if (hasWifi) {
                    WifiApi_WriteToBuffer((const char *) buffer, size);
                }
                if (hasSD) {
                    SDCard_WriteToBuffer((const char *) buffer, size);
                }
            }
        }
    } while (1);

}

void TimestampTimer_Init(void) {
    //     Initialize and start timestamp timer
    //     This is a free running timer used for reference - 
    //     this doesn't interrupt or callback
    if (gStreamingInterruptHandle == NULL) {
        xTaskCreate((TaskFunction_t) _Streaming_Deferred_Interrupt_Task,
                "Stream Interrupt",
                2048, NULL, 8, &gStreamingInterruptHandle);
    }
    TimerApi_Stop(gpStreamingConfig->TSTimerIndex);
    TimerApi_Initialize(gpStreamingConfig->TSTimerIndex);
    TimerApi_InterruptDisable(gpStreamingConfig->TSTimerIndex);
    TimerApi_CallbackRegister(gpStreamingConfig->TSTimerIndex, TSTimerCB, 0);
    TimerApi_PeriodSet(gpStreamingConfig->TSTimerIndex, gpRuntimeConfigStream->TSClockPeriod);
    TimerApi_InterruptEnable(gpStreamingConfig->TSTimerIndex);
    TimerApi_Start(gpStreamingConfig->TSTimerIndex);

}
#if  defined(TEST_STREAMING)

static void Streaming_StuffDummyData(void) {
    // Stuff stream with some data
    // Copy dummy samples to the data list
    uint32_t i = 0;
    int k = 0;
    static AInSample data;

    AInSampleList * pAInSamples = BoardData_Get(
            BOARDDATA_AIN_SAMPLES,
            0);
    StreamingRuntimeConfig * pRunTimeStreamConf = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);

    AInModRuntimeArray * pRunTimeAInModules = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_AIN_MODULES);

    AInRuntimeArray* pAiRunTimeChannelConfig = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_AIN_CHANNELS);

    AInArray * pBoardConfig = BoardConfig_Get(
            BOARDCONFIG_AIN_CHANNELS,
            0);

    if (!pRunTimeStreamConf->IsEnabled) {
        return;
    }
    data.Timestamp++;
    for (i = 0; i < pRunTimeAInModules->Size; ++i) {
        for (k = 0; k < pAiRunTimeChannelConfig->Size; k++) {
            if (pAiRunTimeChannelConfig->Data[k].IsEnabled == 1
                    && pBoardConfig->Data[k].Config.MC12b.IsPublic == 1) {
                data.Value = k;
                data.Channel = k;
                AInSampleList_PushBack(pAInSamples, (const AInSample *) &data);
            }
        }
        //ADC_TriggerConversion(&pBoardConfig->AInModules.Data[i], MC12B_ADC_TYPE_ALL);
    }
}
#endif