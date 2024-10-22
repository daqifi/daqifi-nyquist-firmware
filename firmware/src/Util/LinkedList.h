/* 
 * File:   LinkedList.h
 * Author: Daniel
 * A linked list that is stored on the stack
 * Created on October 6, 2016, 10:45 AM
 */

#pragma once

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

// Defines the maximum size of a single entity
#define LINKED_LIST_NODE_SIZE 128

/**
 * A single item in the list
 */
typedef struct s_LinkedListNode {
    /**
     * The actual size of data in the node
     */
    size_t Size;
    
    /**
     * The data stores in the node
     */
    uint8_t Data[LINKED_LIST_NODE_SIZE];
    
    /**
     * Points to the previous item in the list
     */
    struct s_LinkedListNode* Prev;
    
    /**
     * Points tot he next item in the list
     */
    struct s_LinkedListNode* Next;
} LinkedListNode;

// Defines the maximum number of items in the list
#define LINKED_LIST_MAX_SIZE 16

/**
 * Represents a linked list
 */
typedef struct s_LinkedList {
    /**
     * Storage for the linked list
     */
    LinkedListNode Data[LINKED_LIST_MAX_SIZE];
    
    /**
     * The start of the list
     */
    LinkedListNode* Head;
    
    /**
     * Indicates whether the list will drop or assert on overflow
     */
    bool DropOnOverflow;
} LinkedList;

/**
 * Initializes the linked list
 * @param list The list to initialize
 * @param dropOnOverflow Indicates whether the list will drop on overflow
 * @return A reference to the list
 */
LinkedList* LinkedList_Initialize(LinkedList* list, bool dropOnOverflow);

/**
 * Adds data to the back of the list
 * @param list The list to add to
 * @param data The data to add
 * @param len The length of the data
 * @return True on success, false on failure
 */
bool LinkedList_PushBack(LinkedList* list, const uint8_t* data, size_t len);

/**
 * Removes and returns the first item in the list
 * @param list The list to modify
 * @param data Storage for the returned data
 * @param maxLen The maximum size of the data to return
 * @return The size of the returned data
 */
size_t LinkedList_PopFront(LinkedList* list, uint8_t* data, size_t maxLen);

/**
 * Gets the number of items currently in the list
 * @param list The list to modify
 * @return The current size of the list
 */
size_t LinkedList_Size(LinkedList* list);

#ifdef	__cplusplus
}
#endif

