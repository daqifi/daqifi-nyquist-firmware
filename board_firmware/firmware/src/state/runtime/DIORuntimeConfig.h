#pragma once

#include <stdbool.h>
#include <stdint.h>

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
    } DIORuntimeConfig;
    
    // Define a storage class for DIO Configs
    #define MAX_DIO_RUNTIME_CHANNEL 16
    ARRAYWRAPPERDEF(DIORuntimeArray, DIORuntimeConfig, MAX_DIO_RUNTIME_CHANNEL);
    
#ifdef __cplusplus
}
#endif