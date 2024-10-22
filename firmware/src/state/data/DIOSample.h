#pragma once

#include <stdint.h>

#include "Util/HeapList.h"

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * Contains DIO information for a given time
     */
    typedef struct s_DIOSample
    {
        /**
         * The Processor tick count
         */
        uint32_t Timestamp;
        
        /**
         * The mask indicating which ids (i.e. bits) are valid for this sample
         */
        uint16_t Mask;
        
        /**
         * Storage for the values
         */
        uint32_t Values;
    } DIOSample;

    /**
     * A wrapper around a HeapList to simplify use
     */
    typedef struct s_DIOSampleList
    {
        /**
         * The list to wrap
         */
        HeapList List;
    } DIOSampleList;
    
    /**
    * Initializes the list
    * @param list The list to initialize
    * @param maxSize The maximum number of items in the list
    * @param dropOnOverFlow Set true to drop the first element when adding to a full list (false to error out)
    * @param lockPrototype The lock provider to use
    */
    void DIOSampleList_Initialize(DIOSampleList* list, size_t maxSize, bool dropOnOverflow, const LockProvider* lockPrototype);

    /**
     * Destroys the list
     * @param list The list to destroy
     */
    void DIOSampleList_Destroy(DIOSampleList* list);

    /**
     * Adds data to the back of the list
     * @param list The list to add to
     * @param data The sample to add
     * @return True on success, false on failure
     */
    bool DIOSampleList_PushBack(DIOSampleList* list, const DIOSample* data);

    /**
     * Removes and returns the first item in the list
     * @param list The list to modify
     * @param data [out] Storage for the returned data
     * @return True on success, false on failure
     */
    bool DIOSampleList_PopFront(DIOSampleList* list, DIOSample* data);

    /**
     * Gets the number of items currently in the list
     * @param list The list to query
     * @return The current size of the list
     */
    size_t DIOSampleList_Size(DIOSampleList* list);
    
    /**
     * Indicates whether there is data in the list
     * @param list The list to check
     * @return True if the list is empty, false otherwise
     */
    bool DIOSampleList_IsEmpty(DIOSampleList* list);
    
#ifdef __cplusplus
}
#endif
