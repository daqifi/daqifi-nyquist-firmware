/**
 * @file CircularBuf_Safe.h
 * @brief Thread-safe wrapper functions for CircularBuf operations
 * 
 * This module provides thread-safe versions of common CircularBuf operations
 * by automatically handling mutex acquisition and release. This reduces code
 * duplication and prevents common mutex-related bugs.
 * 
 * Note: All functions use portMAX_DELAY for mutex acquisition to prevent
 * priority inversion. If finite mutex timeouts are needed, consider adding
 * timeout parameters to these functions.
 * 
 * Design rationale for portMAX_DELAY:
 * - These are low-level helpers used in controlled contexts
 * - The mutexes protect very short critical sections (just buffer operations)
 * - Using timeouts here would require complex error handling at every call site
 * - If deadlock is a concern, it should be addressed at the system design level
 * - Null pointer checks omitted for performance (caller responsibility)
 */

#ifndef CIRCULARBUF_SAFE_H
#define CIRCULARBUF_SAFE_H

#include "../Util/CircularBuffer.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Get the number of free bytes in a circular buffer (thread-safe)
 * @param buf Pointer to the circular buffer
 * @param mutex Mutex protecting the buffer
 * @return Number of free bytes
 */
static inline uint32_t CircularBuf_GetFreeSize_Safe(CircularBuf_t* buf, SemaphoreHandle_t mutex) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    uint32_t freeSize = CircularBuf_NumBytesFree(buf);
    xSemaphoreGive(mutex);
    return freeSize;
}

/**
 * @brief Get the number of available bytes in a circular buffer (thread-safe)
 * @param buf Pointer to the circular buffer
 * @param mutex Mutex protecting the buffer
 * @return Number of available bytes
 */
static inline uint32_t CircularBuf_GetAvailableSize_Safe(CircularBuf_t* buf, SemaphoreHandle_t mutex) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    uint32_t availSize = CircularBuf_NumBytesAvailable(buf);
    xSemaphoreGive(mutex);
    return availSize;
}

/**
 * @brief Add bytes to a circular buffer (thread-safe)
 * @param buf Pointer to the circular buffer
 * @param mutex Mutex protecting the buffer
 * @param data Pointer to data to add
 * @param len Number of bytes to add
 * @return Number of bytes actually added
 */
static inline uint32_t CircularBuf_AddBytes_Safe(CircularBuf_t* buf, SemaphoreHandle_t mutex,
                                               uint8_t* data, uint32_t len) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    uint32_t bytesAdded = CircularBuf_AddBytes(buf, data, len);
    xSemaphoreGive(mutex);
    return bytesAdded;
}

/**
 * @brief Try to add bytes to a circular buffer with timeout (thread-safe)
 * 
 * This function will wait up to timeoutMs for sufficient space to become available
 * in the buffer before adding the data.
 * 
 * @param buf Pointer to the circular buffer
 * @param mutex Mutex protecting the buffer
 * @param data Pointer to data to add
 * @param len Number of bytes to add
 * @param timeoutMs Maximum time to wait in milliseconds (use portMAX_DELAY for infinite wait)
 * @param waitIntervalMs Interval between checks in milliseconds
 * @return Number of bytes actually added (0 if timeout)
 */
static inline size_t CircularBuf_TryAddBytes_Safe(CircularBuf_t* buf, SemaphoreHandle_t mutex,
                                                  uint8_t* data, size_t len,
                                                  TickType_t timeoutMs, 
                                                  unsigned int waitIntervalMs) {
    uint32_t len32 = (uint32_t)len;
    TickType_t startTicks = xTaskGetTickCount();
    TickType_t timeoutTicks = (timeoutMs == portMAX_DELAY) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
    TickType_t waitTicks = pdMS_TO_TICKS(waitIntervalMs);
    
    // Ensure at least 1 tick wait
    if (waitTicks == 0) {
        waitTicks = 1;
    }
    
    // Main wait loop - continues until space available or timeout
    while (1) {
        // Check timeout first to avoid unnecessary operations
        if (timeoutTicks != portMAX_DELAY) {
            TickType_t elapsedTicks = xTaskGetTickCount() - startTicks;
            if (elapsedTicks >= timeoutTicks) {
                return 0; // Timeout reached
            }
        }
        
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (CircularBuf_NumBytesFree(buf) >= len32) {
            // Space available, add the data
            uint32_t bytesAdded = CircularBuf_AddBytes(buf, data, len32);
            xSemaphoreGive(mutex);
            return (size_t)bytesAdded;
        }
        xSemaphoreGive(mutex);
        
        // Wait before retry
        vTaskDelay(waitTicks);
    }
}

/**
 * @brief Process bytes from buffer if available (thread-safe)
 * 
 * This function atomically checks if data is available and processes it if so.
 * Useful for the common pattern of checking for data then processing it.
 * 
 * @param buf Pointer to the circular buffer
 * @param mutex Mutex protecting the buffer
 * @param userData User data buffer (can be NULL)
 * @param maxBytes Maximum bytes to process
 * @param result Pointer to store process result
 * @return true if data was processed, false if no data available
 */
static inline bool CircularBuf_ProcessIfAvailable_Safe(CircularBuf_t* buf, 
                                                       SemaphoreHandle_t mutex,
                                                       uint8_t* userData,
                                                       uint32_t maxBytes,
                                                       int* result) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool hasData = (CircularBuf_NumBytesAvailable(buf) > 0);
    if (hasData) {
        CircularBuf_ProcessBytes(buf, userData, maxBytes, result);
    }
    xSemaphoreGive(mutex);
    return hasData;
}

/**
 * @brief Check if buffer has sufficient free space (thread-safe)
 * @param buf Pointer to the circular buffer
 * @param mutex Mutex protecting the buffer
 * @param requiredSpace Space needed in bytes
 * @return true if sufficient space available
 */
static inline bool CircularBuf_HasSpace_Safe(CircularBuf_t* buf, SemaphoreHandle_t mutex,
                                             size_t requiredSpace) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool hasSpace = (CircularBuf_NumBytesFree(buf) >= (uint32_t)requiredSpace);
    xSemaphoreGive(mutex);
    return hasSpace;
}

/**
 * @brief Check if buffer has data available (thread-safe)
 * @param buf Pointer to the circular buffer
 * @param mutex Mutex protecting the buffer
 * @return true if data is available
 */
static inline bool CircularBuf_HasData_Safe(CircularBuf_t* buf, SemaphoreHandle_t mutex) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    bool hasData = (CircularBuf_NumBytesAvailable(buf) > 0);
    xSemaphoreGive(mutex);
    return hasData;
}

/**
 * @brief Atomically check space and add bytes if sufficient space (thread-safe)
 * 
 * This prevents the race condition where space might be consumed between
 * checking and adding.
 * 
 * @param buf Pointer to the circular buffer
 * @param mutex Mutex protecting the buffer
 * @param data Pointer to data to add
 * @param len Number of bytes to add
 * @return Number of bytes added (0 if insufficient space)
 */
static inline size_t CircularBuf_AddBytesIfSpace_Safe(CircularBuf_t* buf, SemaphoreHandle_t mutex,
                                                      uint8_t* data, size_t len) {
    uint32_t len32 = (uint32_t)len;
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (CircularBuf_NumBytesFree(buf) < len32) {
        xSemaphoreGive(mutex);
        return 0;
    }
    uint32_t bytesAdded = CircularBuf_AddBytes(buf, data, len32);
    xSemaphoreGive(mutex);
    return (size_t)bytesAdded;
}

/**
 * @brief Clear the circular buffer (thread-safe)
 * @param buf Pointer to the circular buffer
 * @param mutex Mutex protecting the buffer
 */
static inline void CircularBuf_Clear_Safe(CircularBuf_t* buf, SemaphoreHandle_t mutex) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    CircularBuf_Reset(buf);  // Verified: Reset is the correct function name
    xSemaphoreGive(mutex);
}

/**
 * @brief Get the number of free bytes (size_t version for API compatibility)
 * 
 * This is a convenience wrapper that returns size_t instead of uint16_t,
 * useful when interfacing with APIs that expect size_t return values.
 * 
 * @param buf Pointer to the circular buffer
 * @param mutex Mutex protecting the buffer
 * @return Number of free bytes as size_t
 */
static inline size_t CircularBuf_GetFreeSize_Safe_SizeT(CircularBuf_t* buf, SemaphoreHandle_t mutex) {
    return (size_t)CircularBuf_GetFreeSize_Safe(buf, mutex);
}

#ifdef __cplusplus
}
#endif

#endif /* CIRCULARBUF_SAFE_H */