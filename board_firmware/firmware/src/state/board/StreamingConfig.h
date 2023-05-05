#pragma once

#include "system_config.h"
#include "system_definitions.h"

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
        
        /**
         * The timer intent
         */
        DRV_IO_INTENT TimerIntent;
         
        /**
         * The timestamp timer index (e.g. DRV_TMR_INDEX_1)
         */
        int TSTimerIndex;
        
        /**
         * The timestamp timer intent
         */
        DRV_IO_INTENT TSTimerIntent;
    } tStreamingConfig;


#ifdef	__cplusplus
}
#endif


