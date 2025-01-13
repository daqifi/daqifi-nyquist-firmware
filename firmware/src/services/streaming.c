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
#include "Util/CircularBuffer.h"
#include "UsbCdc/UsbCdc.h"
#include "../HAL/TimerApi/TimerApi.h"
#include "HAL/ADC/MC12bADC.h"
#include "sd_card_services/sd_card_manager.h"

//#define TEST_STREAMING

#define UNUSED(x) (void)(x)
#ifndef min
#define min(x,y) ((x) <= (y) ? (x) : (y))
#endif // min

#ifndef max
#define max(x,y) ((x) >= (y) ? (x) : (y))
#endif // max

#define BUFFER_SIZE min(min(USBCDC_WBUFFER_SIZE, WIFI_WBUFFER_SIZE), SD_CARD_MANAGER_CONF_WBUFFER_SIZE)  //2048
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
static TaskHandle_t gStreamingTaskHandle;
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
    AInRuntimeArray* pAiRunTimeChannelConfig = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_AIN_CHANNELS);
    
    AInPublicSampleList_t *pPublicSampleList=NULL;
    AInSample *pAiSample;

    uint64_t ChannelScanFreqDivCount = 0;
#endif
    while (1) {
        ulTaskNotifyTake(pdFALSE, xBlockTime);

#if  !defined(TEST_STREAMING)
        if (pRunTimeStreamConf->IsEnabled) {
            pPublicSampleList=calloc(1,sizeof(AInPublicSampleList_t));
            if(pPublicSampleList==NULL)
                continue;
            for (i = 0; i < pAiRunTimeChannelConfig->Size; i++) {
                if (pAiRunTimeChannelConfig->Data[i].IsEnabled == 1
                        && pBoardConfig->AInChannels.Data[i].Config.MC12b.IsPublic == 1) {
                    pAiSample = BoardData_Get(BOARDDATA_AIN_LATEST, i);
                    pPublicSampleList->sampleElement[i].Channel=pAiSample->Channel;
                    pPublicSampleList->sampleElement[i].Timestamp=pAiSample->Timestamp;
                    pPublicSampleList->sampleElement[i].Value=pAiSample->Value;
                    pPublicSampleList->isSampleValid[i]=1;
                   
                }
            }
            if(!AInSampleList_PushBack(pPublicSampleList)){//failed pushing to Q
                free(pPublicSampleList);
            }

            if (pRunTimeStreamConf->ChannelScanFreqDiv == 1) {
                for (i = 0; i < pRunTimeAInModules->Size; ++i) {
                    ADC_TriggerConversion(&pBoardConfig->AInModules.Data[i], MC12B_ADC_TYPE_ALL);
                }
            } else if (pRunTimeStreamConf->ChannelScanFreqDiv != 0) {
                for (i = 0; i < pRunTimeAInModules->Size; ++i) {
                    ADC_TriggerConversion(&pBoardConfig->AInModules.Data[i], MC12B_ADC_TYPE_DEDICATED);
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
       
        xTaskNotifyGive(gStreamingTaskHandle);
#else
        Streaming_StuffDummyData();
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

/**
 * @brief Handles streaming tasks by checking available data and writing it to active communication channels.
 * 
 * This function continuously monitors the availability of Analog and Digital I/O data and streams it 
 * over active communication channels (USB, WiFi, SD). It encodes data in the specified format and writes 
 * the output to all active channels based on available buffer sizes.
 * 
 * @param runtimeConfig Pointer to the runtime configuration of the board, including streaming settings.
 * @param boardData Pointer to the data structure that contains the board's input/output data.
 * 
 * @note This function will return early if streaming is disabled or there is no data to process.
 */

void streaming_Task(void) {
     TickType_t xBlockTime = portMAX_DELAY;
    NanopbFlagsArray nanopbFlag;
    size_t usbSize, wifiSize, sdSize, maxSize;
    bool hasUsb, hasWifi, hasSD;
    bool AINDataAvailable;
    bool DIODataAvailable;
    size_t packetSize=0;    
    tBoardData * pBoardData = BoardData_Get(
            BOARDDATA_ALL_DATA,
            0);
     StreamingRuntimeConfig * pRunTimeStreamConf = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);
    while(1) {
        ulTaskNotifyTake(pdFALSE, xBlockTime);
        
        AINDataAvailable = !AInSampleList_IsEmpty();
        DIODataAvailable = !DIOSampleList_IsEmpty(&pBoardData->DIOSamples);

        if (!AINDataAvailable && !DIODataAvailable) {
            continue;
        }
       
        usbSize = UsbCdc_WriteBuffFreeSize(NULL);
        wifiSize = wifi_manager_GetWriteBuffFreeSize();
        sdSize = sd_card_manager_GetWriteBuffFreeSize();

        hasUsb = (usbSize > BUFFER_SIZE);
        hasWifi = (wifiSize > BUFFER_SIZE);
        hasSD = (sdSize > BUFFER_SIZE);

        maxSize = BUFFER_SIZE;
        if (hasUsb) maxSize = min(maxSize, usbSize);
        if (hasWifi) maxSize = min(maxSize, wifiSize);
        if (hasSD) maxSize = min(maxSize, sdSize);

        if (maxSize < 128) {
            continue;
        }
        
        nanopbFlag.Size = 0;
        nanopbFlag.Data[nanopbFlag.Size++] = DaqifiOutMessage_msg_time_stamp_tag;

        if (AINDataAvailable) {
            nanopbFlag.Data[nanopbFlag.Size++] = DaqifiOutMessage_analog_in_data_tag;
        }
        if (DIODataAvailable) {
            nanopbFlag.Data[nanopbFlag.Size++] = DaqifiOutMessage_digital_data_tag;
            nanopbFlag.Data[nanopbFlag.Size++] = DaqifiOutMessage_digital_port_dir_tag;
        }
        
        packetSize = 0;
        if (nanopbFlag.Size > 0) {
            if (pRunTimeStreamConf->Encoding == Streaming_Json) {
                DIO_TIMING_TEST_WRITE_STATE(1);
                packetSize = Json_Encode(pBoardData, &nanopbFlag, (uint8_t *) buffer, maxSize); 
                DIO_TIMING_TEST_WRITE_STATE(0);
            } else {   
                DIO_TIMING_TEST_WRITE_STATE(1);
                packetSize = Nanopb_Encode(pBoardData, &nanopbFlag, (uint8_t *) buffer, maxSize); 
                DIO_TIMING_TEST_WRITE_STATE(0);
            }
        }
        DIO_TIMING_TEST_WRITE_STATE(1);
        if (packetSize > 0) {
            if (hasUsb) {                
                UsbCdc_WriteToBuffer(NULL, (const char *) buffer, packetSize);               
            }
            if (hasWifi) {               
                wifi_manager_WriteToBuffer((const char *) buffer, packetSize);                
            }
            if (hasSD) {
                sd_card_manager_WriteToBuffer((const char *) buffer, packetSize);
            }
        }
        DIO_TIMING_TEST_WRITE_STATE(0);
       
        

    }
}

void TimestampTimer_Init(void) {
    //     Initialize and start timestamp timer
    //     This is a free running timer used for reference - 
    //     this doesn't interrupt or callback
    
    if (gStreamingTaskHandle == NULL) {
        xTaskCreate((TaskFunction_t) streaming_Task,
                "Stream task",
                4000, NULL, 2, &gStreamingTaskHandle);
    }
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
                //AInSampleList_PushBack(pAInSamples, (const AInSample *) &data);
            }
        }
        //ADC_TriggerConversion(&pBoardConfig->AInModules.Data[i], MC12B_ADC_TYPE_ALL);
    }
}
#endif