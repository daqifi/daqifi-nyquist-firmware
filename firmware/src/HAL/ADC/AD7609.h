/*! @file AD7609.h 
 * 
 * This file implements the functions to manage the ADC module
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
 * @param[in] pBoardConfigInit The module configuration
 */
//bool AD7609_InitHardware(const AD7609ModuleConfig* pBoardConfigInit);

/*!
 * Updates the module state based on the provided config
 * param[in] isPowered      Indicate if the module is powered 
 */
bool AD7609_WriteModuleState(bool isPowered); 

/**
 * Updates the state for a single ADC channel
 */
bool AD7609_WriteStateSingle(                                               \
        AInModuleRuntimeConfig* moduleRuntimeConfig,
        const AD7609ChannelConfig* channelConfig,
        AInRuntimeConfig* channelRuntimeConfig);


/*!
 * Sets the state for all ADC channels
 * @param[in] channelConfig Channel configuration
 * @param[in] channelRuntimeConfig Channel configuration in runtime
 */
bool AD7609_WriteStateAll(                                                  \
                        const AInArray* channelConfig,                      \
                        AInRuntimeArray* channelRuntimeConfig);
    

    
/*!
 * Populates the sample array using data in the board config
 * @param[in/out] samples The array to populate
 * @param channelConfig The static channel configuration for the board
 * @param channelRuntimeConfig The runtime channel configuration for the board
 * @param triggerTimeStamp The timestamp when the module was most recently triggered to convert
 */
bool AD7609_ReadSamples(AInSampleArray* samples,                            \
                        const AInArray* channelConfigList,                  \
                        AInRuntimeArray* channelRuntimeConfigList,          \
                        uint32_t triggerTimeStamp);
    
/*!
 * Triggers a conversion
 * @param moduleConfig Describes the hardware configuration
 * @return true on success, false otherwise
 */
bool AD7609_TriggerConversion(const AD7609ModuleConfig* moduleConfig);
    
/*!
 * Calculates a voltage based on the given sample
 * NOTE: This is NOT safe to call in an ISR
 * @param[in] runtimeConfig Runtime channel information
 * @param[in] sample The sample to process
 * @return The converted voltage
 */
double AD7609_ConvertToVoltage(                                             \
                        const AInRuntimeConfig* runtimeConfig,              \
                        const AInSample* sample);
    
#ifdef	__cplusplus
}
#endif


