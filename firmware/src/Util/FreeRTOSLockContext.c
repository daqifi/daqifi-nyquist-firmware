#include "FreeRTOSLockContext.h"

#include "FreeRTOS.h"
#include "task.h"

#include "HeapList.h"
#include "ArrayWrapper.h"

#define UNUSED(x) (void)(x)

static ulong_array savedInterruptState = {
    .Size = 0
};

bool HeapList_Lock(HeapList* list, HeapListErrorFxn lockError)
{
    UNUSED(list);
    UNUSED(lockError);
    
    savedInterruptState.Data[savedInterruptState.Size] = taskENTER_CRITICAL_FROM_ISR();
    savedInterruptState.Size += 1;
    return true;
}
    

bool HeapList_Unlock(HeapList* list, HeapListErrorFxn lockError)
{
    UNUSED(list);
    UNUSED(lockError);
    
    taskEXIT_CRITICAL_FROM_ISR(savedInterruptState.Data[savedInterruptState.Size -1]);
    savedInterruptState.Size -= 1;
    return false;
}
