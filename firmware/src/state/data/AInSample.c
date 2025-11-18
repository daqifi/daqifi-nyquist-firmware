/*! @file AInSample.c
 * @brief File with the implementation of the functionality to send and receive
 * messages with the data coming from Analog Inputs.
 * @author Javier Longares Abaiz - When Technology becomes art.
 * @web www.javierlongares.com
 */

#include "AInSample.h"
#include "FreeRTOS.h"
#include "queue.h"

//! Ticks to wait for QUEUE OPERATIONS
#define AINSAMPLE_QUEUE_TICKS_TO_WAIT               0  // No delay

//! Queue handler for Analog inputs
static QueueHandle_t analogInputsQueue;
//! Size of the queue, in number of items
static uint32_t queueSize = 0;

// Object pool for sample structures (eliminates heap allocation/free overhead)
#define SAMPLE_POOL_SIZE 20  // Must be >= queue size
static AInPublicSampleList_t samplePool[SAMPLE_POOL_SIZE];
static uint8_t poolInUse[SAMPLE_POOL_SIZE];  // 0=free, 1=in use
static SemaphoreHandle_t poolMutex = NULL;

void AInSampleList_Initialize(
                            size_t maxSize,
                            bool dropOnOverflow,
                            const LockProvider* lockPrototype){


    (void)maxSize;
    (void)dropOnOverflow;
    (void)lockPrototype;

    queueSize = maxSize;
    analogInputsQueue = xQueueCreate( maxSize, sizeof(AInPublicSampleList_t *) );

    // Initialize object pool
    if (poolMutex == NULL) {
        poolMutex = xSemaphoreCreateMutex();
    }

    // Mark all pool entries as free
    for (int i = 0; i < SAMPLE_POOL_SIZE; i++) {
        poolInUse[i] = 0;
    }
}

void AInSampleList_Destroy()
{
    if (analogInputsQueue != NULL) {
        AInPublicSampleList_t* pData;
        while (!AInSampleList_IsEmpty()) {
            if (AInSampleList_PopFront(&pData)) {
                if (pData != NULL) {
                    AInSampleList_FreeToPool(pData);  // Use pool instead of vPortFree!
                    pData = NULL;
                }
            }
        }
        vQueueDelete(analogInputsQueue);
        analogInputsQueue = NULL;
    }
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
    if (poolMutex == NULL) {
        return NULL;
    }

    AInPublicSampleList_t* result = NULL;
    xSemaphoreTake(poolMutex, portMAX_DELAY);

    for (int i = 0; i < SAMPLE_POOL_SIZE; i++) {
        if (poolInUse[i] == 0) {
            poolInUse[i] = 1;
            result = &samplePool[i];
            break;
        }
    }

    xSemaphoreGive(poolMutex);

    if (result != NULL) {
        memset(result, 0, sizeof(AInPublicSampleList_t));
    }

    return result;
}

void AInSampleList_FreeToPool(AInPublicSampleList_t* pSample) {
    if (pSample == NULL) {
        return;
    }

    ptrdiff_t index = pSample - samplePool;

    if (index < 0 || index >= SAMPLE_POOL_SIZE) {
        return;
    }

    poolInUse[index] = 0;
}
