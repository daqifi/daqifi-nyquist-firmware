/*! @file AInSample.c
 * @brief File with the implementation of the functionality to send and receive
 * messages with the data coming from Analog Inputs.
 * @author Javier Longares Abaiz - When Technology becomes art.
 * @web www.javierlongares.com
 */

#include "AInSample.h"

#if USE_HEAPLIST == 1
void AInSampleList_Initialize(AInSampleList* list, size_t maxSize, bool dropOnOverflow, const LockProvider* lockPrototype)
{
    HeapList_Initialize(&list->List, maxSize, dropOnOverflow, lockPrototype);
}

void AInSampleList_Destroy(AInSampleList* list)
{
    HeapList_Destroy(&list->List);
}

bool AInSampleList_PushBack(AInSampleList* list, const AInSample* data)
{
    return HeapList_PushBack(&list->List, (const uint8_t*)data, sizeof(AInSample));
}

bool AInSampleList_PopFront(AInSampleList* list, AInSample* data)
{
    return HeapList_PopFront(&list->List, (uint8_t*)data, sizeof(AInSample));
}

bool AInSampleList_PeekFront(AInSampleList* list, AInSample* data)
{
    return HeapList_PeekFront(&list->List, (uint8_t*)data, sizeof(AInSample));
}

size_t AInSampleList_Size(AInSampleList* list)
{
    return HeapList_Size(&list->List);
}

bool AInSampleList_IsEmpty(AInSampleList* list)
{
    return HeapList_IsEmpty(&list->List);
}
#else
#include "FreeRTOS.h"
#include "queue.h"

//! Ticks to wait for QUEUE OPERATIONS
#define AINSAMPLE_QUEUE_TICKS_TO_WAIT               1000

//! Queue handler for Analog inputs
static QueueHandle_t analogInputsQueue;
//! Size of the queue, in number of items
static uint32_t queueSize = 0;

void AInSampleList_Initialize( \
                            AInSampleList* list, \
                            size_t maxSize, \
                            bool dropOnOverflow, \
                            const LockProvider* lockPrototype){
    
    (void)list;
    (void)maxSize;
    (void)dropOnOverflow;
    (void)lockPrototype;
    
    queueSize = maxSize;
    analogInputsQueue = xQueueCreate( maxSize, sizeof(AInSample) );
}

void AInSampleList_Destroy(AInSampleList* list)
{
    (void)list;
    
    vQueueDelete( analogInputsQueue );
}

bool AInSampleList_PushBack(AInSampleList* list, const AInSample* data){
    BaseType_t queueResult;
    
    if( data == NULL ){
        return false;
    }
    
    (void)list;
    
    queueResult = xQueueSendFromISR( \
                    analogInputsQueue, \
                    data, \
                    NULL );
    return ( queueResult == pdTRUE ) ? true : false; 
}

bool AInSampleList_PopFront(AInSampleList* list, AInSample* data)
{
    BaseType_t queueResult;
    
    (void) list;
    
    if( data == NULL ){
        return false;
    }
    
    queueResult = xQueueReceive( \
                    analogInputsQueue, \
                    data, \
                    AINSAMPLE_QUEUE_TICKS_TO_WAIT );
    return ( queueResult == pdTRUE ) ? true : false; 
}

bool AInSampleList_PeekFront(AInSampleList* list, AInSample* data)
{
    BaseType_t queueResult;
    
    (void)list;
    
    if( data == NULL ){
        return false;
    }
    
    queueResult = xQueuePeek( \
                    analogInputsQueue, \
                    data, \
                    AINSAMPLE_QUEUE_TICKS_TO_WAIT );
    return ( queueResult == pdTRUE ) ? true : false; 
}

size_t AInSampleList_Size(AInSampleList* list)
{
    (void)list;
    
    if( queueSize == 0 ){
        return 0;
    }
    return (queueSize - uxQueueSpacesAvailable( analogInputsQueue ) );
}

bool AInSampleList_IsEmpty(AInSampleList* list)
{
    (void)list;
    
    if( AInSampleList_Size(NULL) == queueSize ){
        return true;
    }
    return false;
}
#endif /* USE_HEAP_LIST */