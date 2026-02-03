/*! @file AInSample.c
 * @brief Analog input sample queue and object pool implementation.
 *
 * Implements a FreeRTOS queue for streaming analog input samples between the
 * ADC interrupt handler and the streaming task. Uses a pre-allocated object pool
 * with O(1) free list allocation to eliminate heap overhead at high sample rates.
 *
 * Performance characteristics:
 * - Queue: Standard FreeRTOS queue (thread-safe, ISR-safe)
 * - Pool allocation: O(1) via free list (no fragmentation, deterministic timing)
 * - Pool size: MAX_AIN_SAMPLE_COUNT (700) samples for high-speed streaming
 *
 * @author Javier Longares Abaiz - When Technology becomes art.
 * @web www.javierlongares.com
 */

#include "AInSample.h"
#include "FreeRTOS.h"
#include "queue.h"
#include <string.h>  // For memset

//! Ticks to wait for QUEUE OPERATIONS
#define AINSAMPLE_QUEUE_TICKS_TO_WAIT               0  // No delay

//! Queue handler for Analog inputs
static QueueHandle_t analogInputsQueue;
//! Size of the queue, in number of items
static uint32_t queueSize = 0;

// =============================================================================
// Object Pool Configuration
// =============================================================================
// Pool size uses MAX_AIN_SAMPLE_COUNT directly to ensure consistency.
// This eliminates heap allocation/deallocation overhead during streaming.
static AInPublicSampleList_t samplePool[MAX_AIN_SAMPLE_COUNT];

// =============================================================================
// Free List Data Structures (O(1) allocation/deallocation)
// =============================================================================
// The free list is a singly-linked list embedded in the nextFree array.
// Each free slot contains the index of the next free slot, forming a chain.
// Allocation pops from head, deallocation pushes to head - both O(1).
//
// Example state with slots 0,2,5 free:
//   freeHead = 0
//   nextFree[0] = 2, nextFree[2] = 5, nextFree[5] = -1 (end)
//   Chain: 0 -> 2 -> 5 -> END
static int16_t freeHead = -1;              // Index of first free slot (-1 = empty)
static int16_t nextFree[MAX_AIN_SAMPLE_COUNT]; // Each slot points to next free slot

static SemaphoreHandle_t poolMutex = NULL;
static volatile bool poolActive = false;  // Guards against teardown races

// Compile-time assertion to ensure pool size matches expected queue size
_Static_assert(MAX_AIN_SAMPLE_COUNT >= 20, "Pool must be at least 20 for queue");

void AInSampleList_Initialize(
                            size_t maxSize,
                            bool dropOnOverflow,
                            const LockProvider* lockPrototype){


    (void)dropOnOverflow;
    (void)lockPrototype;

    // Enforce invariant: queue size cannot exceed pool capacity
    configASSERT(maxSize <= MAX_AIN_SAMPLE_COUNT);

    queueSize = maxSize;
    analogInputsQueue = xQueueCreate(queueSize, sizeof(AInPublicSampleList_t *));
    configASSERT(analogInputsQueue != NULL);

    // Initialize object pool
    if (poolMutex == NULL) {
        poolMutex = xSemaphoreCreateMutex();
    }

    // Build free list chain: 0 → 1 → 2 → ... → N-1 → -1
    for (int i = 0; i < MAX_AIN_SAMPLE_COUNT - 1; i++) {
        nextFree[i] = i + 1;
    }
    nextFree[MAX_AIN_SAMPLE_COUNT - 1] = -1;  // End of list
    freeHead = 0;  // Start at first entry

    poolActive = true;
}

/**
 * @brief Destroys the sample queue and resets the object pool.
 *
 * Teardown sequence (order matters for thread safety):
 * 1. Block new pool operations (poolActive = false)
 * 2. Atomically capture and nullify queue handle
 * 3. Drain remaining samples back to pool
 * 4. Delete FreeRTOS resources
 * 5. Reset pool to initial state
 */
void AInSampleList_Destroy()
{
    // Step 1: Block new pool operations first
    poolActive = false;

    // Step 2: Atomically capture and nullify queue handle to prevent further use
    taskENTER_CRITICAL();
    QueueHandle_t q = analogInputsQueue;
    analogInputsQueue = NULL;
    taskEXIT_CRITICAL();

    // Step 3 & 4: Drain queue and delete (using captured handle)
    // Drain until xQueueReceive fails - more robust than checking message count
    if (q != NULL) {
        AInPublicSampleList_t* pData;
        while (xQueueReceive(q, &pData, 0) == pdTRUE) {
            if (pData != NULL) {
                AInSampleList_FreeToPool(pData);
            }
        }
        vQueueDelete(q);
    }

    // Step 5: Reset pool to initial state with proper synchronization
    // Use poolMutex (not critical section) to ensure all in-flight Allocate/Free
    // operations complete before resetting the free list. This prevents corruption
    // if a task allocated a sample before poolActive was set to false.
    //
    // NOTE: We intentionally do NOT delete poolMutex here to prevent UAF.
    // The mutex persists for system lifetime (~80 bytes).
    if (poolMutex != NULL) {
        xSemaphoreTake(poolMutex, portMAX_DELAY);  // Synchronization barrier
        // Rebuild free list chain: 0 -> 1 -> 2 -> ... -> N-1 -> END
        for (int i = 0; i < MAX_AIN_SAMPLE_COUNT - 1; i++) {
            nextFree[i] = i + 1;
        }
        nextFree[MAX_AIN_SAMPLE_COUNT - 1] = -1;
        freeHead = 0;
        xSemaphoreGive(poolMutex);
    }
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
// Object Pool Implementation (replaces heap allocation/deallocation)
// ============================================================================

AInPublicSampleList_t* AInSampleList_AllocateFromPool() {
    if (!poolActive || poolMutex == NULL) {
        return NULL;
    }

    AInPublicSampleList_t* result = NULL;
    xSemaphoreTake(poolMutex, portMAX_DELAY);

    // O(1) allocation: pop from free list head
    if (freeHead >= 0) {
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
    if (pSample == NULL || !poolActive || poolMutex == NULL) {
        return;
    }

    ptrdiff_t index = pSample - samplePool;

    if (index < 0 || index >= MAX_AIN_SAMPLE_COUNT) {
        return;  // Not from our pool
    }

    // O(1) deallocation: push to free list head
    xSemaphoreTake(poolMutex, portMAX_DELAY);
    nextFree[index] = freeHead;
    freeHead = (int16_t)index;
    xSemaphoreGive(poolMutex);
}
