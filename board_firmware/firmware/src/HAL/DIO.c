#include "DIO.h"

#include "system_config.h"
#include "system_definitions.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "state/board/BoardConfig.h"
#include "commTest.h"

bool DIO_InitHardware(const DIOArray* boardConfig)
{
    // Initial condition should be handled by the pin manager
    // We can be sure by running the code here
    int i=0;
    for (i=0; i<boardConfig->Size; ++i)
    {
        // Disable all channels by default        
        if (boardConfig->Data[i].EnableInverted)
        {
            PLIB_PORTS_PinSet(boardConfig->Data[i].EnableModule, boardConfig->Data[i].EnableChannel , boardConfig->Data[i].EnablePin);
        }
        else
        {
            PLIB_PORTS_PinClear(boardConfig->Data[i].EnableModule, boardConfig->Data[i].EnableChannel , boardConfig->Data[i].EnablePin);
        }
        
        PLIB_PORTS_PinDirectionOutputSet(boardConfig->Data[i].EnableModule, boardConfig->Data[i].EnableChannel, boardConfig->Data[i].EnablePin);
    }
    return true;
}

bool DIO_WriteStateAll(const DIOArray* boardConfig, DIORuntimeArray* runtimeConfig)
{
    size_t i;
    bool result = true;
    for (i=0; i<runtimeConfig->Size; ++i)
    {
        result &= DIO_WriteStateSingle(&boardConfig->Data[i], &runtimeConfig->Data[i]);
    }
    
    return result;
}

bool DIO_WriteStateSingle(const DIOConfig* boardConfig, DIORuntimeConfig* runtimeConfig)
{

    if (runtimeConfig->IsInput)
    {
        // Set driver disabled - this value will be the value of EnableInverted config parameter
        PLIB_PORTS_PinWrite(boardConfig->EnableModule, boardConfig->EnableChannel , boardConfig->EnablePin, boardConfig->EnableInverted);
        // Set data pin direction as input
        PLIB_PORTS_PinDirectionInputSet(boardConfig->DataModule, boardConfig->DataChannel , boardConfig->DataPin);
    }
    else
    {
        // Set driver enabled - this value will be the inverse of EnableInverted config parameter
        PLIB_PORTS_PinWrite(boardConfig->EnableModule, boardConfig->EnableChannel , boardConfig->EnablePin, !boardConfig->EnableInverted);
        // Set driver value
        PLIB_PORTS_PinWrite(boardConfig->DataModule, boardConfig->DataChannel, boardConfig->DataPin, runtimeConfig->Value);
        // Set data pin direction as output
        PLIB_PORTS_PinDirectionOutputSet(boardConfig->DataModule, boardConfig->DataChannel , boardConfig->DataPin);
    }
    
    return true;
}

bool DIO_ReadSampleByMask(DIOSample* sample, const DIOArray* boardConfig, DIORuntimeArray* runtimeConfig, uint32_t mask)
{
    StreamingRuntimeConfig * pRuntimeStreamConfig = BoardRunTimeConfig_Get( \
                        BOARDRUNTIME_STREAMING_CONFIGURATION);
    sample->Mask = mask;
    sample->Values = 0;
    // Set module trigger timestamp
    sample->Timestamp = DRV_TMR_CounterValueGet(                            \
                        pRuntimeStreamConfig->TSTimerHandle);
    
    size_t i=0;
    for (i=0; i<runtimeConfig->Size; ++i)
    {
        const DIOConfig* channelBoardConfig = &boardConfig->Data[i];
        bool isMatch = (mask & (1 << i));
        if (isMatch)
        {
            uint8_t val = PLIB_PORTS_PinGet(channelBoardConfig->DataModule, channelBoardConfig->DataChannel , channelBoardConfig->DataPin) ? 1 : 0;
            sample->Values |= (val << i);
        }
    }
    
    return true;
}

void DIO_Tasks(const DIOArray* boardConfig, tBoardRuntimeConfig* runtimeConfig, DIOSample* latest, DIOSampleList* streamingSamples)
{
//    // For debugging streaming frequency only!
//    runtimeConfig->Data[0].Value = !runtimeConfig->Data[0].Value;
    
    DIORuntimeArray* DIOChruntimeConfig;
    DIOChruntimeConfig = &runtimeConfig->DIOChannels;
    
    size_t i = 0;
    
    // Write DIO values
    for (i=0; i<DIOChruntimeConfig->Size; ++i)
    {
        const DIOConfig* channelConfig = &boardConfig->Data[i];
        DIORuntimeConfig* channelRuntimeConfig = &DIOChruntimeConfig->Data[i];
        DIO_WriteStateSingle(channelConfig, channelRuntimeConfig);
    }
    
    // Read DIO values
    if (DIO_ReadSampleByMask(latest, boardConfig, DIOChruntimeConfig, 0xFFFF))
    {
        // If streaming and the DIO is globally enabled, push the values onto the list
        if (runtimeConfig->DIOGlobalEnable && runtimeConfig->StreamingConfig.IsEnabled)
        {
            DIOSample streamingSample;
            streamingSample.Mask = 0xFFFF;
            streamingSample.Values = latest->Values;
            streamingSample.Timestamp = latest->Timestamp;
            if(!DIOSampleList_PushBack(                                     \
                        streamingSamples,                                   \
                        (const DIOSample* )&streamingSample))
            {
                commTest.DIOSampleListOverflow++;
            }
        }
    }
    
}
