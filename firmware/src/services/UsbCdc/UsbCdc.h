/* 
 * File:   UsbCdc.h
 * Author: Daniel
 *
 * Created on August 4, 2016, 3:24 PM
 */

#ifndef USBCDC_H
#define	USBCDC_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "configuration.h"
#include "definitions.h"
#include "UsbCdc_runtimeConfig.h"
// Forward declarations
typedef struct s_UsbCdcData UsbCdcData;

/**
 * Initialization of the USB Process
 */
void UsbCdc_Initialize();

/**
 * State machine entry point for the USB Process
 */
void UsbCdc_ProcessState();

/**
 * Indicates whether USB is up and active
 */
bool UsbCdc_IsActive();

/**
 * Writes to the default (only) client
 * @param client The usb client to write to
 * @param data The data to write
 * @param len The length of data
 * @return The number of bytes written
 */
size_t UsbCdc_WriteToBuffer(UsbCdcData* client, const char* data, size_t len);

/**
 * Writes to the default (only) client
 * @param data The data to write
 * @param len The length of data
 * @return The number of bytes written
 */
size_t UsbCdc_WriteDefault(const char* data, size_t len);

void UsbCdc_SleepStateUpdateCB(bool);

#ifdef	__cplusplus
}
#endif

#endif	/* USBCDC_H */

