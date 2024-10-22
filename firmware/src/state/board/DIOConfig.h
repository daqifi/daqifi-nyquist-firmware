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
         * The channel associated with the data line
         */
        GPIO_PORT DataChannel;
        
        /**
         * The pin associated with the data line
         */
        GPIO_PIN DataPin;
        
        /**
         * The channel associated with the enable line 
         */
        GPIO_PORT EnableChannel;
        
        /**
         * The pin associated with the enable line 
         */
        GPIO_PIN EnablePin;
        
        /**
         * Indicates whether the 'enable' line is inverted
         */
        bool EnableInverted;
         /**
         * PWM driver index
         */
        bool IsPwmCapable;
        /**
         * Id of the Output compare unit. 0xFF is Invalid
         */
        uint8_t PwmOcmpId;
        
        /**
         * Pwm port remap pin number
         */
        uint16_t PwmRemapPin;
        
    } DIOConfig;
    
    // Define a storage class for DIO Configs
    #define MAX_DIO_CHANNEL 16
    #define MAX_DIO_SAMPLE_COUNT 255
    ARRAYWRAPPERDEF(DIOArray, DIOConfig, MAX_DIO_CHANNEL);
    
#ifdef __cplusplus
}
#endif
