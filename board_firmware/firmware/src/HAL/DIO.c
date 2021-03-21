#include "DIO.h"

#include "system_config.h"
#include "system_definitions.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "state/board/BoardConfig.h"

//! Pointer to the board configuration. It must be set in the initialization
static BoardConfig *pBoardConfig;
//! Pointer to the runtime board configuration. It must be set in the initialization
static BoardRuntimeConfig *pRuntimeBoardConfig;

bool DIO_InitHardware( const BoardConfig *pInitBoardConfiguration, \
                       const BoardRuntimeConfig *pInitBoardRuntimeConfig )
{
    bool enableInverted;
    PORTS_MODULE_ID enableModule;
    PORTS_CHANNEL enableChannel;
    PORTS_BIT_POS enablePin;
    size_t channelsSize;
    
    pBoardConfig = (BoardConfig *)pInitBoardConfiguration;
    pRuntimeBoardConfig = (BoardRuntimeConfig *)pInitBoardRuntimeConfig;
    channelsSize = pBoardConfig->DIOChannels.Size;
    
    
    // Initial condition should be handled by the pin manager
    // We can be sure by running the code here
    int i=0;
    for( i = 0; i < channelsSize; ++i )
    {
        enableInverted = pBoardConfig->DIOChannels.Data[ i ].EnableInverted;
        enableModule = pBoardConfig->DIOChannels.Data[ i ].EnableModule;
        enableChannel = pBoardConfig->DIOChannels.Data[ i ]. EnableChannel;
        enablePin = pBoardConfig->DIOChannels.Data[ i ].EnablePin;
        
        // Disable all channels by default        
        if( enableInverted ){
            PLIB_PORTS_PinSet( enableModule, enableChannel , enablePin );
        }
        else{
            PLIB_PORTS_PinClear( enableModule, enableChannel , enablePin );
        }
        
        PLIB_PORTS_PinDirectionOutputSet( enableModule, enableChannel, enablePin );
    }
    return true;
}

bool DIO_WriteStateAll( void )
{
    uint8_t dataIndex;
    bool result = true;
    
    for( dataIndex = 0; \
         dataIndex < pRuntimeBoardConfig->DIOChannels.Size; \
         ++dataIndex ){
                result &= DIO_WriteStateSingle( dataIndex );
    }
    
    return result;
}

bool DIO_WriteStateSingle( uint8_t dataIndex ){
    bool enableInverted = \
            pBoardConfig->DIOChannels.Data[ dataIndex ].EnableInverted;
    PORTS_MODULE_ID enableModule = \
            pBoardConfig->DIOChannels.Data[ dataIndex ].EnableModule;
    PORTS_CHANNEL enableChannel = \
            pBoardConfig->DIOChannels.Data[ dataIndex ]. EnableChannel;
    PORTS_BIT_POS enablePin = \
            pBoardConfig->DIOChannels.Data[ dataIndex ].EnablePin;

    PORTS_MODULE_ID dataModule = \
            pBoardConfig->DIOChannels.Data[ dataIndex ].DataModule;
    PORTS_CHANNEL dataChannel = \
            pBoardConfig->DIOChannels.Data[ dataIndex ].DataChannel;
    PORTS_BIT_POS dataPin = \
            pBoardConfig->DIOChannels.Data[ dataIndex ].DataPin;
    
    _Bool value = pRuntimeBoardConfig->DIOChannels.Data[ dataIndex ].Value;
    if( pRuntimeBoardConfig->DIOChannels.Data[ dataIndex ].IsInput ){
        // Set driver disabled - this value will be the value of EnableInverted config parameter
        PLIB_PORTS_PinWrite( \
                        enableModule, \
                        enableChannel , \
                        enablePin, \
                        enableInverted );
        // Set data pin direction as input
        PLIB_PORTS_PinDirectionInputSet( dataModule, dataChannel , dataPin );
    }
    else
    {
        // Set driver enabled - this value will be the inverse of EnableInverted config parameter
        PLIB_PORTS_PinWrite( \
                        enableModule, \
                        enableChannel , \
                        enablePin, \
                        !enableInverted );
        // Set driver value
        PLIB_PORTS_PinWrite( \
                        dataModule, \
                        dataChannel, \
                        dataPin, \
                        value );
        // Set data pin direction as output
        PLIB_PORTS_PinDirectionOutputSet( dataModule, dataChannel , dataPin );
    }
    
    return true;
}

bool DIO_ReadSampleByMask(DIOSample* sample, uint32_t mask)
{
    sample->Mask = mask;
    sample->Values = 0;
    // Set module trigger timestamp
    sample->Timestamp = \
    DRV_TMR_CounterValueGet(pRuntimeBoardConfig->StreamingConfig.TSTimerHandle);
    
    size_t dataIndex = 0;
    for( \
            dataIndex = 0; \
            dataIndex < pRuntimeBoardConfig->DIOChannels.Size; \
            ++dataIndex )
    {
        PORTS_MODULE_ID dataModule = \
            pBoardConfig->DIOChannels.Data[ dataIndex ].DataModule;
        PORTS_CHANNEL dataChannel = \
                pBoardConfig->DIOChannels.Data[ dataIndex ].DataChannel;
        PORTS_BIT_POS dataPin = \
                pBoardConfig->DIOChannels.Data[ dataIndex ].DataPin;
        //const DIOConfig* channelBoardConfig = &boardConfig->Data[ dataIndex ];
        bool isMatch = ( mask & ( 1 << dataIndex ) );
        if( isMatch )
        {
            uint8_t val;
            if( PLIB_PORTS_PinGet( dataModule, dataChannel , dataPin ) ){
                val = 1;
            }
            else{
                val = 0;
            }
            sample->Values |= ( val << dataIndex );
        }
    }
    
    return true;
}

void DIO_Tasks( DIOSample* latest, DIOSampleList* streamingSamples )
{
//    // For debugging streaming frequency only!
//    runtimeConfig->Data[0].Value = !runtimeConfig->Data[0].Value;
    
    size_t i = 0;
    
    // Write DIO values
    for( i = 0;\
         i < pRuntimeBoardConfig->DIOChannels.Size;\
         ++i ){
                DIO_WriteStateSingle( i );
    }
    
    // Read DIO values
    if( DIO_ReadSampleByMask( latest, 0xFFFF ) )
    {
        // If streaming and the DIO is globally enabled, push the values onto the list
        if( pRuntimeBoardConfig->DIOGlobalEnable && \
            pRuntimeBoardConfig->StreamingConfig.IsEnabled )
        {
            DIOSample streamingSample;
            streamingSample.Mask = 0xFFFF;
            streamingSample.Values = latest->Values;
            streamingSample.Timestamp = latest->Timestamp;
            DIOSampleList_PushBack( streamingSamples, &streamingSample );
        }
    }
}
