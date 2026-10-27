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
     * #909: Re-stamp a loaded TopLevelSettings image with THIS build's
     * revision strings.
     *
     * Until #909 the bootloader erased the whole program flash on every
     * in-app update, so the TopLevel page never survived one: boot always
     * took the "load failed -> factory defaults" branch, and those defaults
     * stamp FIRMWARE_REVISION. The reported revision therefore advanced as a
     * SIDE EFFECT of the settings being destroyed. Now that the page
     * survives, a loaded image still carries the PREVIOUS build's strings,
     * and InitBoardConfig copies them straight into the live board config --
     * which is what CONF:CAPabilities:JSON?'s identity.firmware_rev reports.
     * Without this the device would report its old version forever after an
     * update.
     *
     * Lives here rather than in the caller so that FIRMWARE_REVISION /
     * HARDWARE_REVISION stay knowledge of this module, which is the only
     * place that stamps them today.
     *
     * IN MEMORY ONLY -- this writes no NVM, deliberately, and the caller must
     * not add a save on its behalf. daqifi_settings_SaveToNvm erases the
     * TopLevel page before writing it, so a save here would put a window on
     * the post-update boot in which a failed write leaves the page blank and
     * the next boot falls back to factory defaults -- losing exactly the
     * settings #909 exists to preserve. Nothing needs the write: boot re-stamps
     * on every startup, and SaveToNvm already re-stamps both revision strings
     * on any ordinary TopLevel save, so the persisted copy self-heals.
     *
     * @param pTopLevelSettings The loaded image to refresh; NULL is a no-op
     */
    void daqifi_settings_RefreshTopLevelRevisions(
            TopLevelSettings* pTopLevelSettings);

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
     * Answers whether an ADC calibration bank is loadable, WITHOUT installing
     * it into any runtime array.
     *
     * This is the first half of daqifi_settings_LoadADCCalSettings: that
     * function validates the stored record (daqifi_settings_LoadFromNvm) and
     * resolves the bank, then copies CalM/CalB into the caller's array in a
     * loop that cannot fail. Splitting the validation out lets a caller prove
     * a bank is good BEFORE it mutates anything, so a later step that can fail
     * -- persisting a selector to NVM -- cannot leave the runtime array and
     * NVM disagreeing.
     *
     * Deliberately a separate function rather than a NULL `channelRuntimeConfig`
     * overload on LoadADCCalSettings: a NULL passed by accident would silently
     * skip the install and still return true, which is the silent-permissive
     * shape this file should not grow.
     *
     * Its DaqifiSettings scratch lives in this function's own frame, so a
     * caller that validates and then installs pays max(), not sum(), of the two
     * frames -- unlike a scratch buffer held in the caller across both steps,
     * which costs MAX_AIN_RUNTIME_CHANNEL*2 doubles (768 B) for the whole call
     * including every successful one.
     *
     * @param type The calibration bank to check (FactAInCalParams or
     *             UserAInCalParams; any other type answers false)
     * @return True if the bank's stored record validates and the type names a
     *         calibration bank, false otherwise
     */
    bool daqifi_settings_ValidateADCCalBank(DaqifiSettingsType type);

    /**
     * Answers whether the COMMITTED top-level record in NVM already equals the
     * record a caller intended to persist.
     *
     * Exists because daqifi_settings_SaveToNvm can report failure over a write
     * that LANDED: it stages into the shared global gTempFflashBuffer and
     * nvm_WriteRowtoAddr's success test is a readback comparison against that
     * same buffer, so any concurrent settings writer on another transport can
     * make the comparison fail without touching the flash. This function reads
     * the medium independently -- LoadFromNvm into this function's own local,
     * never through the staging buffer -- and requires checksum validation, the
     * expected top-level type (which rides out of LoadFromNvm uncovered by the
     * checksum), and equality of the SELECTOR FIELD `calVals`.
     *
     * ⛔ Deliberately NOT whole-payload equality. Other fields of this record
     * have other legitimate owners, and SaveToNvm auto-captures
     * voltagePrecision and friendlyDeviceName on every save, so a payload
     * comparison answers a strictly stronger question than the caller asks and
     * is false whenever anyone else writes.
     *
     * ⛔ THIS IS NOT TRANSACTION SAFETY, and a caller must not claim it is.
     * Another top-level writer can overwrite the record after this returns; a
     * save prepared earlier elsewhere can land after the caller has returned;
     * the caller's own subsequent work may still re-read other pages; and a
     * save that HANGS never reaches this check at all. What it closes is the
     * specific demonstrated interleaving: a readback comparison corrupted by a
     * concurrent writer, over a write that succeeded.
     *
     * @param expected The record the caller handed to SaveToNvm. Only its
     *                 `settings.topLevelSettings.calVals` field is the
     *                 comparand -- see above (and
     *                 daqifi_settings.c:424-437, the implementation) for why
     *                 a whole-payload compare is deliberately NOT used.
     * @return True only if a valid top-level record is committed, its
     *         stored type is DaqifiSettings_TopLevelSettings, and its
     *         `calVals` equals `expected`'s. False on NULL, on no valid
     *         record, on an unexpected stored type, or on a DIFFERENT
     *         `calVals`. Every OTHER payload field (voltagePrecision,
     *         friendlyDeviceName, ...) is NOT compared, by design, so a
     *         difference there does not make this return false.
     */
    bool daqifi_settings_TopLevelMatchesCommitted(const DaqifiSettings* expected);

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

