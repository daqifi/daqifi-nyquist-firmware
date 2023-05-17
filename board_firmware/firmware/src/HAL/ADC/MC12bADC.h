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

/*!
 * Performs board initialization
 * @param[in] pModuleConfigInit Pointer to module configuration
 * @param[in] pModuleRuntimeConfigInit Pointer to module configuration in 
 *                                     runtime
 * @return true on success
 */
bool MC12b_InitHardware(const MC12bModuleConfig* pModuleConfigInit,          \
                    const AInModuleRuntimeConfig * pModuleRuntimeConfigInit);

/*!
 * Updates the module state based on the provided config
 */
bool MC12b_WriteModuleState( void );

/*!
 * Sets the state for all ADC channels
 * @param[in] channelConfig Pointer to channel configuration
 * @param[in] channelRuntimeConfig Pointer to channel configuration in runtime
 */
bool MC12b_WriteStateAll(                                                   \
                        const AInArray* channelConfig,                      \
                        AInRuntimeArray* channelRuntimeConfig);
    
/*!
 * Updates the state for a single ADC channel
 * @param[] channelConfig       Pointer to channel configuration
 * @param[] channelRuntimeConfig Pointer to channel configuration in runtime
 */
bool MC12b_WriteStateSingle(                                                \
                        const MC12bChannelConfig* channelConfig,            \
                        AInRuntimeConfig* channelRuntimeConfig); 
    
/*!
 * Populates the sample array using data in the board config
 * @param[in/out] samples The array to populate
 * @param[in] channelConfig The static channel configuration for the board
 * @param[in] channelRuntimeConfig The runtime channel configuration for the board
 * @param[in] triggerTimeStamp The timestamp when the module was most recently triggered to convert
 */
bool MC12b_ReadSamples( AInSampleArray* samples,                            \
                        const AInArray* channelConfig,                      \
                        AInRuntimeArray* channelRuntimeConfig,              \
                        uint32_t triggerTimeStamp);
    
/**
 * Triggers a conversion
 * @return true on success, false otherwise
 */
    bool MC12b_TriggerConversion( void );

/**
 * Calculates a voltage based on the given sample
 * NOTE: This is NOT safe to call in an ISR
 * @param[in] channelConfig Information about the channel
 * @param[in] runtimeConfig Runtime channel information
 * @param[in] sample The sample to process
 * @return The converted voltage
 */
double MC12b_ConvertToVoltage(                                              \
                        const MC12bChannelConfig* channelConfig,            \
                        const AInRuntimeConfig* runtimeConfig,              \
                        const AInSample* sample);
    
#ifdef	__cplusplus
}
#endif


