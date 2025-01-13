#include "StackList.h"

#include <math.h>
#include <string.h>

#ifndef min
    #define min(x,y) x <= y ? x : y
#endif // min

#ifndef max
    #define max(x,y) x >= y ? x : y
#endif // min

static StackListNode* StackList_FreeNode(StackList* list)
{
    StackListNode* node = NULL;
    size_t i = 0;
    for (i=0; i<STACK_LIST_MAX_SIZE; ++i)
    {
        if (list->Data[i].Size < 1)
        {
            node = &list->Data[i];
            break;
        }
    }

    return node;
}

static StackListNode* StackList_PopFrontImpl(StackList* list)
{
    if (list->Head == NULL)
    {
        return NULL;
    }

    StackListNode* curr = list->Head;
    StackListNode* next = curr->Next;
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

    return curr;
}

void StackList_Initialize(StackList* list, bool dropOnOverflow, const LockProvider* lockPrototype)
{
    memset(list, 0, sizeof(StackList));
    list->DropOnOverflow = dropOnOverflow;
    memcpy(&list->Lock, lockPrototype, sizeof(LockProvider));
    list->Lock.LockInitFxn(&list->Lock.Handle);
}

void StackList_Destroy(StackList* list)
{
    list->Lock.LockDestroyFxn(&list->Lock.Handle);
}

bool StackList_PushBack(StackList* list, const uint8_t* data, const size_t len)
{
    list->Lock.LockFxn(&list->Lock.Handle);

    StackListNode* node = StackList_FreeNode(list);
    if (node == NULL && list->DropOnOverflow)
    {
        node = StackList_PopFrontImpl(list);
    }

    if (node == NULL)
    {
        list->Lock.UnlockFxn(&list->Lock.Handle);
        return false;
    }

    node->Size = min(len, STACK_LIST_NODE_SIZE);
    memcpy(node->Data, data, node->Size);

    if (list->Head == NULL)
    {
        list->Head = node;
    }

    StackListNode* lastTail = list->Tail;
    if (lastTail != NULL)
    {
        lastTail->Next = node;
        node->Prev = lastTail;
    }
    list->Tail = node;

    list->Lock.UnlockFxn(&list->Lock.Handle);
    return true;
}

size_t StackList_PopFront(StackList* list, uint8_t* data, const size_t maxLen)
{
    list->Lock.LockFxn(&list->Lock.Handle);

    if (list->Head == NULL)
    {
        list->Lock.UnlockFxn(&list->Lock.Handle);
        return 0;
    }

    StackListNode* removed = StackList_PopFrontImpl(list);
    size_t size = min(maxLen, removed->Size);
    if (size > 0)
    {
        memcpy(data, removed->Data, size);
    }

    removed->Size = 0;

    list->Lock.UnlockFxn(&list->Lock.Handle);
    return size;
}

size_t StackList_Size(StackList* list)
{
    list->Lock.LockFxn(&list->Lock.Handle);

    size_t result = 0;
    StackListNode* curr = list->Head;
    while (curr != NULL)
    {
        result += 1;
            curr = curr->Next;
        }

    list->Lock.UnlockFxn(&list->Lock.Handle);
    return result;
}

bool StackList_IsEmpty(StackList* list)
{
    list->Lock.LockFxn(&list->Lock.Handle);
    
    bool result = list->Head != NULL;
    list->Lock.UnlockFxn(&list->Lock.Handle);
    
    return result;
}