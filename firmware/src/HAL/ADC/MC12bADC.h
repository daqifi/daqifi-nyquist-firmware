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

#ifdef	__cplusplus
}
#endif


