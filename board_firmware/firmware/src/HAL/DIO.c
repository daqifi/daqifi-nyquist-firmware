/*! @file DIO.c 
 * 
 * This file implements the functions to manage the digital input/output
 */

#include "DIO.h"

#include "system_config.h"
#include "system_definitions.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "state/board/BoardConfig.h"
#include "commTest.h"

//! Pointer to the board configuration. It must be set in the initialization
static tBoardConfig *pBoardConfigDIO;
//! Pointer to the runtime board configuration. It must be set in the initialization
static tBoardRuntimeConfig *pRuntimeBoardConfigDIO;

bool DIO_InitHardware( const tBoardConfig *pInitBoardConfiguration,         \
                        const tBoardRuntimeConfig *pInitBoardRuntimeConfig )
{
    bool enableInverted;
    PORTS_MODULE_ID enableModule;
    PORTS_CHANNEL enableChannel;
    PORTS_BIT_POS enablePin;
    size_t channelsSize;    
    uint8_t i=0;
    
    pBoardConfigDIO = (tBoardConfig *)pInitBoardConfiguration;
    pRuntimeBoardConfigDIO = (tBoardRuntimeConfig *)pInitBoardRuntimeConfig;
    channelsSize = pBoardConfigDIO->DIOChannels.Size;
    
    // Initial condition should be handled by the pin manager
    // We can be sure by running the code here

    for (i=0; i<channelsSize; ++i)
    {
        enableInverted = pBoardConfigDIO->DIOChannels.Data[ i ].EnableInverted;
        enableModule = pBoardConfigDIO->DIOChannels.Data[ i ].EnableModule;
        enableChannel = pBoardConfigDIO->DIOChannels.Data[ i ]. EnableChannel;
        enablePin = pBoardConfigDIO->DIOChannels.Data[ i ].EnablePin;
        
        // Disable all channels by default        
        if( enableInverted ){
            PLIB_PORTS_PinSet( enableModule, enableChannel , enablePin );
        }
        else{
            PLIB_PORTS_PinClear( enableModule, enableChannel , enablePin );
        }
        
        PLIB_PORTS_PinDirectionOutputSet(                                   \
                        enableModule,                                       \
                        enableChannel,                                      \
                        enablePin );
    }
    return true;
}

bool DIO_WriteStateAll( void )
{
    size_t dataIndex;
    bool result = true;
    for (               dataIndex = 0;                                      \
                        dataIndex<pRuntimeBoardConfigDIO->DIOChannels.Size; \
                        ++dataIndex)
    {
        result &= DIO_WriteStateSingle(dataIndex );
    }
    
    return result;
}

bool DIO_WriteStateSingle( uint8_t dataIndex )
{
    bool enableInverted =                                                   \
            pBoardConfigDIO->DIOChannels.Data[ dataIndex ].EnableInverted;
    PORTS_MODULE_ID enableModule =                                          \
            pBoardConfigDIO->DIOChannels.Data[ dataIndex ].EnableModule;
    PORTS_CHANNEL enableChannel =                                           \
            pBoardConfigDIO->DIOChannels.Data[ dataIndex ]. EnableChannel;
    PORTS_BIT_POS enablePin =                                               \
            pBoardConfigDIO->DIOChannels.Data[ dataIndex ].EnablePin;

    PORTS_MODULE_ID dataModule =                                            \
            pBoardConfigDIO->DIOChannels.Data[ dataIndex ].DataModule;
    PORTS_CHANNEL dataChannel =                                             \
            pBoardConfigDIO->DIOChannels.Data[ dataIndex ].DataChannel;
    PORTS_BIT_POS dataPin =                                                 \
            pBoardConfigDIO->DIOChannels.Data[ dataIndex ].DataPin;
    
    _Bool value =                                                           \
                pRuntimeBoardConfigDIO->DIOChannels.Data[ dataIndex ].Value;
    bool isPwmRunning=pRuntimeBoardConfigDIO->DIOChannels.Data[ dataIndex ].IsPwmActive;
    if(isPwmRunning){
        return 1;
    }
    if( pRuntimeBoardConfigDIO->DIOChannels.Data[ dataIndex ].IsInput )
    {
        // Set driver disabled - this value will be the value of 
        // EnableInverted config parameter
        PLIB_PORTS_PinWrite(                                                \
                        enableModule,                                       \
                        enableChannel ,                                     \
                        enablePin,                                          \
                        enableInverted );
        // Set data pin direction as input
        PLIB_PORTS_PinDirectionInputSet( dataModule, dataChannel , dataPin );
    }
    else
    {
        // Set driver enabled - this value will be the inverse of 
        // EnableInverted config parameter
        PLIB_PORTS_PinWrite(                                                \
                        enableModule,                                       \
                        enableChannel,                                      \
                        enablePin,                                          \
                        !enableInverted );
        // Set driver value
        PLIB_PORTS_PinWrite(                                                \
                        dataModule,                                         \
                        dataChannel,                                        \
                        dataPin,                                            \
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
    sample->Timestamp =                                                     \
    DRV_TMR_CounterValueGet(                                                \
                    pRuntimeBoardConfigDIO->StreamingConfig.TSTimerHandle);
    
    size_t dataIndex = 0;
    for(                                                                    \
                        dataIndex = 0;                                      \
                        dataIndex < pRuntimeBoardConfigDIO->DIOChannels.Size;\
                        ++dataIndex )
    {
        PORTS_MODULE_ID dataModule =                                        \
            pBoardConfigDIO->DIOChannels.Data[ dataIndex ].DataModule;
        PORTS_CHANNEL dataChannel =                                         \
                pBoardConfigDIO->DIOChannels.Data[ dataIndex ].DataChannel;
        PORTS_BIT_POS dataPin =                                             \
                pBoardConfigDIO->DIOChannels.Data[ dataIndex ].DataPin;
        
        if( mask & ( 1 << dataIndex ) )
        {
            uint8_t val;
            if( PLIB_PORTS_PinGet(dataModule, dataChannel , dataPin ))
            {
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

bool DIO_PWMWriteStateSingle( uint8_t dataIndex )
{
    bool enableInverted = pBoardConfigDIO->DIOChannels.Data[ dataIndex ].EnableInverted;
    PORTS_MODULE_ID enableModule = pBoardConfigDIO->DIOChannels.Data[ dataIndex ].EnableModule;
    PORTS_CHANNEL enableChannel = pBoardConfigDIO->DIOChannels.Data[ dataIndex ]. EnableChannel;
    PORTS_BIT_POS enablePin = pBoardConfigDIO->DIOChannels.Data[ dataIndex ].EnablePin;
    PORTS_MODULE_ID dataModule = pBoardConfigDIO->DIOChannels.Data[ dataIndex ].DataModule;
    bool isPwmSupported= pBoardConfigDIO->DIOChannels.Data[ dataIndex ].IsPwmCapable;
    SYS_MODULE_INDEX pwmDriverInstance=pBoardConfigDIO->DIOChannels.Data[ dataIndex ].PwmDrvIndex;
    PORTS_REMAP_OUTPUT_PIN pwmPPSPinNo=pBoardConfigDIO->DIOChannels.Data[ dataIndex ].pwmRemapPin;
    PORTS_REMAP_OUTPUT_FUNCTION pwmPPSFunction=pBoardConfigDIO->DIOChannels.Data[ dataIndex ].pwmRemapFuction;
    bool pwmState=pRuntimeBoardConfigDIO->DIOChannels.Data[ dataIndex ].IsPwmActive;
    if(isPwmSupported!=1){
        return false;
    }
    if(pwmState){
        SYS_PORTS_RemapOutput(dataModule,pwmPPSFunction,pwmPPSPinNo);
        DRV_OC_Start(pwmDriverInstance,DRV_IO_INTENT_WRITE);
        PLIB_PORTS_PinWrite(enableModule,enableChannel,enablePin,!enableInverted );
    }else{
        SYS_PORTS_RemapOutput(dataModule,OUTPUT_FUNC_NO_CONNECT,pwmPPSPinNo);
        DRV_OC_Stop(pwmDriverInstance);
        PLIB_PORTS_PinWrite(enableModule,enableChannel,enablePin,enableInverted );
    }
    return true;
}
bool DIO_PWMDutyCycleSetSingle( uint8_t dataIndex ){
     SYS_MODULE_INDEX pwmDriverInstance=pBoardConfigDIO->DIOChannels.Data[ dataIndex ].PwmDrvIndex;
     uint32_t timerClock=SYS_CLK_PeripheralFrequencyGet(CLK_BUS_PERIPHERAL_3)/PLIB_TMR_PrescaleGet(TMR_ID_3);
     uint16_t pwmDutyCycle=pRuntimeBoardConfigDIO->DIOChannels.Data[ dataIndex ].PwmDutyCycle;
     uint32_t pwmFrequency=pRuntimeBoardConfigDIO->DIOChannels.Data[ dataIndex ].PwmFrequency;
     uint16_t period=(timerClock/pwmFrequency)*(pwmDutyCycle/100.00);
     DRV_OC_PulseWidthSet(pwmDriverInstance,period);
     return true;
}

bool DIO_PWMFrequencySet(uint8_t dataIndex){
    
    const uint16_t tim3PreScalers[8]={1,2,4,8,16,32,64,256};
    uint32_t timerClock=SYS_CLK_PeripheralFrequencyGet(CLK_BUS_PERIPHERAL_3);
    uint32_t pwmFrequency=pRuntimeBoardConfigDIO->DIOChannels.Data[ dataIndex ].PwmFrequency;
    uint32_t timer3ScaledClock=timerClock;
    uint64_t temp;
    uint8_t preScalerIndex=(sizeof(tim3PreScalers)/sizeof(tim3PreScalers[0]))-1;
    uint16_t period=100;
    temp=period*pwmFrequency; //100 is kept as the ideal minimum period value
    for(preScalerIndex;preScalerIndex>0;preScalerIndex--){
        timer3ScaledClock=timerClock/tim3PreScalers[preScalerIndex];
        if(timer3ScaledClock>temp){
            break;
        }
    }
    timer3ScaledClock=timerClock/tim3PreScalers[preScalerIndex];
    period=timer3ScaledClock/pwmFrequency;
    PLIB_TMR_Stop(TMR_ID_3);
    PLIB_TMR_PrescaleSelect(TMR_ID_3,preScalerIndex);
    PLIB_TMR_Period16BitSet(TMR_ID_3, period);  
    PLIB_TMR_Start(TMR_ID_3);

    return true;
}

void DIO_Tasks( DIOSample* latest, DIOSampleList* streamingSamples)
{
//    // For debugging streaming frequency only!
//    runtimeConfig->Data[0].Value = !runtimeConfig->Data[0].Value;
    
    DIORuntimeArray* DIOChruntimeConfig;
    DIOChruntimeConfig = &pRuntimeBoardConfigDIO->DIOChannels;
    
    size_t i = 0;
    
    // Write DIO values
    for (i=0; i<DIOChruntimeConfig->Size; ++i)
    {
        DIO_WriteStateSingle( i );
    }
    
    // Read DIO values
    if (DIO_ReadSampleByMask(latest, 0xFFFF))
    {
        // If streaming and the DIO is globally enabled, push the values onto the list
        if (            pRuntimeBoardConfigDIO->DIOGlobalEnable &&          \
                        pRuntimeBoardConfigDIO->StreamingConfig.IsEnabled)
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
