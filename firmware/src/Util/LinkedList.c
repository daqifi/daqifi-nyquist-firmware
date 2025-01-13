#include <string.h>

#include "LinkedList.h"

LinkedList* LinkedList_Initialize(LinkedList* list, bool dropOnOverflow)
{
    memset(list, 0, sizeof(LinkedList));
    list->DropOnOverflow = dropOnOverflow;
    return list;
}

static LinkedListNode* LinkedList_Tail(LinkedList* list)
{
    if (list->Head == NULL)
    {
        return NULL;
    }
    
    LinkedListNode* curr = list->Head;
    while(curr->Next != NULL)
    {
        curr = curr->Next;
    }
    
    return curr;
}

static LinkedListNode* LinkedList_FreeNode(LinkedList* list)
{
    LinkedListNode* node = NULL;
    size_t i = 0;
    for (i=0; i<LINKED_LIST_MAX_SIZE; ++i)
    {
        if (list->Data[i].Size < 1)
        {
            node = &list->Data[i];
            break;
        }
    }
    
    return node;
}

static LinkedListNode* LinkedList_PopFrontImpl(LinkedList* list)
{
    if (list->Head == NULL)
    {
        return NULL;
    }
    
    LinkedListNode* curr = list->Head;
    LinkedListNode* next = curr->Next;
    list->Head = next;
    if (list->Head != NULL)
    {
        list->Head->Prev = NULL;
    }
    
    curr->Next = NULL;
    curr->Prev = NULL;
    
    return curr;
}

bool LinkedList_PushBack(LinkedList* list, const uint8_t* data, size_t len)
{
    LinkedListNode* node = LinkedList_FreeNode(list);
    if (node == NULL && list->DropOnOverflow)
    {
        node = LinkedList_PopFrontImpl(list);
    }
    
    if (node == NULL)
    {
        return false;
    }
    
    node->Size = min(len, LINKED_LIST_NODE_SIZE);
    memcpy(node->Data, data, node->Size);
    
    LinkedListNode* tail = LinkedList_Tail(list);
    
    if (tail == NULL)
    {
        list->Head = node;
    }
    else
    {
        tail->Next = node;
        node->Prev = tail;
    }
    
    return true;
}

size_t LinkedList_PopFront(LinkedList* list, uint8_t* data, size_t maxLen)
{
    if (list->Head == NULL)
    {
        return 0;
    }
    
    LinkedListNode* removed = LinkedList_PopFrontImpl(list);
    size_t size = min(maxLen, removed->Size);
    if (size > 0)
    {
        memcpy(data, removed->Data, size);
    }
    
    removed->Size = 0;
    
    return size;
}

size_t LinkedList_Size(LinkedList* list)
{
    size_t result = 0;
    LinkedListNode* curr = list->Head;
    while (curr != NULL)
    {
        result += 1;
        curr = curr->Next;
    }
    
    return result;
}
