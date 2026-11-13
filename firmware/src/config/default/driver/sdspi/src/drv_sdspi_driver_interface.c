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
#include <xc.h>               /* #1255: _CP0_GET_COUNT */
#include "configuration.h"    /* #1255: SYS_TIME_CPU_CLOCK_FREQUENCY */
#include "Util/Logger.h"

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


/* #567 rework (Qodo #590): remember a watchdog-aborted transfer so its late
 * completion -- DRV_SPI has no cancel API -- is ignored exactly once.
 *
 * #1217: record the handle ONLY while DRV_SPI still holds the transfer, i.e.
 * only while a late completion can still arrive. Recording it unconditionally
 * planted an entry whose completion had ALREADY been delivered, so nothing ever
 * consumed it. DRV_SDSPI_ReleaseBus records on every STANDBY trip, almost always
 * after the last transfer finished, and the xfer-timeout path records the very
 * handle whose completion this ring had just swallowed. DRV_SPI hands out the
 * same handle value again after one token-and-index cycle (measured 759.2 s
 * under SD logging), and the stale entry then swallowed that new transfer's
 * real completion. 500 ms later the #567 watchdog aborted it as `xfer-timeout`
 * and recorded it again, so each event re-armed the next. On the bench, 6
 * STANDBY cycles produced 4 abandoned writes in 30 min, each one the swallowed
 * handle, with the SPI queue empty and both DMA channels idle; with no STANDBY
 * cycle first, the same 30 min produced none.
 *
 * The check and the store share one critical section, which masks the DMA
 * completion interrupt (IPL 2, below configMAX_SYSCALL_INTERRUPT_PRIORITY), so
 * the transfer cannot complete between "still pending" and "recorded". */
void DRV_SDSPI_AbortedXferRecord(DRV_SDSPI_OBJ* const dObj)
{
    OSAL_CRITSECT_DATA_TYPE critState;

    if (dObj->expectedXferHandle == DRV_SPI_TRANSFER_HANDLE_INVALID)
    {
        return;
    }
    critState = OSAL_CRIT_Enter(OSAL_CRIT_TYPE_HIGH);
    if (DRV_SPI_TransferIsPending(dObj->spiDrvHandle, dObj->expectedXferHandle))
    {
        dObj->abortedXferHandles[dObj->abortedXferIdx] = dObj->expectedXferHandle;
        dObj->abortedXferIdx = (uint8_t)((dObj->abortedXferIdx + 1U) % DRV_SDSPI_ABORTED_XFER_SLOTS);
    }
    OSAL_CRIT_Leave(OSAL_CRIT_TYPE_HIGH, critState);
}

/* #1217: drop every entry whose transfer DRV_SPI no longer holds, BEFORE a new
 * submit. Such an entry can never be consumed (its completion was delivered or
 * will never come), and once DRV_SPI reuses its handle value it would swallow
 * the new transfer's completion. This replaces a forget that ran AFTER the
 * submit (Qodo #590): a short transfer completes from the DMA ISR before
 * DRV_SPI_*TransferAdd returns, so the event handler met the stale entry first
 * and the forget came too late. While an entry's transfer is still pending, its
 * pool slot is in use, so no new transfer can receive that handle value. */
static void lDRV_SDSPI_AbortedXferPrune(DRV_SDSPI_OBJ* const dObj)
{
    OSAL_CRITSECT_DATA_TYPE critState;

    for (uint8_t slot = 0; slot < DRV_SDSPI_ABORTED_XFER_SLOTS; slot++)
    {
        if (dObj->abortedXferHandles[slot] == DRV_SPI_TRANSFER_HANDLE_INVALID)
        {
            continue;
        }
        critState = OSAL_CRIT_Enter(OSAL_CRIT_TYPE_HIGH);
        if (!DRV_SPI_TransferIsPending(dObj->spiDrvHandle, dObj->abortedXferHandles[slot]))
        {
            dObj->abortedXferHandles[slot] = DRV_SPI_TRANSFER_HANDLE_INVALID;
        }
        OSAL_CRIT_Leave(OSAL_CRIT_TYPE_HIGH, critState);
    }
}

void DRV_SDSPI_SPIDriverEventHandler(
    DRV_SPI_TRANSFER_EVENT event,
    DRV_SPI_TRANSFER_HANDLE transferHandle,
    uintptr_t context
)
{
    DRV_SDSPI_OBJ* dObj = (DRV_SDSPI_OBJ *)context;

    /* #1217: raised first, so a late completion of an aborted transfer (the
     * early return below) cannot leave the card selected for whoever is next on
     * the bus. DRV_SPI raises CS itself only if the client's setup still names
     * the pin, and DRV_SDSPI_SPIWriteWithChipSelectDisabled clears that.
     *
     * Unconditional, and not gated on "no newer SD transfer in progress", because
     * this cannot pull CS from under one: DRV_SPI's queue is FIFO and it asserts
     * the next transfer's CS only after this handler returns (drv_spi.c, the
     * lDRV_SPI_RX_DMA_CallbackHandler tail). A gate would be actively wrong: with
     * a newer SD transfer queued behind a WINC one, it would leave CS low across
     * the WINC transfer. */
    if (!dObj->csHeld)   /* #1255: a held CS is raised at bus release instead */
    {
        SYS_PORT_PinSet(dObj->chipSelectPin);
    }

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
    for (uint8_t slot = 0; slot < DRV_SDSPI_ABORTED_XFER_SLOTS; slot++)
    {
        if ((dObj->abortedXferHandles[slot] != DRV_SPI_TRANSFER_HANDLE_INVALID) &&
            (transferHandle == dObj->abortedXferHandles[slot]))
        {
            dObj->abortedXferHandles[slot] = DRV_SPI_TRANSFER_HANDLE_INVALID;
            return;
        }
    }

    if (event == DRV_SPI_TRANSFER_EVENT_COMPLETE)
    {
        dObj->spiTransferStatus = DRV_SDSPI_SPI_TRANSFER_STATUS_COMPLETE;
    }
    else
    {
        dObj->spiTransferStatus = DRV_SDSPI_SPI_TRANSFER_STATUS_ERROR;
    }
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

    lDRV_SDSPI_AbortedXferPrune(dObj);   /* #1217: before the submit, not after */
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

    lDRV_SDSPI_AbortedXferPrune(dObj);   /* #1217: before the submit, not after */
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

    lDRV_SDSPI_AbortedXferPrune(dObj);   /* #1217: before the submit, not after */
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

/* #1255: end a held-CS request. Raise SD CS, then clock one 0xFF that does not
 * touch CS: the card releases DataOut only on the clock after CS goes high
 * (SanDisk SD Card Product Manual v2.2, section 5.23.4), and without that clock
 * it can still be driving MISO into the next transfer on the shared bus, which
 * is a WINC transfer. Runs while this driver still holds the exclusive lock, so
 * the byte is queued ahead of any other client's transfer.
 *
 * The byte is a no-CS transfer (DRV_SPI_WriteTransferAddNoCs), not a change to
 * the client setup: DRV_SPI applies a setup when a transfer STARTS, so a byte
 * still queued behind a watchdog-abandoned transfer would start under whatever
 * setup was current by then and assert CS. A no-CS transfer cannot, however
 * late it runs.
 *
 * The wait is for completion bookkeeping: the byte's completion must arrive as
 * this request's, not the next one's. One byte takes well under 1 us at the
 * card clock; the wait is capped at 1 ms, and on expiry the handle goes to the
 * abort ring so its late completion is ignored.
 *
 * Skipped when an SD transfer is still in progress (a lost completion being
 * unwound, e.g. DRV_SDSPI_ReleaseBus): that handle must stay in
 * expectedXferHandle for the abort ring, and the caller raises CS itself. */
static void lDRV_SDSPI_ReleaseHeldCs(DRV_SDSPI_OBJ* const dObj)
{
    DRV_SPI_TRANSFER_HANDLE h;
    uint32_t t0;
    const uint32_t limit = (SYS_TIME_CPU_CLOCK_FREQUENCY / 2U) / 1000U;   /* 1 ms */

    dObj->csHeld = false;
    DRV_SPI_ChipSelectHold(dObj->spiDrvHandle, false);
    SYS_PORT_PinSet(dObj->chipSelectPin);
    if (dObj->spiTransferStatus == DRV_SDSPI_SPI_TRANSFER_STATUS_IN_PROGRESS)
    {
        return;
    }
    lDRV_SDSPI_AbortedXferPrune(dObj);
    dObj->spiTransferStatus = DRV_SDSPI_SPI_TRANSFER_STATUS_IN_PROGRESS;
    DRV_SPI_WriteTransferAddNoCs(dObj->spiDrvHandle, dObj->pClkPulseData, 1U, &h);
    dObj->expectedXferHandle = h;
    if (h == DRV_SPI_TRANSFER_HANDLE_INVALID)
    {
        dObj->spiTransferStatus = DRV_SDSPI_SPI_TRANSFER_STATUS_ERROR;
    }
    else
    {
        t0 = _CP0_GET_COUNT();
        while ((dObj->spiTransferStatus == DRV_SDSPI_SPI_TRANSFER_STATUS_IN_PROGRESS) &&
               ((_CP0_GET_COUNT() - t0) < limit))
        {
            /* the DMA completion ISR updates spiTransferStatus */
        }
        if (dObj->spiTransferStatus == DRV_SDSPI_SPI_TRANSFER_STATUS_IN_PROGRESS)
        {
            DRV_SDSPI_AbortedXferRecord(dObj);
            dObj->spiTransferStatus = DRV_SDSPI_SPI_TRANSFER_STATUS_ERROR;
            dObj->csTrailTimeouts++;
            if (dObj->csTrailTimeouts <= 4U)
            {
                LOG_E("SD: MISO-release clock timed out (n=%lu)", (unsigned long)dObj->csTrailTimeouts);
            }
        }
    }
}

bool DRV_SDSPI_SPIExclusiveAccess(DRV_SDSPI_OBJ* const dObj, bool isExclusive)
{
    bool ok;

    if (isExclusive)
    {
        ok = DRV_SPI_Lock(dObj->spiDrvHandle, true);
        if (ok && (dObj->csLockDepth < 255U))
        {
            dObj->csLockDepth++;
        }
        return ok;
    }
    if (dObj->csLockDepth > 0U)
    {
        dObj->csLockDepth--;
    }
    if ((dObj->csLockDepth == 0U) && dObj->csHeld)
    {
        lDRV_SDSPI_ReleaseHeldCs(dObj);
    }
    return DRV_SPI_Lock(dObj->spiDrvHandle, false);
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
