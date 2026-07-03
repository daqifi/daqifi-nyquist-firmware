/******************************************************************************
  SD Card (SPI) Driver Interface Implementation

  Company:
    Microchip Technology Inc.

  File Name:
    drv_sdspi_driver_interface.c

  Summary:
    SD Card (SPI) Driver Interface implementation

  Description:
    This interface file segregates the SD Card SPI protocol from the underlying
    hardware layer implementation for SPI and Timer System service
*******************************************************************************/

// DOM-IGNORE-BEGIN
/*******************************************************************************
* Copyright (C) 2019 Microchip Technology Inc. and its subsidiaries.
*
* Subject to your compliance with these terms, you may use Microchip software
* and any derivatives exclusively with Microchip products. It is your
* responsibility to comply with third party license terms applicable to your
* use of third party software (including open source software) that may
* accompany Microchip software.
*
* THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
* EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED
* WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A
* PARTICULAR PURPOSE.
*
* IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
* INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
* WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS
* BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO THE
* FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN
* ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
* THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
 *******************************************************************************/
// DOM-IGNORE-END

// *****************************************************************************
// *****************************************************************************
// Section: Include Files
// *****************************************************************************
// *****************************************************************************

#include <string.h>
#include "driver/spi/drv_spi.h"
#include "drv_sdspi_driver_interface.h"

// *****************************************************************************
/* Timer Event Handler

  Summary:
    Event handler registered by the SD card driver with the Timer System Service

  Description:
    This event handler is called by the Timer System Service when the requested
    time period has elapsed.

  Remarks:

*/

static void DRV_SDSPI_TimerCallback( uintptr_t context )
{
    bool *flag = (bool *)context;
    *flag = true;
}

// *****************************************************************************
/* SDSPI SPI Driver Event Handler

  Summary:
    Event handler registered by the SD card driver with the SPI Driver

  Description:
    This event handler is called by the SPI Driver when a request is complete

  Remarks:
*/

void DRV_SDSPI_SPIDriverEventHandler(
    DRV_SPI_TRANSFER_EVENT event,
    DRV_SPI_TRANSFER_HANDLE transferHandle,
    uintptr_t context
)
{
    DRV_SDSPI_OBJ* dObj = (DRV_SDSPI_OBJ *)context;

    /* #567 rework (SD-detect regression fix): the original guard ignored any
     * completion whose handle didn't match expectedXferHandle — but that
     * field is stored only AFTER DRV_SPI_*TransferAdd() returns, and short
     * transfers complete from ISR context before the store executes, so
     * valid completions were dropped (spiTransferStatus stuck IN_PROGRESS,
     * watchdog aborted, the card never initialized — bisected to #567/#578,
     * clean on its parent commit). Accept completions by default; ignore
     * ONLY the late completion of a transfer the watchdog explicitly aborted
     * (recorded at abort time, when the handle is stable), which is the case
     * the #567 guard existed for. */
    if ((dObj->abortedXferHandle != DRV_SPI_TRANSFER_HANDLE_INVALID) &&
        (transferHandle == dObj->abortedXferHandle))
    {
        dObj->abortedXferHandle = DRV_SPI_TRANSFER_HANDLE_INVALID;
        return;
    }

    if (event == DRV_SPI_TRANSFER_EVENT_COMPLETE)
    {
        dObj->spiTransferStatus = DRV_SDSPI_SPI_TRANSFER_STATUS_COMPLETE;
    }
    else
    {
        dObj->spiTransferStatus = DRV_SDSPI_SPI_TRANSFER_STATUS_ERROR;
    }

    SYS_PORT_PinSet(dObj->chipSelectPin);
}

// *****************************************************************************
/* SD Card SPI Write

  Summary:
    Writes the requested number of bytes to the SD Card

  Description:
    This API submits a write request to the SPI driver. Reports true if the
    request is accepted successfully by the SPI driver; false otherwise.
  Remarks:
*/

bool DRV_SDSPI_SPIWrite(
    DRV_SDSPI_OBJ* dObj,
    void* pWriteBuffer,
    uint32_t nBytes
)
{
    bool isSuccess = false;
    DRV_SPI_TRANSFER_HANDLE wrTransferHandle;

    dObj->spiTransferStatus = DRV_SDSPI_SPI_TRANSFER_STATUS_IN_PROGRESS;

    DRV_SPI_WriteTransferAdd (dObj->spiDrvHandle, pWriteBuffer, nBytes, &wrTransferHandle);
    dObj->expectedXferHandle = wrTransferHandle;   /* #567: correlate completion */

    if (wrTransferHandle != DRV_SPI_TRANSFER_HANDLE_INVALID)
    {
        isSuccess = true;
    }
    else
    {
        dObj->spiTransferStatus = DRV_SDSPI_SPI_TRANSFER_STATUS_ERROR;
    }

    return isSuccess;
}

// *****************************************************************************
/* SD Card SPI read

  Summary:
    Reads the requested number of bytes from the SD Card

  Description:
    This API submits a read request to the SPI driver. Reports true if the
    request is accepted successfully by the SPI driver; false otherwise.

  Remarks:
*/

bool DRV_SDSPI_SPIRead(
    DRV_SDSPI_OBJ* dObj,
    void* pReadBuffer,
    uint32_t nBytes
)
{
    bool isSuccess = false;
    DRV_SPI_TRANSFER_HANDLE rdTransferHandle;

    dObj->spiTransferStatus = DRV_SDSPI_SPI_TRANSFER_STATUS_IN_PROGRESS;

    DRV_SPI_ReadTransferAdd (dObj->spiDrvHandle, pReadBuffer, nBytes, &rdTransferHandle);
    dObj->expectedXferHandle = rdTransferHandle;   /* #567: correlate completion */

    if (rdTransferHandle != DRV_SPI_TRANSFER_HANDLE_INVALID)
    {
        isSuccess = true;
    }
    else
    {
        dObj->spiTransferStatus = DRV_SDSPI_SPI_TRANSFER_STATUS_ERROR;
    }

    return isSuccess;
}

bool DRV_SDSPI_SPIWriteWithChipSelectDisabled(
    DRV_SDSPI_OBJ* dObj,
    void* pWriteBuffer,
    uint32_t nBytes
)
{
    bool isSuccess = false;
    DRV_SPI_TRANSFER_HANDLE wrTransferHandle;

    /* Disable Chip Select */
    (void) DRV_SDSPI_SPISpeedSetup(dObj, DRV_SDSPI_SPI_INITIAL_SPEED, SYS_PORT_PIN_NONE);

    dObj->spiTransferStatus = DRV_SDSPI_SPI_TRANSFER_STATUS_IN_PROGRESS;

    DRV_SPI_WriteTransferAdd (dObj->spiDrvHandle, pWriteBuffer, nBytes, &wrTransferHandle);
    dObj->expectedXferHandle = wrTransferHandle;   /* #567: correlate completion */

    if (wrTransferHandle != DRV_SPI_TRANSFER_HANDLE_INVALID)
    {
        isSuccess = true;
    }
    else
    {
        dObj->spiTransferStatus = DRV_SDSPI_SPI_TRANSFER_STATUS_ERROR;

        /* Re-enable Chip Select */
        (void) DRV_SDSPI_SPISpeedSetup(dObj, DRV_SDSPI_SPI_INITIAL_SPEED, dObj->chipSelectPin);
    }

    return isSuccess;
}

// *****************************************************************************
/* SD Card SPI Speed Setup

  Summary:
    Configures the SPI clock frequency.

  Description:
    This function is used by the SD Card driver to switch between the initial
    low frequency and to higher clock frequency once the SD card is initialized.

  Remarks:

*/

bool DRV_SDSPI_SPISpeedSetup(
    DRV_SDSPI_OBJ* const dObj,
    uint32_t clockFrequency,
    SYS_PORT_PIN chipSelectPin
)
{
    bool isSuccess = false;

    DRV_SPI_TRANSFER_SETUP sdspiSetup;

    /* SD Card reads the data on the rising edge of SCK, which means SPI Mode 0
     * and 3 => CPOL = 0, CPHA = 0 and CPOL = 1, CPHA = 1 are supported */

    sdspiSetup.baudRateInHz = clockFrequency;
    sdspiSetup.clockPhase = DRV_SPI_CLOCK_PHASE_VALID_LEADING_EDGE;
    sdspiSetup.clockPolarity = DRV_SPI_CLOCK_POLARITY_IDLE_LOW;
    sdspiSetup.dataBits = DRV_SPI_DATA_BITS_8;
    sdspiSetup.chipSelect = chipSelectPin;
    sdspiSetup.csPolarity = DRV_SPI_CS_POLARITY_ACTIVE_LOW;

    isSuccess = DRV_SPI_TransferSetup(dObj->spiDrvHandle, &sdspiSetup);

    return isSuccess;
}

// *****************************************************************************
/* SD Card SPI driver exclusive access lock

  Summary:
    Locks the SPI driver for exclusive use by the SDSPI driver

  Description:
    SDSPI driver calls this API to lock the SPI driver during the entire command-response
    sequence.

  Remarks:
    None
*/

bool DRV_SDSPI_SPIExclusiveAccess(DRV_SDSPI_OBJ* const dObj, bool isExclusive)
{
    return DRV_SPI_Lock(dObj->spiDrvHandle, isExclusive);
}

// *****************************************************************************
/* Card Insertion/Removal Polling Timer

  Summary:
    Registers an event handler with the Timer System Service and starts the
    command-response timer.

  Description:
    The registered event handler is called when the time period elapses.

  Remarks:

*/
bool DRV_SDSPI_CardDetectPollingTimerStart(
    DRV_SDSPI_OBJ* const dObj,
    uint32_t period
)
{
    bool isSuccess = false;
    dObj->cardPollingTimerExpired = false;

    dObj->cardPollingTmrHandle = SYS_TIME_CallbackRegisterMS(DRV_SDSPI_TimerCallback,
             (uintptr_t)&dObj->cardPollingTimerExpired, period, SYS_TIME_SINGLE);

    if (dObj->cardPollingTmrHandle != SYS_TIME_HANDLE_INVALID)
    {
        isSuccess = true;
    }

    return isSuccess;
}

// *****************************************************************************
/* Command Response Timer Start

  Summary:
    Registers an event handler with the Timer System Service and starts the
    command-response timer.

  Description:
    The registered event handler is called when the time period elapses.

  Remarks:

*/
bool DRV_SDSPI_CmdResponseTimerStart(
    DRV_SDSPI_OBJ* const dObj,
    uint32_t period
)
{
    bool isSuccess = false;
    dObj->cmdRespTmrExpired = false;

    dObj->cmdRespTmrHandle = SYS_TIME_CallbackRegisterMS(DRV_SDSPI_TimerCallback,
             (uintptr_t)&dObj->cmdRespTmrExpired, period, SYS_TIME_SINGLE);

    if (dObj->cmdRespTmrHandle != SYS_TIME_HANDLE_INVALID)
    {
        isSuccess = true;
    }

    return isSuccess;
}

// *****************************************************************************
/* Command Response Timer Stop

  Summary:
    Stops the command-response timer.

  Description:

  Remarks:

*/

bool DRV_SDSPI_CmdResponseTimerStop( DRV_SDSPI_OBJ* const dObj )
{
    bool isSuccess = false;

    if (dObj->cmdRespTmrHandle != SYS_TIME_HANDLE_INVALID)
    {
        (void) SYS_TIME_TimerDestroy(dObj->cmdRespTmrHandle);
        isSuccess = true;
    }

    return isSuccess;
}

// *****************************************************************************
/* SD Card Timer Start

  Summary:
    Starts the SD card timer.

  Description:
    The registered event handler is called when the time period elapses.

  Remarks:

*/

bool DRV_SDSPI_TimerStart(
    DRV_SDSPI_OBJ* const dObj,
    uint32_t period
)
{
    bool isSuccess = false;
    dObj->timerExpired = false;

    dObj->timerHandle = SYS_TIME_CallbackRegisterMS(DRV_SDSPI_TimerCallback,
             (uintptr_t)&dObj->timerExpired, period, SYS_TIME_SINGLE);

    if (dObj->timerHandle != SYS_TIME_HANDLE_INVALID)
    {
        isSuccess = true;
    }

    return isSuccess;
}

// *****************************************************************************
/* SD Card Timer Stop

  Summary:
    Stops the SD card timer.

  Description:
    The registered event handler is called when the time period elapses.

  Remarks:

*/

bool DRV_SDSPI_TimerStop( DRV_SDSPI_OBJ* const dObj )
{
    bool isSuccess = false;

    if (dObj->timerHandle != SYS_TIME_HANDLE_INVALID)
    {
        (void) SYS_TIME_TimerDestroy(dObj->timerHandle);
        isSuccess = true;
    }

    return isSuccess;
}

/* #567: dedicated start/stop for the SPI bus-completion (DMA) wait timer.
 * Mirrors DRV_SDSPI_TimerStart/Stop but targets spiXferTimer* so it never
 * collides with the shared read/write busy-token timer (timerHandle), which
 * can be concurrently armed while DRV_SDSPI_TASK_SPI_STATUS is waiting.
 * Reuses the generic DRV_SDSPI_TimerCallback (sets *(bool*)context = true). */
bool DRV_SDSPI_SpiXferTimerStart(
    DRV_SDSPI_OBJ* const dObj,
    uint32_t period
)
{
    bool isSuccess = false;
    dObj->spiXferTimerExpired = false;

    dObj->spiXferTimerHandle = SYS_TIME_CallbackRegisterMS(DRV_SDSPI_TimerCallback,
             (uintptr_t)&dObj->spiXferTimerExpired, period, SYS_TIME_SINGLE);

    if (dObj->spiXferTimerHandle != SYS_TIME_HANDLE_INVALID)
    {
        isSuccess = true;
    }

    return isSuccess;
}

bool DRV_SDSPI_SpiXferTimerStop( DRV_SDSPI_OBJ* const dObj )
{
    bool isSuccess = false;

    if (dObj->spiXferTimerHandle != SYS_TIME_HANDLE_INVALID)
    {
        (void) SYS_TIME_TimerDestroy(dObj->spiXferTimerHandle);
        isSuccess = true;
    }

    return isSuccess;
}
