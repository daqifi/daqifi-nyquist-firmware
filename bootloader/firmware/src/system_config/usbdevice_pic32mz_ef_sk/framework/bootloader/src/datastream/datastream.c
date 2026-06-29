/*******************************************************************************
 Data Stream Source File

  File Name:
    datastream.c

  Summary:
 Data Stream

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

#include <string.h>
#include <sys/kmem.h>

#include "system/common/sys_module.h"
#include "../datastream.h"
#include "bootloader/src/bootloader.h"
#include "system/reset/sys_reset.h"

SYS_MODULE_OBJ  datastreamModule;
extern BOOTLOADER_DATA bootloaderData __attribute__((coherent, aligned(16)));

DRV_CLIENT_STATUS __attribute__((weak)) DATASTREAM_ClientStatus(DRV_HANDLE handle)
{
    return DRV_CLIENT_STATUS_READY;
}

void __attribute__((weak)) DATASTREAM_BufferEventHandlerSet
(
    const DRV_HANDLE hClient,
    const void * eventHandler,
    const uintptr_t context
)
{
    handler = (DATASTREAM_HandlerType*)eventHandler;
    _context = context;
}

DRV_HANDLE __attribute__((weak)) DATASTREAM_Open(const DRV_IO_INTENT ioIntent)
{
    return 0;
}

void Bootloader_BufferEventHandler(DATASTREAM_BUFFER_EVENT buffEvent,
                            DATASTREAM_BUFFER_HANDLE hBufferEvent,
                            uint16_t context )
{

  uint16_t crc;
  int i = 0;
  while(i < context)
  {    
    switch(buffEvent)
    {
        /* Buffer event is completed successfully */
        case DATASTREAM_BUFFER_EVENT_COMPLETE:
        {
                /* Check previous state to know what to check */
                if (BOOTLOADER_GET_COMMAND == bootloaderData.prevState)
                {
                    // If we were in an Escape sequence, copy the data on and reset the flag.
                    if (bootloaderData.rxEscapePending)
                    {
                        // Bounds-guard the accumulator (buff2 is BOOTLOADER_BUFFER_SIZE): an over-long
                        // unframed stream must never write past it. On overflow drop the partial frame;
                        // the EOT CRC check (the parser's integrity gate) rejects any mis-framed remainder.
                        if (bootloaderData.cmdBufferLength < BOOTLOADER_BUFFER_SIZE)
                        {
                            bootloaderData.data->buffers.buff2[bootloaderData.cmdBufferLength++] = bootloaderData.data->buffers.buff1[i];
                        }
                        else
                        {
                            bootloaderData.cmdBufferLength = 0;
                        }
                        bootloaderData.rxEscapePending = false;
                    }
                    else
                    {
                        switch (bootloaderData.data->buffers.buff1[i])
                        {
                            case SOH:   // Start of header
                                bootloaderData.cmdBufferLength = 0;
                                bootloaderData.rxEscapePending = false;   // start a fresh frame: drop any dangling escape
                                break;

                            case EOT:   // End of transmission
                                // Calculate CRC and see if this frame is valid
                                if (bootloaderData.cmdBufferLength > 2)
                                {
                                    crc = bootloaderData.data->buffers.buff2[bootloaderData.cmdBufferLength-2];
                                    crc += ((bootloaderData.data->buffers.buff2[bootloaderData.cmdBufferLength-1])<<8);

                                    if (APP_CalculateCrc(bootloaderData.data->buffers.buff2, bootloaderData.cmdBufferLength-2) == crc)
                                    {
                                        // CRC matches so the frame is valid; hand it to the processor.
                                        bootloaderData.usrBufferEventComplete = true;
                                        return;
                                    }
                                    // CRC mismatch: drop the corrupt frame so a stray/garbled transfer
                                    // can never be dispatched as a command (e.g. a bogus ERASE_FLASH on
                                    // this CRC-checked, self-powered device). Reset the accumulator AND the
                                    // escape state and keep reading; the host times out and retries. This
                                    // is the inbound integrity gate that was previously disabled (empty
                                    // if-body). Clearing rxEscapePending here keeps a corrupt frame that
                                    // ended mid-escape from desyncing the parser across the retry.
                                    bootloaderData.cmdBufferLength = 0;
                                    bootloaderData.rxEscapePending = false;
                                }
                                break;

                            case DLE:   // Escape sequence
                                bootloaderData.rxEscapePending = true;
                                break;

                            default:
                                // Bounds-guard the accumulator (buff2 is BOOTLOADER_BUFFER_SIZE): an
                                // over-long/unframed stream must never write past it. On overflow drop the
                                // partial frame; the EOT CRC check (the parser's integrity gate) rejects any
                                // mis-framed remainder. (Frames are validated by CRC, not by SOH-gating.)
                                if (bootloaderData.cmdBufferLength < BOOTLOADER_BUFFER_SIZE)
                                {
                                    bootloaderData.data->buffers.buff2[bootloaderData.cmdBufferLength++] = bootloaderData.data->buffers.buff1[i];
                                }
                                else
                                {
                                    bootloaderData.cmdBufferLength = 0;
                                    bootloaderData.rxEscapePending = false;
                                }
                                break;
                        }
                    }
                  i++;  

                }
                else    /* APP_SEND_RESPONSE */
                {
                    bootloaderData.usrBufferEventComplete = true;
                    return;
                }
            
            break;
        }

        /* Buffer event has some error */
        case DATASTREAM_BUFFER_EVENT_ERROR:
        case DATASTREAM_BUFFER_EVENT_ABORT:
            break;
    }             
  }
  
      /* We don't have a complete command yet. Continue reading. */
                        DATASTREAM_Data_Read(&(bootloaderData.datastreamBufferHandle),
                                bootloaderData.data->buffers.buff1, bootloaderData.bufferSize);
  
}

void Bootloader_ProcessBuffer( BOOTLOADER_DATA *handle )
{
    uint8_t Cmd;
    uint32_t Address;
    uint32_t Length;
    uint16_t crc;

    /* First, check that we have a valid command. */
    Cmd = handle->data->buffers.buff2[0];

    /* Build the response frame from the command. */
    handle->data->buffers.buff1[0] = handle->data->buffers.buff2[0];
    handle->bufferSize = 0;
    
    switch (Cmd)
    {
        case READ_BOOT_INFO:
            memcpy(&handle->data->buffers.buff1[1], BootInfo, 2);
            handle->bufferSize = 2 + 1;
            handle->currentState = BOOTLOADER_SEND_RESPONSE;
            break;

        case ERASE_FLASH:
            APP_FlashErase();
            handle->currentState = BOOTLOADER_WAIT_FOR_NVM;
            handle->bufferSize = 1;
            break;

        case PROGRAM_FLASH:
            if(APP_ProgramHexRecord(&handle->data->buffers.buff2[1], handle->cmdBufferLength-3) != HEX_REC_NORMAL)
                break;
            handle->bufferSize = 1;
            handle->currentState = BOOTLOADER_SEND_RESPONSE;
            break;

        case READ_CRC:
            memcpy(&Address, &handle->data->buffers.buff2[1], sizeof(Address));
            memcpy(&Length, &handle->data->buffers.buff2[5], sizeof(Length));
#if defined(BOOTLOADER_LIVE_UPDATE_STATE_SAVE)
            crc = APP_CalculateCrc((uint8_t *)KVA0_TO_KVA1(Address + 
                    ((APP_FLASH_END_ADDRESS - APP_FLASH_BASE_ADDRESS) + 1)), Length);
#else            
            crc = APP_CalculateCrc((uint8_t *)KVA0_TO_KVA1(Address), Length);
#endif
            memcpy(&handle->data->buffers.buff1[1], &crc, 2);

            handle->bufferSize = 1 + 2;
            handle->currentState = BOOTLOADER_SEND_RESPONSE;
            break;

        case JMP_TO_APP:
            handle->currentState = BOOTLOADER_CLOSE_DATASTREAM;
#if(BOOTLOADER_LIVE_UPDATE_STATE_SAVE != 1)
            SYS_RESET_SoftwareReset();
#endif
            break;

        default:
            break;
    }
}

int DATASTREAM_Data_Read(uintptr_t * const bufferHandle, unsigned char* buffer, const int maxsize)
{
    if (*bufferHandle == DRV_HANDLE_INVALID)
    {
        *bufferHandle = 0;
        _bufferHandle = *bufferHandle;
    }
    _rxBuffer = buffer;
    _rxMaxSize = maxsize;
    _rxCurSize = 0;
    currDir = RX;
    return(0);
}

int DATASTREAM_Data_Write(uintptr_t * const bufferHandle, unsigned char* buffer, const int bufsize)
{
    if (*bufferHandle == DRV_HANDLE_INVALID)
    {
        *bufferHandle = 0;
        _bufferHandle = *bufferHandle;
    }
    _txBuffer = buffer;
    _txMaxSize = bufsize;
    _txCurPos = 0;
    currDir = TX;
    return(0);
}

void __attribute__((weak)) DATASTREAM_Close(void)
{
    bootloaderData.currentState = BOOTLOADER_ENTER_APPLICATION;
}
