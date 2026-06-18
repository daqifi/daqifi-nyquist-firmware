#include "DIOSample.h"
#include "FreeRTOS.h"
#include "queue.h"

//! Ticks to wait for QUEUE OPERATIONS
#define DIOSAMPLE_QUEUE_TICKS_TO_WAIT               0// No delay

//! Queue handler for DIO values
static QueueHandle_t DIOQueue;
//! Size of the queue, in number of items
static uint32_t queueSize = 0;

void DIOSampleList_Initialize(
                            DIOSampleList* list,
                            size_t maxSize,
                            bool dropOnOverflow){

    (void)list;
    (void)dropOnOverflow;

    // Delete existing queue to avoid leaking the handle on re-init.
    // Matches the pattern in AInSample.c.
    if (DIOQueue != NULL) {
        vQueueDelete(DIOQueue);
        DIOQueue = NULL;
    }

    queueSize = maxSize;
    DIOQueue = xQueueCreate( maxSize, sizeof(DIOSample) );
    configASSERT(DIOQueue != NULL);
}

void DIOSampleList_Destroy(DIOSampleList* list)
{
    (void)list;
    
    vQueueDelete( DIOQueue );
}

bool DIOSampleList_PushBack(DIOSampleList* list, const DIOSample* data){
    BaseType_t queueResult;
    
    if( data == NULL ){
        return false;
    }
    
    (void)list;
    
    queueResult = xQueueSend( 
                    DIOQueue, 
                    data, 
                    (TickType_t)0 );
    
    return ( queueResult == pdTRUE ) ? true : false; 
}

/* ISR producer (#525): the streaming-timer ISR now captures DIO samples
 * directly (DIO_StreamingTrigger moved into ISR context). xQueueSendFromISR
 * is the ISR-safe send; the consumer (encoder) polls DIOQueue with a 0-tick
 * timeout and is never blocked on it, so this never wakes a task. */
bool DIOSampleList_PushBackFromISR(DIOSampleList* list, const DIOSample* data){
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    BaseType_t queueResult;

    if( data == NULL ){
        return false;
    }

    (void)list;

    queueResult = xQueueSendFromISR( DIOQueue, data, &xHigherPriorityTaskWoken );
    (void)xHigherPriorityTaskWoken;  /* consumer polls; never blocked on DIOQueue */

    return ( queueResult == pdTRUE ) ? true : false;
}

bool DIOSampleList_PopFront(DIOSampleList* list, DIOSample* data)
{
    BaseType_t queueResult;
    
    (void) list;
    
    if( data == NULL ){
        return false;
    }
    
    queueResult = xQueueReceive( 
                    DIOQueue, 
                    data, 
                    DIOSAMPLE_QUEUE_TICKS_TO_WAIT );
    return ( queueResult == pdTRUE ) ? true : false; 
}

bool DIOSampleList_PeekFront(DIOSampleList* list, DIOSample* data)
{
    BaseType_t queueResult;
    
    (void)list;
    
    if( data == NULL ){
        return false;
    }
    
    queueResult = xQueuePeek( 
                    DIOQueue, 
                    data, 
                    DIOSAMPLE_QUEUE_TICKS_TO_WAIT );
    return ( queueResult == pdTRUE ) ? true : false; 
}

size_t DIOSampleList_Size(DIOSampleList* list)
{
    (void)list;
    
    if( queueSize == 0 ){
        return 0;
    }
    return (queueSize - uxQueueSpacesAvailable( DIOQueue ) );
}

bool DIOSampleList_IsEmpty(DIOSampleList* list)
{
    (void)list;
    DIOSample data;
    BaseType_t queueResult;
    
    queueResult = xQueuePeek( 
                    DIOQueue, 
                    &data, 
                    0 );
    /*if( DIOSampleList_Size(NULL) == queueSize ){
        return true;
    }*/
    if( queueResult == pdTRUE ){
        return false;
    }
    return true;
}
