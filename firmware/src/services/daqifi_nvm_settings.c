#include "daqifi_nvm_settings.h"

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

bool LoadNvmSettings(DaqifiSettingsType type, DaqifiSettings* settings)
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
        //TODO : Remove commented part
        //dataSize = sizeof(AInCalArray);
        break;
    case DaqifiSettings_UserAInCalParams:
        address = UAINCAL_SETTINGS_ADDR;    
        //TODO : Remove commented part
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