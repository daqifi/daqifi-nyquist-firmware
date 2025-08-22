#include "wifi_manager.h"
#include "wdrv_winc_client_api.h"
#include "Util/Logger.h"
#include "wifi_tcp_server.h"
#include "state/data/BoardData.h"
#include "wifi_serial_bridge.h"
#include "wifi_serial_bridge_interface.h"
#include "driver/winc/include/dev/wdrv_winc_gpio.h"
#include "driver/winc/include/drv/driver/m2m_wifi.h"

#define UNUSED(x) (void)(x)
#define WIFI_MANAGER_UDP_LISTEN_PORT         (uint16_t)30303

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
        WIFI_MANAGER_STATE_FLAG_INITIALIZED = 1 << 0,
        WIFI_MANAGER_STATE_FLAG_AP_STARTED = 1 << 1,
        WIFI_MANAGER_STATE_FLAG_STA_STARTED = 1 << 2,
        WIFI_MANAGER_STATE_FLAG_STA_CONNECTED = 1 << 3,
        WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_OPEN = 1 << 4,
        WIFI_MANAGER_STATE_FLAG_TCP_SOCKET_OPEN = 1 << 5,
        WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_CONNECTED = 1 << 6,
        WIFI_MANAGER_STATE_FLAG_TCP_SOCKET_CONNECTED = 1 << 7,
        WIFI_MANAGER_STATE_FLAG_OTA_MODE_READY = 1 << 8,
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
    wifi_manager_stateFlag_t eventFlags;
    wifi_manager_settings_t *pWifiSettings;
    DRV_HANDLE wdrvHandle;
    WDRV_WINC_ASSOC_HANDLE assocHandle;  // Store association handle for RSSI queries
    SOCKET udpServerSocket;
    wifi_tcp_server_context_t *pTcpServerContext;
    WDRV_WINC_BSS_CONTEXT bssCtx;
    WDRV_WINC_AUTH_CONTEXT authCtx;
    wifi_serial_bridge_context_t serialBridgeContext;
    tstrM2mRev wifiFirmwareVersion;
    TaskHandle_t fwUpdateTaskHandle;
    uint8_t staReconnectAttempts;  // Track STA reconnection attempts
};
//===============Private Function Declrations================
static void __attribute__((unused)) SetEventFlag(wifi_manager_stateFlag_t *pEventFlagState, uint16_t flag);
static void __attribute__((unused)) ResetEventFlag(wifi_manager_stateFlag_t *pEventFlagState, uint16_t flag);
static uint8_t __attribute__((unused)) GetEventFlagStatus(wifi_manager_stateFlag_t eventFlagState, uint16_t flag);
static bool SendEvent(wifi_manager_event_t event);
static wifi_manager_stateMachineReturnStatus_t MainState(stateMachineInst_t * const pInstance, uint16_t event);

extern bool wifi_tcp_server_TransmitBufferedData();
extern size_t wifi_tcp_server_WriteBuffer(const char* data, size_t len);
extern size_t wifi_tcp_server_GetWriteBuffFreeSize();
extern void wifi_tcp_server_OpenSocket(uint16_t port);
extern void wifi_tcp_server_CloseSocket();
extern void wifi_tcp_server_CloseClientSocket();
extern bool wifi_tcp_server_ProcessReceivedBuff();
extern void wifi_tcp_server_Initialize(wifi_tcp_server_context_t *pServerData);
//===========================================================
//=====================Private Variables=====================
static stateMachineInst_t gStateMachineContext;
static QueueHandle_t gEventQH = NULL;
//(TODO(Daqifi):Remove from here
static wifi_tcp_server_context_t gTcpServerContext;

// Volatile flags for on-demand RSSI queries
static volatile bool gRssiUpdatePending = false;
static volatile bool gRssiUpdateComplete = false;
static volatile uint8_t gLastRssiPercentage = 0;

//===========================================================
//=====================Private Callbacks=====================

static void RssiEventCallback(DRV_HANDLE handle, WDRV_WINC_ASSOC_HANDLE assocHandle, int8_t rssi) {
    // In ISR/callback context - keep this minimal
    // Validate this callback is for our current association
    taskENTER_CRITICAL();
    if (assocHandle != gStateMachineContext.assocHandle) {
        taskEXIT_CRITICAL();
        return;
    }
    
    // Convert RSSI (dBm) to a 0-100 scale for compatibility
    // RSSI typically ranges from -100 dBm (poor) to -30 dBm (excellent)
    int signalStrength;
    if (rssi >= -30) {
        signalStrength = 100;
    } else if (rssi <= -100) {
        signalStrength = 0;
    } else {
        // Linear scale from -100 to -30
        signalStrength = ((rssi + 100) * 100) / 70;
    }
    
    // Update all shared state atomically within critical section
    gLastRssiPercentage = (uint8_t)signalStrength;
    if (gStateMachineContext.pWifiSettings != NULL) {
        gStateMachineContext.pWifiSettings->rssi_percent = (uint8_t)signalStrength;
    }
    if (gRssiUpdatePending) {
        gRssiUpdateComplete = true;
        gRssiUpdatePending = false;
    }
    taskEXIT_CRITICAL();
    // No logging in ISR/callback context - keep it minimal
}

static void DhcpEventCallback(DRV_HANDLE handle, uint32_t ipAddress) {
    char s[20];
    UNUSED(s);
    
    if (GetEventFlagStatus(gStateMachineContext.eventFlags, WIFI_MANAGER_STATE_FLAG_AP_STARTED)) {
        // In AP mode, this callback is triggered when DHCP assigns an IP to a client
        LOG_D("AP Mode: DHCP assigned IP %s to a client\r\n", inet_ntop(AF_INET, &ipAddress, s, sizeof (s)));
    } else if (GetEventFlagStatus(gStateMachineContext.eventFlags, WIFI_MANAGER_STATE_FLAG_STA_STARTED)) {
        // In STA mode, this is our IP address from the DHCP server
        gStateMachineContext.pWifiSettings->ipAddr.Val = ipAddress;
        LOG_D("STA Mode: Station IP address is %s\r\n", inet_ntop(AF_INET, &ipAddress, s, sizeof (s)));
    }
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
        gStateMachineContext.assocHandle = assocHandle;  // Store association handle for RSSI queries
        SendEvent(WIFI_MANAGER_EVENT_STA_CONNECTED);
    } else if (WDRV_WINC_CONN_STATE_DISCONNECTED == currentState && errorCode != WDRV_WINC_CONN_ERROR_INPROGRESS) {
        LOG_D("STA mode: Station disconnected\r\n");
        gStateMachineContext.assocHandle = WDRV_WINC_ASSOC_HANDLE_INVALID;  // Clear association handle
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
                    LOG_D("UDP socket bind successful (socket=%d), starting recvfrom on port %d\r\n", socket, WIFI_MANAGER_UDP_LISTEN_PORT);
                    recvfrom(socket, udpBuffer, UDP_BUFFER_SIZE, 0);
                    SendEvent(WIFI_MANAGER_EVENT_UDP_SOCKET_CONNECTED);
                } else if (socket == gStateMachineContext.pTcpServerContext->serverSocket) {
                    listen(gStateMachineContext.pTcpServerContext->serverSocket, 0);
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

            if ((NULL != pListenMessage) && (0 == pListenMessage->status) && (socket == gStateMachineContext.pTcpServerContext->serverSocket)) {
                accept(gStateMachineContext.pTcpServerContext->serverSocket, NULL, NULL);
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
                if (gStateMachineContext.pTcpServerContext->client.clientSocket >= 0) // close any open client (only one client supported at one time)
                {
                    wifi_tcp_server_CloseClientSocket();
                }
                gStateMachineContext.pTcpServerContext->client.clientSocket = pAcceptMessage->sock;
                LOG_D("Connection from %s:%d\r\n", inet_ntop(AF_INET, &pAcceptMessage->strAddr.sin_addr.s_addr, s, sizeof (s)), pAcceptMessage->strAddr.sin_port);
                recv(gStateMachineContext.pTcpServerContext->client.clientSocket, gStateMachineContext.pTcpServerContext->client.readBuffer, WIFI_RBUFFER_SIZE, 0);

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
                gStateMachineContext.pTcpServerContext->client.readBufferLength = pRecvMessage->s16BufferSize;
                wifi_tcp_server_ProcessReceivedBuff();
                recv(gStateMachineContext.pTcpServerContext->client.clientSocket, gStateMachineContext.pTcpServerContext->client.readBuffer, WIFI_RBUFFER_SIZE, 0);

            } else {
                LOG_E("[%s:%d]Error Socket MSG Recv", __FILE__, __LINE__);
                wifi_tcp_server_CloseClientSocket();
            }
            break;
        }
        case SOCKET_MSG_RECVFROM:
        {
            tstrSocketRecvMsg *pstrRx = (tstrSocketRecvMsg*) pMessage;
            if (pstrRx->s16BufferSize > 0 && socket == gStateMachineContext.udpServerSocket) {
                //get the remote host address and port number
                uint16_t u16port = _htons(pstrRx->strRemoteAddr.sin_port);
                uint32_t strRemoteHostAddr = pstrRx->strRemoteAddr.sin_addr.s_addr;
                char s[20];
                inet_ntop(AF_INET, &strRemoteHostAddr, s, sizeof (s));
                LOG_D("\r\nUDP Discovery: Received frame with size=%d\r\nHost address=%s\r\nPort number = %d\r\n", pstrRx->s16BufferSize, s, u16port);
                LOG_D("UDP Discovery: Frame Data : %.*s\r\n", pstrRx->s16BufferSize, (char*) pstrRx->pu8Buffer);
                uint16_t announcePacktLen = UDP_BUFFER_SIZE;
                wifi_manager_FormUdpAnnouncePacketCB(gStateMachineContext.pWifiSettings, udpBuffer, &announcePacktLen);
                struct sockaddr_in addr;
                addr.sin_family = AF_INET;
                // For backward compatibility: if source port is 30303, respond to 30303
                // Otherwise respond to source port (standard UDP behavior)
                if (u16port == WIFI_MANAGER_UDP_LISTEN_PORT) {
                    addr.sin_port = _htons(WIFI_MANAGER_UDP_LISTEN_PORT);
                } else {
                    addr.sin_port = pstrRx->strRemoteAddr.sin_port;
                }
                addr.sin_addr.s_addr = inet_addr(s);
                LOG_D("UDP Discovery: Sending response to %s:%d (packet size: %d, socket=%d)\r\n", s, _htons(addr.sin_port), announcePacktLen, socket);
                // Use the socket parameter, not gStateMachineContext.udpServerSocket
                sendto(socket, udpBuffer, announcePacktLen, 0, (struct sockaddr*) &addr, sizeof (struct sockaddr_in));
                recvfrom(socket, udpBuffer, UDP_BUFFER_SIZE, 0);
            } else if (pstrRx->s16BufferSize > 0) {
                LOG_D("UDP packet received on unexpected socket %d (expected %d)\r\n", socket, gStateMachineContext.udpServerSocket);
            }
        }
            break;

        case SOCKET_MSG_SENDTO:
        {
            LOG_D("UDP Send Success\r\n");
            break;
        }
        case SOCKET_MSG_SEND:
        {
            gStateMachineContext.pTcpServerContext->client.tcpSendPending=0;
        }
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

/**
 * Helper function to properly reset the WINC module after deinit.
 * This works around a bug in the Microchip WINC driver where WDRV_WINC_Deinitialize
 * leaves the module in reset state by asserting reset but never deasserting it.
 */
static void wifi_manager_FixWincResetState(void) {
    // The WINC driver's deinit leaves the module with:
    // - CHIP_EN deasserted (low)
    // - RESET_N asserted (low) 
    // This leaves the module in permanent reset. We need to take it out of reset
    // so it can be re-initialized later.
    
    // Assert chip enable and deassert reset to take module out of reset state
    WDRV_WINC_GPIOChipEnableAssert();
    WDRV_WINC_GPIOResetDeassert();
}

static void __attribute__((unused)) ResetAllEventFlags(wifi_manager_stateFlag_t *pEventFlagState) {
    memset(pEventFlagState, 0, sizeof (wifi_manager_stateFlag_t));
}

static bool CloseUdpSocket(SOCKET *pSocket) {
    if (*pSocket != -1) {
        // The WINC driver's shutdown() automatically closes the socket
        shutdown(*pSocket);
    }
    *pSocket = -1;
    return true;
}

static bool OpenUdpSocket(SOCKET *pSocket) {
    bool returnStatus = false;
    *pSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (*pSocket >= 0) {
        LOG_D("UDP socket created successfully (socket=%d)\r\n", *pSocket);
        struct sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_port = _htons(WIFI_MANAGER_UDP_LISTEN_PORT);
        addr.sin_addr.s_addr = 0;  // Listen on all interfaces (same as INADDR_ANY)
        int bindResult = bind(*pSocket, (struct sockaddr*) &addr, sizeof (struct sockaddr_in));
        LOG_D("UDP socket bind result: %d\r\n", bindResult);
        returnStatus = true;
    } else {
        LOG_E("Failed to create UDP socket\r\n");
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
            pInstance->pTcpServerContext = &gTcpServerContext; 
            wifi_tcp_server_Initialize(pInstance->pTcpServerContext);
            memset(&pInstance->wifiFirmwareVersion, 0, sizeof (tstrM2mRev));
            ResetAllEventFlags(&pInstance->eventFlags);
            pInstance->udpServerSocket = -1;
            pInstance->wdrvHandle = DRV_HANDLE_INVALID;
            pInstance->assocHandle = WDRV_WINC_ASSOC_HANDLE_INVALID;
            if (pInstance->pWifiSettings->isEnabled) {
                if (pInstance->pWifiSettings->isOtaModeEnabled)
                    SendEvent(WIFI_MANAGER_EVENT_OTA_MODE_INIT);
                else
                    SendEvent(WIFI_MANAGER_EVENT_INIT);
            }
            pInstance->pWifiSettings->isOtaModeEnabled=0;
            break;
        case WIFI_MANAGER_EVENT_OTA_MODE_INIT:
            returnStatus = WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_HANDLED;
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_BUSY) {
                SendEvent(WIFI_MANAGER_EVENT_OTA_MODE_INIT);
                break;
            }
            sysObj.drvWifiWinc = WDRV_WINC_Initialize(0, NULL);
            SendEvent(WIFI_MANAGER_EVENT_OTA_MODE_READY);
            break;
        case WIFI_MANAGER_EVENT_OTA_MODE_READY:
        {
            SYS_STATUS wincStatus = WDRV_WINC_Status(sysObj.drvWifiWinc);
            if (wincStatus == SYS_STATUS_BUSY) {
                SendEvent(WIFI_MANAGER_EVENT_OTA_MODE_INIT);
                break;
            }
            SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_INITIALIZED);
            if (wincStatus == SYS_STATUS_READY) {
                if (m2m_wifi_get_firmware_version(&pInstance->wifiFirmwareVersion) != M2M_SUCCESS) {
                    memset(&pInstance->wifiFirmwareVersion, 0, sizeof (tstrM2mRev));
                }
            }
            wifi_serial_bridge_Init(&pInstance->serialBridgeContext);
            SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_OTA_MODE_READY);
            vTaskResume(pInstance->fwUpdateTaskHandle);
        }
            break;
        case WIFI_MANAGER_EVENT_INIT:
            returnStatus = WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_HANDLED;

            static bool initErrorLogged = false;
            
            // Check if we're already initialized (mode switch without deinit)
            bool alreadyInitialized = GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_INITIALIZED);
            
            if (!alreadyInitialized) {
                if (sysObj.drvWifiWinc == SYS_MODULE_OBJ_INVALID || 
                    WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_UNINITIALIZED) {
                    sysObj.drvWifiWinc = WDRV_WINC_Initialize(0, NULL);
                }

                if (WDRV_WINC_Status(sysObj.drvWifiWinc) != SYS_STATUS_READY) {
                    // Log error only once when entering error state
                    if (!initErrorLogged) {
                        LOG_E("WiFi driver not ready, retrying initialization...\r\n");
                        initErrorLogged = true;
                    }
                    //wait for initialization to complete
                    SendEvent(WIFI_MANAGER_EVENT_INIT);
                    break;
                }
                // Reset error flag on success
                if (initErrorLogged) {
                    LOG_D("WiFi driver initialization succeeded\r\n");
                    initErrorLogged = false;
                }
            }
            SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_INITIALIZED);
            if (m2m_wifi_get_firmware_version(&pInstance->wifiFirmwareVersion) != M2M_SUCCESS) {
                memset(&pInstance->wifiFirmwareVersion, 0, sizeof (tstrM2mRev));
            }
            
            // Only open driver handle if not already open
            if (pInstance->wdrvHandle == DRV_HANDLE_INVALID) {
                pInstance->wdrvHandle = WDRV_WINC_Open(0, 0);
                WDRV_WINC_EthernetAddressGet(pInstance->wdrvHandle, pInstance->pWifiSettings->macAddr.addr);
                if (pInstance->wdrvHandle == DRV_HANDLE_INVALID) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
            }
            if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetDefaults(&pInstance->bssCtx)) {
                SendEvent(WIFI_MANAGER_EVENT_ERROR);
                LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                break;
            }
            if (pInstance->pWifiSettings->networkMode == WIFI_MANAGER_NETWORK_MODE_STA) {
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
                if (pInstance->pWifiSettings->securityMode == WIFI_MANAGER_SECURITY_MODE_OPEN) {
                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_AuthCtxSetOpen(&pInstance->authCtx)) {
                        SendEvent(WIFI_MANAGER_EVENT_ERROR);
                        LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                        break;
                    }
                } else if (pInstance->pWifiSettings->securityMode == WIFI_MANAGER_SECURITY_MODE_WPA_AUTO_WITH_PASS_PHRASE) {
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
                // IMPORTANT: Register socket callback BEFORE connecting
                // This ensures we're ready for socket events when connection succeeds
                WDRV_WINC_SocketRegisterEventCallback(pInstance->wdrvHandle, &SocketEventCallback);
                
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSConnect(pInstance->wdrvHandle, &pInstance->bssCtx, &pInstance->authCtx, &StaEventCallback)) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_STARTED);

            } else {
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetSSID(&pInstance->bssCtx, (uint8_t*) pInstance->pWifiSettings->ssid, strlen(pInstance->pWifiSettings->ssid))) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetChannel(&pInstance->bssCtx, WDRV_WINC_CID_2_4G_CH1)) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                if (pInstance->pWifiSettings->securityMode == WIFI_MANAGER_SECURITY_MODE_OPEN) {
                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_AuthCtxSetOpen(&pInstance->authCtx)) {
                        SendEvent(WIFI_MANAGER_EVENT_ERROR);
                        LOG_E("Error setting Open auth context\r\n");
                        break;
                    }
                } else if (pInstance->pWifiSettings->securityMode == WIFI_MANAGER_SECURITY_MODE_WPA_AUTO_WITH_PASS_PHRASE) {
                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_AuthCtxSetWPA(&pInstance->authCtx, (uint8_t*) pInstance->pWifiSettings->passKey, pInstance->pWifiSettings->passKeyLength)) {
                        SendEvent(WIFI_MANAGER_EVENT_ERROR);
                        LOG_E("Error setting WPA auth context\r\n");
                        break;
                    }
                } else {
                    // Add this block to handle errors
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("AP Mode: Unsupported security mode: %d\r\n", pInstance->pWifiSettings->securityMode);
                    break;
                }
                // Use runtime configured IP address for AP mode instead of hardcoded default
                // Validate IP address - if it's 0.0.0.0, use the gateway address
                uint32_t apIpAddr = pInstance->pWifiSettings->ipAddr.Val;
                if (apIpAddr == 0 || apIpAddr == inet_addr("0.0.0.0")) {
                    LOG_E("Invalid AP IP address 0.0.0.0, using gateway address instead\r\n");
                    apIpAddr = pInstance->pWifiSettings->gateway.Val;
                    if (apIpAddr == 0 || apIpAddr == inet_addr("0.0.0.0")) {
                        LOG_E("Gateway also invalid, using default 192.168.1.1\r\n");
                        apIpAddr = inet_addr("192.168.1.1");
                    }
                }
                
                LOG_D("Configuring AP DHCP server with IP: %d.%d.%d.%d, Mask: %d.%d.%d.%d\r\n",
                    ((uint8_t*)&apIpAddr)[0], ((uint8_t*)&apIpAddr)[1],
                    ((uint8_t*)&apIpAddr)[2], ((uint8_t*)&apIpAddr)[3],
                    pInstance->pWifiSettings->ipMask.v[0], pInstance->pWifiSettings->ipMask.v[1],
                    pInstance->pWifiSettings->ipMask.v[2], pInstance->pWifiSettings->ipMask.v[3]);
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_IPDHCPServerConfigure(pInstance->wdrvHandle, 
                    apIpAddr, 
                    pInstance->pWifiSettings->ipMask.Val, 
                    &DhcpEventCallback)) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
                // Small delay to ensure BSS context is fully processed before starting AP
                vTaskDelay(pdMS_TO_TICKS(100));
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_APStart(pInstance->wdrvHandle, &pInstance->bssCtx, &pInstance->authCtx, NULL, &ApEventCallback)) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }

                SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_AP_STARTED);
                
                // IMPORTANT: Register socket callback BEFORE opening sockets
                // This ensures we don't miss the SOCKET_MSG_BIND event
                WDRV_WINC_SocketRegisterEventCallback(pInstance->wdrvHandle, &SocketEventCallback);
                
                // Open UDP socket for discovery in AP mode
                if (!GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_OPEN)) {
                    if (!OpenUdpSocket(&pInstance->udpServerSocket)) {
                        LOG_E("Failed to open UDP socket in AP mode\r\n");
                    } else {
                        SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_OPEN);
                        LOG_D("UDP discovery socket opened in AP mode on port %d\r\n", WIFI_MANAGER_UDP_LISTEN_PORT);
                    }
                }
                
                // Open TCP server socket in AP mode
                if (!GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_TCP_SOCKET_OPEN)) {
                    wifi_tcp_server_OpenSocket(pInstance->pWifiSettings->tcpPort);
                    if (pInstance->pTcpServerContext->serverSocket < 0) {
                        LOG_E("Failed to open TCP server socket in AP mode\r\n");
                    } else {
                        SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_TCP_SOCKET_OPEN);
                        LOG_D("TCP server socket opened in AP mode on port %d\r\n", pInstance->pWifiSettings->tcpPort);
                    }
                }
            }
            break;
        case WIFI_MANAGER_EVENT_STA_CONNECTED:
            returnStatus = WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_HANDLED;
            SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_CONNECTED);
            
            // Reset reconnect attempts on successful connection
            pInstance->staReconnectAttempts = 0;
            LOG_D("STA connection successful, reset reconnect counter\r\n");
            
            // Request initial RSSI after connection
            if (pInstance->assocHandle != WDRV_WINC_ASSOC_HANDLE_INVALID) {
                int8_t rssi;
                WDRV_WINC_STATUS status = WDRV_WINC_AssocRSSIGet(pInstance->assocHandle, &rssi, RssiEventCallback);
                if (status == WDRV_WINC_STATUS_OK) {
                    // RSSI was cached, update immediately
                    RssiEventCallback(pInstance->wdrvHandle, pInstance->assocHandle, rssi);
                }
                // else if status == WDRV_WINC_STATUS_RETRY_REQUEST, callback will be called later
            }
            
            if (!GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_OPEN)) {
                if (!OpenUdpSocket(&pInstance->udpServerSocket)) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("[%s:%d]Error WiFi init", __FILE__, __LINE__);
                    break;
                }
            }
            if (!GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_TCP_SOCKET_OPEN)) {
                wifi_tcp_server_OpenSocket(pInstance->pWifiSettings->tcpPort);
                if (pInstance->pTcpServerContext->serverSocket < 0) {
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
            wifi_tcp_server_CloseSocket();
            ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_TCP_SOCKET_OPEN);
            ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_OPEN);
            
            // Clear signal strength on disconnect
            if (pInstance->pWifiSettings != NULL) {
                pInstance->pWifiSettings->rssi_percent = 0;
            }
            
            // Only increment attempts if we're in STA mode
            if (pInstance->pWifiSettings->networkMode == WIFI_MANAGER_NETWORK_MODE_STA) {
                pInstance->staReconnectAttempts++;
                LOG_D("STA reconnect attempt #%d\r\n", pInstance->staReconnectAttempts);
            }
            
            SendEvent(WIFI_MANAGER_EVENT_ERROR);
            break;
        case WIFI_MANAGER_EVENT_REINIT:
            if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_OTA_MODE_READY)) {
                //If ota is running and again initialized, then deinit OTA
                ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_OTA_MODE_READY);
                pInstance->pWifiSettings->isOtaModeEnabled = 0;
                wifi_serial_bridge_interface_DeInit();
            }
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_BUSY) {
                vTaskDelay(pdMS_TO_TICKS(50)); // Add a small delay to prevent busy-looping
                SendEvent(WIFI_MANAGER_EVENT_REINIT);
                break;
            }
            
            // IMPORTANT: Do NOT use WDRV_WINC_Deinitialize for runtime reconfiguration!
            // The Microchip driver has a bug where it asserts reset but never deasserts it,
            // leaving the WINC1500 in a permanent reset state. Instead, we perform a soft
            // restart by stopping and restarting AP/STA mode with new settings.
            
            // Settings have been updated - check if we need to stop WiFi operations
            // If WiFi is disabled, we should stop operations but still accept the settings update
            if (!pInstance->pWifiSettings->isEnabled) {
                LOG_D("WiFi disabled - stopping active operations but keeping settings\r\n");
                
                // Reset reconnect counter when WiFi is disabled
                pInstance->staReconnectAttempts = 0;
                
                // Stop AP if running
                if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_AP_STARTED)) {
                    // Close sockets before stopping AP
                    CloseUdpSocket(&pInstance->udpServerSocket);
                    wifi_tcp_server_CloseSocket();
                    ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_TCP_SOCKET_OPEN);
                    ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_OPEN);
                    
                    WDRV_WINC_APStop(pInstance->wdrvHandle);
                    ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_AP_STARTED);
                }
                
                // Disconnect STA if connected
                if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_CONNECTED)) {
                    WDRV_WINC_BSSDisconnect(pInstance->wdrvHandle);
                    ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_CONNECTED);
                }
                ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_STARTED);
                
                // Close sockets
                if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_OPEN)) {
                    CloseUdpSocket(&pInstance->udpServerSocket);
                }
                if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_TCP_SOCKET_OPEN)) {
                    wifi_tcp_server_CloseSocket();
                }
                
                LOG_D("WiFi operations stopped, settings updated\r\n");
                // Settings have been updated even though WiFi is disabled
                // This allows the desktop app to configure WiFi while it's off
                break;
            }
            
            // Simple mode-based logic - let MainState handle initialization
            // Just clean up the current mode if we're switching
            if (pInstance->pWifiSettings->networkMode == WIFI_MANAGER_NETWORK_MODE_AP) {
                // Switching to AP mode - disconnect STA if connected
                if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_CONNECTED) ||
                    GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_STARTED)) {
                    LOG_D("Switching from STA to AP mode\r\n");
                    
                    // Reset reconnect counter when switching modes
                    pInstance->staReconnectAttempts = 0;
                    
                    // Disconnect if connected
                    if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_CONNECTED)) {
                        WDRV_WINC_BSSDisconnect(pInstance->wdrvHandle);
                        ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_CONNECTED);
                    }
                    ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_STARTED);
                    
                    // Don't deinitialize - the driver gets into a bad state after deinit
                    // Instead, just wait for STA to disconnect and then configure for AP mode
                    vTaskDelay(pdMS_TO_TICKS(500));  // Let STA fully disconnect
                    
                    // Now configure for AP mode using the existing driver handle
                    // The INIT event will reconfigure for AP without reinitializing
                    SendEvent(WIFI_MANAGER_EVENT_INIT);
                    break;
                }
            }
            else if (pInstance->pWifiSettings->networkMode == WIFI_MANAGER_NETWORK_MODE_STA) {
                // Switching to STA mode - stop AP if running
                if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_AP_STARTED)) {
                    LOG_D("Switching from AP to STA mode\r\n");
                    
                    // Close sockets before stopping AP
                    CloseUdpSocket(&pInstance->udpServerSocket);
                    wifi_tcp_server_CloseSocket();
                    ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_TCP_SOCKET_OPEN);
                    ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_OPEN);
                    
                    WDRV_WINC_APStop(pInstance->wdrvHandle);
                    ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_AP_STARTED);
                    
                    // Don't deinitialize - the driver gets into a bad state (-1) after deinit
                    // Instead, just wait for AP to stop and then configure for STA mode
                    vTaskDelay(pdMS_TO_TICKS(500));  // Let AP fully stop
                    
                    // Now configure for STA mode using the existing driver handle
                    // The INIT event will reconfigure for STA without reinitializing
                    SendEvent(WIFI_MANAGER_EVENT_INIT);
                    break;
                }
            }
            
            // Now handle reconfiguration within the same mode
            if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_AP_STARTED) &&
                pInstance->pWifiSettings->networkMode == WIFI_MANAGER_NETWORK_MODE_AP)
            {
                LOG_D("Restarting Soft AP mode with new settings...\r\n");
                
                // Close sockets before stopping AP
                CloseUdpSocket(&pInstance->udpServerSocket);
                wifi_tcp_server_CloseSocket();
                ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_TCP_SOCKET_OPEN);
                ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_OPEN);
                
                // Stop current AP
                WDRV_WINC_APStop(pInstance->wdrvHandle);
                ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_AP_STARTED);
                
                // Wait for stop to complete
                vTaskDelay(pdMS_TO_TICKS(500));
                
                // Reconfigure with new settings
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetDefaults(&pInstance->bssCtx)) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("Error resetting BSS context\r\n");
                    break;
                }
                
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetSSID(&pInstance->bssCtx, 
                    (uint8_t*) pInstance->pWifiSettings->ssid, 
                    strlen(pInstance->pWifiSettings->ssid))) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("Error setting new SSID\r\n");
                    break;
                }
                
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetChannel(&pInstance->bssCtx, WDRV_WINC_CID_2_4G_CH1)) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("Error setting channel\r\n");
                    break;
                }
                
                // Set auth mode
                if (pInstance->pWifiSettings->securityMode == WIFI_MANAGER_SECURITY_MODE_OPEN) {
                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_AuthCtxSetOpen(&pInstance->authCtx)) {
                        SendEvent(WIFI_MANAGER_EVENT_ERROR);
                        LOG_E("Error setting Open auth context\r\n");
                        break;
                    }
                } else if (pInstance->pWifiSettings->securityMode == WIFI_MANAGER_SECURITY_MODE_WPA_AUTO_WITH_PASS_PHRASE) {
                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_AuthCtxSetWPA(&pInstance->authCtx, 
                        (uint8_t*) pInstance->pWifiSettings->passKey, 
                        pInstance->pWifiSettings->passKeyLength)) {
                        SendEvent(WIFI_MANAGER_EVENT_ERROR);
                        LOG_E("Error setting WPA auth context\r\n");
                        break;
                    }
                }
                
                // Reconfigure DHCP server with updated IP settings
                // Validate IP address - if it's 0.0.0.0, use the gateway address
                uint32_t apIpAddr = pInstance->pWifiSettings->ipAddr.Val;
                if (apIpAddr == 0 || apIpAddr == inet_addr("0.0.0.0")) {
                    LOG_E("Invalid AP IP address 0.0.0.0, using gateway address instead\r\n");
                    apIpAddr = pInstance->pWifiSettings->gateway.Val;
                    if (apIpAddr == 0 || apIpAddr == inet_addr("0.0.0.0")) {
                        LOG_E("Gateway also invalid, using default 192.168.1.1\r\n");
                        apIpAddr = inet_addr("192.168.1.1");
                    }
                }
                
                LOG_D("Reconfiguring AP DHCP server with IP: %d.%d.%d.%d, Mask: %d.%d.%d.%d\r\n",
                    ((uint8_t*)&apIpAddr)[0], ((uint8_t*)&apIpAddr)[1],
                    ((uint8_t*)&apIpAddr)[2], ((uint8_t*)&apIpAddr)[3],
                    pInstance->pWifiSettings->ipMask.v[0], pInstance->pWifiSettings->ipMask.v[1],
                    pInstance->pWifiSettings->ipMask.v[2], pInstance->pWifiSettings->ipMask.v[3]);
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_IPDHCPServerConfigure(pInstance->wdrvHandle, 
                    apIpAddr, 
                    pInstance->pWifiSettings->ipMask.Val, 
                    &DhcpEventCallback)) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("Error reconfiguring DHCP server\r\n");
                    break;
                }
                
                // Restart AP with new settings
                // Small delay to ensure BSS context is fully processed before starting AP
                vTaskDelay(pdMS_TO_TICKS(100));
                if (WDRV_WINC_STATUS_OK != WDRV_WINC_APStart(pInstance->wdrvHandle, 
                    &pInstance->bssCtx, &pInstance->authCtx, NULL, &ApEventCallback)) {
                    SendEvent(WIFI_MANAGER_EVENT_ERROR);
                    LOG_E("Error restarting AP\r\n");
                    break;
                }
                
                SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_AP_STARTED);
                LOG_D("AP restarted with new settings\r\n");
                
                // Re-register socket callback after AP restart
                WDRV_WINC_SocketRegisterEventCallback(pInstance->wdrvHandle, &SocketEventCallback);
                
                // Recreate UDP socket with new IP address
                // Socket was closed before AP restart, need to create new one
                if (!GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_OPEN)) {
                    LOG_D("Creating new UDP socket after IP change\r\n");
                    if (!OpenUdpSocket(&pInstance->udpServerSocket)) {
                        LOG_E("Failed to recreate UDP socket after IP change\r\n");
                    } else {
                        SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_OPEN);
                        LOG_D("UDP socket recreated successfully\r\n");
                    }
                }
            }
            else if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_CONNECTED) ||
                     GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_STARTED))
            {
                // Currently in STA mode and need to update STA settings
                if (pInstance->pWifiSettings->networkMode == WIFI_MANAGER_NETWORK_MODE_STA) {
                    LOG_D("Restarting STA mode with new settings...\r\n");
                    
                    // Disconnect if connected
                    if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_CONNECTED)) {
                        WDRV_WINC_BSSDisconnect(pInstance->wdrvHandle);
                        ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_CONNECTED);
                    }
                    ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_STARTED);
                    
                    // Wait for disconnect
                    vTaskDelay(pdMS_TO_TICKS(500));
                    
                    // Reconfigure BSS context for STA mode
                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetDefaults(&pInstance->bssCtx)) {
                        SendEvent(WIFI_MANAGER_EVENT_ERROR);
                        LOG_E("Error resetting BSS context\r\n");
                        break;
                    }
                    
                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetSSID(&pInstance->bssCtx, 
                        (uint8_t*) pInstance->pWifiSettings->ssid, 
                        strlen(pInstance->pWifiSettings->ssid))) {
                        SendEvent(WIFI_MANAGER_EVENT_ERROR);
                        LOG_E("Error setting SSID for STA\r\n");
                        break;
                    }
                    
                    // Set channel to ANY for STA mode
                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetChannel(&pInstance->bssCtx, WDRV_WINC_CID_ANY)) {
                        SendEvent(WIFI_MANAGER_EVENT_ERROR);
                        LOG_E("Error setting channel for STA\r\n");
                        break;
                    }
                    
                    // Set auth mode
                    if (pInstance->pWifiSettings->securityMode == WIFI_MANAGER_SECURITY_MODE_OPEN) {
                        if (WDRV_WINC_STATUS_OK != WDRV_WINC_AuthCtxSetOpen(&pInstance->authCtx)) {
                            SendEvent(WIFI_MANAGER_EVENT_ERROR);
                            LOG_E("Error setting Open auth context\r\n");
                            break;
                        }
                    } else if (pInstance->pWifiSettings->securityMode == WIFI_MANAGER_SECURITY_MODE_WPA_AUTO_WITH_PASS_PHRASE) {
                        if (WDRV_WINC_STATUS_OK != WDRV_WINC_AuthCtxSetWPA(&pInstance->authCtx, 
                            (uint8_t*) pInstance->pWifiSettings->passKey, 
                            pInstance->pWifiSettings->passKeyLength)) {
                            SendEvent(WIFI_MANAGER_EVENT_ERROR);
                            LOG_E("Error setting WPA auth context\r\n");
                            break;
                        }
                    }
                    
                    // Set DHCP
                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_IPUseDHCPSet(pInstance->wdrvHandle, &DhcpEventCallback)) {
                        SendEvent(WIFI_MANAGER_EVENT_ERROR);
                        LOG_E("Error setting DHCP\r\n");
                        break;
                    }
                    
                    // Register socket callback before reconnecting
                    WDRV_WINC_SocketRegisterEventCallback(pInstance->wdrvHandle, &SocketEventCallback);
                    
                    // Connect to network
                    if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSConnect(pInstance->wdrvHandle, 
                        &pInstance->bssCtx, &pInstance->authCtx, &StaEventCallback)) {
                        SendEvent(WIFI_MANAGER_EVENT_ERROR);
                        LOG_E("Error connecting to network\r\n");
                        break;
                    }
                    
                    SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_STARTED);
                    LOG_D("STA mode connection initiated\r\n");
                }
            }
            else
            {
                // If not currently connected, transition to MainState for fresh init
                returnStatus = WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_TRAN;
                pInstance->nextState = MainState;
            }

            break;
        case WIFI_MANAGER_EVENT_DEINIT:
            if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_OTA_MODE_READY)) {
                //If ota is running and again initialized, then deinit OTA
                ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_OTA_MODE_READY);
                pInstance->pWifiSettings->isOtaModeEnabled = 0;
                wifi_serial_bridge_interface_DeInit();
            }
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_BUSY) {
                SendEvent(WIFI_MANAGER_EVENT_DEINIT);
                break;
            }
            returnStatus = WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_TRAN;
            pInstance->nextState = MainState;            
            if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_OPEN))
                CloseUdpSocket(&pInstance->udpServerSocket);
            if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_TCP_SOCKET_OPEN))
                wifi_tcp_server_CloseSocket();
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) != SYS_STATUS_UNINITIALIZED) {
                WDRV_WINC_Close(pInstance->wdrvHandle);
                pInstance->wdrvHandle = DRV_HANDLE_INVALID;
                WDRV_WINC_Deinitialize(sysObj.drvWifiWinc);
                sysObj.drvWifiWinc = SYS_MODULE_OBJ_INVALID;  // Reset system object for clean reinit
                
                // Fix the WINC reset state after deinit
                wifi_manager_FixWincResetState();
                
                ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_INITIALIZED);
                LOG_D("WiFi de-initialized and taken out of reset\r\n");
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
            {
                static bool errorLogged = false;
                static int consecutiveErrors = 0;
                
                consecutiveErrors++;
                
                // Only log error on first occurrence or every 10th error
                if (!errorLogged || (consecutiveErrors % 10 == 0)) {
                    LOG_E("[%s:%d]WiFi error occurred (count: %d)\r\n", __FILE__, __LINE__, consecutiveErrors);
                    errorLogged = true;
                }
                
                // Implement power-efficient reconnect logic for STA mode
                if (pInstance->pWifiSettings->networkMode == WIFI_MANAGER_NETWORK_MODE_STA &&
                    GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_STARTED)) {
                    
                    uint32_t delayMs;
                    if (pInstance->staReconnectAttempts < 5) {
                        // First 5 attempts: 1 second delay
                        delayMs = 1000;
                        if (pInstance->staReconnectAttempts == 0) {  // Only log first attempt
                            LOG_D("STA reconnect attempt %d/5 with 1s delay\r\n", pInstance->staReconnectAttempts + 1);
                        }
                    } else {
                        // After 5 attempts: 30 second delay to save power
                        delayMs = 30000;
                        if (pInstance->staReconnectAttempts == 5) {  // Only log when switching to power save
                            LOG_D("Switching to power save mode - reconnect attempts with 30s delay\r\n");
                        }
                    }
                    
                    // Wait before attempting reconnection
                    vTaskDelay(pdMS_TO_TICKS(delayMs));
                    
                    SendEvent(WIFI_MANAGER_EVENT_REINIT);
                } else {
                    // For AP mode or other errors, reinit immediately
                    SendEvent(WIFI_MANAGER_EVENT_REINIT);
                }
            }
            break;
        default:
            break;
    }
    return returnStatus;
}

void fwUpdateTask(void *pvParameters) {
    while (1) {
        if (GetEventFlagStatus(gStateMachineContext.eventFlags, WIFI_MANAGER_STATE_FLAG_OTA_MODE_READY))
            wifi_serial_bridge_Process(&gStateMachineContext.serialBridgeContext);
        else {
            vTaskSuspend(NULL);
        }
    }
}

bool wifi_manager_Init(wifi_manager_settings_t * pSettings) {

    if (gEventQH == NULL)
        gEventQH = xQueueCreate(20, sizeof (wifi_manager_event_t));
    else return true;
    if (pSettings != NULL)
        gStateMachineContext.pWifiSettings = pSettings;
    if (gStateMachineContext.fwUpdateTaskHandle == NULL) {
        xTaskCreate(fwUpdateTask, "fwUpdateTask", 1024, NULL, 2, &gStateMachineContext.fwUpdateTaskHandle);
    }
    gStateMachineContext.pWifiSettings->isOtaModeEnabled = false;
    gStateMachineContext.active = MainState;
    gStateMachineContext.active(&gStateMachineContext, WIFI_MANAGER_EVENT_ENTRY);
    gStateMachineContext.nextState = NULL;

    return true;
}

bool wifi_manager_GetChipInfo(wifi_manager_chipInfo_t *pChipInfo) {
    if (!GetEventFlagStatus(gStateMachineContext.eventFlags, WIFI_MANAGER_STATE_FLAG_INITIALIZED)) {
        return false;
    }
    if (pChipInfo == NULL) {
        return false;
    }
    memset(pChipInfo, 0, sizeof (wifi_manager_chipInfo_t));
    pChipInfo->chipID = gStateMachineContext.wifiFirmwareVersion.u32Chipid;
    snprintf(pChipInfo->frimwareVersion, WIFI_MANAGER_CHIP_INFO_FW_VERSION_MAX_SIZE, "%d.%d.%d", gStateMachineContext.wifiFirmwareVersion.u8FirmwareMajor,
            gStateMachineContext.wifiFirmwareVersion.u8FirmwareMinor,
            gStateMachineContext.wifiFirmwareVersion.u8FirmwarePatch);
    strncpy(pChipInfo->BuildDate, (char*) gStateMachineContext.wifiFirmwareVersion.BuildDate, sizeof (__DATE__));
    strncpy(pChipInfo->BuildTime, (char*) gStateMachineContext.wifiFirmwareVersion.BuildTime, sizeof (__DATE__));
    return true;
}

wifi_status_t wifi_manager_GetWiFiStatus(void) {
    // First check if WiFi is enabled in settings
    if (gStateMachineContext.pWifiSettings == NULL || 
        !gStateMachineContext.pWifiSettings->isEnabled) {
        return WIFI_STATUS_DISABLED;
    }
    
    // Check the actual WiFi driver state
    uint8_t wifiState = m2m_wifi_get_state();
    
    switch(wifiState) {
        case WIFI_STATE_START:
            // WiFi is active, check connection status
            
            // For STA mode: connected means connected to a router
            if (GetEventFlagStatus(gStateMachineContext.eventFlags, WIFI_MANAGER_STATE_FLAG_STA_CONNECTED)) {
                return WIFI_STATUS_CONNECTED;
            }
            
            // For AP mode: check if any clients are connected
            if (GetEventFlagStatus(gStateMachineContext.eventFlags, WIFI_MANAGER_STATE_FLAG_AP_STARTED)) {
                // Check if we have an active TCP client connection
                if (gStateMachineContext.pTcpServerContext && 
                    gStateMachineContext.pTcpServerContext->client.clientSocket >= 0) {
                    return WIFI_STATUS_CONNECTED;  // Client connected to our AP
                }
                // AP is running but no clients connected
                return WIFI_STATUS_DISCONNECTED;
            }
            
            return WIFI_STATUS_DISCONNECTED;
            
        case WIFI_STATE_INIT:
            // WiFi is initializing
            return WIFI_STATUS_DISCONNECTED;
            
        case WIFI_STATE_DEINIT:
        default:
            // WiFi is not initialized
            return WIFI_STATUS_DISABLED;
    }
}

bool wifi_manager_IsWiFiConnected(void) {
    return (wifi_manager_GetWiFiStatus() == WIFI_STATUS_CONNECTED);
}

bool wifi_manager_Deinit() {
    gStateMachineContext.pWifiSettings->isEnabled = 0;
    SendEvent(WIFI_MANAGER_EVENT_DEINIT);
    return true;
}

bool wifi_manager_UpdateNetworkSettings(wifi_manager_settings_t * pSettings) {

    // Always allow settings to be updated regardless of power state
    // This allows configuration while the device is off or WiFi is disabled
    
    if (pSettings != NULL && gStateMachineContext.pWifiSettings != NULL) {
        
        // Preserve the MAC address before copying new settings
        WDRV_WINC_MAC_ADDR savedMacAddr;
        memcpy(&savedMacAddr, &gStateMachineContext.pWifiSettings->macAddr, sizeof(WDRV_WINC_MAC_ADDR));
        
        // Copy new settings
        memcpy(gStateMachineContext.pWifiSettings, pSettings, sizeof (wifi_manager_settings_t));
        
        // Restore the MAC address
        memcpy(&gStateMachineContext.pWifiSettings->macAddr, &savedMacAddr, sizeof(WDRV_WINC_MAC_ADDR));
        
        // Also update BoardData so the app task sees the changes
        BoardData_Set(BOARDDATA_WIFI_SETTINGS, 0, gStateMachineContext.pWifiSettings);
        // LOG_D("BoardData updated with new WiFi settings\r\n");
        
        // Only trigger a reinit if WiFi is enabled and powered
        const tPowerData *pPowerState = (tPowerData *) BoardData_Get(BOARDDATA_POWER_DATA, 0);
        bool isPowered = (pPowerState != NULL && 
                         (pPowerState->powerState == POWERED_UP || 
                          pPowerState->powerState == POWERED_UP_EXT_DOWN));
        
        if (pSettings->isEnabled && isPowered) {
            // WiFi is enabled and board is powered, apply changes immediately
            SendEvent(WIFI_MANAGER_EVENT_REINIT);
        } else {
            // Settings saved but not applied - they'll be applied when WiFi is enabled
            // and the board is powered
            // No SCPI error here - settings are successfully saved
        }
    }
    return true;
}

size_t wifi_manager_GetWriteBuffFreeSize() {
    return wifi_tcp_server_GetWriteBuffFreeSize();
}

size_t wifi_manager_WriteToBuffer(const char* pData, size_t len) {
    return wifi_tcp_server_WriteBuffer(pData, len);
}

void wifi_manager_ProcessState() {
    wifi_manager_event_t event;
    wifi_manager_stateMachineReturnStatus_t ret;
    while (gEventQH == NULL) {
        return;
    }
    wifi_tcp_server_TransmitBufferedData();
    
    if (xQueueReceive(gEventQH, &event, 1) != pdPASS) {
        return;
    }
    ret = gStateMachineContext.active(&gStateMachineContext, event);
    if (ret == WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_TRAN) {
        gStateMachineContext.active(&gStateMachineContext, WIFI_MANAGER_EVENT_EXIT);
        if (gStateMachineContext.nextState != NULL) {
            gStateMachineContext.active = gStateMachineContext.nextState;
            gStateMachineContext.active(&gStateMachineContext, WIFI_MANAGER_EVENT_ENTRY);
        } else {
            LOG_E("%s : %d : State Change Requested, but NextSate is NULL", __func__, __LINE__);
        }
    }
}

wifi_tcp_server_context_t* wifi_manager_GetTcpServerContext() {
    return gStateMachineContext.pTcpServerContext;
}

bool wifi_manager_GetRSSI(uint8_t *pRssi, uint32_t timeoutMs) {
    // Check if we're connected as STA
    if (!GetEventFlagStatus(gStateMachineContext.eventFlags, WIFI_MANAGER_STATE_FLAG_STA_STARTED) ||
        gStateMachineContext.assocHandle == WDRV_WINC_ASSOC_HANDLE_INVALID) {
        // Not connected, return 0
        if (pRssi != NULL) {
            *pRssi = 0;
        }
        return false;
    }
    
    // Reset flags with critical section to prevent race conditions
    taskENTER_CRITICAL();
    gRssiUpdatePending = true;
    gRssiUpdateComplete = false;
    taskEXIT_CRITICAL();
    
    // Request RSSI from WiFi module
    int8_t rssi;
    WDRV_WINC_STATUS status = WDRV_WINC_AssocRSSIGet(gStateMachineContext.assocHandle, &rssi, RssiEventCallback);
    
    if (status == WDRV_WINC_STATUS_OK) {
        // RSSI was cached, callback was already called
        taskENTER_CRITICAL();
        if (pRssi != NULL) {
            *pRssi = gLastRssiPercentage;
        }
        gRssiUpdatePending = false;
        gRssiUpdateComplete = false;  // Clear for next call
        taskEXIT_CRITICAL();
        return true;
    } else if (status == WDRV_WINC_STATUS_RETRY_REQUEST) {
        // Need to wait for callback
        uint32_t startTime = SYS_TIME_CounterGet();
        uint32_t elapsed = 0;
        
        // Wait for callback or timeout
        bool updateComplete = false;
        while (!updateComplete && elapsed < timeoutMs) {
            vTaskDelay(pdMS_TO_TICKS(10)); // Check every 10ms
            
            taskENTER_CRITICAL();
            updateComplete = gRssiUpdateComplete;
            taskEXIT_CRITICAL();
            
            elapsed = SYS_TIME_CountToMS(SYS_TIME_CounterGet() - startTime);
        }
        
        taskENTER_CRITICAL();
        if (updateComplete) {
            if (pRssi != NULL) {
                *pRssi = gLastRssiPercentage;
            }
            // Clear flags for next call
            gRssiUpdatePending = false;
            gRssiUpdateComplete = false;
            taskEXIT_CRITICAL();
            return true;
        } else {
            // Timeout - return last known value if available
            gRssiUpdatePending = false;
            gRssiUpdateComplete = false;
            if (pRssi != NULL && gStateMachineContext.pWifiSettings != NULL) {
                *pRssi = gStateMachineContext.pWifiSettings->rssi_percent;
            }
            taskEXIT_CRITICAL();
            return false;
        }
    } else {
        // Error getting RSSI
        taskENTER_CRITICAL();
        gRssiUpdatePending = false;
        gRssiUpdateComplete = false;  // Clear for next call
        if (pRssi != NULL && gStateMachineContext.pWifiSettings != NULL) {
            *pRssi = gStateMachineContext.pWifiSettings->rssi_percent; // Return last known value
        }
        taskEXIT_CRITICAL();
        return false;
    }
}