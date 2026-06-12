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
#include "state/runtime/BoardRuntimeConfig.h"
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

    // #541 D-B: replace the Harmony boot scan list with the idle list —
    // identical except the dead temp sensor (AN44, erratum 18) is dropped,
    // which the boot CSS wasted a full scan slot on.
    MC12b_RestoreIdleScanList();
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
        for (int i = 0; i < aiChannelSize; i++) {
            if (pRunTimeChannlConfig->Data[i].IsEnabled &&
                pAIConfigArr->Data[i].Type == AIn_MC12bADC &&
                pAIConfigArr->Data[i].Config.MC12b.ChannelType == 1) {
                ADCHS_ChannelConversionStart(pAIConfigArr->Data[i].Config.MC12b.ChannelId);
            }
        }
        // The former unconditional CH3 "batch trigger" (fire MODULE3 even
        // with ch14 disabled so its data-ready ISR could read all Type 1
        // results) was removed with #541: its consumer, the CH3 batch ISR,
        // was deleted in #292, and results are now read by ARDY-gated
        // direct reads (streaming) or the EOS task (idle) — both of which
        // only touch enabled channels. Triggering a disabled module was
        // pure wasted conversion.
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

// --- #541 D-B: dynamic shared-scan list (ADCCSS) management ---------------
//
// The Harmony boot init writes a STATIC scan list (all public T2 inputs +
// all monitoring inputs, 19 total) and the firmware never changed it: every
// scan trigger walked all 19 inputs (~216 us at SAMC=100) regardless of
// which channels were actually enabled.  That fixed T_scan is what made the
// #539 EOS collapse channel-count-independent.  These functions rebuild the
// scan list per streaming session from the channels that actually need
// scanning, so T_scan scales with the session (1xT2 OBDiag=0 -> 1 input ->
// ~11.5 us) and the computed scan-rate bound (MC12b_ScanMaxFreq) is honest.
//
// Safe because mid-stream channel-set changes are REJECTED (#116:
// CONF:ADC:CHANnel; CONF:ADC:OBDiag likewise) — the session's scan list
// cannot go stale while streaming.
//
// Direct SFR access (ADCCSS1/2, ADCCON3 TRGSUSP/UPDRDY): no Harmony PLIB
// API exists for online CSS updates; the TRGSUSP -> UPDRDY -> write ->
// resume sequence is the FRM-documented safe-update path ("ADC SFRs are
// ready to be (and can be safely) updated with new values" — DS60001344E
// ADCCON3 bits 12/10 + Update Ready Event, p.22-108).

// Erratum 18 (DS80000663R): the internal temperature sensor (AN44) is
// nonfunctional on all silicon revs, no workaround.  It is never included
// in any scan list — the boot CSS wasted a full SAMC+conversion slot per
// scan reading dead silicon.
#define ADC_AN_TEMP_SENSOR  44u

uint32_t MC12b_ComputeScanList(bool enabledOnly, bool includeMonitoring,
                               uint32_t *pCss1, uint32_t *pCss2) {
    const tBoardConfig* pCfg =
            (const tBoardConfig*)BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    const AInRuntimeArray* pRt =
            (const AInRuntimeArray*)BoardRunTimeConfig_Get(
                    BOARDRUNTIMECONFIG_AIN_CHANNELS);
    uint32_t css1 = 0, css2 = 0, count = 0;
    size_t n = (pCfg->AInChannels.Size < pRt->Size)
             ? pCfg->AInChannels.Size : pRt->Size;
    for (size_t i = 0; i < n; i++) {
        const AInChannel* ch = &pCfg->AInChannels.Data[i];
        if (ch->Type != AIn_MC12bADC) continue;
        if (ch->Config.MC12b.ChannelType == 1) continue;  // dedicated, never scanned
        uint32_t an = ch->Config.MC12b.ChannelId;          // CSS bit == AN number
        if (an == ADC_AN_TEMP_SENSOR) continue;            // erratum 18
        if (an >= 64u) continue;  // defensive: beyond ADCCSS1/2 bit range
                                  // (hardware max is AN44; a corrupted config
                                  // entry must not shift out of range)
        bool isMonitoring = (ch->Config.MC12b.IsPublic != 1);
        if (isMonitoring) {
            if (!includeMonitoring) continue;
            // Monitoring channels are not user-controllable; IsEnabled is
            // the boot default (true except the dead temp sensor).
            if (pRt->Data[i].IsEnabled != 1) continue;
        } else if (enabledOnly && pRt->Data[i].IsEnabled != 1) {
            continue;
        }
        if (an < 32u) css1 |= (1U << an);
        else          css2 |= (1U << (an - 32u));
        count++;
    }
    if (pCss1 != NULL) *pCss1 = css1;
    if (pCss2 != NULL) *pCss2 = css2;
    return count;
}

void MC12b_ApplyScanList(uint32_t css1, uint32_t css2) {
    if (ADCCSS1 == css1 && ADCCSS2 == css2) return;
    if (ADCCON1bits.ON) {
        // FRM-documented online update: suspend triggers, wait until no
        // conversion is in flight (UPDRDY), write, resume.  Worst case a
        // full in-progress scan must drain first (~216 us at the boot
        // config); the spin bound is ms-scale — far above any legal scan.
        ADCCON3bits.TRGSUSP = 1;
        uint32_t spin = 500000u;
        while ((ADCCON3bits.UPDRDY == 0) && (--spin != 0u)) { }
        if (spin == 0u) {
            LOG_E("ADC CSS update: UPDRDY timeout — scan list unchanged");
            ADCCON3bits.TRGSUSP = 0;
            return;
        }
        ADCCSS1 = css1;
        ADCCSS2 = css2;
        ADCCON3bits.TRGSUSP = 0;
    } else {
        // ADC off — registers update directly.
        ADCCSS1 = css1;
        ADCCSS2 = css2;
    }
}

void MC12b_RestoreIdleScanList(void) {
    // Idle list = ALL public T2 inputs (regardless of enable state, so
    // MEAS:VOLT:DC? works immediately after an idle channel enable without
    // a rebuild hook) + enabled monitoring inputs.  Equals the Harmony boot
    // CSS minus the dead temp sensor (verified bit-for-bit against the
    // board map, docs/ADC_HW_SEMANTICS.md).
    uint32_t css1, css2;
    (void)MC12b_ComputeScanList(false, true, &css1, &css2);
    MC12b_ApplyScanList(css1, css2);
}

uint32_t MC12b_ScanMaxFreq(uint32_t nActive) {
    // #541 D-C: max safe shared-scan trigger rate.  Retriggering the scan
    // while in progress is documented-undefined (FRM §22.3.2) — the #539
    // mechanism — so the streaming tick period must exceed the scan's
    // true busy time with margin:
    //
    //   T_busy = N_active x (SAMC + 2 + 14) x TAD7  +  T_fixed(~5.5 us)
    //   cap    = 1 / (T_busy x 1.1)
    //
    // 2 = sample-time offset (acquisition = SAMC+2 TAD), 14 = measured
    // per-input conversion+handoff (13 TAD datasheet conversion + ~1 TAD
    // scan handoff).  T_fixed is a PER-SCAN constant the per-input model
    // misses; both terms are pinned by two silicon anchors (2026-06-12,
    // SAMC=100): n=7 scan wedges the device at 11750 Hz (85.1 us period)
    // and is clean at 11500 (87.0 us) -> T_busy(7) in (85.1, 87.0]; n=19
    // boot scan measured timer->EOS = 216 us and verified clean at
    // 4500 Hz (222.2 us) -> T_busy(19) in [216, 222.2].  Solving both:
    // per-input ~= (SAMC+16) TAD, T_fixed ~= 5.5 us.  We use 6 us + a
    // 10% period margin so no admitted rate sits at the boundary —
    // OPERATING AT THE BOUNDARY IS NOT A SOFT FAILURE: sustained mid-scan
    // retriggering at n=7 killed the USB peripheral outright (device off
    // the bus until PICkit reset), unlike the silent EOS death #539 saw
    // with the 19-input scan.
    //
    // All terms read live so SAMC/divider changes are honored:
    //   TAD7 = 2 x ADCDIV x TQ;  TQ = (CONCLKDIV+1) x TCLK  (DS60001320H
    //   Reg 28-2/28-3 — note the EF datasheet deviates from the FRM on
    //   CONCLKDIV semantics; the datasheet matches silicon).  TCLK = 10 ns
    //   (ADCSEL=00 -> PBCLK3 = 100 MHz, fixed clock tree).
    if (nActive == 0u) return 0xFFFFFFFFu;  // no scan armed — no bound
    uint32_t conclkdiv = (ADCCON3 >> 24) & 0x3Fu;
    uint32_t adcdiv    = ADCCON2 & 0x7Fu;
    if (adcdiv == 0u) adcdiv = 1u;          // 0 is reserved — defensive
    uint32_t samc      = (ADCCON2 >> 16) & 0x3FFu;
    uint32_t tadNs     = 2u * adcdiv * (conclkdiv + 1u) * 10u;
    uint64_t busyNs    = (uint64_t)nActive * (samc + 16u) * tadNs + 6000u;
    uint64_t minPeriodNs = (busyNs * 11u) / 10u;   // +10% margin
    uint32_t hz = (uint32_t)(1000000000ULL / minPeriodNs);
    // EOS-RATE limit (v3, 2026-06-12): independent of scan length, the
    // end-of-scan interrupt/task machinery is USB-FATAL when driven
    // sustained above ~11.5-12 kHz.  Silicon anchors: an n=1 scan
    // (in-spec — T_busy ~17 us << 83 us period, mid-scan retrigger
    // impossible) wedged the USB peripheral at 12,000 Hz on the plain
    // ADMITTED path, clean at 10,000 x 60 s; the n=7 scan was clean at
    // 11,500 x 3 s and wedged at 11,750.  Soak-proven: 10,425 x 120 s
    // (twice, plus the full at-cap matrix <= 10,425).  This is also why
    // pre-#541 firmware never hit it: the static 19-input scan went
    // out-of-spec above ~4.6 kHz and EOS simply DIED (#539), so the EOS
    // rate could never reach the fatal zone — the dynamic scan list
    // unlocked in-spec high-rate scans and exposed the limit.  10400 sits
    // just under 11,500-clean / 1.1 and at-or-below every endurance-
    // proven EOS rate.  Storm-vs-starvation mechanism unconfirmed: #545.
    #define ADC_EOS_RATE_MAX_HZ 10400u
    if (hz > ADC_EOS_RATE_MAX_HZ) hz = ADC_EOS_RATE_MAX_HZ;
    return (hz == 0u) ? 1u : hz;
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
// ADCxTIMEbits.SAMC  = dedicated module sample time (modules 0..4). 10-bit, max 1023.
// ADCCON2bits.SAMC   = shared MODULE7 sample time.                  10-bit, max 1023.
// Actual acquisition = (SAMC + 2) TAD.  At the boot clock config TAD7 = 100 ns:
// TCLK = 10 ns (ADCSEL=00 -> PBCLK3 = 100 MHz), TQ = (CONCLKDIV+1) x TCLK =
// 5 x 10 ns, TAD = 2 x ADCDIV x TQ = 2 x 50 ns (DS60001320H Reg 28-2/28-3;
// silicon-verified within 2% — docs/ADC_HW_SEMANTICS.md.  An earlier comment
// here claimed 50 MHz / 20 ns, a 5x misdecode of the divider chain).
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