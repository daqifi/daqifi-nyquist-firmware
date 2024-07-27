/*! @file streaming.c 
 * 
 * This file implements the functions to manage the streaming
 */

#include "streaming.h"

//#include "HAL/ADC.h"
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

#define UNUSED(x) (void)(x)

#define BUFFER_SIZE  USB_WBUFFER_SIZE //2048
uint8_t buffer[BUFFER_SIZE];

//! Pointer to the board configuration data structure to be set in 
//! initialization
//static tStreamingConfig *pConfigStream;
//! Pointer to the board runtime configuration data structure, to be 
//! set in initialization
static StreamingRuntimeConfig *gpRuntimeConfigStream;
static tStreamingConfig* gpStreamingConfigInit;
//! Indicate if handler is used 
//static bool inHandler = false;


/*!
 *  Function for debugging - fills buffer with dummy data
 */
//static void Streaming_StuffDummyData(void);

/*!
 * Function to manage timer handler
 * @param[in] context    unused
 * @param[in] alarmCount unused
 */
//static void Streaming_TimerHandler(uintptr_t context, uint32_t alarmCount) {

//    UNUSED(context);
//    UNUSED(alarmCount);
//
//    static uint64_t scanTimerCount=0;
//    volatile uint32_t valueTMR = DRV_TMR_CounterValueGet(pRuntimeConfigStream->TSTimerHandle);
//    BoardData_Set(BOARDDATA_STREAMING_TIMESTAMP,0,&valueTMR ); 
//    volatile AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_AIN_CHANNELS);  
//    volatile AInArray *pBoardConfigADC=BoardConfig_Get(BOARDCONFIG_AIN_CHANNELS,0);
//    int i=0;
//    
//    if (inHandler) return;
//    inHandler = true;
//    
//    for(i=0;i<pBoardConfigADC->Size;i++){
//        if(pBoardConfigADC->Data[i].Config.MC12b.ChannelType==1){
//            if(pRuntimeAInChannels->Data[i].IsEnabled==1){
//                ADCCON3bits.ADINSEL = pBoardConfigADC->Data[i].Config.MC12b.BufferIndex;
//                ADCCON3bits.RQCNVRT = 1;
//            } 
//            //BoardData_Set(BOARDDATA_AIN_LATEST_TIMESTAMP,i,&valueTMR);
//        }
//        BoardData_Set(BOARDDATA_AIN_LATEST_TIMESTAMP,i,&valueTMR);
//    }
//    if(pRuntimeConfigStream->Frequency<=1000){
//        Streaming_Defer_Interrupt();
//    }else{
//        scanTimerCount++;
//        if(scanTimerCount>=pRuntimeConfigStream->ChannelScanTimeDiv){
//            Streaming_Defer_Interrupt();
//            scanTimerCount=0;
//        }       
//    }
// 
//    // On a 'System' prescale match
//    // - Read the latest DIO (if it's not streaming- otherwise we'll wind 
//    //   up with an extra sample)
//    // - Read the latest ADC (if it's not streaming- otherwise we'll wind up
//    //   with an extra sample)
//    // - Disable the charger and read battery voltages (Omit the regular 
//    //   channels so we don't wind up with extra samples)
//    // Otherwise
//    // - Trigger conversions if, and only if, their prescale has been matched
//
//    // TODO: Remove for production
//    //Streaming_StuffDummyData();
//    //inHandler = false;
//    //return;
//
//   
//
//    
//    //DIO_TIMING_TEST_TOGGLE_STATE();
//    inHandler = false;
//}

//static void Streaming_DedicatedADCHandler(uintptr_t context, uint32_t alarmCount) {
//    UNUSED(context);
//    UNUSED(alarmCount);
//    ADCCON3bits.ADINSEL = 0;
//    ADCCON3bits.RQCNVRT = 1;
//    ADCCON3bits.ADINSEL = 1;
//    ADCCON3bits.RQCNVRT = 1;
//    ADCCON3bits.ADINSEL = 2;
//    ADCCON3bits.RQCNVRT = 1;
//    ADCCON3bits.ADINSEL = 3;
//    ADCCON3bits.RQCNVRT = 1;
//    ADCCON3bits.ADINSEL = 4;
//    ADCCON3bits.RQCNVRT = 1;
//    asm("nop ");
//    //DIO_TIMING_TEST_WRITE_STATE(0);
//
//}

/*!
 * Starts the streaming timer
 */
//static void Streaming_Start(void) {
//    if (!pRuntimeConfigStream->Running) {
//        DRV_TMR_AlarmRegister(                                              
//                        pRuntimeConfigStream->TimerHandle,                  
//                        pRuntimeConfigStream->ClockDivider,                 
//                        true,                                               
//                        0,                                                  
//                        Streaming_TimerHandler);
//
//        DRV_TMR_Start(pRuntimeConfigStream->TimerHandle);
//      
//        pRuntimeConfigStream->Running = true;
//    }
//}

/*! 
 * Stops the streaming timer
 */
//static void Streaming_Stop(void) {
//    if (pRuntimeConfigStream->Running) {
//        DRV_TMR_Stop(pRuntimeConfigStream->TimerHandle);
//        DRV_TMR_AlarmDeregister(pRuntimeConfigStream->TimerHandle);
//        pRuntimeConfigStream->Running = false;
//    }
//    DRV_TMR_CounterValue32BitSet(pRuntimeConfigStream->TimerHandle, 0);
//}

void Streaming_Init(const tStreamingConfig* pStreamingConfigInit,           
                    const StreamingRuntimeConfig* pStreamingRuntimeConfigInit) {
//    pConfigStream = pStreamingConfigInit;
//    pRuntimeConfigStream = pStreamingRuntimeConfigInit;
//
//    TimestampTimer_Init();
//
//    // Initialize sample trigger timer
//    pRuntimeConfigStream->TimerHandle = DRV_TMR_Open(                       
//                        pConfigStream->TimerIndex,                         
//                        pConfigStream->TimerIntent);
//    if (DRV_HANDLE_INVALID ==                                               
//                    pStreamingRuntimeConfigInit->TimerHandle) {
//        // Client cannot open the instance.
//        SYS_DEBUG_BreakPoint();
//    }
//  
//
//    pRuntimeConfigStream->Running = false;
}

void Streaming_UpdateState(void) {
//    Streaming_Stop();
//
//    /* TODO: Calculate an appropriate runtimeConfig->ClockDivider and 
//     * Prescale value for each module/channel
//      - ClockDivider = Least Common Denominator of all the desired 
//                       frequencies
//      - Prescale = The integral multiple of ClockDivider closest to the 
//                   desired frequency
//      - The 'System' prescale should be selected to be approximately 1s 
//        (if possible), and should probably be excluded from the LCD calculation
//     */
//    pRuntimeConfigStream->StreamCountTrigger = 0;
//    pRuntimeConfigStream->MaxStreamCount =                                  
//                        pRuntimeConfigStream->StreamCountTrigger + 1;
//
//    // We never actually disable the streaming time because the system 
//    //functions (battery level, voltages, actually depend on it)
//    Streaming_Start();
}

void Streaming_Tasks(tBoardRuntimeConfig* runtimeConfig,                 
                        tBoardData* boardData) {
//    //Nanopb flag, decide what to write
//    NanopbFlagsArray nanopbFlag;
//    //! Structures size
//    volatile size_t usbSize, wifiSize, maxSize;
//    //! Boolean to indicate if the system has USB and Wifi actives. 
//    volatile bool hasUsb, hasWifi;
//    //! Counter to wifi configuration
//    int wifiCnt;
//    //! Pointer to client data in runtime, variable for legibility
//    TcpClientData * pClientData;
//
//    //Analog input availability. Digital input/output availability
//    bool AINDataAvailable = !AInSampleList_IsEmpty(&boardData->AInSamples);
//    bool DIODataAvailable = !DIOSampleList_IsEmpty(&boardData->DIOSamples);
//
//    UsbCdcData * pRunTimeUsbSettings = BoardRunTimeConfig_Get(              
//                        BOARDRUNTIME_USB_SETTINGS);
//
//    if (!runtimeConfig->StreamingConfig.IsEnabled) {
//        return;
//    }
//
//    do {
//        AINDataAvailable = !AInSampleList_IsEmpty(&boardData->AInSamples);
//        DIODataAvailable = !DIOSampleList_IsEmpty(&boardData->DIOSamples);
//        nanopbFlag.Size = 0;
//        usbSize = 0;
//        hasWifi = false;
//        hasUsb = true;
//        usbSize = wifiSize = 0;
//        maxSize = 0;
//
//        if (AINDataAvailable || DIODataAvailable) {
//            nanopbFlag.Data[nanopbFlag.Size++] =                            
//                        DaqifiOutMessage_msg_time_stamp_tag;
//        } else {
//            return;
//        }
//
//        // Decide how many samples we can send out
//        if (runtimeConfig->usbSettings.state == USB_CDC_STATE_PROCESS) {
//            usbSize = CircularBuf_NumBytesFree(                             
//                        &runtimeConfig->usbSettings.wCirbuf);
//            if (usbSize == runtimeConfig->usbSettings.wCirbuf.buf_size) {
//                hasUsb = true;
//            } else {
//                hasUsb = false;
//            }
//        }
//
//
//        if (runtimeConfig->serverData.state == IP_SERVER_PROCESS) {
//            for (wifiCnt = 0; wifiCnt < WIFI_MAX_CLIENT; ++wifiCnt) {
//                if (runtimeConfig->serverData.clients[wifiCnt].client !=    
//                        INVALID_SOCKET) {
//                    wifiSize = WIFI_WBUFFER_SIZE -                          
//                    runtimeConfig->serverData.clients[wifiCnt].writeBufferLength;
//                    hasWifi = true;
//                }
//            }
//        }
//
//        if (hasUsb && hasWifi) {
//            maxSize = min(usbSize, wifiSize);
//        } else if (hasUsb) {
//            maxSize = usbSize;
//        } else {
//            maxSize = wifiSize;
//        }
//
//        if (maxSize < 128) {
//            return;
//        }
//
//        if (AINDataAvailable) {
//            nanopbFlag.Data[nanopbFlag.Size++] =                            
//                        DaqifiOutMessage_analog_in_data_tag;
//        }
//
//        if (DIODataAvailable) {
//            nanopbFlag.Data[nanopbFlag.Size++] =                            
//                        DaqifiOutMessage_digital_data_tag;
//            nanopbFlag.Data[nanopbFlag.Size++] =                            
//                        DaqifiOutMessage_digital_port_dir_tag;
//        }
//
//        // Generate a packet
//        // TODO: ASCII Encoder
//        if (nanopbFlag.Size > 0) {
//
//            size_t size = 0;
//            if (runtimeConfig->StreamingConfig.Encoding == Streaming_Json) {
//                size = Json_Encode(                                         
//                        boardData,                                          
//                        &nanopbFlag,                                        
//                        (uint8_t **) & buffer);
//            } else {
//
//                size = Nanopb_Encode(                                       
//                        boardData,                                          
//                        &nanopbFlag,                                        
//                        (uint8_t **) & buffer);
//
//                if (runtimeConfig->StreamingConfig.Encoding ==               
//                        Streaming_TestData) {
//
//                    // if TestData_Len is specified, overwrite the buffer length
//                    if (commTest.TestData_len > 0)
//                        size = commTest.TestData_len;
//
//                    if (CommTest_FillTestData(buffer, size) == false) {
//                        size = 0; //discard the frame 
//                    }
//                }
//            }
//
//            // Write the packet out
//            if (size > 0) {
//                if (hasUsb) {
//                    UsbCdc_WriteToBuffer(                                   
//                        pRunTimeUsbSettings,                                
//                        (const char *) buffer,                              
//                        size);
//                }
//                if (hasWifi) {
//                    if (TCP_Server_Is_Blocked() == 0) {
//                        for (wifiCnt = 0; wifiCnt < WIFI_MAX_CLIENT; ++wifiCnt) {
//                            pClientData =                                   
//                                &runtimeConfig->serverData.clients[wifiCnt];
//                            if (INVALID_SOCKET != pClientData->client) {
//                                memcpy(                                     
//                                pClientData->writeBuffer +                  
//                                pClientData->writeBufferLength,             
//                                buffer,
//                                        size);
//                                pClientData->writeBufferLength += size;
//                            }
//                        }
//                    } else {
//                        maxSize = 0;
//                        continue;
//                    }
//                }
//                maxSize = maxSize - size;
//            } else {
//                // We don't have enough available buffer to encode another message
//                // Set maxSize to 0 to break out of loop
//                maxSize = 0;
//            }
//        }
//    } while (1);

}

void TimestampTimer_Init(void) {
//     Initialize and start timestamp timer
//     This is a free running timer used for reference - 
//     this doesn't interrupt or callback
    TimerApi_CallbackRegister();
    DRV_TMR_AlarmRegister(                                                  
                        pRuntimeConfigStream->TSTimerHandle,                
                        pRuntimeConfigStream->TSClockDivider,               
                        true,                                               
                        0,                                                  
                        NULL);
    DRV_TMR_AlarmDisable(pRuntimeConfigStream->TSTimerHandle);
    DRV_TMR_Start(pRuntimeConfigStream->TSTimerHandle);
}

//static void Streaming_StuffDummyData(void) {
    // Stuff stream with some data
    // Copy dummy samples to the data list
//    uint32_t i = 0;
//    static AInSample data;
//
//    AInSampleList * pAInSamples = BoardData_Get(                            
//                            BOARDDATA_AIN_SAMPLES,                          
//                            0);
//
//    data.Value = 'O';
//    data.Timestamp++;
//    // Skip zero so as not to allow multiple duplicate timestamps 
//    // (uninitialized channels will have 0 timestamp)
//    if (data.Timestamp == 0) data.Timestamp++;
//
//    for (i = 0; i < 16; ++i) {
//        data.Channel = i;
//        AInSampleList_PushBack(pAInSamples, (const AInSample *) &data);
//    }
//}