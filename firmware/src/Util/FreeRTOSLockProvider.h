#pragma once

#include "Util/LockProvider.h"

#ifdef	__cplusplus
extern "C" {
#endif
    /**
     * Implements a lock using FreeRTOS mutexes
     */
    extern LockProvider g_RTOSLockProvider;
#ifdef	__cplusplus
}
#endif


