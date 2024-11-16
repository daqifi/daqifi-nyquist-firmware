#ifndef _WIFI_SERIAL_BRIDGE_INTERFACE_H
#define _WIFI_SERIAL_BRIDGE_INTERFACE_H

#ifdef __cplusplus  // Provide C++ Compatibility
extern "C" {
#endif

/**
 * @brief Initializes the WiFi Serial Bridge interface.
 * 
 * Sets up the UART receive buffer and enables transparent mode on the USB CDC interface.
 * Also creates a mutex for synchronized buffer access in multithreaded environments.
 */
void wifi_serial_bridge_interface_Init(void);
/**
 * @brief Deinitializes the WiFi Serial Bridge interface.
 * 
 * Disables transparent mode on the USB CDC interface and deletes the mutex used for
 * buffer synchronization.
 */
void wifi_serial_bridge_interface_DeInit(void);
/**
 * @brief Gets the current number of bytes available in the UART receive buffer.
 * 
 * Provides a thread-safe way to check the number of unread bytes in the buffer.
 * 
 * @return The count of unread bytes in the UART receive buffer.
 */
size_t wifi_serial_bridge_interface_UARTReadGetCount(void);
/**
 * @brief Reads a single byte from the UART receive buffer.
 * 
 * Uses the buffer read function to retrieve one byte from the buffer. If no data
 * is available, it returns zero.
 * 
 * @return The byte read from the buffer, or zero if no data is available.
 */
uint8_t wifi_serial_bridge_interface_UARTReadGetByte(void);
/**
 * @brief Reads data from the UART receive buffer.
 * 
 * Reads up to the specified number of bytes from the receive buffer into the provided
 * buffer. This function handles buffer wrap-around if the requested data exceeds the buffer?s end.
 * 
 * @param[out] pBuf Pointer to the buffer where the read data will be stored.
 * @param[in] numBytes Number of bytes to read from the receive buffer.
 * 
 * @return The actual number of bytes read from the UART receive buffer.
 */
size_t wifi_serial_bridge_interface_UARTReadGetBuffer(void *pBuf, size_t numBytes);
/**
 * @brief Writes a single byte to the UART transmit buffer.
 * 
 * Sends one byte to the UART transmit buffer through the USB CDC interface.
 * 
 * @param[in] b Byte to write to the UART transmit buffer.
 * 
 * @return True if the byte was successfully written, otherwise false.
 */
bool wifi_serial_bridge_interface_UARTWritePutByte(uint8_t b);
/**
 * @brief Writes data to the UART transmit buffer.
 * 
 * Writes the specified number of bytes from the provided buffer to the UART transmit
 * buffer through the USB CDC interface.
 * 
 * @param[in] pBuf Pointer to the data to be written to the UART transmit buffer.
 * @param[in] numBytes Number of bytes to write.
 * 
 * @return True if all bytes were successfully written, otherwise false.
 */
bool wifi_serial_bridge_interface_UARTWritePutBuffer(const void *pBuf, size_t numBytes);

void wifi_serial_bridge_interface_DataReceivedCB();
#ifdef __cplusplus
}
#endif

#endif /* _WIFI_SERIAL_BRIDGE_INTERFACE_H */
