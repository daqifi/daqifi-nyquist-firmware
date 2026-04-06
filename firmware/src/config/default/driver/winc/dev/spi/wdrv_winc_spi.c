/*******************************************************************************
  WINC Wireless Driver SPI Communication Support

  File Name:
    wdrv_winc_spi.c

  Summary:
    WINC Wireless Driver SPI Communications Support

  Description:
    Supports SPI communications to the WINC module.
 *******************************************************************************/

//DOM-IGNORE-BEGIN
/*
Copyright (C) 2019-22, Microchip Technology Inc., and its subsidiaries. All rights reserved.

The software and documentation is provided by microchip and its contributors
"as is" and any express, implied or statutory warranties, including, but not
limited to, the implied warranties of merchantability, fitness for a particular
purpose and non-infringement of third party intellectual property rights are
disclaimed to the fullest extent permitted by law. In no event shall microchip
or its contributors be liable for any direct, indirect, incidental, special,
exemplary, or consequential damages (including, but not limited to, procurement
of substitute goods or services; loss of use, data, or profits; or business
interruption) however caused and on any theory of liability, whether in contract,
strict liability, or tort (including negligence or otherwise) arising in any way
out of the use of the software and documentation, even if advised of the
possibility of such damage.

Except as expressly permitted hereunder and subject to the applicable license terms
for any third-party software incorporated in the software and any applicable open
source software license terms, no license or other rights, whether express or
implied, are granted under any patent or other intellectual property rights of
Microchip or any third party.
*/

#include "configuration.h"
#include "definitions.h"
#include "osal/osal.h"
#include "driver/spi/drv_spi.h"
#include "wdrv_winc_common.h"
#include "wdrv_winc_spi.h"
#include "Util/Logger.h"

// *****************************************************************************
// *****************************************************************************
// Section: Data Type Definitions
// *****************************************************************************
// *****************************************************************************

typedef struct
{
    /* This is the SPI configuration. */
    WDRV_WINC_SPI_CFG       cfg;
    DRV_HANDLE              spiHandle;
    DRV_SPI_TRANSFER_HANDLE transferTxHandle;
    DRV_SPI_TRANSFER_HANDLE transferRxHandle;
    OSAL_SEM_HANDLE_TYPE    txSyncSem;
    OSAL_SEM_HANDLE_TYPE    rxSyncSem;
} WDRV_WINC_SPIDCPT;

// *****************************************************************************
// DAQiFi MODIFICATION SENTINEL — if this line causes a build error after an
// MCC/Harmony update, the file was overwritten. Re-apply patches from:
// https://github.com/daqifi/daqifi-nyquist-firmware/wiki/Harmony-Driver-Patches
// Changes: static alignedBuffer[] → CoherentPool pointer, added SetBuffer/WaitIdle
#define DAQIFI_WINC_SPI_PATCHED 1
// *****************************************************************************
// *****************************************************************************
// Section: Global Data
// *****************************************************************************
// *****************************************************************************

static WDRV_WINC_SPIDCPT spiDcpt;
// WiFi SPI DMA staging buffer — allocated from CoherentPool at boot,
// auto-balanced at stream start. Pool-managed instead of static to
// share coherent memory with SD/USB DMA buffers.
static uint8_t* alignedBuffer = NULL;
static uint32_t alignedBufferSize = 0;

void WDRV_WINC_SPI_SetBuffer(uint8_t* buf, uint32_t size) {
    if (buf == NULL || size == 0) return;
    alignedBuffer = buf;
    alignedBufferSize = size;
}

bool WDRV_WINC_SPI_WaitIdle(uint32_t timeout_ms) {
    TickType_t start = xTaskGetTickCount();
    TickType_t timeoutTicks = pdMS_TO_TICKS(timeout_ms);
    while ((xTaskGetTickCount() - start) < timeoutTicks) {
        if (spiDcpt.transferTxHandle == DRV_SPI_TRANSFER_HANDLE_INVALID &&
            spiDcpt.transferRxHandle == DRV_SPI_TRANSFER_HANDLE_INVALID) {
            return true;
        }
        vTaskDelay(1);
    }
    return false;
}

// *****************************************************************************
// *****************************************************************************
// Section: File scope functions
// *****************************************************************************
// *****************************************************************************

static void spiTransferEventHandler(DRV_SPI_TRANSFER_EVENT event,
        DRV_SPI_TRANSFER_HANDLE handle, uintptr_t context)
{
    switch(event)
    {
        case DRV_SPI_TRANSFER_EVENT_COMPLETE:
            // This means the data was transferred.
            // Post semaphore only — do NOT clear handle here.
            // Handle is cleared in SPISend/SPIReceive AFTER post-DMA
            // memcpy completes. This prevents WaitIdle() from returning
            // true while the task is still using alignedBuffer.
            if (spiDcpt.transferTxHandle == handle)
            {
                OSAL_SEM_PostISR(&spiDcpt.txSyncSem);
            }
            else if (spiDcpt.transferRxHandle == handle)
            {
                OSAL_SEM_PostISR(&spiDcpt.rxSyncSem);
            }

            break;

        case DRV_SPI_TRANSFER_EVENT_ERROR:
            // Post semaphore on error to prevent deadlock in SPISend/SPIReceive.
            if (spiDcpt.transferTxHandle == handle)
            {
                OSAL_SEM_PostISR(&spiDcpt.txSyncSem);
            }
            else if (spiDcpt.transferRxHandle == handle)
            {
                OSAL_SEM_PostISR(&spiDcpt.rxSyncSem);
            }
            break;

        default:
            break;
    }
}

//*******************************************************************************
/*
  Function:
    bool WDRV_WINC_SPISend(void* pTransmitData, size_t txSize)

  Summary:
    Sends data out to the module through the SPI bus.

  Description:
    This function sends data out to the module through the SPI bus.

  Remarks:
    See wdrv_winc_spi.h for usage information.
 */

bool WDRV_WINC_SPISend(void* pTransmitData, size_t txSize)
{
    if (alignedBuffer == NULL || txSize > alignedBufferSize) return false;
    memcpy(alignedBuffer, pTransmitData, txSize);
    DRV_SPI_WriteTransferAdd(spiDcpt.spiHandle, alignedBuffer, txSize, &spiDcpt.transferTxHandle);

    if (DRV_SPI_TRANSFER_HANDLE_INVALID == spiDcpt.transferTxHandle)
    {
        return false;
    }

    while (OSAL_RESULT_FALSE == OSAL_SEM_Pend(&spiDcpt.txSyncSem, OSAL_WAIT_FOREVER))
    {
    }

    // DMA complete, buffer no longer in use — mark idle for WaitIdle()
    spiDcpt.transferTxHandle = DRV_SPI_TRANSFER_HANDLE_INVALID;
    return true;
}

//*******************************************************************************
/*
  Function:
    bool WDRV_WINC_SPIReceive(void* pReceiveData, size_t rxSize)

  Summary:
    Receives data from the module through the SPI bus.

  Description:
    This function receives data from the module through the SPI bus.

  Remarks:
    See wdrv_winc_spi.h for usage information.
 */

bool WDRV_WINC_SPIReceive(void* pReceiveData, size_t rxSize)
{
    static uint8_t dummy = 0;

    if (alignedBuffer == NULL || rxSize > alignedBufferSize) return false;
    DRV_SPI_WriteReadTransferAdd(spiDcpt.spiHandle, &dummy, 1, alignedBuffer, rxSize, &spiDcpt.transferRxHandle);

    if (DRV_SPI_TRANSFER_HANDLE_INVALID == spiDcpt.transferRxHandle)
    {
        return false;
    }

    while (OSAL_RESULT_FALSE == OSAL_SEM_Pend(&spiDcpt.rxSyncSem, OSAL_WAIT_FOREVER))
    {
    }

    memcpy(pReceiveData, alignedBuffer, rxSize);

    // Buffer fully consumed — mark idle for WaitIdle()
    spiDcpt.transferRxHandle = DRV_SPI_TRANSFER_HANDLE_INVALID;
    return true;
}

//*******************************************************************************
/*
  Function:
    bool WDRV_WINC_SPIOpen(void)

  Summary:
    Opens the SPI object for the WiFi driver.

  Description:
    This function opens the SPI object for the WiFi driver.

  Remarks:
    See wdrv_winc_spi.h for usage information.
 */

bool WDRV_WINC_SPIOpen(void)
{
    DRV_SPI_TRANSFER_SETUP spiTransConf = {
        .clockPhase     = DRV_SPI_CLOCK_PHASE_VALID_LEADING_EDGE,
        .clockPolarity  = DRV_SPI_CLOCK_POLARITY_IDLE_LOW,
        .dataBits       = DRV_SPI_DATA_BITS_8,
        .csPolarity     = DRV_SPI_CS_POLARITY_ACTIVE_LOW
    };

    if (OSAL_RESULT_TRUE != OSAL_SEM_Create(&spiDcpt.txSyncSem, OSAL_SEM_TYPE_COUNTING, 10, 0))
    {
        WDRV_DBG_ERROR_PRINT("WiFi SPI: txSyncSem create failed\r\n");
        LOG_E("WiFi SPI: txSyncSem create failed (heap exhaustion?)\r\n");
        return false;
    }

    if (OSAL_RESULT_TRUE != OSAL_SEM_Create(&spiDcpt.rxSyncSem, OSAL_SEM_TYPE_COUNTING, 10, 0))
    {
        WDRV_DBG_ERROR_PRINT("WiFi SPI: rxSyncSem create failed\r\n");
        LOG_E("WiFi SPI: rxSyncSem create failed (heap exhaustion?)\r\n");
        return false;
    }

    // Initialize transfer handles to INVALID so WaitIdle() works correctly.
    // Static init leaves them at 0, but INVALID is 0xFFFFFFFF.
    spiDcpt.transferTxHandle = DRV_SPI_TRANSFER_HANDLE_INVALID;
    spiDcpt.transferRxHandle = DRV_SPI_TRANSFER_HANDLE_INVALID;

    if (DRV_HANDLE_INVALID == spiDcpt.spiHandle)
    {
        spiDcpt.spiHandle = DRV_SPI_Open(spiDcpt.cfg.drvIndex, DRV_IO_INTENT_READWRITE | DRV_IO_INTENT_BLOCKING);

        if (DRV_HANDLE_INVALID == spiDcpt.spiHandle)
        {
            WDRV_DBG_ERROR_PRINT("WiFi SPI: DRV_SPI_Open failed (index=%d)\r\n", spiDcpt.cfg.drvIndex);
            LOG_E("WiFi SPI: DRV_SPI_Open(index=%d) failed\r\n", spiDcpt.cfg.drvIndex);
            return false;
        }
        WDRV_DBG_INFORM_PRINT("WiFi SPI: DRV_SPI_Open succeeded (index=%d, handle=%p)\r\n",
                              spiDcpt.cfg.drvIndex, spiDcpt.spiHandle);
    }

    spiTransConf.baudRateInHz = spiDcpt.cfg.baudRateInHz;
    spiTransConf.chipSelect   = spiDcpt.cfg.chipSelect;

    if (false == DRV_SPI_TransferSetup(spiDcpt.spiHandle, &spiTransConf))
    {
        WDRV_DBG_ERROR_PRINT("SPI transfer setup failed\r\n");

        return false;
    }

    DRV_SPI_TransferEventHandlerSet(spiDcpt.spiHandle, spiTransferEventHandler, 0);

    return true;
}

//*******************************************************************************
/*
  Function:
    void WDRV_WINC_SPIInitialize(const WDRV_WINC_SPI_CFG *const pInitData)

  Summary:
    Initializes the SPI object for the WiFi driver.

  Description:
    This function initializes the SPI object for the WiFi driver.

  Remarks:
    See wdrv_winc_spi.h for usage information.
 */

void WDRV_WINC_SPIInitialize(const WDRV_WINC_SPI_CFG *const pInitData)
{
    if (NULL == pInitData)
    {
        return;
    }

    memcpy(&spiDcpt.cfg, pInitData, sizeof(WDRV_WINC_SPI_CFG));

    spiDcpt.spiHandle = DRV_HANDLE_INVALID;
}

//*******************************************************************************
/*
  Function:
    void WDRV_WINC_SPIDeinitialize(void)

  Summary:
    Deinitializes the SPI object for the WiFi driver.

  Description:
    This function deinitializes the SPI object for the WiFi driver.

  Remarks:
    See wdrv_winc_spi.h for usage information.
 */

void WDRV_WINC_SPIDeinitialize(void)
{
    OSAL_SEM_Post(&spiDcpt.txSyncSem);
    OSAL_SEM_Delete(&spiDcpt.txSyncSem);

    OSAL_SEM_Post(&spiDcpt.rxSyncSem);
    OSAL_SEM_Delete(&spiDcpt.rxSyncSem);

    if (DRV_HANDLE_INVALID != spiDcpt.spiHandle)
    {
        DRV_SPI_Close(spiDcpt.spiHandle);
        spiDcpt.spiHandle = DRV_HANDLE_INVALID;
    }
}

//DOM-IGNORE-END
