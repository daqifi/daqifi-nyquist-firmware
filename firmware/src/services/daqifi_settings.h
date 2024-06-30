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

typedef enum {
    WIFI_API_NETWORK_MODE_AP = 0,
    WIFI_API_NETWORK_MODE_STA = 1

} WifiApi_networkMode_t;

//TODO(Daqifi): Add this back
//#include "../../state/runtime/AInRuntimeConfig.h"
#define DEFAULT_WIFI_NETWORK_MODE WIFI_API_NETWORK_MODE_AP
#define DEFAULT_WIFI_AP_SSID "DAQiFi" //TODO(Daqifi): Relocate in proper place
#define DEFAULT_WIFI_AP_SECURITY_MODE WDRV_WINC_AUTH_TYPE_OPEN //TODO(Daqifi): Relocate in proper place
#define DEFAULT_WIFI_WPA_PSK_PASSKEY "12345678" //TODO(Daqifi): Relocate in proper place
#define DEFAULT_NETWORK_IP_ADDRESS		"0.0.0.0" //TODO(Daqifi): Relocate in proper place
#define DEFAULT_NETWORK_IP_MASK	"255.255.255.0" //TODO(Daqifi): Relocate in proper place
#define DEFAULT_NETWORK_GATEWAY_IP_ADDRESS	"192.168.1.1" //TODO(Daqifi): Relocate in proper place
//#define DEFAULT_NETWORK_DEFAULT_DNS		"192.168.1.1" //TODO(Daqifi): Relocate in proper place
//#define DEFAULT_NETWORK_DEFAULT_SECOND_DNS	"0.0.0.0"  //TODO(Daqifi): Relocate in proper place
#define DEFAULT_NETWORK_HOST_NAME	    "NYQUIST"  //TODO(Daqifi): Relocate in proper place
#define DNS_CLIENT_MAX_HOSTNAME_LEN			32

#define MAX_AV_NETWORK_SSID 8

typedef union {
    uint32_t Val;
    uint16_t w[2];
    uint8_t v[4];
} IPV4_ADDR;


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
     * Stores the wifi settings
     */
    typedef struct sWifiSettings {
        /**
         * Toggles the power mode
         */
        bool isEnabled;

        /**
         * One of:
         * WIFI_API_NETWORK_MODE_AP = 0,
         * WIFI_API_NETWORK_MODE_STA=1
         */
        uint8_t networkMode;

        /**
         * The number of available networks after scan
         */
        uint8_t av_num;

        //        /**
        //         * The available network types
        //         */
        //        uint8_t av_networkType[MAX_AV_NETWORK_SSID];

        /**
         * The network ssid
         */
        char ssid[WDRV_WINC_MAX_SSID_LEN + 1];

        /**
         * The network ssid strength
         */
        uint8_t ssid_str;

        /**
         * The available network SSIDs
         */
        char av_ssid[MAX_AV_NETWORK_SSID][WDRV_WINC_MAX_SSID_LEN + 1]; // This should match the definition in protobuf

        /**
         * The available network ssid strengths
         */
        uint8_t av_ssid_str[MAX_AV_NETWORK_SSID];

        /**
         * One of:
         * 
         * WDRV_WINC_AUTH_TYPE_OPEN,
         * WDRV_WINC_AUTH_TYPE_WPA_PSK,
         *  
         */
        uint8_t securityMode;

        /**
         * The available network security modes
         */
        uint8_t av_securityMode[MAX_AV_NETWORK_SSID];

        /**
         * The length of the passkey
         */
        uint8_t passKeyLength;

        /**
         * The passkey for the given security type
         */
        uint8_t passKey[WDRV_WINC_PSK_LEN + 1];

        /**
         * The MAX Address
         */
        WDRV_WINC_MAC_ADDR macAddr;

        /**
         * The ip address
         */
        IPV4_ADDR ipAddr;

        /**
         * The ip mask
         */
        IPV4_ADDR ipMask;

        /**
         * The ip gateway
         */
        IPV4_ADDR gateway;

        /**
         * The primary dns
         */
        IPV4_ADDR priDns;


        /**
         * The port to open for incoming TCP connections
         */
        uint16_t tcpPort;

    } WifiSettings;

    /**
     * A polymorphic wrapper for daqifi settings
     */
    typedef union uDaqifiSettingsImpl {
        TopLevelSettings topLevelSettings;
        WifiSettings wifi;
        //AInCalArray factAInCalParams;
        //AInCalArray userAInCalParams;

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
    //TODO(Daqifi): Add  back LoadADCCalSettings AND SaveADCCalSettings
    //    /**
    //     * Loads the ADC calibration settings
    //     * @param type The type of settings to load
    //     * @param channelRuntimeConfig Channel config into which to load the saved settings
    //     * @return True on success, false otherwise
    //     */    
    //    bool LoadADCCalSettings(DaqifiSettingsType type, AInRuntimeArray* channelRuntimeConfig);
    //    
    //    /**
    //     * Saves the ADC calibration settings
    //     * @param type The type of settings to save
    //     * @param channelRuntimeConfig Channel config from which to save the settings
    //     * @return True on success, false otherwise
    //     */    
    //    bool SaveADCCalSettings(DaqifiSettingsType type, AInRuntimeArray* channelRuntimeConfig);
    //    



#ifdef	__cplusplus
}
#endif

#endif	/* DAQIFISETTINGS_H */

