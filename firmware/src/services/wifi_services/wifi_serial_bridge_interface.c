#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#define LOG_MODULE LOG_MODULE_WIFI
#include "Util/Logger.h"
#include "wifi_serial_bridge_interface.h"
#include "definitions.h"
#include "osal/osal.h"
#include "../UsbCdc/UsbCdc.h"

/* One full USB CDC read (512) of headroom on top of the largest single read,
 * BSS is too tight for a burst-sized ring (linker reserves 8 KB ISR stack).
 * Loss-freedom comes from backpressure in UsbCdc_TransparentReadCmpltCB
 * instead: it blocks (bounded) until the bridge drains, which holds off the
 * next CDC read arm and NAK-throttles the host at the USB level.
 * The previous 512-byte ring had NO overflow protection at all — it silently
 * wrapped over unread payload, corrupting WINC firmware images mid-flash with
 * a valid-looking ACK (#WINC-recovery). */
#define WIFI_SERIAL_BRIDGE_INTERFACE_USART_RECEIVE_BUFFER_SIZE   1024



static OSAL_MUTEX_HANDLE_TYPE gUsartReadMutex;
static uint8_t gUsartReceiveBuffer[WIFI_SERIAL_BRIDGE_INTERFACE_USART_RECEIVE_BUFFER_SIZE];
static size_t gUsartReceiveInOffset;
static size_t gUsartReceiveOutOffset;
static volatile size_t gUsartReceiveLength;

bool UsbCdc_TransparentReadCmpltCB(uint8_t* pBuff, size_t buffLen) {
    if (buffLen == 0)
        return true;
    if(gUsartReadMutex==NULL)
        return false;

    /* Backpressure (#WINC-recovery): runs on the USB task, and the next CDC
     * read is not re-armed until this callback returns — so waiting here for
     * the bridge task to drain makes the host NAK at the USB level instead of
     * losing bytes. The previous code copied unconditionally, silently
     * wrapping the ring over unread payload and corrupting WINC firmware
     * images mid-flash behind a valid-looking ACK. Bounded so a dead consumer
     * degrades to a visible drop, not a wedged USB task — the mutex take is
     * bounded too and the window uses wrap-safe tick arithmetic, so the whole
     * callback is hard-limited to ~500 ms (Qodo #587). */
    const TickType_t startTick = xTaskGetTickCount();
    const TickType_t windowTicks = pdMS_TO_TICKS(500);
    do {
        if (OSAL_RESULT_TRUE == OSAL_MUTEX_Lock(&gUsartReadMutex, 10)) {
            size_t freeSpace = WIFI_SERIAL_BRIDGE_INTERFACE_USART_RECEIVE_BUFFER_SIZE - gUsartReceiveLength;
            if (freeSpace >= buffLen) {
                for (size_t i = 0; i < buffLen; i++) {
                    gUsartReceiveBuffer[gUsartReceiveInOffset] = pBuff[i];
                    gUsartReceiveInOffset++;
                    gUsartReceiveLength++;
                    if (WIFI_SERIAL_BRIDGE_INTERFACE_USART_RECEIVE_BUFFER_SIZE == gUsartReceiveInOffset) {
                        gUsartReceiveInOffset = 0;
                    }
                }
                OSAL_MUTEX_Unlock(&gUsartReadMutex);
                return true;
            }
            OSAL_MUTEX_Unlock(&gUsartReadMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    } while ((xTaskGetTickCount() - startTick) < windowTicks);

    LOG_E_ONCE(LOG_ONCE_BRIDGE_RX_OVERFLOW,
               "Serial bridge RX ring overflow; bytes dropped");
    return true;
}

void wifi_serial_bridge_interface_Init(void) {
    gUsartReceiveInOffset = 0;
    gUsartReceiveOutOffset = 0;
    gUsartReceiveLength = 0;
    UsbCdc_SetTransparentMode(true);
    if (gUsartReadMutex == NULL)
        OSAL_MUTEX_Create(&gUsartReadMutex);
}

void wifi_serial_bridge_interface_DeInit(void) {
    UsbCdc_SetTransparentMode(false);
    //OSAL_MUTEX_Delete(&gUsartReadMutex);
    //gUsartReadMutex = NULL;
}

size_t wifi_serial_bridge_interface_UARTReadGetCount(void) {
    size_t count = 0;
    if(gUsartReadMutex==NULL)
        return 0;
    if (OSAL_RESULT_TRUE == OSAL_MUTEX_Lock(&gUsartReadMutex, OSAL_WAIT_FOREVER)) {
        count = gUsartReceiveLength;
        OSAL_MUTEX_Unlock(&gUsartReadMutex);
    }
    return count;
}

/* Declared noinline to prevent GCC -O3 from inlining it into the 1-byte
   GetByte wrapper and falsely warning about memcpy overflow. The runtime
   path is safe: numBytes=1 bounds partialReadNum to at most 1. Other
   callers still benefit from interprocedural optimization. */
size_t __attribute__((noinline)) wifi_serial_bridge_interface_UARTReadGetBuffer(void *pBuf, size_t numBytes);

uint8_t wifiSerialBridgeIntf_UARTReadGetByte(void) {
    uint8_t byte = 0;

    if (0 == wifi_serial_bridge_interface_UARTReadGetBuffer(&byte, 1)) {
        return 0;
    }

    return byte;
}

size_t __attribute__((noinline)) wifi_serial_bridge_interface_UARTReadGetBuffer(void *pBuf, size_t numBytes) {
    if (gUsartReadMutex == NULL) {
        return 0;
    }

    // Lock mutex for entire read operation to prevent race with write
    if (OSAL_RESULT_FALSE == OSAL_MUTEX_Lock(&gUsartReadMutex, OSAL_WAIT_FOREVER)) {
        return 0;
    }

    size_t count = gUsartReceiveLength;

    if (0 == count) {
        OSAL_MUTEX_Unlock(&gUsartReadMutex);
        return 0;
    }

    if (numBytes > count) {
        numBytes = count;
    }

    if ((gUsartReceiveOutOffset + numBytes) > WIFI_SERIAL_BRIDGE_INTERFACE_USART_RECEIVE_BUFFER_SIZE) {
        uint8_t *pByteBuf;
        size_t partialReadNum;

        pByteBuf = pBuf;
        partialReadNum = (WIFI_SERIAL_BRIDGE_INTERFACE_USART_RECEIVE_BUFFER_SIZE - gUsartReceiveOutOffset);

        memcpy(pByteBuf, &gUsartReceiveBuffer[gUsartReceiveOutOffset], partialReadNum);

        pByteBuf += partialReadNum;
        numBytes -= partialReadNum;

        memcpy(pByteBuf, gUsartReceiveBuffer, numBytes);

        gUsartReceiveOutOffset = numBytes;

        numBytes += partialReadNum;
    } else {
        memcpy(pBuf, &gUsartReceiveBuffer[gUsartReceiveOutOffset], numBytes);

        gUsartReceiveOutOffset += numBytes;
    }

    gUsartReceiveLength -= numBytes;
    OSAL_MUTEX_Unlock(&gUsartReadMutex);

    return numBytes;
}

bool wifi_serial_bridge_interface_UARTWritePutByte(uint8_t b) {
    return wifi_serial_bridge_interface_UARTWritePutBuffer((void*) &b, 1);
}

bool wifi_serial_bridge_interface_UARTWritePutBuffer(const void *pBuf, size_t numBytes) {
    if ((NULL == pBuf) || (0 == numBytes)) {
        return false;
    }
    if (UsbCdc_WriteToBuffer(NULL, pBuf, numBytes) == 0) {
        return false;
    }
    return true;
}
