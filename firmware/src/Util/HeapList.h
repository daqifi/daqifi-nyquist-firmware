#pragma once

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "Util/LockProvider.h"

/**
 * A single item in the list
 */
typedef struct s_HeapListNode
{
    /**
     * The actual size of data in the node
     */
    size_t Size;

    /**
     * The data stores in the node
     */
    uint8_t* Data;

    /**
     * Points to the previous item in the list
     */
    struct s_HeapListNode* Prev;

    /**
     * Points tot he next item in the list
     */
    struct s_HeapListNode* Next;
} HeapListNode;

/**
 * Represents a linked list
 */
typedef struct s_HeapList
{
    /**
    * Locks the list for thread safety
    */
    LockProvider Lock;

    /**
     * The start of the list
     */
    HeapListNode* Head;

    /**
     * The end of the list
     */
    HeapListNode* Tail;

    /**
    * The maximum size of the list
    */
    size_t MaxSize;

    /**
     * Indicates whether the list will drop or assert on overflow
     */
    bool DropOnOverflow;
} HeapList;

/**
* Initializes the list
* @param list The list to initialize
* @param maxSize The maximum number of items in the list
* @param dropOnOverFlow Set true to drop the first element when adding to a full list (false to error out)
* @param lockPrototype The lock provider to use
*/
void HeapList_Initialize(HeapList* list, size_t maxSize, bool dropOnOverflow, const LockProvider* lockPrototype);

/**
 * Destroys the list
 * @param list The list to destroy
 */
void HeapList_Destroy(HeapList* list);

/**
 * Adds data to the back of the list
 * @param list The list to add to
 * @param data The data to add
 * @param len The length of the data
 * @return True on success, false on failure
 */
bool HeapList_PushBack(HeapList* list, const uint8_t* data, const size_t len);

/**
 * Removes and returns the first item in the list
 * @param list The list to modify
 * @param data Storage for the returned data
 * @param maxLen The maximum size of the data to return
 * @return The size of the returned data
 */
size_t HeapList_PopFront(HeapList* list, uint8_t* data, const size_t maxLen);

/**
 * Returns the first item in the list
 * @param list The list to modify
 * @param data Storage for the returned data
 * @param maxLen The maximum size of the data to return
 * @return The size of the returned data
 */
size_t HeapList_PeekFront(HeapList* list, uint8_t* data, const size_t maxLen);

/**
 * Gets the number of items currently in the list
 * @param list The list to modify
 * @return The current size of the list
 */
size_t HeapList_Size(HeapList* list);

/**
 * Indicates whether there is data in the list
 * @param list The list to check
 * @return True if the list is empty, false otherwise
 */
bool HeapList_IsEmpty(HeapList* list);

#ifdef	__cplusplus
}
#endif
