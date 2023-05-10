#include "streaming.h"

#include "HAL/ADC.h"
#include "HAL/DIO.h"
#include "json/Encoder.h"
#include "nanopb/DaqifiOutMessage.pb.h"
#include "nanopb/Encoder.h"
#include "Util/Logger.h"
#include "TcpServer/TcpServer.h"
#include "Util/CircularBuffer.h"
#include "commTest.h"
#include "UsbCdc/UsbCdc.h"

#define UNUSED(x) (void)(x)

#define BUFFER_SIZE  USB_WBUFFER_SIZE //2048
uint8_t buffer[BUFFER_SIZE];
size_t loop = 0;
void Streaming_StuffDummyData (void); // Function for debugging - fills buffer with dummy data

static void Streaming_TimerHandler(uintptr_t context, uint32_t alarmCount)
{
    static bool inHandler = false;
    
    UNUSED(context);
    UNUSED(alarmCount);
    
    if(inHandler) return;
    inHandler = true;
    
    // On a 'System' prescale match
    // - Read the latest DIO (if it's not streaming- otherwise we'll wind up with an extra sample)
    // - Read the latest ADC (if it's not streaming- otherwise we'll wind up with an extra sample)
    // - Disable the charger and read battery voltages (Omit the regular channels so we don't wind up with extra samples)
    // Otherwise
    // - Trigger conversions if, and only if, their prescale has been matched
    
    // TODO: Remove for production
    //Streaming_StuffDummyData();
    //inHandler = false;
    //return;

    Streaming_Defer_Interrupt();
    
    inHandler = false;
}

/*!
 * Starts the streaming timer
 * @param[in] runtimeConfig The runtime configurations
 */
static void Streaming_Start(StreamingRuntimeConfig* runtimeConfig)
{
    if (!runtimeConfig->Running)
    {       
        DRV_TMR_AlarmRegister(runtimeConfig->TimerHandle,
            runtimeConfig->ClockDivider,
            true,
            0,
            Streaming_TimerHandler);
        
        DRV_TMR_Start(runtimeConfig->TimerHandle);
        
        runtimeConfig->Running = true;
    }
}

/*! Stops the streaming timer
 * @param{in/out] runtimeConfig The runtime configuration
 */
static void Streaming_Stop(StreamingRuntimeConfig* runtimeConfig)
{
    if (runtimeConfig->Running)
    {
        DRV_TMR_Stop(runtimeConfig->TimerHandle);
        DRV_TMR_AlarmDeregister(runtimeConfig->TimerHandle);
        runtimeConfig->Running = false;
    }
    DRV_TMR_CounterValue32BitSet(runtimeConfig->TimerHandle, 0);
}

/*! Initializes the streaming component
 * @param[in] Streaming configuration
 * @param[out] Streaming configuration in runtime
 */
void Streaming_Init(    const tStreamingConfig* config,                     \
                        StreamingRuntimeConfig* runtimeConfig)
{
    // Initialize sample trigger timer
    runtimeConfig->TimerHandle = DRV_TMR_Open(                              \
                        config->TimerIndex,                                 \
                        config->TimerIntent);
    if( runtimeConfig->TimerHandle == DRV_HANDLE_INVALID )
    {
        // Client cannot open the instance.
         SYS_DEBUG_BreakPoint();
    }
    
    runtimeConfig->Running = false;
}

/*! Updates the streaming timer 
 * @param[in] boardConfig       Board configuration, includes board type
 * @param[in/out] runtimeConfig The runtime configuration
 */
void Streaming_UpdateState(                                                 \
                        const tBoardConfig* boardConfig,                    \
                        tBoardRuntimeConfig* runtimeConfig )
{
    Streaming_Stop(&runtimeConfig->StreamingConfig);
    
    /* TODO: Calculate an appropriate runtimeConfig->ClockDivider and 
     * Prescale value for each module/channel
      - ClockDivider = Least Common Denominator of all the desired 
                       frequencies
      - Prescale = The integral multiple of ClockDivider closest to the 
                   desired frequency
      - The 'System' prescale should be selected to be approximately 1s 
        (if possible), and should probably be excluded from the LCD calculation
    */
    runtimeConfig->StreamingConfig.StreamCountTrigger = 0;
    runtimeConfig->StreamingConfig.MaxStreamCount =                         \
                        runtimeConfig->StreamingConfig.StreamCountTrigger+1;
    
    // We never actually disable the streaming time because the system 
    //functions (battery level, voltages, actually depend on it)
    Streaming_Start(&runtimeConfig->StreamingConfig);
}

/*!
 * Called to write streaming data to the underlying tasks
 * @param boardConfig   The hardware configuration
 * @param runtimeConfig The runtime configuration
 * @param boardData     The board data
 */
void Streaming_Tasks(   const tBoardConfig* boardConfig,                    \
                        tBoardRuntimeConfig* runtimeConfig,                 \
                        tBoardData* boardData)
{
    //Nanopb flag, decide what to write
    NanopbFlagsArray nanopbFlag;
    //! 
    size_t usbSize, wifiSize, maxSize;
    //! Boolean to indicate if the system has USB and Wifi actives. 
    bool hasUsb, hasWifi;
    //! 
    int wifiCnt;
    
    //Analog input availability. Digital input/output availability
    bool AINDataAvailable=!AInSampleList_IsEmpty(&boardData->AInSamples);
    bool DIODataAvailable=!DIOSampleList_IsEmpty(&boardData->DIOSamples);
    
    UsbCdcData * pRunTimeUsbSettings = BoardRunTimeConfig_Get(              \
                        BOARDRUNTIME_USB_SETTINGS);
    
    if (!runtimeConfig->StreamingConfig.IsEnabled)
    {
        return;
    }

    do{
        AINDataAvailable=!AInSampleList_IsEmpty(&boardData->AInSamples);
        DIODataAvailable=!DIOSampleList_IsEmpty(&boardData->DIOSamples);
        nanopbFlag.Size = 0;
        usbSize    = 0;
        hasUsb     = hasWifi    = false;
        usbSize    = wifiSize   = 0;
        maxSize    = 0;
        
        if(AINDataAvailable || DIODataAvailable){
            nanopbFlag.Data[nanopbFlag.Size++] = DaqifiOutMessage_msg_time_stamp_tag;
        }else{
            return;
        }
         
        // Decide how many samples we can send out
        if (runtimeConfig->usbSettings.state == USB_CDC_STATE_PROCESS)
        {
            usbSize = CircularBuf_NumBytesFree(&runtimeConfig->usbSettings.wCirbuf);
            hasUsb  = true;
        }

        if (runtimeConfig->serverData.state == IP_SERVER_PROCESS)
        {
            for (wifiCnt=0; wifiCnt<WIFI_MAX_CLIENT; ++wifiCnt)
            {
                if (runtimeConfig->serverData.clients[wifiCnt].client != INVALID_SOCKET)
                {
                    wifiSize = WIFI_WBUFFER_SIZE - runtimeConfig->serverData.clients[wifiCnt].writeBufferLength;
                    hasWifi = true;
                }
            }
        }
        
        if (hasUsb && hasWifi){
            maxSize = min(usbSize, wifiSize);
        }
        else if(hasUsb){
            maxSize = usbSize;
        }
        else{
            maxSize = wifiSize;
        }

        if (maxSize < 128){
            return;
        }
        
        if (AINDataAvailable){
            nanopbFlag.Data[nanopbFlag.Size++] = DaqifiOutMessage_analog_in_data_tag;
        }

        if (DIODataAvailable){
            nanopbFlag.Data[nanopbFlag.Size++] = DaqifiOutMessage_digital_data_tag;
            nanopbFlag.Data[nanopbFlag.Size++] = DaqifiOutMessage_digital_port_dir_tag;
        }
           
        // Generate a packet
        // TODO: ASCII Encoder
        if(nanopbFlag.Size>0){
            
            size_t size = 0;
            if (runtimeConfig->StreamingConfig.Encoding == Streaming_Json){
                size = Json_Encode(                                         \
                        boardData,                                          \
                        &nanopbFlag,                                        \
                        buffer,                                             \
                        maxSize);
            }
            else{

                size = Nanopb_Encode(boardData, &nanopbFlag, buffer, maxSize);
                
                if(runtimeConfig->StreamingConfig.Encoding == Streaming_TestData){
  
                    // if TestData_Len is specified, overwrite the buffer length
                    if(commTest.TestData_len>0)
                    size = commTest.TestData_len;
                    
                    if(CommTest_FillTestData(buffer, size)==false){
                        size = 0;//discard the frame 
                    }
                }
            }

            // Write the packet out
            if (size > 0){

                if (hasUsb){
                    UsbCdc_WriteToBuffer(                                   \
                        pRunTimeUsbSettings,                                \
                        (const char *)buffer,                               \
                        size);
                }

                if (hasWifi){
                    
                    if( TCP_Server_Is_Blocked() == 0 ){
                        for (wifiCnt=0; wifiCnt<WIFI_MAX_CLIENT; ++wifiCnt)
                        {
                            if (runtimeConfig->serverData.clients[wifiCnt].client != INVALID_SOCKET)
                            {
                                memcpy(runtimeConfig->serverData.clients[wifiCnt].writeBuffer + runtimeConfig->serverData.clients[wifiCnt].writeBufferLength, buffer, size);
                                runtimeConfig->serverData.clients[wifiCnt].writeBufferLength += size;
                            }
                        }
                    }
                    else{
                        maxSize = 0;
                        continue;
                    }
                }
                maxSize = maxSize - size;
            }else
            {
                // We don't have enough available buffer to encode another message
                // Set maxSize to 0 to break out of loop
                maxSize = 0;                
            }
        }
    }while(1);
    
}

void TimestampTimer_Init(const tStreamingConfig* config, StreamingRuntimeConfig* runtimeConfig)
{
    // Initialize and start timestamp timer
    // This is a free running timer used for reference - this doesn't interrupt or callback
    runtimeConfig->TSTimerHandle = DRV_TMR_Open(config->TSTimerIndex, config->TSTimerIntent);
    if( runtimeConfig->TSTimerHandle == DRV_HANDLE_INVALID )
    {
        // Client cannot open the instance.
         SYS_DEBUG_BreakPoint();
    }
    DRV_TMR_AlarmRegister(runtimeConfig->TSTimerHandle,
            runtimeConfig->TSClockDivider,
            true,
            0,
            NULL);
    DRV_TMR_AlarmDisable(runtimeConfig->TSTimerHandle);
    DRV_TMR_Start(runtimeConfig->TSTimerHandle);
}

void Streaming_StuffDummyData (void)
{
    // Stuff stream with some data
    // Copy dummy samples to the data list
    uint32_t i = 0;
    static AInSample data;
    
    AInSampleList * pAInSamples = BoardData_Get(                            \
                            BOARDDATA_AIN_SAMPLES,                          \
                            0); 
    
    data.Value = 'O';
    data.Timestamp ++;
    if (data.Timestamp == 0) data.Timestamp++;  // Skip zero so as not to allow multiple duplicate timestamps (uninitialized channels will have 0 timestamp)

    for (i=0; i<16; ++i)
    {
        data.Channel = i;
        AInSampleList_PushBack(pAInSamples, (const AInSample *)&data); 
    }
}