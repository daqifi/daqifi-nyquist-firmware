
#ifndef _WIFI_SERIAL_BRIDGE_H
#define _WIFI_SERIAL_BRIDGE_H

#include <stdint.h>

#ifdef __cplusplus  // Provide C++ Compatibility
extern "C" {
#endif

#define WIFI_SERIAL_BRIDGE_CMD_BUFFER_SIZE  2048

    typedef enum {
        WIFI_SERIAL_BRIDGE_STATE_UNKNOWN,
        WIFI_SERIAL_BRIDGE_STATE_WAIT_OP_CODE,
        WIFI_SERIAL_BRIDGE_STATE_WAIT_HEADER,
        WIFI_SERIAL_BRIDGE_STATE_PROCESS_COMMAND,
        WIFI_SERIAL_BRIDGE_STATE_WAIT_PAYLOAD,
    } wifi_serial_bridge_State_t;

    typedef struct {
        wifi_serial_bridge_State_t state;
        uint32_t baudRate;
        uint8_t dataBuf[WIFI_SERIAL_BRIDGE_CMD_BUFFER_SIZE];
        uint16_t rxDataLen;
        uint8_t cmdType;
        uint16_t cmdSize;
        uint32_t cmdAddr;
        uint32_t cmdVal;
        uint16_t payloadLength;
    } wifi_serial_bridge_context_t;
    /**
     * @brief Initializes the WiFi Serial Bridge.
     * 
     * Sets up the initial state of the bridge, enabling the UART interface
     * and setting the context to wait for the next operation code.
     * 
     * @param[in,out] context Pointer to the WiFi Serial Bridge context structure.
     */
    void wifi_serial_bridge_Init(wifi_serial_bridge_context_t * const pContext);
    
    /**
     * @brief Processes data within the WiFi Serial Bridge.
     * 
     * Based on the current state, this function manages incoming commands, validates
     * headers, and executes actions (such as reading/writing registers or handling payloads).
     * 
     * @param[in,out] context Pointer to the WiFi Serial Bridge context structure.
     */
    void wifi_serial_bridge_Process(wifi_serial_bridge_context_t * const pContext);
    
    /**
     * @brief Deinitializes the WiFi Serial Bridge.
     * 
     * Cleans up resources and disables the UART interface used by the WiFi Serial Bridge.
     * 
     * @param[in,out] context Pointer to the WiFi Serial Bridge context structure.
     */
    void wifi_serial_bridge_Deinit(wifi_serial_bridge_context_t * const pContext);

#ifdef __cplusplus
}
#endif

#endif /* _WIFI_SERIAL_BRIDGE_H */
