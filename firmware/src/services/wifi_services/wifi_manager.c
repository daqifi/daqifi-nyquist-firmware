#include "wifi_manager.h"
#include "wdrv_winc_client_api.h"
#include "Util/Logger.h"
#include "wifi_tcp_server.h"
#include "state/data/BoardData.h"
#include "wifi_serial_bridge.h"
#include "wifi_serial_bridge_interface.h"

#define UNUSED(x) (void)(x)
#define UDP_LISTEN_PORT         (uint16_t)30303

typedef enum {
   WIFI_MANAGER_EVENT_ENTRY,
   WIFI_MANAGER_EVENT_EXIT,
   WIFI_MANAGER_EVENT_INIT,
   WIFI_MANAGER_EVENT_OTA_MODE_INIT,
   WIFI_MANAGER_EVENT_OTA_MODE_READY,
   WIFI_MANAGER_EVENT_REINIT,
   WIFI_MANAGER_EVENT_DEINIT,
   WIFI_MANAGER_EVENT_STA_CONNECTED,
   WIFI_MANAGER_EVENT_STA_DISCONNECTED,
   WIFI_MANAGER_EVENT_UDP_SOCKET_CONNECTED,
   WIFI_MANAGER_EVENT_ERROR,
} wifi_manager_event_t;

typedef struct {

    enum app_eventFlag {
        WIFI_MANAGER_STATE_FLAG_AP_STARTED = 1 << 0,
        WIFI_MANAGER_STATE_FLAG_STA_STARTED = 1 << 1,
        WIFI_MANAGER_STATE_FLAG_STA_CONNECTED = 1 << 2,
        WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_OPEN = 1 << 3,
        WIFI_MANAGER_STATE_FLAG_TCP_SOCKET_OPEN = 1 << 4,
        WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_CONNECTED = 1 << 5,
        WIFI_MANAGER_STATE_FLAG_TCP_SOCKET_CONNECTED = 1 << 6,
        WIFI_MANAGER_STATE_FLAG_OTA_MODE_READY = 1 << 7,
    } flag;
    uint16_t value;
} wifi_manager_stateFlag_t;

typedef enum {
    WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_HANDLED,
    WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_IGNORED,
    WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_TRAN,
} wifi_manager_stateMachineReturnStatus_t;

typedef struct stateMachineInst stateMachineInst_t;
typedef wifi_manager_stateMachineReturnStatus_t(*stateMachineHandle_t)(stateMachineInst_t * const instance, uint16_t event);

struct stateMachineInst {
    stateMachineHandle_t active;
    stateMachineHandle_t nextState;
    stateMachineHandle_t childState;
    wifi_manager_stateFlag_t eventFlags;
    WifiSettings *pWifiSettings;
    DRV_HANDLE wdrvHandle;
    SOCKET udpServerSocket;
    wifi_tcp_server_context_t *pTcpServerData;
    WDRV_WINC_BSS_CONTEXT bssCtx;
    WDRV_WINC_AUTH_CONTEXT authCtx;
    wifi_serial_bridge_context_t serialBridgeContext;
};
//===============Private Function Declrations================
static void __attribute__((unused)) SetEventFlag(wifi_manager_stateFlag_t *pEventFlagState, uint16_t flag);
static void __attribute__((unused)) ResetEventFlag(wifi_manager_stateFlag_t *pEventFlagState, uint16_t flag);
static uint8_t __attribute__((unused)) GetEventFlagStatus(wifi_manager_stateFlag_t eventFlagState, uint16_t flag);
static bool SendEvent(wifi_manager_event_t event);
static wifi_manager_stateMachineReturnStatus_t MainState(stateMachineInst_t * const pInstance, uint16_t event);

extern bool TcpServer_ProcessSendBuffer();
extern size_t TcpServer_WriteBuffer(const char* data, size_t len);
extern size_t TcpServer_WriteBuffFreeSize();
extern void TcpServer_OpenSocket(uint16_t port);
extern void TcpServer_CloseSocket();
extern void TcpServer_CloseClientSocket();
extern bool TcpServer_ProcessReceivedBuff();
extern void TcpServer_Initialize(wifi_tcp_server_context_t *pServerData);
//===========================================================
//=====================Private Variables=====================
static stateMachineInst_t gStateMachineContext;
static QueueHandle_t gEventQH = NULL;
//(TODO(Daqifi):Remove from here
static wifi_tcp_server_context_t gTcpServerData;
//===========================================================
//=====================Private Callbacks=====================

static void DhcpEventCallback(DRV_HANDLE handle, uint32_t ipAddress) {
    char s[20];
    UNUSED(s);
    if (GetEventFlagStatus(gStateMachineContext.eventFlags, WIFI_MANAGER_STATE_FLAG_STA_STARTED)) {
        gStateMachineContext.pWifiSettings->ipAddr.Val = ipAddress;
    }
    LOG_D("STA Mode: Station IP address is %s\r\n", inet_ntop(AF_INET, &ipAddress, s, sizeof (s)));
}

static void ApEventCallback(DRV_HANDLE handle, WDRV_WINC_ASSOC_HANDLE assocHandle, WDRV_WINC_CONN_STATE currentState, WDRV_WINC_CONN_ERROR errorCode) {
    if (WDRV_WINC_CONN_STATE_CONNECTED == currentState) {
        LOG_D("AP mode: Station connected\r\n");
        SendEvent(WIFI_MANAGER_EVENT_STA_CONNECTED);
    } else if (WDRV_WINC_CONN_STATE_DISCONNECTED == currentState) {
        LOG_D("AP mode: Station disconnected\r\n");
        SendEvent(WIFI_MANAGER_EVENT_STA_DISCONNECTED);
    }
}

static void StaEventCallback(DRV_HANDLE handle, WDRV_WINC_ASSOC_HANDLE assocHandle, WDRV_WINC_CONN_STATE currentState, WDRV_WINC_CONN_ERROR errorCode) {
    if (WDRV_WINC_CONN_STATE_CONNECTED == currentState) {
        LOG_D("STA mode: Station connected\r\n");
        SendEvent(WIFI_MANAGER_EVENT_STA_CONNECTED);
    } else if (WDRV_WINC_CONN_STATE_DISCONNECTED == currentState && errorCode != WDRV_WINC_CONN_ERROR_INPROGRESS) {
        LOG_D("STA mode: Station disconnected\r\n");
        SendEvent(WIFI_MANAGER_EVENT_STA_DISCONNECTED);
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
                if (socket == gStateMachineContext.udpServerSocket) {
                    recvfrom(gStateMachineContext.udpServerSocket, udpBuffer, UDP_BUFFER_SIZE, 0);
                    SendEvent(WIFI_MANAGER_EVENT_UDP_SOCKET_CONNECTED);
                } else if (socket == gStateMachineContext.pTcpServerData->serverSocket) {
                    listen(gStateMachineContext.pTcpServerData->serverSocket, 0);
                }
            } else {
                LOG_E("[%s:%d]Error Socket Bind", __FILE__, __LINE__);
                SendEvent(WIFI_MANAGER_EVENT_ERROR);
            }
            break;
        }
        case SOCKET_MSG_LISTEN:
        {
            tstrSocketListenMsg *pListenMessage = (tstrSocketListenMsg*) pMessage;

            if ((NULL != pListenMessage) && (0 == pListenMessage->status) && (socket == gStateMachineContext.pTcpServerData->serverSocket)) {
                accept(gStateMachineContext.pTcpServerData->serverSocket, NULL, NULL);
            } else {
                LOG_E("[%s:%d]Error Socket Listen", __FILE__, __LINE__);
                SendEvent(WIFI_MANAGER_EVENT_ERROR);
            }
            break;
        }
        case SOCKET_MSG_ACCEPT:
        {
            tstrSocketAcceptMsg *pAcceptMessage = (tstrSocketAcceptMsg*) pMessage;

            if (NULL != pAcceptMessage) {
                char s[20];
                if (gStateMachineContext.pTcpServerData->client.clientSocket >= 0) // close any open client (only one client supported at one time)
                {
                    TcpServer_CloseClientSocket();
                }
                gStateMachineContext.pTcpServerData->client.clientSocket = pAcceptMessage->sock;
                LOG_D("Connection from %s:%d\r\n", inet_ntop(AF_INET, &pAcceptMessage->strAddr.sin_addr.s_addr, s, sizeof (s)), pAcceptMessage->strAddr.sin_port);
                recv(gStateMachineContext.pTcpServerData->client.clientSocket, gStateMachineContext.pTcpServerData->client.readBuffer, WIFI_RBUFFER_SIZE, 0);

            } else {
                LOG_E("[%s:%d]Error Socket accept", __FILE__, __LINE__);
                SendEvent(WIFI_MANAGER_EVENT_ERROR);
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
                gStateMachineContext.pTcpServerData->client.readBufferLength = pRecvMessage->s16BufferSize;
                TcpServer_ProcessReceivedBuff();
                recv(gStateMachineContext.pTcpServerData->client.clientSocket, gStateMachineContext.pTcpServerData->client.readBuffer, WIFI_RBUFFER_SIZE, 0);

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
                wifi_manager_FormUdpAnnouncePacketCB(gStateMachineContext.pWifiSettings, udpBuffer, &announcePacktLen);
                struct sockaddr_in addr;
                addr.sin_family = AF_INET;
                addr.sin_port = _htons(UDP_LISTEN_PORT); //pstrRx->strRemoteAddr.sin_port;
                addr.sin_addr.s_addr = inet_addr(s);
                sendto(gStateMachineContext.udpServerSocket, udpBuffer, announcePacktLen, 0, (struct sockaddr*) &addr, sizeof (struct sockaddr_in));
                recvfrom(gStateMachineContext.udpServerSocket, udpBuffer, UDP_BUFFER_SIZE, 0);
            }
        }
            break;

        case SOCKET_MSG_SENDTO:
        {
            LOG_D("UDP Send Success\r\n");
            break;
        }
        case SOCKET_MSG_SEND:
            gStateMachineContext.pTcpServerData->client.tcpSendPending = 0;
            break;
        default:
        {
            break;
        }
    }
}
//===========================================================
//=====================Private Functions=====================

static void __attribute__((unused)) SetEventFlag(wifi_manager_stateFlag_t *pEventFlagState, uint16_t flag) {
    pEventFlagState->value = pEventFlagState->value | flag;
}

static void __attribute__((unused)) ResetEventFlag(wifi_manager_stateFlag_t *pEventFlagState, uint16_t flag) {
    pEventFlagState->value = pEventFlagState->value & (~flag);
}

static uint8_t __attribute__((unused)) GetEventFlagStatus(wifi_manager_stateFlag_t eventFlagState, uint16_t flag) {
    if (eventFlagState.value & flag)
        return 1;
    else
        return 0;
}

static void __attribute__((unused)) ResetAllEventFlags(wifi_manager_stateFlag_t *pEventFlagState) {
    memset(pEventFlagState, 0, sizeof (wifi_manager_stateFlag_t));
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

static bool SendEvent(wifi_manager_event_t event) {
    if (gEventQH != NULL) {
        if (xQueueSend(gEventQH, &event, portMAX_DELAY) == pdPASS) {
            return true;
        } else {
            return false;
        }

    }
    return true;
}

static wifi_manager_stateMachineReturnStatus_t MainState(stateMachineInst_t * const pInstance, uint16_t event) {
    wifi_manager_stateMachineReturnStatus_t returnStatus = WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_HANDLED;
    switch (event) {
        case WIFI_MANAGER_EVENT_ENTRY:
            returnStatus = WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_HANDLED;
            pInstance->pTcpServerData = &gTcpServerData; //TODO(Daqifi): Remove from there here
            TcpServer_Initialize(pInstance->pTcpServerData);
            ResetAllEventFlags(&pInstance->eventFlags);
            pInstance->udpServerSocket = -1;
            pInstance->wdrvHandle = DRV_HANDLE_INVALID;
            if (pInstance->pWifiSettings->isEnabled) {
                if (pInstance->pWifiSettings->isOtaModeEnabled)
                    SendEvent(WIFI_MANAGER_EVENT_OTA_MODE_INIT);
                else
                    SendEvent(WIFI_MANAGER_EVENT_INIT);
            }
            break;
        case WIFI_MANAGER_EVENT_OTA_MODE_INIT:
            returnStatus = WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_HANDLED;
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_BUSY) {
                SendEvent(WIFI_MANAGER_EVENT_OTA_MODE_INIT);
                break;
            }
            sysObj.drvWifiWinc = WDRV_WINC_Initialize(0, NULL);
            SendEvent(WIFI_MANAGER_EVENT_OTA_MODE_READY);
        case WIFI_MANAGER_EVENT_OTA_MODE_READY:
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_BUSY) {
                SendEvent(WIFI_MANAGER_EVENT_OTA_MODE_INIT);
                break;
            }
            wifi_serial_bridge_Init(&pInstance->serialBridgeContext);
            SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_OTA_MODE_READY);            
            break;
        case WIFI_MANAGER_EVENT_INIT:
            returnStatus = WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_HANDLED;

            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_UNINITIALIZED) {
                sysObj.drvWifiWinc = WDRV_WINC_Initialize(0, NULL);
            }

            if (WDRV_WINC_Status(sysObj.drvWifiWinc) != SYS_STATUS_READY) {
                //wait for initialization to complete 
                SendEvent(WIFI_MANAGER_EVENT_INIT);
                break;
            }
            pInstance->wdrvHandle = WDRV_WINC_Open(0, 0);
            WDRV_WINC_EthernetAddressGet(pInstance->wdrvHandle, pInstance->pWifiSettings->macAddr.addr);
            if (pInstance->wdrvHandle == DRV_HANDLE_INVALID) {
                SendEvent(WIFI_MANAGER_EVENT_ERROR);
                LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                break;
            }
            if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetDefaults(&pInstance->bssCtx)) {
                SendEvent(WIFI_MANAGER_EVENT_ERROR);
                LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                break;
            }
            if (pInstance->pWifiSettings->networkMode == WIFI_API_NETWORK_MODE_STA) {
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetSSID(&pInstance->bssCtx, (uint8_t*) pInstance->pWifiSettings->ssid, strlen(pInstance->pWifiSettings->ssid))) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetChannel(&pInstance->bssCtx, WDRV_WINC_CID_ANY)) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                if (pInstance->pWifiSettings->securityMode == WIFI_API_SEC_OPEN) {
                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_AuthCtxSetOpen(&pInstance->authCtx)) {
                        SendEvent(WIFI_MANAGER_EVENT_ERROR);
                        LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                        break;
                    }
                } else if (pInstance->pWifiSettings->securityMode == WIFI_API_SEC_WPA_AUTO_WITH_PASS_PHRASE) {
                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_AuthCtxSetWPA(&pInstance->authCtx, (uint8_t*) pInstance->pWifiSettings->passKey, pInstance->pWifiSettings->passKeyLength)) {
                        SendEvent(WIFI_MANAGER_EVENT_ERROR);
                        LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                        break;
                    }

                } else {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_IPUseDHCPSet(pInstance->wdrvHandle, &DhcpEventCallback)) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSConnect(pInstance->wdrvHandle, &pInstance->bssCtx, &pInstance->authCtx, &StaEventCallback)) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_STARTED);


            } else {
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetSSID(&pInstance->bssCtx, (uint8_t*) DEFAULT_WIFI_AP_SSID, strlen(DEFAULT_WIFI_AP_SSID))) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetChannel(&pInstance->bssCtx, WDRV_WINC_CID_2_4G_CH1)) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_AuthCtxSetOpen(&pInstance->authCtx)) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                gStateMachineContext.pWifiSettings->ipAddr.Val = inet_addr(DEFAULT_NETWORK_GATEWAY_IP_ADDRESS);
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_IPDHCPServerConfigure(pInstance->wdrvHandle, inet_addr(DEFAULT_NETWORK_GATEWAY_IP_ADDRESS), inet_addr(DEFAULT_NETWORK_IP_MASK), &DhcpEventCallback)) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_APStart(pInstance->wdrvHandle, &pInstance->bssCtx, &pInstance->authCtx, NULL, &ApEventCallback)) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }

                SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_AP_STARTED);
            }
            WDRV_WINC_SocketRegisterEventCallback(pInstance->wdrvHandle, &SocketEventCallback);
            break;
        case WIFI_MANAGER_EVENT_STA_CONNECTED:
            returnStatus = WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_HANDLED;
            SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_CONNECTED);
            if (!GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_OPEN)) {
                if (!OpenUdpSocket(&pInstance->udpServerSocket)) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
            }
            if (!GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_TCP_SOCKET_OPEN)) {
                TcpServer_OpenSocket(pInstance->pWifiSettings->tcpPort);
                if (pInstance->pTcpServerData->serverSocket < 0) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
            }
            SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_TCP_SOCKET_OPEN);
            SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_OPEN);
            break;
        case WIFI_MANAGER_EVENT_STA_DISCONNECTED:
            returnStatus = WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_HANDLED;
            ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_CONNECTED);
            CloseUdpSocket(&pInstance->udpServerSocket);
            TcpServer_CloseSocket();
            ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_TCP_SOCKET_OPEN);
            ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_OPEN);
            SendEvent(WIFI_MANAGER_EVENT_ERROR);
            break;
        case WIFI_MANAGER_EVENT_REINIT:
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_BUSY) {
                SendEvent(WIFI_MANAGER_EVENT_REINIT);
                break;
            }
            returnStatus = WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_TRAN;
            pInstance->nextState = MainState;
            if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_OTA_MODE_READY))
                wifi_serial_bridge_interface_DeInit();
            if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_OPEN))
                CloseUdpSocket(&pInstance->udpServerSocket);
            if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_TCP_SOCKET_OPEN))
                TcpServer_CloseSocket();
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) != SYS_STATUS_UNINITIALIZED) {
                WDRV_WINC_Close(pInstance->wdrvHandle);
                WDRV_WINC_Deinitialize(sysObj.drvWifiWinc);
                LOG_D("WiFi de-initializing\r\n");
            }

            break;
        case WIFI_MANAGER_EVENT_DEINIT:
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_BUSY) {
                SendEvent(WIFI_MANAGER_EVENT_REINIT);
                break;
            }
            returnStatus = WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_TRAN;
            pInstance->nextState = MainState;
            if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_OTA_MODE_READY))
                wifi_serial_bridge_interface_DeInit();
            if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_OPEN))
                CloseUdpSocket(&pInstance->udpServerSocket);
            if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_TCP_SOCKET_OPEN))
                TcpServer_CloseSocket();
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) != SYS_STATUS_UNINITIALIZED) {
                WDRV_WINC_Close(pInstance->wdrvHandle);
                WDRV_WINC_Deinitialize(sysObj.drvWifiWinc);
                LOG_D("WiFi de-initializing\r\n");
            }
            break;
        case WIFI_MANAGER_EVENT_UDP_SOCKET_CONNECTED:
            SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_CONNECTED);
            break;
        case WIFI_MANAGER_EVENT_EXIT:
            xQueueReset(gEventQH);
            returnStatus = WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_HANDLED;
            break;
        case WIFI_MANAGER_EVENT_ERROR:
            SendEvent(WIFI_MANAGER_EVENT_REINIT);
            LOG_E("[%s:%d]Error WiFi", __FILE__, __LINE__);
            break;
        default:
            break;
    }
    return returnStatus;
}


bool wifi_manager_Init(WifiSettings * pSettings) {

    if (gEventQH == NULL)
        gEventQH = xQueueCreate(20, sizeof (wifi_manager_event_t));
    else return true;
    if (pSettings != NULL)
        gStateMachineContext.pWifiSettings = pSettings;
    gStateMachineContext.pWifiSettings->isOtaModeEnabled = false;
    gStateMachineContext.pWifiSettings->isEnabled = 1;
    gStateMachineContext.active = MainState;
    gStateMachineContext.active(&gStateMachineContext,WIFI_MANAGER_EVENT_ENTRY);
    gStateMachineContext.nextState = NULL;

    return true;
}

bool wifi_manager_Deinit() {
    const tPowerData *pPowerState = BoardData_Get(
            BOARDATA_POWER_DATA,
            0);
    if (NULL != pPowerState &&
            pPowerState->powerState < POWERED_UP) {
        LogMessage("Board must be powered-on for WiFi operations\n\r");
        return false;
    }
    gStateMachineContext.pWifiSettings->isEnabled = 0;
    SendEvent(WIFI_MANAGER_EVENT_DEINIT);
    return true;
}

bool wifi_manager_UpdateNetworkSettings(WifiSettings * pSettings) {

    const tPowerData *pPowerState = (tPowerData *) BoardData_Get(
            BOARDATA_POWER_DATA,
            0);
    if (NULL != pPowerState &&
            pPowerState->powerState < POWERED_UP) {
        LogMessage("Board must be powered-on for WiFi operations\n\r");
        return false;
    }
    if (pSettings != NULL && gStateMachineContext.pWifiSettings != NULL) {
        memcpy(gStateMachineContext.pWifiSettings, pSettings, sizeof (WifiSettings));
    }
    SendEvent(WIFI_MANAGER_EVENT_REINIT);
    return true;
}

size_t wifi_manager_GetWriteBuffFreeSize() {
    return TcpServer_WriteBuffFreeSize();
}

size_t wifi_manager_WriteToBuffer(const char* pData, size_t len) {
    return TcpServer_WriteBuffer(pData, len);
}

void wifi_manager_ProcessState() {
    wifi_manager_event_t event;
    wifi_manager_stateMachineReturnStatus_t ret;
    while (gEventQH == NULL) {
        return;
    }
    if (GetEventFlagStatus(gStateMachineContext.eventFlags, WIFI_MANAGER_STATE_FLAG_OTA_MODE_READY)) {
        wifi_serial_bridge_Process(&gStateMachineContext.serialBridgeContext);
    } else {
        TcpServer_ProcessSendBuffer();
    }
    if (xQueueReceive(gEventQH, &event, 1) != pdPASS) {
        return;
    }
    ret = gStateMachineContext.active(&gStateMachineContext, event);
    if (ret == WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_TRAN) {
        gStateMachineContext.active(&gStateMachineContext,WIFI_MANAGER_EVENT_EXIT);
        if (gStateMachineContext.nextState != NULL) {
            gStateMachineContext.active = gStateMachineContext.nextState;
            gStateMachineContext.active(&gStateMachineContext,WIFI_MANAGER_EVENT_ENTRY);
        } else {
            LOG_E("%s : %d : State Change Requested, but NextSate is NULL", __func__, __LINE__);
        }
    }
}

wifi_tcp_server_context_t* wifi_manager_GetTcpServerContext() {
    return gStateMachineContext.pTcpServerData;
}