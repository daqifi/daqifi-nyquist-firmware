#pragma once

#include "configuration.h"
#include "definitions.h"

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
        /**
         *Indicates if the channel is  PWM capable
         */
        SYS_MODULE_INDEX PwmDrvIndex;
         /**
         * PWM driver index
         */
        bool IsPwmCapable;
         /**
         * PPS remap function
         */
        PORTS_REMAP_OUTPUT_FUNCTION pwmRemapFuction;
         /**
         * PPS remap pin
         */
        PORTS_REMAP_OUTPUT_PIN pwmRemapPin;
    } DIOConfig;
    
    // Define a storage class for DIO Configs
    #define MAX_DIO_CHANNEL 16
    #define MAX_DIO_SAMPLE_COUNT 255
    ARRAYWRAPPERDEF(DIOArray, DIOConfig, MAX_DIO_CHANNEL);
    
#ifdef __cplusplus
}
#endif
