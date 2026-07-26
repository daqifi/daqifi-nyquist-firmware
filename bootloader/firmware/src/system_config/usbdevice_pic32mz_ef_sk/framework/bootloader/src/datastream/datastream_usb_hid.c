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

/* #568 re-enumeration backstop. A reopen of a bootloader that the host had let go to
 * USB selective-suspend produces a bus RESET with NO follow-up SET_CONFIGURATION, so
 * USB_DEVICE_EVENT_CONFIGURED never fires: the data endpoints the device layer disabled
 * on the reset are never re-enabled, deviceConfigured stays false, and DATASTREAM_Tasks
 * early-returns forever -- the dark-endpoint wedge that historically needed a power-cycle.
 * The datastream state-reset (already on main) re-arms the moment a reconfigure lands, but
 * a bare host close+reopen only re-triggers SET_CONFIGURATION intermittently. So if we sit
 * deconfigured past a grace period after a reset, proactively Detach/Attach to force a
 * clean re-enumeration; the host's reconnect then reliably finds a fresh, reconfigurable
 * device. Armed ONLY after at least one successful configuration (_everConfigured) so it
 * can never disturb the initial enumeration, which legitimately has several resets before
 * its first SET_CONFIGURATION. */
/* volatile: written from the USB device-event handler and read/written in DATASTREAM_Tasks
 * (different execution contexts), so the compiler must not cache them. */
static volatile bool _awaitingReconfigure = false;
static volatile bool _everConfigured = false;
static volatile bool _vbusPresent = false;        /* tracked from POWER_DETECTED/REMOVED; gates the backstop attach */
static volatile bool _detachHoldActive = false;   /* non-blocking detach-hold in progress */
static volatile uint32_t _deconfiguredSince = 0;
static volatile uint32_t _detachUntil = 0;        /* CP0 deadline for the non-blocking detach hold */
/* CP0 increments at SYS_CLK_FREQ/2; ~1.5 s cleanly separates a true wedge (never
 * reconfigures) from ordinary reset->reconfigure latency (a few ms). */
#define DATASTREAM_RECONFIG_TIMEOUT_TICKS (3U * SYS_CLK_FREQ / 4U)   /* ~1.5 s */
#define DATASTREAM_DETACH_HOLD_TICKS      (SYS_CLK_FREQ / 20U)        /* ~100 ms detach */

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
        /* #568 re-enumeration backstop. Non-blocking: the detach hold is timed across
         * DATASTREAM_Tasks() calls rather than a spin-wait, so DRV_USBHS_Tasks /
         * USB_DEVICE_Tasks keep running and actually process the detach during the hold. */
        if (_detachHoldActive)
        {
            if ((int32_t)(_CP0_GET_COUNT() - _detachUntil) >= 0)
            {
                /* Hold elapsed -- re-attach so the host re-runs enumeration + SET_CONFIGURATION,
                 * but only if VBUS is still present (it may have been pulled during the hold). */
                if (_vbusPresent)
                {
                    USB_DEVICE_Attach(bootloaderData.datastreamBufferHandle);
                }
                _detachHoldActive = false;
            }
        }
        else if (_awaitingReconfigure && _vbusPresent &&
                 ((int32_t)(_CP0_GET_COUNT() - _deconfiguredSince) >= (int32_t)DATASTREAM_RECONFIG_TIMEOUT_TICKS))
        {
            /* A bus reset deconfigured us and no SET_CONFIGURATION followed. Force a clean
             * re-enumeration: detach now, then re-attach after the non-blocking hold above.
             * Gated on VBUS so we never attach without bus power. */
            _awaitingReconfigure = false;
            USB_DEVICE_Detach(bootloaderData.datastreamBufferHandle);
            _detachUntil = _CP0_GET_COUNT() + DATASTREAM_DETACH_HOLD_TICKS;
            _detachHoldActive = true;
        }
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
            /* #568 wedge-fix: a RESET/DECONFIGURE strands the datastream RX/TX state.
             * The stock handler cleared only deviceConfigured, leaving readRequest /
             * currDir / DataReceived stale, so after the host reconnected the
             * "else if (readRequest == false)" re-arm guard in DATASTREAM_Tasks never
             * tripped and the endpoint stayed dark until a power-cycle. Reset the
             * bookkeeping and kick the state machine back to GET_COMMAND so that as soon
             * as the device is reconfigured it re-issues a read and responds.
             *
             * NOTE (Saleae-confirmed): a reopen-from-suspend produces a bus RESET with
             * NO follow-up SET_CONFIGURATION, so CONFIGURED never fires and the endpoints
             * disabled by the device layer here are never re-enabled on this enumeration.
             * Recovery therefore requires the host to close+reopen (a fresh enumeration
             * that does send SET_CONFIGURATION); the desktop/Core side performs that
             * bounded reconnect, and this fix makes the device respond the instant it
             * reconfigures. */
            readRequest = false;
            DataReceived = false;
            DataSent = false;
            currDir = IDLE;
            _txCurPos = 0;            /* clear stale TX progress so a reset mid-send can't resume a partial frame */
            _txMaxSize = 0;
            _bufferHandle = 0;
            bootloaderData.usrBufferEventComplete = false;
            bootloaderData.cmdBufferLength = 0;
            bootloaderData.rxEscapePending = false;   /* clear stale DLE-escape parser state across the reset */
            bootloaderData.currentState = BOOTLOADER_GET_COMMAND;
            bootloaderData.prevState = BOOTLOADER_GET_COMMAND;   /* keep prev/current consistent after the reset */
            /* Arm the re-enumeration backstop, but only once we've configured at least
             * once -- never during the initial enumeration (see DATASTREAM_Tasks). Stamp the
             * deconfigure time only on the FIRST reset of a wedge: repeated resets must not keep
             * pushing the grace-period deadline forward, or the backstop would never fire during
             * the exact repeated-reset wedge it targets. */
            if (_everConfigured && !_awaitingReconfigure)
            {
                _awaitingReconfigure = true;
                _deconfiguredSince = _CP0_GET_COUNT();
            }
            break;

        case USB_DEVICE_EVENT_CONFIGURED:
            /* Set the flag indicating device is configured. */
            bootloaderData.deviceConfigured = true;
            _everConfigured = true;          /* enable the #568 re-enumeration backstop */
            _awaitingReconfigure = false;    /* reconfigure landed -- disarm the backstop */
            _detachHoldActive = false;       /* a reconfigure landed; no detach hold pending */
            //BSP_LEDOn(BSP_LED_2);
            /* Register application HID event handler */
            USB_DEVICE_HID_EventHandlerSet(USB_DEVICE_HID_INDEX_0, APP_USBDeviceHIDEventHandler, (uintptr_t)&bootloaderData);
            break;

        case USB_DEVICE_EVENT_SUSPENDED:
            break;

        case USB_DEVICE_EVENT_POWER_DETECTED:

            /* VBUS was detected. We can attach the device */
            _vbusPresent = true;
            USB_DEVICE_Attach (bootloaderData.datastreamBufferHandle);
            break;

        case USB_DEVICE_EVENT_POWER_REMOVED:

            /* VBUS is not available */
            _vbusPresent = false;
            _awaitingReconfigure = false;   /* #568: disarm the backstop -- never attach without VBUS */
            _detachHoldActive = false;
            _everConfigured = false;        /* #568: this device stays powered across a USB unplug/replug,
                                             * so a replug is effectively a fresh INITIAL enumeration --
                                             * re-gate the backstop (it must never disturb initial enum) */
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
