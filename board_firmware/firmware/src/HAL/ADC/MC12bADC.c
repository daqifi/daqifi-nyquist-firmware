/*! @file MC12bADC.c 
 * 
 * This file implements the functions to manage the module ADC MC12bADC. 
 */

#include "MC12bADC.h"

#include <peripheral/peripheral.h>
#include "system_definitions.h"
#include "system_config.h"
#include "framework/driver/adc/drv_adc_static.h"
#include "Util/Delay.h"

//#define UNUSED(x) (void)(x)
#define UNUSED(identifier) /* identifier */

//! Pointer to the module configuration data structure to be set in initialization
static MC12bModuleConfig* pModuleConfigMC12; 
//! Pointer to the module configuration data structure in runtime
//! to be set in initialization
static AInModuleRuntimeConfig* pModuleRuntimeConfigMC12; 
//! Boolean to indicate if this module is enabled
static bool isEnabled = false;

bool MC12b_InitHardware(const MC12bModuleConfig* pModuleConfigInit,         \
                    const AInModuleRuntimeConfig * pModuleRuntimeConfigInit)
{
    pModuleConfigMC12 = pModuleConfigInit;
    pModuleRuntimeConfigMC12 = pModuleRuntimeConfigInit;
    
    // Copy factory calibration data to calibration registers
    ADC0CFG = DEVADC0;
    ADC1CFG = DEVADC1;
    ADC2CFG = DEVADC2;
    ADC3CFG = DEVADC3;
    ADC4CFG = DEVADC4;
    ADC7CFG = DEVADC7;
    return true;
}

bool MC12b_WriteModuleState( void )
{
    
    if (pModuleRuntimeConfigMC12->IsEnabled == isEnabled)
    {
        return false;
    }
    
    if (pModuleRuntimeConfigMC12->IsEnabled)
    {
        /* Enable clock to analog circuit */
        ADCANCONbits.ANEN0 = 1; // Enable the clock to analog bias
        ADCANCONbits.ANEN1 = 1; // Enable the clock to analog bias
        ADCANCONbits.ANEN2 = 1; // Enable the clock to analog bias
        ADCANCONbits.ANEN3 = 1; // Enable the clock to analog bias
        ADCANCONbits.ANEN4 = 1; // Enable the clock to analog bias
        ADCANCONbits.ANEN7 = 1; // Enable the clock to analog bias

        while(!ADCANCONbits.WKRDY0); // Wait until ADC0 is ready
        while(!ADCANCONbits.WKRDY1); // Wait until ADC1 is ready
        while(!ADCANCONbits.WKRDY2); // Wait until ADC2 is ready
        while(!ADCANCONbits.WKRDY3); // Wait until ADC3 is ready
        while(!ADCANCONbits.WKRDY4); // Wait until ADC4 is ready
        while(!ADCANCONbits.WKRDY7); // Wait until ADC7 is ready

        DRV_ADC0_Open();
        DRV_ADC1_Open();
        DRV_ADC2_Open();
        DRV_ADC3_Open();
        DRV_ADC4_Open();
        DRV_ADC5_Open();
    
        //Enable module
        PLIB_ADCHS_Enable(pModuleConfigMC12->moduleId);   
        
        isEnabled = true;
    }
    else
    {
        //Disable module
        PLIB_ADCHS_Disable(pModuleConfigMC12->moduleId);   
        
        DRV_ADC0_Close();
        DRV_ADC1_Close();
        DRV_ADC2_Close();
        DRV_ADC3_Close();
        DRV_ADC4_Close();
        DRV_ADC5_Close();
        
        /* Enable clock to analog circuit */
        ADCANCONbits.ANEN0 = 1; // Enable the clock to analog bias
        ADCANCONbits.ANEN1 = 1; // Enable the clock to analog bias
        ADCANCONbits.ANEN2 = 1; // Enable the clock to analog bias
        ADCANCONbits.ANEN3 = 1; // Enable the clock to analog bias
        ADCANCONbits.ANEN4 = 1; // Enable the clock to analog bias
        ADCANCONbits.ANEN7 = 1; // Enable the clock to analog bias
        
        isEnabled = false;
    }
    
    return true;
}

bool MC12b_WriteStateAll(                                                   \
                        const AInArray* channelConfig,                      \
                        AInRuntimeArray* channelRuntimeConfig)
{
    bool isEnabled = pModuleRuntimeConfigMC12->IsEnabled;
    if (isEnabled)
    {
        PLIB_ADCHS_Disable(pModuleConfigMC12->moduleId);
        pModuleRuntimeConfigMC12->IsEnabled = false;
    }
    
    size_t i=0;
    bool result = true;
    for (i=0; i<channelConfig->Size; ++i)
    {
        result &= MC12b_WriteStateSingle(                                   \
                        &(channelConfig->Data[i].Config.MC12b),             \
                        &channelRuntimeConfig->Data[i]);
    }
    
    if (isEnabled)
    {
        PLIB_ADCHS_Enable(pModuleConfigMC12->moduleId);
        pModuleRuntimeConfigMC12->IsEnabled = isEnabled;
    }
    
    return result;
}

bool MC12b_WriteStateSingle(                                                \
                        const MC12bChannelConfig* channelConfig,            \
                        AInRuntimeConfig* channelRuntimeConfig)
{
    if (pModuleRuntimeConfigMC12->IsEnabled)
    {
        PLIB_ADCHS_Disable(pModuleConfigMC12->moduleId);
    }
    
    if (channelConfig->ChannelType == 1)
    {
        ADCHS_AN_INPUT_ID channelId = ADCHS_CHANNEL_0 +                     \
                        channelConfig->BufferIndex;
        if (channelRuntimeConfig->IsEnabled)
        {
            PLIB_ADCHS_ChannelDigitalFeatureEnable(                         \
                        pModuleConfigMC12->moduleId,                        \
                        channelId);
        }
        else
        {
            PLIB_ADCHS_ChannelDigitalFeatureDisable(                        \
                        pModuleConfigMC12->moduleId,                        \
                        channelId);
        }
        
        
        if (channelConfig->AllowDifferential &&                             \
                        channelRuntimeConfig->IsDifferential)
        {
            PLIB_ADCHS_AnalogInputModeSelect(                               \
                        pModuleConfigMC12->moduleId,                        \
                        channelId,                                          \
                        ADCHS_INPUT_MODE_DIFFERENTIAL_TWOS_COMP);
        }
        else
        {
            PLIB_ADCHS_AnalogInputModeSelect(                               \
                        pModuleConfigMC12->moduleId,                        \
                        channelId,                                          \
                        ADCHS_INPUT_MODE_SINGLE_ENDED_UNIPOLAR);
        }
        
        // TODO: Calculate channelClockDivider (5) and sampleTimeCount (8) 
        //from channelRuntimeConfig->Frequency
        // PLIB_ADCHS_ChannelSetup(moduleConfig->moduleId, channelId, 
        //ADCHS_DATA_RESOLUTION_12BIT, 5, 8, 0);
    }
    
    // TODO: What about channel 2-3
    
    if (pModuleRuntimeConfigMC12->IsEnabled)
    {
        PLIB_ADCHS_Enable(pModuleConfigMC12->moduleId);
    }
    
    return true;
}

bool MC12b_ReadSamples( AInSampleArray* samples,                            \
                        const AInArray* channelConfig,                      \
                        AInRuntimeArray* channelRuntimeConfig,              \
                        uint32_t triggerTimeStamp)
{
    size_t i=0;
    
    for(i=0; i < channelConfig->Size; ++i)
    {
        if (!channelRuntimeConfig->Data[i].IsEnabled)
        {
            continue;
        }
        
        const AInChannel* currentChannelConfig = &channelConfig->Data[i];
        uint8_t bufIndex = currentChannelConfig->Config.MC12b.BufferIndex;
        if(!DRV_ADC_SamplesAvailable(bufIndex))
        {
            continue;
        }
        
        AInSample* sample = &samples->Data[samples->Size];
        sample->Channel = channelConfig->Data[i].ChannelId;
        sample->Timestamp = triggerTimeStamp;   // We are using the module trigger timestamp here to allow streaming to know which are part of the same set
        volatile uint32_t data = DRV_ADC_SamplesRead(bufIndex);
        sample->Value = data; // The XYZ_ConvertToVoltage functions are called downstream for conversion (FPU doesn't work in an ISR)
        
        samples->Size += 1;
    }
    
    return true;
}

bool MC12b_TriggerConversion( void )
{
    DRV_ADC_Start();
    return true;
}

double MC12b_ConvertToVoltage(                                              \
                        const MC12bChannelConfig* channelConfig,            \
                        const AInRuntimeConfig* runtimeConfig,              \
                        const AInSample* sample)
{
    
    double dataOut = 0.0;
    double range = pModuleRuntimeConfigMC12->Range;
    double scale = channelConfig->InternalScale;
    double CalM = runtimeConfig->CalM;

    dataOut = ( range * scale * CalM * (double)sample->Value)/              \
                        (pModuleConfigMC12->Resolution) + runtimeConfig->CalB;
    return (dataOut);
}
