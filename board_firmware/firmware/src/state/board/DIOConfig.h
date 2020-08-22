#pragma once

#include "system_config.h"
#include "system_definitions.h"

#include "Util/ArrayWrapper.h"

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * Defines the immutable DIO parameters for a single DIO channel
     */
    typedef struct s_DIOConfig
    {
        /**
         * The port module to use for the data line (probably always PORTS_ID_0)
         */
        PORTS_MODULE_ID DataModule;
        
        /**
         * The channel associated with the data line
         */
        PORTS_CHANNEL DataChannel;
        
        /**
         * The pin associated with the data line
         */
        PORTS_BIT_POS DataPin;
        
        /**
         * The port module to use for the enable line (probably always PORTS_ID_0)
         */
        PORTS_MODULE_ID EnableModule;
        
        /**
         * The channel associated with the enable line 
         */
        PORTS_CHANNEL EnableChannel;
        
        /**
         * The pin associated with the enable line 
         */
        PORTS_BIT_POS EnablePin;
        
        /**
         * Indicates whether the 'enable' line is inverted
         */
        bool EnableInverted;
    } DIOConfig;
    
    // Define a storage class for DIO Configs
    #define MAX_DIO_CHANNEL 16
    #define MAX_DIO_SAMPLE_COUNT 16 //255
    ARRAYWRAPPERDEF(DIOArray, DIOConfig, MAX_DIO_CHANNEL);
    
#ifdef __cplusplus
}
#endif
