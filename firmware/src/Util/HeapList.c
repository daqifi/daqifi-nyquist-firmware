#include "HeapList.h"

#include <math.h>
#include <string.h>
#include "Util/Logger.h"

#ifndef min
    #define min(x,y) x <= y ? x : y
#endif // min

#ifndef max
    #define max(x,y) x >= y ? x : y
#endif // min

static HeapListNode* HeapList_PopFrontImpl(HeapList* list)
{
    list->Lock.LockFxn(&list->Lock.Handle);
    if (list->Head == NULL)
    {
        list->Lock.UnlockFxn(&list->Lock.Handle);
        return NULL;
    }

    HeapListNode* curr = list->Head;
    HeapListNode* next = curr->Next;
    list->Head = next;
    if (list->Head != NULL)
    {
        list->Head->Prev = NULL;
    }

    if (curr == list->Tail)
    {
        list->Tail = NULL;
    }

    curr->Next = NULL;
    curr->Prev = NULL;

    list->Lock.UnlockFxn(&list->Lock.Handle);
    return curr;
}

void HeapList_Initialize(HeapList* list, size_t maxSize, bool dropOnOverflow, const LockProvider* lockPrototype)
{
    memset(list, 0, sizeof(HeapList));
    list->DropOnOverflow = dropOnOverflow;
    list->MaxSize = maxSize;
    memcpy(&list->Lock, lockPrototype, sizeof(LockProvider));
    list->Lock.LockInitFxn(&list->Lock.Handle);
}

void HeapList_Destroy(HeapList* list)
{
    list->Lock.LockFxn(&list->Lock.Handle);

    HeapListNode* node = HeapList_PopFrontImpl(list);
    while (node != NULL)
    {
        if (node->Data != NULL)
        {
            free(node->Data);
        }
        
        free(node);
        node = HeapList_PopFrontImpl(list);
    }

    list->Lock.UnlockFxn(&list->Lock.Handle);

    list->Lock.LockDestroyFxn(&list->Lock.Handle);
}

bool HeapList_PushBack(HeapList* list, const uint8_t* data, const size_t len)
{
    list->Lock.LockFxn(&list->Lock.Handle);

    HeapListNode* node = NULL;
    size_t currSize = HeapList_Size(list);
    if (currSize >= list->MaxSize)
    {
        if (list->DropOnOverflow)
        {
            node = HeapList_PopFrontImpl(list);
            if (node == NULL)
            {
                list->Lock.UnlockFxn(&list->Lock.Handle);
                return false;
            }

            if (node->Data != NULL)
            {
                free(node->Data);
            }
            
            free(node);
            node = NULL;
        }
        else
        {
            list->Lock.UnlockFxn(&list->Lock.Handle);
            return false;
        }
    }

    node = malloc(sizeof(HeapListNode));

    if(node == NULL)
    {
        LogMessage("malloc fail HeapList.c ln 104\n\r");
        list->Lock.UnlockFxn(&list->Lock.Handle);
        return false;
    }
    
    node->Next = NULL;
    node->Prev = NULL;
    node->Data = malloc(len);

    if(node->Data == NULL)
    {
        LogMessage("malloc fail HeapList.c ln 110\n\r");
        list->Lock.UnlockFxn(&list->Lock.Handle);
        return false;
    }
    node->Size = len;
    memcpy(node->Data, data, node->Size);

    if (list->Head == NULL)
    {
        list->Head = node;
    }

    HeapListNode* lastTail = list->Tail;
    if (lastTail != NULL)
    {
        lastTail->Next = node;
        node->Prev = lastTail;
    }
    list->Tail = node;

    list->Lock.UnlockFxn(&list->Lock.Handle);
    return true;
}

size_t HeapList_PopFront(HeapList* list, uint8_t* data, const size_t maxLen)
{
    list->Lock.LockFxn(&list->Lock.Handle);

    HeapListNode* removed = HeapList_PopFrontImpl(list);
    if (removed == NULL)
    {
        list->Lock.UnlockFxn(&list->Lock.Handle);
        return 0;
    }

    size_t size = min(maxLen, removed->Size);
    if (size > 0)
    {
        memcpy(data, removed->Data, size);
    }
    if (removed->Data != NULL)
    {
        free(removed->Data);
    }
    
    free(removed);

    list->Lock.UnlockFxn(&list->Lock.Handle);
    return size;
}

size_t HeapList_PeekFront(HeapList* list, uint8_t* data, const size_t maxLen)
{
    list->Lock.LockFxn(&list->Lock.Handle);

    HeapListNode* peekNode = list->Head;
    if (peekNode == NULL)
    {
        list->Lock.UnlockFxn(&list->Lock.Handle);
        return 0;
    }

    size_t size = min(maxLen, peekNode->Size);
    if (size > 0)
    {
        memcpy(data, peekNode->Data, size);
    }
    
    list->Lock.UnlockFxn(&list->Lock.Handle);
    return size;
}

size_t HeapList_Size(HeapList* list)
{
    list->Lock.LockFxn(&list->Lock.Handle);

    size_t result = 0;
    HeapListNode* curr = list->Head;
    while (curr != NULL)
    {
        result += 1;
        curr = curr->Next;
    }

    list->Lock.UnlockFxn(&list->Lock.Handle);
    return result;
}

bool HeapList_IsEmpty(HeapList* list)
{
    list->Lock.LockFxn(&list->Lock.Handle);
    
    bool result = list->Head == NULL;
    list->Lock.UnlockFxn(&list->Lock.Handle);
    
    return result;  
}
