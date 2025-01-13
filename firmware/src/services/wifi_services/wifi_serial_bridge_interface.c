#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "wifi_serial_bridge_interface.h"
#include "definitions.h"
#include "osal/osal.h"
#include "../UsbCdc/UsbCdc.h"

#define WIFI_SERIAL_BRIDGE_INTERFACE_USART_RECEIVE_BUFFER_SIZE   512



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
    if (OSAL_RESULT_TRUE == OSAL_MUTEX_Lock(&gUsartReadMutex, OSAL_WAIT_FOREVER)) {
        for (size_t i = 0; i < buffLen; i++) {
            gUsartReceiveBuffer[gUsartReceiveInOffset] = pBuff[i];
            gUsartReceiveInOffset++;
            gUsartReceiveLength++;
            if (WIFI_SERIAL_BRIDGE_INTERFACE_USART_RECEIVE_BUFFER_SIZE == gUsartReceiveInOffset) {
                gUsartReceiveInOffset = 0;
            }
        }
        OSAL_MUTEX_Unlock(&gUsartReadMutex);
    }
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

uint8_t wifiSerialBridgeIntf_UARTReadGetByte(void) {
    uint8_t byte = 0;

    if (0 == wifi_serial_bridge_interface_UARTReadGetBuffer(&byte, 1)) {
        return 0;
    }

    return byte;
}

size_t wifi_serial_bridge_interface_UARTReadGetBuffer(void *pBuf, size_t numBytes) {
    size_t count = wifi_serial_bridge_interface_UARTReadGetCount();

    if (0 == count) {
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
    if (OSAL_RESULT_TRUE == OSAL_MUTEX_Lock(&gUsartReadMutex, OSAL_WAIT_FOREVER)) {
        gUsartReceiveLength -= numBytes;
        OSAL_MUTEX_Unlock(&gUsartReadMutex);
    }

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
