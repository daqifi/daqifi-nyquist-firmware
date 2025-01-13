/* 
 * File:   DaqifiSettings.h
 * Author: Daniel
 *
 * Created on May 13, 2016, 12:09 PM
 */

#ifndef _DAQIFI_NVM_SETTINGS_H
#define	_DAQIFI_NVM_SETTINGS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include "configuration.h"
#include "definitions.h"
#include "wdrv_winc_common.h"
#include "wdrv_winc_authctx.h"
#include "../../state/runtime/AInRuntimeConfig.h"
#include "socket.h"
#include "wifi_services/wifi_manager.h"



//TODO(Daqifi): Add this back
//#include "../../state/runtime/AInRuntimeConfig.h"
#define DEFAULT_WIFI_NETWORK_MODE WIFI_MANAGER_NETWORK_MODE_AP
#define DEFAULT_WIFI_AP_SSID "DAQiFi" //TODO(Daqifi): Relocate in proper place
#define DEFAULT_WIFI_AP_SECURITY_MODE WIFI_MANAGER_SECURITY_MODE_OPEN //TODO(Daqifi): Relocate in proper place
#define DEFAULT_WIFI_WPA_PSK_PASSKEY "12345678" //TODO(Daqifi): Relocate in proper place
#define DEFAULT_NETWORK_IP_ADDRESS		"0.0.0.0" //TODO(Daqifi): Relocate in proper place
#define DEFAULT_NETWORK_IP_MASK	"255.255.255.0" //TODO(Daqifi): Relocate in proper place
#define DEFAULT_NETWORK_GATEWAY_IP_ADDRESS	"192.168.1.1" //TODO(Daqifi): Relocate in proper place
#define DEFAULT_TCP_PORT 9760 //TODO(Daqifi): Relocate in proper place
#define DEFAULT_NETWORK_HOST_NAME   "NYQUIST1"  //TODO(Daqifi): Relocate in proper place



#define BOARD_HARDWARE_REV  "2.0.0" //TODO(Daqifi) : Relocate to proper location
#define BOARD_FIRMWARE_REV "1.1.0"  //TODO(Daqifi) : Relocate to proper location
#define BOARD_VARIANT       1       //TODO(Daqifi) : Relocate to proper location
#define MAX_AV_NETWORK_SSID 8


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
#ifdef	__cplusplus
extern "C" {
#endif

    /**
     * Stores the master board settings
     */
    typedef struct sTopLevelSettings {
        /**
         * The type of board: 1=Nq1, 2=Nq2, 3=Nq3
         */
        uint8_t boardVariant;

        /**
         * The board hardware revision
         */
        char boardHardwareRev[16];

        /**
         * The board firmware revision
         */
        char boardFirmwareRev[16];

        /**
         * Selected calibration values: 0=factory, 1=user
         */
        bool calVals;

    } TopLevelSettings;

    

    /**
     * A polymorphic wrapper for daqifi settings
     */
    typedef union uDaqifiSettingsImpl {
        TopLevelSettings topLevelSettings;
        wifi_manager_settings_t wifi;
        AInCalArray factAInCalParams;
        AInCalArray userAInCalParams;

        // TODO: Other settings here
    } DaqifiSettingsImpl;

    /**
     * Defines the wifi settings
     */
    typedef enum eDaqifiSettingsType {
        DaqifiSettings_TopLevelSettings,
        DaqifiSettings_Wifi,
        DaqifiSettings_FactAInCalParams,
        DaqifiSettings_UserAInCalParams,
    } DaqifiSettingsType;

    /**
     * The wrapper for all Daqifi NVM settings
     */
    typedef struct sDaqifiSettings {
        /**
         * The MD5 Checksum of this structure. This is how the system determines whether the values are valid.
         */
        uint8_t md5Sum[CRYPT_MD5_DIGEST_SIZE];

        /**
         * The type of settings stored in this object
         */
        DaqifiSettingsType type;

        /**
         * The actual settings
         */
        DaqifiSettingsImpl settings;

    } DaqifiSettings;

    /**
     * Loads the specified NVM settings into the provided storage
     * @param type The type of settings to load
     * @param settings The location to store the settings
     * @return True on success, false otheriwse
     */
    bool daqifi_settings_LoadFromNvm(DaqifiSettingsType type, DaqifiSettings* settings);

    /**
     * Loads the specified factory default settings into the provided storage
     * @param type The type of settings to load
     * @param settings The location to store the settings
     * @return True on success, false otheriwse
     */
    bool daqifi_settings_LoadFactoryDeafult(DaqifiSettingsType type, DaqifiSettings* settings);

    /**
     * Saves the provided NVM settins
     * @param settings The settings to save
     * @return True on success, false otherwise
     */
    bool daqifi_settings_SaveToNvm(DaqifiSettings* settings);

    /**
     * Clears the provided settings type from NVM
     * @param type The type of settings to clear
     * @return True on success, false otherwise
     */
    bool daqifi_settings_ClearNvm(DaqifiSettingsType type);
    /**
     * Loads the ADC calibration settings
     * @param type The type of settings to load
     * @param channelRuntimeConfig Channel config into which to load the saved settings
     * @return True on success, false otherwise
     */
    bool daqifi_settings_LoadADCCalSettings(DaqifiSettingsType type, AInRuntimeArray* channelRuntimeConfig);

    /**
     * Saves the ADC calibration settings
     * @param type The type of settings to save
     * @param channelRuntimeConfig Channel config from which to save the settings
     * @return True on success, false otherwise
     */
    bool daqifi_settings_SaveADCCalSettings(DaqifiSettingsType type, AInRuntimeArray* channelRuntimeConfig);




#ifdef	__cplusplus
}
#endif

#endif	/* DAQIFISETTINGS_H */

