/*! @file MC12bADC.c 
 * 
 * This file implements the functions to manage the module ADC MC12bADC. 
 */

#include "MC12bADC.h"
#include "../DIO.h"
#include "configuration.h"
#include "definitions.h"
#include "clock_config.h"   /* #487: DAQIFI_PBCLK_MHZ for ADC TCLK */
#include "../TimerApi/TimerApi.h"  /* #716: TimerApi_PeripheralClockHz() -- the LIVE PBCLK3, not the compile-time constant */
#include "state/data/BoardData.h"
#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "Util/Logger.h"
#include "FreeRTOS.h"
#include "task.h"           /* #1054: taskENTER_CRITICAL for the CalM/CalB snapshot */

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
    gSavedSTRGSRC = ADCCON1bits.STRGSRC;   // [20:16] (DFP _ADCCON1_STRGSRC)

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

    double scale = channelConfig->InternalScale;

    // #1054 (the read half of #904): CalM and CalB are 64-bit doubles, so a
    // bare read of either is two 32-bit loads on PIC32MZ (CLAUDE.md
    // atomicity rules), and a calibration writer on another task (the
    // CONF:ADC:chanCALM/chanCALB setters, or the LOADcal/USECal bulk load)
    // can land between them. #1048 makes the WRITES atomic, which does not
    // stop a reader straddling a completed write. Copy the pair under ONE
    // critical section: that closes the tear, and also reads slope and
    // offset at the same instant, so a LOADcal/USECal bulk load (which #1048
    // writes one channel's pair at a time) is seen entirely old or entirely
    // new -- reading them separately did not guarantee that. This runs once
    // per converted sample, so the section holds the loads and nothing
    // else; the arithmetic stays outside it.
    //
    // #1086: the module Range is the same 64-bit shape, so it loads in this
    // same section rather than a second one -- one more load, not another
    // interrupt-mask round trip on this per-sample path. No runtime writer
    // targets the MC12b module's Range today (CONF:ADC:RANGe stores only the
    // AD7609 slot -- ADCChanRangeSetClaimed, SCPIADC.c), so on this path it
    // is the shared-field rule applied uniformly rather than a live race.
    taskENTER_CRITICAL();
    double range = gpModuleRuntimeConfigMC12->Range;
    double calM = runtimeConfig->CalM;
    double calB = runtimeConfig->CalB;
    taskEXIT_CRITICAL();

    return (range * scale * calM * (double)rawValue) /
            (gpModuleConfigMC12->Resolution) + calB;
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
    if (n > MAX_AIN_RUNTIME_CHANNEL) {
        n = MAX_AIN_RUNTIME_CHANNEL;   // defensive: the snapshot below is a
                                       // fixed-size local, and Size is runtime
                                       // data like every other field here
    }

    /* #1112 round 3 -- PHASE 1: CAPTURE THE ENABLE FLAGS ATOMICALLY.
     *
     * The mask-building loop below used to read pRt->Data[i].IsEnabled inline,
     * one entry at a time, holding nothing: up to 48 independently-timed live
     * reads rather than one observation of the channel set. The scan list it
     * returned could therefore describe a channel combination that never
     * existed. Concrete case (Qodo /agentic_review, PR firmware#1112, round 2,
     * reported independently by two hunters, confirmed by the arbiter): idle
     * NQ1, OBDiag=0, SAMC=100, only channel 0 enabled. A WiFi CONF:CAP:JSON?
     * enters here and reads channel 0 as enabled; the USB task then runs
     * `CONF:ADC:CHAN 2` to completion, which CLEARS channel 0 and SETS channel
     * 1, in that order -- so the true sequence of enabled-sets is {0}, {}, {1},
     * never {0,1}. The resumed loop reads channel 1 as enabled too and reports
     * BOTH, and the response's scan_offset_ticks for channel 1 comes out a
     * whole (SAMC+16)*TAD7 scan-position step wrong.
     *
     * Deliberately NOT Streaming_BeginConfigChange(): a read-only diagnostic
     * query must never BLOCK or be REFUSED under contention with a legitimate
     * config writer -- the same trade MC12b_ChannelScanOffsetTicks' comment
     * below records for its own residual. A bounded <=48-field-read critical
     * section costs microseconds and refuses nothing.
     *
     * This is the READER half only. The WRITER half is the matching section
     * around the bulk-mask loop in ADCChanEnableSetClaimed (SCPIADC.c), which
     * applies up to 16 per-channel stores: without it this snapshot can still
     * land strictly INSIDE that loop and capture an intermediate enabled-set
     * the operator never commanded. Neither half is sufficient alone -- the
     * same pairing #1048/#1054 state for CalM/CalB and #1086 states for the
     * AD7609 Range (ADCChanRangeSetClaimed / SCPI_ADCChanRangeGet, SCPIADC.c).
     *
     * The section holds field reads and nothing else -- no logging, no SCPI
     * parsing, no blocking call -- matching MC12b_ConvertToVoltage's CalM/CalB
     * section above. Task context only: every caller is a SCPI callback, the
     * streaming start/stop path, or boot init; none is an ISR, so
     * taskENTER_CRITICAL (not the FROM_ISR form) is the right primitive. */
    bool enabledSnapshot[MAX_AIN_RUNTIME_CHANNEL];
    taskENTER_CRITICAL();
    for (size_t i = 0; i < n; i++) {
        enabledSnapshot[i] = pRt->Data[i].IsEnabled;
    }
    taskEXIT_CRITICAL();

    /* PHASE 2: pure computation over the snapshot. No live read of
     * pRt->Data[].IsEnabled occurs below this line -- that is the property the
     * $(SCAN1112_BIN) guard in tests/host/Makefile pins. */
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
            if (!enabledSnapshot[i]) continue;
        } else if (enabledOnly && !enabledSnapshot[i]) {
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
            LOG_E("ADC CSS update: UPDRDY timeout - scan list unchanged");
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

/* Shared-MODULE7 ADC clock period TAD7, in nanoseconds (rounded UP — see
 * below), derived live from the SFRs:
 *   TAD7 = 2 x ADCDIV x TQ;  TQ = (CONCLKDIV + 1) x TCLK;  TCLK = 1/PBCLK3
 * ADCDIV is ADCCON2<6:0>; CONCLKDIV is ADCCON3<29:24> (Qodo /agentic_review,
 * PR firmware#1112, round 2, "ADC timing math is hard to verify" — both
 * bitfields named explicitly here, not only by register number). DS60001320H
 * Reg 28-2 (ADCCON2) / Reg 28-3 (ADCCON3) — the EF datasheet deviates from
 * the FRM (DS60001344E §22 "12-bit High-Speed SAR ADC") on CONCLKDIV
 * semantics and the DATASHEET is what matches silicon; the full chain is
 * worked in docs/ADC_HW_SEMANTICS.md.
 *
 * #716/#487, via TimerApi_PeripheralClockHz() (Qodo /agentic_review, PR
 * firmware#1112, "Clock mismatches corrupt timing offsets"): TCLK comes from
 * the LIVE PBCLK3 the silicon actually runs, not the compile-time
 * DAQIFI_PBCLK_MHZ. On a unit whose PLL configuration word disagrees with the
 * build (clock_ok=false — the bootloader cannot reprogram it, erratum 45; see
 * CLAUDE.md and TimerApi.c), the OLD compile-time TAD was wrong by the same
 * ratio pbclk_hz/pbclk_built_hz that every other streaming-rate computation
 * was already fixed for by #716 (TimerApi_FrequencyGet's own comment: "Every
 * streaming-rate computation funnels through this function ... making it
 * honest here fixes the rate, the reported timebase and the caps together").
 *
 * BEHAVIOUR-PRESERVING ON EVERY CLOCK-MATCHED UNIT (the overwhelming common
 * case, clock_ok=true): TimerApi_PeripheralClockHz() returns EXACTLY
 * DAQIFI_PBCLK_MHZ*1_000_000 there, and scaling both the ceil-divide's
 * numerator and denominator by the same positive constant (1e6) leaves the
 * quotient identical — verified algebraically, not merely argued: old
 * tadNumer/DAQIFI_PBCLK_MHZ (numerator scaled x1000 for ns) is the exact
 * same ratio as new tadNumer/pbclkHz (numerator scaled x1e9 for ns, pbclkHz
 * = DAQIFI_PBCLK_MHZ*1e6). Only a clock-mismatched unit's result moves, and
 * it moves toward the true value.
 *
 * The divide rounds UP (ceil) so tadNs never UNDER-estimates TAD — the
 * conservative direction for MC12b_HardwareScanMaxFreq's safety cap below
 * (over-estimating busy time -> a lower, safer max freq; Qodo #584).
 *
 * ONLY MC12b_HardwareScanMaxFreq uses this whole-nanosecond, rounded form.
 * MC12b_ChannelScanOffsetTicks (below) does NOT call this helper: rounding
 * TAD7 up before multiplying by pos x (SAMC+16) compounds the rounding error
 * pos times, which is itself a confirmed defect (Qodo /agentic_review, PR
 * firmware#1112, round 1, "Rounding each ADC clock period introduces
 * cumulative timestamp error") — that function instead reads
 * adcdiv/conclkdiv/pbclkHz from its own MC12b_ScanTimingSnapshot and carries
 * the UN-rounded rational value through to one final division, so its
 * reported offset is not an upper bound the way the busy-time cap is; it is
 * the nearest-below-exact tick count. */
static uint32_t MC12b_SharedTadNs(void) {
    uint32_t conclkdiv = ADCCON3bits.CONCLKDIV;   // [29:24]
    uint32_t adcdiv    = ADCCON2bits.ADCDIV;       // [6:0]
    if (adcdiv == 0u) adcdiv = 1u;          // 0 is reserved — defensive
    uint32_t pbclkHz = TimerApi_PeripheralClockHz();
    if (pbclkHz == 0u) return 0xFFFFFFFFu;  // defensive: unmeasurable clock —
                                             // MC12b_HardwareScanMaxFreq below
                                             // folds this straight into its
                                             // own busy-time bound, which
                                             // saturates the same direction
                                             // (a huge TAD -> a lower max
                                             // freq, never a higher one)
    /* 64-bit: 2*adcdiv*(conclkdiv+1) can reach ~2*127*64 = 16256, and the
     * numerator is now scaled x1e9 (ns from a Hz denominator) rather than
     * x1000 (ns from a MHz one) — 16256*1e9 ~= 1.6e13 overflows uint32_t. */
    uint64_t tadNumer = 2ULL * adcdiv * (conclkdiv + 1u) * 1000000000ULL;
    return (uint32_t)((tadNumer + pbclkHz - 1u) / pbclkHz);
}

/* #563/#557: the SAMC/divider-dependent hardware scan-busy limit ALONE — the
 * real #539 bound (retriggering MODULE7 mid-conversion is documented-undefined,
 * FRM §22.3.2). Extracted from MC12b_ScanMaxFreq so the NQ1 freeze-aware additive
 * cap can min() with it directly: the additive model intentionally replaces the
 * EOS-rate/event-rate terms (re-tested non-fatal on v3.6.1, #557) but NOT this
 * one, which must scale with the live SAMC/TAD config. All terms read live:
 *   TAD7 = 2 x ADCDIV x TQ;  TQ = (CONCLKDIV+1) x TCLK, TCLK = 1000/84 ns (PBCLK3 84MHz, #487)
 *          — see MC12b_SharedTadNs above, which is where that is now computed.
 *   T_busy = N x (SAMC + 16) x TAD7 + ~6us;  cap = 1 / (T_busy x 1.1).
 * Returns 0xFFFFFFFF when no scan is armed (no bound). */
uint32_t MC12b_HardwareScanMaxFreq(uint32_t nActive) {
    if (nActive == 0u) return 0xFFFFFFFFu;  // no scan armed — no bound
    uint32_t samc      = ADCCON2bits.SAMC;         // [25:16]
    uint32_t tadNs     = MC12b_SharedTadNs();
    uint64_t busyNs    = (uint64_t)nActive * (samc + 16u) * tadNs + 6000u;
    uint64_t minPeriodNs = (busyNs * 11u) / 10u;   // +10% margin
    uint32_t hz = (uint32_t)(1000000000ULL / minPeriodNs);
    return (hz == 0u) ? 1u : hz;
}

/* Population count over a 32-bit scan mask.  Written out rather than
 * __builtin_popcount: this runs on a SCPI query path (once per public channel,
 * never per sample), so the loop costs nothing worth a builtin, and the code
 * stays independent of the toolchain's builtin set. */
static uint32_t MC12b_CountSetBits(uint32_t v) {
    uint32_t n = 0u;
    while (v != 0u) {
        v &= (v - 1u);      // clear the lowest set bit
        n++;
    }
    return n;
}

/* #267/#1112 round-1: one-shot read of the SAMC/clock-divider state, taken
 * ONCE by the caller before the per-channel emission loop (Qodo
 * /agentic_review, PR firmware#1112, round 1, "Snapshot SAMC before emitting
 * channel offsets" — see the .h-file comment for the full reachability
 * argument: a CONF:ADC:SAMC:SHARed setter is only rejected #116 MID-STREAM,
 * so it is NOT blocked while a CONF:CAP:JSON? query is idle-time emitting the
 * channels[] array, and MC12b_ChannelScanOffsetTicks used to reread
 * ADCCON2.SAMC and the ADCCON2/ADCCON3 dividers behind MC12b_SharedTadNs on
 * EVERY call — so two channels in the SAME response could each be scored
 * against a different SAMC). Deliberately reads adcdiv/conclkdiv/pbclkHz
 * itself rather than delegating to MC12b_SharedTadNs(): that helper rounds
 * TAD7 UP to a whole nanosecond for MC12b_HardwareScanMaxFreq's safety-cap
 * use (intentional there, Qodo #584 — an over-estimate makes the cap more
 * conservative); MC12b_ChannelScanOffsetTicks needs the UN-rounded rational
 * value so pos x (SAMC+16) x TAD7 doesn't compound that rounding pos times
 * (Qodo /agentic_review, PR firmware#1112, round 1, "Rounding each ADC clock
 * period introduces cumulative timestamp error" — see below). */
MC12b_ScanTimingSnapshot MC12b_CaptureScanTiming(void) {
    MC12b_ScanTimingSnapshot snap;
    snap.samc      = ADCCON2bits.SAMC;
    snap.adcdiv    = ADCCON2bits.ADCDIV;
    snap.conclkdiv = ADCCON3bits.CONCLKDIV;
    snap.pbclkHz   = TimerApi_PeripheralClockHz();
    return snap;
}

uint32_t MC12b_ChannelScanOffsetTicks(const AInChannel* ch,
                                      uint32_t css1, uint32_t css2,
                                      const MC12b_ScanTimingSnapshot* timing,
                                      uint32_t timestampHz) {
    if (ch == NULL || timestampHz == 0u || timing == NULL) return 0u;

    /* AD7609 (NQ2/NQ3) converts all 8 inputs simultaneously — no scan-order
     * skew, and no MODULE7 scan to be positioned within. */
    if (ch->Type != AIn_MC12bADC) return 0u;

    /* Type 1 / Class 1: dedicated S&H per input.  "When a trigger occurs, all
     * Class 1 inputs are captured simultaneously and conversions are started
     * simultaneously" — DS60001344E §22.3.2 "Input Scan", p.22-64.  Offset is
     * structurally 0, regardless of what else is in the scan list. */
    if (ch->Config.MC12b.ChannelType == MC12B_CHANNEL_TYPE_DEDICATED) return 0u;

    uint32_t an = (uint32_t)ch->Config.MC12b.ChannelId;   // CSS bit == AN number
    if (an >= 64u) return 0u;    // defensive: outside ADCCSS1/2 (see ComputeScanList)

    /* css1/css2: the scan list a session would arm RIGHT NOW, PASSED IN by the
     * caller rather than recomputed here (Qodo /agentic_review, PR
     * firmware#1112, "Channel timing can describe wrong scan"). This used to
     * call MC12b_ComputeScanList() itself, independently, on EVERY channel —
     * so if the enabled-channel set or OnboardDiagEnabled changed on the
     * OTHER SCPI transport between one channel's call and the next, two
     * channels in the SAME capability response could be positioned against
     * two different scans, and neither would necessarily agree with
     * cap_terms.scan_bound_hz either (computed earlier in the same query via
     * Streaming_ComputeMaxFreqTermsForConfigIface's own, separate,
     * independently-timed call). Taking ONE snapshot before the per-channel
     * loop and reusing it for every channel closes the CROSS-CHANNEL half of
     * that window entirely — the response's channels[] array is now always
     * self-consistent. It does not close the narrower window against
     * scan_bound_hz itself: that term is computed by a different function,
     * earlier in the same query, and unifying the two would mean either that
     * function taking a snapshot parameter (an API change to a
     * frequently-called streaming-cap path, out of scope here) or the whole
     * query taking the streaming config-change claim (which would make a
     * read-only diagnostic query BLOCK or GET REFUSED under contention with a
     * legitimate config writer — a worse trade for a capability endpoint than
     * the narrow, self-correcting-on-the-next-query inconsistency it would
     * prevent). See docs/ADC_HW_SEMANTICS.md for the accepted residual. */
    bool inList = (an < 32u) ? (((css1 >> an) & 1u) != 0u)
                             : (((css2 >> (an - 32u)) & 1u) != 0u);
    if (!inList) return 0u;      // not scanned in this configuration

    /* Scan position = how many OTHER armed inputs convert BEFORE this one.
     *
     * V (DS60001344E §22.3.2 "Input Scan", p.22-64): "For Class 2 or Class 3
     * inputs, the sampling and conversion occur in the natural input order is
     * used; lower number inputs are sampled before higher number inputs."
     * (sic — the sentence is garbled in the FRM, the meaning is not.)  §22.3
     * p.22-63 states the same rule for the shared module generally: "the ADC
     * module is used to convert the next in line Class 2 or Class 3 inputs,
     * according to the natural order of priority", where "AN7 has a higher
     * priority than AN12".  So the CSS bit index IS the conversion order, and
     * counting set bits below this input's bit gives its position.
     *
     * The one documented way this order can be perturbed is an INDIVIDUAL
     * Class 2 trigger pre-empting the scan (§22.3.2 / Figure 22-8, p.22-64) —
     * not reachable here: MC12b_ConfigureHardwareTrigger sets every scanned
     * Class 1/2 input's ADCTRGx TRGSRC to STRIG, so no input has an
     * independent trigger while a session is armed. */
    uint32_t pos;
    if (an < 32u) {
        pos = MC12b_CountSetBits(css1 & ((1U << an) - 1U));
    } else {
        /* every ADCCSS1 input is a lower AN number than any ADCCSS2 input */
        pos = MC12b_CountSetBits(css1)
            + MC12b_CountSetBits(css2 & ((1U << (an - 32u)) - 1U));
    }

    /* clockTerm x pbclkHz-denominator carries TAD7 UN-rounded through every
     * multiply, so the one unavoidable floor happens ONCE, at the very end,
     * instead of a whole-nanosecond ceil(TAD7) compounding pos times (Qodo
     * /agentic_review, PR firmware#1112, round 1, "Rounding each ADC clock
     * period introduces cumulative timestamp error" — ~415 ticks / ~9.9us at
     * SAMC=1023 from the old ceil-then-multiply order; e.g. TAD7 = 119.0476ns
     * on the shipped default clocks is EXACTLY 5 timestamp ticks, so an exact
     * integer answer exists and no rounding was structurally required).
     *   TAD7[ns]      = 2 x adcdiv x (conclkdiv+1) x 1e9 / pbclkHz
     *                   (adcdiv = ADCCON2<6:0> ADCDIV, conclkdiv =
     *                   ADCCON3<29:24> CONCLKDIV — DS60001320H Reg 28-2/28-3;
     *                   see MC12b_SharedTadNs above for the full FRM-vs-
     *                   datasheet citation)
     *   ticks(pos)    = [pos x (SAMC+16) + (SAMC+2)] x TAD7[ns] x timestampHz
     *                   / 1e9
     *                 = [pos x (SAMC+16) + (SAMC+2)]
     *                   x [2 x adcdiv x (conclkdiv+1)] x timestampHz
     *                   / pbclkHz                        (the 1e9 cancels)
     * 64-bit is required and sufficient: worst case pos<=~50, (SAMC+16)<=1039
     * so the bracket <=~53000, clockTerm<=2*127*64=16256, timestampHz a few
     * hundred MHz — the product stays under 2^63 (verified:
     * 53000*16256*4e8 ~= 3.4e17 << 9.2e18). */
    uint32_t adcdiv = (timing->adcdiv == 0u) ? 1u : timing->adcdiv; // 0 reserved
    if (timing->pbclkHz == 0u) return 0xFFFFFFFFu;  // unmeasurable clock —
                                                     // saturate like every
                                                     // other clamp here
    uint64_t clockTerm = 2ULL * adcdiv * (timing->conclkdiv + 1u);

    /* Unified model — EVERY scanned shared/Type-2 position, including
     * position 0, carries its own (SAMC+2) x TAD7 acquisition aperture ON
     * TOP OF the pos x (SAMC+16) x TAD7 slots consumed by the channels
     * ahead of it (Qodo /agentic_review, PR firmware#1112, round 2,
     * "Clients place later samples too early", confirmed by this project's
     * own re-derivation from first principles — matches Qodo's independently
     * recommended formula exactly).
     *
     * Round 1 folded the aperture into position 0 ONLY and left pos>=1 at
     * `pos * (SAMC+16) * TAD7` alone, on the theory that pos>=1 was already
     * self-consistent among the shared channels — true internally, but
     * WRONG relative to the Type-1-zero baseline every position is meant to
     * share. V — DS60001344E §22.3.2 Figure 22-7 + Equation 22-2: the
     * (SAMC+2) x TAD acquisition delay is the instant THIS channel's OWN
     * value is latched (Hold begins) — it applies to every shared channel's
     * own Hold instant, not only the first one's. A client reconstructing
     * position k's true capture instant as `packetTimestamp + offsetTicks`
     * was therefore short by the whole aperture (~510 ticks at the shipped
     * default clocks) for every channel after the first.
     *
     * Per-input step = the same (SAMC + 16) x TAD7 term the scan-busy bound
     * uses: (SAMC + 2) TAD acquisition + ~14 TAD conversion/handoff.
     *
     * #1112 round 3: that 14-TAD figure is IMPORTED from
     * MC12b_ScanMaxFreq's deliberately conservative scan-busy bound above
     * (MC12bADC.c:660-700 — over-estimates busy time on purpose, plus a 10%
     * margin, because operating at that boundary is fatal, #539/#543), not
     * independently measured for this exact-timestamp use. An earlier
     * revision of this comment (and docs/ADC_HW_SEMANTICS.md) cited the
     * SAMC-sweep fit + silicon anchors as confirming 14 TAD specifically
     * ("E confirming V") — re-examined during round 3's adversarial audit,
     * that same fit does NOT discriminate a 13-TAD (K=15 step) from a
     * 14-TAD (K=16 step) conversion/handoff constant: solving the two
     * anchors for the per-scan fixed term independently at each candidate
     * gives ~0.25us (K=16) vs ~2.1us (K=15), both outside the ~5.4-7.3us the
     * n=7 anchor demands on its own. So this is an I (inference), not an
     * E-confirmed V, per CLAUDE.md's V/E/I/X/N discipline — reusing a
     * safety-biased conservative term as an exact per-channel timestamp
     * inherits that bias as a systematic error of up to ~1 TAD7 (5
     * timestamp ticks, ~119ns at the shipped default clocks) per scan
     * position, cumulative (~15 ticks at position 3, ~90 ticks/~2.1us at
     * the tail of a 19-input scan). See docs/ADC_HW_SEMANTICS.md's Evidence
     * class note (same section) and #1117 for the direct per-position
     * measurement that would resolve K=15 vs K=16 for real. The per-SCAN
     * fixed term (~6 us) still deliberately does NOT appear here: it is
     * paid once per scan, so it shifts the whole scan (T1 and T2 alike)
     * rather than being a cross-class skew the way the aperture is. */
    uint64_t numer = ((uint64_t)pos * (timing->samc + 16u) + (timing->samc + 2u))
                    * clockTerm * (uint64_t)timestampHz;
    uint64_t ticks = numer / (uint64_t)timing->pbclkHz;
    return (ticks > 0xFFFFFFFFULL) ? 0xFFFFFFFFu : (uint32_t)ticks;
}

uint32_t MC12b_ScanMaxFreq(uint32_t nActive, uint32_t nUserT2) {
    // #557/#563 SCOPE: on NQ1 this whole function is SUPERSEDED — the enforced
    // cap uses Streaming_AdcAdditiveCap_NQ1() min()'d only with the scan-busy
    // term MC12b_HardwareScanMaxFreq() (see Streaming_ComputeMaxFreqForConfigIface).
    // MC12b_ScanMaxFreq() is called only for NQ2/NQ3 (AD7609 — no MODULE7 scan,
    // different timing), which still use the legacy formula below. The EOS-rate
    // and event-rate terms it applies were NEVER a silicon limit: the "USB-fatal"
    // symptom that motivated them (#544/#545) was root-caused (#557) to the #525
    // EOS-task vsnprintf stack overflow, fixed in v3.6.1 (PR #551) and re-tested
    // GONE. They are retained here as conservative NQ2/NQ3 placeholders pending a
    // per-variant headroom review; the scan-busy term below is the only one that
    // reflects a real (FRM-documented #539) hardware bound.
    //
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
    // Scan-busy hardware limit (SAMC/divider-dependent) — extracted to
    // MC12b_HardwareScanMaxFreq (#563) so the NQ1 additive cap can reuse it.
    uint32_t hz = MC12b_HardwareScanMaxFreq(nActive);
    // EOS-RATE limit (v3, 2026-06-12; NQ2/NQ3-only since #563 — see banner):
    // independent of scan length. Was believed USB-fatal above ~11.5-12 kHz;
    // #557 re-root-caused that to the #525 vsnprintf overflow (not silicon),
    // fixed v3.6.1. Retained as a conservative NQ2/NQ3 bound.  Bench anchors: an n=1 scan
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
    // AGGREGATE ADC-event-rate limit (v4, 2026-06-12; NQ2/NQ3-only since #563):
    // a third threshold once believed independently fatal.  Each enabled T2 USER
    // channel fires a per-conversion data-ready ISR in addition to the per-scan
    // EOS, and the combined event rate  f x (nUserT2 + 1)  was seen "fatal"
    // around ~66-72k events/s (also the #525 overflow, #557 — not silicon):
    // 11xT2 OBDiag=0 wedges at 6,000 Hz on the plain ADMITTED path
    // (12 events/tick = 72k/s) and at 6,750 NOCAP (81k/s), clean at
    // 5,500 x 60 s (66k/s); the 16-channel cell is endurance-proven at
    // 5,000 x 120 s (60k/s).  Neither the EOS-rate nor the scan-busy
    // bound catches this (EOS here is only 6 kHz; scan 10% under busy).
    // Bound at the 120 s-proven 60k events/s.  Monitoring channels have
    // no data-ready ISRs and do not count.  Mechanism: #545.
    #define ADC_EVENT_RATE_MAX_PER_S 60000u
    if (nUserT2 > 0u) {
        uint32_t aggMax = ADC_EVENT_RATE_MAX_PER_S / (nUserT2 + 1u);
        if (hz > aggMax) hz = aggMax;
    }
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

    // MODULE7 scan trigger (ADCCON1.STRGSRC [20:16]): controls when the
    // shared module starts its multiplexed scan of all enabled shared
    // channels. The bitfield assignment is the same read-clear-set-store
    // as the prior manual RMW; scanSrc is always a valid 5-bit trigger
    // source (ADC_TRGSRC_TMR5 or the saved default read from this field).
    uint32_t scanSrc = hwShared ? ADC_TRGSRC_TMR5 : gSavedSTRGSRC;
    ADCCON1bits.STRGSRC = scanSrc;
}

// Query functions read SFR registers directly — the register value IS
// the authoritative state.  No shadow variables needed.
bool MC12b_IsHwTriggerDedicated(void) {
    // Check first dedicated channel (AN0) — all Type 1 channels are set
    // together so any one is representative.
    return ADCTRG1bits.TRGSRC0 == ADC_TRGSRC_TMR5;   // [4:0]
}

bool MC12b_IsHwTriggerShared(void) {
    return ADCCON1bits.STRGSRC == ADC_TRGSRC_TMR5;   // [20:16]
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