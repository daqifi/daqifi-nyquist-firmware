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
 * - Pool size: 512 samples to support 15kHz+ streaming
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
// Pool size must match MAX_AIN_SAMPLE_COUNT in AInSample.h for consistency.
// This eliminates heap allocation/deallocation overhead during streaming.
#define SAMPLE_POOL_SIZE 512
static AInPublicSampleList_t samplePool[SAMPLE_POOL_SIZE];

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
static int16_t nextFree[SAMPLE_POOL_SIZE]; // Each slot points to next free slot

static SemaphoreHandle_t poolMutex = NULL;
static volatile bool poolActive = false;  // Guards against teardown races

// Compile-time assertion to ensure pool size matches expected queue size
_Static_assert(SAMPLE_POOL_SIZE >= 20, "Pool must be at least 20 for queue");

void AInSampleList_Initialize(
                            size_t maxSize,
                            bool dropOnOverflow,
                            const LockProvider* lockPrototype){


    (void)dropOnOverflow;
    (void)lockPrototype;

    // Enforce invariant: queue size cannot exceed pool capacity
    configASSERT(maxSize <= SAMPLE_POOL_SIZE);

    queueSize = maxSize;
    analogInputsQueue = xQueueCreate(queueSize, sizeof(AInPublicSampleList_t *));
    configASSERT(analogInputsQueue != NULL);

    // Initialize object pool
    if (poolMutex == NULL) {
        poolMutex = xSemaphoreCreateMutex();
    }

    // Build free list chain: 0 → 1 → 2 → ... → N-1 → -1
    for (int i = 0; i < SAMPLE_POOL_SIZE - 1; i++) {
        nextFree[i] = i + 1;
    }
    nextFree[SAMPLE_POOL_SIZE - 1] = -1;  // End of list
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

    // Step 5: Reset pool to initial state (keep mutex for next session)
    // NOTE: We intentionally do NOT delete poolMutex here to prevent UAF.
    // If Allocate/Free is racing with Destroy, the mutex must remain valid
    // until the racing operation completes. The mutex is only ~80 bytes and
    // persists for system lifetime.
    taskENTER_CRITICAL();
    // Rebuild free list chain: 0 -> 1 -> 2 -> ... -> N-1 -> END
    for (int i = 0; i < SAMPLE_POOL_SIZE - 1; i++) {
        nextFree[i] = i + 1;
    }
    nextFree[SAMPLE_POOL_SIZE - 1] = -1;
    freeHead = 0;
    taskEXIT_CRITICAL();
}

bool AInSampleList_PushBack(const AInPublicSampleList_t* pData){
    if (pData == NULL || analogInputsQueue == NULL) {
        return false;
    }

    BaseType_t queueResult = xQueueSend(analogInputsQueue, &pData, AINSAMPLE_QUEUE_TICKS_TO_WAIT);

    return queueResult == pdTRUE;
}
bool AInSampleList_PushBackFromIsr(const AInPublicSampleList_t* pData){
    if (pData == NULL || analogInputsQueue == NULL) {
        return false;
    }

    BaseType_t xTaskWokenByReceive = pdFALSE;
    BaseType_t queueResult = xQueueSendFromISR(analogInputsQueue, &pData, &xTaskWokenByReceive);

    portEND_SWITCHING_ISR(xTaskWokenByReceive);
    return queueResult == pdTRUE;
}
bool AInSampleList_PopFront( AInPublicSampleList_t** ppData)
{
    if (ppData == NULL || analogInputsQueue == NULL) {
        return false;
    }

    BaseType_t queueResult = xQueueReceive(analogInputsQueue, ppData, AINSAMPLE_QUEUE_TICKS_TO_WAIT);
    return queueResult == pdTRUE;
}

bool AInSampleList_PeekFront(AInPublicSampleList_t** ppData)
{
    if (ppData == NULL || analogInputsQueue == NULL) {
        return false;
    }

    BaseType_t queueResult = xQueuePeek(analogInputsQueue, ppData, AINSAMPLE_QUEUE_TICKS_TO_WAIT);
    return queueResult == pdTRUE;
}

size_t AInSampleList_Size()
{
  if (queueSize == 0 || analogInputsQueue == NULL) {
        return 0;
    }
    return (queueSize - uxQueueSpacesAvailable(analogInputsQueue));
}

bool AInSampleList_IsEmpty()
{
    if (analogInputsQueue == NULL) {
        return true;
    }

    AInPublicSampleList_t* pData;
    BaseType_t queueResult = xQueuePeek(analogInputsQueue, &pData, 0);
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

    if (index < 0 || index >= SAMPLE_POOL_SIZE) {
        return;  // Not from our pool
    }

    // O(1) deallocation: push to free list head
    xSemaphoreTake(poolMutex, portMAX_DELAY);
    nextFree[index] = freeHead;
    freeHead = (int16_t)index;
    xSemaphoreGive(poolMutex);
}
