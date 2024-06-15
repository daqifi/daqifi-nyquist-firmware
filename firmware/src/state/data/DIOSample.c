#include "DIOSample.h"
#include "FreeRTOS.h"
#include "queue.h"

//! Ticks to wait for QUEUE OPERATIONS
#define DIOSAMPLE_QUEUE_TICKS_TO_WAIT               0// No delay

//! Queue handler for DIO values
static QueueHandle_t DIOQueue;
//! Size of the queue, in number of items
static uint32_t queueSize = 0;

void DIOSampleList_Initialize( \
                            DIOSampleList* list, \
                            size_t maxSize, \
                            bool dropOnOverflow, \
                            const LockProvider* lockPrototype){
    
    (void)list;
    (void)maxSize;
    (void)dropOnOverflow;
    (void)lockPrototype;
    
    queueSize = maxSize;
    DIOQueue = xQueueCreate( maxSize, sizeof(DIOSample) );
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
    
    queueResult = xQueueSend( \
                    DIOQueue, \
                    data, \
                    (TickType_t)0 );
    
    return ( queueResult == pdTRUE ) ? true : false; 
}

bool DIOSampleList_PopFront(DIOSampleList* list, DIOSample* data)
{
    BaseType_t queueResult;
    
    (void) list;
    
    if( data == NULL ){
        return false;
    }
    
    queueResult = xQueueReceive( \
                    DIOQueue, \
                    data, \
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
    
    queueResult = xQueuePeek( \
                    DIOQueue, \
                    data, \
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
    
    queueResult = xQueuePeek( \
                    DIOQueue, \
                    &data, \
                    0 );
    /*if( DIOSampleList_Size(NULL) == queueSize ){
        return true;
    }*/
    if( queueResult == pdTRUE ){
        return false;
    }
    return true;
}
