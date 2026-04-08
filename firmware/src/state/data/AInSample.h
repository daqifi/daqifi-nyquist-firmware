#pragma once

#include <stdint.h>

#include "Util/ArrayWrapper.h"
#include "Util/HeapList.h"
#include "../board/AInConfig.h"
#ifdef __cplusplus
extern "C" {
#endif

    /**
     * State machine states for analog input task processing
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
     * Contains a single analog input sample with timestamp and value.
     * Used internally by BoardData for per-channel latest values.
     * NOT used inside the streaming sample pool (see AInPublicSampleList_t).
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

    /**
     * Channel mapping: built at stream start, maps packed indices to
     * board config channel IDs and array indices.
     *
     * Stored globally in streaming.c, used by ISR and all encoders.
     */
    typedef struct {
        uint8_t count;                                    /**< Number of enabled public channels */
        uint8_t channelIds[MAX_AIN_PUBLIC_CHANNELS];      /**< Packed index -> DaqifiAdcChannelId */
        uint8_t configIndices[MAX_AIN_PUBLIC_CHANNELS];   /**< Packed index -> board config array index */
    } AInChannelMapping;

    /**
     * Compact streaming sample: shared timestamp + packed channel values.
     *
     * Runtime-sized via flexible array member. Each sample is
     * (8 + channelCount * 4) bytes. At stream start, the pool element
     * stride is computed from the number of enabled public channels.
     *
     * Size comparison (16 channels):
     *   Old: 208 bytes (16 × 12-byte AInSample + 16 bools)
     *   New:  72 bytes (8-byte header + 16 × 4-byte values)
     *
     * Size at low channel counts:
     *   1ch: 12 bytes | 4ch: 24 bytes | 8ch: 40 bytes
     */
    typedef struct {
        uint32_t Timestamp;        /**< Shared timestamp for all channels in this sample */
        uint16_t validMask;        /**< Bitmask: bit j = Values[j] is valid */
        uint16_t channelCount;     /**< Number of Values[] entries (= mapping.count) */
        uint32_t Values[];         /**< Packed channel values [0..channelCount-1] */
    } AInPublicSampleList_t;

    /**
     * Compute the per-element byte size for a given channel count.
     * Used by pool partitioning and allocation.
     */
    static inline size_t AInSampleList_ElementSize(uint8_t channelCount) {
        return sizeof(AInPublicSampleList_t) + (size_t)channelCount * sizeof(uint32_t);
    }

    // Define a storage class for analog input channels

    /**
     * Default sample pool depth.
     * Used when no runtime override is configured (MemoryConfig.samplePoolCount = 0).
     *
     * Memory per sample depends on enabled channel count:
     *   1ch: 14 bytes (12 data + 2 nextFree)
     *   8ch: 42 bytes (40 data + 2 nextFree)
     *  16ch: 74 bytes (72 data + 2 nextFree)
     *
     * Default 1100 @ 16ch = ~81 KB (was 231 KB before compact pool).
     * At 1ch, same 194KB pool yields ~14,000 samples.
     */
#define DEFAULT_AIN_SAMPLE_COUNT 1100
#define MIN_AIN_SAMPLE_COUNT     100
#define MAX_AIN_SAMPLE_COUNT     10000
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
     * @brief Initializes using externally-provided pool memory.
     *
     * Like AInSampleList_Initialize but uses caller-provided memory for
     * the sample pool and free-list arrays (e.g., from StreamingBufferPool).
     * The FreeRTOS queue is still heap-allocated.
     *
     * @param poolMem       Pre-allocated byte array for sample pool
     * @param freeMem       Pre-allocated array of int16_t[maxSize]
     * @param maxSize       Number of samples the arrays can hold
     * @param elementSize   Bytes per sample element (from AInSampleList_ElementSize)
     */
    void AInSampleList_InitializeExternal(void* poolMem, int16_t* freeMem,
                                           size_t maxSize, size_t elementSize);

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

    /**
     * @brief Allocates a sample structure from the pre-allocated object pool.
     *
     * Uses O(1) free list allocation to eliminate heap allocation overhead.
     * The returned structure is zero-initialized.
     *
     * @return Pointer to allocated sample, or NULL if pool is exhausted.
     */
    AInPublicSampleList_t* AInSampleList_AllocateFromPool(void);

    /**
     * @brief Returns a sample structure to the object pool.
     *
     * Uses O(1) free list deallocation to eliminate heap free overhead.
     * Safe to call with NULL or non-pool pointers (will be ignored).
     *
     * @param pSample Pointer to sample structure to return to pool.
     */
    void AInSampleList_FreeToPool(AInPublicSampleList_t* pSample);

    /**
     * @brief Returns the current pool capacity (number of samples).
     */
    size_t AInSampleList_PoolCapacity(void);

    /**
     * @brief Returns the number of samples currently allocated (in use).
     */
    uint32_t AInSampleList_PoolInUse(void);

    /**
     * @brief Returns the peak pool usage (max samples ever simultaneously in use).
     */
    uint32_t AInSampleList_PoolMaxUsed(void);

    /**
     * @brief Resets the peak usage counter to current usage.
     */
    void AInSampleList_PoolResetMaxUsed(void);

    /**
     * @brief Returns the current per-element stride in bytes.
     * This is the runtime element size used for pool indexing.
     */
    size_t AInSampleList_PoolElementSize(void);

#ifdef __cplusplus
}
#endif
