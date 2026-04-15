/*! @file MC12bADC.c 
 * 
 * This file implements the functions to manage the module ADC MC12bADC. 
 */

#include "MC12bADC.h"
#include "../DIO.h"
#include "configuration.h"
#include "definitions.h"
#include "Util/Delay.h"
#include "state/data/BoardData.h"
#include "state/board/BoardConfig.h"

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

// --- Hardware trigger constants (DS60001320) ---
// Trigger source encoding shared by per-channel TRGSRC (ADCTRGx) and
// scan trigger STRGSRC (ADCCON1[20:16]).
#define ADC_TRGSRC_TMR5             7       // Timer4/5 match (streaming timer)

// ADCTRGx register layout: 4 channels per register, each TRGSRC field
// is 5 bits wide at byte-aligned positions.
//   ADCTRG(n) covers channels [(n-1)*4 .. (n-1)*4+3]
//   Channel C → register index (C/4), bit shift (C%4)*8
// Only channels 0-11 have per-channel TRGSRC fields (ADCTRG1-3).
// Higher-numbered shared channels are scan-triggered via ADCCON1.STRGSRC.
#define ADCTRG_REG_COUNT            3
#define ADCTRG_CHANNELS_PER_REG     4
#define ADCTRG_FIELD_MASK           0x1FU   // 5-bit trigger source field
#define ADCCON1_STRGSRC_SHIFT       16      // STRGSRC is bits [20:16]

// PLIB-initialized trigger register values, saved once in MC12b_InitHardware
// (after ADCHS_Initialize). Restored when hardware triggering is disabled so
// non-streaming ADC reads continue to work with MHC-configured trigger sources.
static uint32_t gSavedADCTRG[ADCTRG_REG_COUNT];
static uint32_t gSavedSTRGSRC;

bool MC12b_InitHardware(MC12bModuleConfig* pModuleConfigInit,
        AInModuleRuntimeConfig * pModuleRuntimeConfigInit) {
    gpModuleConfigMC12 = pModuleConfigInit;
    gpModuleRuntimeConfigMC12 = pModuleRuntimeConfigInit;

    // Save PLIB trigger defaults for restore after streaming
    gSavedADCTRG[0] = ADCTRG1;
    gSavedADCTRG[1] = ADCTRG2;
    gSavedADCTRG[2] = ADCTRG3;
    gSavedSTRGSRC = (ADCCON1 >> ADCCON1_STRGSRC_SHIFT) & ADCTRG_FIELD_MASK;

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

/**
 * Set a single channel's trigger source in ADCTRGx.
 * @param trg   Working copy of ADCTRG[3] array (modified in-place)
 * @param chId  ADCHS channel number (0-11 only)
 * @param src   Trigger source value (0-31)
 */
static void SetChannelTrigSrc(uint32_t trg[ADCTRG_REG_COUNT],
                              uint8_t chId, uint32_t src) {
    uint8_t regIdx = chId / ADCTRG_CHANNELS_PER_REG;
    if (regIdx >= ADCTRG_REG_COUNT) return;   // channels > 11 not in ADCTRG1-3
    uint32_t shift = (chId % ADCTRG_CHANNELS_PER_REG) * 8;
    trg[regIdx] &= ~(ADCTRG_FIELD_MASK << shift);
    trg[regIdx] |= ((src & ADCTRG_FIELD_MASK) << shift);
}

void MC12b_ConfigureHardwareTrigger(bool hwDedicated, bool hwShared) {
    const tBoardConfig* pCfg = (const tBoardConfig*)BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);

    // Start from saved PLIB defaults, overlay hardware trigger sources
    uint32_t trg[ADCTRG_REG_COUNT] = {
        gSavedADCTRG[0], gSavedADCTRG[1], gSavedADCTRG[2]
    };

    if (hwDedicated) {
        // Walk the board channel table — set TMR5 trigger for each Type 1
        for (size_t i = 0; i < pCfg->AInChannels.Size; i++) {
            const AInChannel* ch = &pCfg->AInChannels.Data[i];
            if (ch->Type != AIn_MC12bADC) continue;
            if (ch->Config.MC12b.ChannelType != 1) continue;
            SetChannelTrigSrc(trg, ch->Config.MC12b.ChannelId, ADC_TRGSRC_TMR5);
        }
    }

    ADCTRG1 = trg[0];
    ADCTRG2 = trg[1];
    ADCTRG3 = trg[2];

    // MODULE7 scan trigger (ADCCON1.STRGSRC): controls when the shared
    // module starts its multiplexed scan of all enabled shared channels.
    uint32_t scanSrc = hwShared ? ADC_TRGSRC_TMR5 : gSavedSTRGSRC;
    uint32_t con1 = ADCCON1;
    con1 &= ~(ADCTRG_FIELD_MASK << ADCCON1_STRGSRC_SHIFT);
    con1 |= (scanSrc << ADCCON1_STRGSRC_SHIFT);
    ADCCON1 = con1;
}

// Query functions read SFR registers directly — the register value IS
// the authoritative state.  No shadow variables needed.
bool MC12b_IsHwTriggerDedicated(void) {
    // Check first dedicated channel (AN0) — all Type 1 channels are set
    // together so any one is representative.
    return (ADCTRG1 & ADCTRG_FIELD_MASK) == ADC_TRGSRC_TMR5;
}

bool MC12b_IsHwTriggerShared(void) {
    return ((ADCCON1 >> ADCCON1_STRGSRC_SHIFT) & ADCTRG_FIELD_MASK) == ADC_TRGSRC_TMR5;
}