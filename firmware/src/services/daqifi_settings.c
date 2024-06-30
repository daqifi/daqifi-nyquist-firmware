#include "daqifi_settings.h"
#include "wdrv_winc_client_api.h"
#include "HAL/NVM/nvm.h"

#define NVM_PROGRAM_UNLOCK_KEY1 0xAA996655
#define NVM_PROGRAM_UNLOCK_KEY2 0x556699AA

// We store settings at the end of program flash
/* Row size for MZ device is 2Kbytes */
/* Page size for MZ device is 16Kbytes */

#define KSEG0_PROGRAM_MEM_BASE_END __KSEG0_PROGRAM_MEM_BASE + __KSEG0_PROGRAM_MEM_LENGTH
#define RESERVED_SETTINGS_SPACE (128*1024)  // 128kB
#define RESERVED_SETTINGS_ADDR KSEG0_PROGRAM_MEM_BASE_END - RESERVED_SETTINGS_SPACE // RESERVED_SETTINGS_SPACE from end of prog memory (0x9D1E0000))

#define TOP_LEVEL_SETTINGS_ADDR RESERVED_SETTINGS_ADDR
#define TOP_LEVEL_SETTINGS_SIZE NVM_FLASH_PAGESIZE  // 16KB allotment - uses ~512 bytes

#define WIFI_SETTINGS_ADDR TOP_LEVEL_SETTINGS_ADDR + TOP_LEVEL_SETTINGS_SIZE
#define WIFI_SETTINGS_SIZE NVM_FLASH_PAGESIZE  // 16KB allotment - uses ~512 bytes

#define FAINCAL_SETTINGS_ADDR WIFI_SETTINGS_ADDR + WIFI_SETTINGS_SIZE    
#define FAINCAL_SETTINGS_SIZE NVM_FLASH_PAGESIZE // 16KB allotment - uses ~512 bytes

#define UAINCAL_SETTINGS_ADDR FAINCAL_SETTINGS_ADDR + FAINCAL_SETTINGS_SIZE
#define UAINCAL_SETTINGS_SIZE NVM_FLASH_PAGESIZE // 16KB allotment - uses ~512 bytes

uint8_t gTempFflashBuffer[NVM_FLASH_ROWSIZE] __attribute__((coherent, aligned(16)));

bool daqifi_settings_LoadFromNvm(DaqifiSettingsType type, DaqifiSettings* settings)
{
    
    // Read the settings into a temporary object
    DaqifiSettings tmpSettings;
    
    uint32_t address = 0;
    uint16_t dataSize = 0;
    
    
    switch(type)
    {
    case DaqifiSettings_TopLevelSettings:
        address = TOP_LEVEL_SETTINGS_ADDR;        
        dataSize = sizeof(TopLevelSettings);
        break;        
    case DaqifiSettings_FactAInCalParams:
        address = FAINCAL_SETTINGS_ADDR;     
        //TODO(Daqifi): Add this back
        //dataSize = sizeof(AInCalArray);
        break;
    case DaqifiSettings_UserAInCalParams:
        address = UAINCAL_SETTINGS_ADDR;    
        //TODO(Daqifi): Add this back
        //dataSize = sizeof(AInCalArray);
        break;
    case DaqifiSettings_Wifi:
        address = WIFI_SETTINGS_ADDR;
      
        dataSize = sizeof(WifiSettings);
        break;
    default:
        return false;
    }

   
    
    // Copy settings from flash to temporary buffer
    memcpy(&tmpSettings, (uint32_t *) PA_TO_KVA1(address), sizeof(DaqifiSettings));
    
    // Calculate the MD5 sum
    uint8_t hash[CRYPT_MD5_DIGEST_SIZE];
    CRYPT_MD5_CTX md5Sum;
    CRYPT_MD5_Initialize(&md5Sum);
    CRYPT_MD5_DataAdd(&md5Sum, (const uint8_t*)&(tmpSettings.settings), dataSize);
    CRYPT_MD5_Finalize(&md5Sum, hash);
    
    // Compare the md5 sums for validity
    if (memcmp(hash, &tmpSettings.md5Sum, CRYPT_MD5_DIGEST_SIZE) != 0)
    {
        return false;
    }
    
    memcpy(settings, &tmpSettings, sizeof(DaqifiSettings));
    
    return true;
}

bool daqifi_settings_LoadFactoryDeafult(DaqifiSettingsType type, DaqifiSettings* settings)
{
    settings->type = type;
    
    switch(settings->type)
    {
    case DaqifiSettings_TopLevelSettings:
    {
        TopLevelSettings* topLevelSettings = &(settings->settings.topLevelSettings);
        topLevelSettings->calVals = 0;
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
        WifiSettings* wifi = &(settings->settings.wifi);     
        wifi->isEnabled = true;
        strncpy(wifi->ssid, DEFAULT_WIFI_AP_SSID, strlen(DEFAULT_WIFI_AP_SSID)+1);
        wifi->ssid[WDRV_WINC_MAX_SSID_LEN]='\0';
        wifi->securityMode =  DEFAULT_WIFI_AP_SECURITY_MODE;
        switch (wifi->securityMode)
        {
        case WDRV_WINC_AUTH_TYPE_WPA_PSK:
            strncpy((char*)wifi->passKey, DEFAULT_WIFI_WPA_PSK_PASSKEY, strlen(DEFAULT_WIFI_WPA_PSK_PASSKEY)+1);
            wifi->ssid[WDRV_WINC_PSK_LEN]='\0';
            break;
        case WDRV_WINC_AUTH_TYPE_OPEN:
        default:
            memset(wifi->passKey, 0, WDRV_WINC_PSK_LEN);
            break;
        }
        
        
        //mac address will be populated after reading from ATWINC
        wifi->networkMode = DEFAULT_WIFI_NETWORK_MODE;
        
        memset(&wifi->macAddr, 0, sizeof(WDRV_WINC_MAC_ADDR));
       
        wifi->ipAddr.Val=inet_addr(DEFAULT_NETWORK_IP_ADDRESS);
        wifi->ipMask.Val=inet_addr(DEFAULT_NETWORK_IP_MASK);
        wifi->gateway.Val=inet_addr(DEFAULT_NETWORK_GATEWAY_IP_ADDRESS);
        wifi->ssid[DNS_CLIENT_MAX_HOSTNAME_LEN]='\0';
        
        wifi->tcpPort = 9760;
        break;
    }
    default:
        return false;
        break;
    }
    
    settings->type = type;
    
    return true;
}

bool daqifi_settings_SaveToNvm(DaqifiSettings* settings)
{
    uint32_t address = 0;
    uint32_t dataSize = 0;
       
    switch(settings->type)
    {
    case DaqifiSettings_TopLevelSettings:
        address = TOP_LEVEL_SETTINGS_ADDR;
        dataSize = sizeof(TopLevelSettings);
        break;        
    case DaqifiSettings_FactAInCalParams:
        address = FAINCAL_SETTINGS_ADDR;       
        //TODO(Daqifi): Add this back
        //dataSize = sizeof(AInCalArray);
        break;
    case DaqifiSettings_UserAInCalParams:
        address = UAINCAL_SETTINGS_ADDR;        
        //TODO(Daqifi): Add this back
        //dataSize = sizeof(AInCalArray);
        break;
    case DaqifiSettings_Wifi:
        address = WIFI_SETTINGS_ADDR;
        dataSize = WIFI_SETTINGS_SIZE;
        dataSize = sizeof(WifiSettings);
        break;
    default:
        return false;
    }
    
    if (!daqifi_settings_ClearNvm(settings->type))
    {
        return false;
    }
    
      
    // Calculate the MD5 sum
    CRYPT_MD5_CTX md5Sum;
    CRYPT_MD5_Initialize(&md5Sum);
    CRYPT_MD5_DataAdd(&md5Sum, (const uint8_t*)&(settings->settings), dataSize);
    CRYPT_MD5_Finalize(&md5Sum, settings->md5Sum);
    
        // Check to see our settings can fit in one row
    if (sizeof(DaqifiSettings)>sizeof(gTempFflashBuffer))
    {
        // TODO: Trap error/write to error log
         //TODO(Daqifi): Add this back
        //LogMessage("Settings save error. DaqifiSettings.c ln 275\n\r");
        return false;
    }
    
    // Copy the data to the non-buffered array
    memcpy(gTempFflashBuffer, settings, sizeof(DaqifiSettings));
    
    // Write the data
    return nvm_WriteRowtoAddr(address, gTempFflashBuffer);
}
bool daqifi_settings_ClearNvm(DaqifiSettingsType type)
{
    uint32_t address = 0;
    //uint32_t dataSize = 0;
    
    switch(type)
    {
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
    if (sizeof(DaqifiSettings) > NVM_FLASH_PAGESIZE) return false;
    
    // Erase the flash
    return nvm_ErasePage(address);
}