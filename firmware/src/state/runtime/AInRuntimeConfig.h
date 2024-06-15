#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "Util/ArrayWrapper.h"

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * Defines the runtime/configurable parameters for a single Analog input channel
     */
    typedef struct s_AInModuleRuntimeConfig
    {
        /**
         * Indicates whether this module is enabled
         */
        bool IsEnabled;
        
        /***
         * Currently configured module range 
         */
        double Range;
    } AInModuleRuntimeConfig;
    
    /**
     * Defines the runtime/configurable parameters for a single Analog input channel
     */
    typedef struct s_AInRuntimeConfig
    {
        /**
         * Indicates whether this channel is enabled
         */
        bool IsEnabled;
        
        /**
         * Indicates whether this is a differential input
         */
        bool IsDifferential;
        
        /**
         * The conversion frequency
         * TODO: Do we control this at the channel level or at the module level
         */
        uint32_t Frequency;
        
        /**
         * The m (slope) calibration value for the channel
         */
        double CalM;
        
        /**
         * The b (intercept) calibration value for the channel
         */
        double CalB;
        
    } AInRuntimeConfig;
    
    /**
     * Stores the ADC channel calibration settings
     * These are intended to be stored in NVM and stuffed into AInRuntimeConfig by a helper function.
     * They are not intended to be accessed directly by any ADC functions.
     */
    typedef struct s_AInCalParam
    {  
        /**
         * The m (slope) calibration value for the channel
         */
        double CalM;
        
        /**
         * The b (intercept) calibration value for the channel
         */
        double CalB;
    }AInCalParam;

    // Define a storage class for analog input modules
    #define MAX_AIN_RUNTIME_MOD 3
    ARRAYWRAPPERDEF(AInModRuntimeArray, AInModuleRuntimeConfig, MAX_AIN_RUNTIME_MOD);
    
    // Define a storage class for analog input channels
    #define MAX_AIN_RUNTIME_CHANNEL 48
    ARRAYWRAPPERDEF(AInRuntimeArray, AInRuntimeConfig, MAX_AIN_RUNTIME_CHANNEL);
    
    // Define a storage class for analog input channel calibration settings.
    // These are intended to be stored in NVM and stuffed into AInRuntimeConfig by a helper function.
    // They are not intended to be accessed directly by any ADC functions.
    ARRAYWRAPPERDEF(AInCalArray, AInCalParam, MAX_AIN_RUNTIME_CHANNEL);
#ifdef __cplusplus
}
#endif