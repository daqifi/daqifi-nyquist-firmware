#include "WifiApi.h"
#include "wdrv_winc_client_api.h"
#include "Util/Logger.h"
#include "tcpServer.h"
#include "state/data/BoardData.h"
#include "wifiSerialBridge.h"
#include "wifiSerailBrideIntf.h"

#define UNUSED(x) (void)(x)
#define UDP_LISTEN_PORT         (uint16_t)30303

typedef enum {
    WIFIAPI_CONSUMED_EVENT_ENTRY,
    WIFIAPI_CONSUMED_EVENT_EXIT,
    WIFIAPI_CONSUMED_EVENT_INIT,
    WIFIAPI_CONSUMED_EVENT_OTA_MODE_INIT,
    WIFIAPI_CONSUMED_EVENT_OTA_MODE_READY,
    WIFIAPI_CONSUMED_EVENT_REINIT,
    WIFIAPI_CONSUMED_EVENT_DEINIT,
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
        WIFIAPI_EVENT_FLAG_TCP_SOCKET_OPEN = 1 << 4,
        WIFIAPI_EVENT_FLAG_UDP_SOCKET_CONNECTED = 1 << 5,
        WIFIAPI_EVENT_FLAG_TCP_SOCKET_CONNECTED = 1 << 6,
        WIFIAPI_EVENT_FLAG_OTA_MODE_READY = 1 << 7,
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
    WifiSettings *pWifiSettings;
    DRV_HANDLE wdrvHandle;
    SOCKET udpServerSocket;
    TcpServerData *pTcpServerData;
    WDRV_WINC_BSS_CONTEXT bssCtx;
    WDRV_WINC_AUTH_CONTEXT authCtx;
    WIFI_SERIAL_BRIDGE_DECODER_STATE serialBridgeDecodeState;
};
//===============Private Function Declrations================
static void __attribute__((unused)) SetEventFlag(wifiApi_eventFlagState_t *pEventFlagState, uint16_t flag);
static void __attribute__((unused)) ResetEventFlag(wifiApi_eventFlagState_t *pEventFlagState, uint16_t flag);
static uint8_t __attribute__((unused)) GetEventFlagStatus(wifiApi_eventFlagState_t eventFlagState, uint16_t flag);
static bool SendEvent(WifiApi_consumedEvent_t event);
static WifiApi_eventStatus_t MainState(stateMachineInst_t * const pInstance, uint16_t event);

extern bool TcpServer_ProcessSendBuffer();
extern size_t TcpServer_WriteBuffer(const char* data, size_t len);
extern size_t TcpServer_WriteBuffFreeSize();
extern void TcpServer_OpenSocket(uint16_t port);
extern void TcpServer_CloseSocket();
extern void TcpServer_CloseClientSocket();
extern bool TcpServer_ProcessReceivedBuff();
extern void TcpServer_Initialize(TcpServerData *pServerData);
//===========================================================
//=====================Private Variables=====================
static stateMachineInst_t gStateMachineData;
static QueueHandle_t gEventQH = NULL;
//(TODO(Daqifi):Remove from here
static TcpServerData gTcpServerData;
//===========================================================
//=====================Private Callbacks=====================

static void DhcpEventCallback(DRV_HANDLE handle, uint32_t ipAddress) {
    char s[20];
    UNUSED(s);
    if (GetEventFlagStatus(gStateMachineData.eventFlags, WIFIAPI_EVENT_FLAG_STA_STARTED)) {
        gStateMachineData.pWifiSettings->ipAddr.Val = ipAddress;
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

static void SocketEventCallback(SOCKET socket, uint8_t messageType, void *pMessage) {
#define UDP_BUFFER_SIZE 1460
    static uint8_t udpBuffer[UDP_BUFFER_SIZE];
    switch (messageType) {
        case SOCKET_MSG_BIND:
        {
            tstrSocketBindMsg *pBindMessage = (tstrSocketBindMsg*) pMessage;
            if ((NULL != pBindMessage) && (0 == pBindMessage->status)) {
                if (socket == gStateMachineData.udpServerSocket) {
                    recvfrom(gStateMachineData.udpServerSocket, udpBuffer, UDP_BUFFER_SIZE, 0);
                    SendEvent(WIFIAPI_CONSUMED_EVENT_UDP_SOCKET_CONNECTED);
                } else if (socket == gStateMachineData.pTcpServerData->serverSocket) {
                    listen(gStateMachineData.pTcpServerData->serverSocket, 0);
                }
            } else {
                LOG_E("[%s:%d]Error Socket Bind", __FILE__, __LINE__);
                SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
            }
            break;
        }
        case SOCKET_MSG_LISTEN:
        {
            tstrSocketListenMsg *pListenMessage = (tstrSocketListenMsg*) pMessage;

            if ((NULL != pListenMessage) && (0 == pListenMessage->status) && (socket == gStateMachineData.pTcpServerData->serverSocket)) {
                accept(gStateMachineData.pTcpServerData->serverSocket, NULL, NULL);
            } else {
                LOG_E("[%s:%d]Error Socket Listen", __FILE__, __LINE__);
                SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
            }
            break;
        }
        case SOCKET_MSG_ACCEPT:
        {
            tstrSocketAcceptMsg *pAcceptMessage = (tstrSocketAcceptMsg*) pMessage;

            if (NULL != pAcceptMessage) {
                char s[20];
                if (gStateMachineData.pTcpServerData->client.clientSocket >= 0) // close any open client (only one client supported at one time)
                {
                    TcpServer_CloseClientSocket();
                }
                gStateMachineData.pTcpServerData->client.clientSocket = pAcceptMessage->sock;
                LOG_D("Connection from %s:%d\r\n", inet_ntop(AF_INET, &pAcceptMessage->strAddr.sin_addr.s_addr, s, sizeof (s)), pAcceptMessage->strAddr.sin_port);
                recv(gStateMachineData.pTcpServerData->client.clientSocket, gStateMachineData.pTcpServerData->client.readBuffer, WIFI_RBUFFER_SIZE, 0);

            } else {
                LOG_E("[%s:%d]Error Socket accept", __FILE__, __LINE__);
                SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
            }
            break;
        }
        case SOCKET_MSG_RECV:
        {
            tstrSocketRecvMsg *pRecvMessage = (tstrSocketRecvMsg*) pMessage;

            if ((NULL != pRecvMessage) && (pRecvMessage->s16BufferSize > 0)) {
                LOG_D("Receive on socket %d successful\r\n", socket);
                LOG_D("Client sent %d bytes\r\n", pRecvMessage->s16BufferSize);
                LOG_D("Client sent %s\r\n", pRecvMessage->pu8Buffer);
                LOG_D("Sending a test message to client\r\n");
                gStateMachineData.pTcpServerData->client.readBufferLength = pRecvMessage->s16BufferSize;
                TcpServer_ProcessReceivedBuff();
                recv(gStateMachineData.pTcpServerData->client.clientSocket, gStateMachineData.pTcpServerData->client.readBuffer, WIFI_RBUFFER_SIZE, 0);

            } else {
                LOG_E("[%s:%d]Error Socket MSG Recv", __FILE__, __LINE__);
                TcpServer_CloseClientSocket();
            }
            break;
        }
        case SOCKET_MSG_RECVFROM:
        {
            tstrSocketRecvMsg *pstrRx = (tstrSocketRecvMsg*) pMessage;
            if (pstrRx->s16BufferSize > 0) {
                //get the remote host address and port number
                uint16_t u16port = _htons(pstrRx->strRemoteAddr.sin_port);
                uint32_t strRemoteHostAddr = pstrRx->strRemoteAddr.sin_addr.s_addr;
                char s[20];
                inet_ntop(AF_INET, &strRemoteHostAddr, s, sizeof (s));
                LOG_D("\r\nReceived frame with size=%d\r\nHost address=%s\r\nPort number = %d\r\n", pstrRx->s16BufferSize, s, u16port);
                LOG_D("Frame Data : %.*s\r\n", pstrRx->s16BufferSize, (char*) pstrRx->pu8Buffer);
                uint16_t announcePacktLen = UDP_BUFFER_SIZE;
                WifiApi_FormUdpAnnouncePacketCallback(gStateMachineData.pWifiSettings, udpBuffer, &announcePacktLen);
                struct sockaddr_in addr;
                addr.sin_family = AF_INET;
                addr.sin_port = _htons(UDP_LISTEN_PORT); //pstrRx->strRemoteAddr.sin_port;
                addr.sin_addr.s_addr = inet_addr(s);
                sendto(gStateMachineData.udpServerSocket, udpBuffer, announcePacktLen, 0, (struct sockaddr*) &addr, sizeof (struct sockaddr_in));
                recvfrom(gStateMachineData.udpServerSocket, udpBuffer, UDP_BUFFER_SIZE, 0);
            }
        }
            break;

        case SOCKET_MSG_SENDTO:
        {
            LOG_D("UDP Send Success\r\n");
            break;
        }
        case SOCKET_MSG_SEND:
            gStateMachineData.pTcpServerData->client.tcpSendPending = 0;
            break;
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
            pInstance->pTcpServerData = &gTcpServerData; //TODO(Daqifi): Remove from there here
            TcpServer_Initialize(pInstance->pTcpServerData);
            ResetAllEventFlags(&pInstance->eventFlags);
            pInstance->udpServerSocket = -1;
            pInstance->wdrvHandle = DRV_HANDLE_INVALID;
            if (pInstance->pWifiSettings->isEnabled) {
                if (pInstance->pWifiSettings->isOtaModeEnabled)
                    SendEvent(WIFIAPI_CONSUMED_EVENT_OTA_MODE_INIT);
                else
                    SendEvent(WIFIAPI_CONSUMED_EVENT_INIT);
            }
            break;
        case WIFIAPI_CONSUMED_EVENT_OTA_MODE_INIT:
            returnStatus = WIFIAPI_EVENT_STATUS_HANDLED;
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_BUSY) {
                SendEvent(WIFIAPI_CONSUMED_EVENT_OTA_MODE_INIT);
                break;
            }
            sysObj.drvWifiWinc = WDRV_WINC_Initialize(0, NULL);
            SendEvent(WIFIAPI_CONSUMED_EVENT_OTA_MODE_READY);
        case WIFIAPI_CONSUMED_EVENT_OTA_MODE_READY:
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_BUSY) {
                SendEvent(WIFIAPI_CONSUMED_EVENT_OTA_MODE_INIT);
                break;
            }
            wifiSerialBridge_Init(&pInstance->serialBridgeDecodeState);
            SetEventFlag(&pInstance->eventFlags, WIFIAPI_EVENT_FLAG_OTA_MODE_READY);            
            break;
        case WIFIAPI_CONSUMED_EVENT_INIT:
            returnStatus = WIFIAPI_EVENT_STATUS_HANDLED;

            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_UNINITIALIZED) {
                sysObj.drvWifiWinc = WDRV_WINC_Initialize(0, NULL);
            }

            if (WDRV_WINC_Status(sysObj.drvWifiWinc) != SYS_STATUS_READY) {
                //wait for initialization to complete 
                SendEvent(WIFIAPI_CONSUMED_EVENT_INIT);
                break;
            }
            pInstance->wdrvHandle = WDRV_WINC_Open(0, 0);
            WDRV_WINC_EthernetAddressGet(pInstance->wdrvHandle, pInstance->pWifiSettings->macAddr.addr);
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
            if (pInstance->pWifiSettings->networkMode == WIFI_API_NETWORK_MODE_STA) {
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetSSID(&pInstance->bssCtx, (uint8_t*) pInstance->pWifiSettings->ssid, strlen(pInstance->pWifiSettings->ssid))) {
                    SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetChannel(&pInstance->bssCtx, WDRV_WINC_CID_ANY)) {
                    SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                if (pInstance->pWifiSettings->securityMode == WIFI_API_SEC_OPEN) {
                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_AuthCtxSetOpen(&pInstance->authCtx)) {
                        SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
                        LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                        break;
                    }
                } else if (pInstance->pWifiSettings->securityMode == WIFI_API_SEC_WPA_AUTO_WITH_PASS_PHRASE) {
                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_AuthCtxSetWPA(&pInstance->authCtx, (uint8_t*) pInstance->pWifiSettings->passKey, pInstance->pWifiSettings->passKeyLength)) {
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
                gStateMachineData.pWifiSettings->ipAddr.Val = inet_addr(DEFAULT_NETWORK_GATEWAY_IP_ADDRESS);
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
            WDRV_WINC_SocketRegisterEventCallback(pInstance->wdrvHandle, &SocketEventCallback);
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
            if (!GetEventFlagStatus(pInstance->eventFlags, WIFIAPI_EVENT_FLAG_TCP_SOCKET_OPEN)) {
                TcpServer_OpenSocket(pInstance->pWifiSettings->tcpPort);
                if (pInstance->pTcpServerData->serverSocket < 0) {
                    SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
            }
            SetEventFlag(&pInstance->eventFlags, WIFIAPI_EVENT_FLAG_TCP_SOCKET_OPEN);
            SetEventFlag(&pInstance->eventFlags, WIFIAPI_EVENT_FLAG_UDP_SOCKET_OPEN);
            break;
        case WIFIAPI_CONSUMED_EVENT_STA_DISCONNECTED:
            returnStatus = WIFIAPI_EVENT_STATUS_HANDLED;
            ResetEventFlag(&pInstance->eventFlags, WIFIAPI_EVENT_FLAG_STA_CONNECTED);
            CloseUdpSocket(&pInstance->udpServerSocket);
            TcpServer_CloseSocket();
            ResetEventFlag(&pInstance->eventFlags, WIFIAPI_EVENT_FLAG_TCP_SOCKET_OPEN);
            ResetEventFlag(&pInstance->eventFlags, WIFIAPI_EVENT_FLAG_UDP_SOCKET_OPEN);
            SendEvent(WIFIAPI_CONSUMED_EVENT_ERROR);
            break;
        case WIFIAPI_CONSUMED_EVENT_REINIT:
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_BUSY) {
                SendEvent(WIFIAPI_CONSUMED_EVENT_REINIT);
                break;
            }
            returnStatus = WIFIAPI_EVENT_STATUS_TRAN;
            pInstance->nextState = MainState;
            if (GetEventFlagStatus(pInstance->eventFlags, WIFIAPI_EVENT_FLAG_OTA_MODE_READY))
                wifiSerialBridgeIntf_DeInit();
            if (GetEventFlagStatus(pInstance->eventFlags, WIFIAPI_EVENT_FLAG_UDP_SOCKET_OPEN))
                CloseUdpSocket(&pInstance->udpServerSocket);
            if (GetEventFlagStatus(pInstance->eventFlags, WIFIAPI_EVENT_FLAG_TCP_SOCKET_OPEN))
                TcpServer_CloseSocket();
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) != SYS_STATUS_UNINITIALIZED) {
                WDRV_WINC_Close(pInstance->wdrvHandle);
                WDRV_WINC_Deinitialize(sysObj.drvWifiWinc);
                LOG_D("WiFi de-initializing\r\n");
            }

            break;
        case WIFIAPI_CONSUMED_EVENT_DEINIT:
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_BUSY) {
                SendEvent(WIFIAPI_CONSUMED_EVENT_REINIT);
                break;
            }
            returnStatus = WIFIAPI_EVENT_STATUS_TRAN;
            pInstance->nextState = MainState;
            if (GetEventFlagStatus(pInstance->eventFlags, WIFIAPI_EVENT_FLAG_OTA_MODE_READY))
                wifiSerialBridgeIntf_DeInit();
            if (GetEventFlagStatus(pInstance->eventFlags, WIFIAPI_EVENT_FLAG_UDP_SOCKET_OPEN))
                CloseUdpSocket(&pInstance->udpServerSocket);
            if (GetEventFlagStatus(pInstance->eventFlags, WIFIAPI_EVENT_FLAG_TCP_SOCKET_OPEN))
                TcpServer_CloseSocket();
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) != SYS_STATUS_UNINITIALIZED) {
                WDRV_WINC_Close(pInstance->wdrvHandle);
                WDRV_WINC_Deinitialize(sysObj.drvWifiWinc);
                LOG_D("WiFi de-initializing\r\n");
            }
            break;
        case WIFIAPI_CONSUMED_EVENT_UDP_SOCKET_CONNECTED:
            SetEventFlag(&pInstance->eventFlags, WIFIAPI_EVENT_FLAG_UDP_SOCKET_CONNECTED);
            break;
        case WIFIAPI_CONSUMED_EVENT_EXIT:
            xQueueReset(gEventQH);
            returnStatus = WIFIAPI_EVENT_STATUS_HANDLED;
            break;
        case WIFIAPI_CONSUMED_EVENT_ERROR:
            SendEvent(WIFIAPI_CONSUMED_EVENT_REINIT);
            LOG_E("[%s:%d]Error WiFi", __FILE__, __LINE__);
            break;
        default:
            break;
    }
    return returnStatus;
}
//===========================================================




//===========================================================
//TODO(Daqifi): Remove this comment
//SYSTem:COMMunicate:LAN:NETMode 1
//SYSTem:COMMunicate:LAN:SECurity 2
//SYSTem:COMMunicate:LAN:SSID "Typical_G"
//SYSTem:COMMunicate:LAN:PASs "Arghya@19"
//SYSTem:COMMunicate:LAN:APPLY 1
//SYSTem:LOG?

bool WifiApi_Init(WifiSettings * pSettings) {

    if (gEventQH == NULL)
        gEventQH = xQueueCreate(20, sizeof (WifiApi_consumedEvent_t));
    else return true;
    if (pSettings != NULL)
        gStateMachineData.pWifiSettings = pSettings;
    gStateMachineData.pWifiSettings->isOtaModeEnabled = false;
    gStateMachineData.pWifiSettings->isEnabled = 1;
    gStateMachineData.active = MainState;
    gStateMachineData.active(&gStateMachineData, WIFIAPI_CONSUMED_EVENT_ENTRY);
    gStateMachineData.nextState = NULL;

    return true;
}

bool WifiApi_Deinit() {
    const tPowerData *pPowerState = BoardData_Get(
            BOARDATA_POWER_DATA,
            0);
    if (NULL != pPowerState &&
            pPowerState->powerState < POWERED_UP) {
        LogMessage("Board must be powered-on for WiFi operations\n\r");
        return false;
    }
    gStateMachineData.pWifiSettings->isEnabled = 0;
    SendEvent(WIFIAPI_CONSUMED_EVENT_DEINIT);
    return true;
}

bool WifiApi_UpdateNetworkSettings(WifiSettings * pSettings) {

    const tPowerData *pPowerState = (tPowerData *) BoardData_Get(
            BOARDATA_POWER_DATA,
            0);
    if (NULL != pPowerState &&
            pPowerState->powerState < POWERED_UP) {
        LogMessage("Board must be powered-on for WiFi operations\n\r");
        return false;
    }
    if (pSettings != NULL && gStateMachineData.pWifiSettings != NULL) {
        memcpy(gStateMachineData.pWifiSettings, pSettings, sizeof (WifiSettings));
    }
    SendEvent(WIFIAPI_CONSUMED_EVENT_REINIT);
    return true;
}

size_t WifiApi_WriteBuffFreeSize() {
    return TcpServer_WriteBuffFreeSize();
}

size_t WifiApi_WriteToBuffer(const char* pData, size_t len) {
    return TcpServer_WriteBuffer(pData, len);
}

void WifiApi_ProcessState() {
    WifiApi_consumedEvent_t event;
    WifiApi_eventStatus_t ret;
    while (gEventQH == NULL) {
        return;
    }
    if (GetEventFlagStatus(gStateMachineData.eventFlags, WIFIAPI_EVENT_FLAG_OTA_MODE_READY)) {
        wifiSerialBridge_Process(&gStateMachineData.serialBridgeDecodeState);
    } else {
        TcpServer_ProcessSendBuffer();
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

TcpServerData* WifiApi_GetTcpServerData() {
    return gStateMachineData.pTcpServerData;
}