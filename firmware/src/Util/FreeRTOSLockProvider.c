#include "FreeRTOSLockProvider.h"

#include "string.h"
#include "FreeRTOS.h"
#include "task.h"

typedef struct s_FreeRTOSLockProviderContext
{
    UBaseType_t LockHandle;
    uint8_t RefCount;
} FreeRTOSLockProviderContext;

static FreeRTOSLockProviderContext interruptStatuses[64];

static void FreeRTOSLockProvider_Lock(const LockHandle* handle)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) return;
    FreeRTOSLockProviderContext* context = &interruptStatuses[*handle];
    if (context->RefCount == 0)
    {
        context->LockHandle =  taskENTER_CRITICAL_FROM_ISR();
    }
    
    ++context->RefCount;
}

static void FreeRTOSLockProvider_Unlock(const LockHandle* handle)
{
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING) return;
    FreeRTOSLockProviderContext* context = &interruptStatuses[*handle];
    
    --context->RefCount;
    if (context->RefCount == 0)
    {
        taskEXIT_CRITICAL_FROM_ISR(context->LockHandle);
        context->LockHandle = 0;
    }
}

static void FreeRTOSLockProvider_Init(LockHandle* handle)
{
    static LockHandle currentLockId = 0;
    *handle = currentLockId;
    memset(&interruptStatuses[*handle], 0, sizeof(FreeRTOSLockProviderContext));
    ++currentLockId;
}

static void FreeRTOSLockProvider_Destroy(const LockHandle* handle)
{
    FreeRTOSLockProviderContext* context = &interruptStatuses[*handle];
    while(context->RefCount > 0)
    {
        FreeRTOSLockProvider_Unlock(handle);
    }
    
    memset(context, 0, sizeof(FreeRTOSLockProviderContext));
}

LockProvider g_RTOSLockProvider = {
    .Handle = 0,
    .LockInitFxn = FreeRTOSLockProvider_Init,
    .LockDestroyFxn = FreeRTOSLockProvider_Destroy,
    .LockFxn = FreeRTOSLockProvider_Lock,
    .UnlockFxn = FreeRTOSLockProvider_Unlock
};