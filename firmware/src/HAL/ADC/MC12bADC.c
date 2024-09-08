/*! @file MC12bADC.c 
 * 
 * This file implements the functions to manage the module ADC MC12bADC. 
 */

#include "MC12bADC.h"
#include "../DIO.h"
#include "configuration.h"
#include "definitions.h"
//#include "system_config.h"
//#include "framework/driver/adc/drv_adc_static.h"
#include "Util/Delay.h"
#include "state/data/BoardData.h"

//#define UNUSED(x) (void)(x)
#define UNUSED(identifier) /* identifier */


#ifndef min
#define min(x,y) x <= y ? x : y
#endif // min

#ifndef max
#define max(x,y) x >= y ? x : y
#endif // min

//! Pointer to the module configuration data structure to be set in initialization
static MC12bModuleConfig* gpModuleConfigMC12;
//! Pointer to the module configuration data structure in runtime
//! to be set in initialization
static AInModuleRuntimeConfig* gpModuleRuntimeConfigMC12;
//! Boolean to indicate if this module is enabled
static bool gIsEnabled = false;

bool MC12b_InitHardware(MC12bModuleConfig* pModuleConfigInit,
        AInModuleRuntimeConfig * pModuleRuntimeConfigInit) {
    gpModuleConfigMC12 = pModuleConfigInit;
    gpModuleRuntimeConfigMC12 = pModuleRuntimeConfigInit;

    // Copy factory calibration data to calibration registers
    ADC0CFG = DEVADC0;
    ADC1CFG = DEVADC1;
    ADC2CFG = DEVADC2;
    ADC3CFG = DEVADC3;
    ADC4CFG = DEVADC4;
    ADC7CFG = DEVADC7;
    return true;
}

bool MC12b_WriteModuleState(void) {

    if (gpModuleRuntimeConfigMC12->IsEnabled == gIsEnabled) {
        return false;
    }

    if (gpModuleRuntimeConfigMC12->IsEnabled) {
        /* Enable clock to analog circuit */
        ADCANCONbits.ANEN0 = 1; // Enable the clock to analog bias
        ADCANCONbits.ANEN1 = 1; // Enable the clock to analog bias
        ADCANCONbits.ANEN2 = 1; // Enable the clock to analog bias
        ADCANCONbits.ANEN3 = 1; // Enable the clock to analog bias
        ADCANCONbits.ANEN4 = 1; // Enable the clock to analog bias
        ADCANCONbits.ANEN7 = 1; // Enable the clock to analog bias

        while (!ADCANCONbits.WKRDY0); // Wait until ADC0 is ready
        while (!ADCANCONbits.WKRDY1); // Wait until ADC1 is ready
        while (!ADCANCONbits.WKRDY2); // Wait until ADC2 is ready
        while (!ADCANCONbits.WKRDY3); // Wait until ADC3 is ready
        while (!ADCANCONbits.WKRDY4); // Wait until ADC4 is ready
        while (!ADCANCONbits.WKRDY7); // Wait until ADC7 is ready

        ADCHS_ModulesEnable(ADCHS_MODULE0_MASK);
        ADCHS_ModulesEnable(ADCHS_MODULE1_MASK);
        ADCHS_ModulesEnable(ADCHS_MODULE2_MASK);
        ADCHS_ModulesEnable(ADCHS_MODULE3_MASK);
        ADCHS_ModulesEnable(ADCHS_MODULE4_MASK);
        ADCHS_ModulesEnable(ADCHS_MODULE7_MASK);

        gIsEnabled = true;
    } else {
        //Disable module


        ADCHS_ModulesDisable(ADCHS_MODULE0_MASK);
        ADCHS_ModulesDisable(ADCHS_MODULE1_MASK);
        ADCHS_ModulesDisable(ADCHS_MODULE2_MASK);
        ADCHS_ModulesDisable(ADCHS_MODULE3_MASK);
        ADCHS_ModulesDisable(ADCHS_MODULE4_MASK);
        ADCHS_ModulesDisable(ADCHS_MODULE7_MASK);


        /* Enable clock to analog circuit */
        ADCANCONbits.ANEN0 = 1; // Enable the clock to analog bias
        ADCANCONbits.ANEN1 = 1; // Enable the clock to analog bias
        ADCANCONbits.ANEN2 = 1; // Enable the clock to analog bias
        ADCANCONbits.ANEN3 = 1; // Enable the clock to analog bias
        ADCANCONbits.ANEN4 = 1; // Enable the clock to analog bias
        //ADCANCONbits.ANEN7 = 1; // Enable the clock to analog bias

        gIsEnabled = false;
    }

    return true;
}

bool MC12b_WriteStateAll(
        const AInArray* channelConfig,
        AInRuntimeArray* channelRuntimeConfig) {
    bool isEnabled = gpModuleRuntimeConfigMC12->IsEnabled;
    if (isEnabled) {
        gpModuleRuntimeConfigMC12->IsEnabled = false;
    }

    size_t i = 0;
    bool result = true;
    for (i = 0; i < channelConfig->Size; ++i) {
        result &= MC12b_WriteStateSingle(
                &(channelConfig->Data[i].Config.MC12b),
                &channelRuntimeConfig->Data[i]);
    }

    if (isEnabled) {
        gpModuleRuntimeConfigMC12->IsEnabled = isEnabled;
    }

    return result;
}

bool MC12b_WriteStateSingle(
        const MC12bChannelConfig* channelConfig,
        AInRuntimeConfig* channelRuntimeConfig) {


    if (channelConfig->ChannelType == 1) {
        if (channelRuntimeConfig->IsEnabled) {
            ADCHS_ModulesEnable(channelConfig->ModuleId);
            ADCHS_ChannelResultInterruptEnable(channelConfig->ChannelId);
        } else {
            ADCHS_ModulesDisable(channelConfig->ModuleId);
            ADCHS_ChannelResultInterruptDisable(channelConfig->ChannelId);
        }
    }
    //    else {
    //        if (channelRuntimeConfig->IsEnabled) {
    //            ADCHS_ChannelResultInterruptEnable(channelConfig->ChannelId);          
    //        } else {
    //            ADCHS_ChannelResultInterruptDisable(channelConfig->ChannelId);           
    //            
    //        }
    //    }
    // TODO: What about channel 2-3
    return true;
}

bool MC12b_TriggerConversion(AInRuntimeArray* pRunTimeChannlConfig, AInArray* pAIConfigArr, MC12b_adcType_t type) {
    int aiChannelSize = min(pRunTimeChannlConfig->Size, pAIConfigArr->Size);
    if (type == MC12B_ADC_TYPE_DEDICATED || type==MC12B_ADC_TYPE_ALL) {
        for (int i = 0; i < aiChannelSize; i++) {
            if (pRunTimeChannlConfig->Data[i].IsEnabled) {
                if (pAIConfigArr->Data[i].Config.MC12b.ChannelType == 1){                   
                    ADCHS_ChannelConversionStart(pAIConfigArr->Data[i].Config.MC12b.ChannelId);                     
                }
            }
        }
    }
    if(type==MC12B_ADC_TYPE_SHARED || type==MC12B_ADC_TYPE_ALL)
        ADCHS_GlobalEdgeConversionStart();
    return true;
}

double MC12b_ConvertToVoltage(
        const MC12bChannelConfig* channelConfig,
        const AInRuntimeConfig* runtimeConfig,
        const AInSample* sample) {

    double dataOut = 0.0;
    double range = gpModuleRuntimeConfigMC12->Range;
    double scale = channelConfig->InternalScale;
    double CalM = runtimeConfig->CalM;

    dataOut = (range * scale * CalM * (double) sample->Value) /
            (gpModuleConfigMC12->Resolution) + runtimeConfig->CalB;
    return (dataOut);
}

bool MC12b_ReadResult(ADCHS_CHANNEL_NUM channel, uint32_t *pVal) {
    if (ADCHS_ChannelResultIsReady(channel)) {
        *pVal = ADCHS_ChannelResultGet(channel);
        return true;
    }
    *pVal = 0;
    return false;
}