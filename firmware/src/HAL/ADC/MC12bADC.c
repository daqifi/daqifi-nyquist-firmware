/*! @file MC12bADC.c 
 * 
 * This file implements the functions to manage the module ADC MC12bADC. 
 */

#include "MC12bADC.h"
#include "../DIO.h"
#include "configuration.h"
#include "definitions.h"
#include "state/data/BoardData.h"
#include "state/board/BoardConfig.h"
#include "Util/Logger.h"

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
//! Boolean to indicate if this module is enabled.
//! volatile per #421 — set by MC12b_WriteModuleState (called from
//! ADC_Tasks polling and SCPI ENA paths), read in the same function.
//! Without volatile -O3 may treat it as an independent truth source
//! and skip re-fetching from the module runtime config it shadows.
static volatile bool gIsEnabled = false;

// --- Hardware trigger constants (Section 22 ADC FRM DS60001344E) ---
// Trigger source encoding shared by per-channel TRGSRC (ADCTRGx) and
// scan trigger STRGSRC (ADCCON1[20:16]). Verbatim from Register 22-19
// (page 22-38) of the 12-bit HS SAR ADC FRM:
//   00000 = No Trigger
//   00001 = GSWTRG (Global Software Edge — fires on ADCCON3.GSWTRG)
//   00010 = GLSWTRG (Global Level Software Trigger)
//   00011 = STRIG (Scan Trigger — required for MODULE7 mux scan
//                  inclusion; see ADCCSS1 Note 2 page 22-32)
//   00100..11111 = device-specific (TMR1/3/5/OC1 etc., per
//                  PIC32MZ-EF datasheet DS60001320H ADC chapter)
#define ADC_TRGSRC_NONE             0       // No trigger / channel disabled
#define ADC_TRGSRC_GSWTRG           1       // Fired by ADCHS_GlobalEdgeConversionStart()
#define ADC_TRGSRC_STRIG            3       // Follow MODULE7 STRGSRC scan trigger
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

    // Type 1 enable mask (used by MC12b_TriggerConversion software path).
    // Do NOT enable CH3 result interrupt — #292 moves T1 result reads to
    // the EOS deferred task, eliminating the ~5μs ISR entry/exit overhead
    // that was the T1 throughput bottleneck.
    uint32_t mask = 0;
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
    } else {
        ADCHS_ModulesDisable(ADCHS_MODULE3_MASK);
    }
    ADCHS_ChannelResultInterruptDisable(ADCHS_CH3);

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

void MC12b_DrainType1Results(void) {
    // #541 D-A: read-and-discard any pending Type 1 results so a stale
    // conversion parked since the last idle poll can't be emitted as the
    // first sample of a new session (ARDY would otherwise still be set,
    // and the direct-read path trusts ARDY for freshness).  Reading
    // ADCDATAx clears ARDY (FRM Fig 22-7).  Called from Streaming_Start
    // before the streaming timer is armed.
    const tBoardConfig* pCfg =
            (const tBoardConfig*)BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    for (size_t i = 0; i < pCfg->AInChannels.Size; i++) {
        const AInChannel* ch = &pCfg->AInChannels.Data[i];
        if (ch->Type != AIn_MC12bADC) continue;
        if (ch->Config.MC12b.ChannelType != 1) continue;
        uint32_t discard;
        (void)MC12b_ReadResult(ch->Config.MC12b.ChannelId, &discard);
    }
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

    // #421 fix v2: per-channel TRGSRC fields for shared MODULE7 channels
    // (HW channels 0-11) come up with TRGSRC=1 (GSWTRG) in the Microchip-
    // generated boot config (plib_adchs.c ADCTRG2/3). With TRGSRC=GSWTRG,
    // the channel converts only when ADCCON3.GSWTRG is pulsed via
    // ADCHS_GlobalEdgeConversionStart(). Pre-#282 (commit f114f44e) the
    // streaming task explicitly fired GSWTRG every cycle, so this worked
    // implicitly. After #282, hardware-trigger sync replaced that call
    // with STRGSRC=TMR5 (which fires the MODULE7 scan), but the per-
    // channel TRGSRC field takes precedence over STRGSRC for channels
    // 0-11 — so they stayed waiting for a GSWTRG that never arrived,
    // firing exactly once during ADCANCON warmup and then going silent.
    // HW channels 24+ are Class 3 (no per-channel TRGSRC slot) and
    // automatically follow STRGSRC, which is why they kept working.
    //
    // The correct value to enroll a Class 1/2 channel in MODULE7 scan is
    // TRGSRC=STRIG (3), per Section 22 ADC FRM DS60001344E:
    //   - Register 22-19 (page 22-38) defines TRGSRC=00011 = STRIG.
    //   - ADCCSS1 Note 2 (page 22-32): "If a Class 1 or Class 2 input
    //     is included in the scan by setting the CSSx bit to '1' and
    //     by setting the TRGSRCx<4:0> bits to STRIG mode ('0b011'),
    //     the user application must ensure that no other triggers are
    //     generated for that input using the RQCNVRT bit in the
    //     ADCCON3 register or the hardware input or any digital filter."
    //
    // The previous v1 patch used TRGSRC=4 — that is TMR1 trigger per
    // device datasheet, not STRIG. It accidentally worked when TMR1 was
    // healthy (FreeRTOS tick at 1 kHz) and collapsed when other code
    // paths (WiFi STA association) disturbed TMR1 cadence. Fixed
    // 2026-05-07 after datasheet citation. See docs/406_O3_INVESTIGATION.md.
    //
    // Gated on hwShared (mirrors the hwDedicated branch above): when
    // shared HW triggering is off, MODULE7 isn't being scan-driven and
    // the saved per-channel GSWTRG defaults are correct for the
    // non-streaming software-trigger path (ADCHS_GlobalEdgeConversionStart).
    if (hwShared) {
        for (size_t i = 0; i < pCfg->AInChannels.Size; i++) {
            const AInChannel* ch = &pCfg->AInChannels.Data[i];
            if (ch->Type != AIn_MC12bADC) continue;
            if (ch->Config.MC12b.ChannelType == 1) continue;  // Type 1 handled above
            uint8_t chId = ch->Config.MC12b.ChannelId;
            if (chId > 11) continue;  // CH12+ have no per-channel TRGSRC field
            SetChannelTrigSrc(trg, chId, ADC_TRGSRC_STRIG);
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

// --- ADC acquisition-time (SAMC) runtime control — #328 phase 1 ----------
// ADCxTIMEbits.SAMC  = dedicated module sample time (0..4). 10-bit, max 1023.
// ADCCON2bits.SAMC   = shared MODULE7 sample time.         10-bit, max 1023.
// Actual acquisition = (SAMC + 2) ADC clocks. With ADCDIV=1, ADC_clk=50 MHz so
// one clock = 20 ns.
// Writing these requires ADCCON1.ON = 0. We toggle it around the update so
// the change applies cleanly to all modules.

bool MC12b_SetAcquisitionSamc(int32_t samcDedicated, int32_t samcShared) {
    if (samcDedicated > (int32_t)MC12B_SAMC_MAX) return false;
    if (samcShared > (int32_t)MC12B_SAMC_MAX) return false;

    // Nothing to do — don't disturb the ADC.
    if (samcDedicated < 0 && samcShared < 0) return true;

    bool adcWasOn = (ADCCON1bits.ON != 0U);
    if (adcWasOn) {
        ADCCON1bits.ON = 0;
    }

    if (samcDedicated >= 0) {
        uint32_t s = (uint32_t)samcDedicated;
        ADC0TIMEbits.SAMC = s;
        ADC1TIMEbits.SAMC = s;
        ADC2TIMEbits.SAMC = s;
        ADC3TIMEbits.SAMC = s;
        ADC4TIMEbits.SAMC = s;
    }
    if (samcShared >= 0) {
        ADCCON2bits.SAMC = (uint32_t)samcShared;
    }

    if (adcWasOn) {
        ADCCON1bits.ON = 1;
        // Bound the hardware polling loops so an unhealthy reference can't
        // hang the SCPI task forever. ~20 ms at 100 MHz is plenty for a
        // normal bandgap settle (typically microseconds).
        uint32_t timeout = 2000000U;
        while (ADCCON2bits.BGVRRDY == 0U && timeout-- != 0U) { /* wait */ }
        if (ADCCON2bits.BGVRRDY == 0U || ADCCON2bits.REFFLT != 0U) {
            ADCCON1bits.ON = 0;
            return false;
        }
    }
    return true;
}

void MC12b_GetAcquisitionSamc(uint16_t* outSamcDedicated, uint16_t* outSamcShared) {
    if (outSamcDedicated) {
        *outSamcDedicated = (uint16_t)ADC0TIMEbits.SAMC;
    }
    if (outSamcShared) {
        *outSamcShared = (uint16_t)ADCCON2bits.SAMC;
    }
}