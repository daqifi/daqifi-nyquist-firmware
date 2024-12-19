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
#include "libraries/microrl/src/microrl.h"
#include "libraries/scpi/libscpi/inc/scpi/scpi.h"
#include "Util/CircularBuffer.h"

#define USBCDC_WBUFFER_SIZE 700
#define USBCDC_RBUFFER_SIZE 512 
#define USBCDC_CIRCULAR_BUFF_SIZE USBCDC_WBUFFER_SIZE*4

/**
     * State machine states
     */
    typedef enum e_UsbCdcState {
        /** Application's state machine's initial state. */
        USB_CDC_STATE_INIT = 0,

        /** Wait for a character receive */
        USB_CDC_STATE_PROCESS,

        /** Triggers the close process */
        USB_CDC_STATE_BEGIN_CLOSE,

        /** Waits for an event, not processing data */
        USB_CDC_STATE_WAIT,

        /** Called when the device is closed*/
        USB_CDC_STATE_CLOSED

    } UsbCdcState_t;

    /**
     * Contains the parameters for the CDC endpoint
     */

    typedef struct s_UsbCdcData {
        /** Device layer handle returned by device layer open function */
        USB_DEVICE_HANDLE deviceHandle;

        /** Application's current state*/
        UsbCdcState_t state;

        /** Set Line Coding Data */
        USB_CDC_LINE_CODING hostSetLineCodingData;

        /** Get Line Coding Data */
        USB_CDC_LINE_CODING deviceLineCodingData;

        /** Control Line State */
        USB_CDC_CONTROL_LINE_STATE controlLineStateData;

        /** Read transfer handle */
        USB_DEVICE_CDC_TRANSFER_HANDLE readTransferHandle;

        /** Write transfer handle */
        USB_DEVICE_CDC_TRANSFER_HANDLE writeTransferHandle;

        /** Break data */
        uint16_t breakData;

        /** The Microrl console */
        microrl_t console;

        /** The associated SCPI context */
        scpi_t scpiContext;

        /** The current length of the read buffer */
        size_t readBufferLength;

        /** The current length of the write buffer */
        size_t writeBufferLength;

        /** Client read buffer */
        uint8_t readBuffer[USBCDC_RBUFFER_SIZE] __attribute__((coherent, aligned(16)));
        ;

        /** Client write buffer */
        uint8_t writeBuffer[USBCDC_WBUFFER_SIZE] __attribute__((coherent, aligned(16)));

        CircularBuf_t wCirbuf;
        SemaphoreHandle_t wMutex;

        /** The flag is if the CDC is in transparent mode
         ie the received data wont go to microrl interface
         so no scip commands would work*/
        bool isCdcHostConnected;
        bool isTransparentModeActive;
    } UsbCdcData_t;
    UsbCdcData_t* UsbCdc_GetSettings();
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
    size_t UsbCdc_WriteBuffFreeSize(UsbCdcData_t* client);
    /**
     * Writes to the default (only) client
     * @param client The usb client to write to
     * @param data The data to write
     * @param len The length of data
     * @return The number of bytes written
     */
    size_t UsbCdc_WriteToBuffer(UsbCdcData_t* client, const char* data, size_t len);

    /**
     * Writes to the default (only) client
     * @param data The data to write
     * @param len The length of data
     * @return The number of bytes written
     */
    size_t UsbCdc_WriteDefault(const char* data, size_t len);

    void UsbCdc_SleepStateUpdateCB(bool);
    /**
     * Sets or clears the CDC transparent Mode
     * @param vale  if True, transparent mode is set
     *              if False transparent mode is cleared 
     */
    void UsbCdc_SetTransparentMode(bool value);
   

#ifdef	__cplusplus
}
#endif

#endif	/* USBCDC_H */

