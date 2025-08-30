/*******************************************************************************
  SPI0 Protected Wrapper Implementation (Conditional)

  File Name:
    spi0_protected.c

  Summary:
    Thread-safe wrapper for SPI0 operations with conditional mutex protection

  Description:
    Provides mutex-protected wrappers around Harmony SPI driver functions
    when SPI coordination is required. Currently disabled due to electrical
    compatibility between WiFi and SD card clients.
*******************************************************************************/

#include "spi0_protected.h"

#if SPI0_PROTECTED_WRAPPER_ENABLED

// Include coordination functions from app_freertos.c
extern bool SPI0_Operation_Lock(spi0_client_t client, TickType_t timeout);
extern void SPI0_Operation_Unlock(spi0_client_t client);

// Default timeouts for different clients
#define SPI0_SD_TIMEOUT_MS     100    // SD card operations can wait
#define SPI0_WIFI_TIMEOUT_MS   10     // WiFi needs fast response

/*******************************************************************************
  Function:
    static TickType_t get_client_timeout(spi0_client_t client)

  Summary:
    Get appropriate timeout for SPI client type
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
    if (!SPI0_Operation_Lock(client, get_client_timeout(client))) {
        return DRV_SPI_TRANSFER_HANDLE_INVALID;
    }
    
    // Perform the actual SPI operation using Harmony driver
    result = DRV_SPI_WriteRead(handle, pTransmitData, txSize, pReceiveData, rxSize);
    
    // Release SPI0 bus
    SPI0_Operation_Unlock(client);
    
    return result;
}

DRV_SPI_TRANSFER_HANDLE DRV_SPI0_Write_Protected(
    spi0_client_t client,
    DRV_HANDLE handle,
    void* pTransmitData,
    size_t txSize
)
{
    DRV_SPI_TRANSFER_HANDLE result = DRV_SPI_TRANSFER_HANDLE_INVALID;
    
    if (!SPI0_Operation_Lock(client, get_client_timeout(client))) {
        return DRV_SPI_TRANSFER_HANDLE_INVALID;
    }
    
    result = DRV_SPI_Write(handle, pTransmitData, txSize);
    
    SPI0_Operation_Unlock(client);
    
    return result;
}

DRV_SPI_TRANSFER_HANDLE DRV_SPI0_Read_Protected(
    spi0_client_t client,
    DRV_HANDLE handle,
    void* pReceiveData,
    size_t rxSize
)
{
    DRV_SPI_TRANSFER_HANDLE result = DRV_SPI_TRANSFER_HANDLE_INVALID;
    
    if (!SPI0_Operation_Lock(client, get_client_timeout(client))) {
        return DRV_SPI_TRANSFER_HANDLE_INVALID;
    }
    
    result = DRV_SPI_Read(handle, pReceiveData, rxSize);
    
    SPI0_Operation_Unlock(client);
    
    return result;
}

bool DRV_SPI0_TransferSetup_Protected(
    spi0_client_t client,
    DRV_HANDLE handle,
    DRV_SPI_TRANSFER_SETUP* setup
)
{
    bool result = false;
    
    if (!SPI0_Operation_Lock(client, get_client_timeout(client))) {
        return false;
    }
    
    result = DRV_SPI_TransferSetup(handle, setup);
    
    SPI0_Operation_Unlock(client);
    
    return result;
}

bool SPI0_Protected_Initialize(void)
{
    // Protected wrapper functionality enabled
    return true;
}

#else

/*
 * Protected Wrapper Implementation Disabled
 * 
 * SPI protected wrappers are currently disabled due to electrical compatibility
 * between WiFi and SD card clients. Both use identical SPI Mode 0 settings.
 * 
 * This implementation is preserved for future activation when:
 * - Client-specific SPI configurations are needed
 * - Enhanced timing control is required
 * - Fault isolation between clients is necessary
 */

bool SPI0_Protected_Initialize(void)
{
    // Wrapper disabled - using direct Harmony SPI driver access
    return true;
}

#endif // SPI0_PROTECTED_WRAPPER_ENABLED