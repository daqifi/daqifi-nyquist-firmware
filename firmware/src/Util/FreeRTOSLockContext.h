/* 
 * File:   FreeRTOSLockContext.h
 * Author: Daniel
 * A FreeRTOS backed lock implementation
 */

#pragma once

#include "Util/HeapList.h"

#ifdef	__cplusplus
extern "C" {
#endif

    /**
     * Locks a heaplist
     * @param list The list to lock
     * @param lockError A function to call on error
     */
    bool HeapList_Lock(HeapList* list, HeapListErrorFxn lockError);
    
    /**
     * Unlocks a heaplist
     * @param list The list to lock
     * @param lockError A function to call on error
     */
    bool HeapList_Unlock(HeapList* list, HeapListErrorFxn lockError);

#ifdef	__cplusplus
}
#endif


