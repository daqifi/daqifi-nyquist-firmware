#include "SCPILAN.h"

// General
#include <stdlib.h>
#include <string.h>

// Harmony
#include "system_config.h"
#include "system_definitions.h"

// Project
#include "Util/StringFormatters.h"
#include "Util/Logger.h"
#include "state/data/BoardData.h"
#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "HAL/Wifi/WifiApi.h"
#include "wdrv_winc1500_main.h"

extern WF_CONFIG g_wifi_cfg;

/**
 * Encodes the given ip multi-address as a scpi string
 * @param context The scpi context
 * @param ipv6 Indicates whether the address is expected to be an ipv6 address
 * @param address The address to encode
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
static scpi_result_t SCPI_LANAddrGetImpl(scpi_t * context, bool ipv6, IP_MULTI_ADDRESS* address);

/**
 * Decodes the given string into am ip multi-address
 * @param context The scpi context
 * @param ipv6 Indicates whether the address is expected to be an ipv6 address
 * @param address The address to populate
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
static scpi_result_t SCPI_LANAddrSetImpl(scpi_t * context, bool ipv6, IP_MULTI_ADDRESS* address);

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

/**
 * SCPI Callback: Get the Enabled/Disabled status of LAN
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANEnabledGet(scpi_t * context)
{
    DaqifiSettings * pWifiSettings = BoardData_Get(                         \
                        BOARDDATA_WIFI_SETTINGS,                            \
                        0); 
    bool enabled = pWifiSettings->settings.wifi.isEnabled;
    SCPI_ResultInt32(context, (int)enabled);
    
    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Set the Enabled/Disabled status of LAN
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANEnabledSet(scpi_t * context)
{
    DaqifiSettings * pRunTimeWifiSettings = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIME_WIFI_SETTINGS);
    
    int param1;
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
    
    pRunTimeWifiSettings->settings.wifi.isEnabled = (bool)param1;
    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Get the type of LAN network
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANNetTypeGet(scpi_t * context)
{
    DaqifiSettings * pWifiSettings = BoardData_Get(                         \
                        BOARDDATA_WIFI_SETTINGS,                            \
                        0); 
    uint8_t type = pWifiSettings->settings.wifi.networkType;
    SCPI_ResultInt32(context, (int)type);
    
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANAVNetTypeGet(scpi_t * context)
{
    uint8_t index = 0;
    DaqifiSettings * pWifiSettings = BoardData_Get(                         \
                        BOARDDATA_WIFI_SETTINGS,                            \
                        0); 
    uint8_t numberOfResults = pWifiSettings->settings.wifi.av_num;

    for(index = 0;index<numberOfResults;index++)
    {
        SCPI_ResultInt32(                                                   \
                context,                                                    \
                (int)pWifiSettings->settings.wifi.av_networkType[index]);
    }
    
    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Set the type of LAN network
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANNetTypeSet(scpi_t * context)
{
    int param1;
    
    DaqifiSettings * pRunTimeWifiSettings = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIME_WIFI_SETTINGS);
    
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
    
    switch (param1)
    {
    case WDRV_NETWORK_TYPE_INFRASTRUCTURE:
    case WDRV_NETWORK_TYPE_SOFT_AP:
        break;
    default:
        return SCPI_RES_ERR;
    }
    
    pRunTimeWifiSettings->settings.wifi.networkType = (uint8_t)param1;
    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Get the Ip address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANIpv6Get(scpi_t * context)
{
    DaqifiSettings * pWifiSettings = BoardData_Get(                         \
                        BOARDDATA_WIFI_SETTINGS,                            \
                        0); 
    bool ipv6 = pWifiSettings->settings.wifi.configFlags &                  \
                        TCPIP_NETWORK_CONFIG_IPV6_ADDRESS;
    SCPI_ResultInt32(context, (int)ipv6);
    
    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Set the IP address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANIpv6Set(scpi_t * context)
{
    int param1;
    
    DaqifiSettings * pRunTimeWifiSettings = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIME_WIFI_SETTINGS);
    
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
    
    bool ipv6 = (bool)param1;
    if (ipv6)
    {
        pRunTimeWifiSettings->settings.wifi.configFlags |=                  \
                        TCPIP_NETWORK_CONFIG_IPV6_ADDRESS;
    }
    else
    {
        pRunTimeWifiSettings->settings.wifi.configFlags &=                  \
                        ~TCPIP_NETWORK_CONFIG_IPV6_ADDRESS;
    }
    
    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Get the Ip address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANAddrGet(scpi_t * context)
{
    DaqifiSettings * pWifiSettings = BoardData_Get(                         \
                        BOARDDATA_WIFI_SETTINGS,                            \
                        0); 
    bool ipv6 = pWifiSettings->settings.wifi.configFlags &                  \
                        TCPIP_NETWORK_CONFIG_IPV6_ADDRESS;
    return SCPI_LANAddrGetImpl(                                             \
                        context,                                            \
                        ipv6,                                               \
                        &pWifiSettings->settings.wifi.ipAddr);
}

/**
 * SCPI Callback: Set the IP address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANAddrSet(scpi_t * context)
{    
    DaqifiSettings * pRunTimeWifiSettings = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIME_WIFI_SETTINGS);
    
    bool ipv6 = pRunTimeWifiSettings->settings.wifi.configFlags &           \
                        TCPIP_NETWORK_CONFIG_IPV6_ADDRESS;
    return SCPI_LANAddrSetImpl(                                             \
                        context,                                            \
                        ipv6,                                               \
                        &pRunTimeWifiSettings->settings.wifi.ipAddr);
}

/**
 * SCPI Callback: Get the Ip mask of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANMaskGet(scpi_t * context)
{
    DaqifiSettings * pWifiSettings = BoardData_Get(                         \
                        BOARDDATA_WIFI_SETTINGS,                            \
                        0); 
    bool ipv6 = pWifiSettings->settings.wifi.configFlags &                  \
                        TCPIP_NETWORK_CONFIG_IPV6_ADDRESS;
    return SCPI_LANAddrGetImpl(                                             \
                        context,                                            \
                        ipv6,                                               \
                        &pWifiSettings->settings.wifi.ipMask);
}

/**
 * SCPI Callback: Set the IP mask of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANMaskSet(scpi_t * context)
{
    DaqifiSettings * pRunTimeWifiSettings = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIME_WIFI_SETTINGS);
    
    bool ipv6 = pRunTimeWifiSettings->settings.wifi.configFlags &           \
                        TCPIP_NETWORK_CONFIG_IPV6_ADDRESS;
    return SCPI_LANAddrGetImpl(                                             \
                        context,                                            \
                        ipv6,                                               \
                        &pRunTimeWifiSettings->settings.wifi.ipMask);
}

/**
 * SCPI Callback: Get the Ip address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANGatewayGet(scpi_t * context)
{
    DaqifiSettings * pWifiSettings = BoardData_Get(                         \
                        BOARDDATA_WIFI_SETTINGS,                            \
                        0); 
    bool ipv6 = pWifiSettings->settings.wifi.configFlags &                  \
                        TCPIP_NETWORK_CONFIG_IPV6_ADDRESS;
    return SCPI_LANAddrGetImpl(                                             \
                        context,                                            \
                        ipv6,                                               \
                        &pWifiSettings->settings.wifi.gateway);
}

/**
 * SCPI Callback: Set the IP address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANGatewaySet(scpi_t * context)
{
    DaqifiSettings * pRunTimeWifiSettings = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIME_WIFI_SETTINGS);
    
    bool ipv6 = pRunTimeWifiSettings->settings.wifi.configFlags &           \
                        TCPIP_NETWORK_CONFIG_IPV6_ADDRESS;
    return SCPI_LANAddrGetImpl(                                             \
                        context,                                            \
                        ipv6,                                               \
                        &pRunTimeWifiSettings->settings.wifi.gateway);
}

/**
 * SCPI Callback: Get the Ip address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANDns1Get(scpi_t * context)
{
    DaqifiSettings * pWifiSettings = BoardData_Get(                         \
                        BOARDDATA_WIFI_SETTINGS,                            \
                        0); 
    bool ipv6 = pWifiSettings->settings.wifi.configFlags &                  \
                        TCPIP_NETWORK_CONFIG_IPV6_ADDRESS;
    return SCPI_LANAddrGetImpl(                                             \
                        context,                                            \
                        ipv6,                                               \
                        &pWifiSettings->settings.wifi.priDns);
}

/**
 * SCPI Callback: Set the IP address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANDns1Set(scpi_t * context)
{
    DaqifiSettings * pRunTimeWifiSettings = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIME_WIFI_SETTINGS);
    
    bool ipv6 = pRunTimeWifiSettings->settings.wifi.configFlags &           \
                        TCPIP_NETWORK_CONFIG_IPV6_ADDRESS;
    return SCPI_LANAddrGetImpl(                                             \
                        context,                                            \
                        ipv6,                                               \
                        &pRunTimeWifiSettings->settings.wifi.priDns);
}

/**
 * SCPI Callback: Get the Ip address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANDns2Get(scpi_t * context)
{
    DaqifiSettings * pWifiSettings = BoardData_Get(                         \
                        BOARDDATA_WIFI_SETTINGS,                            \
                        0); 
    bool ipv6 = pWifiSettings->settings.wifi.configFlags &                  \
                        TCPIP_NETWORK_CONFIG_IPV6_ADDRESS;
    return SCPI_LANAddrGetImpl(                                             \
                        context,                                            \
                        ipv6,                                               \
                        &pWifiSettings->settings.wifi.secDns);
}

/**
 * SCPI Callback: Set the IP address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANDns2Set(scpi_t * context)
{
    DaqifiSettings * pRunTimeWifiSettings = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIME_WIFI_SETTINGS);
    
    bool ipv6 = pRunTimeWifiSettings->settings.wifi.configFlags &           \
                        TCPIP_NETWORK_CONFIG_IPV6_ADDRESS;
    return SCPI_LANAddrGetImpl(                                             \
                        context,                                            \
                        ipv6,                                               \
                        &pRunTimeWifiSettings->settings.wifi.secDns);
}

/**
 * SCPI Callback: Get the mac address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANMacGet(scpi_t * context)
{
    char buffer[MAX_MAC_ADDR_LEN];

    DaqifiSettings * pWifiSettings = BoardData_Get(                         \
                        BOARDDATA_WIFI_SETTINGS,                            \
                        0); 
    if (MacAddr_ToString(&pWifiSettings->settings.wifi.macAddr,             \
                        buffer,                                             \
                        MAX_MAC_ADDR_LEN) < 1)
    {
        return SCPI_RES_ERR;
    }
    
    SCPI_LANStringGetImpl(context, buffer);
    return SCPI_RES_OK;
}

/**
 * SCPI Callback: Set the mac address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANMacSet(scpi_t * context)
{
    char value[MAX_MAC_ADDR_LEN + 1];
    size_t len = SCPI_SafeParamString(context, value, MAX_MAC_ADDR_LEN, TRUE);
   
    DaqifiSettings * pRunTimeWifiSettings = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIME_WIFI_SETTINGS);
    
    if (len < 1)
    {
        return SCPI_RES_ERR;
    }
    
    if (!MacAddr_FromString(                                                \
                        value,                                              \
                        len,                                                \
                        &pRunTimeWifiSettings->settings.wifi.macAddr))
    {
        return SCPI_RES_ERR;
    }
    
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANHostnameGet(scpi_t * context)
{
    DaqifiSettings * pWifiSettings = BoardData_Get(                         \
                        BOARDDATA_WIFI_SETTINGS,                            \
                        0); 
    return SCPI_LANStringGetImpl(context, pWifiSettings->settings.wifi.hostName);
}

scpi_result_t SCPI_LANHostnameSet(scpi_t * context)
{
    DaqifiSettings * pRunTimeWifiSettings = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIME_WIFI_SETTINGS);
    
    return SCPI_LANStringSetImpl(                                           \
                        context,                                            \
                        pRunTimeWifiSettings->settings.wifi.hostName,       \
                        TCPIP_DNS_CLIENT_MAX_HOSTNAME_LEN);
}

scpi_result_t SCPI_LANSsidGet(scpi_t * context)
{
    DaqifiSettings * pRunTimeWifiSettings = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIME_WIFI_SETTINGS);
    
    return SCPI_LANStringGetImpl(                                           \
                        context,                                            \
                        pRunTimeWifiSettings->settings.wifi.ssid);
}

scpi_result_t SCPI_LANSsidSet(scpi_t * context)
{
    DaqifiSettings * pRunTimeWifiSettings = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIME_WIFI_SETTINGS);
    
    return SCPI_LANStringSetImpl(                                           \
                        context,                                            \
                        pRunTimeWifiSettings->settings.wifi.ssid,           \
                        WDRV_MAX_SSID_LENGTH);
}

scpi_result_t SCPI_LANSsidStrengthGet(scpi_t * context)
{
    DaqifiSettings * pRunTimeWifiSettings = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIME_WIFI_SETTINGS);
    
    return SCPI_ResultInt32(                                                \
                        context,                                            \
                        (int) pRunTimeWifiSettings->settings.wifi.ssid_str);
}

scpi_result_t SCPI_LANAVSsidStrengthGet(scpi_t * context)
{
    uint8_t index = 0;
    DaqifiSettings * pWifiSettings = BoardData_Get(                         \
                        BOARDDATA_WIFI_SETTINGS,                            \
                        0); 
    uint8_t numberOfResults = pWifiSettings->settings.wifi.av_num;


    for(index = 0;index<numberOfResults;index++)
    {
        SCPI_ResultInt32(                                                   \
                    context,                                                \
                    (int) pWifiSettings->settings.wifi.av_ssid_str[index]);
    }
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANSecurityGet(scpi_t * context)
{
    DaqifiSettings * pWifiSettings = BoardData_Get(                         \
                        BOARDDATA_WIFI_SETTINGS,                            \
                        0); 
    SCPI_ResultInt32(context, pWifiSettings->settings.wifi.securityMode);
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANAVSecurityGet(scpi_t * context)
{
    uint8_t index = 0;DaqifiSettings * pWifiSettings = BoardData_Get(                          \
                        BOARDDATA_WIFI_SETTINGS,                            \
                        0); 
    uint8_t numberOfResults = pWifiSettings->settings.wifi.av_num;

    for(index = 0;index<numberOfResults;index++)
    {
        SCPI_ResultInt32(                                                   \
                    context,                                                \
                    pWifiSettings->settings.wifi.av_securityMode[index]);
    }

    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANSecuritySet(scpi_t * context)
{
    int param1;
    DaqifiSettings * pRunTimeWifiSettings = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIME_WIFI_SETTINGS);
    
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
    
    switch (param1)
    {
    case WDRV_SECURITY_OPEN:                        // 0
        break;
    case WDRV_SECURITY_WEP_40:                      // 1
        break;
    case WDRV_SECURITY_WEP_104:                     // 2
        break;
    case WDRV_SECURITY_WPA_AUTO_WITH_PASS_PHRASE:   // 3
        break;
    case 4:                                         // 4 - keep for backwards compatibility with MRF series modules
        param1 = WDRV_SECURITY_WPA_AUTO_WITH_PASS_PHRASE;
        break;
    case WDRV_SECURITY_WPS_PUSH_BUTTON:             // 6
        break;
    case WDRV_SECURITY_WPS_PIN:                     // 7
        break;
    default:
        return SCPI_RES_ERR;
    }
    
    pRunTimeWifiSettings->settings.wifi.securityMode = (uint8_t)param1;
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANPasskeySet(scpi_t * context)
{
    char value[WDRV_MAX_SECURITY_KEY_LENGTH + 1];
    size_t len = SCPI_SafeParamString(                                      \
                        context,                                            \
                        value,                                              \
                        WDRV_MAX_SECURITY_KEY_LENGTH, TRUE);
    
    DaqifiSettings * pRunTimeWifiSettings = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIME_WIFI_SETTINGS);
    
    if (len < 1)
    {
        return SCPI_RES_ERR;
    }
    
    // TODO: Additional validation (length?))
    switch (pRunTimeWifiSettings->settings.wifi.securityMode)
    {
    case WDRV_SECURITY_WEP_40:
    case WDRV_SECURITY_WEP_104:
    case WDRV_SECURITY_WPA_AUTO_WITH_PASS_PHRASE:
    case WDRV_SECURITY_WPS_PIN: 
        break;
    case WDRV_SECURITY_OPEN: // No Key
    case WDRV_SECURITY_WPS_PUSH_BUTTON: // No Key
    default:
        return SCPI_RES_ERR;
    }
    
    memcpy(pRunTimeWifiSettings->settings.wifi.passKey, value, len);
    pRunTimeWifiSettings->settings.wifi.passKeyLength = len;
    pRunTimeWifiSettings->settings.wifi.passKey[len] = '\0';
    
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANPasskeyCheck(scpi_t * context)
{
    char value[WDRV_MAX_SECURITY_KEY_LENGTH + 1];
    size_t len = SCPI_SafeParamString(                                      \
                        context,                                            \
                        value,                                              \
                        WDRV_MAX_SECURITY_KEY_LENGTH, TRUE);
    DaqifiSettings * pRunTimeWifiSettings = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIME_WIFI_SETTINGS);
    if (len < 1)
    {
        return SCPI_RES_ERR;
    }
    
    if (pRunTimeWifiSettings->settings.wifi.passKeyLength != len)
    {
        SCPI_ResultInt32(context, 0);
    }
    else if (len > 0 &&
        memcmp(pRunTimeWifiSettings->settings.wifi.passKey, value, len) != 0)
    {
        SCPI_ResultInt32(context, 0);
    }
    else
    {
        SCPI_ResultInt32(context, 1);
    }
    
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANSettingsApply(scpi_t * context)
{
    bool saveSettings = false;
    int param1;
    DaqifiSettings * pRunTimeWifiSettings = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIME_WIFI_SETTINGS);
    if (SCPI_ParamInt32(context, &param1, FALSE))
    {
        saveSettings = (bool)param1;
    }
    
    if (saveSettings)
    {
        if (!SaveNvmSettings(pRunTimeWifiSettings))
        {
            return SCPI_RES_ERR;
        }
    }
    
    if (!WifiApplyNetworkSettings(&pRunTimeWifiSettings->settings.wifi))
    {
        return SCPI_RES_ERR;
    }
    BoardData_Set(                                                          \
                            BOARDDATA_WIFI_SETTINGS,                        \
                            0,                                              \
                            pRunTimeWifiSettings);
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANSettingsSave(scpi_t * context)
{    
    DaqifiSettings * pWifiSettings =  BoardData_Get(                        \
                        BOARDDATA_WIFI_SETTINGS,                            \
                        0); 
    if (!SaveNvmSettings(pWifiSettings))
    {
        return SCPI_RES_ERR;
    }
    
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANSettingsLoad(scpi_t * context)
{
    bool applySettings = false;
    int param1;
    DaqifiSettings * pRunTimeWifiSettings = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIME_WIFI_SETTINGS);
    if (SCPI_ParamInt32(context, &param1, FALSE))
    {
        applySettings = (bool)param1;
    }
    
    if (!LoadNvmSettings(                                                   \
                        pRunTimeWifiSettings->type,                         \
                        pRunTimeWifiSettings))
    {
        return SCPI_RES_ERR;
    }
    
    if (applySettings)
    {
        if (!WifiApplyNetworkSettings(&pRunTimeWifiSettings->settings.wifi))
        {
            return SCPI_RES_ERR;
        }
        BoardData_Set(                                                      \
                        BOARDDATA_WIFI_SETTINGS,                            \
                        0,                                                  \
                        pRunTimeWifiSettings);
    }
    
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANSettingsFactoryLoad(scpi_t * context)
{
    bool applySettings = false;
    int param1;
    DaqifiSettings * pRunTimeWifiSettings = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIME_WIFI_SETTINGS);
    if (SCPI_ParamInt32(context, &param1, FALSE))
    {
        applySettings = (bool)param1;
    }
    
    if (!LoadFactorySettings(                                               \
                        pRunTimeWifiSettings->type,                         \
                        pRunTimeWifiSettings))
    {
        return SCPI_RES_ERR;
    }
    
    if (applySettings)
    {
        if (!WifiApplyNetworkSettings(&pRunTimeWifiSettings->settings.wifi))
        {
            return SCPI_RES_ERR;
        }
        BoardData_Set(                                                      \
                        BOARDDATA_WIFI_SETTINGS,                            \
                        0,                                                  \
                        pRunTimeWifiSettings);
    }
    
    return SCPI_RES_OK;
}

scpi_result_t SCPI_LANSettingsClear(scpi_t * context)
{
    DaqifiSettings * pWifiSettings = BoardData_Get(                         \
                        BOARDDATA_WIFI_SETTINGS,                            \
                        0); 
    if (ClearNvmSettings(pWifiSettings->type))
    {
        return SCPI_RES_ERR;
    }
    
    return SCPI_RES_OK;
}

////////
// Private implementations
////////

static scpi_result_t SCPI_LANAddrGetImpl(scpi_t * context, bool ipv6, IP_MULTI_ADDRESS* address)
{
    char buffer[MAX_TCPIPV6_LEN];

    if (IpAddr_ToString(ipv6, address, buffer, MAX_TCPIPV6_LEN) < 1)
    {
        return SCPI_RES_ERR;
    }
    
    SCPI_LANStringGetImpl(context, buffer);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_LANAddrSetImpl(scpi_t * context, bool ipv6, IP_MULTI_ADDRESS* address)
{
    char value[MAX_TCPIPV6_LEN + 1];
    size_t len = SCPI_SafeParamString(context, value, MAX_TCPIPV6_LEN, TRUE);
    if (len < 1)
    {
        return SCPI_RES_ERR;
    }
    
    bool addrType = IpAddr_IsIpv6(value, len);
    if (addrType != ipv6)
    {
        // Address type does not match system settings
        return SCPI_RES_ERR;
    }
    
    if (!IpAddr_FromString(value, len, address))
    {
        return SCPI_RES_ERR;
    }
    
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_LANStringGetImpl(scpi_t * context, const char* string)
{
    if (string == NULL)
    {
        return SCPI_RES_ERR;
    }
    
    SCPI_ResultMnemonic(context, string);
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_LANStringSetImpl(scpi_t * context, char* string, size_t maxLen)
{
    char value[128];
    size_t len = SCPI_SafeParamString(context, value, 127, TRUE);
    if (len < 1)
    {
        return SCPI_RES_ERR;
    }
    
    if (len > maxLen)
    {
        return SCPI_RES_ERR;
    }
    else if (len < 1)
    {
        string[0] = '\0';
    }
    else
    {
        memcpy(string, value, len);
        string[len] = '\0';
    }
    
    return SCPI_RES_OK;
}

static size_t SCPI_SafeParamString(scpi_t * context, char* value, const size_t maxLength, scpi_bool_t mandatory)
{
    const char* buffer;
    size_t len;
    if (!SCPI_ParamCharacters(context, &buffer, &len, mandatory))
    {
        return SCPI_RES_ERR;
    }
    
    if (len > 0)
    {
        if (len > maxLength)
        {
            return 0;
        }

        memcpy(value, buffer, len);
        value[len] = '\0';
    }
    
    return len;
}

scpi_result_t SCPI_LANAVSsidGet(scpi_t * context)
{
    uint8_t index = 0;
    DaqifiSettings * pWifiSettings = BoardData_Get(                         \
                        BOARDDATA_WIFI_SETTINGS,                            \
                        0); 
    uint8_t numberOfResults = pWifiSettings->settings.wifi.av_num;

    for(index = 0;index<numberOfResults;index++)
    {
        SCPI_LANStringGetImpl(                                              \
                context,                                                    \
                (const char*) pWifiSettings->settings.wifi.av_ssid[index]);
    }
    

    return SCPI_RES_OK;   
}

scpi_result_t SCPI_LANAVSsidScan(scpi_t * context)
{
    uint8_t index = 0;
    uint16_t numberOfResults = 0;
    WDRV_SCAN_RESULT scanResult;
    uint8_t maxSSIDs = MAX_AV_NETWORK_SSID;
    const uint8_t backSpace = 8;
    int32_t scanTimeout = 2000; // 1 second of timeout for network scan
    
    DaqifiSettings * pWifiSettings = BoardData_Get(                         \
                        BOARDDATA_WIFI_SETTINGS,                            \
                        0); 
    DaqifiSettings * pRunTimeWifiSettings = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIME_WIFI_SETTINGS);
    
    if( pRunTimeWifiSettings->settings.wifi.networkType != 1 ){
        context->interface->write( context, \
                (const char*) "No SSID found, WiFi needs to restart\r\n", \
                strlen( "No SSID found, WiFi needs to restart\r\n" ) );
        context->interface->flush(context);
        context->interface->write( context, \
                (const char*) "Please Wait\r\n", \
                strlen( "Please Wait\r\n" ) );
        context->interface->flush(context);
        
        pRunTimeWifiSettings->settings.wifi.networkType = 1;
        WifiApplyNetworkSettings(&pRunTimeWifiSettings->settings.wifi);
        WifiInit( &pRunTimeWifiSettings->settings.wifi );
        // After WiFi initialization with new settings, it is needed a timeout so
        // APP_Task is able to run the WiFi state machine
        uint32_t timeout = 0;
        char stringBuffer[ 4 ];
        while( timeout < 25000 ){
            if( timeout != 0 ){
                context->interface->write( context, (const char*) &backSpace, 1 );
                context->interface->write( context, (const char*) &backSpace, 1 );
            }
            if( timeout > 2500 ){
                context->interface->write( context, (const char*) &backSpace, 1 );
            }
            sprintf( stringBuffer, "%d%%", timeout / 250 );
            context->interface->write( context, \
                    (const char*) stringBuffer, \
                    strlen( stringBuffer ) );
            context->interface->flush(context);
            vTaskDelay( 250 );
            timeout += 250;
        }
        context->interface->write( context, (const char*) &backSpace, 1 );
        context->interface->write( context, (const char*) &backSpace, 1 );
        context->interface->write( context, (const char*) &backSpace, 1 );
        context->interface->write( context, "100%\r\n", strlen( "100%\r\n" ) );
        context->interface->write( context, \
                (const char*) "WiFi Reset Completed. Ready for SSID scan\r\n\r\n",\
                strlen( "WiFi Reset Completed. Ready for SSID scan\r\n\r\n" ) );
        context->interface->flush(context);
    }
    
    if( g_wdrv_scanStatus.scanInProgress ){
        context->interface->write( context, (const char*) "SCAN ALREADY IN PROGRESS!!\r\n", 1 );
        context->interface->flush(context);
        return SCPI_RES_ERR; // If scan is in progress, exit and return nothing
    }
    if (WDRV_ScanStart() == WDRV_SUCCESS)
    {
        do{
            context->interface->write( context, (const char*) "/", 1 );
            context->interface->flush(context);
            vTaskDelay( 15 );
            context->interface->write( context, (const char*) &backSpace, 1 );
            context->interface->write( context, (const char*) "-", 1 );
            context->interface->flush(context);
            vTaskDelay( 15 ); 
            context->interface->write( context, (const char*) &backSpace, 1 );
            context->interface->write( context, (const char*) "\\", 1 );
            context->interface->flush(context);
            vTaskDelay( 15 );
            context->interface->write( context, (const char*) &backSpace, 1 );
            context->interface->write( context, (const char*) "|", 1 );
            context->interface->flush(context);
            vTaskDelay( 15 );
            context->interface->write( context, (const char*) &backSpace, 1 );
            context->interface->write( context, (const char*) ".", 1 );
            context->interface->flush(context);
            scanTimeout -= 4* 15;
        }while( ( numberOfResults != maxSSIDs ) && ( scanTimeout > 0 ) );
        
        context->interface->write( context, (const char*) "\r\n", 2 );
        context->interface->flush(context);
        WDRV_EXT_CmdScanGet(&numberOfResults);
        if(numberOfResults>maxSSIDs) numberOfResults = maxSSIDs;
         // Set number of results in the global settings structure
        pWifiSettings->settings.wifi.av_num = numberOfResults;  
        
        for(index = 0;index<numberOfResults;index++)
        {          
            WDRV_EXT_ScanResultGet(index, &scanResult);
            /*
            if( WDRV_EXT_ScanResultGet(index, &scanResult) != 0 ){
                context->interface->write( context, "ERROR\r\n", strlen( "ERROR\r\n" ) );
                context->interface->flush(context);
                nm_drv_init();
                
                return SCPI_RES_ERR;
            } 
             */           
            
            if( ( scanResult.ssidLen == 0 ) || ( scanResult.ssidLen >= WDRV_MAX_SSID_LENGTH ) ){            
                continue;
            }                 
            // Store SSID 
            strcpy(     pWifiSettings->settings.wifi.av_ssid[index],         \
                        (const char*) scanResult.ssid); 
            
            context->interface->write(                                      \
                        context,                                            \
                        (const char*) scanResult.ssid,                      \
                        strlen( (const char*) scanResult.ssid ) );
            context->interface->write( context, (const char*) "\r\n", 2 );
            context->interface->flush(context);
            
            // Store strength
            pWifiSettings->settings.wifi.av_ssid_str[index] = scanResult.rssi;      
            // Store network type
            pWifiSettings->settings.wifi.av_networkType[index] =            \
                        scanResult.bssType;   
            // Store network security settings
            pWifiSettings->settings.wifi.av_securityMode[index] =           \
                        scanResult.apConfig; 
            vTaskDelay( 25 );
        }            
    }
    return SCPI_RES_OK;   
}
