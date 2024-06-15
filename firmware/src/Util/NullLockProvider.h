#pragma once

#include "Util/LockProvider.h"

#ifdef	__cplusplus
extern "C" {
#endif
    /**
     * Implements a dummy implementation of the lock
     */
    extern LockProvider g_NullLockProvider;
#ifdef	__cplusplus
}
#endif