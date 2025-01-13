#include "NullLockProvider.h"

#define UNUSED(x) (void)(x)

static void NullLockProvider_Init(LockHandle* handle)
{
    UNUSED(handle);
    // No action
}

/**
 * A Dummy function
 * @param handle The lock handle (ignored)
 */
static void NullLockProvider_NoOp(const LockHandle* handle)
{
    UNUSED(handle);
    // No action
}

LockProvider g_NullLockProvider = {
    .Handle = 0,
    .LockInitFxn = NullLockProvider_Init,
    .LockDestroyFxn = NullLockProvider_NoOp,
    .LockFxn = NullLockProvider_NoOp,
    .UnlockFxn = NullLockProvider_NoOp
};
