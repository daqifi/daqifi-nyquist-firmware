#ifndef _WIFI_MANAGER_H
#define	_WIFI_MANAGER_H

#include <stdlib.h>
#include <string.h>

// Harmony
#include "configuration.h"
#include "definitions.h"
#include "services/daqifi_settings.h"
#include "wifi_tcp_server.h"
#ifdef	__cplusplus
extern "C" {
#endif

    /**
     * @brief Initializes the WiFi manager and its state machine.
     * 
     * Sets up the event queue, initializes WiFi settings, and prepares the system to handle WiFi events.
     * 
     * @param[in] pSettings Pointer to the WifiSettings structure containing network configuration.
     * 
     * @return True if initialization is successful, false otherwise.
     */
    bool wifi_manager_Init(WifiSettings* settings);

    /**
     * @brief Deinitializes the WiFi manager.
     * 
     * Disables WiFi operations and cleans up resources. Ensures the board is powered on before deinitializing.
     * 
     * @return True if deinitialization is successful, false otherwise.
     */
    bool wifi_manager_Deinit();

    /**
     * @brief Updates the WiFi network settings.
     * 
     * Changes the current WiFi configuration and reinitializes the WiFi manager with the updated settings.
     * 
     * @param[in] pSettings Pointer to the updated WifiSettings structure.
     * 
     * @return True if the settings update is successful, false otherwise.
     */
    bool wifi_manager_UpdateNetworkSettings(WifiSettings* pSettings);


    /**
     * @brief Processes the current state of the WiFi manager.
     * 
     * Handles queued events and processes the WiFi state machine, including TCP and UDP operations, 
     * and serial bridge communication if OTA mode is active.
     */
    void wifi_manager_ProcessState();

    /**
     * @brief Gets the available free size in the write buffer.
     * 
     * Checks the buffer size in the TCP server for free space available to write.
     * 
     * @return The number of bytes available in the write buffer.
     */
    size_t wifi_manager_GetWriteBuffFreeSize();

    /**
     * @brief Writes data to the TCP server buffer.
     * 
     * Adds data to the TCP server buffer for transmission to connected clients.
     * 
     * @param[in] pData Pointer to the data to write.
     * @param[in] len Length of the data to write.
     * 
     * @return The number of bytes successfully written.
     */
    size_t wifi_manager_WriteToBuffer(const char* data, size_t len);
    /**
     * @brief Retrieves the TCP server Context.
     * 
     * Provides access to the TCP server Context structure for further configuration or inspection.
     * 
     * @return Pointer to the TcpServerContext structure.
     */
    wifi_tcp_server_context_t* wifi_manager_GetTcpServerContext();

    /**
     * @brief Formats a UDP announcement packet with the provided WiFi settings.
     * 
     * This callback function prepares a UDP packet containing relevant network information, such as IP address,
     * device name, or other WiFi settings, for broadcasting or responding to discovery requests on the network.
     * The packet is written to the provided buffer, and its length is set through the packet length parameter.
     * 
     * @param[in] pWifiSettings Pointer to the WifiSettings structure holding the current network configuration.
     * @param[out] pBuffer Pointer to the buffer where the UDP announcement packet will be formatted.
     * @param[in,out] pPacketLen Pointer to a variable holding the length of the buffer. On return, this variable
     *                           will contain the length of the formatted announcement packet.
     */
    void wifi_manager_FormUdpAnnouncePacketCB(const WifiSettings *pWifiSettings, uint8_t *pBuffer, uint16_t *pPacketLen);


#ifdef	__cplusplus
}
#endif

#endif	/* _WIFI_STATE_MACHINE_H */

