#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "../board/DIOConfig.h"
#include "Util/ArrayWrapper.h"

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * Defines the runtime/configurable parameters for a single DIO channel
     */
    typedef struct s_DIORuntimeConfig
    {
       /**
         * Indicates whether this an input (true) or output (false)
         */
        bool IsInput;
        
        /**
         * Indicates whether this a read only pin
         */
        bool IsReadOnly;
        
        /**
         * For output, defines the output value
         */
        bool Value;
        
        /**
         * Indicates if PWM is active for this pin
         */
        bool IsPwmActive;
        
        /**
         * Frequency of PWM output
         */
        uint32_t PwmFrequency;
        
        /**
         * Duty Cycle of PWM output
         */
        uint16_t PwmDutyCycle;
    } DIORuntimeConfig;

    ARRAYWRAPPERDEF(DIORuntimeArray, DIORuntimeConfig, MAX_DIO_CHANNEL);
    
#ifdef __cplusplus
}
#endif