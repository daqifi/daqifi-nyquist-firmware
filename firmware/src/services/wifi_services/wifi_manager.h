#ifndef _WIFI_MANAGER_H
#define	_WIFI_MANAGER_H

#include <stdlib.h>
#include <string.h>

// Harmony
#include "configuration.h"
#include "definitions.h"
#include "wifi_tcp_server.h"
#ifdef	__cplusplus
extern "C" {
#endif
#define WIFI_MANAGER_DNS_CLIENT_MAX_HOSTNAME_LEN HOSTNAME_MAX_SIZE
#define WIFI_MANAGER_CHIP_INFO_FW_VERSION_MAX_SIZE 20
#define WIFI_MANAGER_CHIP_INFO_FW_BUILD_DATE

    typedef enum {
        WIFI_MANAGER_NETWORK_MODE_STA = 1,
        WIFI_MANAGER_NETWORK_MODE_AP = 4
    } wifi_manager_networkMode_t;

    typedef enum {
        // DAQiFi defines 0 = OPEN
        WIFI_MANAGER_SECURITY_MODE_OPEN,
        // DAQiFi defines 1 = WEP_40 which is now deprecated
        WIFI_MANAGER_SECURITY_MODE_WEP_40,
        // DAQiFi defines 2 = WEP_104 which is now deprecated
        WIFI_MANAGER_SECURITY_MODE_WEP_104,
        // DAQiFi defines 3 = WPA_AUTO_WITH_PASS_PHRASE
        WIFI_MANAGER_SECURITY_MODE_WPA_AUTO_WITH_PASS_PHRASE,
        // DAQiFi defines 4 = WIFI_API_SEC_WPA_DEPRECATED - keeping for backwards compatibility
        WIFI_MANAGER_SECURITY_MODE_WPA_DEPRECATED,
        // DAQiFi defines 5 = 802_1X
        WIFI_MANAGER_SECURITY_MODE_802_1X,
        // DAQiFi defines 6 = SECURITY_WPS_PUSH_BUTTON which is now deprecated
        WIFI_MANAGER_SECURITY_MODE_WPS_PUSH_BUTTON,
        // DAQiFi defines 7 = SECURITY_WPS_PIN which is now deprecated
        WIFI_MANAGER_SECURITY_MODE_SEC_WPS_PIN,
        // DAQiFi defines 8 = WDRV_WINC_AUTH_TYPE_802_1X_MSCHAPV2
        WIFI_MANAGER_SECURITY_MODE_SEC_802_1X_MSCHAPV2,
        // DAQiFi defines 9 = WDRV_WINC_AUTH_TYPE_802_1X_TLS
        WIFI_MANAGER_SECURITY_MODE_SEC_802_1X_TLS
    } wifi_manager_securityMode_t;

    typedef union {
        uint32_t Val;
        uint16_t w[2];
        uint8_t v[4];
    } wifi_manager_ipv4Addr_t;

    /**
     * Stores the wifi settings
     */
    typedef struct sWifiSettings {
        /**
         * Toggles the power mode
         */
        bool isEnabled;
        /**
         * this is true is ota mode is enabled
         */
        bool isOtaModeEnabled;
        /**
         * One of:
         *  WIFI_MANAGER_NETWORK_MODE_STA = 1,
         *  WIFI_MANAGER_NETWORK_MODE_AP = 4
         */
        wifi_manager_networkMode_t networkMode;

        /**
         * The network ssid
         */
        char ssid[WDRV_WINC_MAX_SSID_LEN + 1];

        /**
         * The board hostname
         */
        char hostName[WIFI_MANAGER_DNS_CLIENT_MAX_HOSTNAME_LEN + 1];

        /**
         * The network ssid strength
         */
        uint8_t ssid_str;

        /**
         * One of:
         * 
         * WIFI_MANAGER_SECURITY_MODE_OPEN,
         * WIFI_MANAGER_SECURITY_MODE_WPA_AUTO_WITH_PASS_PHRASE,
         *  
         */
        wifi_manager_securityMode_t securityMode;

        /**
         * The length of the passkey
         */
        uint8_t passKeyLength;

        /**
         * The passkey for the given security type
         */
        uint8_t passKey[WDRV_WINC_PSK_LEN + 1];

        /**
         * The MAX Address
         */
        WDRV_WINC_MAC_ADDR macAddr;

        /**
         * The ip address
         */
        wifi_manager_ipv4Addr_t ipAddr;

        /**
         * The ip mask
         */
        wifi_manager_ipv4Addr_t ipMask;

        /**
         * The ip gateway
         */
        wifi_manager_ipv4Addr_t gateway;

        /**
         * The port to open for incoming TCP connections
         */
        uint16_t tcpPort;

    } wifi_manager_settings_t;

    typedef struct {
        uint32_t chipID;
        char frimwareVersion[WIFI_MANAGER_CHIP_INFO_FW_VERSION_MAX_SIZE + 1];
        char BuildDate[sizeof (__DATE__)];
        char BuildTime[sizeof (__TIME__)];
    } wifi_manager_chipInfo_t;
    /**
     * @brief Initializes the WiFi manager and its state machine.
     * 
     * Sets up the event queue, initializes WiFi settings, and prepares the system to handle WiFi events.
     * 
     * @param[in] pSettings Pointer to the WifiSettings structure containing network configuration.
     * 
     * @return True if initialization is successful, false otherwise.
     */
    bool wifi_manager_Init(wifi_manager_settings_t* settings);
    /**
     * @brief Reads the Wi-Fi chip information and populates the provided structure.
     *
     * This function retrieves the chip ID, firmware version, build date, and build time
     * from the Wi-Fi firmware and stores them into the `wifi_manager_chipInfo_t` structure
     * pointed to by `pChipInfo`.
     *
     * @param[out] pChipInfo Pointer to a `wifi_manager_chipInfo_t` structure where the chip information will be stored.
     *
     * @return `true` if the chip information was successfully read and stored; `false` otherwise.
     *
     * @note This function will return `false` if the Wi-Fi manager has not been initialized
     *       or if `pChipInfo` is `NULL`.
     */
    bool wifi_manager_GetChipInfo(wifi_manager_chipInfo_t *pChipInfo);
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
    bool wifi_manager_UpdateNetworkSettings(wifi_manager_settings_t* pSettings);


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
    void wifi_manager_FormUdpAnnouncePacketCB(const wifi_manager_settings_t *pWifiSettings, uint8_t *pBuffer, uint16_t *pPacketLen);


#ifdef	__cplusplus
}
#endif

#endif	/* _WIFI_MANAGER_H */

