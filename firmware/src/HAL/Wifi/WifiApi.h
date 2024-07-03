/* 
 * File:   WifiApi.h
 * Author: Daniel
 *
 * Defines the publicly accessible API for the wifi portion of the application
 * 
 * Created on May 9, 2016, 2:55 PM
 */

#ifndef WIFIAPI_H
#define	WIFIAPI_H

#include <stdlib.h>
#include <string.h>

// Harmony
#include "configuration.h"
#include "definitions.h"

#include "state/data/BoardData.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "services/daqifi_settings.h"
#ifdef	__cplusplus
extern "C" {
#endif


    void WifiApi_ProcessStates();
    /**
     * Applies provided network settings (and resets the wifi connection)
     * @return True on success, false otherwise
     */
    bool WifiApi_Init(WifiSettings* settings);
    /**
     * Reset the wifi connection
     * NOTE: This does not apply new settings- call ApplyNetworkSettings to do that
     * @return True on success, false otherwise
     */
    bool WifiApi_UpdateNetworkSettings(WifiSettings* pSettings);
     /**
     * Brings the network connection down
     * @return 
     */
    bool WifiApi_Deinit();
    bool WifiApi_ReInit();
    




#ifdef	__cplusplus
}
#endif

#endif	/* WIFIAPI_H */

