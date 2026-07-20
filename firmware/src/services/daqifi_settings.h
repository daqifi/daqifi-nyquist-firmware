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
#define DEFAULT_NETWORK_IP_ADDRESS		"192.168.1.1" //TODO(Daqifi): Relocate in proper place
#define DEFAULT_NETWORK_IP_MASK	"255.255.255.0" //TODO(Daqifi): Relocate in proper place
#define DEFAULT_NETWORK_GATEWAY_IP_ADDRESS	"192.168.1.1" //TODO(Daqifi): Relocate in proper place
#define DEFAULT_TCP_PORT 9760 //TODO(Daqifi): Relocate in proper place
#define DEFAULT_NETWORK_HOST_NAME   "DAQiFi"  //TODO(Daqifi): Relocate in proper place

// Version strings moved to version.h (single source of truth)
#define BOARD_VARIANT       1       //TODO(Daqifi) : Relocate to proper location
#define MAX_AV_NETWORK_SSID 8

// User-definable device friendly name (#14). Buffer size MUST match the
// protobuf field DaqifiOutMessage.friendly_device_name (char[32]); the
// stored string is always NUL-terminated, so the usable length is 31.
#define FRIENDLY_DEVICE_NAME_SIZE 32


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

        /**
         * Voltage output precision for CSV/JSON/SCPI encoders.
         * 0 = integer millivolts, 1-10 = volts with N decimal places.
         * Default: 4
         */
        uint8_t voltagePrecision;

        /**
         * #454: when true, auto-transition STANDBY → POWERED_UP whenever
         * VBUS (USB) is present.  Fires at boot (if USB is plugged in
         * at power-on) and on VBUS-rising edge mid-session.  Default
         * false for back-compat.
         */
        bool autoPowerOnUsb;

        /**
         * #14: user-definable device friendly name, emitted in the
         * protobuf/JSON info message when set (empty string = unset).
         * NUL-terminated; sized to match the proto field (char[32]).
         */
        char friendlyDeviceName[FRIENDLY_DEVICE_NAME_SIZE];

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
     * Size of the NVM integrity checksum field. #306: this used to hold a
     * 16-byte MD5 (wolfSSL); it now holds a 4-byte CRC32 (Util/CRC32) in the
     * first 4 bytes, with the remaining bytes zero. The field is kept at the
     * MD5 digest size so the on-NVM struct layout is byte-identical to pre-#306
     * releases — a device upgrading from MD5 firmware still reads its stored
     * settings via the CRC32-fails -> MD5-fallback path in daqifi_settings.c
     * (see daqifi_settings_LoadFromNvm). Decoupling this from wolfSSL entirely
     * (and dropping the MD5 fallback) is #306 phase 2, once all fielded devices
     * have re-saved with CRC32.
     */
#define DAQIFI_SETTINGS_CHECKSUM_SIZE CRYPT_MD5_DIGEST_SIZE

    /**
     * The wrapper for all Daqifi NVM settings
     */
    typedef struct sDaqifiSettings {
        /**
         * Integrity checksum of this structure's `settings` payload — how the
         * system determines whether stored values are valid. CRC32 since #306
         * (was MD5); see DAQIFI_SETTINGS_CHECKSUM_SIZE above.
         */
        uint8_t checksum[DAQIFI_SETTINGS_CHECKSUM_SIZE];

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

    /**
     * #14: Sets the runtime device friendly name cache. The value is
     * persisted to NVM only on the next TopLevelSettings save (which
     * captures this cache automatically). Truncated to fit
     * FRIENDLY_DEVICE_NAME_SIZE and always NUL-terminated. A NULL or
     * empty string clears the name.
     * @param name The friendly name to store (may be NULL to clear)
     */
    void daqifi_settings_SetFriendlyName(const char* name);

    /**
     * #625: True when @p name is a valid friendly name — printable ASCII
     * (0x20..0x7E) with no NUL-region overrun and none of the
     * JSON-structural characters '"' or '\\'. Used by the SCPI setter to
     * reject unsafe input before it can corrupt the JSON info message.
     * A NULL pointer or a field with no terminator returns false.
     * @param name Candidate name (scanned up to FRIENDLY_DEVICE_NAME_SIZE)
     * @return true if safe to store and emit, false otherwise
     */
    bool daqifi_settings_FriendlyNameIsValid(const char* name);

    /**
     * #14: Returns a pointer to the runtime device friendly name cache
     * (NUL-terminated; empty string when unset). Never returns NULL.
     */
    const char* daqifi_settings_GetFriendlyName(void);

    /**
     * #14: Seeds the runtime friendly-name cache from a loaded
     * TopLevelSettings blob (called at boot after NVM load).
     * @param name The NVM-stored name (may be empty)
     */
    void daqifi_settings_SeedFriendlyName(const char* name);


#ifdef	__cplusplus
}
#endif

#endif	/* DAQIFISETTINGS_H */

