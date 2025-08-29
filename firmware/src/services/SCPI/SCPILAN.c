#include "SCPILAN.h"

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
// SPI0 mutex functions declared in app_freertos.c
extern bool SPI0_Mutex_Lock(int client, unsigned int timeout);
extern void SPI0_Mutex_Unlock(int client);
#define SPI0_CLIENT_SD_CARD 0
#define SPI0_CLIENT_WIFI 1

#include "Util/Logger.h"

#define LOG_LEVEL_LOCAL 'D'

#define SD_CARD_ACTIVE_ERROR_MSG "\r\nPlease Disable SD Card\r\n"

// WiFi status functions have been moved to wifi_manager

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
    char value[128];
    size_t len = SCPI_SafeParamString(context, value, 127, TRUE);
    if (len < 1) {
        return SCPI_RES_ERR;
    }

    if (len > maxLen) {
        return SCPI_RES_ERR;
    } else if (len < 1) {
        string[0] = '\0';
    } else {
        memcpy(string, value, len);
        string[len] = '\0';
    }

    return SCPI_RES_OK;
}

static size_t SCPI_SafeParamString(scpi_t * context, char* value, const size_t maxLength, scpi_bool_t mandatory) {
    const char* buffer;
    size_t len;
    if (!SCPI_ParamCharacters(context, &buffer, &len, mandatory)) {
        return SCPI_RES_ERR;
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
    if (param1 != 0) {
        // Try to acquire SPI0 bus for WiFi (lower priority than SD card)
        if (!SPI0_Mutex_Lock(SPI0_CLIENT_WIFI, 0)) {
            context->interface->write(context, "\r\nError: SPI0 bus busy (SD card has priority)\r\n", 50);
            result = SCPI_RES_ERR;
            goto __exit_point;
        }
        
        pRunTimeWifiSettings->isEnabled = true;
    } else {
        pRunTimeWifiSettings->isEnabled = false;
        
        // Release SPI0 bus when disabling WiFi
        SPI0_Mutex_Unlock(SPI0_CLIENT_WIFI);
    }
    // LOG_D("WiFi enabled set to %d in runtime config\r\n", param1);
    
    result = SCPI_RES_OK;
__exit_point:
    return result;
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
 * SCPI Callback: Get the Ip address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANAddrGet(scpi_t * context) {
    wifi_manager_settings_t * pWifiSettings = BoardData_Get(
            BOARDDATA_WIFI_SETTINGS, 0);

    return SCPI_LANAddrGetImpl(
            context,
            &pWifiSettings->ipAddr);
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
    wifi_manager_settings_t * pWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);

    return SCPI_LANAddrGetImpl(
            context,
            &pWifiSettings->ipMask);
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
    wifi_manager_settings_t * pWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);
    return SCPI_LANAddrGetImpl(
            context,
            &pWifiSettings->gateway);
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
    uint8_t rssiPercentage = 0;
    
    // Try to get fresh RSSI with 1 second timeout
    if (wifi_manager_GetRSSI(&rssiPercentage, 1000)) {
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

scpi_result_t SCPI_LANSettingsApply(scpi_t * context) {
    bool saveSettings = false;
    int param1;
    wifi_manager_settings_t * pRunTimeWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);
    sd_card_manager_settings_t* pSdCardRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);

    // LOG_D("SCPI_LANSettingsApply called - WiFi enabled=%d, mode=%d\r\n", 
    //       pRunTimeWifiSettings->isEnabled, pRunTimeWifiSettings->networkMode);

    if (SCPI_ParamInt32(context, &param1, FALSE)) {
        saveSettings = (bool) param1;
    }
    
    //Wifi and SD card cannot be active simultaneously because they share same SPI
    if (pSdCardRuntimeConfig->enable == 1 &&
            pRunTimeWifiSettings->isEnabled == 1) {
        context->interface->write(context, SD_CARD_ACTIVE_ERROR_MSG, strlen(SD_CARD_ACTIVE_ERROR_MSG));
        return SCPI_RES_ERR;
    }

    if (saveSettings) {
        DaqifiSettings daqifiSettings;
        memcpy(&daqifiSettings.settings.wifi, pRunTimeWifiSettings, sizeof (wifi_manager_settings_t));
        daqifiSettings.type = DaqifiSettings_Wifi;
        if (!daqifi_settings_SaveToNvm(&daqifiSettings)) {
            return SCPI_RES_ERR;
        }
    }
    if (!wifi_manager_UpdateNetworkSettings(pRunTimeWifiSettings)) {
        return SCPI_RES_ERR;
    }
    //once fw update mode is enabled it should be cleared to disable automatic fw update mode
    pRunTimeWifiSettings->isOtaModeEnabled = false;
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANFwUpdate(scpi_t * context) {
    wifi_manager_settings_t * pRunTimeWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);

    pRunTimeWifiSettings->isOtaModeEnabled = true;
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANGetChipInfo(scpi_t * context) {
    char jsonChar[100];
    wifi_manager_chipInfo_t chipInfo;
    if (wifi_manager_GetChipInfo(&chipInfo) == false)
        return SCPI_RES_ERR;
    sprintf(jsonChar, "{\"ChipId\":%d,\"FwVersion\":\"%s\",\"BuildDate\":\"%s\"}\n",
            chipInfo.chipID, chipInfo.frimwareVersion, chipInfo.BuildDate);
    return SCPI_LANStringGetImpl(
            context,
            jsonChar);
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
    
    // Copy loaded settings to runtime config
    memcpy(pRunTimeWifiSettings, &daqifiSettings.settings.wifi, sizeof(wifi_manager_settings_t));

    if (applySettings) {
        if (!wifi_manager_UpdateNetworkSettings(pRunTimeWifiSettings)) {
            return SCPI_RES_ERR;
        }
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
    
    // Copy loaded settings to runtime config
    memcpy(pRunTimeWifiSettings, &daqifiSettings.settings.wifi, sizeof(wifi_manager_settings_t));

    if (applySettings) {
        if (!wifi_manager_UpdateNetworkSettings(pRunTimeWifiSettings)) {
            return SCPI_RES_ERR;
        }
    }

    return SCPI_RES_OK;
}


scpi_result_t SCPI_LANHostnameGet(scpi_t * context) {
    SCPI_LANStringGetImpl(context, DEFAULT_NETWORK_HOST_NAME);
    return SCPI_RES_OK;
}