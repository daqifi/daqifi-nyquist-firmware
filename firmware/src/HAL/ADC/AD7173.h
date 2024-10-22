/* 
 * File:   AD7173.h
 * Author: Daniel
 *
 * This file implements the interface to the functions to manage the module
 * ADC AD7609. 
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
 * @param[in] moduleConfig The module configuration, it is necesary to update 
 *                          the static pointer variable. 
 * @param[in] channelConfig The channel configuration
  * @return True on success, false otherwise. 
 */
bool AD7173_InitHardware(const AD7173ModuleConfig* pModuleConfigAD7173,     \
                         const AInArray* channelConfig);
    
/*!
* Updates the module state based on the provided config
 * @param[in] isPowered Indicate if the module is powered. 
 * @return True on success, false otherwise. 
*/
bool AD7173_WriteModuleState(bool isPowered);
    
/*!
 * Sets the state for all ADC channels
 * @param[in] channelConfig         Channel configuration init
 * @param[in] channelRuntimeConfig  Channel configuration in runtime
 */
bool AD7173_WriteStateAll(                                                  \
                        const AInArray* channelConfig,                      \
                        AInRuntimeArray* channelRuntimeConfig);
    
/*!
 * Updates the state for a single ADC channel
 * @param[in] channelConfig         Channel configuration init
 * @param[in] channelRuntimeConfig  Channel configuration in runtime
 * @return True on success, false otherwise. 
 */
bool AD7173_WriteStateSingle(                                               \
                        const AD7173ChannelConfig* channelConfig,           \
                        AInRuntimeConfig* channelRuntimeConfig);
/*!
 * Populates the sample array using data in the board config
 * @param[in/out] moduleConfig The static module configuration for the board
 * @param[in] channelConfig The static channel configuration for the board
 * @param[in] channelRuntimeConfig The runtime channel configuration for the
 *                                 board
 * @param[in] triggerTimeStamp The timestamp when the module was most 
 *                             recently triggered to convert
 * @return True on success, false otherwise. 
 */
bool AD7173_ReadSamples(AInSampleArray* samples,                            \
                        const AInArray* channelConfigList,                  \
                        AInRuntimeArray* channelRuntimeConfigList,          \
                        uint32_t triggerTimeStamp); 

/*!
 * Triggers a conversion
 * @param moduleConfig Describes the hardware configuration
 * @return true on success, false otherwise
 */
bool AD7173_TriggerConversion(const AD7173ModuleConfig* moduleConfig);
    
/*!
 * Calculates a voltage based on the given sample
 * NOTE: This is NOT safe to call in an ISR
 * @param runtimeConfig Runtime channel information
 * @param sample The sample to process
 * @return The converted voltage
 */
double AD7173_ConvertToVoltage(                                             \
                        const AInRuntimeConfig* runtimeConfig,              \
                        const AInSample* sample);
    
#ifdef	__cplusplus
}
#endif


