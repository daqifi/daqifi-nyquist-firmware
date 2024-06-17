/* 
 * File:   UsbCdc.h
 * Author: Daniel
 *
 * Created on August 4, 2016, 3:24 PM
 */

#ifndef _USBCDC_RUNTIME_CONFIG_H
#define	_USBCDC_RUNTIME_CONFIG_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include "configuration.h"
#include "definitions.h"

#include "libraries/scpi/libscpi/inc/scpi/scpi.h"
#include "libraries/microrl/src/microrl.h"
#include "Util/CircularBuffer.h"


#ifdef	__cplusplus
extern "C" {
#endif
   
#define USB_WBUFFER_SIZE 512
#define USB_RBUFFER_SIZE 512 // 32 * 64
/**
 * State machine states
 */
typedef enum e_UsbCdcState
{
    /** Application's state machine's initial state. */
    USB_CDC_STATE_INIT=0,

    /** Wait for a character receive */
    USB_CDC_STATE_PROCESS,

    /** Triggers the close process */
    USB_CDC_STATE_BEGIN_CLOSE,
            
    /** Waits for an event, not processing data */
    USB_CDC_STATE_WAIT,
            
    /** Called when the device is closed*/
    USB_CDC_STATE_CLOSED

} UsbCdcState;

/**
 * Contains the parameters for the CDC endpoint
 */
typedef struct s_UsbCdcData
{
    /** Device layer handle returned by device layer open function */
    USB_DEVICE_HANDLE deviceHandle;

    /** Application's current state*/
    UsbCdcState state;

    /** Set Line Coding Data */
    USB_CDC_LINE_CODING setLineCodingData;

    /** Get Line Coding Data */
    USB_CDC_LINE_CODING getLineCodingData;

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
    uint8_t readBuffer[USB_RBUFFER_SIZE] __attribute__((coherent, aligned(16)));;
    
    /** Client write buffer */
    uint8_t writeBuffer[USB_WBUFFER_SIZE] __attribute__((coherent, aligned(16)));
    
    CircularBuf wCirbuf;
    SemaphoreHandle_t wMutex;
} UsbCdcData;

#ifdef	__cplusplus
}
#endif

#endif	/* USBCDC_H */

