#include "DaqifiSettings.h"

#include <string.h>

#include "tcpip/tcpip.h"
#include "driver/driver_common.h"
#include "driver/nvm/drv_nvm.h"
#include "driver/nvm/src/drv_nvm_local.h"
#include "tcpip/tcpip_mac.h"

#include "state/data/BoardData.h"
#include "Util/Logger.h"
#include "Util/StringFormatters.h"

#define UNUSED(x) (void)(x)

#define NVM_PROGRAM_UNLOCK_KEY1 0xAA996655
#define NVM_PROGRAM_UNLOCK_KEY2 0x556699AA

// We store settings at the end of program flash
/* Row size for MZ device is 2Kbytes */
/* Page size for MZ device is 16Kbytes */

#define KSEG0_PROGRAM_MEM_BASE_END __KSEG0_PROGRAM_MEM_BASE + __KSEG0_PROGRAM_MEM_LENGTH
#define RESERVED_SETTINGS_SPACE (128*1024)  // 128kB
#define RESERVED_SETTINGS_ADDR KSEG0_PROGRAM_MEM_BASE_END - RESERVED_SETTINGS_SPACE // RESERVED_SETTINGS_SPACE from end of prog memory (0x9D1E0000))

#define TOP_LEVEL_SETTINGS_ADDR RESERVED_SETTINGS_ADDR
#define TOP_LEVEL_SETTINGS_SIZE DRV_FLASH_PAGE_SIZE  // 16KB allotment - uses ~512 bytes

#define WIFI_SETTINGS_ADDR TOP_LEVEL_SETTINGS_ADDR + TOP_LEVEL_SETTINGS_SIZE
#define WIFI_SETTINGS_SIZE DRV_FLASH_PAGE_SIZE  // 16KB allotment - uses ~512 bytes

#define FAINCAL_SETTINGS_ADDR WIFI_SETTINGS_ADDR + WIFI_SETTINGS_SIZE    
#define FAINCAL_SETTINGS_SIZE DRV_FLASH_PAGE_SIZE // 16KB allotment - uses ~512 bytes

#define UAINCAL_SETTINGS_ADDR FAINCAL_SETTINGS_ADDR + FAINCAL_SETTINGS_SIZE
#define UAINCAL_SETTINGS_SIZE DRV_FLASH_PAGE_SIZE // 16KB allotment - uses ~512 bytes

// Cannot figure out how to get the compiler to refrain from loading these with 0s - so we mustn't use the variable initializers
// tried the noload attribute but that doesn't seem to work - assuming because they are globals?
//const uint8_t __attribute__((space(prog),address(TOP_LEVEL_SETTINGS_ADDR))) TOP_LEVEL_SETTINGS[TOP_LEVEL_SETTINGS_SIZE];
//const uint8_t __attribute__((space(prog),address(WIFI_SETTINGS_ADDR))) WIFI_SETTINGS[WIFI_SETTINGS_SIZE];
//const uint8_t __attribute__((space(prog),address(FAINCAL_SETTINGS_ADDR))) FAINCAL_SETTINGS[FAINCAL_SETTINGS_SIZE];
//const uint8_t __attribute__((space(prog),address(UAINCAL_SETTINGS_ADDR))) UAINCAL_SETTINGS[FAINCAL_SETTINGS_SIZE];
//const uint8_t __attribute__((space(prog),address(UNUSED_SETTINGS_ADDR))) UNUSED_SETTINGS[UNUSED_SETTINGS_SIZE]; 

/* Array in the KSEG1 RAM to store the data */
uint8_t temp_flash_buffer[DRV_FLASH_ROW_SIZE] __attribute__((coherent, aligned(16)));

bool LoadNvmSettings(DaqifiSettingsType type, DaqifiSettings* settings)
{
    
    // Read the settings into a temporary object
    DaqifiSettings tmpSettings;
    
    uint32_t address = 0;
    uint16_t dataSize = 0;
    uint16_t memSize = 0;
    
    switch(type)
    {
    case DaqifiSettings_TopLevelSettings:
        address = TOP_LEVEL_SETTINGS_ADDR;
        memSize = TOP_LEVEL_SETTINGS_SIZE;
        dataSize = sizeof(TopLevelSettings);
        break;        
    case DaqifiSettings_FactAInCalParams:
        address = FAINCAL_SETTINGS_ADDR;
        memSize = FAINCAL_SETTINGS_SIZE;
        dataSize = sizeof(AInCalArray);
        break;
    case DaqifiSettings_UserAInCalParams:
        address = UAINCAL_SETTINGS_ADDR;
        memSize = UAINCAL_SETTINGS_SIZE;
        dataSize = sizeof(AInCalArray);
        break;
    case DaqifiSettings_Wifi:
        address = WIFI_SETTINGS_ADDR;
        memSize = WIFI_SETTINGS_SIZE;
        dataSize = sizeof(WifiSettings);
        break;
    default:
        return false;
    }

    UNUSED(memSize);
    
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

bool LoadFactorySettings(DaqifiSettingsType type, DaqifiSettings* settings)
{
    settings->type = type;
    
    switch(settings->type)
    {
    case DaqifiSettings_TopLevelSettings:
    {
        TopLevelSettings* topLevelSettings = &(settings->settings.topLevelSettings);
        // Factory settings should not change the variant, or revisions
//        topLevelSettings->boardVariant = 1;
//        topLevelSettings->boardHardwareRev = 1.0;        
//        topLevelSettings->boardFirmwareRev = 1.0;
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
                
        if (strncmp(TCPIP_NETWORK_DEFAULT_POWER_MODE_IDX0, TCPIP_STACK_IF_POWER_FULL, 4) == 0)
        {
            wifi->isEnabled = true;
        }
        
        wifi->networkType = WDRV_DEFAULT_NETWORK_TYPE;
        strncpy(wifi->ssid, WDRV_DEFAULT_SSID, WDRV_MAX_SSID_LENGTH);
        wifi->securityMode =  WDRV_DEFAULT_SECURITY_MODE;
        switch (wifi->securityMode)
        {
        case WDRV_SECURITY_WEP_40:
            strncpy((char*)wifi->passKey, WDRV_DEFAULT_WEP_KEYS_40, WDRV_MAX_SECURITY_KEY_LENGTH);
            break;
        case WDRV_SECURITY_WEP_104:
            strncpy((char*)wifi->passKey, WDRV_DEFAULT_WEP_KEYS_104, WDRV_MAX_SECURITY_KEY_LENGTH);
            break;
        case WDRV_SECURITY_WPA_AUTO_WITH_PASS_PHRASE:
            strncpy((char*)wifi->passKey, WDRV_DEFAULT_PSK_PHRASE, WDRV_MAX_SECURITY_KEY_LENGTH);
            break;
        case WDRV_SECURITY_WPS_PUSH_BUTTON:
        case WDRV_SECURITY_WPS_PIN:
            strncpy((char*)wifi->passKey, WDRV_DEFAULT_WPS_PIN, WDRV_MAX_SECURITY_KEY_LENGTH);
            break;
        case WDRV_SECURITY_OPEN:
        default:
            memset(wifi->passKey, 0, WDRV_MAX_SECURITY_KEY_LENGTH);
            break;
        }
        
        wifi->configFlags = TCPIP_NETWORK_DEFAULT_INTERFACE_FLAGS_IDX0;
        const char* macAddrPtr = TCPIP_NETWORK_DEFAULT_MAC_ADDR_IDX0;
        if (macAddrPtr == NULL)
        {
            memset(&wifi->macAddr, 0, sizeof(TCPIP_MAC_ADDR));
        }
        else
        {
            
            MacAddr_FromString(TCPIP_NETWORK_DEFAULT_MAC_ADDR_IDX0,
                    strlen(macAddrPtr),
                    &wifi->macAddr);
        }
        
        IpAddr_FromString(TCPIP_NETWORK_DEFAULT_IP_ADDRESS_IDX0,
                strlen(TCPIP_NETWORK_DEFAULT_IP_ADDRESS_IDX0),
                &wifi->ipAddr);
        IpAddr_FromString(TCPIP_NETWORK_DEFAULT_IP_MASK_IDX0,
                strlen(TCPIP_NETWORK_DEFAULT_IP_MASK_IDX0),
                &wifi->ipMask);
        IpAddr_FromString(TCPIP_NETWORK_DEFAULT_GATEWAY_IDX0,
                strlen(TCPIP_NETWORK_DEFAULT_GATEWAY_IDX0),
                &wifi->gateway);
        IpAddr_FromString(TCPIP_NETWORK_DEFAULT_DNS_IDX0,
                strlen(TCPIP_NETWORK_DEFAULT_DNS_IDX0),
                &wifi->priDns);
        IpAddr_FromString(TCPIP_NETWORK_DEFAULT_SECOND_DNS_IDX0,
                strlen(TCPIP_NETWORK_DEFAULT_SECOND_DNS_IDX0),
                &wifi->secDns);
        
        strncpy(wifi->hostName, TCPIP_NETWORK_DEFAULT_HOST_NAME_IDX0, TCPIP_DNS_CLIENT_MAX_HOSTNAME_LEN);
        
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

bool SaveNvmSettings(DaqifiSettings* settings)
{
    uint32_t address = 0;
    uint32_t dataSize = 0;
    uint32_t memSize = 0;
    
    switch(settings->type)
    {
    case DaqifiSettings_TopLevelSettings:
        address = TOP_LEVEL_SETTINGS_ADDR;
        memSize = TOP_LEVEL_SETTINGS_SIZE;
        dataSize = sizeof(TopLevelSettings);
        break;        
    case DaqifiSettings_FactAInCalParams:
        address = FAINCAL_SETTINGS_ADDR;
        memSize = FAINCAL_SETTINGS_SIZE;
        dataSize = sizeof(AInCalArray);
        break;
    case DaqifiSettings_UserAInCalParams:
        address = UAINCAL_SETTINGS_ADDR;
        memSize = UAINCAL_SETTINGS_SIZE;
        dataSize = sizeof(AInCalArray);
        break;
    case DaqifiSettings_Wifi:
        address = WIFI_SETTINGS_ADDR;
        dataSize = WIFI_SETTINGS_SIZE;
        dataSize = sizeof(WifiSettings);
        break;
    default:
        return false;
    }
    
    if (!ClearNvmSettings(settings->type))
    {
        return false;
    }
    
    UNUSED(memSize);
    
    // Calculate the MD5 sum
    CRYPT_MD5_CTX md5Sum;
    CRYPT_MD5_Initialize(&md5Sum);
    CRYPT_MD5_DataAdd(&md5Sum, (const uint8_t*)&(settings->settings), dataSize);
    CRYPT_MD5_Finalize(&md5Sum, settings->md5Sum);
    
        // Check to see our settings can fit in one row
    if (sizeof(DaqifiSettings)>sizeof(temp_flash_buffer))
    {
        // TODO: Trap error/write to error log
        LogMessage("Settings save error. DaqifiSettings.c ln 275\n\r");
        return false;
    }
    
    // Copy the data to the non-buffered array
    memcpy(temp_flash_buffer, settings, sizeof(DaqifiSettings));
    
    // Write the data
    return WriteRowtoAddr(address, temp_flash_buffer);
}

bool ClearNvmSettings(DaqifiSettingsType type)
{
    uint32_t address = 0;
    uint32_t dataSize = 0;
    
    switch(type)
    {
    case DaqifiSettings_TopLevelSettings:
        address = TOP_LEVEL_SETTINGS_ADDR;
        dataSize = TOP_LEVEL_SETTINGS_SIZE;
        break;        
    case DaqifiSettings_FactAInCalParams:
        address = FAINCAL_SETTINGS_ADDR;
        dataSize = FAINCAL_SETTINGS_SIZE;
        break;
    case DaqifiSettings_UserAInCalParams:
        address = UAINCAL_SETTINGS_ADDR;
        dataSize = UAINCAL_SETTINGS_SIZE;
        break;
    case DaqifiSettings_Wifi:
        address = WIFI_SETTINGS_ADDR;
        dataSize = WIFI_SETTINGS_SIZE;
        break;
    default:
        return false;
    }
    
    UNUSED(dataSize);
    
    // Check to see our settings can be erased in one page
    if (sizeof(DaqifiSettings) > DRV_FLASH_PAGE_SIZE) return false;
    
    // Erase the flash
    return ErasePage(address);
}

bool LoadADCCalSettings(DaqifiSettingsType type, AInRuntimeArray* channelRuntimeConfig)
{
    bool status = true;
    uint8_t x = 0;
    AInCalArray* calArray;
    // Read the settings into a temporary object
    DaqifiSettings tmpSettings;
    
    status = LoadNvmSettings(type, &tmpSettings);
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

bool SaveADCCalSettings(DaqifiSettingsType type, AInRuntimeArray* channelRuntimeConfig)
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
    status = SaveNvmSettings(&tmpSettings);
    return status;
}

bool ErasePage(uint32_t addr)
{
    uint32_t interruptStatus;     // storage for current Interrupt Enable state
    uint32_t dmaStatus;           // storage for current DMA state
    DRV_HANDLE flashHandle;    
    
    UNUSED(flashHandle);   // Apparently the flash functions don't actually use this internally!
    
    // Disable interrupts
    interruptStatus = PLIB_INT_GetStateAndDisable( INT_ID_0 );
    
    // Disable DMA
    if(!(dmaStatus=DMACONbits.SUSPEND))
    {
        DMACONSET=_DMACON_SUSPEND_MASK; // suspend
        while((DMACONbits.DMABUSY)); // wait to be actually suspended
    }
    
    // Open driver
    flashHandle = DRV_FLASH_Open(DRV_FLASH_INDEX_0, intent);
    
    /* Erase the page which consist of the row to be written */
    DRV_FLASH_ErasePage(flashHandle, addr);
    while(DRV_FLASH_IsBusy(flashHandle));
   
    // Restore interrupts
    PLIB_INT_SetState(INT_ID_0, interruptStatus);
    
    // Restore DMA
    if(!dmaStatus)
    {
        DMACONCLR=_DMACON_SUSPEND_MASK; // resume DMA activity
    }
    
    if (PLIB_NVM_FlashRead(NVM_ID_0, (uint32_t) PA_TO_KVA1(addr))==0xFFFFFFFF)
    {
        return true;
    }
    else
    {
        // TODO: Trap error/write to error log
        LogMessage("Settings erase error. DaqifiSettings.c ln 445\n\r");
        return false;
    }
}

bool WriteWordtoAddr(uint32_t addr, uint32_t data)
{
    uint32_t interruptStatus;     // storage for current Interrupt Enable state
    uint32_t dmaStatus;           // storage for current DMA state
    DRV_HANDLE flashHandle;    
    
    UNUSED(flashHandle);   // Apparently the flash functions don't actually use this internally!    
    
    // Disable interrupts
    interruptStatus = PLIB_INT_GetStateAndDisable( INT_ID_0 );
    
    // Disable DMA
    if(!(dmaStatus=DMACONbits.SUSPEND))
    {
        DMACONSET=_DMACON_SUSPEND_MASK; // suspend
        while((DMACONbits.DMABUSY)); // wait to be actually suspended
    }
    
    // Open driver
    flashHandle = DRV_FLASH_Open(DRV_FLASH_INDEX_0, intent);
    
    /* Write a word of data to address, using data array as the source */
    DRV_FLASH_WriteWord(flashHandle, addr, data);
    while(DRV_FLASH_IsBusy(flashHandle));
    
    // Restore interrupts
    PLIB_INT_SetState(INT_ID_0, interruptStatus);
    
    // Restore DMA
    if(!dmaStatus)
    {
        DMACONCLR=_DMACON_SUSPEND_MASK; // resume DMA activity
    }
    
    
    if (!memcmp(&data, (uint32_t *) PA_TO_KVA1(addr), sizeof(data)))
    {
        return true;
    }
    else
    {
        // TODO: Trap error/write to error log
        LogMessage("Settings write error. DaqifiSettings.c ln 492\n\r");
        return false;
    }
}

uint32_t ReadfromAddr(uint32_t addr)
{
    return PLIB_NVM_FlashRead(NVM_ID_0, (uint32_t)PA_TO_KVA1(addr));
}

bool WriteRowtoAddr(uint32_t addr, uint8_t rowdata[])
{
    uint32_t interruptStatus;     // storage for current Interrupt Enable state
    uint32_t dmaStatus;           // storage for current DMA state
    DRV_HANDLE flashHandle;

    UNUSED(flashHandle);   // Apparently the flash functions don't actually use this internally!
    
    // Disable interrupts
    interruptStatus = PLIB_INT_GetStateAndDisable( INT_ID_0 );
    
    // Disable DMA
    if(!(dmaStatus=DMACONbits.SUSPEND))
    {
        DMACONSET=_DMACON_SUSPEND_MASK; // suspend
        while((DMACONbits.DMABUSY)); // wait to be actually suspended
    }
    
    // Open driver
    flashHandle = DRV_FLASH_Open(DRV_FLASH_INDEX_0, intent);

//    LOWER_BOOT_ALIAS_PAGE0  0xBFC00000
//    LOWER_BOOT_ALIAS_PAGE1  0xBFC04000
//    LOWER_BOOT_ALIAS_PAGE2  0xBFC08000
//    LOWER_BOOT_ALIAS_PAGE3  0xBFC0C000
//    LOWER_BOOT_ALIAS_PAGE4  0xBFC10000
    
//    UPPER_BOOT_ALIAS_PAGE0  0xBFC20000
//    UPPER_BOOT_ALIAS_PAGE1  0xBFC24000
//    UPPER_BOOT_ALIAS_PAGE2  0xBFC28000
//    UPPER_BOOT_ALIAS_PAGE3  0xBFC2C000
//    UPPER_BOOT_ALIAS_PAGE4  0xBFC30000    

//    Uncomment to write to boot flash!    
//    PLIB_NVM_FlashWriteKeySequence(NVM_ID_0, NVM_PROGRAM_UNLOCK_KEY1);
//    PLIB_NVM_FlashWriteKeySequence(NVM_ID_0, NVM_PROGRAM_UNLOCK_KEY2);
//    PLIB_NVM_BootPageWriteProtectionDisable(NVM_ID_0, UPPER_BOOT_ALIAS_PAGE0);
//    PLIB_NVM_FlashWriteKeySequence(NVM_ID_0, NVM_PROGRAM_UNLOCK_KEY1);
//    PLIB_NVM_FlashWriteKeySequence(NVM_ID_0, NVM_PROGRAM_UNLOCK_KEY2);
//    PLIB_NVM_BootPageWriteProtectionDisable(NVM_ID_0, UPPER_BOOT_ALIAS_PAGE1);
//    PLIB_NVM_FlashWriteKeySequence(NVM_ID_0, NVM_PROGRAM_UNLOCK_KEY1);
//    PLIB_NVM_FlashWriteKeySequence(NVM_ID_0, NVM_PROGRAM_UNLOCK_KEY2);
//    PLIB_NVM_BootPageWriteProtectionDisable(NVM_ID_0, UPPER_BOOT_ALIAS_PAGE2);
//    PLIB_NVM_FlashWriteKeySequence(NVM_ID_0, NVM_PROGRAM_UNLOCK_KEY1);
//    PLIB_NVM_FlashWriteKeySequence(NVM_ID_0, NVM_PROGRAM_UNLOCK_KEY2);
//    PLIB_NVM_BootPageWriteProtectionDisable(NVM_ID_0, UPPER_BOOT_ALIAS_PAGE3);
//    PLIB_NVM_FlashWriteKeySequence(NVM_ID_0, NVM_PROGRAM_UNLOCK_KEY1);
//    PLIB_NVM_FlashWriteKeySequence(NVM_ID_0, NVM_PROGRAM_UNLOCK_KEY2);
//    PLIB_NVM_BootPageWriteProtectionDisable(NVM_ID_0, UPPER_BOOT_ALIAS_PAGE4);
    
    /* Write a word of data to address, using data array as the source */
    DRV_FLASH_WriteRow(flashHandle, addr, (uint32_t *)rowdata);
    while(DRV_FLASH_IsBusy(flashHandle));
    
    // Restore interrupts
    PLIB_INT_SetState(INT_ID_0, interruptStatus);
    
    // Restore DMA
    if(!dmaStatus)
    {
        DMACONCLR=_DMACON_SUSPEND_MASK; // resume DMA activity
    }
    
    
    if (!memcmp(rowdata, (uint32_t *) PA_TO_KVA1(addr), DRV_FLASH_ROW_SIZE))
    {
        return true;
    }
    else
    {
        // TODO: Trap error/write to error log
        LogMessage("Settings write error. DaqifiSettings.c ln 573\n\r");
        return false;
    }
}