/*******************************************************************************
  SPI0 Protected Wrapper Header

  File Name:
    spi0_protected.h

  Summary:
    Thread-safe wrapper for SPI0 operations with mutex protection

  Description:
    Provides mutex-protected wrappers around Harmony SPI driver functions
    to enable safe concurrent access between WiFi and SD card modules.
    This approach preserves Harmony upgrade compatibility by not modifying
    generated code.
*******************************************************************************/

#ifndef SPI0_PROTECTED_H
#define SPI0_PROTECTED_H

#include <stdbool.h>
#include "driver/spi/drv_spi.h"

// SPI0 client identification for mutex coordination
typedef enum {
    SPI0_CLIENT_SD_CARD = 0,    // Higher priority
    SPI0_CLIENT_WIFI = 1,       // Lower priority
    SPI0_CLIENT_MAX
} spi0_client_t;

// Timeout sentinel value for using default timeouts
#define SPI0_MUTEX_USE_DEFAULT ((TickType_t)~(TickType_t)0)

// Protected SPI0 wrapper functions
DRV_SPI_TRANSFER_HANDLE DRV_SPI0_WriteRead_Protected(
    spi0_client_t client,
    DRV_HANDLE handle,
    void* pTransmitData,
    size_t txSize,
    void* pReceiveData,
    size_t rxSize
);

DRV_SPI_TRANSFER_HANDLE DRV_SPI0_Write_Protected(
    spi0_client_t client,
    DRV_HANDLE handle,
    void* pTransmitData,
    size_t txSize
);

DRV_SPI_TRANSFER_HANDLE DRV_SPI0_Read_Protected(
    spi0_client_t client,
    DRV_HANDLE handle,
    void* pReceiveData,
    size_t rxSize
);

bool DRV_SPI0_TransferSetup_Protected(
    spi0_client_t client,
    DRV_HANDLE handle,
    DRV_SPI_TRANSFER_SETUP* setup
);

// Utility functions
bool SPI0_Protected_Initialize(void);

#endif // SPI0_PROTECTED_H