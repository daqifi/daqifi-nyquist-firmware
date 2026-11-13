/* #1217: the REAL SD driver, as one host translation unit, for
 * test_1217_abandoned_write.c. Built with warnings off: this is vendored code
 * compiled for a platform it was not written for, and warnings in it are not
 * this test's subject. The driver has one-shot Initialize state and no
 * teardown, so this also gives the test a way to start each case clean. */
#include "drv_sdspi.c"
#include "drv_sdspi_driver_interface.c"

void test1217_DriverReset(void)
{
    (void) memset(gDrvSDSPIObj, 0, sizeof(gDrvSDSPIObj));
    for (unsigned i = 0U; i < DRV_SDSPI_INSTANCES_NUMBER; i++)
    {
        /* These are file-scope, so they outlive the memset. Same value as
         * Initialize leaves them, but a previous case may have sent a token
         * through the buffers. */
        (void) memset(gDrvSDSPICmdResponseBuffer[i], 0, sizeof(gDrvSDSPICmdResponseBuffer[i]));
    }
}

bool test1217_InResync(void)
{
    const DRV_SDSPI_CMD_STATES st = gDrvSDSPIObj[0].cmdState;
    return (st == DRV_SDSPI_CMD_RESYNC_START) || (st == DRV_SDSPI_CMD_RESYNC_FLUSH) ||
            (st == DRV_SDSPI_CMD_RESYNC_BUSY_CHECK) || (st == DRV_SDSPI_CMD_RESYNC_STOP);
}

unsigned test1217_WriteRetries(void)
{
    return gDrvSDSPIObj[0].writeInDoubtRetries;
}

unsigned test1217_RetryLimit(void)
{
    return DRV_SDSPI_WRITE_IN_DOUBT_RETRIES;
}

/* #1217 (stale-ring fix): the aborted-transfer ring, read for the tests that
 * pin which handles it may hold. */
unsigned test1217_AbortedRingCount(void)
{
    unsigned n = 0U;
    for (unsigned i = 0U; i < DRV_SDSPI_ABORTED_XFER_SLOTS; i++)
    {
        if (gDrvSDSPIObj[0].abortedXferHandles[i] != DRV_SPI_TRANSFER_HANDLE_INVALID)
        {
            n++;
        }
    }
    return n;
}

DRV_SPI_TRANSFER_HANDLE test1217_LastSubmitted(void)
{
    return gDrvSDSPIObj[0].expectedXferHandle;
}

/* Every ring entry names a transfer DRV_SPI still holds. */
bool test1217_AbortedRingAllPending(void)
{
    for (unsigned i = 0U; i < DRV_SDSPI_ABORTED_XFER_SLOTS; i++)
    {
        const DRV_SPI_TRANSFER_HANDLE h = gDrvSDSPIObj[0].abortedXferHandles[i];
        if ((h != DRV_SPI_TRANSFER_HANDLE_INVALID) &&
                !DRV_SPI_TransferIsPending(gDrvSDSPIObj[0].spiDrvHandle, h))
        {
            return false;
        }
    }
    return true;
}

/* #1255 audit r2: the card model detaches after a dead bus, which closes the
 * window the audit found (a request starting while an abandoned request's
 * transfers are still queued, reached on the board through a failed timer
 * allocation in the resync). This reopens it so the hold decision is tested. */
void test1255_ForceAttached(void)
{
    gDrvSDSPIObj[0].mediaState = SYS_MEDIA_ATTACHED;
}
