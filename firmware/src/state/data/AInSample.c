/*! @file AInSample.c
 * @brief File with the implementation of the functionality to send and receive
 * messages with the data coming from Analog Inputs.
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

// Object pool for sample structures (eliminates heap allocation/free overhead)
// Must match queue size initialized in AInSampleList_Initialize()
#define SAMPLE_POOL_SIZE 512
static AInPublicSampleList_t samplePool[SAMPLE_POOL_SIZE];

// Free list for O(1) allocation/deallocation (replaces linear scan)
static int16_t freeHead = -1;              // Index of first free slot (-1 = empty)
static int16_t nextFree[SAMPLE_POOL_SIZE]; // Each slot points to next free

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

    // Clamp queue size to pool size to prevent exhaustion
    queueSize = (maxSize > SAMPLE_POOL_SIZE) ? SAMPLE_POOL_SIZE : maxSize;
    analogInputsQueue = xQueueCreate( queueSize, sizeof(AInPublicSampleList_t *) );

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

void AInSampleList_Destroy()
{
    // Block new pool operations first
    poolActive = false;

    // Atomically capture and nullify queue handle to prevent further use
    taskENTER_CRITICAL();
    QueueHandle_t q = analogInputsQueue;
    analogInputsQueue = NULL;
    taskEXIT_CRITICAL();

    // Drain queue safely (using captured handle)
    if (q != NULL) {
        AInPublicSampleList_t* pData;
        while (uxQueueMessagesWaiting(q) > 0) {
            if (xQueueReceive(q, &pData, 0) == pdTRUE) {
                if (pData != NULL) {
                    AInSampleList_FreeToPool(pData);
                }
            }
        }
        vQueueDelete(q);
    }

    // Atomically delete mutex and reset pool
    taskENTER_CRITICAL();
    if (poolMutex != NULL) {
        vSemaphoreDelete(poolMutex);
        poolMutex = NULL;
    }
    // Rebuild free list to initial state
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
