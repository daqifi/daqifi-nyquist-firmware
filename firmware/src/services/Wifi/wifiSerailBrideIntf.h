#ifndef _WIFI_SERIAL_BRIDGE_INTF_H
#define _WIFI_SERIAL_BRIDGE_INTF_H

#ifdef __cplusplus  // Provide C++ Compatibility
extern "C" {
#endif

void wifiSerialBridgeIntf_Init(void);
void wifiSerialBridgeIntf_DeInit(void);
size_t wifiSerialBridgeIntf_UARTReadGetCount(void);
uint8_t wifiSerialBridgeIntf_UARTReadGetByte(void);
size_t wifiSerialBridgeIntf_UARTReadGetBuffer(void *pBuf, size_t numBytes);
bool wifiSerialBridgeIntf_UARTWritePutByte(uint8_t b);
bool wifiSerialBridgeIntf_UARTWritePutBuffer(const void *pBuf, size_t numBytes);


#ifdef __cplusplus
}
#endif

#endif /* _PLATFORM_H */
