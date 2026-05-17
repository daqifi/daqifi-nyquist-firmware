#define LOG_LVL LOG_LEVEL_WIFI
#define LOG_MODULE LOG_MODULE_WIFI
#include "wifi_manager.h"
#include "wdrv_winc_client_api.h"
#include "Util/Logger.h"
#include "wifi_tcp_server.h"
#include "state/data/BoardData.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "wifi_serial_bridge.h"
#include "wifi_serial_bridge_interface.h"
#include "iperf2/iperf2.h"
#include "driver/winc/include/drv/common/nm_common.h"  // nm_reset (canonical WINC reset pulse)
#include "semphr.h"
#include "driver/winc/include/drv/driver/m2m_wifi.h"

#define UNUSED(x) (void)(x)
/* WIFI_MANAGER_UDP_LISTEN_PORT moved to wifi_manager.h so the
 * capability rollup can reference it without duplication. */

// Standard IPv4 address string length (xxx.xxx.xxx.xxx + null = 16)
#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif

typedef enum {
    WIFI_MANAGER_EVENT_ENTRY,
    WIFI_MANAGER_EVENT_EXIT,
    WIFI_MANAGER_EVENT_INIT,
    WIFI_MANAGER_EVENT_WIFI_FW_UPDATE_INIT,
    WIFI_MANAGER_EVENT_WIFI_FW_UPDATE_READY,
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
        WIFI_MANAGER_STATE_FLAG_WIFI_FW_UPDATE_REQUESTED = 1 << 8,  // WiFi firmware update requested but not yet initialized
        WIFI_MANAGER_STATE_FLAG_WIFI_FW_UPDATE_READY = 1 << 9,      // WiFi firmware update initialized and active
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
    TickType_t retryAfterTick;     // Earliest tick the next REINIT attempt is allowed (#416 round 2)
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

// #353 Option 2: decouple SCPI dispatch from WINC callback context.
// SOCKET_MSG_RECV fires on WDRV_WINC_Tasks (the WINC driver's task). Prior
// to this change, the handler ran the whole microrl + libscpi + SCPI handler
// chain inline on that task's stack — which caused #347 v2 (stack overflow
// on SCPI handlers with large locals). PR #354 fixed the specific overflow;
// this decouples the dispatch path entirely so the next SCPI handler with a
// fat stack frame can't cause a similar crash.
//
// Flow: SOCKET_MSG_RECV stores size + sets flag + returns (no ProcessRx,
// no recv re-arm). WifiTask's wifi_manager_ProcessState() picks up the flag,
// runs wifi_tcp_server_ProcessReceivedBuff on WifiTask's stack, then re-arms
// recv. WINC buffers inbound TCP while we process (per WINC docs).
static volatile bool gTcpRxPending = false;

// Deadline (in ticks) at which wifi_manager_ProcessState should queue
// the deferred WIFI_MANAGER_EVENT_INIT after a HardReset.  0 = none
// pending.  32-bit reads/writes are atomic on PIC32MZ (see CLAUDE.md
// concurrency rules).  Marked volatile because it's set from any task
// calling HardReset/Deinit and read every tick from app_WifiTask.
static volatile TickType_t gWifiReinitDeadlineTick = 0;

// Recursive mutex serializing wifi_manager_ProcessState across callers.
// Today app_WifiTask is the primary owner; SCPI_Reset's active pump
// (SYSTem:REboot path) also enters from the SCPI dispatch task.  Without
// the mutex, the body's xQueueReceive + state-machine update on shared
// gStateMachineContext could corrupt under any future priority change
// that lets the two callers interleave.
//
// Recursive: TCP-SCPI dispatch nests ProcessState → wifi_tcp_server callback
// → SCPI_Reset → active pump → ProcessState.  Same task takes the mutex
// twice on that path; recursive avoids self-deadlock.  Requires
// configUSE_RECURSIVE_MUTEXES=1 in FreeRTOSConfig.h.  Created in
// wifi_manager_Init.
static StaticSemaphore_t gProcessStateMutexBuf;
static SemaphoreHandle_t gProcessStateMutex = NULL;
static volatile bool gRssiUpdateComplete = false;
static volatile uint8_t gLastRssiPercentage = 0;

// #382 sub-bug 1 — STA_CONNECTED reconciliation.
// The flag is event-driven (set by StaEventCallback on CONN_STATE_CHANGED).
// If the chip silently re-associates (AP rekey, brief deauth handled
// internally) without firing a CONN_STATE_CHANGED up to our callback,
// the flag stays cleared while the TCP stack is healthy — user sees
// "Disconnected" / SSIDStr=0 even though pings + iperf2 work fine.
// Every ~5 s, when the flag looks drifted (STA_STARTED set, valid
// assocHandle, but STA_CONNECTED cleared), fire an async RSSI probe.
// If the chip responds, the RssiEventCallback signals via
// gStaReconcilePending and the next ProcessStateImpl pump applies
// SetEventFlag(STA_CONNECTED) from task context — same writer as
// every other eventFlags mutation (Qodo data-race finding).
// No work happens when the flag is in sync, so the overhead is zero
// in the healthy case.
#define WIFI_STA_RECONCILE_PERIOD_TICKS  pdMS_TO_TICKS(5000)
static volatile TickType_t gStaReconcileLastTick = 0;
static volatile bool gStaReconcilePending = false;
// Captured at the same moment as gStaReconcilePending in RssiEventCallback.
// Phase 1 of MaybeReconcileStaConnected validates that the live
// gStateMachineContext.assocHandle still equals this snapshot before
// setting STA_CONNECTED — guards against the disconnect race where
// StaEventCallback clears assocHandle between the RSSI callback and
// the next ProcessState pump.  Without this check, a stale repair
// could promote a now-disconnected chip back to CONNECTED state
// (Qodo #451 pass 3 finding, observed on bench 2026-05-13 as
// "ADDR=192.168.1.160 RSSI=92 but device unreachable from network").
static volatile WDRV_WINC_ASSOC_HANDLE gStaReconcilePendingHandle = WDRV_WINC_ASSOC_HANDLE_INVALID;

// Apply-in-progress gate (#425).
// Set true under taskENTER_CRITICAL by wifi_manager_UpdateNetworkSettings
// when a REINIT is about to be queued; cleared when the state machine
// reaches a steady state (STA_CONNECTED, AP_STARTED, or FW_UPDATE_READY)
// or the safety deadline elapses.
//
// Test-and-set is atomic so concurrent SCPI callers (USB pri 7 + WifiTask
// TCP-SCPI pri 2) can't both pass the gate and stack REINIT events faster
// than the state machine can drain them.  Without this gate, the second-
// and-later REINIT enters INIT processing while the WINC chip is still
// tearing down the prior attempt, WDRV_WINC_IPUseDHCPSet returns
// WDRV_WINC_STATUS_REQUEST_ERROR (8), and the manager spins on
// WIFI_MANAGER_EVENT_ERROR until power-cycle.
//
// volatile because cleared from WifiTask (pri 2 — steady-state events,
// safety deadline) but set from any task that calls UpdateNetworkSettings
// (USB SCPI pri 7, TCP SCPI on WifiTask itself).
static volatile bool gApplyInProgress = false;
// Safety deadline: clear gApplyInProgress unconditionally if the state
// machine hasn't reached steady state within ~30 s (e.g. unreachable AP,
// stuck WINC).  Without this, a stuck transition could permanently lock
// out further APPLYs.  0 = no deadline armed.  Read from
// wifi_manager_ProcessState every tick when gApplyInProgress is true.
// Computed deadline is bumped from 0 to 1 to keep the sentinel reliable
// across tick wraparound.
#define WIFI_APPLY_GATE_TIMEOUT_MS 30000u
static volatile TickType_t gApplyInProgressDeadlineTick = 0;

// #423 round-3: bounded window opened immediately before each APPLY-
// driven BSSDisconnect / HardReset call site.  The async WINC disconnect
// callback that ensues should be demoted (deliberate teardown, not a
// real failure) — but ONLY for the ~callback-arrival window.  Past the
// window, fresh BSSConnect failures (wrong password, scan miss, etc.)
// still surface as LOG_E.  This outlives gApplyInProgress and
// STA_CONNECTED across the HardReset cycle (which clears both), which
// the round-2 (gApplyInProgress && STA_CONNECTED) check could not.
//
// Round-4: store the *start* tick and compare elapsed `(now - start)`
// against the window.  Unsigned subtraction handles FreeRTOS tick wrap
// (TickType_t rolls over after ~50 days at 1 kHz), which `now < deadline`
// would silently mis-classify.  Sentinel 0 = disarmed; bumped to 1 if
// the captured start lands on 0.
#define WIFI_APPLY_TEARDOWN_WINDOW_MS 2000u
static volatile TickType_t gApplyTeardownStartTick = 0;

// Arm the teardown-demotion window iff an APPLY is in flight; called
// immediately before any BSSDisconnect / HardReset on an APPLY path.
static inline void ArmApplyTeardownDeadline(void) {
    if (gApplyInProgress) {
        TickType_t start = xTaskGetTickCount();
        if (start == 0) {
            start = 1;  // keep 0 reserved as the disarmed sentinel
        }
        gApplyTeardownStartTick = start;
    }
}

// True iff we're still inside the teardown-demotion window.  Wrap-safe
// via unsigned subtraction.  On window-expiry detection, also clears the
// start tick to avoid a long-uptime + no-APPLY false-positive: if the
// device never APPLYs for ~50 days (unlikely but reachable), the tick
// counter wraps back near the stored start value and `elapsed` would
// collapse to near-zero — putting us spuriously back "inside the
// window."  Self-disarming closes that hole.
static inline bool IsWithinApplyTeardownWindow(void) {
    TickType_t start = gApplyTeardownStartTick;
    if (start == 0) {
        return false;
    }
    TickType_t elapsed = xTaskGetTickCount() - start;
    if (elapsed >= pdMS_TO_TICKS(WIFI_APPLY_TEARDOWN_WINDOW_MS)) {
        gApplyTeardownStartTick = 0;  // disarm; prevents post-wrap false positive
        return false;
    }
    return true;
}

//===========================================================
//=====================Private Callbacks=====================

static void RssiEventCallback(DRV_HANDLE handle, WDRV_WINC_ASSOC_HANDLE assocHandle, int8_t rssi) {
    // In ISR/callback context - keep this minimal
    // Validate association handle is valid and matches our current association
    taskENTER_CRITICAL();
    if (assocHandle == WDRV_WINC_ASSOC_HANDLE_INVALID ||
        assocHandle != gStateMachineContext.assocHandle) {
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
    // #382 sub-bug 1: chip confirmed it's associated to the BSS we know
    // about (assocHandle matched above) — signal the next ProcessState
    // pump to repair STA_CONNECTED if it drifted.  Don't touch
    // eventFlags here: this callback runs from WDRV_WINC_Tasks
    // (priority 2, same as app_WifiTask, time-sliced), and existing
    // SetEventFlag/ResetEventFlag writers in task context are
    // unprotected RMWs.  A direct |= here would race with those and
    // could drop unrelated bits (Qodo #451 review).
    //
    // Capture the assocHandle alongside the pending flag so Phase 1
    // can detect a disconnect race: if StaEventCallback clears
    // gStateMachineContext.assocHandle between this point and the
    // next ProcessState pump, the snapshot vs live compare in Phase 1
    // will mismatch and the repair will be skipped (Qodo #451 pass 3
    // "Stale reconcile sets connected" finding).
    gStaReconcilePendingHandle = assocHandle;
    gStaReconcilePending = true;
    taskEXIT_CRITICAL();
    // No logging in ISR/callback context - keep it minimal
}

static void DhcpEventCallback(DRV_HANDLE handle, uint32_t ipAddress) {
    char s[INET_ADDRSTRLEN] = {0};
    UNUSED(s);

    // Validate we have an active association before processing DHCP
    if (gStateMachineContext.assocHandle == WDRV_WINC_ASSOC_HANDLE_INVALID) {
        return;
    }

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
    // Validate this callback is for a valid association and we're in AP mode
    if (assocHandle == WDRV_WINC_ASSOC_HANDLE_INVALID) {
        return;
    }
    if (!GetEventFlagStatus(gStateMachineContext.eventFlags, WIFI_MANAGER_STATE_FLAG_AP_STARTED)) {
        return;  // Ignore callbacks when not in AP mode (e.g., during mode switching)
    }

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
        // Validate we're in STA mode before accepting connection
        if (!GetEventFlagStatus(gStateMachineContext.eventFlags, WIFI_MANAGER_STATE_FLAG_STA_STARTED)) {
            return;  // Ignore callbacks when not in STA mode (e.g., during mode switching)
        }
        LOG_D("STA mode: Station connected\r\n");
        gStateMachineContext.assocHandle = assocHandle;  // Store association handle for RSSI queries
        SendEvent(WIFI_MANAGER_EVENT_STA_CONNECTED);
    } else if (WDRV_WINC_CONN_STATE_DISCONNECTED == currentState && errorCode != WDRV_WINC_CONN_ERROR_INPROGRESS) {
        // Only process disconnect for our current connection or if already invalid
        if (assocHandle != gStateMachineContext.assocHandle &&
            gStateMachineContext.assocHandle != WDRV_WINC_ASSOC_HANDLE_INVALID) {
            return;  // Stale disconnect callback from previous connection
        }
        const char *reason =
              (errorCode == WDRV_WINC_CONN_ERROR_AUTH) ? "Auth Failed" :
              (errorCode == WDRV_WINC_CONN_ERROR_ASSOC) ? "Association Failed" :
              (errorCode == WDRV_WINC_CONN_ERROR_SCAN) ? "Scan Failed" :
              (errorCode == WDRV_WINC_CONN_ERROR_NOCRED) ? "No Credentials" :
              "Unknown";
        // #423: demote LOG_E only when the disconnect callback arrives
        // within the short window after an APPLY-driven teardown was
        // initiated (ArmApplyTeardownDeadline, called at each call site).
        // This decouples the demotion decision from gApplyInProgress /
        // STA_CONNECTED, both of which are cleared synchronously by the
        // REINIT and HardReset paths before the async callback fires
        // (Qodo round-2 finding #1).  Past the window, fresh BSSConnect
        // failures on new settings still surface as LOG_E.
        bool isApplyTeardown = IsWithinApplyTeardownWindow();
        if (isApplyTeardown) {
            LOG_I("WiFi STA Disconnected during APPLY teardown - errorCode=%d (%s)\r\n",
                  errorCode, reason);
        } else {
            LOG_E("WiFi STA Disconnected - Error Code: %d, Time: %u ticks, Reason: %s\r\n",
                  errorCode, (unsigned int)xTaskGetTickCount(), reason);
        }
        gStateMachineContext.assocHandle = WDRV_WINC_ASSOC_HANDLE_INVALID;  // Clear association handle
        SendEvent(WIFI_MANAGER_EVENT_STA_DISCONNECTED);
    }
}

#define UDP_BUFFER_SIZE 1460

static void SocketEventCallback(SOCKET socket, uint8_t messageType, void *pMessage) {
    static uint8_t udpBuffer[UDP_BUFFER_SIZE];

    // #377: iperf2 first claim — when an iperf benchmark is running it owns
    // its own listen + data sockets and dispatches the full state machine
    // here.  Skip the wifi_tcp_server / UDP-discovery cases below if iperf2
    // handles the event.
    if (Iperf2_HandleSocketEvent(socket, messageType, pMessage)) {
        return;
    }

    switch (messageType) {
        case SOCKET_MSG_BIND:
        {
            tstrSocketBindMsg *pBindMessage = (tstrSocketBindMsg*) pMessage;
            if ((NULL != pBindMessage) && (0 == pBindMessage->status)) {
                if (socket == gStateMachineContext.udpServerSocket) {
                    LOG_D("UDP socket bind successful (socket=%d), starting recvfrom on port %d\r\n", socket, WIFI_MANAGER_UDP_LISTEN_PORT);
                    recvfrom(socket, udpBuffer, UDP_BUFFER_SIZE, 0);
                    SendEvent(WIFI_MANAGER_EVENT_UDP_SOCKET_CONNECTED);
                } else if (gStateMachineContext.pTcpServerContext != NULL &&
                           socket == gStateMachineContext.pTcpServerContext->serverSocket) {
                    listen(gStateMachineContext.pTcpServerContext->serverSocket, 0);
                }
            } else {
                LOG_E("Socket bind failed (socket=%d, status=%d)", socket,
                      (pBindMessage != NULL) ? pBindMessage->status : -1);
                // Close failed socket to clean up resources
                if (socket >= 0) {
                    shutdown(socket);
                }
                if (socket == gStateMachineContext.udpServerSocket) {
                    gStateMachineContext.udpServerSocket = -1;
                } else if (gStateMachineContext.pTcpServerContext != NULL &&
                           socket == gStateMachineContext.pTcpServerContext->serverSocket) {
                    gStateMachineContext.pTcpServerContext->serverSocket = -1;
                }
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
                (void)s;  // May be unused when LOG_D is disabled
                // One client per port.  If a client is already
                // connected, refuse the new connection by shutting
                // down the just-accept'd socket — NOT the active
                // client's socket.  The existing client keeps
                // streaming intact.
                //
                // Why not close the active client like we used to:
                // that path called shutdown() on the streaming socket
                // from this WDRV_WINC_Tasks callback context while the
                // streaming task was mid-send() on the same socket.
                // The synchronous WINC HIF re-entrancy corrupted the
                // chip state and wedged all TCP I/O until SYST:COMM:
                // LAN:HRESet (or power-cycle in worse cases).  See
                // #452.  The just-accept'd socket has no in-flight
                // state on our side — no buffer, no send queue, no
                // streaming task references — so a sync shutdown of
                // it is safe (the WINC sends a RST to the new peer,
                // no corruption of the active session).
                if (gStateMachineContext.pTcpServerContext->client.clientSocket >= 0) {
                    LOG_I("TCP: refusing 2nd client on listen socket (one-client policy, #452)");
                    int8_t rc = shutdown(pAcceptMessage->sock);
                    if (rc != SOCK_ERR_NO_ERROR) {
                        // WINC shutdown() clears local socket state even on
                        // HIF send failure (winc/drv/socket/socket.c:1012),
                        // so we cannot retry — the WINC side may hold the
                        // refused-client socket resource until next reset.
                        LOG_E("TCP: shutdown(refused 2nd client sock=%d) failed rc=%d", pAcceptMessage->sock, rc);
                    }
                    break;
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
                // #353 Option 2: defer ProcessReceivedBuff + recv re-arm to
                // WifiTask context. See gTcpRxPending comment above. The
                // WINC driver buffers subsequent TCP data internally until
                // we call recv() again, so missing this tick's re-arm is
                // safe.
                gStateMachineContext.pTcpServerContext->client.readBufferLength = pRecvMessage->s16BufferSize;
                gTcpRxPending = true;

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
                (void)s; (void)u16port;  // May be unused when LOG_D is disabled
                LOG_D("\r\nUDP Discovery: Received frame with size=%d\r\nHost address=%s\r\nPort number = %d\r\n", pstrRx->s16BufferSize, s, u16port);
                LOG_D("UDP Discovery: Frame Data : %.*s\r\n", pstrRx->s16BufferSize, (char*) pstrRx->pu8Buffer);
                uint16_t announcePacktLen = UDP_BUFFER_SIZE;
                wifi_manager_FormUdpAnnouncePacketCB(gStateMachineContext.pWifiSettings, udpBuffer, &announcePacktLen);
                struct sockaddr_in addr;
                addr.sin_family = AF_INET;
                // Always respond to sender's source port (standard UDP behavior)
                addr.sin_port = pstrRx->strRemoteAddr.sin_port;
                addr.sin_addr.s_addr = pstrRx->strRemoteAddr.sin_addr.s_addr;
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
            if (gStateMachineContext.pTcpServerContext != NULL && pMessage != NULL) {
                int16_t sentBytes = *(int16_t*)pMessage;
                wifi_tcp_server_clientContext_t* client = &gStateMachineContext.pTcpServerContext->client;

                // #362 Step C: consume one ring slot per callback so partial-send
                // accounting matches the actual send size, not the most-recently-
                // queued one.  Decrement tcpInFlight + advance ring tail in the
                // SAME critical section so a re-armed send (TransmitBufferedData
                // below) writes to a fresh head slot without aliasing the slot
                // we just snapshotted.
                uint16_t sendSize = 0;
                uint32_t errorCount, partialCount;
                bool isPartial = false;
                bool isError = false;
                bool hasInflight;

                taskENTER_CRITICAL();
                hasInflight = (client->tcpInFlight > 0);
                if (hasInflight) {
                    client->tcpInFlight--;
                    sendSize = client->inflightSizes[client->inflightTail];
                    client->inflightTail =
                        (client->inflightTail + 1) % WIFI_TCP_MAX_IN_FLIGHT;
                }
                taskEXIT_CRITICAL();

                // Re-arm AFTER snapshotting sendSize from the ring tail —
                // otherwise the new send writes to head, which can alias
                // tail when the ring is full at cap.
                wifi_tcp_server_TransmitBufferedData();

                taskENTER_CRITICAL();
                if (sentBytes >= 0) {
                    client->wifiTcpBytesConfirmed += (uint16_t)sentBytes;
                    if (sendSize > 0 && (uint16_t)sentBytes < sendSize) {
                        client->wifiTcpPartialSends++;
                        // #367 diag: track cumulative shortfall to test the
                        // partial-send-loss hypothesis as the gap source.
                        client->wifiPartialBytesMissing +=
                            (uint32_t)(sendSize - (uint16_t)sentBytes);
                        isPartial = true;
                    }
                } else {
                    client->wifiTcpSendErrors++;
                    isError = true;
                }
                errorCount = client->wifiTcpSendErrors;
                partialCount = client->wifiTcpPartialSends;
                taskEXIT_CRITICAL();

                // Log outside critical section using snapshotted counts
                if (isPartial && partialCount == 1) {
                    LOG_I("WiFi TCP partial send: %d/%u bytes (normal segmentation)", (int)sentBytes, (unsigned)sendSize);
                } else if (isError && errorCount == 1) {
                    LOG_E("WiFi TCP send error: %d", (int)sentBytes);
                }
            }
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
    // Run Microchip's canonical WINC1500 power-cycle pulse (nm_reset, in
    // drv/common/nm_common.c).  Sequence + timing:
    //
    //   CHIP_EN low + RESET_N low   (held in full reset)
    //   sleep 100 ms
    //   CHIP_EN high                (power on)
    //   sleep 10 ms                 (rail settle)
    //   RESET_N high                (release from reset; chip starts boot)
    //   sleep 10 ms                 (boot before next SPI access)
    //
    // Earlier this function only did a half-reset — Assert(CHIP_EN) +
    // Deassert(RESET_N) — which left the chip "powered but undefined"
    // between Deinit and the next Init.  Across multiple POW:STAT or
    // HRESet cycles, the chip would accumulate stuck state that no
    // amount of partial GPIO toggling could recover (#400 multi-cycle
    // wedge).  The full nm_reset pulse is what Microchip's own driver
    // (nmbus.c:66, nm_common.c:56) uses internally and what their
    // wdrv_winc.c Deinit ends with — aligning ours fixes the
    // unrecoverable-soft-wedge class.
    nm_reset();
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
        (void)bindResult;  // May be unused when LOG_D is disabled
        LOG_D("UDP socket bind result: %d\r\n", bindResult);
        returnStatus = true;
    } else {
        LOG_E("Failed to create UDP socket\r\n");
    }
    return returnStatus;
}

/**
 * Appends the last 4 hex digits of the MAC address to the default SSID and
 * hostname ("DAQiFi" → "DAQiFi-95A7") so multiple devices are distinguishable.
 * Also syncs the result to the runtime config and sets the WINC device name.
 * The strcmp guard prevents double-appending on repeated calls.
 */
static void ApplyDefaultSuffixAndDeviceName(stateMachineInst_t *pInstance) {
    char macSuffix[6];
    uint8_t *mac = pInstance->pWifiSettings->macAddr.addr;
    snprintf(macSuffix, sizeof(macSuffix), "-%02X%02X", mac[4], mac[5]);

    if (strcmp(pInstance->pWifiSettings->ssid, "DAQiFi") == 0) {
        strncat(pInstance->pWifiSettings->ssid, macSuffix,
                WDRV_WINC_MAX_SSID_LEN - strlen(pInstance->pWifiSettings->ssid));
    }
    if (strcmp(pInstance->pWifiSettings->hostName, "DAQiFi") == 0) {
        strncat(pInstance->pWifiSettings->hostName, macSuffix,
                WIFI_MANAGER_DNS_CLIENT_MAX_HOSTNAME_LEN - strlen(pInstance->pWifiSettings->hostName));
    }

    // Sync runtime config copy so SCPI queries reflect the suffixed values
    wifi_manager_settings_t *pRtWifi = BoardRunTimeConfig_Get(
            BOARDRUNTIME_WIFI_SETTINGS);
    if (pRtWifi != NULL) {
        taskENTER_CRITICAL();
        strncpy(pRtWifi->ssid, pInstance->pWifiSettings->ssid,
                WDRV_WINC_MAX_SSID_LEN);
        pRtWifi->ssid[WDRV_WINC_MAX_SSID_LEN] = '\0';
        strncpy(pRtWifi->hostName, pInstance->pWifiSettings->hostName,
                WIFI_MANAGER_DNS_CLIENT_MAX_HOSTNAME_LEN);
        pRtWifi->hostName[WIFI_MANAGER_DNS_CLIENT_MAX_HOSTNAME_LEN] = '\0';
        taskEXIT_CRITICAL();
    }

    // Set DHCP hostname (truncated to WINC limit)
    if (pInstance->wdrvHandle != DRV_HANDLE_INVALID) {
        char devName[M2M_DEVICE_NAME_MAX];
        strncpy(devName, pInstance->pWifiSettings->hostName, sizeof(devName) - 1);
        devName[sizeof(devName) - 1] = '\0';
        if (WDRV_WINC_STATUS_OK != WDRV_WINC_InfoDeviceNameSet(pInstance->wdrvHandle, devName)) {
            LOG_E("[%s:%d]Warning: Could not set device name", __FILE__, __LINE__);
        }
    }
}

static bool SendEvent(wifi_manager_event_t event) {
    if (gEventQH == NULL) {
        // Init hasn't run yet — caller is misusing the API.  Surface as
        // an error rather than silently succeeding.
        LOG_E("WiFi SendEvent: queue not initialized (event=%u)",
              (unsigned)event);
        return false;
    }

    // Bounded timeout: SendEvent can be called from inside
    // wifi_manager_ProcessState (state-machine handlers, deferred-INIT
    // path).  ProcessState holds gProcessStateMutex AND is the
    // queue-drainer.  A blocking portMAX_DELAY send on a full queue
    // would deadlock — the same task can't drain.
    //
    // Detect "I'm already the holder" via xSemaphoreGetMutexHolder and
    // use timeout=0 in that case: waiting can't possibly help when
    // we're the only drainer.  Otherwise 20 ms — long enough for a
    // higher-priority drainer (USB SCPI pump path) to clear room,
    // short enough not to wedge the calling task.
    TickType_t timeout = pdMS_TO_TICKS(20);
    if (gProcessStateMutex != NULL) {
        if (xSemaphoreGetMutexHolder(gProcessStateMutex) ==
            xTaskGetCurrentTaskHandle()) {
            timeout = 0;
        }
    }
    if (xQueueSend(gEventQH, &event, timeout) == pdPASS) {
        return true;
    }

    // DEINIT/INIT are lifecycle-critical: the manager can wedge if these
    // are dropped.  If the queue is full enough that we couldn't even
    // wait 20 ms, something has already gone wrong; drain the queue
    // and force the event in.  Pending stale events would have been
    // invalidated by the lifecycle transition anyway.
    //
    // Use xQueueReceive(timeout=0) drain instead of xQueueReset:
    // FreeRTOS docs warn xQueueReset is unsafe if any task is blocked
    // on the queue (would leave it in inconsistent state).  In our
    // case the only drainer is ProcessState's xQueueReceive(timeout=1)
    // which doesn't block long, but the drain-loop approach is
    // safer-by-construction.
    if (event == WIFI_MANAGER_EVENT_DEINIT ||
        event == WIFI_MANAGER_EVENT_INIT ||
        event == WIFI_MANAGER_EVENT_REINIT) {
        LOG_E("WiFi SendEvent: queue full, draining for lifecycle event=%u",
              (unsigned)event);
        wifi_manager_event_t drop;
        while (xQueueReceive(gEventQH, &drop, 0) == pdPASS) {
            /* drop stale events */
        }
        if (xQueueSend(gEventQH, &event, 0) == pdPASS) {
            return true;
        }
    }
    LOG_E("WiFi SendEvent: queue full (event=%u)", (unsigned)event);
    return false;
}

static wifi_manager_stateMachineReturnStatus_t MainState(stateMachineInst_t * const pInstance, uint16_t event) {
    wifi_manager_stateMachineReturnStatus_t returnStatus = WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_HANDLED;
    switch (event) {
        case WIFI_MANAGER_EVENT_ENTRY:
            returnStatus = WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_HANDLED;
            pInstance->pTcpServerContext = &gTcpServerContext;
            wifi_tcp_server_Initialize(pInstance->pTcpServerContext);
            memset(&pInstance->wifiFirmwareVersion, 0, sizeof (tstrM2mRev));

            // Preserve WiFi firmware update mode request flag across state reset
            bool wifiFwUpdateRequested = GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_WIFI_FW_UPDATE_REQUESTED);

            ResetAllEventFlags(&pInstance->eventFlags);
            pInstance->udpServerSocket = -1;
            pInstance->wdrvHandle = DRV_HANDLE_INVALID;
            pInstance->assocHandle = WDRV_WINC_ASSOC_HANDLE_INVALID;
            // Clear any retry-backoff deadline carried over from a previous
            // ERROR cycle.  ENTRY also fires on transitions back to
            // MainState (FW-update divert, fresh-init fallback at the
            // bottom of MainState, #437b HardReset re-entry), where a
            // stale deadline would unnecessarily delay the next REINIT
            // attempt by up to 30 s.  Qodo round-3 finding (medium).
            pInstance->retryAfterTick = 0;

            if (wifiFwUpdateRequested) {
                SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_WIFI_FW_UPDATE_REQUESTED);
                SendEvent(WIFI_MANAGER_EVENT_WIFI_FW_UPDATE_INIT);
            }
            else if (pInstance->pWifiSettings->isEnabled) {
                SendEvent(WIFI_MANAGER_EVENT_INIT);
            }
            break;
        case WIFI_MANAGER_EVENT_WIFI_FW_UPDATE_INIT:
            returnStatus = WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_HANDLED;
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_BUSY) {
                SendEvent(WIFI_MANAGER_EVENT_WIFI_FW_UPDATE_INIT);
                break;
            }
            sysObj.drvWifiWinc = WDRV_WINC_Initialize(0, NULL);
            SendEvent(WIFI_MANAGER_EVENT_WIFI_FW_UPDATE_READY);
            break;
        case WIFI_MANAGER_EVENT_WIFI_FW_UPDATE_READY:
        {
            SYS_STATUS wincStatus = WDRV_WINC_Status(sysObj.drvWifiWinc);
            if (wincStatus == SYS_STATUS_BUSY) {
                SendEvent(WIFI_MANAGER_EVENT_WIFI_FW_UPDATE_INIT);
                break;
            }
            SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_INITIALIZED);
            if (wincStatus == SYS_STATUS_READY) {
                if (m2m_wifi_get_firmware_version(&pInstance->wifiFirmwareVersion) != M2M_SUCCESS) {
                    memset(&pInstance->wifiFirmwareVersion, 0, sizeof (tstrM2mRev));
                }
            }
            wifi_serial_bridge_Init(&pInstance->serialBridgeContext);
            SetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_WIFI_FW_UPDATE_READY);
            // Reached steady state for FW-update mode — release APPLY gate (#425).
            // Without this, FW-update mode never clears the gate (no STA_CONNECTED
            // / AP_STARTED in this lifecycle), so APPLY is locked out for 30 s
            // and a misleading "safety release" log fires every FW-update cycle.
            // Reverse store order — see AP_STARTED comment.
            gApplyInProgressDeadlineTick = 0;
            gApplyInProgress = false;
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

                SYS_STATUS wincStatus = WDRV_WINC_Status(sysObj.drvWifiWinc);
                if (wincStatus != SYS_STATUS_READY) {
                    // Log error only once when entering error state
                    if (!initErrorLogged) {
                        LOG_E("WiFi driver not ready (status=%d), retrying...\r\n", wincStatus);
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
            // Apply MAC suffix to default SSID/hostname and set WINC device name
            ApplyDefaultSuffixAndDeviceName(pInstance);
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
                // Reached steady state — release APPLY gate (#425).
                // Order: deadline first, flag second.  If a higher-priority
                // SCPI APPLY preempts between the two stores, it enters
                // UpdateNetworkSettings's critical section and sees flag=true
                // (still set by us) — so it cleanly rejects with -200.
                // Original order (flag first) would let the preempting arm
                // observe flag=false, set its own deadline+flag, and then
                // we'd resume to overwrite its deadline with 0 — causing
                // the safety release to fire prematurely against the
                // newly-armed gate.
                gApplyInProgressDeadlineTick = 0;
                gApplyInProgress = false;

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
            // Reached steady state — release APPLY gate (#425).  Reverse
            // store order (deadline first, flag second) — see AP_STARTED
            // comment for why.
            gApplyInProgressDeadlineTick = 0;
            gApplyInProgress = false;

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
            // #416 / #446: deadline-based retry backoff.  The ERROR
            // handler sets retryAfterTick on STA failure; honor it by
            // polling at 50 ms (same pattern as the BUSY case below)
            // instead of holding the state-machine mutex for up to 30 s.
            //
            // Round-5: gate on STA mode.  If the user APPLY'd a mode
            // change mid-backoff (STA→AP, FW-update entry, etc.), the
            // STA-side deadline becomes stale — clear it instead of
            // delaying the unrelated reinit path.
            if (pInstance->retryAfterTick != 0) {
                if (pInstance->pWifiSettings->networkMode != WIFI_MANAGER_NETWORK_MODE_STA) {
                    pInstance->retryAfterTick = 0;
                } else {
                    TickType_t now = xTaskGetTickCount();
                    if ((int32_t)(pInstance->retryAfterTick - now) > 0) {
                        vTaskDelay(pdMS_TO_TICKS(50));
                        SendEvent(WIFI_MANAGER_EVENT_REINIT);
                        break;
                    }
                    pInstance->retryAfterTick = 0;  // deadline reached
                }
            }
            if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_WIFI_FW_UPDATE_READY)) {
                //If WiFi firmware update is running and again initialized, then deinit WiFi firmware update
                ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_WIFI_FW_UPDATE_READY);
                ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_WIFI_FW_UPDATE_REQUESTED);
                wifi_serial_bridge_interface_DeInit();

                // Use DEINIT event for proper cleanup instead of manual reset
                // DEINIT will Close handle, Deinitialize driver, fix reset state, then transition to MainState
                SendEvent(WIFI_MANAGER_EVENT_DEINIT);
                break;
            }
            if (WDRV_WINC_Status(sysObj.drvWifiWinc) == SYS_STATUS_BUSY) {
                vTaskDelay(pdMS_TO_TICKS(50)); // Add a small delay to prevent busy-looping
                SendEvent(WIFI_MANAGER_EVENT_REINIT);
                break;
            }

            // If WiFi firmware update requested, transition to MainState which will route to WIFI_FW_UPDATE_INIT
            if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_WIFI_FW_UPDATE_REQUESTED)) {
                LOG_D("WiFi firmware update mode requested - transitioning to MainState\r\n");
                returnStatus = WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_TRAN;
                pInstance->nextState = MainState;
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
                    ArmApplyTeardownDeadline();  // #423: demote the impending async disconnect callback
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
                    
                    // Disconnect if connected — or even just started.  A
                    // previous association attempt (e.g. bad-password retry
                    // cycle) may leave the WINC driver's pCtrl->isConnected
                    // stuck true even after our STA_CONNECTED flag was
                    // cleared by the STA_DISCONNECTED handler — observed
                    // when WINC emits CONNECTED briefly during 4-way
                    // handshake then auth-fail DISCONNECTED.  Without
                    // this disconnect the next WDRV_WINC_IPUseDHCPSet
                    // below returns REQUEST_ERROR and the manager wedges
                    // into "Error setting DHCP" -> ERROR -> REINIT loop
                    // recoverable only by SYST:POW:STAT 0/1 (#467).
                    // BSSDisconnect returns REQUEST_ERROR harmlessly when
                    // isConnected is already false; we ignore the return.
                    if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_CONNECTED) ||
                        GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_STARTED)) {
                        ArmApplyTeardownDeadline();  // #423
                        (void)WDRV_WINC_BSSDisconnect(pInstance->wdrvHandle);
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
            
            // Now handle reconfiguration within the same mode.
            // Apply MAC suffix first — same-mode paths bypass the INIT handler.
            ApplyDefaultSuffixAndDeviceName(pInstance);

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

                    // #435: if a TCP socket is active when APPLY hits, the
                    // WINC chip's internal socket table retains stale state
                    // through soft reconfigure and the new association
                    // oscillates (8+ CONNECT→DISCONNECT cycles before
                    // settling).  Bench-confirmed via instrumentation.  Soft
                    // reconfigure of just the BSS context isn't enough — we
                    // need a full chip reset to clear the socket table.
                    //
                    // Divert to HardReset (DEINIT → 2 s settle → INIT) which
                    // pulls RESET_N + CHIP_EN low and brings the chip back
                    // up clean.  The deferred-INIT path in ProcessState
                    // picks up the new settings on its way back up.
                    // Divert to HardReset whenever ANY socket is bound on
                    // the chip (server-listening counts).  Bench evidence
                    // (#437b regression, 2026-05-09) showed that even a
                    // bare listening server socket retains stale state
                    // across BSSDisconnect+BSSConnect and produces the
                    // same flap-every-1.5 s symptom as the active-client
                    // case (#435).  The earlier #438 round-3 narrowing
                    // to HasActiveClient() was based on Qodo's "too broad"
                    // critique but was disproven by bench storm-after-bind
                    // testing — soft-reconfigure cannot fully clear the
                    // chip's per-association socket table without RESET_N.
                    //
                    // Cost: ~30 s HardReset cycle on every APPLY in normal
                    // STA operation (server is bound throughout).  Soft-
                    // reconfigure was ~10-25 s anyway, so the practical
                    // difference is minor; reliability beats speed.
                    if (wifi_tcp_server_HasActiveClient() ||
                        GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_TCP_SOCKET_OPEN) ||
                        GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_OPEN)) {
                        LOG_E("STA reconfig with bound socket — diverting to HardReset (#435/#437b)");
                        wifi_tcp_server_CloseSocket();
                        if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_OPEN)) {
                            CloseUdpSocket(&pInstance->udpServerSocket);
                        }
                        // Clear socket + STA state flags so callers that
                        // check status during the chip-reset window (~2 s)
                        // don't observe stale "connected" state.  No
                        // critical section: every writer of
                        // pInstance->eventFlags in this codebase is on
                        // WifiTask (StaEventCallback / SocketEventCallback
                        // only call SendEvent — no flag mutations) so the
                        // RMW is single-threaded.  Per CLAUDE.md
                        // ("do not add unnecessary critical sections"), the
                        // earlier speculative wrap was inappropriate.  Also
                        // clear UDP_SOCKET_CONNECTED so it doesn't outlast
                        // UDP_SOCKET_OPEN.
                        ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_TCP_SOCKET_OPEN);
                        ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_OPEN);
                        ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_CONNECTED);
                        ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_CONNECTED);
                        ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_STARTED);
                        // wifi_manager_HardReset arms the deferred-INIT
                        // deadline and queues DEINIT.  isEnabled was just
                        // set to 1 by UpdateNetworkSettings (since this
                        // REINIT was queued) — HardReset will clear it
                        // for the DEINIT path, then ProcessState's
                        // deferred-INIT branch flips it back and queues
                        // INIT once the 2 s settle elapses.  If the DEINIT
                        // event can't be queued, propagate the failure as
                        // an ERROR event so the manager retries instead of
                        // silently leaving the chip in a half-reset state.
                        ArmApplyTeardownDeadline();  // #423: HardReset triggers an async disconnect callback during deinit
                        if (!wifi_manager_HardReset()) {
                            LOG_E("HardReset divert failed to queue DEINIT");
                            // Try ERROR fallback so the manager retries; if
                            // *that* enqueue also fails, the queue is wedged
                            // and the only recovery is SYST:REBoot — log
                            // explicitly so the user sees it in SYST:LOG?.
                            if (!SendEvent(WIFI_MANAGER_EVENT_ERROR)) {
                                LOG_E("HardReset divert: ERROR fallback also failed; manager may be stuck — SYST:REBoot");
                            }
                        }
                        break;
                    }

                    // Disconnect if connected — or even just started.  A
                    // previous association attempt (e.g. bad-password retry
                    // cycle) may leave the WINC driver's pCtrl->isConnected
                    // stuck true even after our STA_CONNECTED flag was
                    // cleared by the STA_DISCONNECTED handler — observed
                    // when WINC emits CONNECTED briefly during 4-way
                    // handshake then auth-fail DISCONNECTED.  Without
                    // this disconnect the next WDRV_WINC_IPUseDHCPSet
                    // below returns REQUEST_ERROR and the manager wedges
                    // into "Error setting DHCP" -> ERROR -> REINIT loop
                    // recoverable only by SYST:POW:STAT 0/1 (#467).
                    // BSSDisconnect returns REQUEST_ERROR harmlessly when
                    // isConnected is already false; we ignore the return.
                    if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_CONNECTED) ||
                        GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_STA_STARTED)) {
                        ArmApplyTeardownDeadline();  // #423
                        (void)WDRV_WINC_BSSDisconnect(pInstance->wdrvHandle);
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
                // Reset driver object to force complete reinitialization
                // This is especially important after WiFi firmware update mode exit
                sysObj.drvWifiWinc = SYS_MODULE_OBJ_INVALID;
                returnStatus = WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_TRAN;
                pInstance->nextState = MainState;
            }

            break;
        case WIFI_MANAGER_EVENT_DEINIT:
            if (GetEventFlagStatus(pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_WIFI_FW_UPDATE_READY)) {
                //If WiFi firmware update is running and again initialized, then deinit WiFi firmware update
                ResetEventFlag(&pInstance->eventFlags, WIFI_MANAGER_STATE_FLAG_WIFI_FW_UPDATE_READY);
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

                // #423: when this ERROR is the cascade from an APPLY-
                // triggered teardown (#440 HardReset divert path or
                // BSSDisconnect-then-reconfig), demote the log and don't
                // bump the consecutive-error counter — those events are
                // intentional, not faults.  Same wrap-safe time-window
                // discriminator used in StaEventCallback so genuine
                // failures (wrong password etc.) past the window still
                // LOG_E and bump the counter.  Short-circuit the rest of
                // the recovery pipeline as well: the existing power-
                // efficient reconnect logic below is already gated on
                // STA_STARTED (cleared by the teardown) so it's a no-op
                // in practice, but breaking here makes the intent
                // explicit and stops future refactors from accidentally
                // driving reconnects during a deliberate teardown.
                if (IsWithinApplyTeardownWindow()) {
                    LOG_I("WiFi event ERROR during APPLY teardown\r\n");
                    returnStatus = WIFI_MANAGER_STATE_MACHINE_RETURN_STATUS_HANDLED;
                    break;
                }

                consecutiveErrors++;
                // Only log error on first occurrence or every 10th error.
                if (!errorLogged || (consecutiveErrors % 10 == 0)) {
                    LOG_E("[%s:%d]WiFi error occurred (count: %d)\r\n",
                          __FILE__, __LINE__, consecutiveErrors);
                    errorLogged = true;
                }

                // Implement power-efficient reconnect logic for STA mode.
                // #416: applies to BOTH initial-association failures (the
                // configured AP isn't reachable yet) AND reconnect-after-
                // disconnect, not just post-STA_STARTED.  Previously the
                // STA_STARTED gate let the else branch below hammer the
                // WINC at ~30/sec on unreachable APs (200 Hz state-machine
                // dispatch × ~30 ms per failed BSSConnect cycle) — measured
                // 18,920 retries in 10 min in the field.
                if (pInstance->pWifiSettings->networkMode == WIFI_MANAGER_NETWORK_MODE_STA) {

                    // Snapshot the counter BEFORE the increment so the
                    // delay/log decisions reflect the attempt that JUST
                    // failed.  Without the snapshot, the increment below
                    // would bump it to 1 on the first failure and the
                    // `attempt == 0` log branch would never fire (Qodo
                    // round-2 suggestion on PR #446).
                    uint8_t attempt = pInstance->staReconnectAttempts;

                    // For initial-association failures, STA_DISCONNECTED
                    // never fires (we never connected), so its attempt-
                    // counter increment at line ~969 is skipped — bump it
                    // here in that case so backoff escalates the same way
                    // it does after a disconnect.  Clamp to prevent the
                    // uint8_t from wrapping to 0 on long-running failures
                    // (#446 Qodo round 1 finding 3).
                    if (!GetEventFlagStatus(pInstance->eventFlags,
                                            WIFI_MANAGER_STATE_FLAG_STA_STARTED)) {
                        if (pInstance->staReconnectAttempts < 200) {
                            pInstance->staReconnectAttempts++;
                        }
                    }

                    uint32_t delayMs;
                    if (attempt < 5) {
                        // First 5 attempts: 1 second delay
                        delayMs = 1000;
                        if (attempt == 0) {  // Only log first attempt
                            LOG_D("STA reconnect attempt %u/5 with 1s delay\r\n",
                                  (unsigned)(attempt + 1));
                        }
                    } else {
                        // After 5 attempts: 30 second delay to save power
                        delayMs = 30000;
                        if (attempt == 5) {  // Only log when switching to power save
                            LOG_D("Switching to power save mode - reconnect attempts with 30s delay\r\n");
                        }
                    }

                    // Set a deadline and let the REINIT handler poll the
                    // BUSY/RETRY-wait path that's already there (50 ms
                    // vTaskDelay + re-queue).  Without this, we'd hold
                    // the state-machine mutex during the 30 s delay,
                    // blocking SCPI active-pump commands (#446 Qodo
                    // round 1 finding 2).
                    //
                    // Sentinel-collision guard: `0` means "no deadline";
                    // if the tick arithmetic happens to land on 0 (rare
                    // at 32-bit wrap), bump to 1 so the REINIT handler's
                    // `retryAfterTick != 0` check still trips.
                    //
                    // Round-5: keep the earliest existing deadline if one
                    // is already armed.  A burst of ERROR events (e.g.
                    // queued faster than REINIT drains) would otherwise
                    // each push the deadline forward, starving the retry.
                    TickType_t newDeadline = xTaskGetTickCount() + pdMS_TO_TICKS(delayMs);
                    if (newDeadline == 0) {
                        newDeadline = 1;
                    }
                    if (pInstance->retryAfterTick == 0 ||
                        (int32_t)(pInstance->retryAfterTick - newDeadline) > 0) {
                        pInstance->retryAfterTick = newDeadline;
                    }
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
        if (GetEventFlagStatus(gStateMachineContext.eventFlags, WIFI_MANAGER_STATE_FLAG_WIFI_FW_UPDATE_READY))
            wifi_serial_bridge_Process(&gStateMachineContext.serialBridgeContext);
        else {
            vTaskSuspend(NULL);
        }
    }
}

void wifi_manager_BootInit(void) {
    /* Defensive zero-init of every file-static in this module so retained
     * RAM doesn't leak across MCLR / IPE flash (#409). Must run BEFORE
     * the scheduler starts and BEFORE wifi_manager_Init can be called.
     * No locking needed — pre-scheduler, single-threaded. */
    memset(&gStateMachineContext, 0, sizeof(gStateMachineContext));
    memset(&gTcpServerContext, 0, sizeof(gTcpServerContext));
    memset(&gProcessStateMutexBuf, 0, sizeof(gProcessStateMutexBuf));
    gEventQH = NULL;
    gProcessStateMutex = NULL;
    gRssiUpdatePending = false;
    gRssiUpdateComplete = false;
    gTcpRxPending = false;
    gLastRssiPercentage = 0;
    gWifiReinitDeadlineTick = 0;
    gApplyInProgress = false;
    gApplyInProgressDeadlineTick = 0;
    gApplyTeardownStartTick = 0;
}

bool wifi_manager_Init(wifi_manager_settings_t * pSettings) {

    // Init order matters: mutex first (so ProcessState calls before queue
    // creation are still safe), then queue.  If queue already exists, we
    // were already initialized — bail.
    if (gProcessStateMutex == NULL) {
        gProcessStateMutex = xSemaphoreCreateRecursiveMutexStatic(&gProcessStateMutexBuf);
    }

    if (gEventQH == NULL) {
        gEventQH = xQueueCreate(20, sizeof (wifi_manager_event_t));
        if (gEventQH == NULL) {
            LOG_E("WiFi Init: failed to create event queue");
            return false;
        }
    } else {
        // Re-init path: queue already exists from a prior Init.  Previously
        // this short-circuited with `return true`, but that path leaves the
        // state machine stuck in DEINIT after a POW:STAT 0 → 1 cycle (queue
        // and FSM state survive but the chip was reset by Deinit) — the
        // chip never gets driven back through INIT.  Instead, re-arm
        // isEnabled and queue a fresh INIT event to walk the state machine
        // back up to MAIN.  Mirrors the deferred-INIT branch in
        // wifi_manager_ProcessState.
        if (pSettings != NULL) {
            gStateMachineContext.pWifiSettings = pSettings;
        }
        if (gStateMachineContext.pWifiSettings != NULL) {
            taskENTER_CRITICAL();
            gWifiReinitDeadlineTick = 0;  // cancel any pending deferred-INIT
            gStateMachineContext.pWifiSettings->isEnabled = 1;
            taskEXIT_CRITICAL();
        }
        SendEvent(WIFI_MANAGER_EVENT_INIT);
        return true;
    }
    if (pSettings != NULL)
        gStateMachineContext.pWifiSettings = pSettings;
    if (gStateMachineContext.fwUpdateTaskHandle == NULL) {
        BaseType_t result = xTaskCreate(fwUpdateTask, "fwUpdateTask", 1024, NULL, 2, &gStateMachineContext.fwUpdateTaskHandle);  // Keep original — FW flash path not fully profiled
        if (result != pdPASS) {
            LOG_E("Failed to create fwUpdateTask (1024 bytes)\r\n");
            gStateMachineContext.fwUpdateTaskHandle = NULL;
        }
    }
    gStateMachineContext.active = MainState;
    gStateMachineContext.active(&gStateMachineContext, WIFI_MANAGER_EVENT_ENTRY);
    gStateMachineContext.nextState = NULL;

    // #377: iperf2 module — initialize once at boot.  Doesn't open any
    // sockets until SYST:WIFI:IPERF:* is invoked.
    Iperf2_Initialize();

    return true;
}

bool wifi_manager_GetChipInfo(wifi_manager_chipInfo_t *pChipInfo) {
    if (!GetEventFlagStatus(gStateMachineContext.eventFlags, WIFI_MANAGER_STATE_FLAG_INITIALIZED)) {
        LOG_D("WiFi GetChipInfo: not initialized");
        return false;
    }
    if (pChipInfo == NULL) {
        LOG_E("WiFi GetChipInfo: NULL pointer");
        return false;
    }
    memset(pChipInfo, 0, sizeof (wifi_manager_chipInfo_t));
    pChipInfo->chipID = gStateMachineContext.wifiFirmwareVersion.u32Chipid;
    snprintf(pChipInfo->frimwareVersion, WIFI_MANAGER_CHIP_INFO_FW_VERSION_MAX_SIZE, "%d.%d.%d", gStateMachineContext.wifiFirmwareVersion.u8FirmwareMajor,
            gStateMachineContext.wifiFirmwareVersion.u8FirmwareMinor,
            gStateMachineContext.wifiFirmwareVersion.u8FirmwarePatch);
    strncpy(pChipInfo->BuildDate,
            (const char*)gStateMachineContext.wifiFirmwareVersion.BuildDate,
            sizeof(pChipInfo->BuildDate) - 1);
    pChipInfo->BuildDate[sizeof(pChipInfo->BuildDate) - 1] = '\0';
    strncpy(pChipInfo->BuildTime,
            (const char*)gStateMachineContext.wifiFirmwareVersion.BuildTime,
            sizeof(pChipInfo->BuildTime) - 1);
    pChipInfo->BuildTime[sizeof(pChipInfo->BuildTime) - 1] = '\0';
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
    if (gStateMachineContext.pWifiSettings == NULL) return false;
    // Tear down iperf2 sockets before the WINC GPIO toggle — otherwise
    // the dedicated Iperf2 task could call send() against an
    // already-deinitialized chip and wedge the HIF queue.
    Iperf2_Stop();
    // Cancel any pending deferred-INIT and clear isEnabled atomically —
    // without the critical section, a higher-priority caller of Deinit
    // could race ProcessState's deferred-INIT branch and end up with
    // isEnabled=1 even though the user requested Deinit.
    taskENTER_CRITICAL();
    gWifiReinitDeadlineTick = 0;
    gStateMachineContext.pWifiSettings->isEnabled = 0;
    // Explicit teardown — release APPLY gate so a follow-up APPLY isn't
    // blocked by the 30 s safety timeout.  Reverse store order (#425).
    gApplyInProgressDeadlineTick = 0;
    gApplyInProgress = false;
    taskEXIT_CRITICAL();
    return SendEvent(WIFI_MANAGER_EVENT_DEINIT);
}

// True hardware reset of the WINC chip: disable -> DEINIT (toggles
// CHIP_EN/RESET_N GPIOs via wifi_manager_FixWincResetState) -> after
// 2 s settle, re-enable -> REINIT to bring the chip back up.
//
// This is the only path that recovers a wedged outbound TCP stack
// (#383). APPLY's REINIT path explicitly skips Deinitialize because
// of a Microchip driver bug (see comment near line 867), so APPLY
// alone never drives the WINC reset GPIOs.
//
// Returns immediately — the deferred-INIT runs from
// wifi_manager_ProcessState's deadline check, so this works
// correctly whether HardReset is called from app_WifiTask itself
// (TCP SCPI dispatch path, post-#353) or from another task (USB
// CDC).  Full recovery (DEINIT settle + STA reassoc + DHCP)
// typically takes ~20 s; poll SYST:COMM:LAN:ADDR? to confirm.
bool wifi_manager_HardReset(void) {
    if (gStateMachineContext.pWifiSettings == NULL) return false;
    // Tear down iperf2 sockets before the WINC GPIO toggle (same reason
    // as wifi_manager_Deinit).
    Iperf2_Stop();
    LOG_I("WiFi: hard reset (DEINIT -> REINIT in 2 s)");
    // Set isEnabled=0 + arm deadline atomically so a concurrent Deinit
    // can't race the (deadline, isEnabled) pair into an inconsistent
    // state.
    taskENTER_CRITICAL();
    gStateMachineContext.pWifiSettings->isEnabled = 0;
    gWifiReinitDeadlineTick = xTaskGetTickCount() + pdMS_TO_TICKS(2000);
    // Explicit teardown — release APPLY gate (#425).  Reverse store order.
    gApplyInProgressDeadlineTick = 0;
    gApplyInProgress = false;
    taskEXIT_CRITICAL();
    return SendEvent(WIFI_MANAGER_EVENT_DEINIT);
}

bool wifi_manager_UpdateNetworkSettings(wifi_manager_settings_t * pSettings) {

    // Always allow settings to be updated regardless of power state
    // This allows configuration while the device is off or WiFi is disabled

    if (pSettings == NULL || gStateMachineContext.pWifiSettings == NULL) {
        // Caller programming error — surface as -200 rather than silently
        // pretending success.
        LOG_E("WiFi UpdateNetworkSettings: NULL settings pointer");
        return false;
    }

    // Decide up front whether this call would queue a REINIT.  If yes, the
    // gate must be claimed atomically before we touch runtime state — see
    // #425 bugs 2 & 3: a check-then-act window let two concurrent SCPI
    // tasks (USB pri 7 + WifiTask TCP-SCPI) both pass the check and stack
    // REINIT events, AND callers other than SCPI APPLY (LANSettingsLoad,
    // LANSettingsFactoryLoad) bypassed the gate entirely.  Centralizing
    // the gate here protects every caller automatically.
    const tPowerData *pPowerState = (tPowerData *) BoardData_Get(BOARDDATA_POWER_DATA, 0);
    bool isPowered = (pPowerState != NULL &&
                     (pPowerState->powerState == POWERED_UP ||
                      pPowerState->powerState == POWERED_UP_EXT_DOWN));
    bool wifiFwUpdateRequested = GetEventFlagStatus(gStateMachineContext.eventFlags, WIFI_MANAGER_STATE_FLAG_WIFI_FW_UPDATE_REQUESTED);
    bool willReinit = (pSettings->isEnabled || wifiFwUpdateRequested) && isPowered;

    if (willReinit) {
        TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(WIFI_APPLY_GATE_TIMEOUT_MS);
        // 0 is the "unarmed" sentinel — bump to 1 so a wraparound landing
        // exactly on 0 doesn't disable the safety release.
        if (deadline == 0) {
            deadline = 1;
        }
        // Atomic test-and-set.  Without the critical section, two SCPI
        // contexts can both observe gApplyInProgress=false and both
        // proceed to set it true and queue REINIT — defeating the gate.
        taskENTER_CRITICAL();
        if (gApplyInProgress) {
            taskEXIT_CRITICAL();
            return false;  // gate engaged — caller translates to -200
        }
        gApplyInProgress = true;
        gApplyInProgressDeadlineTick = deadline;
        taskEXIT_CRITICAL();
    }

    // Preserve the MAC address before copying new settings
    WDRV_WINC_MAC_ADDR savedMacAddr;
    memcpy(&savedMacAddr, &gStateMachineContext.pWifiSettings->macAddr, sizeof(WDRV_WINC_MAC_ADDR));

    // Snapshot prior settings so we can roll back on REINIT enqueue failure
    // (#425 round 3 / Qodo).  When willReinit is false we don't need this —
    // there's no failure path that mutates between this point and return.
    wifi_manager_settings_t prevSettings;
    if (willReinit) {
        memcpy(&prevSettings, gStateMachineContext.pWifiSettings, sizeof(prevSettings));
    }

    // Copy new settings
    memcpy(gStateMachineContext.pWifiSettings, pSettings, sizeof (wifi_manager_settings_t));

    // Restore the MAC address
    memcpy(&gStateMachineContext.pWifiSettings->macAddr, &savedMacAddr, sizeof(WDRV_WINC_MAC_ADDR));

    // Also update BoardData so the app task sees the changes
    BoardData_Set(BOARDDATA_WIFI_SETTINGS, 0, gStateMachineContext.pWifiSettings);

    if (willReinit) {
        if (!SendEvent(WIFI_MANAGER_EVENT_REINIT)) {
            // Queue full or queue not initialized — REINIT won't fire.
            // Roll back the settings copy so a "false" return cleanly means
            // "no state changed", and release the gate (reverse store order).
            memcpy(gStateMachineContext.pWifiSettings, &prevSettings, sizeof(prevSettings));
            BoardData_Set(BOARDDATA_WIFI_SETTINGS, 0, gStateMachineContext.pWifiSettings);
            taskENTER_CRITICAL();
            gApplyInProgressDeadlineTick = 0;
            gApplyInProgress = false;
            taskEXIT_CRITICAL();
            LOG_E("WiFi REINIT enqueue failed; settings + APPLY gate rolled back");
            return false;
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

// #382 sub-bug 1: periodically reconcile STA_CONNECTED with chip reality.
// Fires an async RSSI probe at most once per WIFI_STA_RECONCILE_PERIOD_TICKS
// when STA is started, an association handle is valid, but our flag says
// disconnected.  The RssiEventCallback repairs the flag on a successful
// response (chip confirms it knows about this assocHandle).  No-op when
// the flag is in sync (the common case) — zero cost on a healthy link.
static void MaybeReconcileStaConnected(void) {
    // Phase 1: apply any pending repair signaled by RssiEventCallback.
    // Snapshot+clear pending state + handle atomically under a critical
    // section.  The handle snapshot guards against the disconnect race:
    // if StaEventCallback cleared assocHandle between the RSSI callback
    // and now, the snapshot won't match the live handle and we skip
    // the repair (otherwise we'd set STA_CONNECTED on a disconnected
    // chip, misreporting status to userspace).  Gate on STA_STARTED so
    // an out-of-mode signal can't promote into AP mode where AP_STARTED
    // is the active flag.
    //
    // The SetEventFlag call itself is wrapped in a critical section to
    // satisfy the PIC32MZ atomicity rule (CLAUDE.md: "RMW |= needs
    // critical section") — the value |= flag inside SetEventFlag is
    // a non-atomic RMW shared with other task-context writers in this
    // file.  Qodo #451 pass 3 findings.
    bool repairPending;
    WDRV_WINC_ASSOC_HANDLE pendingHandle;
    taskENTER_CRITICAL();
    repairPending = gStaReconcilePending;
    pendingHandle = gStaReconcilePendingHandle;
    gStaReconcilePending = false;
    gStaReconcilePendingHandle = WDRV_WINC_ASSOC_HANDLE_INVALID;
    taskEXIT_CRITICAL();
    if (repairPending &&
        pendingHandle != WDRV_WINC_ASSOC_HANDLE_INVALID &&
        gStateMachineContext.assocHandle == pendingHandle &&
        GetEventFlagStatus(gStateMachineContext.eventFlags,
                           WIFI_MANAGER_STATE_FLAG_STA_STARTED) &&
        !GetEventFlagStatus(gStateMachineContext.eventFlags,
                            WIFI_MANAGER_STATE_FLAG_STA_CONNECTED)) {
        taskENTER_CRITICAL();
        SetEventFlag(&gStateMachineContext.eventFlags,
                     WIFI_MANAGER_STATE_FLAG_STA_CONNECTED);
        taskEXIT_CRITICAL();
    }

    // Phase 2: probe the chip if the flag still looks drifted.
    // Skip the probe during APPLY/REINIT teardown: the flag-clear sequence
    // in the divert-to-HardReset path (STA_CONNECTED before STA_STARTED)
    // is preemptable at a tick boundary, so we could observe STA_STARTED
    // still set with a stale gStateMachineContext.assocHandle.  Driving a
    // WDRV_WINC_AssocRSSIGet against a handle the driver no longer
    // recognises could trigger an assertion in the chip-state machine
    // (Qodo #451 pass 2 finding, importance 8).
    if (gApplyInProgress || IsWithinApplyTeardownWindow()) {
        return;
    }
    if (gStateMachineContext.assocHandle == WDRV_WINC_ASSOC_HANDLE_INVALID) {
        return;
    }
    if (!GetEventFlagStatus(gStateMachineContext.eventFlags,
                            WIFI_MANAGER_STATE_FLAG_STA_STARTED)) {
        return;
    }
    if (GetEventFlagStatus(gStateMachineContext.eventFlags,
                           WIFI_MANAGER_STATE_FLAG_STA_CONNECTED)) {
        return;  // flag is in sync — nothing to do
    }
    TickType_t now = xTaskGetTickCount();
    // Rate-limit using elapsed delta, not absolute compare — avoids tick
    // wraparound bugs (~50 days on 1 kHz tick, but a robust pattern is
    // cheap and matches the deadline math used elsewhere in this file).
    if ((TickType_t)(now - gStaReconcileLastTick) < WIFI_STA_RECONCILE_PERIOD_TICKS) {
        return;
    }
    gStaReconcileLastTick = now;
    // Fire-and-forget probe.  If the chip responds (cached or retry-
    // request), RssiEventCallback runs and sets STA_CONNECTED.  If the
    // chip is genuinely disconnected, the driver call may return an
    // error or simply never fire the callback — both leave the flag
    // cleared, which is the correct state.
    int8_t rssi;
    (void)WDRV_WINC_AssocRSSIGet(gStateMachineContext.assocHandle, &rssi,
                                 RssiEventCallback);
}

// Internal worker for both ProcessState entry points.  drainTcpRx=true
// is the normal WifiTask path (drains deferred TCP rx and re-arms recv).
// drainTcpRx=false is the SCPI active-pump path: it drives WiFi
// lifecycle/event-queue work without dispatching new TCP-arrived SCPI,
// to prevent re-entrant SCPI_Input() on the same scpiContext when the
// outer SCPI command itself arrived over TCP.
static void wifi_manager_ProcessStateImpl(bool drainTcpRx) {
    wifi_manager_event_t event;
    wifi_manager_stateMachineReturnStatus_t ret;
    while (gEventQH == NULL) {
        return;
    }

    // Serialize re-entrant callers (app_WifiTask + SCPI active-pump
    // paths, and any future call site).  Recursive — same task can
    // re-enter (TCP SCPI nests ProcessState inside ProcessState); see
    // gProcessStateMutex declaration for the rationale.
    //
    // Bounded 250 ms timeout (instead of portMAX_DELAY): if the holder
    // is genuinely wedged, the caller must be able to bail and return
    // — otherwise SCPI active pumps would hang and the device could
    // never reboot.  250 ms must exceed the worst-case in-mutex delay,
    // which is nm_reset()'s ~120 ms during DEINIT (CHIP_EN/RESET_N
    // pulse with vTaskDelays).  Normal body completes in <10 ms, so
    // 250 ms gives generous headroom for healthy operation while
    // failing fast on actual stalls.  Skipped iterations recover
    // naturally on the next invocation.
    if (gProcessStateMutex != NULL) {
        if (xSemaphoreTakeRecursive(gProcessStateMutex,
                                    pdMS_TO_TICKS(250)) != pdTRUE) {
            LOG_E("WiFi ProcessState: mutex timeout — skipping iteration");
            return;
        }
    }

    // Deferred-INIT after wifi_manager_HardReset(): once the 2 s settle
    // window has elapsed, re-enable WiFi and queue INIT.  See HardReset
    // for why we can't just vTaskDelay(2000) — the calling task may be
    // app_WifiTask itself (TCP SCPI dispatch path post-#353), and that
    // task is what drains gEventQH.
    //
    // Double-checked under a critical section: a concurrent Deinit could
    // clear gWifiReinitDeadlineTick between the outer check and the
    // isEnabled write, leaving us with isEnabled=1 even though the user
    // called Deinit.  Re-check while holding the lock so only one of
    // (Deinit, deferred-INIT) wins atomically.  SendEvent runs outside
    // the critical section because it can take a queue mutex.
    bool doInit = false;
    if (gWifiReinitDeadlineTick != 0 &&
        (int32_t)(xTaskGetTickCount() - gWifiReinitDeadlineTick) >= 0) {
        taskENTER_CRITICAL();
        if (gWifiReinitDeadlineTick != 0 &&
            (int32_t)(xTaskGetTickCount() - gWifiReinitDeadlineTick) >= 0) {
            gWifiReinitDeadlineTick = 0;
            if (gStateMachineContext.pWifiSettings != NULL) {
                gStateMachineContext.pWifiSettings->isEnabled = 1;
                doInit = true;
            }
        }
        taskEXIT_CRITICAL();
    }
    if (doInit) {
        SendEvent(WIFI_MANAGER_EVENT_INIT);
    }

    // APPLY gate safety release (#425): drop gApplyInProgress if the state
    // machine hasn't reached steady state within the timeout.  Without this,
    // a stuck transition (e.g. unreachable AP) would lock out further SCPI
    // APPLYs permanently.  The arm path bumps the computed deadline from 0
    // to 1, so deadline is never 0 while armed — no separate sentinel check
    // needed (and dropping it lets us recover even if a race somehow leaves
    // flag=true with deadline=0).
    //
    // Critical section: between the condition check and the clear, a
    // preempting Deinit→APPLY sequence (USB SCPI pri 7 vs WifiTask pri 2)
    // could clear the gate then re-arm with a fresh deadline.  Without the
    // critical section, we would then stomp that new state and trigger a
    // spurious safety release.  Reverse store order is preserved inside.
    bool armed_release = false;
    taskENTER_CRITICAL();
    if (gApplyInProgress &&
        (int32_t)(xTaskGetTickCount() - gApplyInProgressDeadlineTick) >= 0) {
        gApplyInProgressDeadlineTick = 0;
        gApplyInProgress = false;
        armed_release = true;
    }
    taskEXIT_CRITICAL();
    if (armed_release) {
        // LOG_E does its own queue work — keep it outside the critical section.
        LOG_E("APPLY gate safety release after %u ms", (unsigned)WIFI_APPLY_GATE_TIMEOUT_MS);
    }

    // #382 sub-bug 1 — repair STA_CONNECTED flag if it drifted from chip
    // reality.  Runs at most every WIFI_STA_RECONCILE_PERIOD_TICKS.
    MaybeReconcileStaConnected();

    wifi_tcp_server_TransmitBufferedData();

    // #377 iperf2 now runs in its own dedicated task with adaptive 2 ms
    // (active) / 50 ms (idle) cadence — see Iperf2_StartTask() in iperf2.c.
    // Removed from this task's loop so streaming + SCPI dispatch keep their
    // 5 ms cadence undisturbed.

    // #353 Option 2: drain any deferred TCP rx data on this task's stack.
    // SOCKET_MSG_RECV (WDRV_WINC_Tasks context) stored length + set the flag
    // but did NOT call ProcessReceivedBuff or re-arm recv — so all of that
    // runs here, on WifiTask's stack, with ~4 KB of headroom for the
    // microrl + libscpi + handler chain.
    //
    // Skipped on the SCPI active-pump path (drainTcpRx=false) so we
    // don't re-enter SCPI_Input() on the same scpiContext when the
    // outer command itself arrived over TCP.  TCP rx remains queued
    // (gTcpRxPending stays set) and gets drained by the normal
    // WifiTask loop after the SCPI handler returns.
    //
    // Always clear the flag under the socket-valid guard, so a RECV that
    // arrived just before the client disconnected doesn't leave stale data
    // queued for the next client.
    if (drainTcpRx && gTcpRxPending) {
        gTcpRxPending = false;
        if (gStateMachineContext.pTcpServerContext != NULL &&
            gStateMachineContext.pTcpServerContext->client.clientSocket >= 0) {
            wifi_tcp_server_ProcessReceivedBuff();
            recv(gStateMachineContext.pTcpServerContext->client.clientSocket,
                 gStateMachineContext.pTcpServerContext->client.readBuffer,
                 WIFI_RBUFFER_SIZE, 0);
        }
    }

    if (xQueueReceive(gEventQH, &event, 1) == pdPASS) {
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

    if (gProcessStateMutex != NULL) {
        xSemaphoreGiveRecursive(gProcessStateMutex);
    }
}

void wifi_manager_ProcessState() {
    wifi_manager_ProcessStateImpl(true);
}

void wifi_manager_ProcessStateNoTcpRx(void) {
    wifi_manager_ProcessStateImpl(false);
}

wifi_tcp_server_context_t* wifi_manager_GetTcpServerContext() {
    return gStateMachineContext.pTcpServerContext;
}

void wifi_manager_RequestWifiFirmwareUpdate(void) {
    SetEventFlag(&gStateMachineContext.eventFlags, WIFI_MANAGER_STATE_FLAG_WIFI_FW_UPDATE_REQUESTED);
}

bool wifi_manager_IsWifiFirmwareUpdateActive(void) {
    return GetEventFlagStatus(gStateMachineContext.eventFlags, WIFI_MANAGER_STATE_FLAG_WIFI_FW_UPDATE_READY);
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
            LOG_I("WiFi GetRSSI: timeout");
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
        LOG_E("WiFi GetRSSI: WINC driver error");
        return false;
    }
}