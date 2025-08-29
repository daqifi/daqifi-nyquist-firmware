/*******************************************************************************
  SPI0 Bus Mutex Header

  File Name:
    spi0_mutex.h

  Summary:
    Simple SPI0 bus mutex with SD card priority

  Description:
    Provides a simple mutex mechanism for shared SPI0 bus access between
    WiFi and SD card, with SD card having priority.
*******************************************************************************/

#ifndef SPI0_MUTEX_H
#define SPI0_MUTEX_H

#include <stdbool.h>
#include "FreeRTOS.h"
#include "semphr.h"

// SPI0 bus clients
typedef enum {
    SPI0_CLIENT_SD_CARD = 0,    // Higher priority
    SPI0_CLIENT_WIFI = 1,       // Lower priority
    SPI0_CLIENT_MAX
} spi0_client_t;

// SPI0 mutex functions
bool SPI0_Mutex_Initialize(void);
bool SPI0_Mutex_Lock(spi0_client_t client, TickType_t timeout);
void SPI0_Mutex_Unlock(spi0_client_t client);
bool SPI0_Mutex_IsLocked(void);
spi0_client_t SPI0_Mutex_GetOwner(void);

#endif // SPI0_MUTEX_H