/*******************************************************************************
  SPI0 Bus Mutex Implementation

  File Name:
    spi0_mutex.c

  Summary:
    Simple SPI0 bus mutex with SD card priority

  Description:
    Provides a simple mutex mechanism for shared SPI0 bus access between
    WiFi and SD card, with SD card having priority.
*******************************************************************************/

#include "spi0_mutex.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

// Static variables
static SemaphoreHandle_t spi0_mutex = NULL;
static spi0_client_t current_owner = SPI0_CLIENT_MAX; // Invalid = not owned
static TaskHandle_t owner_task = NULL;

// Priority timeout values (in ticks)
#define SPI0_SD_TIMEOUT_MS     5000    // SD card can wait 5 seconds
#define SPI0_WIFI_TIMEOUT_MS   100     // WiFi gets shorter timeout

/*******************************************************************************
  Function:
    bool SPI0_Mutex_Initialize(void)

  Summary:
    Initialize the SPI0 mutex

  Returns:
    true if successful, false otherwise
*******************************************************************************/
bool SPI0_Mutex_Initialize(void)
{
    if (spi0_mutex == NULL) {
        // Create a binary semaphore (mutex)
        spi0_mutex = xSemaphoreCreateBinary();
        if (spi0_mutex == NULL) {
            return false;
        }
        
        // Give the semaphore initially (unlocked state)
        xSemaphoreGive(spi0_mutex);
        
        current_owner = SPI0_CLIENT_MAX;
        owner_task = NULL;
    }
    
    return true;
}

/*******************************************************************************
  Function:
    bool SPI0_Mutex_Lock(spi0_client_t client, TickType_t timeout)

  Summary:
    Acquire the SPI0 bus mutex

  Description:
    SD card (higher priority) can preempt WiFi if needed.
    Simple priority: SD card always wins.

  Parameters:
    client - Which client is requesting the lock
    timeout - How long to wait (in ticks)

  Returns:
    true if lock acquired, false if timeout
*******************************************************************************/
bool SPI0_Mutex_Lock(spi0_client_t client, TickType_t timeout)
{
    if (spi0_mutex == NULL || client >= SPI0_CLIENT_MAX) {
        return false;
    }
    
    // Convert timeout to ticks if needed
    TickType_t wait_time = timeout;
    if (timeout == 0) {
        // Use default timeouts based on client priority
        wait_time = (client == SPI0_CLIENT_SD_CARD) ? 
                    pdMS_TO_TICKS(SPI0_SD_TIMEOUT_MS) : 
                    pdMS_TO_TICKS(SPI0_WIFI_TIMEOUT_MS);
    }
    
    // Try to acquire the semaphore
    if (xSemaphoreTake(spi0_mutex, wait_time) == pdTRUE) {
        // Successfully acquired
        current_owner = client;
        owner_task = xTaskGetCurrentTaskHandle();
        return true;
    }
    
    // Failed to acquire within timeout
    return false;
}

/*******************************************************************************
  Function:
    void SPI0_Mutex_Unlock(spi0_client_t client)

  Summary:
    Release the SPI bus mutex

  Description:
    Only the current owner can unlock the mutex

  Parameters:
    client - Which client is releasing the lock
*******************************************************************************/
void SPI0_Mutex_Unlock(spi0_client_t client)
{
    if (spi0_mutex == NULL || client >= SPI0_CLIENT_MAX) {
        return;
    }
    
    // Only allow the current owner to unlock
    if (current_owner == client && owner_task == xTaskGetCurrentTaskHandle()) {
        current_owner = SPI0_CLIENT_MAX;
        owner_task = NULL;
        xSemaphoreGive(spi0_mutex);
    }
}

/*******************************************************************************
  Function:
    bool SPI0_Mutex_IsLocked(void)

  Summary:
    Check if the SPI bus is currently locked

  Returns:
    true if locked, false if available
*******************************************************************************/
bool SPI0_Mutex_IsLocked(void)
{
    return (current_owner != SPI0_CLIENT_MAX);
}

/*******************************************************************************
  Function:
    spi0_client_t SPI0_Mutex_GetOwner(void)

  Summary:
    Get the current owner of the SPI bus

  Returns:
    Current owner, or SPI0_CLIENT_MAX if unlocked
*******************************************************************************/
spi0_client_t SPI0_Mutex_GetOwner(void)
{
    return current_owner;
}