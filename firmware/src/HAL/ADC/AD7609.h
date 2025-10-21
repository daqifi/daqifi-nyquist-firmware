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

// AD7609 Hardware Constants
#define AD7609_NUM_CHANNELS      8       // 8 simultaneous sampling input channels (0-7)
#define AD7609_BITS_PER_CHANNEL  18      // 18-bit resolution per channel
#define AD7609_TOTAL_BITS        144     // Total bitstream: 8 × 18 = 144 bits
#define AD7609_BUFFER_BYTES      18      // 144 bits = 18 bytes
#define AD7609_MAX_VALUE         0x1FFFF // 18-bit max value (131071)
#define AD7609_SIGN_BIT          0x20000 // Bit 17 (sign bit for 2's complement)
#define AD7609_SIGN_EXTEND       0xFFFC0000U // Sign extension mask for negative values

/*!
 * Performs board initialization
 * @param[in] pBoardConfigInit The module configuration
 */
bool AD7609_InitHardware(const AD7609ModuleConfig* pBoardConfigInit);

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

/*!
 * AD7609 deferred interrupt task (handles SPI read after BSY interrupt)
 * This function is called by FreeRTOS task scheduler
 */
void AD7609_DeferredInterruptTask(void);

/*!
 * AD7609 BSY pin interrupt callback (called from GPIO ISR)
 * @param[in] pin The GPIO pin that triggered the interrupt
 * @param[in] context User context (unused)
 */
void AD7609_BSY_InterruptCallback(GPIO_PIN pin, uintptr_t context);

/*!
 * Returns the task handle for the AD7609 deferred interrupt task
 * @return Task handle pointer (volatile to ensure ISR/task synchronization)
 */
volatile void* AD7609_GetTaskHandle(void);

#ifdef	__cplusplus
}
#endif


