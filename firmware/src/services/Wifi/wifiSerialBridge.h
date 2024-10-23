
#ifndef _WIFI_SERIAL_BRIDGE_H
#define _WIFI_SERIAL_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus  // Provide C++ Compatibility
extern "C" {
#endif

#define WIFI_SERIAL_BRIDGE_CMD_BUFFER_SIZE  2048

typedef enum
{
    WIFI_SERIAL_BRIDGE_STATE_UNKNOWN,
    WIFI_SERIAL_BRIDGE_STATE_WAIT_OP_CODE,
    WIFI_SERIAL_BRIDGE_STATE_WAIT_HEADER,
    WIFI_SERIAL_BRIDGE_STATE_PROCESS_COMMAND,
    WIFI_SERIAL_BRIDGE_STATE_WAIT_PAYLOAD,
} WIFI_SERIAL_BRIDGE_STATE;

typedef struct
{
    WIFI_SERIAL_BRIDGE_STATE state;
    uint32_t            baudRate;
    uint8_t             dataBuf[WIFI_SERIAL_BRIDGE_CMD_BUFFER_SIZE];
    uint16_t            rxDataLen;
    uint8_t             cmdType;
    uint16_t            cmdSize;
    uint32_t            cmdAddr;
    uint32_t            cmdVal;
    uint16_t            payloadLength;
} WIFI_SERIAL_BRIDGE_DECODER_STATE;

void wifiSerialBridge_Init(WIFI_SERIAL_BRIDGE_DECODER_STATE *const pSBDecoderState);
void wifiSerialBridge_Process(WIFI_SERIAL_BRIDGE_DECODER_STATE *const pSBDecoderState);
void wifiSerialBridge_DeInit(WIFI_SERIAL_BRIDGE_DECODER_STATE *const pSBDecoderState);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_SERIAL_BRIDGE_H */
