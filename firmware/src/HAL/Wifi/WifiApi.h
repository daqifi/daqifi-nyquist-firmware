/* 
 * File:   WifiApi.h
 * Author: Daniel
 *
 * Defines the publicly accessible API for the wifi portion of the application
 * 
 * Created on May 9, 2016, 2:55 PM
 */

#ifndef WIFIAPI_H
#define	WIFIAPI_H

#ifdef	__cplusplus
extern "C" {
#endif
    
#define WIFI_EASY_CONFIG_DEMO
#define WIFI_EASY_CONFIG_DEMO_VERSION_NUMBER "1.0"

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************
#include "system_config.h"
#include "system_definitions.h"
    
#include "../NVM/DaqifiSettings.h"    

#if defined(TCPIP_IF_MRF24WN) /* Wi-Fi Interface */
#include "app_wifi_mrf24wn.h"
#elif defined(TCPIP_IF_WINC1500)
#include "app_wifi_winc1500.h"
#endif

#define WF_SCAN_RESULTS_BUFFER_SIZE 32

// *****************************************************************************
// *****************************************************************************
// Section: Type Definitions
// *****************************************************************************
// *****************************************************************************

// *****************************************************************************
/* Application States

  Summary:
    Application states enumeration

  Description:
    This enumeration defines the valid application states.  These states
    determine the behavior of the application at various times.
 */
typedef enum
{
    /* The application mounts the disk. */
    APP_MOUNT_DISK = 0,

    /* In this state, the application waits for the initialization of the TCP/IP stack
       to complete. */
    APP_TCPIP_WAIT_INIT,

    /* The application configures the Wi-Fi settings. */
    APP_WIFI_CONFIG,

    /* In this state, the application runs the Wi-Fi prescan. */
    APP_WIFI_PRESCAN,

    /* In this state, the application enables TCP/IP modules such as DHCP, NBNS and mDNS
       in all available interfaces. */
    APP_TCPIP_MODULES_ENABLE,

    /* In this state, the application can do TCP/IP transactions. */
    APP_TCPIP_TRANSACT,

    /* In this state, the application performs module FW update over the air. */
    APP_FW_OTA_UPDATE,

    /* In this state, the application waits till FW update gets completed. */
    APP_WAIT_FOR_FW_UPDATE,

    /* The application waits in this state for the driver to be ready
       before sending the "hello world" message. */
    //APP_STATE_WAIT_FOR_READY,

    /* The application waits in this state for the driver to finish
       sending the message. */
    //APP_STATE_WAIT_FOR_DONE,

    /* The application does nothing in the idle state. */
    //APP_STATE_IDLE

    APP_USERIO_LED_DEASSERTED,

    APP_USERIO_LED_ASSERTED,

    APP_TCPIP_ERROR,

} APP_STATE;

typedef enum
{
    /* Initialize the state machine, and also checks if prescan is allowed. */
    APP_WIFI_PRESCAN_INIT,

    /* In this state the application waits for the prescan to finish. */
    APP_WIFI_PRESCAN_WAIT,

    /* After prescan, Wi-Fi module is reset in this state. */
    APP_WIFI_PRESCAN_RESET,

    /* In this state, the application waits for Wi-Fi reset to finish. */
    APP_WIFI_PRESCAN_WAIT_RESET,

    /* Prescan is complete. */
    APP_WIFI_PRESCAN_DONE,

} APP_WIFI_PRESCAN_STATE;

// *****************************************************************************
/* Application Data

  Summary:
    Holds application data

  Description:
    This structure holds the application's data.

  Remarks:
    Application strings and buffers are be defined outside this structure.
 */
typedef struct
{
    /* SYS_FS file handle */
    SYS_FS_HANDLE fileHandle;

    /* application's current state */
    APP_STATE state;

    /* prescan's current state */
    APP_WIFI_PRESCAN_STATE prescanState;

    /* application data buffer */
    //uint8_t data[64];

    //uint32_t nBytesWritten;

    //uint32_t nBytesRead;
} APP_DATA;

/* It is intentionally declared this way to sync with WDRV_DEVICE_TYPE. */
typedef enum {
    MRF24WN_MODULE = 3,
    WINC1500_MODULE = 4,
    WILC1000_MODULE = 5
} WF_MODULE_TYPE;

typedef struct {
    uint8_t numberOfResults;
    WF_SCAN_RESULT results[WF_SCAN_RESULTS_BUFFER_SIZE];
} WF_SCAN_CONTEXT;

typedef struct {
    uint8_t ssid[WF_MAX_SSID_LENGTH + 1]; // 32-byte SSID plus null terminator
    uint8_t networkType;
    uint8_t prevSSID[WF_MAX_SSID_LENGTH + 1]; // previous SSID
    uint8_t prevNetworkType; // previous network type
    uint8_t wepKeyIndex;
    uint8_t securityMode;
    uint8_t securityKey[WDRV_MAX_SECURITY_KEY_LENGTH]; // null terminator is included
    uint8_t securityKeyLen; // number of bytes in security key (can be 0)
} WF_REDIRECTION_CONFIG;

// *****************************************************************************
// *****************************************************************************
// Section: Application Initialization and State Machine Functions
// *****************************************************************************
// *****************************************************************************

/*******************************************************************************
  Function:
    uint8_t APP_WIFI_Prescan(void)

  Summary:
    Wi-Fi Prescan Function

  Description:
    This function implements the Wi-Fi prescan in a non blocking manner.

  Precondition:
    The system and application initialization ("SYS_Initialize") should be
    called before calling this function.

  Parameters:
    None.

  Returns:
    None.

  Remarks:
    None.
 */
uint8_t APP_WIFI_Prescan(void);

    
    /**
     * Initializes Wifi state machine and settings
     * 
     */
    void WifiInit(const WifiSettings* settings);

    /**
     * Should be called periodically to run the Wifi state machine
     * 
     */
    void WifiTasks(void);
    
    /**
     * Applies provided network settings (and resets the wifi connection)
     * @return True on success, false otherwise
     */
    bool WifiApplyNetworkSettings(WifiSettings* settings);

    /**
     * Reset the wifi connection
     * NOTE: This does not apply new settings- call ApplyNetworkSettings to do that
     * @return True on success, false otherwise
     */
    bool WifiResetConnection(WifiSettings* settings);

    /**
     * Copies the wifi key from the source to the buffer
     * @param buffer
     * @param securityMode
     * @param key
     * @return 
     */
    uint8_t WifiCopyKey(uint8_t* buffer, uint8_t securityMode, const uint8_t* source);
    
    /**
     * Brings the network connection up
     */
    bool WifiConnectionUp();
    
    /**
     * Brings the network connection down
     * @return 
     */
    bool WifiConnectionDown();

#ifdef	__cplusplus
}
#endif

#endif	/* WIFIAPI_H */

