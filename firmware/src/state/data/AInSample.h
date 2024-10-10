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
    typedef enum e_AInTaskState {
        AINTASK_INITIALIZING,
        AINTASK_IDLE,
        AINTASK_CONVSTART,
        AINTASK_BUSY,
        AINTASK_CONVCOMPLETE,
        AINTASK_DISABLED
    } AInTaskState_t;

    typedef struct s_AInModData {
        AInTaskState_t AInTaskState;
    } AInModData;

    // Define a storage class for analog input data
    ARRAYWRAPPERDEF(AInModDataArray, AInModData, MAX_AIN_MOD);

    /**
     * Contains DIO information for a given time
     */
    typedef struct s_AInSample {
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

    typedef struct {
        AInSample sampleElement[MAX_AIN_PUBLIC_CHANNELS];
        bool isSampleValid[MAX_AIN_PUBLIC_CHANNELS];
    } AInPublicSampleList_t;

    // Define a storage class for analog input channels
    //#define MAX_AIN_CHANNEL    48
#define MAX_AIN_SAMPLE_COUNT 20
    ARRAYWRAPPERDEF(AInSampleArray, AInSample, MAX_AIN_CHANNEL);

    /**
     * A wrapper around a HeapList to simplify use
     */
    typedef struct s_AInSampleList {
        /**
         * The list to wrap
         */
        HeapList List;
    } AInSampleList;

    /**
     * @brief Initializes the Analog Input Sample List queue.
     * 
     * This function initializes the queue with a maximum size.
     * 
     * @param maxSize Maximum number of items the queue can hold.
     */
    void AInSampleList_Initialize(size_t maxSize,
            bool dropOnOverflow,
            const LockProvider* lockPrototype);

    /**
     * @brief Destroys the Analog Input Sample List queue.
     * 
     * This function frees all items in the queue and deletes the queue.
     */
    void AInSampleList_Destroy(void);

    /**
     * @brief Adds a new data sample to the queue.
     * 
     * This function adds a new data sample to the queue.
     * 
     * @param pData Pointer to the data sample to be added.
     * @return True if the data was successfully added, false otherwise.
     */
    bool AInSampleList_PushBack(const AInPublicSampleList_t* pData);

    /**
     * @brief Adds a new data sample to the queue from an ISR.
     * 
     * Similar to AInSampleList_PushBack but used within an interrupt context.
     * 
     * @param pData Pointer to the data sample to be added.
     * @return True if the data was successfully added, false otherwise.
     */
    bool AInSampleList_PushBackFromIsr(const AInPublicSampleList_t* pData);

    /**
     * @brief Removes and returns the first data sample from the queue.
     * 
     * This function removes the first data sample from the queue and assigns it
     * to the provided pointer.
     * 
     * @param ppData Pointer to where the removed data sample will be stored.
     * @return True if a data sample was successfully removed, false otherwise.
     */
    bool AInSampleList_PopFront(AInPublicSampleList_t** ppData);

    /**
     * @brief Peeks at the first data sample in the queue without removing it.
     * 
     * This function retrieves the first data sample in the queue without removing it.
     * 
     * @param ppData Pointer to where the peeked data sample will be stored.
     * @return True if a data sample was successfully retrieved, false otherwise.
     */
    bool AInSampleList_PeekFront(AInPublicSampleList_t** ppData);

    /**
     * @brief Gets the current number of items in the queue.
     * 
     * @return The number of items in the queue.
     */
    size_t AInSampleList_Size(void);

    /**
     * @brief Checks if the queue is empty.
     * 
     * @return True if the queue is empty, false otherwise.
     */
    bool AInSampleList_IsEmpty(void);

#ifdef __cplusplus
}
#endif
