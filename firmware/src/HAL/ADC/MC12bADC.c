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

//! Bitmask of enabled Type 1 ADCHS channels (bits 0-4).
//! Set by MC12b_WriteStateAll; getter available for diagnostics.
//! uint32_t (native bus width) — see Issue #277.
static volatile uint32_t gType1EnabledMask = 0;

uint32_t MC12b_GetType1EnabledMask(void) { return gType1EnabledMask; }

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
        if (channelConfig->Data[i].Type != AIn_MC12bADC) continue;
        result &= MC12b_WriteStateSingle(
                &(channelConfig->Data[i].Config.MC12b),
                &channelRuntimeConfig->Data[i]);
    }

    // Batch interrupt for Type 1: build bitmask of enabled ADCHS channels
    // and enable only CH3's result interrupt (the batch trigger).
    uint8_t mask = 0;
    for (i = 0; i < channelConfig->Size; ++i) {
        if (channelConfig->Data[i].Type == AIn_MC12bADC &&
            channelConfig->Data[i].Config.MC12b.ChannelType == 1) {
            uint8_t chId = channelConfig->Data[i].Config.MC12b.ChannelId;
            if (channelRuntimeConfig->Data[i].IsEnabled) {
                mask |= (1U << chId);
            }
            ADCHS_ChannelResultInterruptDisable(chId);
        }
    }
    gType1EnabledMask = mask;
    if (mask != 0) {
        ADCHS_ModulesEnable(ADCHS_MODULE3_MASK);
        ADCHS_ChannelResultInterruptEnable(ADCHS_CH3);
    } else {
        ADCHS_ChannelResultInterruptDisable(ADCHS_CH3);
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
            // Do NOT enable per-channel result interrupt here.
            // Type 1 channels use batch ISR via CH3 (see WriteStateAll).
        } else {
            ADCHS_ModulesDisable(channelConfig->ModuleId);
            ADCHS_ChannelResultInterruptDisable(channelConfig->ChannelId);
        }
    }
    return true;
}

bool MC12b_TriggerConversion(AInRuntimeArray* pRunTimeChannlConfig, AInArray* pAIConfigArr, MC12b_adcType_t type) {
    int aiChannelSize = min(pRunTimeChannlConfig->Size, pAIConfigArr->Size);
    if (type == MC12B_ADC_TYPE_DEDICATED || type==MC12B_ADC_TYPE_ALL) {
        bool anyType1Triggered = false;
        bool triggerChTriggered = false;
        for (int i = 0; i < aiChannelSize; i++) {
            if (pRunTimeChannlConfig->Data[i].IsEnabled &&
                pAIConfigArr->Data[i].Type == AIn_MC12bADC &&
                pAIConfigArr->Data[i].Config.MC12b.ChannelType == 1) {
                ADCHS_ChannelConversionStart(pAIConfigArr->Data[i].Config.MC12b.ChannelId);
                anyType1Triggered = true;
                if (pAIConfigArr->Data[i].Config.MC12b.ChannelId == ADCHS_CH3) {
                    triggerChTriggered = true;
                }
            }
        }
        // Always trigger CH3 (batch trigger) so its data-ready ISR fires
        // and reads all Type 1 results. If ch14 (CH3) is disabled, MODULE3
        // converts but the result is harmlessly discarded by the ISR
        // (no matching enabled channel in ADC_ReadADCSampleFromISR).
        if (anyType1Triggered && !triggerChTriggered) {
            ADCHS_ChannelConversionStart(ADCHS_CH3);
        }
    }
    if(type==MC12B_ADC_TYPE_SHARED || type==MC12B_ADC_TYPE_ALL)
        ADCHS_GlobalEdgeConversionStart();
    return true;
}

double MC12b_ConvertToVoltage(
        const MC12bChannelConfig* channelConfig,
        const AInRuntimeConfig* runtimeConfig,
        uint32_t rawValue) {

    double range = gpModuleRuntimeConfigMC12->Range;
    double scale = channelConfig->InternalScale;
    double CalM = runtimeConfig->CalM;

    return (range * scale * CalM * (double)rawValue) /
            (gpModuleConfigMC12->Resolution) + runtimeConfig->CalB;
}

bool MC12b_ReadResult(ADCHS_CHANNEL_NUM channel, uint32_t *pVal) {
    if (ADCHS_ChannelResultIsReady(channel)) {
        *pVal = ADCHS_ChannelResultGet(channel);
        return true;
    }
    *pVal = 0;
    return false;
}

// Hardware trigger source values from PIC32MZ EFM ATDF (DS60001320).
// Per-channel TRGSRC (ADCTRGx) and scan STRGSRC (ADCCON1) share the
// same encoding.
#define ADC_TRGSRC_NONE     0   // Software only (RQCNVRT / GSWTRG)
#define ADC_TRGSRC_GSWTRG   1   // Global software edge trigger
#define ADC_TRGSRC_TMR5     7   // Timer4/5 match (streaming timer)

void MC12b_ConfigureHardwareTrigger(bool hwDedicated, bool hwShared) {
    uint32_t dedicatedSrc = hwDedicated ? ADC_TRGSRC_TMR5 : ADC_TRGSRC_NONE;
    uint32_t sharedSrc    = hwShared    ? ADC_TRGSRC_TMR5 : ADC_TRGSRC_GSWTRG;

    // Dedicated modules (AN0-AN4): set per-channel trigger source in ADCTRGx.
    // Each TRGSRCn field is 5 bits at byte-aligned positions within ADCTRGx.
    // AN0-AN3 in ADCTRG1, AN4 in ADCTRG2[4:0].
    ADCTRG1 = (dedicatedSrc <<  0) |   // AN0 (MODULE0)
              (dedicatedSrc <<  8) |   // AN1 (MODULE1)
              (dedicatedSrc << 16) |   // AN2 (MODULE2)
              (dedicatedSrc << 24);    // AN3 (MODULE3 / batch trigger)

    // ADCTRG2: AN4 (dedicated MODULE4) + AN5-AN7 (shared MODULE7)
    uint32_t sharedMuxSrc = hwShared ? ADC_TRGSRC_TMR5 : ADC_TRGSRC_GSWTRG;
    ADCTRG2 = (dedicatedSrc <<  0) |   // AN4 (MODULE4)
              (sharedMuxSrc <<  8) |   // AN5 (shared)
              (sharedMuxSrc << 16) |   // AN6 (shared)
              (sharedMuxSrc << 24);    // AN7 (shared)

    // ADCTRG3: AN8 (shared), AN9-AN10 (unused?), AN11 (shared)
    ADCTRG3 = (sharedMuxSrc <<  0) |   // AN8 (shared)
              (ADC_TRGSRC_NONE << 8) | // AN9
              (ADC_TRGSRC_NONE << 16) | // AN10
              (sharedMuxSrc << 24);    // AN11 (shared)

    // ADCCON1.STRGSRC: scan trigger source for MODULE7 global edge conversion.
    // Bits [20:16]. Preserve other ADCCON1 bits (FSSCLKEN, FSPBCLKEN, etc.).
    uint32_t con1 = ADCCON1;
    con1 &= ~(0x1FU << 16);           // Clear STRGSRC[20:16]
    con1 |= (sharedSrc << 16);        // Set new scan trigger source
    ADCCON1 = con1;
}

// No static shadow variables — read the SFR registers directly to
// determine if hardware triggering is active.  The register value IS
// the authoritative state.  Avoids O3 .sbss layout issues (#277, #282).
bool MC12b_IsHwTriggerDedicated(void) {
    return (ADCTRG1 & 0x1FU) == ADC_TRGSRC_TMR5;
}

bool MC12b_IsHwTriggerShared(void) {
    return ((ADCCON1 >> 16) & 0x1FU) == ADC_TRGSRC_TMR5;
}