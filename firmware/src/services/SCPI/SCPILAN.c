#define LOG_LVL LOG_LEVEL_SCPI
#define LOG_MODULE LOG_MODULE_SCPI

#include "SCPILAN.h"
#include "SCPIInterface.h"

// General
#include <stdlib.h>
#include <string.h>

// Harmony
#include "configuration.h"
#include "definitions.h"

// Project
#include "Util/StringFormatters.h"
#include "Util/Logger.h"
#include "state/data/BoardData.h"
//#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "wdrv_winc_client_api.h"
#include "services/daqifi_settings.h"
#include "services/wifi_services/wifi_manager.h"
#include "services/wifi_services/mdns_responder.h"   // #58: mDNS diagnostics
#include "services/SCPI/SCPIInterface.h"              // #58: shared response buffer
#include "HAL/UserSpi/UserSpi.h"                      // #665: user SPI1 master
#include "HAL/UserUart/UserUart.h"                    // #16: user UART
#include "services/sd_card_services/sd_card_manager.h"


// WiFi status functions have been moved to wifi_manager

/**
 * Check if WiFi is powered and ready. Returns true if WiFi GET commands
 * should proceed, false if they should fail with execution error.
 */
static bool SCPI_LANRequireWiFiReady(scpi_t* context) {
    wifi_status_t status = wifi_manager_GetWiFiStatus();
    if (status == WIFI_STATUS_DISABLED) {
        SCPI_ExecutionError(context, "SYST:COMM:LAN: WiFi not ready (device powered up?)");
        return false;
    }
    return true;
}

/**
 * Encodes the given ip multi-address as a scpi string
 * @param context The scpi context
 * @param ipv6 Indicates whether the address is expected to be an ipv6 address
 * @param address The address to encode
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
static scpi_result_t SCPI_LANAddrGetImpl(scpi_t * context, wifi_manager_ipv4Addr_t* pIpAddress);

/**
 * Decodes the given string into am ip multi-address
 * @param context The scpi context
 * @param ipv6 Indicates whether the address is expected to be an ipv6 address
 * @param address The address to populate
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
static scpi_result_t SCPI_LANAddrSetImpl(scpi_t * context, wifi_manager_ipv4Addr_t* pIpAddress);
//
/**
 * Adds a string to the SCPI result
 * @param context The scpi context
 * @param string The string to send
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
static scpi_result_t SCPI_LANStringGetImpl(scpi_t * context, const char* string);

/**
 * Sets a string based on the next available SCPI parameter
 * @param context The SCPI context
 * @param string The string to populate
 * @param maxLen The maximum length of the string
 * @return 
 */
static scpi_result_t SCPI_LANStringSetImpl(scpi_t * context, char* string, size_t maxLen);

/**
 * A safe version of SCPI_ParamString that reads the value into a provided buffer and null terminates it
 * @param context The SCPI context
 * @param value The buffer to populate
 * @param maxLength The max length of actual data in the buffer (minus the null tuerminator)
 * @param mandatory Indicates whether the value is mandatory
 * @return The size of the data in 'value'
 */
static size_t SCPI_SafeParamString(scpi_t * context, char* value, const size_t maxLength, scpi_bool_t mandatory);

////////
// Private implementations
////////

static scpi_result_t SCPI_LANAddrGetImpl(scpi_t * context, wifi_manager_ipv4Addr_t* pIpAddress) {
    char buffer[MAX_TCPIPV4_STR_LEN + 1];
    memset(buffer, 0, sizeof (buffer));
    inet_ntop(AF_INET, &pIpAddress->Val, buffer, MAX_TCPIPV4_STR_LEN);
    SCPI_LANStringGetImpl(context, buffer);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_LANAddrSetImpl(scpi_t * context, wifi_manager_ipv4Addr_t* pIpAddress) {
    char buffer[MAX_TCPIPV4_STR_LEN + 1];
    memset(buffer, 0, sizeof (buffer));
    size_t len = SCPI_SafeParamString(context, buffer, MAX_TCPIPV4_STR_LEN, TRUE);
    if (len < 1) {
        return SCPI_RES_ERR;
    }

    pIpAddress->Val = inet_addr(buffer);

    return SCPI_RES_OK;
}

static scpi_result_t SCPI_LANStringGetImpl(scpi_t * context, const char* string) {
    if (string == NULL) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultMnemonic(context, string);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_LANStringSetImpl(scpi_t * context, char* string, size_t maxLen) {
    // Parse straight into the caller's buffer — no intermediate 128 B stack
    // local. (#355)
    //
    // API contract shared with the other SCPI_SafeParamString callers in
    // this file: `maxLen` is the max PAYLOAD length. Callers allocate
    // `maxLen + 1` bytes so SafeParamString's `value[len] = '\0'` write at
    // index `len == maxLen` lands inside the buffer. Current outer caller
    // `SCPI_LANSsidSet` passes `pRunTimeWifiSettings->ssid` (char[33]) with
    // maxLen = WDRV_WINC_MAX_SSID_LEN (= 32), matching the convention also
    // used by SCPI_LANAddrSetImpl, SCPI_LANPasskeySet, and
    // SCPI_LANPasskeyGet (all of which declare `char value[FOO_LEN + 1]`
    // and pass maxLength = FOO_LEN).
    size_t len = SCPI_SafeParamString(context, string, maxLen, TRUE);
    if (len < 1) {
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

static size_t SCPI_SafeParamString(scpi_t * context, char* value, const size_t maxLength, scpi_bool_t mandatory) {
    const char* buffer;
    size_t len;
    if (!SCPI_ParamCharacters(context, &buffer, &len, mandatory)) {
        // Return 0 on parse failure, not SCPI_RES_ERR (= -1). This function
        // returns size_t, and (size_t)-1 is a huge positive number that
        // bypasses `if (len < 1)` checks in callers. 0 makes "failure" and
        // "empty" indistinguishable at the type level, but every caller
        // treats both the same way ("nothing usable was parsed") and the
        // parser separately raises an SCPI error for the client.
        return 0;
    }

    if (len > 0) {
        if (len > maxLength) {
            return 0;
        }

        memcpy(value, buffer, len);
        value[len] = '\0';
    }

    return len;
}

/**
 * SCPI Callback: Get the Enabled/Disabled status of LAN
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANEnabledGet(scpi_t * context) {
    wifi_manager_settings_t * pWifiSettings = BoardRunTimeConfig_Get(BOARDRUNTIME_WIFI_SETTINGS);
    bool enabled = pWifiSettings->isEnabled;
    // LOG_D("SCPI_LANEnabledGet: Runtime enabled=%d\r\n", enabled);
    SCPI_ResultInt32(context, (int) enabled);

    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Set the Enabled/Disabled status of LAN
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANEnabledSet(scpi_t * context) {
    wifi_manager_settings_t * pRunTimeWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);
    scpi_result_t result;
    int param1;
    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }
    // Simple enable/disable - no mutex blocking for concurrent operations
    // SPI coordination handled at operation level, not enable level
    pRunTimeWifiSettings->isEnabled = (bool) param1;
    // LOG_D("WiFi enabled set to %d in runtime config\r\n", param1);
    
    result = SCPI_RES_OK;
__exit_point:
    return result;
}

/**
 * SCPI Callback: Query WINC deep-power-down state (#334).
 * Returns 1 if the WINC is powered/available, 0 if held in deep
 * power-down (CHIP_EN low).
 */
scpi_result_t SCPI_LANPowerGet(scpi_t * context) {
    SCPI_ResultInt32(context, wifi_manager_IsPoweredOff() ? 0 : 1);
    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Deep power-down / power-up the WINC1500 chip (#334).
 *
 * SYST:COMM:LAN:POWer 0  -> hold WINC in shutdown (CHIP_EN low) for
 *                           battery savings; the rest of the device
 *                           (USB/SD streaming, ADC) keeps running.
 * SYST:COMM:LAN:POWer 1  -> bring the WINC back up (re-init).
 *
 * Distinct from SYST:POWer:STATe, which standbys the *whole* device.
 * Distinct from SYST:COMM:LAN:ENAbled, which only toggles the runtime
 * enable flag and leaves the chip powered/idle.
 *
 * The deep-power-down teardown is queued via wifi_manager_PowerOff and
 * completed by app_WifiTask; PowerOn re-inits asynchronously (~1-2 s).
 */
scpi_result_t SCPI_LANPowerSet(scpi_t * context) {
    int param1;
    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }
    if (param1 != 0 && param1 != 1) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    bool ok = (param1 == 0) ? wifi_manager_PowerOff()
                            : wifi_manager_PowerOn();
    if (!ok) {
        SCPI_ExecutionError(context,
                "SYST:COMM:LAN:POWer: WiFi manager not ready");
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANNetModeGet(scpi_t * context) {
    wifi_manager_settings_t * pWifiSettings = BoardRunTimeConfig_Get(BOARDRUNTIME_WIFI_SETTINGS);
    uint8_t type = pWifiSettings->networkMode;
    SCPI_ResultInt32(context, (int) type);

    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Set the type of LAN network
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANNetModeSet(scpi_t * context) {
    int param1;

    wifi_manager_settings_t * pRunTimeWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);

    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }

    // Validate network mode: 1 = STA, 4 = AP
    switch (param1) {
        case WIFI_MANAGER_NETWORK_MODE_STA:  // 1
        case WIFI_MANAGER_NETWORK_MODE_AP:   // 4
            break;
        default:
            SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
            return SCPI_RES_ERR;
    }

    // Check if already in the requested mode - no-op if so
    if (pRunTimeWifiSettings->networkMode == (uint8_t)param1) {
        // Already in requested mode, nothing to do
        return SCPI_RES_OK;
    }

    pRunTimeWifiSettings->networkMode = (uint8_t) param1;
    // LOG_D("WiFi mode set to %d\r\n", param1);
    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Get the AP-mode SSID hidden/cloaked flag (#45)
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANHiddenGet(scpi_t * context) {
    wifi_manager_settings_t * pWifiSettings = BoardRunTimeConfig_Get(BOARDRUNTIME_WIFI_SETTINGS);
    SCPI_ResultInt32(context, (int) pWifiSettings->ssidHidden);

    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Set the AP-mode SSID hidden/cloaked flag (#45)
 * 1 = SSID hidden (not broadcast in beacons), 0 = SSID visible (default).
 * Takes effect at the next LAN:APPLY / AP (re)start.
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANHiddenSet(scpi_t * context) {
    wifi_manager_settings_t * pRunTimeWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);
    int param1;

    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }

    if (param1 != 0 && param1 != 1) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }

    pRunTimeWifiSettings->ssidHidden = (bool) param1;
    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Get the Ip address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANAddrGet(scpi_t * context) {
    if (!SCPI_LANRequireWiFiReady(context)) return SCPI_RES_ERR;
    wifi_manager_settings_t * pWifiSettings = BoardData_Get(
            BOARDDATA_WIFI_SETTINGS, 0);

    return SCPI_LANAddrGetImpl(
            context,
            &pWifiSettings->ipAddr);
}

scpi_result_t SCPI_LANConfAddrGet(scpi_t * context) {
    wifi_manager_settings_t * pWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);
    return SCPI_LANAddrGetImpl(context, &pWifiSettings->ipAddr);
}

/**
 * SCPI Callback: Set the IP address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANAddrSet(scpi_t * context) {
    wifi_manager_settings_t * pRunTimeWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);

    return SCPI_LANAddrSetImpl(
            context,
            &pRunTimeWifiSettings->ipAddr);
}

/**
 * SCPI Callback: Get the Ip mask of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANMaskGet(scpi_t * context) {
    if (!SCPI_LANRequireWiFiReady(context)) return SCPI_RES_ERR;
    wifi_manager_settings_t * pWifiSettings = BoardData_Get(
            BOARDDATA_WIFI_SETTINGS, 0);

    return SCPI_LANAddrGetImpl(
            context,
            &pWifiSettings->ipMask);
}

scpi_result_t SCPI_LANConfMaskGet(scpi_t * context) {
    wifi_manager_settings_t * pWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);
    return SCPI_LANAddrGetImpl(context, &pWifiSettings->ipMask);
}

/**
 * SCPI Callback: Set the IP mask of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANMaskSet(scpi_t * context) {
    wifi_manager_settings_t * pRunTimeWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);
    return SCPI_LANAddrSetImpl(
            context,
            &pRunTimeWifiSettings->ipMask);
}

/**
 * SCPI Callback: Get the Ip address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANGatewayGet(scpi_t * context) {
    if (!SCPI_LANRequireWiFiReady(context)) return SCPI_RES_ERR;
    wifi_manager_settings_t * pWifiSettings = BoardData_Get(
            BOARDDATA_WIFI_SETTINGS, 0);
    return SCPI_LANAddrGetImpl(
            context,
            &pWifiSettings->gateway);
}

scpi_result_t SCPI_LANConfGatewayGet(scpi_t * context) {
    wifi_manager_settings_t * pWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);
    return SCPI_LANAddrGetImpl(context, &pWifiSettings->gateway);
}

/**
 * SCPI Callback: Set the IP address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANGatewaySet(scpi_t * context) {
    wifi_manager_settings_t * pRunTimeWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);
    return SCPI_LANAddrSetImpl(
            context,
            &pRunTimeWifiSettings->gateway);
}

/**
 * SCPI Callback: Get the mac address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANMacGet(scpi_t * context) {
    if (!SCPI_LANRequireWiFiReady(context)) return SCPI_RES_ERR;
    char buffer[MAX_MAC_ADDR_STR_LEN];

    wifi_manager_settings_t * pWifiSettings = BoardData_Get(
            BOARDDATA_WIFI_SETTINGS,0);
    if (MacAddr_ToString(pWifiSettings->macAddr.addr,
            buffer,
            MAX_MAC_ADDR_STR_LEN) < 1) {
        return SCPI_RES_ERR;
    }

    SCPI_LANStringGetImpl(context, buffer);
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANSsidGet(scpi_t * context) {
    wifi_manager_settings_t * pWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);

    return SCPI_LANStringGetImpl(
            context,
            pWifiSettings->ssid);
}

scpi_result_t SCPI_LANSsidSet(scpi_t * context) {
    wifi_manager_settings_t * pRunTimeWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);

    scpi_result_t result = SCPI_LANStringSetImpl(
            context,
            pRunTimeWifiSettings->ssid,
            WDRV_WINC_MAX_SSID_LEN);
    
    if (result == SCPI_RES_OK) {
        // LOG_D("WiFi SSID set to '%s'\r\n", pRunTimeWifiSettings->ssid);
    }
    
    return result;
}

scpi_result_t SCPI_LANSsidStrengthGet(scpi_t * context) {
    if (!SCPI_LANRequireWiFiReady(context)) return SCPI_RES_ERR;
    uint8_t rssiPercentage = 0;
    
    // Only attempt fresh RSSI read when connected (avoids 1s blocking timeout)
    if (wifi_manager_GetWiFiStatus() == WIFI_STATUS_CONNECTED &&
        wifi_manager_GetRSSI(&rssiPercentage, 1000)) {
        // Successfully got fresh RSSI
        SCPI_ResultInt32(context, (int) rssiPercentage);
    } else {
        // Failed to get fresh RSSI, return last known value from BoardData
        wifi_manager_settings_t * pWifiSettings = BoardData_Get(
                BOARDDATA_WIFI_SETTINGS, 0);
        if (pWifiSettings != NULL) {
            SCPI_ResultInt32(context, (int) pWifiSettings->rssi_percent);
        } else {
            // No valid settings, return 0
            SCPI_ResultInt32(context, 0);
        }
    }
    
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANSecurityGet(scpi_t * context) {
    wifi_manager_settings_t * pWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);
    
    // Normalize mode 4 (deprecated WPA) to mode 3 for consistency
    int32_t mode = pWifiSettings->securityMode;
    if (mode == WIFI_MANAGER_SECURITY_MODE_WPA_DEPRECATED) {
        mode = WIFI_MANAGER_SECURITY_MODE_WPA_AUTO_WITH_PASS_PHRASE;
    }
    
    SCPI_ResultInt32(context, mode);
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANSecuritySet(scpi_t * context) {
    int param1;
    wifi_manager_settings_t * pRunTimeWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);

    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }

    // Validate security mode and handle supported/unsupported modes
    switch (param1) {
        case WIFI_MANAGER_SECURITY_MODE_OPEN: // 0 = SECURITY_OPEN
            // Check if already in OPEN mode
            if (pRunTimeWifiSettings->securityMode == WIFI_MANAGER_SECURITY_MODE_OPEN) {
                return SCPI_RES_OK;  // No change needed
            }
            pRunTimeWifiSettings->securityMode = WIFI_MANAGER_SECURITY_MODE_OPEN;
            break;
        case WIFI_MANAGER_SECURITY_MODE_WPA_AUTO_WITH_PASS_PHRASE: // 3 = SECURITY_WPA_AUTO_WITH_PASS_PHRASE
        case WIFI_MANAGER_SECURITY_MODE_WPA_DEPRECATED: // 4 = WPA_DEPRECATED - keeping for backwards compatibility
            // Both modes 3 and 4 normalize to mode 3 (WPA_AUTO_WITH_PASS_PHRASE)
            // Check if already in WPA mode (stored as mode 3)
            if (pRunTimeWifiSettings->securityMode == WIFI_MANAGER_SECURITY_MODE_WPA_AUTO_WITH_PASS_PHRASE ||
                pRunTimeWifiSettings->securityMode == WIFI_MANAGER_SECURITY_MODE_WPA_DEPRECATED) {
                // Already in WPA mode (could be stored as 3 or 4 from legacy code)
                // Normalize to mode 3 if it's currently 4
                if (pRunTimeWifiSettings->securityMode == WIFI_MANAGER_SECURITY_MODE_WPA_DEPRECATED) {
                    pRunTimeWifiSettings->securityMode = WIFI_MANAGER_SECURITY_MODE_WPA_AUTO_WITH_PASS_PHRASE;
                }
                return SCPI_RES_OK;  // No change needed
            }
            pRunTimeWifiSettings->securityMode = WIFI_MANAGER_SECURITY_MODE_WPA_AUTO_WITH_PASS_PHRASE;
            break;
        
        // Deprecated/unsupported modes
        case WIFI_MANAGER_SECURITY_MODE_WEP_40: // 1 = WEP_40 (deprecated)
        case WIFI_MANAGER_SECURITY_MODE_WEP_104: // 2 = WEP_104 (deprecated)
        case WIFI_MANAGER_SECURITY_MODE_802_1X: // 5 = 802.1X (not implemented)
        case WIFI_MANAGER_SECURITY_MODE_SEC_802_1X_MSCHAPV2: // 6 = 802.1X_MSCHAPV2 (not implemented)
        case WIFI_MANAGER_SECURITY_MODE_WPS_PUSH_BUTTON: // 7 = WPS_PUSH_BUTTON (deprecated)
        case WIFI_MANAGER_SECURITY_MODE_SEC_WPS_PIN: // 8 = WPS_PIN (deprecated)
        case WIFI_MANAGER_SECURITY_MODE_SEC_802_1X_TLS: // 9 = 802.1X_TLS (not implemented)
            SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
            return SCPI_RES_ERR;
            
        default:
            SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
            return SCPI_RES_ERR;
    }

    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANPasskeySet(scpi_t * context) {
    char value[WDRV_WINC_PSK_LEN + 1];
    size_t len = SCPI_SafeParamString(
            context,
            value,
            WDRV_WINC_PSK_LEN, TRUE);

    wifi_manager_settings_t * pRunTimeWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);

    if (len < 1) {
        return SCPI_RES_ERR;
    }

    // TODO: Additional validation (length?))
    switch (pRunTimeWifiSettings->securityMode) {
        case WIFI_MANAGER_SECURITY_MODE_WPA_AUTO_WITH_PASS_PHRASE:
            break;
        case WIFI_MANAGER_SECURITY_MODE_OPEN: // No Key
        default:
            return SCPI_RES_ERR;
    }

    memcpy(pRunTimeWifiSettings->passKey, value, len);
    pRunTimeWifiSettings->passKeyLength = len;
    pRunTimeWifiSettings->passKey[len] = '\0';

    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANPasskeyGet(scpi_t * context) {
    char value[WDRV_WINC_PSK_LEN + 1];
    size_t len = SCPI_SafeParamString(
            context,
            value,
            WDRV_WINC_PSK_LEN, TRUE);
    wifi_manager_settings_t * pWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);
    if (len < 1) {
        return SCPI_RES_ERR;
    }

    if (pWifiSettings->passKeyLength != len) {
        SCPI_ResultInt32(context, 0);
    } else if (len > 0 &&
            memcmp(pWifiSettings->passKey, value, len) != 0) {
        SCPI_ResultInt32(context, 0);
    } else {
        SCPI_ResultInt32(context, 1);
    }

    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANBssidGet(scpi_t * context) {
    // Gate on WiFi-ready FIRST, same as ADDRess?/SSIDStr? — without this, BSSID?
    // would return a stale cached MAC after the link is disabled/torn down (the
    // wifi_manager precheck alone wasn't cleared on the ENA 0 + APPLY path).
    if (!SCPI_LANRequireWiFiReady(context)) return SCPI_RES_ERR;
    uint8_t bssid[6];
    // Strictly non-blocking (safe on app_WifiTask via TCP-SCPI). The cache is
    // primed at association, so this returns the AP MAC immediately.
    if (!wifi_manager_GetBSSID(bssid)) {
        // GetBSSID returns false for two distinct reasons — report each accurately
        // so the client knows whether retrying helps:
        //   - not connected as STA (AP mode / not associated) → retry won't help
        //   - connected but the STA_CONNECTED prefetch hasn't published yet → retry
        if (wifi_manager_GetWiFiStatus() != WIFI_STATUS_CONNECTED) {
            SCPI_ExecutionError(context, "SYST:COMM:LAN:BSSID?: not connected as STA");
        } else {
            SCPI_ExecutionError(context, "SYST:COMM:LAN:BSSID?: BSSID not available yet (retry)");
        }
        return SCPI_RES_ERR;
    }
    char buf[18]; // "XX:XX:XX:XX:XX:XX\0"
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    // A MAC (colon-separated hex) is arbitrary string data, not a SCPI keyword
    // mnemonic — return it as a quoted SCPI string (Qodo). BSSID? is new in this
    // PR so quoting it correctly costs no client compatibility. (ADDR? above keeps
    // SCPI_ResultMnemonic intentionally — it ships, and changing its wire format
    // would break existing clients that parse the bare value.)
    SCPI_ResultText(context, buf);
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANSettingsApply(scpi_t * context) {
    bool saveSettings = false;
    int param1;
    wifi_manager_settings_t * pRunTimeWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);

    // LOG_D("SCPI_LANSettingsApply called - WiFi enabled=%d, mode=%d\r\n", 
    //       pRunTimeWifiSettings->isEnabled, pRunTimeWifiSettings->networkMode);

    if (SCPI_ParamInt32(context, &param1, FALSE)) {
        saveSettings = (bool) param1;
    }

    // Concurrent WiFi and SD card operations now supported with enhanced SPI coordination

    // #425: apply first so a gate rejection (REINIT in flight) doesn't leave
    // the rejected settings persisted in NVM after we tell the client the
    // call failed.  The gate is centralized inside UpdateNetworkSettings
    // (atomic test-and-set); false return = surface as -200.
    if (!wifi_manager_UpdateNetworkSettings(pRunTimeWifiSettings)) {
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
    if (saveSettings) {
        DaqifiSettings daqifiSettings;
        memcpy(&daqifiSettings.settings.wifi, pRunTimeWifiSettings, sizeof (wifi_manager_settings_t));
        daqifiSettings.type = DaqifiSettings_Wifi;
        if (!daqifi_settings_SaveToNvm(&daqifiSettings)) {
            // Apply already succeeded and REINIT may already be in flight.
            // Returning -200 here would mislead the client into thinking
            // the apply itself failed (#425 r4 Qodo bug 5).  Instead push
            // an error so SYST:ERR? reports the persistence failure, but
            // return OK because the apply itself succeeded.  Settings will
            // revert to NVM contents on next boot.
            LOG_E("WiFi APPLY: settings applied, but NVM save failed");
            SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
            return SCPI_RES_OK;
        }
    }
    // Note: WiFi firmware update mode request (if set via FWUPDATE) is handled by the WiFi state machine.
    // The REQUESTED flag will be checked and cleared in ENTRY event handler.
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANHardReset(scpi_t * context) {
    if (!wifi_manager_HardReset()) {
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANFwUpdate(scpi_t * context) {
    // Block if SD card is actively logging (shared SPI bus)
    if (sd_card_manager_IsBusy()) {
        LOG_E("Cannot start WiFi firmware update while SD card is busy (SPI bus conflict)");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }

    // Request WiFi firmware update mode via state machine flag
    // On next APPLY, state machine will transition to WiFi firmware update mode instead of reconfiguring WiFi
    wifi_manager_RequestWifiFirmwareUpdate();
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANGetChipInfo(scpi_t * context) {
    char jsonChar[100];
    wifi_manager_chipInfo_t chipInfo;
    if (wifi_manager_GetChipInfo(&chipInfo) == false)
        return SCPI_RES_ERR;
    snprintf(jsonChar, sizeof(jsonChar), "{\"ChipId\":%d,\"FwVersion\":\"%s\",\"BuildDate\":\"%s\"}\n",
            chipInfo.chipID, chipInfo.frimwareVersion, chipInfo.BuildDate);
    return SCPI_LANStringGetImpl(
            context,
            jsonChar);
}

/* #58: mDNS responder diagnostics. Reports whether the responder is still
 * receiving/answering queries (recvArmed + rx/match/resp counters) and how many
 * times the self-heal has re-opened a deaf socket (armFail/heal) — answerable
 * on-device, independent of host-side mDNS tooling (unreliable on multi-homed
 * hosts). Uses the shared response buffer (JSON can approach 256 B). */
scpi_result_t SCPI_LANMdnsDiagGet(scpi_t * context) {
    mdns_diag_t d;
    mdns_responder_GetDiag(&d);
    char *buf = (char *)SCPI_ResponseBuf_Take();
    if (buf == NULL) {
        return SCPI_RES_ERR;
    }
    snprintf(buf, 512,
             "{\"Active\":%d,\"RecvArmed\":%d,\"Rx\":%lu,\"Match\":%lu,\"Resp\":%lu,"
             "\"ArmFail\":%lu,\"Heal\":%lu,\"LastArmRc\":%d,"
             "\"Instance\":\"%s\",\"Host\":\"%s\"}\n",
             (int)d.active, (int)d.recvArmed,
             (unsigned long)d.rxCount, (unsigned long)d.matchCount,
             (unsigned long)d.respCount, (unsigned long)d.armFailCount,
             (unsigned long)d.healCount, (int)d.lastArmRc,
             d.instance, d.host);
    scpi_result_t r = SCPI_LANStringGetImpl(context, buf);
    SCPI_ResponseBuf_Give();
    return r;
}

// =========================================================================
// User SPI1 master (#665, epic #664) — SYST:COMM:SPI:*
// =========================================================================

/* Map a signed SCPI pin argument to a DIO channel or the "none" sentinel.
 * Negative = unused line; anything > 15 is rejected by the caller to avoid
 * a uint8_t truncation-alias (cf. #678). */
static uint8_t spi_ScpiPinToDio(int32_t v) {
    return (v < 0) ? USER_SPI_PIN_NONE : (uint8_t)v;
}

/* True if the SCPI line has an unconsumed trailing token after the expected
 * parameters — a real extra parameter (comma-separated: SCPI_Parameter returns
 * TRUE) or an invalid separator (space-separated: SCPI_Parameter returns FALSE
 * but queues an error). libscpi only flags a trailing token AFTER the callback
 * returns, so every SPI SETTER must call this and bail BEFORE applying any side
 * effect (pin claims, PPS/PMD, clocking, config store) — a rejected command
 * must not leave hardware or config changed (e.g. `SPI:ENA 1,0` would otherwise
 * leave SPI enabled). Pushes an execution error for the extra-parameter case;
 * SCPI_Parameter already queued one for the invalid-separator case. */
static bool spi_RejectTrailingParam(scpi_t* context) {
    scpi_parameter_t extra;
    if (SCPI_Parameter(context, &extra, FALSE)) {
        SCPI_ExecutionError(context, "unexpected trailing parameter");
        return true;
    }
    return SCPI_ParamErrorOccurred(context);
}

scpi_result_t SCPI_SpiConfigSet(scpi_t * context) {
    int32_t mosi, miso, cs, baud, mode;
    int32_t order = 0;   /* optional, default MSB-first */

    if (!SCPI_ParamInt32(context, &mosi, TRUE) ||
        !SCPI_ParamInt32(context, &miso, TRUE) ||
        !SCPI_ParamInt32(context, &cs,   TRUE) ||
        !SCPI_ParamInt32(context, &baud, TRUE) ||
        !SCPI_ParamInt32(context, &mode, TRUE)) {
        return SCPI_RES_ERR;   /* libscpi already pushed a parse error */
    }
    /* Optional order param: if it is SUPPLIED it must be a valid integer, else
     * reject the whole config. The old (void)SCPI_ParamInt32 discarded the
     * parse result, so a bad 6th token (e.g. ...,0,BOGUS) pushed a DATA_TYPE
     * error yet still committed the config with the default order. Mirror the
     * SCPI_Parameter(FALSE)+SCPI_ParamToInt32 idiom used elsewhere. */
    scpi_parameter_t orderParam;
    if (SCPI_Parameter(context, &orderParam, FALSE)) {
        if (!SCPI_ParamToInt32(context, &orderParam, &order)) {
            return SCPI_RES_ERR;   /* present but not an integer (error already pushed) */
        }
    }
    /* else: absent -> keep the MSB-first default (order = 0). */

    if (spi_RejectTrailingParam(context)) {
        return SCPI_RES_ERR;   /* a rejected CONFig must not store the config */
    }

    /* Reject out-of-range pin indices before the uint8_t narrowing so a
     * value like 258 can't alias onto DIO2. Negative = unused line. */
    if (mosi > 15 || miso > 15 || cs > 15) {
        SCPI_ExecutionError(context, "SPI pin index out of range (-1..15)");
        return SCPI_RES_ERR;
    }
    if (mode < 0 || mode > 3) {
        SCPI_ExecutionError(context, "SPI mode must be 0..3");
        return SCPI_RES_ERR;
    }

    UserSpiConfig_t cfg;
    cfg.mosiDio  = spi_ScpiPinToDio(mosi);
    cfg.misoDio  = spi_ScpiPinToDio(miso);
    cfg.csDio    = spi_ScpiPinToDio(cs);
    cfg.baudHz   = (baud <= 0) ? USER_SPI_DEFAULT_BAUD_HZ : (uint32_t)baud;  /* 0/neg = safe default */
    cfg.mode     = (uint8_t)mode;
    cfg.lsbFirst = (order != 0);

    const char* err = NULL;
    if (!UserSpi_Configure(&cfg, &err)) {
        SCPI_ExecutionError(context, (err != NULL) ? err : "invalid SPI config");
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

scpi_result_t SCPI_SpiConfigGet(scpi_t * context) {
    UserSpiConfig_t cfg;
    if (!UserSpi_GetConfig(&cfg)) {
        SCPI_ExecutionError(context, "no SPI config set");
        return SCPI_RES_ERR;
    }
    char *buf = (char *)SCPI_ResponseBuf_Take();
    if (buf == NULL) {
        return SCPI_RES_ERR;
    }
    int mosi = (cfg.mosiDio == USER_SPI_PIN_NONE) ? -1 : (int)cfg.mosiDio;
    int miso = (cfg.misoDio == USER_SPI_PIN_NONE) ? -1 : (int)cfg.misoDio;
    int cs   = (cfg.csDio   == USER_SPI_PIN_NONE) ? -1 : (int)cfg.csDio;
    snprintf(buf, SCPI_RESPONSE_BUF_SIZE,
             "{\"Sck\":0,\"Mosi\":%d,\"Miso\":%d,\"Cs\":%d,\"Baud\":%lu,"
             "\"Mode\":%d,\"LsbFirst\":%d,\"Enabled\":%d,\"ActualBaud\":%lu}\n",
             mosi, miso, cs, (unsigned long)cfg.baudHz,
             (int)cfg.mode, (int)cfg.lsbFirst,
             (int)UserSpi_IsEnabled(), (unsigned long)UserSpi_GetActualBaud());
    /* SCPI_ResultMnemonic emits the JSON raw (no SCPI string-quoting), so
     * clients get parseable JSON — matching the mDNS diagnostic convention. */
    SCPI_ResultMnemonic(context, buf);
    SCPI_ResponseBuf_Give();
    return SCPI_RES_OK;
}

scpi_result_t SCPI_SpiEnableSet(scpi_t * context) {
    int32_t on;
    if (!SCPI_ParamInt32(context, &on, TRUE)) {
        return SCPI_RES_ERR;
    }
    if (spi_RejectTrailingParam(context)) {
        return SCPI_RES_ERR;   /* a rejected ENAble must not claim pins / power SPI1 */
    }
    const char* err = NULL;
    bool ok = (on != 0) ? UserSpi_Enable(&err) : UserSpi_Disable();
    if (!ok) {
        SCPI_ExecutionError(context, (err != NULL) ? err : "SPI enable failed");
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

scpi_result_t SCPI_SpiEnableGet(scpi_t * context) {
    SCPI_ResultInt32(context, UserSpi_IsEnabled() ? 1 : 0);
    return SCPI_RES_OK;
}

/* Parse a hex byte string (tolerant of quotes, spaces, commas, underscores
 * and an optional 0x prefix) into @p out. Requires an even, non-zero count
 * of hex digits and at most @p maxOut bytes. */
static bool spi_ParseHex(const char* p, size_t len, uint8_t* out,
                         uint16_t maxOut, uint16_t* nOut) {
    uint16_t n = 0;
    int hi = -1;   /* pending high nibble, -1 = none */
    size_t i = 0;
    if (len >= 2u && p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        i = 2u;
    }
    for (; i < len; ++i) {
        char c = p[i];
        if (c == ' ' || c == '\t' || c == '_' || c == ',' || c == '"') {
            continue;
        }
        int v;
        if (c >= '0' && c <= '9')      { v = c - '0'; }
        else if (c >= 'a' && c <= 'f') { v = c - 'a' + 10; }
        else if (c >= 'A' && c <= 'F') { v = c - 'A' + 10; }
        else { return false; }
        if (hi < 0) {
            hi = v;
        } else {
            if (n >= maxOut) { return false; }
            out[n++] = (uint8_t)(((uint8_t)hi << 4) | (uint8_t)v);
            hi = -1;
        }
    }
    if (hi >= 0 || n == 0u) {
        return false;   /* odd nibble count or empty */
    }
    *nOut = n;
    return true;
}

scpi_result_t SCPI_SpiTransfer(scpi_t * context) {
    const char* p = NULL;
    size_t len = 0;
    if (!SCPI_ParamCharacters(context, &p, &len, TRUE)) {
        return SCPI_RES_ERR;
    }
    /* Exactly one (quoted) hex arg — reject any trailing token (comma- OR
     * space-separated) BEFORE we clock anything, else a truncated frame reaches
     * the slave before the error surfaces. The documented form is one quoted
     * token, e.g. "9F0000" (spi_ParseHex tolerates commas inside it). */
    if (spi_RejectTrailingParam(context)) {
        return SCPI_RES_ERR;
    }
    if (!UserSpi_IsEnabled()) {
        SCPI_ExecutionError(context, "SPI not enabled");
        return SCPI_RES_ERR;
    }

    /* Carve tx/rx/out regions out of the shared 2048B response buffer so we
     * never put a >=256B scratch array on the (small) WifiTask stack. */
    uint8_t *buf = SCPI_ResponseBuf_Take();
    if (buf == NULL) {
        return SCPI_RES_ERR;
    }
    uint8_t *tx  = buf;             /* [0..255]   */
    uint8_t *rx  = buf + 256;       /* [256..511] */
    char    *out = (char *)(buf + 512);  /* [512..2047] */

    uint16_t n = 0;
    if (!spi_ParseHex(p, len, tx, 256u, &n)) {
        SCPI_ResponseBuf_Give();
        SCPI_ExecutionError(context, "SPI:TRAN: bad hex (even digits, <=256 bytes)");
        return SCPI_RES_ERR;
    }
    if (!UserSpi_Transfer(tx, rx, n)) {
        SCPI_ResponseBuf_Give();
        SCPI_ExecutionError(context, "SPI transfer timeout");
        return SCPI_RES_ERR;
    }

    static const char hexd[] = "0123456789ABCDEF";
    for (uint16_t i = 0; i < n; ++i) {
        out[2 * i]     = hexd[(rx[i] >> 4) & 0x0Fu];
        out[2 * i + 1] = hexd[rx[i] & 0x0Fu];
    }
    out[2 * n] = '\0';

    /* Raw hex, no SCPI quoting, so the client parses MISO bytes directly. */
    SCPI_ResultMnemonic(context, out);
    SCPI_ResponseBuf_Give();
    return SCPI_RES_OK;
}

// *****************************************************************************
// Section: user UART (#16, epic #664) — SYST:COMM:UART:*
//
// Reuses spi_ScpiPinToDio (-1 -> 0xFF, same sentinel) and spi_RejectTrailingParam.
// *****************************************************************************

/* Read an optional integer parameter: if SUPPLIED it must be a valid int (sets
 * *ok=false, leaving a queued DATA_TYPE error), else returns @p dflt. Avoids the
 * "invalid optional token commits anyway" class. */
static int32_t uart_ScpiOptInt(scpi_t* context, int32_t dflt, bool* ok) {
    scpi_parameter_t p;
    *ok = true;
    if (SCPI_Parameter(context, &p, FALSE)) {
        int32_t v;
        if (!SCPI_ParamToInt32(context, &p, &v)) { *ok = false; return dflt; }
        return v;
    }
    return dflt;
}

scpi_result_t SCPI_UartConfigSet(scpi_t * context) {
    int32_t rx, tx, baud;
    if (!SCPI_ParamInt32(context, &rx,   TRUE) ||
        !SCPI_ParamInt32(context, &tx,   TRUE) ||
        !SCPI_ParamInt32(context, &baud, TRUE)) {
        return SCPI_RES_ERR;   /* libscpi already pushed a parse error */
    }
    bool ok;
    int32_t data   = uart_ScpiOptInt(context, 8, &ok); if (!ok) return SCPI_RES_ERR;
    int32_t parity = uart_ScpiOptInt(context, 0, &ok); if (!ok) return SCPI_RES_ERR;
    int32_t stop   = uart_ScpiOptInt(context, 1, &ok); if (!ok) return SCPI_RES_ERR;
    if (spi_RejectTrailingParam(context)) {
        return SCPI_RES_ERR;   /* rejected CONFig must not store the config */
    }
    if (rx < -1 || rx > 15 || tx < -1 || tx > 15) {
        /* Only -1 (unused) is a valid negative; reject other negatives rather
         * than silently folding them to "unused". */
        SCPI_ExecutionError(context, "UART pin index out of range (-1..15)");
        return SCPI_RES_ERR;
    }
    if (baud < 0) {
        SCPI_ExecutionError(context, "UART baud must be >= 0 (0 = default)");
        return SCPI_RES_ERR;
    }
    /* Validate data/parity/stop on the FULL int32 value, before the uint8_t
     * narrowing below — else an out-of-range arg (e.g. parity 258) truncates
     * into a legal value (258&0xFF=2=ODD) and slips past uart_ConfigureLocked's
     * range check, silently applying the wrong setting (the #678 alias class). */
    if (data != 8 || parity < 0 || parity > 2 || (stop != 1 && stop != 2)) {
        SCPI_ExecutionError(context, "UART requires data=8, parity 0/1/2, stop 1/2");
        return SCPI_RES_ERR;
    }
    UserUartConfig_t cfg;
    cfg.rxDio    = spi_ScpiPinToDio(rx);
    cfg.txDio    = spi_ScpiPinToDio(tx);
    cfg.baudHz   = (baud == 0) ? USER_UART_DEFAULT_BAUD_HZ : (uint32_t)baud;
    cfg.dataBits = (uint8_t)data;
    cfg.parity   = (uint8_t)parity;
    cfg.stopBits = (uint8_t)stop;
    cfg.rxInv    = false;   /* set via SYST:COMM:UART:INVert */
    cfg.txInv    = false;
    const char* err = NULL;
    if (!UserUart_Configure(&cfg, &err)) {
        SCPI_ExecutionError(context, (err != NULL) ? err : "invalid UART config");
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

scpi_result_t SCPI_UartConfigGet(scpi_t * context) {
    UserUartConfig_t cfg;
    if (!UserUart_GetConfig(&cfg)) {
        SCPI_ExecutionError(context, "no UART config set");
        return SCPI_RES_ERR;
    }
    char *buf = (char *)SCPI_ResponseBuf_Take();
    if (buf == NULL) { return SCPI_RES_ERR; }
    int rx = (cfg.rxDio == USER_UART_PIN_NONE) ? -1 : (int)cfg.rxDio;
    int tx = (cfg.txDio == USER_UART_PIN_NONE) ? -1 : (int)cfg.txDio;
    snprintf(buf, SCPI_RESPONSE_BUF_SIZE,
             "{\"Rx\":%d,\"Tx\":%d,\"Baud\":%lu,\"Data\":%d,\"Parity\":%d,"
             "\"Stop\":%d,\"RxInv\":%d,\"TxInv\":%d,\"Enabled\":%d,\"ActualBaud\":%lu}\n",
             rx, tx, (unsigned long)cfg.baudHz, (int)cfg.dataBits, (int)cfg.parity,
             (int)cfg.stopBits, (int)cfg.rxInv, (int)cfg.txInv,
             (int)UserUart_IsEnabled(), (unsigned long)UserUart_GetActualBaud());
    SCPI_ResultMnemonic(context, buf);
    SCPI_ResponseBuf_Give();
    return SCPI_RES_OK;
}

scpi_result_t SCPI_UartEnableSet(scpi_t * context) {
    int32_t on;
    if (!SCPI_ParamInt32(context, &on, TRUE)) {
        return SCPI_RES_ERR;
    }
    if (spi_RejectTrailingParam(context)) {
        return SCPI_RES_ERR;   /* rejected ENAble must not claim pins / power the UART */
    }
    const char* err = NULL;
    bool ok = (on != 0) ? UserUart_Enable(&err) : UserUart_Disable();
    if (!ok) {
        SCPI_ExecutionError(context, (err != NULL) ? err : "UART enable failed");
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

scpi_result_t SCPI_UartEnableGet(scpi_t * context) {
    SCPI_ResultInt32(context, UserUart_IsEnabled() ? 1 : 0);
    return SCPI_RES_OK;
}

scpi_result_t SCPI_UartInvertSet(scpi_t * context) {
    int32_t rxInv, txInv;
    if (!SCPI_ParamInt32(context, &rxInv, TRUE) ||
        !SCPI_ParamInt32(context, &txInv, TRUE)) {
        return SCPI_RES_ERR;
    }
    if (spi_RejectTrailingParam(context)) {
        return SCPI_RES_ERR;
    }
    if (!UserUart_SetInvert(rxInv != 0, txInv != 0)) {
        SCPI_ExecutionError(context, "no UART config set");
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

scpi_result_t SCPI_UartInvertGet(scpi_t * context) {
    UserUartConfig_t cfg;
    if (!UserUart_GetConfig(&cfg)) {
        SCPI_ExecutionError(context, "no UART config set");
        return SCPI_RES_ERR;
    }
    char *buf = (char *)SCPI_ResponseBuf_Take();
    if (buf == NULL) { return SCPI_RES_ERR; }
    snprintf(buf, SCPI_RESPONSE_BUF_SIZE, "{\"RxInv\":%d,\"TxInv\":%d}\n",
             (int)cfg.rxInv, (int)cfg.txInv);
    SCPI_ResultMnemonic(context, buf);
    SCPI_ResponseBuf_Give();
    return SCPI_RES_OK;
}

scpi_result_t SCPI_UartWrite(scpi_t * context) {
    const char* p = NULL;
    size_t len = 0;
    if (!SCPI_ParamCharacters(context, &p, &len, TRUE)) {
        return SCPI_RES_ERR;
    }
    if (spi_RejectTrailingParam(context)) {
        return SCPI_RES_ERR;
    }
    if (!UserUart_IsEnabled()) {
        SCPI_ExecutionError(context, "UART not enabled");
        return SCPI_RES_ERR;
    }
    /* Parse into a small stack local, NOT the shared 2048B SCPI response buffer:
     * UserUart_Write can block for seconds at low baud (yielding), and holding
     * the global scratch mutex across it would stall every OTHER SCPI command
     * on both USB and WiFi. 128 bytes stays well under the WifiTask stack budget
     * (the ">=256B must use the shared buffer" rule) — split larger frames. */
    uint8_t tx[128];
    uint16_t n = 0;
    if (!spi_ParseHex(p, len, tx, sizeof(tx), &n)) {
        SCPI_ExecutionError(context, "UART:WRITE: bad hex (even digits, <=128 bytes)");
        return SCPI_RES_ERR;
    }
    if (!UserUart_Write(tx, n)) {
        SCPI_ExecutionError(context, "UART write timeout (TX not configured?)");
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

scpi_result_t SCPI_UartRead(scpi_t * context) {
    int32_t maxN;
    if (!SCPI_ParamInt32(context, &maxN, TRUE)) {
        return SCPI_RES_ERR;
    }
    if (spi_RejectTrailingParam(context)) {
        return SCPI_RES_ERR;
    }
    if (!UserUart_IsEnabled()) {
        SCPI_ExecutionError(context, "UART not enabled");
        return SCPI_RES_ERR;
    }
    if (maxN < 0) { maxN = 0; }
    if (maxN > 256) { maxN = 256; }
    uint8_t *rx  = SCPI_ResponseBuf_Take();     /* [0..255] read, [256..] hex out */
    if (rx == NULL) { return SCPI_RES_ERR; }
    char *out = (char *)(rx + 256);
    uint16_t got = UserUart_Read(rx, (uint16_t)maxN);
    static const char hexd[] = "0123456789ABCDEF";
    for (uint16_t i = 0; i < got; ++i) {
        out[2 * i]     = hexd[(rx[i] >> 4) & 0x0Fu];
        out[2 * i + 1] = hexd[rx[i] & 0x0Fu];
    }
    out[2 * got] = '\0';
    SCPI_ResultMnemonic(context, out);          /* raw hex of the RX bytes */
    SCPI_ResponseBuf_Give();
    return SCPI_RES_OK;
}

scpi_result_t SCPI_UartCount(scpi_t * context) {
    char *buf = (char *)SCPI_ResponseBuf_Take();
    if (buf == NULL) { return SCPI_RES_ERR; }
    snprintf(buf, SCPI_RESPONSE_BUF_SIZE, "{\"Pending\":%u,\"Overflow\":%lu}\n",
             (unsigned)UserUart_RxPending(), (unsigned long)UserUart_RxOverflowCount());
    SCPI_ResultMnemonic(context, buf);
    SCPI_ResponseBuf_Give();
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANSettingsSave(scpi_t * context) {
    wifi_manager_settings_t * pWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);
    DaqifiSettings daqifiSettings;
    memcpy(&daqifiSettings.settings.wifi, pWifiSettings, sizeof (wifi_manager_settings_t));
    daqifiSettings.type = DaqifiSettings_Wifi;
    if (!daqifi_settings_SaveToNvm(&daqifiSettings)) {
        return SCPI_RES_ERR;
    }

    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANSettingsLoad(scpi_t * context) {
    bool applySettings = false;
    int param1;
    wifi_manager_settings_t * pRunTimeWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);
    if (SCPI_ParamInt32(context, &param1, FALSE)) {
        applySettings = (bool) param1;
    }

    DaqifiSettings daqifiSettings;
    daqifiSettings.type = DaqifiSettings_Wifi;
    if (!daqifi_settings_LoadFromNvm(
            daqifiSettings.type,
            &daqifiSettings)) {
        return SCPI_RES_ERR;
    }

    if (applySettings) {
        // #425: pass the loaded struct straight to UpdateNetworkSettings —
        // it copies into runtime config under the gate's critical section
        // and fires REINIT.  If the gate rejects (REINIT in flight), runtime
        // stays at the prior values: callers get -200 with no half-applied
        // mutation.  Don't memcpy first.
        if (!wifi_manager_UpdateNetworkSettings(&daqifiSettings.settings.wifi)) {
            SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
            return SCPI_RES_ERR;
        }
    } else {
        // No apply requested — just stage in runtime so callers can inspect /
        // edit before a future APPLY.
        memcpy(pRunTimeWifiSettings, &daqifiSettings.settings.wifi, sizeof(wifi_manager_settings_t));
    }

    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANSettingsFactoryLoad(scpi_t * context) {
    bool applySettings = false;
    int param1;
    wifi_manager_settings_t * pRunTimeWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);
    if (SCPI_ParamInt32(context, &param1, FALSE)) {
        applySettings = (bool) param1;
    }

    DaqifiSettings daqifiSettings;
    daqifiSettings.type = DaqifiSettings_Wifi;
    if (!daqifi_settings_LoadFactoryDeafult(
            daqifiSettings.type,
            &daqifiSettings)) {
        return SCPI_RES_ERR;
    }

    if (applySettings) {
        // #425: see SCPI_LANSettingsLoad for rationale — gated copy + REINIT.
        if (!wifi_manager_UpdateNetworkSettings(&daqifiSettings.settings.wifi)) {
            SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
            return SCPI_RES_ERR;
        }
    } else {
        memcpy(pRunTimeWifiSettings, &daqifiSettings.settings.wifi, sizeof(wifi_manager_settings_t));
    }

    return SCPI_RES_OK;
}


scpi_result_t SCPI_LANHostnameGet(scpi_t * context) {
    wifi_manager_settings_t *pWifi = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);
    const char *host = (pWifi != NULL && pWifi->hostName[0] != '\0')
            ? pWifi->hostName
            : DEFAULT_NETWORK_HOST_NAME;
    SCPI_LANStringGetImpl(context, host);
    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Enable/disable automatic WiFi power-save (#29).
 *
 * `SYST:COMM:LAN:PSave <0|1>` — 1 lets the WiFi manager drop the WINC into
 * its light auto power-save mode while associated as a STA but idle; 0
 * forces full power at all times.  Runtime-only (not persisted to NVM).
 */
scpi_result_t SCPI_LANPowerSaveSet(scpi_t * context) {
    int param1;
    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }
    if (param1 != 0 && param1 != 1) {
        /* Match the LAN sibling setters (POWer/HIDden): documented 0|1 only. */
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    wifi_manager_SetPowerSaveEnabled(param1 != 0);
    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Query WiFi power-save state (#29).
 *
 * Returns "<enabled>,<appliedMode>" where enabled is 0/1 and appliedMode is
 * the WDRV_WINC_PS_MODE value currently in force (0 = full power / OFF, or
 * -1 when the WiFi driver is not open).
 */
scpi_result_t SCPI_LANPowerSaveGet(scpi_t * context) {
    SCPI_ResultInt32(context, wifi_manager_GetPowerSaveEnabled() ? 1 : 0);
    SCPI_ResultInt32(context, wifi_manager_GetPowerSaveMode());
    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Set the TCP console idle timeout in seconds (#663).
 *
 * A connected TCP console client with no RX or TX for this long is closed
 * (connect-and-never-send DoS guard). 0 disables the watchdog. Runtime-only.
 * A streaming client is continuously TX-active, so it is never idle and is
 * never torn down by this timeout.
 */
scpi_result_t SCPI_LANIdleTimeoutSet(scpi_t * context) {
    int param1;
    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }
    // #676 gate: bound the range. seconds * configTICK_RATE_HZ (1000) is a
    // 32-bit tick multiply, so values >= ~4.29M s silently wrap (and multiples
    // of 2^29 s wrap to exactly 0 = "disabled", inverting the query). Cap at a
    // generous 24 h — far beyond any real console idle timeout — mirroring the
    // bounded SCPI_SetTransportGrace setter. 0 = off.
    if (param1 < 0 || param1 > SYST_LAN_IDLE_TIMEOUT_MAX_SEC) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    wifi_manager_SetConsoleIdleTimeout((uint32_t)param1);
    return SCPI_RES_OK;
}

/** SCPI Callback: Query the TCP console idle timeout in seconds (0=off, #663). */
scpi_result_t SCPI_LANIdleTimeoutGet(scpi_t * context) {
    SCPI_ResultUInt32(context, wifi_manager_GetConsoleIdleTimeout());
    return SCPI_RES_OK;
}