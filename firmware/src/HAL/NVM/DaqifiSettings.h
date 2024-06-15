/* 
 * File:   DaqifiSettings.h
 * Author: Daniel
 *
 * Created on May 13, 2016, 12:09 PM
 */

#ifndef DAQIFISETTINGS_H
#define	DAQIFISETTINGS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include "system_config.h"
#include "system_definitions.h"
#include "crypto/crypto.h"
#include "../../state/runtime/AInRuntimeConfig.h"

#define MAX_AV_NETWORK_SSID 8

#ifdef	__cplusplus
extern "C" {
#endif
    
    /**
     * Stores the master board settings
     */
    typedef struct sTopLevelSettings
    {       
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
    typedef struct sWifiSettings
    {
        /**
         * Toggles the power mode
         */
        bool isEnabled;
        
        /**
         * One of:
         * 
         * WDRV_NETWORK_TYPE_INFRASTRUCTURE
         * WDRV_NETWORK_TYPE_ADHOC
         * WDRV_NETWORK_TYPE_P2P
         * WDRV_NETWORK_TYPE_SOFT_AP
         */
        uint8_t networkType;
        
        /**
         * The number of available networks after scan
         */
        uint8_t av_num;
        
        /**
         * The available network types
         */
        uint8_t av_networkType[MAX_AV_NETWORK_SSID];
        
        /**
         * The network ssid
         */
        char ssid[WDRV_MAX_SSID_LENGTH];
        
        /**
         * The network ssid strength
         */
        uint8_t ssid_str;
        
        /**
         * The available network SSIDs
         */
        char av_ssid[MAX_AV_NETWORK_SSID][WDRV_MAX_SSID_LENGTH];   // This should match the definition in protobuf
        
        /**
         * The available network ssid strengths
         */
        uint8_t av_ssid_str[MAX_AV_NETWORK_SSID];
        
        /**
         * One of:
         * 
         *   WDRV_SECURITY_OPEN                          0
         *   WDRV_SECURITY_WEP_40                        1
         *   WDRV_SECURITY_WEP_104                       2
         *   WDRV_SECURITY_WPA_AUTO_WITH_PASS_PHRASE     3
         *   WDRV_SECURITY_WPS_PUSH_BUTTON               6
         *   WDRV_SECURITY_WPS_PIN                       7
         */
        uint8_t securityMode;
        
        /**
         * The available network security modes
        */        
        uint8_t av_securityMode[MAX_AV_NETWORK_SSID];
        
        /**
         * One of:
         * 
         * WDRV_SECURITY_WEP_SHAREDKEY
         * WDRV_SECURITY_WEP_OPENKEY
         */
        uint8_t wepKeyType;
        
        /**
         * The index of the wep key
         */
        uint8_t wepKeyIndex;
        
        /**
         * The length of the passkey
         */
        uint8_t  passKeyLength;
        
        /**
         * The passkey for the given security type
         */
        uint8_t passKey[WDRV_MAX_SECURITY_KEY_LENGTH];
        
        /**
         * The MAX Address
         */
        TCPIP_MAC_ADDR macAddr;
        
        /**
         * IPV6 or IPV4
         * TCPIP_NETWORK_CONFIG_IP_STATIC = 0x0000
         * TCPIP_NETWORK_CONFIG_DHCP_CLIENT_ON = 0x0001
         * TCPIP_NETWORK_CONFIG_ZCLL_ON      = 0x0002
         * TCPIP_NETWORK_CONFIG_DHCP_SERVER_ON = 0x0004                                              
         * TCPIP_NETWORK_CONFIG_DNS_CLIENT_ON = 0x0008
         * TCPIP_NETWORK_CONFIG_DNS_SERVER_ON = 0x0010
         * TCPIP_NETWORK_CONFIG_IPV6_ADDRESS = 0x0100
         */
        TCPIP_NETWORK_CONFIG_FLAGS configFlags;
        
        /**
         * The ip address
         */
        IP_MULTI_ADDRESS ipAddr;
        
        /**
         * The ip mask
         */
        IP_MULTI_ADDRESS ipMask;
        
        /**
         * The ip gateway
         */
        IP_MULTI_ADDRESS gateway;
        
        /**
         * The primary dns
         */
        IP_MULTI_ADDRESS priDns;
        
        /**
         * The secondary dns
         */
        IP_MULTI_ADDRESS secDns;
        
        /**
         * The board hostname
         */
        char hostName[TCPIP_DNS_CLIENT_MAX_HOSTNAME_LEN];
        
        /**
         * The port to open for incoming TCP connections
         */
        uint16_t tcpPort;

    } WifiSettings;

    /**
     * A polymorphic wrapper for daqifi settings
     */
    typedef union uDaqifiSettingsImpl
    {
        TopLevelSettings topLevelSettings;
        WifiSettings wifi;
        AInCalArray factAInCalParams;
        AInCalArray userAInCalParams;

        // TODO: Other settings here
    } DaqifiSettingsImpl;
    
    /**
     * Defines the wifi settings
     */
    typedef enum eDaqifiSettingsType
    {
        DaqifiSettings_TopLevelSettings,
        DaqifiSettings_Wifi,
        DaqifiSettings_FactAInCalParams,
        DaqifiSettings_UserAInCalParams,
    } DaqifiSettingsType;
    
    /**
     * The wrapper for all Daqifi NVM settings
     */
    typedef struct sDaqifiSettings
    {
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
    bool LoadNvmSettings(DaqifiSettingsType type, DaqifiSettings* settings);
    
    /**
     * Loads the specified factory default settings into the provided storage
     * @param type The type of settings to load
     * @param settings The location to store the settings
     * @return True on success, false otheriwse
     */
    bool LoadFactorySettings(DaqifiSettingsType type, DaqifiSettings* settings);
    
    /**
     * Saves the provided NVM settins
     * @param settings The settings to save
     * @return True on success, false otherwise
     */
    bool SaveNvmSettings(DaqifiSettings* settings);
    
    /**
     * Clears the provided settings type from NVM
     * @param type The type of settings to clear
     * @return True on success, false otherwise
     */
    bool ClearNvmSettings(DaqifiSettingsType type);

    /**
     * Loads the ADC calibration settings
     * @param type The type of settings to load
     * @param channelRuntimeConfig Channel config into which to load the saved settings
     * @return True on success, false otherwise
     */    
    bool LoadADCCalSettings(DaqifiSettingsType type, AInRuntimeArray* channelRuntimeConfig);
    
    /**
     * Saves the ADC calibration settings
     * @param type The type of settings to save
     * @param channelRuntimeConfig Channel config from which to save the settings
     * @return True on success, false otherwise
     */    
    bool SaveADCCalSettings(DaqifiSettingsType type, AInRuntimeArray* channelRuntimeConfig);
    
    /**
     * Saves data to address specified (test/diagnostic function)
     * @param addr The address of flash to write to
     * @param data Word of data to be written
     * @return True on success, false otherwise
     */       
    bool WriteWordtoAddr(uint32_t addr, uint32_t data);
  
    /**
     * Reads data from address specified (test/diagnostic function)
     * @param addr The address of flash to read from
     * @return Data word read from NVM.
     */           
    uint32_t ReadfromAddr(uint32_t addr);
    
    /**
     * Erases a page of data from address specified (test/diagnostic function)
     * @param addr The address of flash to begin the page erase
     */  
    bool ErasePage(uint32_t addr);
    
     /**
     * Saves row of data to address specified
     * @param addr The address of flash to write to
     * @param rowdata[] Row of data to be written
     * @return True on success, false otherwise
     */  
    bool WriteRowtoAddr(uint32_t addr, uint8_t rowdata[]);


#ifdef	__cplusplus
}
#endif

#endif	/* DAQIFISETTINGS_H */

