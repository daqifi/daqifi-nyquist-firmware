#include "streaming.h"

#include "HAL/ADC.h"
#include "HAL/DIO.h"
#include "json/JSON_Encoder.h"
#include "nanopb/DaqifiOutMessage.pb.h"
#include "nanopb/Nanopb_Encoder.h"
#include "Util/Logger.h"
#include "TcpServer/TcpServer.h"

#define UNUSED(x) (void)(x)
    
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

void Streaming_Init(const StreamingConfig* config, StreamingRuntimeConfig* runtimeConfig)
{
    // Initialize sample trigger timer
    runtimeConfig->TimerHandle = DRV_TMR_Open(config->TimerIndex, config->TimerIntent);
    if( runtimeConfig->TimerHandle == DRV_HANDLE_INVALID )
    {
        // Client cannot open the instance.
         SYS_DEBUG_BreakPoint();
    }
    
    runtimeConfig->Running = false;
}

void Streaming_Start(const StreamingConfig* config, StreamingRuntimeConfig* runtimeConfig)
{
    UNUSED(config);
    
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

void Streaming_Stop(const StreamingConfig* config, StreamingRuntimeConfig* runtimeConfig)
{
    UNUSED(config);
    if (runtimeConfig->Running)
    {
        DRV_TMR_Stop(runtimeConfig->TimerHandle);
        DRV_TMR_AlarmDeregister(runtimeConfig->TimerHandle);
        runtimeConfig->Running = false;
    }
    DRV_TMR_CounterValue32BitSet(runtimeConfig->TimerHandle, 0);
}

void Streaming_UpdateState(const BoardConfig* boardConfig, BoardRuntimeConfig* runtimeConfig)
{
    Streaming_Stop(&boardConfig->StreamingConfig, &runtimeConfig->StreamingConfig);
    
    // TODO: Calculate an appropriate runtimeConfig->ClockDivider and Prescale value for each module/channel
    //  - ClockDivider = Least Common Denominator of all the desired frequencies
    //  - Prescale = The integral multiple of ClockDivider closest to the desired frequency
    //  - The 'System' prescale should be selected to be approximately 1s (if possible), and should probably be excluded from the LCD calculation
    runtimeConfig->StreamingConfig.StreamCountTrigger = 0;
    runtimeConfig->StreamingConfig.MaxStreamCount = runtimeConfig->StreamingConfig.StreamCountTrigger + 1;
    
    // We never actually disable the streaming time because the system functions (battery level, voltages, actually depend on it)
    Streaming_Start(&boardConfig->StreamingConfig, &runtimeConfig->StreamingConfig);
}
    
void Streaming_Tasks(const BoardConfig* boardConfig, BoardRuntimeConfig* runtimeConfig, BoardData* boardData)
{
    if (!runtimeConfig->StreamingConfig.IsEnabled)
    {
        return;
    }
    
    bool AINDataAvailable=!AInSampleList_IsEmpty(&boardData->AInSamples);
    bool DIODataAvailable=!DIOSampleList_IsEmpty(&boardData->DIOSamples);
    
    #if(DAQIFI_DIO_DEBUG == 1)
    {
        // For diagnostic purposes, setup DIO pin 1
        //TODO: DAQiFi For diagnostic purposes, setup DIO pin 1
        g_BoardRuntimeConfig.DIOChannels.Data[1].IsInput = false;
        g_BoardRuntimeConfig.DIOChannels.Data[1].IsReadOnly = false;
        g_BoardRuntimeConfig.DIOChannels.Data[1].Value = !g_BoardRuntimeConfig.DIOChannels.Data[1].Value;
    }
    #endif
    
    // Decide what to write
    NanopbFlagsArray flags;
    
    flags.Size=0;   // Initialize array size to zero
    
    // See if we have anything to write at all, if so, proceed with creating message header, etc.
    if(AINDataAvailable || DIODataAvailable){
        flags.Data[0] = DaqifiOutMessage_msg_time_stamp_tag;
//        flags.Data[1] = DaqifiOutMessage_device_status_tag;
//        flags.Data[2] = DaqifiOutMessage_bat_level_tag;
//        flags.Data[3] = DaqifiOutMessage_pwr_status_tag;
//        flags.Data[4] = DaqifiOutMessage_temp_status_tag;
        flags.Size = 1;
    }else
    {
        return;
    }
    
    // Decide how many samples we can send out
    size_t usbSize = 0;
    bool hasUsb = false;
    if (runtimeConfig->usbSettings.state == USB_CDC_STATE_PROCESS)
    {
        usbSize = min(STREAMING_BUFFER_SIZE, USB_BUFFER_SIZE - runtimeConfig->usbSettings.writeBufferLength);
        hasUsb = true;
    }

    size_t i=0;
    size_t wifiSize = STREAMING_BUFFER_SIZE;
    bool hasWifi = false;
    if (runtimeConfig->serverData.state == IP_SERVER_PROCESS)
    {
        for (i=0; i<WIFI_MAX_CLIENT; ++i)
        {
            if (runtimeConfig->serverData.clients[i].client != INVALID_SOCKET)
            {
                wifiSize = min(wifiSize, WIFI_BUFFER_SIZE - runtimeConfig->serverData.clients[i].writeBufferLength);
                hasWifi = true;
            }
        }
    }
    
    size_t maxSize = 0;
    if (hasUsb && hasWifi)
    {
        maxSize = min(usbSize, wifiSize);
    }
    else if (hasUsb)
    {
        maxSize = usbSize;
    }
    else
    {
        maxSize = wifiSize;
    }
    
    if (maxSize < 128)
    {
        return;
    }
    
    while(maxSize > 0 && (AINDataAvailable || DIODataAvailable))
    {
        AINDataAvailable=!AInSampleList_IsEmpty(&boardData->AInSamples);
        DIODataAvailable=!DIOSampleList_IsEmpty(&boardData->DIOSamples);

        if (AINDataAvailable)
        {
            flags.Data[flags.Size] = DaqifiOutMessage_analog_in_data_tag;
            //flags.Data[flags.Size + 1] = DaqifiOutMessage_analog_in_data_b_tag;   
            //flags.Data[flags.Size + 2] = DaqifiOutMessage_analog_in_port_rse_tag;
            //flags.Data[flags.Size + 3] = DaqifiOutMessage_analog_in_port_enabled_tag;
            //flags.Data[flags.Size + 4] = DaqifiOutMessage_analog_in_port_range_tag;
            //flags.Data[flags.Size + 5] = DaqifiOutMessage_analog_in_res_tag;
            flags.Size += 1;
        }

        if (DIODataAvailable)
        {
            flags.Data[flags.Size] = DaqifiOutMessage_digital_data_tag;
            flags.Data[flags.Size + 1] = DaqifiOutMessage_digital_port_dir_tag;
            flags.Size += 2;
        }



        // Generate a packet
        // TODO: ASCII Encoder
        if(flags.Size>0){
            size_t size = 0;
            uint8_t *pBuffer;
            if (runtimeConfig->StreamingConfig.Encoding == Streaming_Json)
            {
                size = Json_Encode(boardData, &flags, &pBuffer );
            }
            else
            {
                //size = Nanopb_Encode(boardData, &flags, &pBuffer );
            }

            // Write the packet out
            if (size > 0)
            {
                // Toggle DIO pin for diagnostic use
                DIO_WriteStateSingle( 1 );
                
                if (hasUsb)
                {
                    memcpy(runtimeConfig->usbSettings.writeBuffer + runtimeConfig->usbSettings.writeBufferLength, pBuffer, size);
                    runtimeConfig->usbSettings.writeBufferLength += size;
                }

                if (hasWifi)
                {
                    if( TCP_Server_Is_Blocked() == 0 ){
                        for (i=0; i<WIFI_MAX_CLIENT; ++i)
                        {
                            if (runtimeConfig->serverData.clients[i].client != INVALID_SOCKET)
                            {
                                memcpy(runtimeConfig->serverData.clients[i].writeBuffer + runtimeConfig->serverData.clients[i].writeBufferLength, pBuffer, size);
                                runtimeConfig->serverData.clients[i].writeBufferLength += size;
                            }
                        }
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
    }
}

void TimestampTimer_Init(const StreamingConfig* config, StreamingRuntimeConfig* runtimeConfig)
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
    data.Value = 'O';
    data.Timestamp ++;
    if (data.Timestamp == 0) data.Timestamp++;  // Skip zero so as not to allow multiple duplicate timestamps (uninitialized channels will have 0 timestamp)

    for (i=0; i<16; ++i)
    {
        data.Channel = i;
        AInSampleList_PushBack(&g_BoardData.AInSamples, &data); 
    }
}