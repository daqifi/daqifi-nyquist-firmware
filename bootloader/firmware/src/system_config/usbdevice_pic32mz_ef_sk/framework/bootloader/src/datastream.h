/*******************************************************************************
  Bootloader Data Stream Interface Header File

  Company:
    Microchip Technology Inc.

  File Name:
    datastream.h

  Summary:
    Bootloader Data Stream Interface Header File

  Description:
    The Bootloader Data Stream provides an abstraction layer to allow any type
    of communication protocol/interface (USB, USART, SPI, SD Card) to be used
    by the bootloader.
*******************************************************************************/

//DOM-IGNORE-BEGIN
/*******************************************************************************
Copyright (c) 2014 released Microchip Technology Inc.  All rights reserved.

Microchip licenses to you the right to use, modify, copy and distribute
Software only when embedded on a Microchip microcontroller or digital signal
controller that is integrated into your product or third party product
(pursuant to the sublicense terms in the accompanying license agreement).

You should refer to the license agreement accompanying this Software for
additional information regarding your rights and obligations.

SOFTWARE AND DOCUMENTATION ARE PROVIDED AS IS  WITHOUT  WARRANTY  OF  ANY  KIND,
EITHER EXPRESS  OR  IMPLIED,  INCLUDING  WITHOUT  LIMITATION,  ANY  WARRANTY  OF
MERCHANTABILITY, TITLE, NON-INFRINGEMENT AND FITNESS FOR A  PARTICULAR  PURPOSE.
IN NO EVENT SHALL MICROCHIP OR  ITS  LICENSORS  BE  LIABLE  OR  OBLIGATED  UNDER
CONTRACT, NEGLIGENCE, STRICT LIABILITY, CONTRIBUTION,  BREACH  OF  WARRANTY,  OR
OTHER LEGAL  EQUITABLE  THEORY  ANY  DIRECT  OR  INDIRECT  DAMAGES  OR  EXPENSES
INCLUDING BUT NOT LIMITED TO ANY  INCIDENTAL,  SPECIAL,  INDIRECT,  PUNITIVE  OR
CONSEQUENTIAL DAMAGES, LOST  PROFITS  OR  LOST  DATA,  COST  OF  PROCUREMENT  OF
SUBSTITUTE  GOODS,  TECHNOLOGY,  SERVICES,  OR  ANY  CLAIMS  BY  THIRD   PARTIES
(INCLUDING BUT NOT LIMITED TO ANY DEFENSE  THEREOF),  OR  OTHER  SIMILAR  COSTS.
*******************************************************************************/

#ifndef DATASTREAM_H
#define	DATASTREAM_H

#include "system/system.h"
#include "driver/driver_common.h"
#include "nvm.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define APP_USR_CONTEXT 1


// *****************************************************************************
/* Datastream Buffer Handle

  Summary:
    Handle identifying a read or write buffer passed to the driver.

  Description:

  Remarks:
    None
*/

typedef uintptr_t DATASTREAM_BUFFER_HANDLE;

// *****************************************************************************
/* DATASTREAM Driver Buffer Events

   Summary
    Identifies the possible events that can result from a buffer add request.

   Description
    This enumeration identifies the possible events that can result from a
    buffer add request caused by the client calling either the
    DRV_USART_BufferAddRead or DRV_USART_BufferAddWrite functions.

   Remarks:
    One of these values is passed in the "event" parameter of the event
    handling callback function that the client registered with the driver by
    calling the DRV_USART_BufferEventHandlerSet function when a buffer
    transfer request is completed.

*/

typedef enum
{
    /* All data from or to the buffer was transferred successfully. */
    DATASTREAM_BUFFER_EVENT_COMPLETE,

    /* There was an error while processing the buffer transfer request. */
    DATASTREAM_BUFFER_EVENT_ERROR,

    /* Data transfer aborted (Applicable in DMA mode) */
    DATASTREAM_BUFFER_EVENT_ABORT


} DATASTREAM_BUFFER_EVENT;


    extern SYS_MODULE_OBJ   datastreamModule;

    extern void DATASTREAM_Initialize(void);
    DRV_HANDLE DATASTREAM_Open(const DRV_IO_INTENT ioIntent);
    extern void DATASTREAM_BufferEventHandlerSet
    (
        const DRV_HANDLE hClient,
        const void * eventHandler,
        const uintptr_t context
    );
    extern DRV_CLIENT_STATUS DATASTREAM_ClientStatus(DRV_HANDLE);
    
    int DATASTREAM_Data_Read(uintptr_t * const, unsigned char *, const int);
    int DATASTREAM_Data_Write(uintptr_t * const, unsigned char *, const int);
    void DATASTREAM_Close(void);

        typedef void DATASTREAM_HandlerType(DATASTREAM_BUFFER_EVENT,
                            DATASTREAM_BUFFER_HANDLE,
                            uint16_t);
        
DATASTREAM_HandlerType* handler;// = (DATASTREAM_HandlerType*) NULL;
uintptr_t _context;
uintptr_t _bufferHandle;// = NULL;
uint8_t * _rxBuffer;
uint32_t _rxMaxSize;
uint32_t _rxCurSize;
uint8_t * _txBuffer;
uint32_t _txMaxSize;
uint32_t _txCurPos;

typedef enum {
    IDLE = 0,
    RX,
    TX
} eDIR;

eDIR currDir;

void DATASTREAM_Tasks(void);
//DOM-IGNORE-END
#ifdef	__cplusplus
}
#endif

#endif	/* DATASTREAM_H */

