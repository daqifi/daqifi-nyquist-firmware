#pragma once

#include "libraries/scpi/libscpi/inc/scpi/scpi.h"

#ifdef	__cplusplus
extern "C" {
#endif
//TODO(Daqifi): Relocate for proper place
    
/**
 * SCPI Callback: Get the Enabled/Disabled status of LAN
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANEnabledGet(scpi_t * context);

/**
 * SCPI Callback: Set the Enabled/Disabled status of LAN
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANEnabledSet(scpi_t * context);

/**
 * SCPI Callback: Get the type of LAN network
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANNetModeGet(scpi_t * context);


/**
 * SCPI Callback: Set the type of LAN network
 *   Infrastructure: 1
 *   AD-Hoc: 2 (Invalid for now. Validation is TBD.)
 *   P2P: 3 (Invalid for now. Validation is TBD.)
 *   Soft AP: 4 (default)
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANNetModeSet(scpi_t * context);

/**
 * SCPI Callback: Get the Ip address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANAddrGet(scpi_t * context);

/**
 * SCPI Callback: Set the IP address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANAddrSet(scpi_t * context);

/**
 * SCPI Callback: Get the Ip mask of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANMaskGet(scpi_t * context);

/**
 * SCPI Callback: Set the IP mask of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANMaskSet(scpi_t * context);

/**
 * SCPI Callback: Get the Ip address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANGatewayGet(scpi_t * context);

/**
 * SCPI Callback: Set the IP address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANGatewaySet(scpi_t * context);


/**
 * SCPI Callback: Get the mac address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANMacGet(scpi_t * context);

/**
 * SCPI Callback: Set the hostname address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANHostnameGet(scpi_t * context);

/**
 * SCPI Callback: Set the hostname address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
//scpi_result_t SCPI_LANHostnameSet(scpi_t * context);

/**
 * SCPI Callback: Set the ssid address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANSsidGet(scpi_t * context);

/**
 * SCPI Callback: Set the ssid address of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANSsidSet(scpi_t * context);

scpi_result_t SCPI_LANSsidStrengthGet(scpi_t * context);

/**
 * SCPI Callback: Set the security mode of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANSecuritySet(scpi_t * context);
/**
 * SCPI Callback: Get the security mode of the device of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANSecurityGet(scpi_t * context);

/**
 * SCPI Callback: Set the passkey of the device
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANPasskeySet(scpi_t * context);

/**
 * SCPI Callback: Check the passkey of the device (for debugging)
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANPasskeyGet(scpi_t * context);

/**
 * SCPI Callback: Applies wifi settings of the device (optionally saving them)
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANSettingsApply(scpi_t * context);
/**
 * @brief
 * @param context
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANFwUpdate(scpi_t * context);
/**
 * @brief get the WI-FI Module information like CHIP-ID, Firmware version etc in Json format
 * @param context
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANGetChipInfo(scpi_t * context);
//scpi_result_t SCPI_LANAVSsidStrengthGet(scpi_t * context);
//
///**
// * SCPI Callback: Get the security mode of the device of the device
// * @return SCPI_RES_OK on success SCPI_RES_ERR on error
// */
//scpi_result_t SCPI_LANSecurityGet(scpi_t * context);
//
///**
// * SCPI Callback: Get the security mode of the available networks
// * @return SCPI_RES_OK on success SCPI_RES_ERR on error
// */
//scpi_result_t SCPI_LANAVSecurityGet(scpi_t * context);


/**
 * SCPI Callback: Saves the wifi settings of the device (but does not apply them)
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANSettingsSave(scpi_t * context);

/**
 * SCPI Callback: Loads the wifi settings of the device (but does not apply them)
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANSettingsLoad(scpi_t * context);

/**
 * SCPI Callback: Restores the wifi settings to the factory defaults (but does not apply them)
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANSettingsFactoryLoad(scpi_t * context);

/**
 * SCPI Callback: Clears the settings saved in memory, but does not overwrite the current in-memory values
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
scpi_result_t SCPI_LANSettingsClear(scpi_t * context);
//
///**
// * SCPI Callback: Returns SSIDs which were previously scanned with SCPI_LANAVSsidScan
// * @return 
// */
//scpi_result_t SCPI_LANAVSsidGet(scpi_t * context);
//
///**
// * SCPI Callback: Scans for SSIDs
// * @return 
// */
//scpi_result_t SCPI_LANAVSsidScan(scpi_t * context);

#ifdef	__cplusplus
}
#endif


