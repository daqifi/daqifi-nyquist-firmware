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
#include "platform.h"
#include "definitions.h"
#include "osal/osal.h"
#include "../UsbCdc/UsbCdc.h"

#define USART_RECEIVE_BUFFER_SIZE   USBCDC_WBUFFER_SIZE
//#define USART_TRANSMIT_BUFFER_SIZE  USBCDC_RBUFFER_SIZE


static OSAL_MUTEX_HANDLE_TYPE gUsartReadMutex;

static uint8_t usartReceiveBuffer[USART_RECEIVE_BUFFER_SIZE];
static size_t usartReceiveInOffset;
static size_t usartReceiveOutOffset;
static volatile size_t usartReceiveLength;

//static char usartTransmitBuffer[USART_TRANSMIT_BUFFER_SIZE];
//static size_t usartTransmitInOffset;
//static size_t usartTransmitOutOffset;
//static volatile size_t usartTransmitLength;

bool UsbCdc_ReadCmpltCB(uint8_t* pBuff, size_t buffLen) {
    if(buffLen==0)
        return true;
    if (OSAL_RESULT_TRUE == OSAL_MUTEX_Lock(&gUsartReadMutex, OSAL_WAIT_FOREVER)) {
        for (size_t i = 0; i < buffLen; i++) {            
            usartReceiveBuffer[usartReceiveInOffset] = pBuff[i];
            usartReceiveInOffset++;
            usartReceiveLength++;
            if (USART_RECEIVE_BUFFER_SIZE == usartReceiveInOffset) {
                usartReceiveInOffset = 0;
            }           
        }
        OSAL_MUTEX_Unlock(&gUsartReadMutex);
    }
    return true;
}

void SerialBridge_PlatformInit(void) {
    usartReceiveInOffset = 0;
    usartReceiveOutOffset = 0;
    usartReceiveLength = 0;
    OSAL_MUTEX_Create(&gUsartReadMutex);   
}

size_t SerialBridge_PlatformUARTReadGetCount(void) {
    size_t count;
    if (OSAL_RESULT_TRUE == OSAL_MUTEX_Lock(&gUsartReadMutex, OSAL_WAIT_FOREVER)) {
        count = usartReceiveLength;
        OSAL_MUTEX_Unlock(&gUsartReadMutex);
    }
    return count;
}

uint8_t SerialBridge_PlatformUARTReadGetByte(void) {
    uint8_t byte = 0;

    if (0 == SerialBridge_PlatformUARTReadGetBuffer(&byte, 1)) {
        return 0;
    }

    return byte;
}

size_t SerialBridge_PlatformUARTReadGetBuffer(void *pBuf, size_t numBytes) {
    size_t count = SerialBridge_PlatformUARTReadGetCount();

    if (0 == count) {
        return 0;
    }

    if (numBytes > count) {
        numBytes = count;
    }

    if ((usartReceiveOutOffset + numBytes) > USART_RECEIVE_BUFFER_SIZE) {
        uint8_t *pByteBuf;
        size_t partialReadNum;

        pByteBuf = pBuf;
        partialReadNum = (USART_RECEIVE_BUFFER_SIZE - usartReceiveOutOffset);

        memcpy(pByteBuf, &usartReceiveBuffer[usartReceiveOutOffset], partialReadNum);

        pByteBuf += partialReadNum;
        numBytes -= partialReadNum;

        memcpy(pByteBuf, usartReceiveBuffer, numBytes);

        usartReceiveOutOffset = numBytes;

        numBytes += partialReadNum;
    } else {
        memcpy(pBuf, &usartReceiveBuffer[usartReceiveOutOffset], numBytes);

        usartReceiveOutOffset += numBytes;
    }
    if (OSAL_RESULT_TRUE == OSAL_MUTEX_Lock(&gUsartReadMutex, OSAL_WAIT_FOREVER)) {
        usartReceiveLength -= numBytes;
        OSAL_MUTEX_Unlock(&gUsartReadMutex);
    }

    return numBytes;
}

bool SerialBridge_PlatformUARTWritePutByte(uint8_t b) {
    return SerialBridge_PlatformUARTWritePutBuffer((void*) &b, 1);
}

bool SerialBridge_PlatformUARTWritePutBuffer(const void *pBuf, size_t numBytes) {
    if ((NULL == pBuf) || (0 == numBytes)) {
        return false;
    }
    if (UsbCdc_WriteToBuffer(NULL, pBuf, numBytes) == 0) {
        return false;
    }
    return true;
}
