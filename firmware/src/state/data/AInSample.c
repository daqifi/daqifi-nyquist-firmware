/*! @file AInSample.c
 * @brief Analog input sample queue and object pool implementation.
 *
 * Implements a FreeRTOS queue for streaming analog input samples between the
 * ADC interrupt handler and the streaming task. Uses a dynamically allocated
 * object pool with O(1) free list allocation to eliminate heap overhead at
 * high sample rates.
 *
 * The pool is allocated from the FreeRTOS heap at AInSampleList_Initialize()
 * and freed at AInSampleList_Destroy(). Pool size is configurable via the
 * maxSize parameter (clamped to MIN/MAX_AIN_SAMPLE_COUNT).
 *
 * Performance characteristics:
 * - Queue: Standard FreeRTOS queue (thread-safe, ISR-safe)
 * - Pool allocation: O(1) via free list (no fragmentation, deterministic timing)
 * - Pool size: Configurable (default DEFAULT_AIN_SAMPLE_COUNT = 700)
 *
 * @author Javier Longares Abaiz - When Technology becomes art.
 * @web www.javierlongares.com
 */

#include "AInSample.h"
#include "FreeRTOS.h"
#include "queue.h"
#include <string.h>  // For memset
#include "Util/Logger.h"

//! Ticks to wait for QUEUE OPERATIONS
#define AINSAMPLE_QUEUE_TICKS_TO_WAIT               0  // No delay

//! Queue handler for Analog inputs
static QueueHandle_t analogInputsQueue;
//! Size of the queue, in number of items
static uint32_t queueSize = 0;

// =============================================================================
// Object Pool (dynamically allocated from FreeRTOS heap)
// =============================================================================
static AInPublicSampleList_t* samplePool = NULL;
static uint32_t poolCapacity = 0;

// =============================================================================
// Free List Data Structures (O(1) allocation/deallocation)
// =============================================================================
static int16_t freeHead = -1;
static int16_t* nextFree = NULL;

static SemaphoreHandle_t poolMutex = NULL;
static volatile bool poolActive = false;

void AInSampleList_Initialize(
                            size_t maxSize,
                            bool dropOnOverflow,
                            const LockProvider* lockPrototype){

    (void)dropOnOverflow;
    (void)lockPrototype;

    // Destroy previous resources if re-initializing (any size change or same size).
    // Must destroy before creating new queue to prevent orphaning the old handle.
    if (samplePool != NULL) {
        AInSampleList_Destroy();
    }

    // Clamp pool size to compile-time limits
    if (maxSize < MIN_AIN_SAMPLE_COUNT) maxSize = MIN_AIN_SAMPLE_COUNT;
    if (maxSize > MAX_AIN_SAMPLE_COUNT) maxSize = MAX_AIN_SAMPLE_COUNT;

    // Clamp further to what the heap can actually fit.
    // Check BEFORE allocating anything so we don't waste heap on an
    // oversized queue that we'd have to delete and recreate.
    if (samplePool == NULL) {
        size_t perSample = sizeof(AInPublicSampleList_t) + sizeof(int16_t);
        size_t heapAvail = xPortGetFreeHeapSize();
        // Reserve 10KB for queue + FreeRTOS overhead + alignment padding
        size_t usable = (heapAvail > 10240) ? (heapAvail - 10240) : 0;
        uint32_t maxFit = (uint32_t)(usable / perSample);

        if (maxSize > maxFit) {
            maxSize = (maxFit >= MIN_AIN_SAMPLE_COUNT) ? maxFit : MIN_AIN_SAMPLE_COUNT;
        }
    }

    queueSize = maxSize;
    analogInputsQueue = xQueueCreate(queueSize, sizeof(AInPublicSampleList_t *));
    configASSERT(analogInputsQueue != NULL);

    // Allocate pool from heap.
    // Total heap cost: maxSize * (sizeof(AInPublicSampleList_t) + sizeof(int16_t))
    //                = maxSize * (208 + 2) = maxSize * 210 bytes
    if (samplePool == NULL) {
        poolCapacity = maxSize;

        samplePool = (AInPublicSampleList_t*)pvPortMalloc(
            poolCapacity * sizeof(AInPublicSampleList_t));
        if (samplePool == NULL) {
            LOG_E("Sample pool alloc failed (%u samples, %u bytes)",
                  (unsigned)poolCapacity,
                  (unsigned)(poolCapacity * sizeof(AInPublicSampleList_t)));
            vQueueDelete(analogInputsQueue);
            analogInputsQueue = NULL;
            queueSize = 0;
            poolCapacity = 0;
            configASSERT(0);
            return;
        }

        nextFree = (int16_t*)pvPortMalloc(poolCapacity * sizeof(int16_t));
        if (nextFree == NULL) {
            vPortFree(samplePool);
            samplePool = NULL;
            vQueueDelete(analogInputsQueue);
            analogInputsQueue = NULL;
            queueSize = 0;
            poolCapacity = 0;
            LOG_E("Sample pool nextFree alloc failed");
            configASSERT(0);
            return;
        }
    }

    // Initialize object pool mutex
    if (poolMutex == NULL) {
        poolMutex = xSemaphoreCreateMutex();
        configASSERT(poolMutex != NULL);
    }

    // Build free list chain: 0 → 1 → 2 → ... → N-1 → -1
    for (uint32_t i = 0; i < poolCapacity - 1; i++) {
        nextFree[i] = (int16_t)(i + 1);
    }
    nextFree[poolCapacity - 1] = -1;  // End of list
    freeHead = 0;  // Start at first entry

    poolActive = true;
}

/**
 * @brief Destroys the sample queue and resets the object pool.
 *
 * Thread safety note: We do NOT take poolMutex at the top of this function.
 * Instead, we use poolActive (32-bit atomic write on PIC32MZ) as a fast-path
 * guard that immediately blocks new AllocateFromPool/FreeToPool calls without
 * mutex contention. The mutex is only taken later (step 5) as a synchronization
 * barrier to ensure in-flight operations complete before freeing memory.
 * This is safe because Destroy is only called from Streaming_Start (task context,
 * streaming stopped) when no ISR or task is allocating from the pool.
 *
 * Teardown sequence (order matters):
 * 1. Block new pool operations (poolActive = false — atomic on PIC32MZ)
 * 2. Atomically capture and nullify queue handle
 * 3. Drain remaining samples
 * 4. Delete FreeRTOS queue
 * 5. Take mutex as barrier, free pool memory
 */
void AInSampleList_Destroy()
{
    // Step 1: Block new pool operations (atomic 32-bit write, no mutex needed)
    poolActive = false;

    // Step 2: Atomically capture and nullify queue handle to prevent further use
    taskENTER_CRITICAL();
    QueueHandle_t q = analogInputsQueue;
    analogInputsQueue = NULL;
    taskEXIT_CRITICAL();

    // Step 3 & 4: Drain queue and delete (using captured handle)
    if (q != NULL) {
        AInPublicSampleList_t* pData;
        while (xQueueReceive(q, &pData, 0) == pdTRUE) {
            // Just drain — we're about to free the whole pool
        }
        vQueueDelete(q);
    }

    // Step 5: Free pool memory with proper synchronization.
    // Take mutex as barrier if available; free unconditionally either way
    // to prevent heap leaks if mutex creation failed during init.
    if (poolMutex != NULL) {
        xSemaphoreTake(poolMutex, portMAX_DELAY);
    }
    if (samplePool != NULL) {
        vPortFree(samplePool);
        samplePool = NULL;
    }
    if (nextFree != NULL) {
        vPortFree(nextFree);
        nextFree = NULL;
    }
    poolCapacity = 0;
    freeHead = -1;
    if (poolMutex != NULL) {
        xSemaphoreGive(poolMutex);
    }

    queueSize = 0;
}

bool AInSampleList_PushBack(const AInPublicSampleList_t* pData){
    if (pData == NULL) {
        return false;
    }

    // Atomically capture queue handle to prevent TOCTOU race with Destroy
    taskENTER_CRITICAL();
    QueueHandle_t q = analogInputsQueue;
    taskEXIT_CRITICAL();

    if (q == NULL) {
        return false;
    }

    BaseType_t queueResult = xQueueSend(q, &pData, AINSAMPLE_QUEUE_TICKS_TO_WAIT);
    return queueResult == pdTRUE;
}
bool AInSampleList_PushBackFromIsr(const AInPublicSampleList_t* pData){
    if (pData == NULL) {
        return false;
    }

    // Atomically capture queue handle (ISR-safe critical section)
    UBaseType_t uxSavedInterruptStatus = taskENTER_CRITICAL_FROM_ISR();
    QueueHandle_t q = analogInputsQueue;
    taskEXIT_CRITICAL_FROM_ISR(uxSavedInterruptStatus);

    if (q == NULL) {
        return false;
    }

    BaseType_t xTaskWokenByReceive = pdFALSE;
    BaseType_t queueResult = xQueueSendFromISR(q, &pData, &xTaskWokenByReceive);

    portEND_SWITCHING_ISR(xTaskWokenByReceive);
    return queueResult == pdTRUE;
}
bool AInSampleList_PopFront( AInPublicSampleList_t** ppData)
{
    if (ppData == NULL) {
        return false;
    }

    // Atomically capture queue handle to prevent TOCTOU race with Destroy
    taskENTER_CRITICAL();
    QueueHandle_t q = analogInputsQueue;
    taskEXIT_CRITICAL();

    if (q == NULL) {
        return false;
    }

    BaseType_t queueResult = xQueueReceive(q, ppData, AINSAMPLE_QUEUE_TICKS_TO_WAIT);
    return queueResult == pdTRUE;
}

bool AInSampleList_PeekFront(AInPublicSampleList_t** ppData)
{
    if (ppData == NULL) {
        return false;
    }

    // Atomically capture queue handle to prevent TOCTOU race with Destroy
    taskENTER_CRITICAL();
    QueueHandle_t q = analogInputsQueue;
    taskEXIT_CRITICAL();

    if (q == NULL) {
        return false;
    }

    BaseType_t queueResult = xQueuePeek(q, ppData, AINSAMPLE_QUEUE_TICKS_TO_WAIT);
    return queueResult == pdTRUE;
}

size_t AInSampleList_Size()
{
    if (queueSize == 0) {
        return 0;
    }

    // Atomically capture queue handle to prevent TOCTOU race with Destroy
    taskENTER_CRITICAL();
    QueueHandle_t q = analogInputsQueue;
    taskEXIT_CRITICAL();

    if (q == NULL) {
        return 0;
    }

    return (queueSize - uxQueueSpacesAvailable(q));
}

bool AInSampleList_IsEmpty()
{
    // Atomically capture queue handle to prevent TOCTOU race with Destroy
    taskENTER_CRITICAL();
    QueueHandle_t q = analogInputsQueue;
    taskEXIT_CRITICAL();

    if (q == NULL) {
        return true;
    }

    AInPublicSampleList_t* pData;
    BaseType_t queueResult = xQueuePeek(q, &pData, 0);
    return queueResult != pdTRUE;
}


// ============================================================================
// Object Pool Implementation (dynamically allocated from heap)
// ============================================================================

AInPublicSampleList_t* AInSampleList_AllocateFromPool() {
    if (!poolActive || poolMutex == NULL) {
        return NULL;
    }

    AInPublicSampleList_t* result = NULL;
    xSemaphoreTake(poolMutex, portMAX_DELAY);

    // O(1) allocation: pop from free list head
    if (freeHead >= 0 && samplePool != NULL) {
        int idx = freeHead;
        freeHead = nextFree[idx];  // Move head to next free
        result = &samplePool[idx];
        // Clear entire structure to ensure no stale data
        memset(result, 0, sizeof(AInPublicSampleList_t));
    }

    xSemaphoreGive(poolMutex);

    return result;
}

void AInSampleList_FreeToPool(AInPublicSampleList_t* pSample) {
    if (pSample == NULL || !poolActive || poolMutex == NULL || samplePool == NULL) {
        return;
    }

    ptrdiff_t index = pSample - samplePool;

    if (index < 0 || (uint32_t)index >= poolCapacity) {
        return;  // Not from our pool
    }

    // O(1) deallocation: push to free list head
    xSemaphoreTake(poolMutex, portMAX_DELAY);
    nextFree[index] = freeHead;
    freeHead = (int16_t)index;
    xSemaphoreGive(poolMutex);
}

size_t AInSampleList_PoolCapacity(void) {
    return poolCapacity;
}
