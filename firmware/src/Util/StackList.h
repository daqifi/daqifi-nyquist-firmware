#pragma once

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "Util/LockProvider.h"

// Defines the maximum size of a single entity
#define STACK_LIST_NODE_SIZE 128

// Defines the maximum number of items in the list
#define STACK_LIST_MAX_SIZE 12

/**
 * A single item in the list
 */
typedef struct s_StackListNode
{
    /**
     * The actual size of data in the node
     */
    size_t Size;

    /**
     * The data stores in the node
     */
    uint8_t Data[STACK_LIST_NODE_SIZE];

    /**
     * Points to the previous item in the list
     */
    struct s_StackListNode* Prev;

    /**
     * Points tot he next item in the list
     */
    struct s_StackListNode* Next;
} StackListNode;

/**
 * Represents a linked list
 */
typedef struct s_StackList
{
    /**
    * Locks the list for thread safety
    */
    LockProvider Lock;

    /**
     * Storage for the linked list
     */
    StackListNode Data[STACK_LIST_MAX_SIZE];

    /**
     * The start of the list
     */
    StackListNode* Head;

    /**
     * The start of the list
     */
    StackListNode* Tail;

    /**
     * Indicates whether the list will drop or assert on overflow
     */
    bool DropOnOverflow;
} StackList;

/**
 * Initializes the linked list
 * @param list The list to initialize
 * @param dropOnOverflow Indicates whether the list will drop on overflow
 */
void StackList_Initialize(StackList* list, bool dropOnOverflow, const LockProvider* lockPrototype);

/**
 * Destroys the list
 * @param list The list to destroy
 */
void StackList_Destroy(StackList* list);

/**
 * Adds data to the back of the list
 * @param list The list to add to
 * @param data The data to add
 * @param len The length of the data
 * @return True on success, false on failure
 */
bool StackList_PushBack(StackList* list, const uint8_t* data, const size_t len);

/**
 * Removes and returns the first item in the list
 * @param list The list to modify
 * @param data Storage for the returned data
 * @param maxLen The maximum size of the data to return
 * @return The size of the returned data (0 if nothing is copied into data)
 */
size_t StackList_PopFront(StackList* list, uint8_t* data, const size_t maxLen);

/**
 * Gets the number of items currently in the list
 * @param list The list to modify
 * @return The current size of the list
 */
size_t StackList_Size(StackList* list);

/**
 * Indicates whether there is data in the list
 * @param list The list to check
 * @return True if the list is empty, false otherwise
 */
bool StackList_IsEmpty(StackList* list);

#ifdef	__cplusplus
}
#endif
