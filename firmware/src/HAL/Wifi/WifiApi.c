#include "WifiApi.h"
#include "wdrv_winc_client_api.h"
#include "Util/Logger.h"

#define UNUSED(x) (void)(x)
#define UDP_LISTEN_PORT         30303

typedef enum {
    WIFIAPI_CONSUMED_EVENT_ENTRY,
    WIFIAPI_CONSUMED_EVENT_EXIT,
    WIFIAPI_CONSUMED_EVENT_INIT,
    WIFIAPI_CONSUMED_EVENT_REINIT,
    WIFIAPI_CONSUMED_EVENT_STA_CONNECTED,
    WIFIAPI_CONSUMED_EVENT_STA_DISCONNECTED,
    WIFIAPI_CONSUMED_EVENT_UDP_SOCKET_CONNECTED,
    WIFIAPI_CONSUMED_EVENT_ERROR,

    //    WIFIAPI_TASK_STATE_IDLE,
    //    WIFIAPI_TASK_STATE_INIT,
    //    WIFIAPI_TASK_STATE_INITIALIZING,
    //    WIFIAPI_TASK_STATE_REINITIALIZING,
    //    WIFIAPI_TASK_STATE_DEINITIALIZING,
    //    WIFIAPI_TASK_STATE_ERROR,
} WifiApi_consumedEvent_t;

typedef struct {

    enum app_eventFlag {
        WIFIAPI_EVENT_FLAG_AP_STARTED = 1 << 0,
        WIFIAPI_EVENT_FLAG_STA_STARTED = 1 << 1,
        WIFIAPI_EVENT_FLAG_STA_CONNECTED = 1 << 2,
        WIFIAPI_EVENT_FLAG_UDP_SOCKET_OPEN = 1 << 3,
        WIFIAPI_EVENT_FLAG_UDP_SOCKET_CONNECTED = 1 << 4
    } flag;
    uint16_t value;
} wifiApi_eventFlagState_t;

typedef enum {
    WIFIAPI_EVENT_STATUS_HANDLED,
    WIFIAPI_EVENT_STATUS_IGNORED,
    WIFIAPI_EVENT_STATUS_TRAN,
} WifiApi_eventStatus_t;

typedef struct stateMachineInst stateMachineInst_t;
typedef WifiApi_eventStatus_t(*stateMachineHandle_t)(stateMachineInst_t * const instance, uint16_t event);

struct stateMachineInst {
    stateMachineHandle_t active;
    stateMachineHandle_t nextState;
    stateMachineHandle_t childState;
    wifiApi_eventFlagState_t eventFlags;
    WifiSettings wifiSettings;
    DRV_HANDLE wdrvHandle;
    SOCKET udpServerSocket;
    WDRV_WINC_BSS_CONTEXT bssCtx;
    WDRV_WINC_AUTH_CONTEXT authCtx;
};
//===============Private Function Declrations================
static void __attribute__((unused)) SetEventFlag(wifiApi_eventFlagState_t *pEventFlagState, uint16_t flag);
static void __attribute__((unused)) ResetEventFlag(wifiApi_eventFlagState_t *pEventFlagState, uint16_t flag);
static uint8_t __attribute__((unused)) GetEventFlagStatus(wifiApi_eventFlagState_t eventFlagState, uint16_t flag);
static bool SendEvent(WifiApi_consumedEvent_t event);
static WifiApi_eventStatus_t MainState(stateMachineInst_t * const pInstance, uint16_t event);
//===========================================================
//=====================Private Variables=====================
static stateMachineInst_t gStateMachineData;
static QueueHandle_t gEventQH = NULL;
//===========================================================
//=====================Private Callbacks=====================

static void DhcpEventCallback(DRV_HANDLE handle, uint32_t ipAddress) {
    char s[20];
    UNUSED(s);
    if (GetEventFlagStatus(gStateMachineData.eventFlags, WIFIAPI_EVENT_FLAG_STA_STARTED)) {
       gStateMachineData.wifiSettings.ipAddr.Val=ipAddress;
    }
    LOG_D("STA Mode: Station IP address is %s\r\n", inet_ntop(AF_INET, &ipAddress, s, sizeof (s)));
}

static void ApEventCallback(DRV_HANDLE handle, WDRV_WINC_ASSOC_HANDLE assocHandle, WDRV_WINC_CONN_STATE currentState, WDRV_WINC_CONN_ERROR errorCode) {
    if (WDRV_WINC_CONN_STATE_CONNECTED == currentState) {
        LOG_D("AP mode: Station connected\r\n");
        SendEvent(WIFIAPI_CONSUMED_EVENT_STA_CONNECTED);
    } else if (WDRV_WINC_CONN_STATE_DISCONNECTED == currentState) {
        LOG_D("AP mode: Station disconnected\r\n");
        SendEvent(WIFIAPI_CONSUMED_EVENT_STA_DISCONNECTED);
    }
}

static void StaEventCallback(DRV_HANDLE handle, WDRV_WINC_ASSOC_HANDLE assocHandle, WDRV_WINC_CONN_STATE currentState, WDRV_WINC_CONN_ERROR errorCode) {
    if (WDRV_WINC_CONN_STATE_CONNECTED == currentState) {
        LOG_D("STA mode: Station connected\r\n");
        SendEvent(WIFIAPI_CONSUMED_EVENT_STA_CONNECTED);
    } else if (WDRV_WINC_CONN_STATE_DISCONNECTED == currentState && errorCode != WDRV_WINC_CONN_ERROR_INPROGRESS) {
        LOG_D("STA mode: Station disconnected\r\n");
        SendEvent(WIFIAPI_CONSUMED_EVENT_STA_DISCONNECTED);
    }
}

static void udpSocketEventCallback(SOCKET socket, uint8_t messageType, void *pMessage) {
#define UDP_BUFFER_SIZE 1460
    static uint8_t udpBuffer[UDP_BUFFER_SIZE];    
    switch (messageType) {
        case SOCKET_MSG_BIND:
        {
            tstrSocketBindMsg *pBindMessage = (tstrSocketBindMsg*) pMessage;
            if ((NULL != pBindMessage) && (0 == pBindMessage->status)) {               
                recvfrom(gStateMachineData.udpServerSocket, udpBuffer, UDP_BUFFER_SIZE, 0);
                SendEvent(WIFIAPI_CONSUMED_EVENT_UDP_SOCKET_CONNECTED);
            }
            break;
        }
        case SOCKET_MSG_RECVFROM:
        {
            tstrSocketRecvMsg *pstrRx = (tstrSocketRecvMsg*) pMessage;
            if (pstrRx->s16BufferSize > 0) {
                //get the remote host address and port number
                uint16_t u16port = pstrRx->strRemoteAddr.sin_port;
                uint32_t strRemoteHostAddr = pstrRx->strRemoteAddr.sin_addr.s_addr;
                char s[20];
                inet_ntop(AF_INET, &strRemoteHostAddr, s, sizeof (s));
                LOG_D("\r\nReceived frame with size=%d\r\nHost address=%s\r\nPort number = %d\r\n", pstrRx->s16BufferSize, s, u16port);
                LOG_D("Frame Data : %.*s\r\n", pstrRx->s16BufferSize, (char*) pstrRx->pu8Buffer);
                recvfrom(gStateMachineData.udpServerSocket, udpBuffer, UDP_BUFFER_SIZE, 0);
                uint16_t announcePacktLen=UDP_BUFFER_SIZE;
                WifiApi_FormUdpAnnouncePacketCallback(gStateMachineData.wifiSettings,udpBuffer,&announcePacktLen);
                struct sockaddr_in addr;
                addr.sin_family = AF_INET;
                addr.sin_port = pstrRx->strRemoteAddr.sin_port;
                addr.sin_addr.s_addr = pstrRx->strRemoteAddr.sin_addr.s_addr;
                sendto(gStateMachineData.udpServerSocket, udpBuffer, announcePacktLen, 0, (struct sockaddr*) &addr, sizeof (struct sockaddr_in));
            }
        }
            break;

        case SOCKET_MSG_SENDTO:
        {
            LOG_D("UDP Send Success\r\n");
            break;
        }

        default:
        {
            break;
        }
    }
}
//===========================================================
//=====================Private Functions=====================

static void __attribute__((unused)) SetEventFlag(wifiApi_eventFlagState_t *pEventFlagState, uint16_t flag) {
    pEventFlagState->value = pEventFlagState->value | flag;
}

static void __attribute__((unused)) ResetEventFlag(wifiApi_eventFlagState_t *pEventFlagState, uint16_t flag) {
    pEventFlagState->value = pEventFlagState->value & (~flag);
}

static uint8_t __attribute__((unused)) GetEventFlagStatus(wifiApi_eventFlagState_t eventFlagState, uint16_t flag) {
    if (eventFlagState.value & flag)
        return 1;
    else
        return 0;
}

static void __attribute__((unused)) ResetAllEventFlags(wifiApi_eventFlagState_t *pEventFlagState) {
    memset(pEventFlagState, 0, sizeof (wifiApi_eventFlagState_t));
}

static bool CloseUdpSocket(SOCKET *pSocket) {
    if (*pSocket != -1)
        shutdown(*pSocket);
    *pSocket = -1;
    return true;
}

static bool OpenUdpSocket(SOCKET *pSocket) {
    bool returnStatus = false;
    *pSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (*pSocket >= 0) {
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = _htons(UDP_LISTEN_PORT);
        addr.sin_addr.s_addr = 0;
        bind(*pSocket, (struct sockaddr*) &addr, sizeof (struct sockaddr_in));
        returnStatus = true;
    }
    return returnStatus;
}

//static bool wifiDeinit(DRV_HANDLE wdrvHndl) {
//    if (WDRV_WINC_Status(sysObj.drvWifiWinc) != SYS_STATUS_UNINITIALIZED) {
//        WDRV_WINC_Close(wdrvHndl);
//        WDRV_WINC_Deinitialize(sysObj.drvWifiWinc);
//        LOG_D("WiFi deinitializing\r\n");
//    }
//    return true;
//}

static bool SendEvent(WifiApi_consumedEvent_t event) {
    if (gEventQH != NULL) {
        if (xQueueSend(gEventQH, &event, portMAX_DELAY) == pdPASS) {
            return true;
        } else {
            return false;
        }

    }
    return true;
}

static WifiApi_eventStatus_t MainState(stateMachineInst_t * const pInstance, uint16_t event) {
    WifiApi_eventStatus_t returnStatus = WIFIAPI_EVENT_STATUS_HANDLED;
    switch (event) {
        case WIFIAPI_CONSUMED_EVENT_ENTRY:
            returnStatus = WIFIAPI_EVENT_STATUS_HANDLED;
            ResetAllEventFlags(&pInstance->eventFlags);
            pInstance->udpServerSocket = -1;
            pInstance->wdrvHandle = DRV_HANDLE_INVALID;
            SendEvent(WIFIAPI_CONSUMED_EVENT_INIT);            
            break;
        case WIFIAPI_CONSUMED_EVENT_INIT:
            returnStatus = WIFIAPI_EVENT_STATUS_HANDLED;
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_UNINITIALIZED) {
                sysObj.drvWifiWinc = WDRV_WINC_Initialize(0, NULL);
            } else {
                //wait for de-initialization to complete 
                SendEvent(WIFIAPI_CONSUMED_EVENT_INIT);
                break;
            }
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) != SYS_STATUS_READY) {
                //wait for initialization to complete 
                SendEvent(WIFIAPI_CONSUMED_EVENT_INIT);
                break;
            }
            pInstance->wdrvHandle = WDRV_WINC_Open(0, 0);
            WDRV_WINC_EthernetAddressGet(pInstance->wdrvHandle, pInstance->wifiSettings.macAddr.addr);
            if (pInstance->wdrvHandle == DRV_HANDLE_INVALID) {
                SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
                LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                break;
            }
            if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetDefaults(&pInstance->bssCtx)) {
                SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
                LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                break;
            }
            if (pInstance->wifiSettings.networkMode == WIFI_API_NETWORK_MODE_STA) {
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetSSID(&pInstance->bssCtx, (uint8_t*) pInstance->wifiSettings.ssid, strlen(pInstance->wifiSettings.ssid))) {
                    SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetChannel(&pInstance->bssCtx, WDRV_WINC_CID_ANY)) {
                    SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                if (pInstance->wifiSettings.securityMode == WDRV_WINC_AUTH_TYPE_OPEN) {
                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_AuthCtxSetOpen(&pInstance->authCtx)) {
                        SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
                        LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                        break;
                    }
                } else if (pInstance->wifiSettings.securityMode == WDRV_WINC_AUTH_TYPE_WPA_PSK) {
                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_AuthCtxSetWPA(&pInstance->authCtx, (uint8_t*) pInstance->wifiSettings.passKey, pInstance->wifiSettings.passKeyLength)) {
                        SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
                        LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                        break;
                    }

                } else {
                    SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_IPUseDHCPSet(pInstance->wdrvHandle, &DhcpEventCallback)) {
                    SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSConnect(pInstance->wdrvHandle, &pInstance->bssCtx, &pInstance->authCtx, &StaEventCallback)) {
                    SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                SetEventFlag(&pInstance->eventFlags, WIFIAPI_EVENT_FLAG_STA_STARTED);


            } else {
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetSSID(&pInstance->bssCtx, (uint8_t*) DEFAULT_WIFI_AP_SSID, strlen(DEFAULT_WIFI_AP_SSID))) {
                    SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetChannel(&pInstance->bssCtx, WDRV_WINC_CID_2_4G_CH1)) {
                    SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_AuthCtxSetOpen(&pInstance->authCtx)) {
                    SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                gStateMachineData.wifiSettings.ipAddr.Val= inet_addr(DEFAULT_NETWORK_GATEWAY_IP_ADDRESS);
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_IPDHCPServerConfigure(pInstance->wdrvHandle, inet_addr(DEFAULT_NETWORK_GATEWAY_IP_ADDRESS), inet_addr(DEFAULT_NETWORK_IP_MASK), &DhcpEventCallback)) {
                    SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_APStart(pInstance->wdrvHandle, &pInstance->bssCtx, &pInstance->authCtx, NULL, &ApEventCallback)) {
                    SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }

                SetEventFlag(&pInstance->eventFlags, WIFIAPI_EVENT_FLAG_AP_STARTED);
            }
            WDRV_WINC_SocketRegisterEventCallback(pInstance->wdrvHandle, &udpSocketEventCallback);
            break;
        case WIFIAPI_CONSUMED_EVENT_STA_CONNECTED:
            returnStatus = WIFIAPI_EVENT_STATUS_HANDLED;
            SetEventFlag(&pInstance->eventFlags, WIFIAPI_EVENT_FLAG_STA_CONNECTED);
            if (!GetEventFlagStatus(pInstance->eventFlags, WIFIAPI_EVENT_FLAG_UDP_SOCKET_OPEN)) {
                if (!OpenUdpSocket(&pInstance->udpServerSocket)) {
                    SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
            }
            SetEventFlag(&pInstance->eventFlags, WIFIAPI_EVENT_FLAG_UDP_SOCKET_OPEN);
            break;
        case WIFIAPI_CONSUMED_EVENT_STA_DISCONNECTED:
            returnStatus = WIFIAPI_EVENT_STATUS_HANDLED;
            ResetEventFlag(&pInstance->eventFlags, WIFIAPI_EVENT_FLAG_STA_CONNECTED);
            CloseUdpSocket(&pInstance->udpServerSocket);
            if (GetEventFlagStatus(pInstance->eventFlags, WIFIAPI_EVENT_FLAG_STA_STARTED)) {
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSConnect(pInstance->wdrvHandle, &pInstance->bssCtx, &pInstance->authCtx, &StaEventCallback)) {
                    SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
            }
            break;
        case WIFIAPI_CONSUMED_EVENT_REINIT:
            returnStatus = WIFIAPI_EVENT_STATUS_TRAN;
            pInstance->nextState = MainState;
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) != SYS_STATUS_UNINITIALIZED) {
                WDRV_WINC_Close(pInstance->wdrvHandle);
                WDRV_WINC_Deinitialize(sysObj.drvWifiWinc);
                LOG_D("WiFi de-initializing\r\n");
            }
            if (GetEventFlagStatus(pInstance->eventFlags, WIFIAPI_EVENT_FLAG_UDP_SOCKET_OPEN))
                CloseUdpSocket(&pInstance->udpServerSocket);
            break;
        case WIFIAPI_CONSUMED_EVENT_UDP_SOCKET_CONNECTED:
            SetEventFlag(&pInstance->eventFlags, WIFIAPI_EVENT_FLAG_UDP_SOCKET_CONNECTED);
            break;
        case WIFIAPI_CONSUMED_EVENT_EXIT:
            xQueueReset(gEventQH);
            returnStatus = WIFIAPI_EVENT_STATUS_HANDLED;
            break;
    }
    return returnStatus;
}
//===========================================================




//===========================================================

bool WifiApi_Init(WifiSettings * pSettings) {
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
    if (gEventQH == NULL)
        gEventQH = xQueueCreate(20, sizeof (WifiApi_consumedEvent_t));
    else return true;
    if (pSettings != NULL)
        memcpy(&gStateMachineData.wifiSettings, pSettings, sizeof (WifiSettings));

    gStateMachineData.active = MainState;
    gStateMachineData.active(&gStateMachineData, WIFIAPI_CONSUMED_EVENT_ENTRY);
    gStateMachineData.nextState = NULL;
    return true;
}

//bool WifiApi_Deinit() {
//    //TODO(Daqifi): Uncomment this part
//    //    const tPowerData *pPowerState = BoardData_Get(                          
//    //              BOARDATA_POWER_DATA,                                          
//    //              0 );
//    //    if( NULL != pPowerState &&                                              
//    //        pPowerState->powerState <  POWERED_UP )
//    //    {
//    //        LogMessage("Board must be powered-on for WiFi operations\n\r");
//    //        return false;
//    //    }
//
//    wifiDeinit();
//    gWifiState = WIFIAPI_TASK_STATE_DEINITIALIZING;
//    return true;
//}
//

bool WifiApi_UpdateNetworkSettings(WifiSettings * pSettings) {
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
    if (pSettings != NULL)
        memcpy(&gStateMachineData.wifiSettings, pSettings, sizeof (WifiSettings));
    SendEvent(WIFIAPI_CONSUMED_EVENT_REINIT);
    return true;
}

void WifiApi_Dispatcher() {
    WifiApi_consumedEvent_t event;
    WifiApi_eventStatus_t ret;
    while (gEventQH == NULL) {
        return;
    }

    if (xQueueReceive(gEventQH, &event, 1) != pdPASS) {
        return;
    }
    ret = gStateMachineData.active(&gStateMachineData, event);
    if (ret == WIFIAPI_EVENT_STATUS_TRAN) {
        gStateMachineData.active(&gStateMachineData, WIFIAPI_CONSUMED_EVENT_EXIT);
        if (gStateMachineData.nextState != NULL) {
            gStateMachineData.active = gStateMachineData.nextState;
            gStateMachineData.active(&gStateMachineData, WIFIAPI_CONSUMED_EVENT_ENTRY);
        } else {
            LOG_E("%s : %d : State Change Requested, but NextSate is NULL", __func__, __LINE__);
        }
    }
}
//void WifiApi_ProcessStates() {
//    static WDRV_WINC_BSS_CONTEXT bssCtx;
//    static WDRV_WINC_AUTH_CONTEXT authCtx;
//    switch (gWifiState) {
//        case WIFIAPI_TASK_STATE_INIT:
//            ResetAllEventFlags(&gEventFlags);
//            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_UNINITIALIZED) {
//                sysObj.drvWifiWinc = WDRV_WINC_Initialize(0, NULL);
//            }
//            gWifiState = WIFIAPI_TASK_STATE_INITIALIZING;
//            break;
//        case WIFIAPI_TASK_STATE_INITIALIZING:
//            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_READY) {
//
//                gWdrvHandle = WDRV_WINC_Open(0, 0);
//                if (gWdrvHandle == DRV_HANDLE_INVALID) {
//                    gWifiState = WIFIAPI_TASK_STATE_ERROR;
//                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
//                    break;
//                }
//                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetDefaults(&bssCtx)) {
//                    gWifiState = WIFIAPI_TASK_STATE_ERROR;
//                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
//                    break;
//                }
//                if (gWifiSettings.networkMode == WIFI_API_NETWORK_MODE_STA) {
//                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetSSID(&bssCtx, (uint8_t*) gWifiSettings.ssid, strlen(gWifiSettings.ssid))) {
//                        gWifiState = WIFIAPI_TASK_STATE_ERROR;
//                        LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
//                        break;
//                    }
//                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetChannel(&bssCtx, WDRV_WINC_CID_ANY)) {
//                        gWifiState = WIFIAPI_TASK_STATE_ERROR;
//                        LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
//                        break;
//                    }
//                    if (gWifiSettings.securityMode == WDRV_WINC_AUTH_TYPE_OPEN) {
//                        if (WDRV_WINC_STATUS_OK != WDRV_WINC_AuthCtxSetOpen(&authCtx)) {
//                            gWifiState = WIFIAPI_TASK_STATE_ERROR;
//                            LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
//                            break;
//                        }
//                    } else if (gWifiSettings.securityMode == WDRV_WINC_AUTH_TYPE_WPA_PSK) {
//                        if (WDRV_WINC_STATUS_OK != WDRV_WINC_AuthCtxSetWPA(&authCtx, (uint8_t*) gWifiSettings.passKey, gWifiSettings.passKeyLength)) {
//                            gWifiState = WIFIAPI_TASK_STATE_ERROR;
//                            LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
//                            break;
//                        }
//
//                    } else {
//                        gWifiState = WIFIAPI_TASK_STATE_ERROR;
//                        LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
//                        break;
//                    }
//                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_IPUseDHCPSet(gWdrvHandle, &DhcpEventCallback)) {
//                        gWifiState = WIFIAPI_TASK_STATE_ERROR;
//                        LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
//                        break;
//                    }
//                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSConnect(gWdrvHandle, &bssCtx, &authCtx, &StaEventCallback)) {
//                        gWifiState = WIFIAPI_TASK_STATE_ERROR;
//                        LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
//                        break;
//                    }
//                    SetEventFlag(&gEventFlags, WIFIAPI_EVENT_FLAG_STA_STARTED);
//
//
//                } else {
//                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetSSID(&bssCtx, (uint8_t*) DEFAULT_WIFI_AP_SSID, strlen(DEFAULT_WIFI_AP_SSID))) {
//                        gWifiState = WIFIAPI_TASK_STATE_ERROR;
//                        LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
//                        break;
//                    }
//                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetChannel(&bssCtx, WDRV_WINC_CID_2_4G_CH1)) {
//                        gWifiState = WIFIAPI_TASK_STATE_ERROR;
//                        LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
//                        break;
//                    }
//                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_AuthCtxSetOpen(&authCtx)) {
//                        gWifiState = WIFIAPI_TASK_STATE_ERROR;
//                        LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
//                        break;
//                    }
//
//                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_IPDHCPServerConfigure(gWdrvHandle, inet_addr(DEFAULT_NETWORK_GATEWAY_IP_ADDRESS), inet_addr(DEFAULT_NETWORK_IP_MASK), &DhcpEventCallback)) {
//                        gWifiState = WIFIAPI_TASK_STATE_ERROR;
//                        LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
//                        break;
//                    }
//                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_APStart(gWdrvHandle, &bssCtx, &authCtx, NULL, &ApEventCallback)) {
//                        gWifiState = WIFIAPI_TASK_STATE_ERROR;
//                        LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
//                        break;
//                    }
//
//                    SetEventFlag(&gEventFlags, WIFIAPI_EVENT_FLAG_AP_STARTED);
//
//                }
//                gWifiState = WIFIAPI_TASK_STATE_IDLE;
//            }
//            break;
//        case WIFIAPI_TASK_STATE_REINITIALIZING:
//            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_UNINITIALIZED) {
//                ResetAllEventFlags(&gEventFlags);
//                gWifiState = WIFIAPI_TASK_STATE_INIT;
//            }
//            break;
//        case WIFIAPI_TASK_STATE_DEINITIALIZING:
//            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_UNINITIALIZED) {
//                ResetAllEventFlags(&gEventFlags);
//                gWifiState = WIFIAPI_TASK_STATE_IDLE;
//            }
//            break;
//        case WIFIAPI_TASK_STATE_IDLE:
//            if (GetEventFlagStatus(gEventFlags, WIFIAPI_EVENT_FLAG_AP_STARTED)) {
//                if (GetEventFlagStatus(gEventFlags, WIFIAPI_EVENT_FLAG_STA_CONNECTED)) {
//                    if (!GetEventFlagStatus(gEventFlags, WIFIAPI_EVENT_FLAG_UDP_SOCKET_OPEN))
//                        OpenUdpSocket();
//                } else if (GetEventFlagStatus(gEventFlags, WIFIAPI_EVENT_FLAG_STA_DISCONNECTED)) {
//                    if (GetEventFlagStatus(gEventFlags, WIFIAPI_EVENT_FLAG_UDP_SOCKET_OPEN))
//                        CloseUdpSocket();
//                }
//            } else if (GetEventFlagStatus(gEventFlags, WIFIAPI_EVENT_FLAG_STA_STARTED)) {
//                if (GetEventFlagStatus(gEventFlags, WIFIAPI_EVENT_FLAG_STA_CONNECTED)) {
//                    if (!GetEventFlagStatus(gEventFlags, WIFIAPI_EVENT_FLAG_UDP_SOCKET_OPEN))
//                        OpenUdpSocket();
//                } else if (GetEventFlagStatus(gEventFlags, WIFIAPI_EVENT_FLAG_STA_DISCONNECTED)) {
//                    ResetEventFlag(&gEventFlags, WIFIAPI_EVENT_FLAG_STA_DISCONNECTED);
//                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSConnect(gWdrvHandle, &bssCtx, &authCtx, &StaEventCallback)) {
//                        gWifiState = WIFIAPI_TASK_STATE_ERROR;
//                        LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
//                        break;
//                    }
//                    if (GetEventFlagStatus(gEventFlags, WIFIAPI_EVENT_FLAG_UDP_SOCKET_OPEN))
//                        CloseUdpSocket();
//                }
//            }
//            break;
//        case WIFIAPI_TASK_STATE_ERROR:
//            WifiApi_ReInit();
//            break;
//        default:
//            break;
//    }
//
//}