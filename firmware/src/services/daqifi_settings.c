#include "daqifi_settings.h"
#include "wdrv_winc_client_api.h"
#include "HAL/NVM/nvm.h"

uint8_t gTempFflashBuffer[NVM_FLASH_ROWSIZE] __attribute__((coherent, aligned(16)));

bool daqifi_settings_LoadFromNvm(DaqifiSettingsType type, DaqifiSettings* settings) {

    
    DaqifiSettings tmpSettings;
    uint32_t address = 0;
    uint16_t dataSize = 0;
    memset(&tmpSettings,0,sizeof(DaqifiSettings));

    switch (type) {
        case DaqifiSettings_TopLevelSettings:
            address = TOP_LEVEL_SETTINGS_ADDR;
            dataSize = sizeof (TopLevelSettings);
            break;
        case DaqifiSettings_FactAInCalParams:
            address = FAINCAL_SETTINGS_ADDR;
            dataSize = sizeof(AInCalArray);
            break;
        case DaqifiSettings_UserAInCalParams:
            address = UAINCAL_SETTINGS_ADDR;
            dataSize = sizeof(AInCalArray);
            break;
        case DaqifiSettings_Wifi:
            address = WIFI_SETTINGS_ADDR;
            dataSize = sizeof (wifi_manager_settings_t);
            break;
        default:
            return false;
    }
    memcpy(&tmpSettings, (uint32_t *) PA_TO_KVA1(address), sizeof (DaqifiSettings));

    uint8_t hash[CRYPT_MD5_DIGEST_SIZE];
    CRYPT_MD5_CTX md5Sum;
    CRYPT_MD5_Initialize(&md5Sum);
    CRYPT_MD5_DataAdd(&md5Sum, (const uint8_t*) &(tmpSettings.settings), dataSize);
    CRYPT_MD5_Finalize(&md5Sum, hash);

    if (memcmp(hash, &tmpSettings.md5Sum, CRYPT_MD5_DIGEST_SIZE) != 0) {
        return false;
    }

    memcpy(settings, &tmpSettings, sizeof (DaqifiSettings));
    return true;
}

bool daqifi_settings_LoadFactoryDeafult(DaqifiSettingsType type, DaqifiSettings* settings) {
    settings->type = type;

    switch (settings->type) {
        case DaqifiSettings_TopLevelSettings:
        {
            TopLevelSettings* pTopLevelSettings = &(settings->settings.topLevelSettings);
            const size_t hardwareRevSize = sizeof(pTopLevelSettings->boardHardwareRev);
            const size_t firmwareRevSize = sizeof(pTopLevelSettings->boardFirmwareRev);
            
            pTopLevelSettings->calVals = 0;
            
            strncpy(pTopLevelSettings->boardHardwareRev, BOARD_HARDWARE_REV, hardwareRevSize);
            pTopLevelSettings->boardHardwareRev[hardwareRevSize - 1] = '\0';
            
            strncpy(pTopLevelSettings->boardFirmwareRev, BOARD_FIRMWARE_REV, firmwareRevSize);
            pTopLevelSettings->boardFirmwareRev[firmwareRevSize - 1] = '\0';

            pTopLevelSettings->boardVariant = BOARD_VARIANT;
            break;
        }
        case DaqifiSettings_FactAInCalParams:
            return false;
            break;
        case DaqifiSettings_UserAInCalParams:
            return false;
            break;
        case DaqifiSettings_Wifi:
        {
            wifi_manager_settings_t* wifi = &(settings->settings.wifi);

            // Set isEnabled flag correctly
            wifi->isEnabled = true;

            // Safely copy SSID
            strncpy(wifi->ssid, DEFAULT_WIFI_AP_SSID, WDRV_WINC_MAX_SSID_LEN);
            wifi->ssid[WDRV_WINC_MAX_SSID_LEN - 1] = '\0'; // Ensure null termination

            // Set security mode
            wifi->securityMode = DEFAULT_WIFI_AP_SECURITY_MODE;

            // Safely copy Hostname
            strncpy(wifi->hostName, DEFAULT_NETWORK_HOST_NAME, WIFI_MANAGER_DNS_CLIENT_MAX_HOSTNAME_LEN);
            wifi->hostName[WIFI_MANAGER_DNS_CLIENT_MAX_HOSTNAME_LEN - 1] = '\0'; // Ensure null termination

            switch (wifi->securityMode) {
                case WIFI_MANAGER_SECURITY_MODE_WPA_AUTO_WITH_PASS_PHRASE:
                    // Safely copy Passkey
                    strncpy((char*) wifi->passKey, DEFAULT_WIFI_WPA_PSK_PASSKEY, WDRV_WINC_PSK_LEN);
                    wifi->passKey[WDRV_WINC_PSK_LEN - 1] = '\0'; // Ensure null termination
                    wifi->passKeyLength = strlen((const char*)wifi->passKey); // Set the correct length
                    break;
                case WIFI_MANAGER_SECURITY_MODE_OPEN:
                default:
                    memset(wifi->passKey, 0, WDRV_WINC_PSK_LEN);
                    wifi->passKeyLength = 0;
                    break;
            }


            //mac address will be populated after reading from ATWINC
            wifi->networkMode = DEFAULT_WIFI_NETWORK_MODE;
            wifi->isOtaModeEnabled=false;
            memset(&wifi->macAddr, 0, sizeof (WDRV_WINC_MAC_ADDR));
            wifi->ipAddr.Val = inet_addr(DEFAULT_NETWORK_IP_ADDRESS);
            wifi->ipMask.Val = inet_addr(DEFAULT_NETWORK_IP_MASK);
            wifi->gateway.Val = inet_addr(DEFAULT_NETWORK_GATEWAY_IP_ADDRESS);

            wifi->tcpPort = DEFAULT_TCP_PORT;
            break;
        }
        default:
            return false;
            break;
    }

    settings->type = type;

    return true;
}

bool daqifi_settings_SaveToNvm(DaqifiSettings* settings) {
    uint32_t address = 0;
    uint32_t dataSize = 0;

    switch (settings->type) {
        case DaqifiSettings_TopLevelSettings:
        {
            // By adding { and }, we create a new scope, making the declarations valid.
            const size_t hardwareRevSize = sizeof(settings->settings.topLevelSettings.boardHardwareRev);
            const size_t firmwareRevSize = sizeof(settings->settings.topLevelSettings.boardFirmwareRev);

            // Safely copy, truncating if the source macro is ever too long
            strncpy(settings->settings.topLevelSettings.boardHardwareRev, BOARD_HARDWARE_REV, hardwareRevSize);
            settings->settings.topLevelSettings.boardHardwareRev[hardwareRevSize - 1] = '\0';

            strncpy(settings->settings.topLevelSettings.boardFirmwareRev, BOARD_FIRMWARE_REV, firmwareRevSize);
            settings->settings.topLevelSettings.boardFirmwareRev[firmwareRevSize - 1] = '\0';

            settings->settings.topLevelSettings.boardVariant = BOARD_VARIANT;
            address = TOP_LEVEL_SETTINGS_ADDR;
            dataSize = sizeof (TopLevelSettings);
            break;
        }
        case DaqifiSettings_FactAInCalParams:
            address = FAINCAL_SETTINGS_ADDR;
            dataSize = sizeof(AInCalArray);
            break;
        case DaqifiSettings_UserAInCalParams:
            address = UAINCAL_SETTINGS_ADDR;
            dataSize = sizeof(AInCalArray);
            break;
        case DaqifiSettings_Wifi:
            address = WIFI_SETTINGS_ADDR;            
            dataSize = sizeof (wifi_manager_settings_t);
            break;
        default:
            return false;
    }

    if (!daqifi_settings_ClearNvm(settings->type)) {
        return false;
    }

    CRYPT_MD5_CTX md5Sum;
    CRYPT_MD5_Initialize(&md5Sum);
    CRYPT_MD5_DataAdd(&md5Sum, (const uint8_t*) &(settings->settings), dataSize);
    CRYPT_MD5_Finalize(&md5Sum, settings->md5Sum);

    if (sizeof (DaqifiSettings)>sizeof (gTempFflashBuffer)) {        
        return false;
    }
    memcpy(gTempFflashBuffer, settings, sizeof (DaqifiSettings));
    
    return nvm_WriteRowtoAddr(address, gTempFflashBuffer);
}

bool daqifi_settings_ClearNvm(DaqifiSettingsType type) {
    uint32_t address = 0;
    
    switch (type) {
        case DaqifiSettings_TopLevelSettings:
            address = TOP_LEVEL_SETTINGS_ADDR;       
            break;
        case DaqifiSettings_FactAInCalParams:
            address = FAINCAL_SETTINGS_ADDR;           
            break;
        case DaqifiSettings_UserAInCalParams:
            address = UAINCAL_SETTINGS_ADDR;           
            break;
        case DaqifiSettings_Wifi:
            address = WIFI_SETTINGS_ADDR;
            break;
        default:
            return false;
    }
    if (sizeof (DaqifiSettings) > NVM_FLASH_PAGESIZE) return false;

    return nvm_ErasePage(address);
}

bool daqifi_settings_LoadADCCalSettings(DaqifiSettingsType type, AInRuntimeArray* channelRuntimeConfig)
{
    bool status = true;
    uint8_t x = 0;
    AInCalArray* calArray;
    DaqifiSettings tmpSettings;
    memset(&tmpSettings,0,sizeof(DaqifiSettings));
    status = daqifi_settings_LoadFromNvm(type, &tmpSettings);
    if(!status) return status;
            
    switch(type)
    {
        case DaqifiSettings_FactAInCalParams:
        {
            calArray = &(tmpSettings.settings.factAInCalParams);
            break;
        }
        case DaqifiSettings_UserAInCalParams:
        {
            calArray = &(tmpSettings.settings.userAInCalParams);
            break;
        }
        default:
            status = false;
            break;
    }
    
    if(!status) return status;    
    
    for(x=0;x<channelRuntimeConfig->Size;x++)
    {
        channelRuntimeConfig->Data[x].CalM = calArray->Data[x].CalM;
        channelRuntimeConfig->Data[x].CalB = calArray->Data[x].CalB;
    }
    
    return status;
}

bool daqifi_settings_SaveADCCalSettings(DaqifiSettingsType type, AInRuntimeArray* channelRuntimeConfig)
{
    bool status = true;
    uint8_t x = 0;
    AInCalArray* calArray; 
    DaqifiSettings tmpSettings;
    memset(&tmpSettings,0,sizeof(DaqifiSettings));
    tmpSettings.type = type;
    
    switch(type)
    {
        case DaqifiSettings_FactAInCalParams:
        {
            calArray = &(tmpSettings.settings.factAInCalParams);
            break;
        }
        case DaqifiSettings_UserAInCalParams:
        {
            calArray = &(tmpSettings.settings.userAInCalParams);
            break;
        }
        default:
            status = false;
            break;
    }
    
    if(!status) return status;    
    
    for(x=0;x<channelRuntimeConfig->Size;x++)
    {
        calArray->Data[x].CalM = channelRuntimeConfig->Data[x].CalM;
        calArray->Data[x].CalB = channelRuntimeConfig->Data[x].CalB;
    }
    status = daqifi_settings_SaveToNvm(&tmpSettings);
    return status;
}
