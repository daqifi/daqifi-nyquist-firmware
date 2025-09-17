#pragma once

#include "configuration.h"
#include "definitions.h"
#include "Util/ArrayWrapper.h"
#include "state/board/AOutConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * Runtime configuration for a single analog output channel
     */
    typedef struct s_AOutRuntimeChannelConfig {
        /**
         * Indicates whether this channel is enabled for output
         */
        bool IsEnabled;
        
        /**
         * Current output voltage set for this channel
         */
        double OutputVoltage;
        
        /**
         * Current range setting for this channel
         */
        uint8_t Range;
        
        /**
         * User calibration slope (m) for this channel
         */
        double CalibrationM;
        
        /**
         * User calibration intercept (b) for this channel  
         */
        double CalibrationB;
        
        /**
         * Factory calibration slope (m) for this channel
         */
        double FactoryCalibrationM;
        
        /**
         * Factory calibration intercept (b) for this channel
         */
        double FactoryCalibrationB;
        
        /**
         * Use factory calibration (0) or user calibration (1)
         */
        bool UseUserCalibration;
        
    } AOutRuntimeChannelConfig;

    /**
     * Runtime configuration for a single analog output module
     */
    typedef struct s_AOutModRuntimeConfig {
        /**
         * Indicates whether this module is enabled
         */
        bool IsEnabled;
        
        /**
         * Module-specific configuration parameters
         */
        uint8_t ModuleRange;
        
    } AOutModRuntimeConfig;

    // Define storage classes for analog output runtime configurations
#define MAX_AOUT_MOD_RUNTIME 1
    ARRAYWRAPPERDEF(AOutModRuntimeArray, AOutModRuntimeConfig, MAX_AOUT_MOD_RUNTIME);

#define MAX_AOUT_CHANNEL_RUNTIME 8  
    ARRAYWRAPPERDEF(AOutRuntimeArray, AOutRuntimeChannelConfig, MAX_AOUT_CHANNEL_RUNTIME);

#ifdef __cplusplus
}
#endif