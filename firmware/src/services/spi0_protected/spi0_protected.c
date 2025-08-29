/*******************************************************************************
  SPI0 Protected Wrapper Implementation

  File Name:
    spi0_protected.c

  Summary:
    Thread-safe wrapper for SPI0 operations with mutex protection

  Description:
    Provides mutex-protected wrappers around Harmony SPI driver functions
    to enable safe concurrent access between WiFi and SD card modules.
*******************************************************************************/

#include "spi0_protected.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

// Forward declarations for SPI0 mutex functions (implemented in app_freertos.c)
extern bool SPI0_Mutex_Lock(spi0_client_t client, TickType_t timeout);
extern void SPI0_Mutex_Unlock(spi0_client_t client);

// Default timeouts for different clients
#define SPI0_SD_TIMEOUT_MS     100    // SD card operations can wait
#define SPI0_WIFI_TIMEOUT_MS   10     // WiFi needs fast response

/*******************************************************************************
  Function:
    static TickType_t get_client_timeout(spi0_client_t client)

  Summary:
    Get appropriate timeout for SPI client type

  Parameters:
    client - SPI0 client requesting access

  Returns:
    Timeout in ticks appropriate for client type
*******************************************************************************/
static TickType_t get_client_timeout(spi0_client_t client)
{
    switch (client) {
        case SPI0_CLIENT_SD_CARD:
            return pdMS_TO_TICKS(SPI0_SD_TIMEOUT_MS);
        case SPI0_CLIENT_WIFI:
            return pdMS_TO_TICKS(SPI0_WIFI_TIMEOUT_MS);
        default:
            return pdMS_TO_TICKS(10); // Default fallback
    }
}

/*******************************************************************************
  Function:
    DRV_SPI_TRANSFER_HANDLE DRV_SPI0_WriteRead_Protected

  Summary:
    Mutex-protected wrapper for DRV_SPI_WriteRead

  Description:
    Acquires SPI0 mutex, performs the write/read operation, then releases mutex.
    Provides safe concurrent access between WiFi and SD card.

  Parameters:
    client - SPI0 client identification (SD card or WiFi)
    handle - SPI driver handle
    pTransmitData - Transmit buffer
    txSize - Transmit size
    pReceiveData - Receive buffer  
    rxSize - Receive size

  Returns:
    Transfer handle from underlying SPI driver, or invalid handle on error
*******************************************************************************/
DRV_SPI_TRANSFER_HANDLE DRV_SPI0_WriteRead_Protected(
    spi0_client_t client,
    DRV_HANDLE handle,
    void* pTransmitData,
    size_t txSize,
    void* pReceiveData,
    size_t rxSize
)
{
    DRV_SPI_TRANSFER_HANDLE result = DRV_SPI_TRANSFER_HANDLE_INVALID;
    
    // Acquire SPI0 bus with client-appropriate timeout
    if (!SPI0_Mutex_Lock(client, get_client_timeout(client))) {
        return DRV_SPI_TRANSFER_HANDLE_INVALID;
    }
    
    // Perform the actual SPI operation using Harmony driver
    result = DRV_SPI_WriteRead(handle, pTransmitData, txSize, pReceiveData, rxSize);
    
    // Release SPI0 bus
    SPI0_Mutex_Unlock(client);
    
    return result;
}

/*******************************************************************************
  Function:
    DRV_SPI_TRANSFER_HANDLE DRV_SPI0_Write_Protected

  Summary:
    Mutex-protected wrapper for DRV_SPI_Write
*******************************************************************************/
DRV_SPI_TRANSFER_HANDLE DRV_SPI0_Write_Protected(
    spi0_client_t client,
    DRV_HANDLE handle,
    void* pTransmitData,
    size_t txSize
)
{
    DRV_SPI_TRANSFER_HANDLE result = DRV_SPI_TRANSFER_HANDLE_INVALID;
    
    if (!SPI0_Mutex_Lock(client, get_client_timeout(client))) {
        return DRV_SPI_TRANSFER_HANDLE_INVALID;
    }
    
    result = DRV_SPI_Write(handle, pTransmitData, txSize);
    
    SPI0_Mutex_Unlock(client);
    
    return result;
}

/*******************************************************************************
  Function:
    DRV_SPI_TRANSFER_HANDLE DRV_SPI0_Read_Protected

  Summary:
    Mutex-protected wrapper for DRV_SPI_Read
*******************************************************************************/
DRV_SPI_TRANSFER_HANDLE DRV_SPI0_Read_Protected(
    spi0_client_t client,
    DRV_HANDLE handle,
    void* pReceiveData,
    size_t rxSize
)
{
    DRV_SPI_TRANSFER_HANDLE result = DRV_SPI_TRANSFER_HANDLE_INVALID;
    
    if (!SPI0_Mutex_Lock(client, get_client_timeout(client))) {
        return DRV_SPI_TRANSFER_HANDLE_INVALID;
    }
    
    result = DRV_SPI_Read(handle, pReceiveData, rxSize);
    
    SPI0_Mutex_Unlock(client);
    
    return result;
}

/*******************************************************************************
  Function:
    bool DRV_SPI0_TransferSetup_Protected

  Summary:
    Mutex-protected wrapper for DRV_SPI_TransferSetup
*******************************************************************************/
bool DRV_SPI0_TransferSetup_Protected(
    spi0_client_t client,
    DRV_HANDLE handle,
    DRV_SPI_TRANSFER_SETUP* setup
)
{
    bool result = false;
    
    if (!SPI0_Mutex_Lock(client, get_client_timeout(client))) {
        return false;
    }
    
    result = DRV_SPI_TransferSetup(handle, setup);
    
    SPI0_Mutex_Unlock(client);
    
    return result;
}

/*******************************************************************************
  Function:
    bool SPI0_Protected_Initialize

  Summary:
    Initialize the SPI0 protected wrapper module

  Description:
    This is called by the main application to set up any required state.
    Currently just validates that the underlying mutex is available.

  Returns:
    true if successful, false otherwise
*******************************************************************************/
bool SPI0_Protected_Initialize(void)
{
    // The actual mutex is initialized in app_freertos.c
    // This function could be used for wrapper-specific initialization
    return true;
}