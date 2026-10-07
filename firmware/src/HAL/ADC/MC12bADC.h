/* 
 * File:   MC12bADC.h
 * Author: Daniel
 *
 * This file implements the functions to manage the module ADC AD7609. 
 */

#pragma once

#include "state/board/AInConfig.h"
#include "state/runtime/AInRuntimeConfig.h"
#include "state/data/AInSample.h"


#ifdef	__cplusplus
extern "C" {
#endif
    typedef enum{
        MC12B_ADC_TYPE_SHARED,
        MC12B_ADC_TYPE_DEDICATED,  
        MC12B_ADC_TYPE_ALL,        
    } MC12b_adcType_t;
/*!
 * Performs board initialization
 * @param[in] pModuleConfigInit Pointer to module configuration
 * @param[in] pModuleRuntimeConfigInit Pointer to module configuration in 
 *                                     runtime
 * @return true on success
 */
bool MC12b_InitHardware( MC12bModuleConfig* pModuleConfigInit,          
                     AInModuleRuntimeConfig * pModuleRuntimeConfigInit);

/*!
 * Updates the module state based on the provided config
 */
bool MC12b_WriteModuleState( void );

/*!
 * Sets the state for all ADC channels
 * @param[in] channelConfig Pointer to channel configuration
 * @param[in] channelRuntimeConfig Pointer to channel configuration in runtime
 */
bool MC12b_WriteStateAll(                                                   
                        const AInArray* channelConfig,                      
                        AInRuntimeArray* channelRuntimeConfig);
    
/*!
 * Updates the state for a single ADC channel
 * @param[] channelConfig       Pointer to channel configuration
 * @param[] channelRuntimeConfig Pointer to channel configuration in runtime
 */
bool MC12b_WriteStateSingle(                                                
                        const MC12bChannelConfig* channelConfig,            
                        AInRuntimeConfig* channelRuntimeConfig); 
    
/*!
 * Populates the sample array using data in the board config
 * @param[in/out] samples The array to populate
 * @param[in] channelConfig The static channel configuration for the board
 * @param[in] channelRuntimeConfig The runtime channel configuration for the board
 * @param[in] triggerTimeStamp The timestamp when the module was most recently triggered to convert
 */
bool MC12b_ReadSamples( AInSampleArray* samples,                            
                        const AInArray* channelConfig,                      
                        AInRuntimeArray* channelRuntimeConfig,              
                        uint32_t triggerTimeStamp);
    
/**
 * Triggers a conversion
 * @return true on success, false otherwise
 */
    bool MC12b_TriggerConversion( AInRuntimeArray* pRunTimeChannlConfig, AInArray* pAIConfigArr, MC12b_adcType_t type);

/**
 * Calculates a voltage based on the given sample
 * NOTE: This is NOT safe to call in an ISR
 * @param[in] channelConfig Information about the channel
 * @param[in] runtimeConfig Runtime channel information
 * @param[in] rawValue Raw ADC code
 * @return The converted voltage
 */
double MC12b_ConvertToVoltage(
                        const MC12bChannelConfig* channelConfig,
                        const AInRuntimeConfig* runtimeConfig,
                        uint32_t rawValue);
bool MC12b_ReadResult(ADCHS_CHANNEL_NUM channel, uint32_t *pVal);

/**
 * #541 D-A: read-and-discard any pending Type 1 (dedicated-module) results
 * so a stale conversion can't satisfy the first ARDY-gated direct read of a
 * new streaming session. Call from Streaming_Start before arming the timer.
 */
void MC12b_DrainType1Results(void);

/**
 * #541 D-B: compute a shared-scan (ADCCSS) list from the board + runtime
 * config.  Scanned inputs = Type 2 / monitoring MC12bADC channels; the dead
 * temp sensor (AN44, erratum 18) is always excluded.
 *
 * @param enabledOnly        true = only IsEnabled public T2 channels
 *                           (session list); false = all public T2 (idle list)
 * @param includeMonitoring  include enabled monitoring channels
 * @param pCss1/pCss2        [out, may be NULL] ADCCSS1/2 register values
 * @return number of inputs in the list
 */
uint32_t MC12b_ComputeScanList(bool enabledOnly, bool includeMonitoring,
                               uint32_t *pCss1, uint32_t *pCss2);

/**
 * #541 D-B: write ADCCSS1/2 via the FRM-documented online-update sequence
 * (TRGSUSP -> UPDRDY poll -> write -> resume). No-op if values are current.
 */
void MC12b_ApplyScanList(uint32_t css1, uint32_t css2);

/** #541 D-B: restore the idle scan list (all public T2 + enabled monitoring). */
void MC12b_RestoreIdleScanList(void);

/**
 * #541 D-C: max safe scan trigger rate (Hz) — min of the scan-busy bound
 * (nActive-input scan time from live SAMC / clock-divider registers), the
 * EOS-rate ceiling, and the aggregate ADC-event-rate ceiling
 * (rate x (nUserT2 ARDY ISRs + 1 EOS) <= proven-safe events/s).
 * Returns UINT32_MAX when nActive == 0 (no scan armed — no bound).
 *
 * @param nActive  session scan-list length (enabled T2 + monitoring-if-OBDiag)
 * @param nUserT2  enabled T2 USER channels (each fires a per-conversion
 *                 data-ready ISR; monitoring channels do not)
 */
uint32_t MC12b_ScanMaxFreq(uint32_t nActive, uint32_t nUserT2);

/*! #563: the SAMC/divider-dependent hardware scan-busy limit only (no EOS/event
 *  caps) — for the NQ1 freeze-aware additive cap to min() with, so a non-default
 *  SAMC can't push the cap above the real scan-retrigger limit (#539).
 *  Returns UINT32_MAX when nActive == 0. */
uint32_t MC12b_HardwareScanMaxFreq(uint32_t nActive);

/**
 * #267/#1112 round-1: one-shot snapshot of the SAMC/clock-divider state that
 * MC12b_ChannelScanOffsetTicks needs, taken ONCE by the caller — alongside its
 * css1/css2 scan-list snapshot — via MC12b_CaptureScanTiming() below, instead
 * of each call re-reading ADCCON2.SAMC / ADCCON2.ADCDIV / ADCCON3.CONCLKDIV /
 * the live PBCLK3 from hardware (Qodo /agentic_review, PR firmware#1112,
 * round 1, "Snapshot SAMC before emitting channel offsets": a
 * CONF:ADC:SAMC:SHARed setter on the OTHER SCPI transport is NOT rejected
 * while idle — #116 only rejects it mid-STREAM — so it could land between two
 * channels of the same CONF:CAP:JSON? response and make their offsets
 * describe two different scans).
 */
typedef struct {
    uint32_t samc;       ///< ADCCON2bits.SAMC, [0..1023]
    uint32_t adcdiv;      ///< ADCCON2bits.ADCDIV (0 treated as 1 — reserved)
    uint32_t conclkdiv;    ///< ADCCON3bits.CONCLKDIV
    uint32_t pbclkHz;      ///< TimerApi_PeripheralClockHz(), live PBCLK3
} MC12b_ScanTimingSnapshot;

/**
 * #267/#1112 round-1: read the SAMC/clock-divider state ONCE. Call before the
 * per-channel emission loop, exactly like the existing
 * MC12b_ComputeScanList(true, includeMonitoring, ...) css1/css2 snapshot, and
 * pass the SAME struct to every MC12b_ChannelScanOffsetTicks() call in that
 * response.
 */
MC12b_ScanTimingSnapshot MC12b_CaptureScanTiming(void);

/**
 * #267: a channel's DETERMINISTIC intra-scan conversion offset, expressed in
 * timestamp-timer ticks (the `timestamp_hz` domain the capability document and
 * SYSTem:SYSInfoPB? already publish) — NOT in ADC TAD or nanoseconds, so a
 * caller never has to know anything about the ADC clock tree.
 *
 * Every channel in a sample set carries the same timestamp (the acquisition
 * TRIGGER instant, #729/#722), but shared-MODULE7 (Type 2) inputs convert
 * SEQUENTIALLY inside one scan, in ascending AN order — so input k's conversion
 * lands this many ticks after the scan trigger.  The offset is fixed by the
 * ADCHS configuration and the armed scan list, not measured, which is why it is
 * reported once per session rather than per sample.
 *
 * Returns 0 for: Type 1 (dedicated S&H — simultaneous, FRM §22.3.2), AD7609
 * channels, and any channel NOT in the scan the current configuration would
 * arm. EVERY scanned shared/Type-2 channel — including the first — carries
 * its own (SAMC+2) x TAD7 acquisition aperture (Equation 22-2) on top of the
 * pos x (SAMC+16) x TAD7 slots consumed by the channels ahead of it (fixed
 * Qodo /agentic_review, PR firmware#1112: round 1, "First shared channel
 * incorrectly receives the dedicated-channel timestamp offset" — position 0
 * only; then round 2, "Clients place later samples too early" — round 1 had
 * left positions >= 1 short by that same aperture, an inconsistency this
 * project's own re-derivation independently confirmed). Unlike a Type 1
 * input, no shared/Type-2 position is captured at the trigger instant:
 * DS60001344E §22.3.2 Figure 22-7 has the trigger START the shared S&H's own
 * acquisition, which must still elapse before the value is latched (Hold
 * begins) — for EVERY position, not only the first. See
 * MC12b_ChannelScanOffsetTicks' .c-file comment for the full derivation.
 *
 * PURE given its inputs: css1/css2 and *timing are the caller's OWN snapshots
 * — ONE MC12b_ComputeScanList(true, includeMonitoring, ...) call and ONE
 * MC12b_CaptureScanTiming() call, both taken once before looping over
 * channels (Qodo /agentic_review, PR firmware#1112: "Channel timing can
 * describe wrong scan" round 1, then "Snapshot SAMC before emitting channel
 * offsets" round 1) — NOT recomputed/reread per channel, so every channel in
 * one capability response is positioned against the exact same scan AND the
 * exact same acquisition timing, even if the enabled-channel set,
 * OnboardDiagEnabled, or SAMC changes on the OTHER SCPI transport between two
 * channels' calls in the same response.
 *
 * @param ch          board-config channel entry (NULL -> 0)
 * @param css1, css2  ONE MC12b_ComputeScanList(true, includeMonitoring, ...)
 *                    snapshot, shared across every channel in one response
 * @param timing      ONE MC12b_CaptureScanTiming() snapshot, shared across
 *                    every channel in one response (NULL -> 0)
 * @param timestampHz timestamp-timer tick rate (0 -> 0)
 */
uint32_t MC12b_ChannelScanOffsetTicks(const AInChannel* ch,
                                      uint32_t css1, uint32_t css2,
                                      const MC12b_ScanTimingSnapshot* timing,
                                      uint32_t timestampHz);

/**
 * Returns bitmask of enabled Type 1 ADCHS channels (bits 0-4).
 */
uint32_t MC12b_GetType1EnabledMask(void);

/**
 * Configure hardware-triggered ADC conversion via Timer4/5 match event.
 * When enabled, the streaming timer directly triggers ADC modules without
 * software intervention, eliminating inter-channel skew.
 * Call with (false, false) to revert to software triggering.
 * @param hwDedicated  true = dedicated modules (0-4) triggered by TMR5 match
 * @param hwShared     true = shared MODULE7 scan triggered by TMR5 match
 */
void MC12b_ConfigureHardwareTrigger(bool hwDedicated, bool hwShared);

/** Query whether hardware triggering is active for dedicated/shared modules. */
bool MC12b_IsHwTriggerDedicated(void);
bool MC12b_IsHwTriggerShared(void);

/**
 * Set ADC sample-time (SAMC) for dedicated modules (ADC0-4) and the shared
 * MODULE7. Higher SAMC = longer acquisition window = lower noise at the cost
 * of per-scan time. Range: 0-1023 ADC clock cycles (actual acquisition time
 * is SAMC+2 clocks).
 *
 * NOT safe to call while streaming — caller must stop streaming first. The
 * ADC is briefly taken offline to apply the new values.
 *
 * Pass a negative value for either parameter to leave that channel type
 * unchanged.
 *
 * @param samcDedicated 0-1023, or negative to skip
 * @param samcShared    0-1023, or negative to skip
 * @return true on success, false if args out of range
 */
bool MC12b_SetAcquisitionSamc(int32_t samcDedicated, int32_t samcShared);

/** Read current SAMC values. out* may be NULL. */
void MC12b_GetAcquisitionSamc(uint16_t* outSamcDedicated, uint16_t* outSamcShared);

// Boot-default SAMC is 100 for BOTH dedicated and shared modules
// (ADCxTIME / ADCCON2=0x00642001 -> SAMC=0x64; the former
// MC12B_SAMC_SHARED_DEFAULT=1 constant here was an unused misdecode).
#define MC12B_SAMC_MAX                1023

#ifdef	__cplusplus
}
#endif


