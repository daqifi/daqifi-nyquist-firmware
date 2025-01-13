#pragma once

#include "configuration.h"
#include "definitions.h"

#ifdef	__cplusplus
extern "C" {
#endif

    /**
     * Contains the board configuration for the streaming timer
     */
    typedef struct sStreamingConfig
    {
        /**
         * The timer index (e.g. DRV_TMR_INDEX_1)
         */
        int TimerIndex;
        
//        /**
//         *Dedicated adc timer Index
//         */
//        int DedicatedADCTimerIndex;
        
        /**
         * The timestamp timer index (e.g. DRV_TMR_INDEX_1)
         */
        int TSTimerIndex;
        
    } tStreamingConfig;


#ifdef	__cplusplus
}
#endif


