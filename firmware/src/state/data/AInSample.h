#pragma once

#include <stdint.h>

#include "Util/ArrayWrapper.h"
#include "Util/HeapList.h"
#include "../board/AInConfig.h"
#ifdef __cplusplus
extern "C" {
#endif

    /**
     * Contains DIO information for a given time
     */
    typedef enum e_AInTaskState
    {
        AINTASK_INITIALIZING,
        AINTASK_IDLE,
        AINTASK_CONVSTART,
        AINTASK_BUSY,
        AINTASK_CONVCOMPLETE,
        AINTASK_DISABLED
    }AInTaskState_t;
    
    typedef struct s_AInModData
    {
        AInTaskState_t AInTaskState;
    } AInModData;
    
    // Define a storage class for analog input data
    //#define MAX_AIN_DATA_MOD MAX_AIN_MOD
    ARRAYWRAPPERDEF(AInModDataArray, AInModData, MAX_AIN_MOD);
    
    /**
     * Contains DIO information for a given time
     */
    typedef struct s_AInSample
    {
        /**
         * The Processor tick count
         */
        uint32_t Timestamp;
        
        /**
         * The channel that generated this sample
         */
        uint8_t Channel;
        
        /**
         * The value of the channel
         */
        uint32_t Value;
    } AInSample;
    
    // Define a storage class for analog input channels
    //#define MAX_AIN_CHANNEL    48
    #define MAX_AIN_SAMPLE_COUNT 255
    ARRAYWRAPPERDEF(AInSampleArray, AInSample, MAX_AIN_CHANNEL);
    
    /**
     * A wrapper around a HeapList to simplify use
     */
    typedef struct s_AInSampleList
    {
        /**
         * The list to wrap
         */
        HeapList List;
    } AInSampleList;
    
    /**
    * Initializes the list
    * @param list The list to initialize
    * @param maxSize The maximum number of items in the list
    * @param dropOnOverFlow Set true to drop the first element when adding to a full list (false to error out)
    * @param lockPrototype The lock provider to use
    */
    void AInSampleList_Initialize(AInSampleList* list, size_t maxSize, bool dropOnOverflow, const LockProvider* lockPrototype);

    /**
     * Destroys the list
     * @param list The list to destroy
     */
    void AInSampleList_Destroy(AInSampleList* list);

    /**
     * Adds data to the back of the list
     * @param list The list to add to
     * @param data The sample to add
     * @return True on success, false on failure
     */
    bool AInSampleList_PushBack(AInSampleList* list, const AInSample* data);
    /**
     * Adds data to the back of the list from ISR
     * @param list The list to add to
     * @param  data The sample to add
     * @return True on success, false on failure
     */
    bool AInSampleList_PushBackFromIsr(AInSampleList* list, const AInSample* data);
    /**
     * Removes and returns the first item in the list
     * @param list The list to modify
     * @param data [out] Storage for the returned data
     * @return True on success, false on failure
     */
    bool AInSampleList_PopFront(AInSampleList* list, AInSample* data);

    /**
     * Returns the first item in the list
     * @param list The list to modify
     * @param data [out] Storage for the returned data
     * @return True on success, false on failure
     */
    bool AInSampleList_PeekFront(AInSampleList* list, AInSample* data);
    
    /**
     * Gets the number of items currently in the list
     * @param list The list to query
     * @return The current size of the list
     */
    size_t AInSampleList_Size(AInSampleList* list);
    
    /* Indicates whether there is data in the list
     * @param list The list to check
     * @return True if the list is empty, false otherwise
     */
    bool AInSampleList_IsEmpty(AInSampleList* list);
    
#ifdef __cplusplus
}
#endif
