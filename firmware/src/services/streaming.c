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
void Streaming_Tasks(StreamingRuntimeConfig* pStreamConfig, tBoardData* boardData) {
    if (!pStreamConfig->IsEnabled) {
        return;
    }

    NanopbFlagsArray nanopbFlag;
    size_t usbSize, wifiSize, sdSize, maxSize;
    bool hasUsb, hasWifi, hasSD;
    bool AINDataAvailable = !AInSampleList_IsEmpty(&boardData->AInSamples);
    bool DIODataAvailable = !DIOSampleList_IsEmpty(&boardData->DIOSamples);

    if (!AINDataAvailable && !DIODataAvailable) {
        return;
    }

    nanopbFlag.Size = 0;
    nanopbFlag.Data[nanopbFlag.Size++] = DaqifiOutMessage_msg_time_stamp_tag;

    do {
        AINDataAvailable = !AInSampleList_IsEmpty(&boardData->AInSamples);
        DIODataAvailable = !DIOSampleList_IsEmpty(&boardData->DIOSamples);

        if (!AINDataAvailable && !DIODataAvailable) {
            return;
        }

        usbSize = UsbCdc_WriteBuffFreeSize(NULL);
        wifiSize = WifiApi_WriteBuffFreeSize();
        sdSize = SDCard_WriteBuffFreeSize();

        hasUsb = (usbSize > BUFFER_SIZE);
        hasWifi = (wifiSize > BUFFER_SIZE);
        hasSD = (sdSize > BUFFER_SIZE);

        maxSize = BUFFER_SIZE;
        if (hasUsb) maxSize = min(maxSize, usbSize);
        if (hasWifi) maxSize = min(maxSize, wifiSize);
        if (hasSD) maxSize = min(maxSize, sdSize);

        if (maxSize < 128) {
            return;
        }

        if (AINDataAvailable) {
            nanopbFlag.Data[nanopbFlag.Size++] = DaqifiOutMessage_analog_in_data_tag;
        }
        if (DIODataAvailable) {
            nanopbFlag.Data[nanopbFlag.Size++] = DaqifiOutMessage_digital_data_tag;
            nanopbFlag.Data[nanopbFlag.Size++] = DaqifiOutMessage_digital_port_dir_tag;
        }

        size_t packetSize = 0;
        if (nanopbFlag.Size > 0) {
            if (pStreamConfig->Encoding == Streaming_Json) {
                packetSize = Json_Encode(boardData, &nanopbFlag, (uint8_t *) buffer, maxSize);
            } else {
                packetSize = Nanopb_Encode(boardData, &nanopbFlag, (uint8_t *) buffer, maxSize);
            }
        }

        if (packetSize > 0) {
            if (hasUsb) {
                UsbCdc_WriteToBuffer(NULL, (const char *) buffer, packetSize);
            }
            if (hasWifi) {
                WifiApi_WriteToBuffer((const char *) buffer, packetSize);
            }
            if (hasSD) {
                SDCard_WriteToBuffer((const char *) buffer, packetSize);
            }
        }

    } while (1);
}
/*
 * void Streaming_Tasks(tBoardRuntimeConfig* runtimeConfig, tBoardData* boardData) {
    // Check if streaming is enabled
    if (!runtimeConfig->StreamingConfig.IsEnabled) {
        return;
    }

    // Check for available data
    bool AINDataAvailable = !AInSampleList_IsEmpty(&boardData->AInSamples);
    bool DIODataAvailable = !DIOSampleList_IsEmpty(&boardData->DIOSamples);

    if (!AINDataAvailable && !DIODataAvailable) {
        return; // No data to stream
    }

    // Determine available buffer space
    size_t usbSize = UsbCdc_WriteBuffFreeSize(NULL);
    size_t wifiSize = WifiApi_WriteBuffFreeSize();
    size_t sdSize = SDCard_WriteBuffFreeSize();

    // Active channels based on buffer space
    bool hasUsb = usbSize > BUFFER_SIZE;
    bool hasWifi = wifiSize > BUFFER_SIZE;
    bool hasSD = sdSize > BUFFER_SIZE;

    // If no channels are available with sufficient space, exit
    if (!hasUsb && !hasWifi && !hasSD) {
        return;
    }

    // Calculate the smallest available buffer size for sending
    size_t maxSize = BUFFER_SIZE;
    if (hasUsb) maxSize = min(maxSize, usbSize);
    if (hasWifi) maxSize = min(maxSize, wifiSize);
    if (hasSD) maxSize = min(maxSize, sdSize);

    // Ensure there?s enough space to send data
    if (maxSize < 128) {
        return;
    }

    // Prepare data for streaming
    NanopbFlagsArray nanopbFlag = { .Size = 0 };
    nanopbFlag.Data[nanopbFlag.Size++] = DaqifiOutMessage_msg_time_stamp_tag;

    if (AINDataAvailable) {
        nanopbFlag.Data[nanopbFlag.Size++] = DaqifiOutMessage_analog_in_data_tag;
    }

    if (DIODataAvailable) {
        nanopbFlag.Data[nanopbFlag.Size++] = DaqifiOutMessage_digital_data_tag;
        nanopbFlag.Data[nanopbFlag.Size++] = DaqifiOutMessage_digital_port_dir_tag;
    }

    // Encode the data based on the specified format
    size_t packetSize = 0;
    if (runtimeConfig->StreamingConfig.Encoding == Streaming_Json) {
        packetSize = Json_Encode(boardData, &nanopbFlag, (uint8_t*)buffer, maxSize);
    } else {
        packetSize = Nanopb_Encode(boardData, &nanopbFlag, (uint8_t*)buffer, maxSize);
    }

    // Send the encoded data to active channels
    if (packetSize > 0) {
        if (hasUsb) {
            UsbCdc_WriteToBuffer(NULL, (const char*)buffer, packetSize);
        }
        if (hasWifi) {
            WifiApi_WriteToBuffer((const char*)buffer, packetSize);
        }
        if (hasSD) {
            SDCard_WriteToBuffer((const char*)buffer, packetSize);
        }
    }
}
 */
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