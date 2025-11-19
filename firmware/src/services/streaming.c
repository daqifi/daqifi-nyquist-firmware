/*! @file streaming.c 
 * 
 * This file implements the functions to manage the streaming
 */

#include "streaming.h"

#include "HAL/ADC.h"
#include "HAL/DIO.h"
#include "JSON_Encoder.h"
#include "csv_encoder.h"
#include "DaqifiPB/DaqifiOutMessage.pb.h"
#include "DaqifiPB/NanoPB_Encoder.h"
#include "Util/Logger.h"
#include "Util/CircularBuffer.h"
#include "UsbCdc/UsbCdc.h"
#include "../HAL/TimerApi/TimerApi.h"
#include "HAL/ADC/MC12bADC.h"
#include "sd_card_services/sd_card_manager.h"

//#define TEST_STREAMING

#define LOG_QUEUE_DROP_INTERVAL 100  // Log queue full error every N drops
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
            // Use object pool instead of heap allocation (eliminates vPortFree overhead)
            // No heap check needed - pool uses pre-allocated static memory
            pPublicSampleList = AInSampleList_AllocateFromPool();
            if(pPublicSampleList==NULL) {
                LOG_E("Streaming: Sample pool exhausted\r\n");
                continue;
            }
            for (i = 0; i < pAiRunTimeChannelConfig->Size; i++) {
                if (pAiRunTimeChannelConfig->Data[i].IsEnabled == 1
                        && AInChannel_IsPublic(&pBoardConfig->AInChannels.Data[i])) {
                    pAiSample = BoardData_Get(BOARDDATA_AIN_LATEST, i);
                    // Null check to prevent crash
                    if (pAiSample != NULL) {
                        // Use channel ID from BoardConfig (authoritative source) instead of sample data
                        pPublicSampleList->sampleElement[i].Channel=pBoardConfig->AInChannels.Data[i].DaqifiAdcChannelId;
                        pPublicSampleList->sampleElement[i].Timestamp=pAiSample->Timestamp;
                        pPublicSampleList->sampleElement[i].Value=pAiSample->Value;
                        pPublicSampleList->isSampleValid[i]=1;
                    } else {
                        // Mark as invalid if sample data unavailable
                        pPublicSampleList->isSampleValid[i]=0;
                    }
                }
            }
            if(!AInSampleList_PushBack(pPublicSampleList)){//failed pushing to Q
                static uint32_t queueDropCount = 0;
                if ((++queueDropCount % LOG_QUEUE_DROP_INTERVAL) == 0) {
                    LOG_E("Streaming: Sample queue full - dropped %u samples (queue at capacity)",
                          (unsigned)queueDropCount);
                }
                AInSampleList_FreeToPool(pPublicSampleList);  // Use pool!
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
        // Clear any stale samples from previous streaming session to free heap
        AInPublicSampleList_t* pStale;
        while (AInSampleList_PopFront(&pStale)) {
            if (pStale != NULL) {
                AInSampleList_FreeToPool(pStale);  // Use pool instead of vPortFree!
            }
        }

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

        // Single-interface streaming: only stream to the interface that initiated streaming
        // This prevents bandwidth overload at high sample rates
        switch (pRunTimeStreamConf->ActiveInterface) {
            case StreamingInterface_USB:
                hasUsb = (usbSize >= 128);
                hasWifi = false;
                hasSD = false;
                break;
            case StreamingInterface_WiFi:
                hasUsb = false;
                hasWifi = (wifiSize >= 128);
                hasSD = false;
                break;
            case StreamingInterface_SD:
                hasUsb = false;
                hasWifi = false;
                hasSD = (sdSize >= 128);
                break;
            case StreamingInterface_All:
            default:
                // Legacy mode: stream to all interfaces (may cause issues at high rates)
                hasUsb = (usbSize >= 128);
                hasWifi = (wifiSize >= 128);
                hasSD = (sdSize >= 128);
                break;
        }

        // Override: If SD card logging is explicitly enabled, automatically enable SD output
        // Only allow USB+SD (not WiFi+SD, as they share the SPI bus)
        sd_card_manager_settings_t* pSDCardSettings =
            BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
        if (pSDCardSettings && pSDCardSettings->enable &&
            pSDCardSettings->mode == SD_CARD_MANAGER_MODE_WRITE) {
            // Only enable SD if we're not streaming to WiFi (SPI bus conflict)
            if (pRunTimeStreamConf->ActiveInterface != StreamingInterface_WiFi) {
                hasSD = (sdSize >= 128);
            }
        }

        // Log streaming start info once for debugging
        static bool firstLog = false;
        if (!firstLog) {
            LOG_I("Streaming started: interface=%d, usbSize=%u, wifiSize=%u, sdSize=%u",
                  pRunTimeStreamConf->ActiveInterface, usbSize, wifiSize, sdSize);
            firstLog = true;
        }

        // CRITICAL: Always encode to drain the sample queue, even if no outputs have space
        // This prevents queue backup which causes the deferred interrupt task to drop samples
        // If no outputs available, sample will be encoded then discarded (better than queue backup)

        // Use maximum available space among all outputs for encoding (not just enabled ones)
        // This ensures we can always encode even if all outputs are temporarily full
        maxSize = 128;  // Minimum packet size
        if (usbSize > maxSize) maxSize = usbSize;
        if (wifiSize > maxSize) maxSize = wifiSize;
        if (sdSize > maxSize) maxSize = sdSize;

        // Cap at BUFFER_SIZE to prevent encoder overflow
        if (maxSize > BUFFER_SIZE) maxSize = BUFFER_SIZE;
        
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
            if(pRunTimeStreamConf->Encoding == Streaming_Csv){
                DIO_TIMING_TEST_WRITE_STATE(1);
                packetSize = csv_Encode(pBoardData, &nanopbFlag, (uint8_t *) buffer, maxSize);
                DIO_TIMING_TEST_WRITE_STATE(0);
            }
            else if (pRunTimeStreamConf->Encoding == Streaming_Json) {
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
                const uint8_t* p = buffer;
                size_t remaining = packetSize;
                size_t total_written = 0;
                unsigned int attempts = 0;
                const unsigned int max_attempts = 3;

                while (remaining > 0 && attempts < max_attempts) {
                    size_t w = sd_card_manager_WriteToBuffer((const char *)p, remaining);
                    if (w == 0) {
                        // No progress, break to avoid tight loop
                        break;
                    }
                    total_written += w;
                    p += w;
                    remaining -= w;
                    attempts++;
                    if (remaining > 0) {
                        // Partial write, yield to give SD task a chance
                        taskYIELD();
                    }
                }

                if (total_written != packetSize) {
                    LOG_E("SD: Write failed after %u attempts, expected=%u, written=%u",
                          attempts, (unsigned)packetSize, (unsigned)total_written);
                }

                static bool sd_logged = false;
                if (!sd_logged) {
                    LOG_D("SD: Write completed, packetSize=%u, written=%u, attempts=%u",
                          (unsigned)packetSize, (unsigned)total_written, attempts);
                    sd_logged = true;
                }
            } else {
                static bool no_sd_logged = false;
                if (!no_sd_logged) {
                    LOG_D("SD: Write skipped - hasSD is false");
                    no_sd_logged = true;
                }
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
        BaseType_t result = xTaskCreate((TaskFunction_t) streaming_Task,
                "Stream task",
                4096, NULL, 2, &gStreamingTaskHandle);
        if (result != pdPASS) {
            LOG_E("FATAL: Failed to create streaming_Task (4096 bytes)\r\n");
        }
    }
    if (gStreamingInterruptHandle == NULL) {
        BaseType_t result = xTaskCreate((TaskFunction_t) _Streaming_Deferred_Interrupt_Task,
                "Stream Interrupt",
                4096, NULL, 8, &gStreamingInterruptHandle);
        if (result != pdPASS) {
            LOG_E("FATAL: Failed to create _Streaming_Deferred_Interrupt_Task (4096 bytes)\r\n");
        }
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