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
 * #541 D-C: max in-spec scan trigger rate (Hz) for an nActive-input scan,
 * computed from live SAMC / clock-divider registers.  Returns UINT32_MAX
 * when nActive == 0 (no scan armed — no bound).
 */
uint32_t MC12b_ScanMaxFreq(uint32_t nActive);

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


