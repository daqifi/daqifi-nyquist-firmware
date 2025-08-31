/*******************************************************************************
  SPI0 Protected Wrapper Header (Conditional Implementation)

  File Name:
    spi0_protected.h

  Summary:
    Thread-safe wrapper for SPI0 operations with conditional mutex protection

  Description:
    Provides mutex-protected wrappers around Harmony SPI driver functions
    to enable safe concurrent access between WiFi and SD card modules when
    SPI coordination is needed. Currently disabled due to electrical 
    compatibility, but ready for activation when required.
*******************************************************************************/

#ifndef SPI0_PROTECTED_H
#define SPI0_PROTECTED_H

#include <stdbool.h>
#include "driver/spi/drv_spi.h"

// Configuration: Enable/disable protected wrapper functionality
#define SPI0_PROTECTED_WRAPPER_ENABLED 0  // Set to 1 to enable mutex-protected wrappers

// Compile-time consistency check - prevent configuration mistakes
#if (SPI0_PROTECTED_WRAPPER_ENABLED == 1) && !defined(SPI0_COORDINATION_ENABLED)
#error "SPI protected wrapper requires coordination framework! Include app_freertos.c coordination definitions or disable SPI0_PROTECTED_WRAPPER_ENABLED."
#endif

// Additional check when both are defined
#if defined(SPI0_COORDINATION_ENABLED) && (SPI0_PROTECTED_WRAPPER_ENABLED == 1) && (SPI0_COORDINATION_ENABLED == 0)
#error "Configuration mismatch! SPI protected wrapper enabled but coordination disabled. Either enable both or disable both for consistency."
#endif

#if SPI0_PROTECTED_WRAPPER_ENABLED

#include "FreeRTOS.h"
#include "semphr.h"

// SPI0 client identification for mutex coordination
typedef enum {
    SPI0_CLIENT_SD_CARD = 0,    // Higher priority
    SPI0_CLIENT_WIFI = 1,       // Lower priority
    SPI0_CLIENT_MAX
} spi0_client_t;

// Timeout sentinel value for using default timeouts
#define SPI0_MUTEX_USE_DEFAULT ((TickType_t)~(TickType_t)0)

// Protected SPI0 wrapper functions (disabled but preserved for future use)
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

#else

/*
 * Protected Wrapper Disabled
 * 
 * SPI protected wrappers are disabled because:
 * - WiFi and SD card use identical SPI Mode 0 (electrically compatible)
 * - Both use standardized 20 MHz frequency (no conflicts)
 * - Harmony SPI driver provides adequate multi-client coordination
 * 
 * To enable protected wrappers: Set SPI0_PROTECTED_WRAPPER_ENABLED to 1
 * Useful when client-specific SPI requirements emerge.
 */

// Placeholder definitions for future use
typedef enum {
    SPI0_CLIENT_SD_CARD = 0,
    SPI0_CLIENT_WIFI = 1,
    SPI0_CLIENT_MAX
} spi0_client_t;

// Minimal initialization for framework consistency
static inline bool SPI0_Protected_Initialize(void) { return true; }

#endif // SPI0_PROTECTED_WRAPPER_ENABLED

#endif // SPI0_PROTECTED_H