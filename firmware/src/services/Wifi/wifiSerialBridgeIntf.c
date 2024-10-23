/**
 *
 * Copyright (c) 2019 Microchip Technology Inc. and its subsidiaries.
 *
 * Subject to your compliance with these terms, you may use Microchip
 * software and any derivatives exclusively with Microchip products.
 * It is your responsibility to comply with third party license terms applicable
 * to your use of third party software (including open source software) that
 * may accompany Microchip software.
 *
 * THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES,
 * WHETHER EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE,
 * INCLUDING ANY IMPLIED WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY,
 * AND FITNESS FOR A PARTICULAR PURPOSE. IN NO EVENT WILL MICROCHIP BE
 * LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE, INCIDENTAL OR CONSEQUENTIAL
 * LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND WHATSOEVER RELATED TO THE
 * SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS BEEN ADVISED OF THE
 * POSSIBILITY OR THE DAMAGES ARE FORESEEABLE.  TO THE FULLEST EXTENT
 * ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN ANY WAY
 * RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
 * THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
 *
 */
/*
 * Support and FAQ: visit <a href="https://www.microchip.com/support/">Microchip Support</a>
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "wifiSerailBrideIntf.h"
#include "definitions.h"
#include "osal/osal.h"
#include "../UsbCdc/UsbCdc.h"

#define USART_RECEIVE_BUFFER_SIZE   512



static OSAL_MUTEX_HANDLE_TYPE gUsartReadMutex;
static uint8_t gUsartReceiveBuffer[USART_RECEIVE_BUFFER_SIZE];
static size_t gUsartReceiveInOffset;
static size_t gUsartReceiveOutOffset;
static volatile size_t gUsartReceiveLength;


bool UsbCdc_TransparentReadCmpltCB(uint8_t* pBuff, size_t buffLen) {
    if(buffLen==0)
        return true;
    if (OSAL_RESULT_TRUE == OSAL_MUTEX_Lock(&gUsartReadMutex, OSAL_WAIT_FOREVER)) {
        for (size_t i = 0; i < buffLen; i++) {            
            gUsartReceiveBuffer[gUsartReceiveInOffset] = pBuff[i];
            gUsartReceiveInOffset++;
            gUsartReceiveLength++;
            if (USART_RECEIVE_BUFFER_SIZE == gUsartReceiveInOffset) {
                gUsartReceiveInOffset = 0;
            }           
        }
        OSAL_MUTEX_Unlock(&gUsartReadMutex);
    }
    return true;
}

void wifiSerialBridgeIntf_Init(void) {
    gUsartReceiveInOffset = 0;
    gUsartReceiveOutOffset = 0;
    gUsartReceiveLength = 0;
    UsbCdc_SetTransparentMode(true);
    OSAL_MUTEX_Create(&gUsartReadMutex);   
}
void wifiSerialBridgeIntf_DeInit(void) {    
    UsbCdc_SetTransparentMode(false);
    OSAL_MUTEX_Delete(&gUsartReadMutex);   
}

size_t wifiSerialBridgeIntf_UARTReadGetCount(void) {
    size_t count;
    if (OSAL_RESULT_TRUE == OSAL_MUTEX_Lock(&gUsartReadMutex, OSAL_WAIT_FOREVER)) {
        count = gUsartReceiveLength;
        OSAL_MUTEX_Unlock(&gUsartReadMutex);
    }
    return count;
}

uint8_t wifiSerialBridgeIntf_UARTReadGetByte(void) {
    uint8_t byte = 0;

    if (0 == wifiSerialBridgeIntf_UARTReadGetBuffer(&byte, 1)) {
        return 0;
    }

    return byte;
}

size_t wifiSerialBridgeIntf_UARTReadGetBuffer(void *pBuf, size_t numBytes) {
    size_t count = wifiSerialBridgeIntf_UARTReadGetCount();

    if (0 == count) {
        return 0;
    }

    if (numBytes > count) {
        numBytes = count;
    }

    if ((gUsartReceiveOutOffset + numBytes) > USART_RECEIVE_BUFFER_SIZE) {
        uint8_t *pByteBuf;
        size_t partialReadNum;

        pByteBuf = pBuf;
        partialReadNum = (USART_RECEIVE_BUFFER_SIZE - gUsartReceiveOutOffset);

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

bool wifiSerialBridgeIntf_UARTWritePutByte(uint8_t b) {
    return wifiSerialBridgeIntf_UARTWritePutBuffer((void*) &b, 1);
}

bool wifiSerialBridgeIntf_UARTWritePutBuffer(const void *pBuf, size_t numBytes) {
    if ((NULL == pBuf) || (0 == numBytes)) {
        return false;
    }
    if (UsbCdc_WriteToBuffer(NULL, pBuf, numBytes) == 0) {
        return false;
    }
    return true;
}
