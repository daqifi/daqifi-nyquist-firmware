/*******************************************************************************
 Data Stream USB_HID Source File

  File Name:
    datastream_usb_hid.c

  Summary:
 Data Stream USB_HID source

  Description:
    This file contains source code necessary for the data stream interface.
 *******************************************************************************/

// DOM-IGNORE-BEGIN
/*******************************************************************************
Copyright (c) 2013 released Microchip Technology Inc.  All rights reserved.

Microchip licenses to you the right to use, modify, copy and distribute
Software only when embedded on a Microchip microcontroller or digital signal
controller that is integrated into your product or third party product
(pursuant to the sublicense terms in the accompanying license agreement).

You should refer to the license agreement accompanying this Software for
additional information regarding your rights and obligations.

SOFTWARE AND DOCUMENTATION ARE PROVIDED AS IS WITHOUT WARRANTY OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY WARRANTY OF
MERCHANTABILITY, TITLE, NON-INFRINGEMENT AND FITNESS FOR A PARTICULAR PURPOSE.
IN NO EVENT SHALL MICROCHIP OR ITS LICENSORS BE LIABLE OR OBLIGATED UNDER
CONTRACT, NEGLIGENCE, STRICT LIABILITY, CONTRIBUTION, BREACH OF WARRANTY, OR
OTHER LEGAL EQUITABLE THEORY ANY DIRECT OR INDIRECT DAMAGES OR EXPENSES
INCLUDING BUT NOT LIMITED TO ANY INCIDENTAL, SPECIAL, INDIRECT, PUNITIVE OR
CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA, COST OF PROCUREMENT OF
SUBSTITUTE GOODS, TECHNOLOGY, SERVICES, OR ANY CLAIMS BY THIRD PARTIES
(INCLUDING BUT NOT LIMITED TO ANY DEFENSE THEREOF), OR OTHER SIMILAR COSTS.
 *******************************************************************************/
// DOM-IGNORE-END
#include "system/common/sys_module.h"
#include "bootloader/src/datastream.h"
#include "peripheral/ports/plib_ports.h"
#include "peripheral/usart/plib_usart.h"
#include "peripheral/dma/plib_dma.h"
#include "bootloader/src/bootloader.h"
#include "system/clk/sys_clk.h"
#include "usb/usb_chapter_9.h"
#include "usb/usb_device.h"
#include "usb/usb_device_hid.h"
#include "peripheral/tmr/plib_tmr.h"

extern BOOTLOADER_DATA bootloaderData __attribute__((coherent, aligned(16)));
extern DATASTREAM_HandlerType* handler;
extern uintptr_t _context;
bool readRequest = false;
bool DataSent;
bool DataReceived;

USB_DEVICE_HID_EVENT_RESPONSE _USBDeviceHIDEventHandler
(
    USB_DEVICE_HID_INDEX iHID,
    USB_DEVICE_HID_EVENT event,
    void * eventData,
    uintptr_t userData
);

void _USBDeviceEventHandler(USB_DEVICE_EVENT event, void * eventData, uintptr_t context);

void DATASTREAM_Tasks(void)
{
    if (false == bootloaderData.deviceConfigured)
    {
        return;
    }

    if (RX == currDir)
    {
        
        if( DataReceived )
        {        
                DataReceived = false;
                readRequest = false;
                currDir = IDLE;
                handler(DATASTREAM_BUFFER_EVENT_COMPLETE, (DATASTREAM_BUFFER_HANDLE)_bufferHandle, 64);
        }
        else if(readRequest == false)
        {
                  
            DataReceived = false;
            
            /* Place a new read request. */
             USB_DEVICE_HID_ReportReceive (USB_DEVICE_HID_INDEX_0,
                &bootloaderData.hostHandle, &bootloaderData.data->buffers.buff1[0], 64 );
             
             readRequest = true;
        }

    }
    else if (TX == currDir)
    {
        if (!DataSent && (readRequest == false) && (_txCurPos < _txMaxSize))
        {
             /* Prepare the USB module to send the data packet to the host */
             USB_DEVICE_HID_ReportSend (USB_DEVICE_HID_INDEX_0,
             &bootloaderData.hostHandle, &bootloaderData.data->buffers.buff2[_txCurPos], 64 );
             _txCurPos += 64;
            readRequest = true;
             
        }
        else if (DataSent && (_txCurPos >= _txMaxSize)) // All data has been sent or is in the buffer
        {
   
                readRequest = false;
                currDir = IDLE;
                bootloaderData.usrBufferEventComplete = true;
                DataSent = false;
                
        }
        else if (DataSent)
        {
                 readRequest = false;
                DataSent = false;
        }    
        
        }
    }

DRV_HANDLE DATASTREAM_Open(const DRV_IO_INTENT ioIntent)
{
    bootloaderData.datastreamBufferHandle = USB_DEVICE_Open( 0, DRV_IO_INTENT_READWRITE );
    
    if(bootloaderData.datastreamBufferHandle != USB_DEVICE_HANDLE_INVALID)
    {
    
    /* This means host operation is enabled. We can
    * move on to the next state */
    USB_DEVICE_EventHandlerSet(bootloaderData.datastreamBufferHandle, _USBDeviceEventHandler, 0);
    return(0);
    
    }       
    
    return(DRV_HANDLE_INVALID);
}

DRV_CLIENT_STATUS DATASTREAM_ClientStatus(DRV_HANDLE handle)
{
    if(bootloaderData.deviceConfigured == true)
    {
        return DRV_CLIENT_STATUS_READY;
    }
    else
    {    
        return DRV_CLIENT_STATUS_BUSY;
    }
}

USB_DEVICE_HID_EVENT_RESPONSE APP_USBDeviceHIDEventHandler
(
    USB_DEVICE_HID_INDEX iHID,
    USB_DEVICE_HID_EVENT event,
    void * eventData,
    uintptr_t userData
)
{
//    USB_DEVICE_HID_EVENT_DATA_REPORT_SENT * reportSent;
//    USB_DEVICE_HID_EVENT_DATA_REPORT_RECEIVED * reportReceived;

    /* Check type of event */
    switch (event)
    {
        case USB_DEVICE_HID_EVENT_REPORT_SENT:

            /* The eventData parameter will be USB_DEVICE_HID_EVENT_REPORT_SENT
             * pointer type containing details about the report that was
             * sent. */
      //      reportSent = (USB_DEVICE_HID_EVENT_DATA_REPORT_SENT *) eventData;
            DataSent = true;
            break;

        case USB_DEVICE_HID_EVENT_REPORT_RECEIVED:

            /* The eventData parameter will be USB_DEVICE_HID_EVENT_REPORT_RECEIVED
             * pointer type containing details about the report that was
             * received. */

       //     reportReceived = (USB_DEVICE_HID_EVENT_DATA_REPORT_RECEIVED *) eventData;
            DataReceived = true;
            break;

        case USB_DEVICE_HID_EVENT_SET_IDLE:

            /* For now we just accept this request as is. We acknowledge
             * this request using the USB_DEVICE_HID_ControlStatus()
             * function with a USB_DEVICE_CONTROL_STATUS_OK flag */

            USB_DEVICE_ControlStatus(bootloaderData.datastreamBufferHandle, USB_DEVICE_CONTROL_STATUS_OK);

            /* Save Idle rate recieved from Host */
            //appData.idleRate = ((USB_DEVICE_HID_EVENT_DATA_SET_IDLE*)eventData)->duration;
            break;

        case USB_DEVICE_HID_EVENT_GET_IDLE:

            /* Host is requesting for Idle rate. Now send the Idle rate */
            //USB_DEVICE_ControlSend(appData.usbDevHandle, & (appData.idleRate),1);

            /* On successfully reciveing Idle rate, the Host would acknowledge back with a
               Zero Length packet. The HID function drvier returns an event
               USB_DEVICE_HID_EVENT_CONTROL_TRANSFER_DATA_SENT to the application upon
               receiving this Zero Length packet from Host.
               USB_DEVICE_HID_EVENT_CONTROL_TRANSFER_DATA_SENT event indicates this control transfer
               event is complete */

            break;
        default:
            // Nothing to do.
            break;
    }
    return USB_DEVICE_HID_EVENT_RESPONSE_NONE;
}

void _USBDeviceEventHandler(USB_DEVICE_EVENT event, void * eventData, uintptr_t context)
{
    switch(event)
    {
        case USB_DEVICE_EVENT_RESET:
        case USB_DEVICE_EVENT_DECONFIGURED:

            /* Host has de configured the device or a bus reset has happened.
             * Device layer is going to de-initialize all function drivers.
             * Hence close handles to all function drivers (Only if they are
             * opened previously. */
            bootloaderData.deviceConfigured = false;
            break;

        case USB_DEVICE_EVENT_CONFIGURED:
            /* Set the flag indicating device is configured. */
            bootloaderData.deviceConfigured = true;
            //BSP_LEDOn(BSP_LED_2);
            /* Register application HID event handler */
            USB_DEVICE_HID_EventHandlerSet(USB_DEVICE_HID_INDEX_0, APP_USBDeviceHIDEventHandler, (uintptr_t)&bootloaderData);
            break;

        case USB_DEVICE_EVENT_SUSPENDED:
            break;

        case USB_DEVICE_EVENT_POWER_DETECTED:

            /* VBUS was detected. We can attach the device */

            USB_DEVICE_Attach (bootloaderData.datastreamBufferHandle);
            break;

        case USB_DEVICE_EVENT_POWER_REMOVED:

            /* VBUS is not available */
            USB_DEVICE_Detach(bootloaderData.datastreamBufferHandle);
            break;

        /* These events are not used in this demo */
        case USB_DEVICE_EVENT_RESUMED:
        case USB_DEVICE_EVENT_ERROR:
        default:
            break;
    }
}

void DATASTREAM_Close(void)
{
    uint32_t    coreCount;
    
    if (bootloaderData.deviceConfigured)
    {
        USB_DEVICE_Detach(bootloaderData.datastreamBufferHandle);
        USB_DEVICE_Close(bootloaderData.datastreamBufferHandle);
//        USB_DEVICE_Deinitialize(bootloaderData.datastreamBufferHandle);
        coreCount = _CP0_GET_COUNT() + SYS_CLK_FREQ / 20;
        while (coreCount > _CP0_GET_COUNT());
    }
    //Disable Interrupt sources so bootloader application runs without issues
    SYS_INT_SourceDisable(DRV_TMR_INTERRUPT_SOURCE_IDX0);
    SYS_INT_VectorPrioritySet(DRV_TMR_INTERRUPT_VECTOR_IDX0, INT_DISABLE_INTERRUPT);
    SYS_INT_VectorSubprioritySet(DRV_TMR_INTERRUPT_VECTOR_IDX0, INT_SUBPRIORITY_LEVEL0);
    SYS_INT_SourceDisable(INT_SOURCE_USB_1);
    SYS_INT_VectorPrioritySet(INT_VECTOR_USB1, INT_DISABLE_INTERRUPT);
    SYS_INT_VectorSubprioritySet(INT_VECTOR_USB1, INT_SUBPRIORITY_LEVEL0);
#if defined(_USB_DMA_VECTOR)
    SYS_INT_SourceDisable(INT_SOURCE_USB_1_DMA);
    SYS_INT_VectorPrioritySet(INT_VECTOR_USB1_DMA, INT_DISABLE_INTERRUPT);
    SYS_INT_VectorSubprioritySet(INT_VECTOR_USB1_DMA, INT_SUBPRIORITY_LEVEL0);
#endif    
        
    PLIB_TMR_Stop(DRV_TMR_PERIPHERAL_ID_IDX0);
}
