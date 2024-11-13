#include "daqifi_settings.h"
#include "wdrv_winc_client_api.h"
#include "HAL/NVM/nvm.h"

uint8_t gTempFflashBuffer[NVM_FLASH_ROWSIZE] __attribute__((coherent, aligned(16)));

bool daqifi_settings_LoadFromNvm(DaqifiSettingsType type, DaqifiSettings* settings) {

    // Read the settings into a temporary object
    DaqifiSettings tmpSettings;

    uint32_t address = 0;
    uint16_t dataSize = 0;


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



    // Copy settings from flash to temporary buffer
    memcpy(&tmpSettings, (uint32_t *) PA_TO_KVA1(address), sizeof (DaqifiSettings));

    // Calculate the MD5 sum
    uint8_t hash[CRYPT_MD5_DIGEST_SIZE];
    CRYPT_MD5_CTX md5Sum;
    CRYPT_MD5_Initialize(&md5Sum);
    CRYPT_MD5_DataAdd(&md5Sum, (const uint8_t*) &(tmpSettings.settings), dataSize);
    CRYPT_MD5_Finalize(&md5Sum, hash);

    // Compare the md5 sums for validity
    if (memcmp(hash, &tmpSettings.md5Sum, CRYPT_MD5_DIGEST_SIZE) != 0) {
        return false;
    }

    memcpy(settings, &tmpSettings, sizeof (DaqifiSettings));
    strcpy(settings->settings.topLevelSettings.boardHardwareRev, BOARD_HARDWARE_REV);
    strcpy(settings->settings.topLevelSettings.boardFirmwareRev, BOARD_FIRMWARE_REV);
    settings->settings.topLevelSettings.boardVariant = BOARD_VARIANT;
    return true;
}

bool daqifi_settings_LoadFactoryDeafult(DaqifiSettingsType type, DaqifiSettings* settings) {
    settings->type = type;

    switch (settings->type) {
        case DaqifiSettings_TopLevelSettings:
        {
            TopLevelSettings* pTopLevelSettings = &(settings->settings.topLevelSettings);
            pTopLevelSettings->calVals = 0;
            strcpy(pTopLevelSettings->boardHardwareRev, BOARD_HARDWARE_REV);
            strcpy(pTopLevelSettings->boardFirmwareRev, BOARD_FIRMWARE_REV);
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
            wifi->isEnabled = true;
            strncpy(wifi->ssid, DEFAULT_WIFI_AP_SSID, strlen(DEFAULT_WIFI_AP_SSID) + 1);
            wifi->ssid[WDRV_WINC_MAX_SSID_LEN] = '\0';
            wifi->securityMode = DEFAULT_WIFI_AP_SECURITY_MODE;
            strncpy(wifi->hostName, DEFAULT_NETWORK_HOST_NAME, strlen(DEFAULT_NETWORK_HOST_NAME) + 1);
            switch (wifi->securityMode) {
                case WIFI_API_SEC_WPA_AUTO_WITH_PASS_PHRASE:
                    strncpy((char*) wifi->passKey, DEFAULT_WIFI_WPA_PSK_PASSKEY, strlen(DEFAULT_WIFI_WPA_PSK_PASSKEY) + 1);
                    wifi->ssid[WDRV_WINC_PSK_LEN] = '\0';
                    break;
                case WIFI_API_SEC_OPEN:
                default:
                    memset(wifi->passKey, 0, WDRV_WINC_PSK_LEN);
                    break;
            }


            //mac address will be populated after reading from ATWINC
            wifi->networkMode = DEFAULT_WIFI_NETWORK_MODE;

            memset(&wifi->macAddr, 0, sizeof (WDRV_WINC_MAC_ADDR));

            wifi->ipAddr.Val = inet_addr(DEFAULT_NETWORK_IP_ADDRESS);
            wifi->ipMask.Val = inet_addr(DEFAULT_NETWORK_IP_MASK);
            wifi->gateway.Val = inet_addr(DEFAULT_NETWORK_GATEWAY_IP_ADDRESS);
            wifi->ssid[WIFI_MANAGER_DNS_CLIENT_MAX_HOSTNAME_LEN] = '\0';

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
            strcpy(settings->settings.topLevelSettings.boardHardwareRev, BOARD_HARDWARE_REV);
            strcpy(settings->settings.topLevelSettings.boardFirmwareRev, BOARD_FIRMWARE_REV);
            settings->settings.topLevelSettings.boardVariant = BOARD_VARIANT;
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
            dataSize = WIFI_SETTINGS_SIZE;
            dataSize = sizeof (wifi_manager_settings_t);
            break;
        default:
            return false;
    }

    if (!daqifi_settings_ClearNvm(settings->type)) {
        return false;
    }

    // Calculate the MD5 sum
    CRYPT_MD5_CTX md5Sum;
    CRYPT_MD5_Initialize(&md5Sum);
    CRYPT_MD5_DataAdd(&md5Sum, (const uint8_t*) &(settings->settings), dataSize);
    CRYPT_MD5_Finalize(&md5Sum, settings->md5Sum);

    // Check to see our settings can fit in one row
    if (sizeof (DaqifiSettings)>sizeof (gTempFflashBuffer)) {
        // TODO: Trap error/write to error log
        //TODO(Daqifi): Add this back
        //LogMessage("Settings save error. DaqifiSettings.c ln 275\n\r");
        return false;
    }

    // Copy the data to the non-buffered array
    memcpy(gTempFflashBuffer, settings, sizeof (DaqifiSettings));

    // Write the data
    return nvm_WriteRowtoAddr(address, gTempFflashBuffer);
}

bool daqifi_settings_ClearNvm(DaqifiSettingsType type) {
    uint32_t address = 0;
    //uint32_t dataSize = 0;

    switch (type) {
        case DaqifiSettings_TopLevelSettings:
            address = TOP_LEVEL_SETTINGS_ADDR;
            //dataSize = TOP_LEVEL_SETTINGS_SIZE;
            break;
        case DaqifiSettings_FactAInCalParams:
            address = FAINCAL_SETTINGS_ADDR;
            //dataSize = FAINCAL_SETTINGS_SIZE;
            break;
        case DaqifiSettings_UserAInCalParams:
            address = UAINCAL_SETTINGS_ADDR;
            //dataSize = UAINCAL_SETTINGS_SIZE;
            break;
        case DaqifiSettings_Wifi:
            address = WIFI_SETTINGS_ADDR;
            //dataSize = WIFI_SETTINGS_SIZE;
            break;
        default:
            return false;
    }



    // Check to see our settings can be erased in one page
    if (sizeof (DaqifiSettings) > NVM_FLASH_PAGESIZE) return false;

    // Erase the flash
    return nvm_ErasePage(address);
}

bool daqifi_settings_LoadADCCalSettings(DaqifiSettingsType type, AInRuntimeArray* channelRuntimeConfig)
{
    bool status = true;
    uint8_t x = 0;
    AInCalArray* calArray;
    // Read the settings into a temporary object
    DaqifiSettings tmpSettings;
    
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
    // Create the settings in a temporary object
    DaqifiSettings tmpSettings;
    
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