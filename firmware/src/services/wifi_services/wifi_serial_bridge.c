#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "wifi_serial_bridge_interface.h"
#include "wifi_serial_bridge.h"
#include "nmdrv.h"
#include "nmbus.h"
#include "m2m_wifi.h"

#define WIFI_SERIAL_BRIDGE_COMMAND_HEADER_SIZE      12

typedef enum {
    WIFI_SERIAL_BRIDGE_COMMAND_READ_REG_WITH_RET = 0,
    WIFI_SERIAL_BRIDGE_COMMAND_WRITE_REG = 1,
    WIFI_SERIAL_BRIDGE_COMMAND_READ_BLOCK = 2,
    WIFI_SERIAL_BRIDGE_COMMAND_WRITE_BLOCK = 3,
    WIFI_SERIAL_BRIDGE_COMMAND_RECONFIGURE = 5
} wifi_serial_bridge_command_t;

typedef enum {
    WIFI_SERIAL_BRIDGE_RESPONSE_NACK = 0x5a,
    WIFI_SERIAL_BRIDGE_RESPONSE_ID_VAR_BR = 0x5b,
    WIFI_SERIAL_BRIDGE_RESPONSE_ID_FIXED_BR = 0x5c,
    WIFI_SERIAL_BRIDGE_RESPONSE_ACK = 0xac
} wifi_serial_bridge_response_t;

static bool ProcessHeader(wifi_serial_bridge_context_t * const pContext, uint8_t *pHeader) {
    uint8_t checksum;
    uint8_t i;

    if ((NULL == pContext) || (NULL == pHeader)) {
        return false;
    }

    checksum = 0;

    for (i = 0; i < WIFI_SERIAL_BRIDGE_COMMAND_HEADER_SIZE; i++) {
        checksum ^= pHeader[i];
    }

    if (0 != checksum) {
        return false;
    }

    pContext->cmdType = pHeader[0];
    pContext->cmdSize = (((uint16_t) pHeader[3]) << 8) | pHeader[2];
    pContext->cmdAddr = (((uint32_t) pHeader[7]) << 24) | (((uint32_t) pHeader[6]) << 16) | (((uint32_t) pHeader[5]) << 8) | pHeader[4];
    pContext->cmdVal = (((uint32_t) pHeader[11]) << 24) | (((uint32_t) pHeader[10]) << 16) | (((uint32_t) pHeader[9]) << 8) | pHeader[8];

    if (pContext->cmdType == WIFI_SERIAL_BRIDGE_COMMAND_WRITE_BLOCK) {
        pContext->payloadLength = pContext->cmdSize;

        if (pContext->payloadLength > WIFI_SERIAL_BRIDGE_CMD_BUFFER_SIZE) {
            return false;
        }
    } else {
        pContext->payloadLength = 0;
    }

    return true;
}

static bool ProcessCommand(wifi_serial_bridge_context_t * const pContext) {
    uint_fast16_t cnt;

    if (NULL == pContext) {
        return false;
    }

    switch (pContext->cmdType) {
        case WIFI_SERIAL_BRIDGE_COMMAND_READ_REG_WITH_RET:
        {
            uint32_t regVal;

            regVal = nm_read_reg(pContext->cmdAddr);

            pContext->dataBuf[0] = (regVal >> 24) & 0xff;
            pContext->dataBuf[1] = (regVal >> 16) & 0xff;
            pContext->dataBuf[2] = (regVal >> 8) & 0xff;
            pContext->dataBuf[3] = (regVal) & 0xff;

            if (false == wifi_serial_bridge_interface_UARTWritePutBuffer(pContext->dataBuf, 4)) {
                return false;
            }

            return true;
        }

        case WIFI_SERIAL_BRIDGE_COMMAND_WRITE_REG:
        {
            nm_write_reg(pContext->cmdAddr, pContext->cmdVal);

            return true;
        }

        case WIFI_SERIAL_BRIDGE_COMMAND_READ_BLOCK:
        {
            cnt = pContext->cmdSize;

            while (cnt >= WIFI_SERIAL_BRIDGE_CMD_BUFFER_SIZE) {
                if (M2M_SUCCESS != nm_read_block(pContext->cmdAddr, pContext->dataBuf, WIFI_SERIAL_BRIDGE_CMD_BUFFER_SIZE))
                    return false;

                if (false == wifi_serial_bridge_interface_UARTWritePutBuffer(pContext->dataBuf, WIFI_SERIAL_BRIDGE_CMD_BUFFER_SIZE))
                    return false;
            }

            if (cnt) {
                if (M2M_SUCCESS != nm_read_block(pContext->cmdAddr, pContext->dataBuf, cnt))
                    return false;

                if (false == wifi_serial_bridge_interface_UARTWritePutBuffer(pContext->dataBuf, pContext->cmdSize))
                    return false;
            }

            return true;
        }

        case WIFI_SERIAL_BRIDGE_COMMAND_WRITE_BLOCK:
        {
            if (M2M_SUCCESS == nm_write_block(pContext->cmdAddr, pContext->dataBuf, pContext->cmdSize)) {
                pContext->dataBuf[0] = WIFI_SERIAL_BRIDGE_RESPONSE_ACK;
            } else {
                pContext->dataBuf[0] = WIFI_SERIAL_BRIDGE_RESPONSE_NACK;
            }

            return wifi_serial_bridge_interface_UARTWritePutBuffer(pContext->dataBuf, 1);
        }

        case WIFI_SERIAL_BRIDGE_COMMAND_RECONFIGURE:
        {
            pContext->baudRate = pContext->cmdVal;
            return true;
        }

        default:
        {
        }
    }

    return true;
}

void wifi_serial_bridge_Init(wifi_serial_bridge_context_t * const pContext) {
    if (NULL == pContext) {
        return;
    }

    wifi_serial_bridge_interface_Init();
    pContext->state = WIFI_SERIAL_BRIDGE_STATE_WAIT_OP_CODE;
    m2m_wifi_download_mode();
}

void wifi_serial_bridge_Deinit(wifi_serial_bridge_context_t * const pContext) {
    if (NULL == pContext) {
        return;
    }
    wifi_serial_bridge_interface_DeInit();
}

void wifi_serial_bridge_Process(wifi_serial_bridge_context_t * const pContext) {
    if (NULL == pContext) {
        return;
    }

    switch (pContext->state) {
        case WIFI_SERIAL_BRIDGE_STATE_WAIT_OP_CODE:
        {
            uint8_t opCode;

            if (0 == wifi_serial_bridge_interface_UARTReadGetBuffer(&opCode, 1)) {
                break;
            }

            switch (opCode) {
                case 0x12:
                {
                    wifi_serial_bridge_interface_UARTWritePutByte(WIFI_SERIAL_BRIDGE_RESPONSE_ID_VAR_BR);
                    break;
                }

                case 0x13:
                {
                    break;
                }

                case 0xa5:
                {
                    pContext->state = WIFI_SERIAL_BRIDGE_STATE_WAIT_HEADER;
                    pContext->rxDataLen = 0;
                    break;
                }

                default:
                {
                    break;
                }
            }

            break;
        }

        case WIFI_SERIAL_BRIDGE_STATE_WAIT_HEADER:
        {
            pContext->rxDataLen += wifi_serial_bridge_interface_UARTReadGetBuffer(&pContext->dataBuf[pContext->rxDataLen], WIFI_SERIAL_BRIDGE_COMMAND_HEADER_SIZE - pContext->rxDataLen);

            if (WIFI_SERIAL_BRIDGE_COMMAND_HEADER_SIZE != pContext->rxDataLen) {
                break;
            }

            if (true == ProcessHeader(pContext, pContext->dataBuf)) {
                wifi_serial_bridge_interface_UARTWritePutByte(WIFI_SERIAL_BRIDGE_RESPONSE_ACK);

                if (pContext->payloadLength > 0) {
                    pContext->state = WIFI_SERIAL_BRIDGE_STATE_WAIT_PAYLOAD;
                    pContext->rxDataLen = 0;
                } else {
                    pContext->state = WIFI_SERIAL_BRIDGE_STATE_PROCESS_COMMAND;
                }
            } else {
                pContext->state = WIFI_SERIAL_BRIDGE_STATE_WAIT_OP_CODE;

                wifi_serial_bridge_interface_UARTWritePutByte(WIFI_SERIAL_BRIDGE_RESPONSE_NACK);
            }

            break;
        }

        case WIFI_SERIAL_BRIDGE_STATE_PROCESS_COMMAND:
        {
            ProcessCommand(pContext);

            pContext->state = WIFI_SERIAL_BRIDGE_STATE_WAIT_OP_CODE;
            break;
        }

        case WIFI_SERIAL_BRIDGE_STATE_WAIT_PAYLOAD:
        {
            pContext->rxDataLen += wifi_serial_bridge_interface_UARTReadGetBuffer(&pContext->dataBuf[pContext->rxDataLen], pContext->payloadLength - pContext->rxDataLen);

            if (pContext->payloadLength == pContext->rxDataLen) {
                ProcessCommand(pContext);

                pContext->state = WIFI_SERIAL_BRIDGE_STATE_WAIT_OP_CODE;
            }
            break;
        }

        default:
        {
            break;
        }
    }

    return;
}
