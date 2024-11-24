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

#define SD_CARD_ACTIVE_ERROR_MSG "\r\nError !! Please Disable SD Card\r\n"

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
    wifi_manager_settings_t * pWifiSettings = BoardData_Get(BOARDDATA_WIFI_SETTINGS, 0);
    bool enabled = pWifiSettings->isEnabled;
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
    sd_card_manager_settings_t* pSdCardRuntimeConfig = BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
    int param1;
    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        result = SCPI_RES_ERR;
        goto __exit_point;
    }
    if (param1 != 0 && pSdCardRuntimeConfig->enable == 1) {
        context->interface->write(context, SD_CARD_ACTIVE_ERROR_MSG, strlen(SD_CARD_ACTIVE_ERROR_MSG));
        result = SCPI_RES_ERR;
        goto __exit_point;
    }

    pRunTimeWifiSettings->isEnabled = (bool) param1;
    result = SCPI_RES_OK;
__exit_point:
    return result;
}

scpi_result_t SCPI_LANNetModeGet(scpi_t * context) {
    wifi_manager_settings_t * pWifiSettings = BoardData_Get(BOARDDATA_WIFI_SETTINGS, 0);
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

    switch (param1) {
        case WIFI_MANAGER_NETWORK_MODE_AP:
        case WIFI_MANAGER_NETWORK_MODE_STA:
            break;
        default:
            return SCPI_RES_ERR;
    }

    pRunTimeWifiSettings->networkMode = (uint8_t) param1;
    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Get the Ip address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANAddrGet(scpi_t * context) {
    wifi_manager_settings_t * pWifiSettings = BoardData_Get(
            BOARDDATA_WIFI_SETTINGS,
            0);

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
    wifi_manager_settings_t * pWifiSettings = BoardData_Get(
            BOARDDATA_WIFI_SETTINGS,
            0);

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
    return SCPI_LANAddrGetImpl(
            context,
            &pRunTimeWifiSettings->ipMask);
}

/**
 * SCPI Callback: Get the Ip address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANGatewayGet(scpi_t * context) {
    wifi_manager_settings_t * pWifiSettings = BoardData_Get(
            BOARDDATA_WIFI_SETTINGS,
            0);
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
    return SCPI_LANAddrGetImpl(
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
            BOARDDATA_WIFI_SETTINGS,
            0);
    if (MacAddr_ToString(pWifiSettings->macAddr.addr,
            buffer,
            MAX_MAC_ADDR_STR_LEN) < 1) {
        return SCPI_RES_ERR;
    }

    SCPI_LANStringGetImpl(context, buffer);
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANSsidGet(scpi_t * context) {
    wifi_manager_settings_t * pWifiSettings = BoardData_Get(
            BOARDDATA_WIFI_SETTINGS,
            0);

    return SCPI_LANStringGetImpl(
            context,
            pWifiSettings->ssid);
}

scpi_result_t SCPI_LANSsidSet(scpi_t * context) {
    wifi_manager_settings_t * pRunTimeWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);

    return SCPI_LANStringSetImpl(
            context,
            pRunTimeWifiSettings->ssid,
            WDRV_WINC_MAX_SSID_LEN);
}

scpi_result_t SCPI_LANSsidStrengthGet(scpi_t * context) {
    wifi_manager_settings_t * pWifiSettings = BoardData_Get(
            BOARDDATA_WIFI_SETTINGS,
            0);

    return SCPI_ResultInt32(context, (int) pWifiSettings->ssid_str);
}

scpi_result_t SCPI_LANSecurityGet(scpi_t * context) {
    wifi_manager_settings_t * pWifiSettings = BoardData_Get(
            BOARDDATA_WIFI_SETTINGS,
            0);
    SCPI_ResultInt32(context, pWifiSettings->securityMode);
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANSecuritySet(scpi_t * context) {
    int param1;
    wifi_manager_settings_t * pRunTimeWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);

    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }

    switch (param1) {
        case WIFI_MANAGER_SECURITY_MODE_OPEN: // DAQiFi defines 0 = SECURITY_OPEN
            pRunTimeWifiSettings->securityMode = WIFI_MANAGER_SECURITY_MODE_OPEN;
            break;
        case WIFI_MANAGER_SECURITY_MODE_WEP_40: // DAQiFi defines 1 = WEP_40 which is now deprecated
            return SCPI_RES_ERR;
            break;
        case WIFI_MANAGER_SECURITY_MODE_WEP_104: // DAQiFi defines 2 = WEP_104 which is now deprecated
            return SCPI_RES_ERR;
            break;
        case WIFI_MANAGER_SECURITY_MODE_WPA_AUTO_WITH_PASS_PHRASE: // DAQiFi defines 3 = SECURITY_WPA_AUTO_WITH_PASS_PHRASE
        case WIFI_MANAGER_SECURITY_MODE_WPA_DEPRECATED: //DAQiFi defines 4 = WIFI_API_SEC_WPA_DEPRECATED - keeping for backwards compatibility
            pRunTimeWifiSettings->securityMode = WIFI_MANAGER_SECURITY_MODE_WPA_AUTO_WITH_PASS_PHRASE;
            break;
        case WIFI_MANAGER_SECURITY_MODE_802_1X: // DAQiFi defines 5 = WDRV_WINC_AUTH_TYPE_802_1X
            return SCPI_RES_ERR; // Not currently implemented
            pRunTimeWifiSettings->securityMode = WIFI_MANAGER_SECURITY_MODE_802_1X;
            break;
        case WIFI_MANAGER_SECURITY_MODE_SEC_802_1X_MSCHAPV2: // DAQiFi defines 6 = WDRV_WINC_AUTH_TYPE_802_1X_MSCHAPV2
            return SCPI_RES_ERR; // Not currently implemented
            pRunTimeWifiSettings->securityMode = WIFI_MANAGER_SECURITY_MODE_SEC_802_1X_MSCHAPV2;
            break;
        case WIFI_MANAGER_SECURITY_MODE_WPS_PUSH_BUTTON: // DAQiFi defines 7 = SECURITY_WPS_PUSH_BUTTON which is now deprecated
            return SCPI_RES_ERR;
        case WIFI_MANAGER_SECURITY_MODE_SEC_WPS_PIN: // DAQiFi defines 8 = SECURITY_WPS_PIN which is now deprecated
            return SCPI_RES_ERR;
        case WIFI_MANAGER_SECURITY_MODE_SEC_802_1X_TLS: // DAQiFi defines 9 = WDRV_WINC_AUTH_TYPE_802_1X_TLS
            return SCPI_RES_ERR; // Not currently implemented
            pRunTimeWifiSettings->securityMode = WIFI_MANAGER_SECURITY_MODE_SEC_802_1X_TLS;
            break;

        default:
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
    wifi_manager_settings_t * pWifiSettings = BoardData_Get(
            BOARDDATA_WIFI_SETTINGS, 0);
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
    DaqifiSettings * pWifiSettings = BoardData_Get(
            BOARDDATA_WIFI_SETTINGS,
            0);
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
    DaqifiSettings * pRunTimeWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);
    if (SCPI_ParamInt32(context, &param1, FALSE)) {
        applySettings = (bool) param1;
    }

    if (!daqifi_settings_LoadFromNvm(
            pRunTimeWifiSettings->type,
            pRunTimeWifiSettings)) {
        return SCPI_RES_ERR;
    }

    if (applySettings) {
        if (!wifi_manager_UpdateNetworkSettings(&pRunTimeWifiSettings->settings.wifi)) {
            return SCPI_RES_ERR;
        }
        BoardData_Set(
                BOARDDATA_WIFI_SETTINGS,
                0,
                pRunTimeWifiSettings);
    }

    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANSettingsFactoryLoad(scpi_t * context) {
    bool applySettings = false;
    int param1;
    DaqifiSettings * pRunTimeWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);
    if (SCPI_ParamInt32(context, &param1, FALSE)) {
        applySettings = (bool) param1;
    }

    if (!daqifi_settings_LoadFactoryDeafult(
            pRunTimeWifiSettings->type,
            pRunTimeWifiSettings)) {
        return SCPI_RES_ERR;
    }

    if (applySettings) {
        if (!wifi_manager_UpdateNetworkSettings(&pRunTimeWifiSettings->settings.wifi)) {
            return SCPI_RES_ERR;
        }
        BoardData_Set(
                BOARDDATA_WIFI_SETTINGS,
                0,
                pRunTimeWifiSettings);
    }

    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANSettingsClear(scpi_t * context) {
    DaqifiSettings * pWifiSettings = BoardData_Get(
            BOARDDATA_WIFI_SETTINGS,
            0);
    if (daqifi_settings_ClearNvm(pWifiSettings->type)) {
        return SCPI_RES_ERR;
    }

    return SCPI_RES_OK;
}
//scpi_result_t SCPI_LANAVSsidStrengthGet(scpi_t * context)
//{
//    uint8_t index = 0;
//    WifiSettings * pWifiSettings = (WifiSettings *)BoardData_Get(                         
//                        BOARDDATA_WIFI_SETTINGS,                            
//                        0); 
//    uint8_t numberOfResults = pWifiSettings->av_num;
//
//
//    for(index = 0;index<numberOfResults;index++)
//    {
//        SCPI_ResultInt32(                                                   
//                    context,                                                
//                    (int) pWifiSettings->av_ssid_str[index]);
//    }
//    return SCPI_RES_OK;
//}
//

//
//scpi_result_t SCPI_LANAVSecurityGet(scpi_t * context)
//{
//    uint8_t index = 0;
//    WifiSettings * pWifiSettings = (WifiSettings *)BoardData_Get(                          
//                        BOARDDATA_WIFI_SETTINGS,                            
//                        0); 
//    uint8_t numberOfResults = pWifiSettings->av_num;
//
//    for(index = 0;index<numberOfResults;index++)
//    {
//        SCPI_ResultInt32(                                                   
//                    context,                                                
//                    pWifiSettings->av_securityMode[index]);
//    }
//
//    return SCPI_RES_OK;
//}

scpi_result_t SCPI_LANHostnameGet(scpi_t * context) {
    wifi_manager_settings_t * pRunTimeWifiSettings = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);
    return SCPI_LANStringGetImpl(context, pRunTimeWifiSettings->hostName);
}
//
//scpi_result_t SCPI_LANHostnameSet(scpi_t * context)
//{
//    DaqifiSettings * pRunTimeWifiSettings = (DaqifiSettings *)BoardRunTimeConfig_Get(         
//                        BOARDRUNTIME_WIFI_SETTINGS);
//    
//    return SCPI_LANStringSetImpl(                                           
//                        context,                                            
//                        pRunTimeWifiSettings->settings.wifi.hostName,       
//                        WIFI_MANAGER_DNS_CLIENT_MAX_HOSTNAME_LEN);
//}
/**
 * SCPI Callback: Set the mac address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
//scpi_result_t SCPI_LANMacSet(scpi_t * context)
//{
//    char value[MAX_MAC_ADDR_STR_LEN + 1];
//    size_t len = SCPI_SafeParamString(context, value, MAX_MAC_ADDR_STR_LEN, TRUE);
//   
//    WifiSettings * pRunTimeWifiSettings = (WifiSettings *)BoardRunTimeConfig_Get(         
//                        BOARDRUNTIME_WIFI_SETTINGS);
//    
//    if (len < 1)
//    {
//        return SCPI_RES_ERR;
//    }
//    
//    if (!MacAddr_FromString(                                                
//                        value,                                              
//                        len,                                                
//                        pRunTimeWifiSettings->macAddr.addr))
//    {
//        return SCPI_RES_ERR;
//    }
//    
//    return SCPI_RES_OK;
//}
///**
// * SCPI Callback: Set the IP address of the device
// * @return SCPI_RES_OK on success SCPI_RES_ERR on error
// */
//scpi_result_t SCPI_LANDns1Set(scpi_t * context)
//{
//    WifiSettings * pRunTimeWifiSettings = (WifiSettings *)BoardRunTimeConfig_Get(BOARDRUNTIME_WIFI_SETTINGS);
//    return SCPI_LANAddrGetImpl(                                             
//                        context,                               
//                        &pRunTimeWifiSettings->priDns);
//}


/**
 * SCPI Callback: Get the Ip address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
//scpi_result_t SCPI_LANDns2Get(scpi_t * context)
//{
//    DaqifiSettings * pWifiSettings = (DaqifiSettings *)BoardData_Get(                         
//                        BOARDDATA_WIFI_SETTINGS,                            
//                        0); 
//    return SCPI_LANAddrGetImpl(                                             
//                        context,                          
//                        &pWifiSettings->settings.wifi.secDns);
//}

/**
 * SCPI Callback: Set the IP address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
//scpi_result_t SCPI_LANDns2Set(scpi_t * context)
//{
//    DaqifiSettings * pRunTimeWifiSettings = (DaqifiSettings *)BoardRunTimeConfig_Get(         
//                        BOARDRUNTIME_WIFI_SETTINGS);
//    return SCPI_LANAddrGetImpl(                                             
//                        context,                                            
//                        &pRunTimeWifiSettings->settings.wifi.secDns);
//}
//scpi_result_t SCPI_LANAVNetTypeGet(scpi_t * context)
//{
//    uint8_t index = 0;
//    DaqifiSettings * pWifiSettings = (DaqifiSettings *)BoardData_Get(                         
//                        BOARDDATA_WIFI_SETTINGS,                            
//                        0); 
//    uint8_t numberOfResults = pWifiSettings->settings.wifi.av_num;
//
//    for(index = 0;index<numberOfResults;index++)
//    {
//        SCPI_ResultInt32(                                                   
//                context,                                                    
//                (int)pWifiSettings->settings.wifi.av_networkType[index]);
//    }
//    
//    return SCPI_RES_OK;
//}
///**
// * SCPI Callback: Get the Ip address of the device
// * @return SCPI_RES_OK on success SCPI_RES_ERR on error
// */
//scpi_result_t SCPI_LANDns1Get(scpi_t * context)
//{
//    WifiSettings * pWifiSettings = (WifiSettings *)BoardData_Get(                         
//                        BOARDDATA_WIFI_SETTINGS,                            
//                        0); 
//    return SCPI_LANAddrGetImpl(                                             
//                        context,                                            
//                        &pWifiSettings->priDns);
//}