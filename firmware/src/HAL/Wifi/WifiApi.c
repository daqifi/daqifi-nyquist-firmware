
#include "WifiApi.h"
#include "wdrv_winc_client_api.h"

//static WDRV_WINC_BSS_CONTEXT gBssCtx;
//static WDRV_WINC_AUTH_CONTEXT gAuthCtx;
//static WifiApi_dhcpServerConfig_t gDhcpServerConfig;
//static WifiApi_networkMode_t gNetworkMode;
#define UNUSED(x) (void)(x)
typedef enum {
    WIFIAPI_STATE_IDLE,
    WIFIAPI_STATE_INIT,
    WIFIAPI_STATE_INITIALIZING,
    WIFIAPI_STATE_REINITIALIZING,
    WIFIAPI_STATE_DEINITIALIZING,
    WIFIAPI_STATE_ERROR,

} WifiApi_state_t;

typedef struct {

    enum app_eventFlag {
        WIFIAPI_EVENT_FLAG_AP_STARTED = 1 << 0,
        WIFIAPI_EVENT_FLAG_STA_STARTED = 1 << 1,
        WIFIAPI_EVENT_FLAG_STA_CONNECTED = 1 << 2,
        WIFIAPI_EVENT_FLAG_STA_DISCONNECTED = 1 << 3
    } flag;
    uint16_t value;
} wifiApi_eventFlagState_t;



wifiApi_eventFlagState_t gEventFlags;
static DRV_HANDLE gWdrvHandle;
WifiSettings gWifiSettings;
WifiApi_state_t gWifiState;

static void SetEventFlag(wifiApi_eventFlagState_t *pEventFlagState, uint16_t flag) {
    pEventFlagState->value = pEventFlagState->value | flag;
}

static void ResetEventFlag(wifiApi_eventFlagState_t *pEventFlagState, uint16_t flag) {
    pEventFlagState->value = pEventFlagState->value & (~flag);
}

static uint8_t GetEventFlagStatus(wifiApi_eventFlagState_t eventFlagState, uint16_t flag) {
    if (eventFlagState.value & flag)
        return 1;
    else
        return 0;
}

static void ResetAllEventFlags(wifiApi_eventFlagState_t *pEventFlagState) {
    memset(pEventFlagState, 0, sizeof (wifiApi_eventFlagState_t));
}

static bool wifiDeinit() {
    if (WDRV_WINC_Status(sysObj.drvWifiWinc) != SYS_STATUS_UNINITIALIZED) {
        WDRV_WINC_Close(gWdrvHandle);
        WDRV_WINC_Deinitialize(0);
        SYS_CONSOLE_MESSAGE("WiFi going down.\r\n");
    }
    return true;
}
static void DhcpEventCallback(DRV_HANDLE handle, uint32_t ipAddress) {
    char s[20];
    UNUSED(s);
    SYS_CONSOLE_PRINT("STA Mode: Station IP address is %s\r\n", inet_ntop(AF_INET, &ipAddress, s, sizeof (s)));
}

static void ApEventCallback(DRV_HANDLE handle, WDRV_WINC_ASSOC_HANDLE assocHandle, WDRV_WINC_CONN_STATE currentState, WDRV_WINC_CONN_ERROR errorCode) {
    if (WDRV_WINC_CONN_STATE_CONNECTED == currentState) {
        SYS_CONSOLE_PRINT("AP mode: Station connected\r\n");
        SetEventFlag(&gEventFlags, WIFIAPI_EVENT_FLAG_STA_CONNECTED);
        ResetEventFlag(&gEventFlags, WIFIAPI_EVENT_FLAG_STA_DISCONNECTED);
    } else if (WDRV_WINC_CONN_STATE_DISCONNECTED == currentState) {
        SYS_CONSOLE_PRINT(appData.consoleHandle, "AP mode: Station disconnected\r\n");
        ResetEventFlag(&gEventFlags, WIFIAPI_EVENT_FLAG_STA_CONNECTED);
        SetEventFlag(&gEventFlags, WIFIAPI_EVENT_FLAG_STA_DISCONNECTED);
    }
}

static void StaEventCallback(DRV_HANDLE handle, WDRV_WINC_ASSOC_HANDLE assocHandle, WDRV_WINC_CONN_STATE currentState, WDRV_WINC_CONN_ERROR errorCode) {
    if (WDRV_WINC_CONN_STATE_CONNECTED == currentState) {
        SYS_CONSOLE_PRINT("STA mode: Station connected\r\n");
        SetEventFlag(&gEventFlags, WIFIAPI_EVENT_FLAG_STA_CONNECTED);
        ResetEventFlag(&gEventFlags, WIFIAPI_EVENT_FLAG_STA_DISCONNECTED);
    } else if (WDRV_WINC_CONN_STATE_DISCONNECTED == currentState) {
        SYS_CONSOLE_PRINT(appData.consoleHandle, "STA mode: Station disconnected\r\n");
        ResetEventFlag(&gEventFlags, WIFIAPI_EVENT_FLAG_STA_CONNECTED);
        SetEventFlag(&gEventFlags, WIFIAPI_EVENT_FLAG_STA_DISCONNECTED);
    }
}







bool WifiApi_Init(WifiSettings* pSettings) {
    //TODO(Daqifi): Uncomment this part
    //    const tPowerData *pPowerState = BoardData_Get(                          
    //              BOARDATA_POWER_DATA,                                          
    //              0 );
    //    if( NULL != pPowerState &&                                              
    //        pPowerState->powerState <  POWERED_UP )
    //    {
    //        LogMessage("Board must be powered-on for WiFi operations\n\r");
    //        return false;
    //    }
    if(pSettings!=NULL)
        memcpy(&gWifiSettings, pSettings, sizeof (gWifiState));
    
    gWifiState = WIFIAPI_STATE_INIT;
    return true;
}
bool WifiApi_Deinit() {
    //TODO(Daqifi): Uncomment this part
    //    const tPowerData *pPowerState = BoardData_Get(                          
    //              BOARDATA_POWER_DATA,                                          
    //              0 );
    //    if( NULL != pPowerState &&                                              
    //        pPowerState->powerState <  POWERED_UP )
    //    {
    //        LogMessage("Board must be powered-on for WiFi operations\n\r");
    //        return false;
    //    }

    wifiDeinit();
    gWifiState = WIFIAPI_STATE_DEINITIALIZING;
    return true;
}
bool WifiApi_ReInit() {
    //TODO(Daqifi): Uncomment this part
    //    const tPowerData *pPowerState = BoardData_Get(                          
    //              BOARDATA_POWER_DATA,                                          
    //              0 );
    //    if( NULL != pPowerState &&                                              
    //        pPowerState->powerState <  POWERED_UP )
    //    {
    //        LogMessage("Board must be powered-on for WiFi operations\n\r");
    //        return false;
    //    }

    wifiDeinit();
    gWifiState = WIFIAPI_STATE_REINITIALIZING;

    return true;
}
bool WifiApi_UpdateNetworkSettings(WifiSettings* pSettings) {
    //TODO(Daqifi): Uncomment this part
    //    const tPowerData *pPowerState = (tPowerData *)BoardData_Get(                          
    //              BOARDATA_POWER_DATA,                                          
    //              0 );
    //    if( NULL != pPowerState &&                                              
    //        pPowerState->powerState < POWERED_UP )
    //    {
    //        LogMessage("Board must be powered-on for WiFi operations\n\r");
    //        return false;
    //    }

    memcpy(&gWifiSettings, pSettings, sizeof (gWifiState));
    return WifiApi_ReInit();
}

void WifiApi_ProcessStates() {
    static WDRV_WINC_BSS_CONTEXT bssCtx;
    static WDRV_WINC_AUTH_CONTEXT authCtx;
    switch (gWifiState) {
        case WIFIAPI_STATE_INIT:
            ResetAllEventFlags(&gEventFlags);
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_UNINITIALIZED) {
                WDRV_WINC_Initialize(0, NULL);
            }
            gWifiState = WIFIAPI_STATE_INITIALIZING;
            break;
        case WIFIAPI_STATE_INITIALIZING:
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_READY) {
                gWdrvHandle = WDRV_WINC_Open(0, 0);
                if (gWdrvHandle == DRV_HANDLE_INVALID) {
                    gWifiState = WIFIAPI_STATE_ERROR;
                    SYS_CONSOLE_PRINT("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetDefaults(&bssCtx)) {
                    gWifiState = WIFIAPI_STATE_ERROR;
                    SYS_CONSOLE_PRINT("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                if (gWifiSettings.networkMode == WIFI_API_NETWORK_MODE_STA) {
                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetSSID(&bssCtx, (uint8_t*) gWifiSettings.ssid, strlen(gWifiSettings.ssid))) {
                        gWifiState = WIFIAPI_STATE_ERROR;
                        SYS_CONSOLE_PRINT("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                        break;
                    }
                    if (gWifiSettings.securityMode == WDRV_WINC_AUTH_TYPE_OPEN) {
                        if (WDRV_WINC_STATUS_OK != WDRV_WINC_AuthCtxSetOpen(&authCtx)) {
                            gWifiState = WIFIAPI_STATE_ERROR;
                            SYS_CONSOLE_PRINT("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                            break;
                        }
                    } else if (gWifiSettings.securityMode == WDRV_WINC_AUTH_TYPE_WPA_PSK) {
                        if (WDRV_WINC_STATUS_OK != WDRV_WINC_AuthCtxSetWPA(&authCtx, (uint8_t*) gWifiSettings.passKey, gWifiSettings.passKeyLength)) {
                            gWifiState = WIFIAPI_STATE_ERROR;
                            SYS_CONSOLE_PRINT("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                            break;
                        }

                    } else {
                        gWifiState = WIFIAPI_STATE_ERROR;
                        SYS_CONSOLE_PRINT("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                        break;
                    }
                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_IPUseDHCPSet(gWdrvHandle, &DhcpEventCallback)) {
                        gWifiState = WIFIAPI_STATE_ERROR;
                        SYS_CONSOLE_PRINT("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                        break;
                    }

                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSConnect(gWdrvHandle, &bssCtx, &authCtx, &StaEventCallback)) {
                        gWifiState = WIFIAPI_STATE_ERROR;
                        SYS_CONSOLE_PRINT("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                        break;
                    }

                    SetEventFlag(&gEventFlags, WIFIAPI_EVENT_FLAG_STA_STARTED);

                } else {
                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetSSID(&bssCtx, (uint8_t*) DEFAULT_WIFI_AP_SSID, strlen(DEFAULT_WIFI_AP_SSID))) {
                        gWifiState = WIFIAPI_STATE_ERROR;
                        SYS_CONSOLE_PRINT("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                        break;
                    }
                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_AuthCtxSetOpen(&authCtx)) {
                        gWifiState = WIFIAPI_STATE_ERROR;
                        SYS_CONSOLE_PRINT("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                        break;
                    }

                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_IPDHCPServerConfigure(gWdrvHandle, inet_addr(DEFAULT_NETWORK_GATEWAY_IP_ADDRESS), inet_addr(DEFAULT_NETWORK_IP_MASK), &DhcpEventCallback)) {
                        gWifiState = WIFIAPI_STATE_ERROR;
                        SYS_CONSOLE_PRINT("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                        break;
                    }
                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_APStart(gWdrvHandle, &bssCtx, &authCtx, NULL, &ApEventCallback)) {
                        gWifiState = WIFIAPI_STATE_ERROR;
                        SYS_CONSOLE_PRINT("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                        break;
                    }
                    SetEventFlag(&gEventFlags, WIFIAPI_EVENT_FLAG_AP_STARTED);
                }

            }
            break;
        case WIFIAPI_STATE_REINITIALIZING:
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_UNINITIALIZED) {
                ResetAllEventFlags(&gEventFlags);
                gWifiState = WIFIAPI_STATE_INIT;
            }
            break;
        case WIFIAPI_STATE_DEINITIALIZING:
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_UNINITIALIZED) {
                ResetAllEventFlags(&gEventFlags);
                gWifiState = WIFIAPI_STATE_IDLE;
            }
            break;
        case WIFIAPI_STATE_IDLE:
            if (GetEventFlagStatus(gEventFlags, WIFIAPI_EVENT_FLAG_AP_STARTED)) {
                if (GetEventFlagStatus(gEventFlags, WIFIAPI_EVENT_FLAG_STA_CONNECTED)) {

                } else if (GetEventFlagStatus(gEventFlags, WIFIAPI_EVENT_FLAG_STA_DISCONNECTED)) {

                }
            } else if (GetEventFlagStatus(gEventFlags, WIFIAPI_EVENT_FLAG_STA_STARTED)) {
                if (GetEventFlagStatus(gEventFlags, WIFIAPI_EVENT_FLAG_STA_CONNECTED)) {

                } else if (GetEventFlagStatus(gEventFlags, WIFIAPI_EVENT_FLAG_STA_DISCONNECTED)) {
                    ResetEventFlag(&gEventFlags, WIFIAPI_EVENT_FLAG_STA_DISCONNECTED);
                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSConnect(gWdrvHandle, &bssCtx, &authCtx, &StaEventCallback)) {
                        gWifiState = WIFIAPI_STATE_ERROR;
                        SYS_CONSOLE_PRINT("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                        break;
                    }
                }
            }
            break;
        default:
            break;
    }

}