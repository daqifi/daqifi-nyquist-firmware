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
#include "services/daqifi_settings.h"
#include "tcpServer.h"
#ifdef	__cplusplus
extern "C" {
#endif

    void WifiApi_FormUdpAnnouncePacketCallback(WifiSettings *pSettings, uint8_t* pBuff, uint16_t *len);
    void WifiApi_ProcessState();
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
    size_t WifiApi_WriteToBuffer(const char* data, size_t len);


#ifdef	__cplusplus
}
#endif

#endif	/* WIFIAPI_H */

