#define LOG_LVL LOG_LEVEL_WIFI
#define LOG_MODULE LOG_MODULE_WIFI

#include "iperf2.h"
#include "Util/Logger.h"
#include "FreeRTOS.h"
#include "task.h"
#include "socket.h"
#include "m2m_wifi.h"
#include "services/wifi_services/wifi_manager.h"
#include <string.h>

// ============================================================================
// iperf2 — TCP + UDP iperf 2 protocol over WINC m2m sockets, control via
// SCPI.
//
// Modeled on avrxml/asf WINC1500 module-mode iperf_server_example
// (common/components/wifi/winc1500/iperf_server_example/iperf.c) but
// callback-driven instead of poll-driven so it integrates with our
// existing wifi_manager event dispatcher.
// ============================================================================

#define IPERF2_TCP_BUF_SIZE     1400U   // matches WINC SOCKET_BUFFER_MAX_LENGTH for TCP
#define IPERF2_UDP_BUF_SIZE     1470U   // standard iperf2 UDP datagram size (1500 - IP/UDP overhead)
#define IPERF2_MAX_DURATION_S   3600U   // 1h cap
#define IPERF2_MAX_PENDING_TX   4U      // matches WINC HIF queue depth (mirrors wifi_tcp_server)
#define IPERF2_FIN_RETRANSMITS  10U     // iperf2 protocol: 10× retransmit of last UDP pkt

typedef struct {
    volatile Iperf2_Mode    mode;
    SOCKET         listen_sock;     // TCP server only
    SOCKET         data_sock;       // active TCP connection or UDP socket
    volatile bool  client_connected;
    volatile uint8_t pending_tx;    // count of m2m_send not yet ACKed
    volatile TickType_t start_tick;
    volatile TickType_t deadline_tick;   // *_CLIENT mode TX deadline
    uint32_t       duration_ms;     // requested duration
    uint64_t       bytes_transferred;
    uint64_t       bytes_confirmed; // server: rcv'd; client: ACKed by m2m_send callback
    // UDP-specific:
    struct sockaddr_in udp_remote;  // remote peer (set on first recvfrom in server, or in client setup)
    int32_t        udp_id;          // client: next seq to send; server: next expected seq
    uint32_t       udp_total_pkt;
    uint32_t       udp_lost_pkt;
    uint32_t       udp_outoforder;
    uint8_t        udp_fin_count;   // FIN retransmits sent
    volatile bool  abort_pending;   // set in callback context, processed in Iperf2_Tasks
    Iperf2_Stats   last_stats;
} Iperf2_Context;

static Iperf2_Context gCtx;

// Active-mode task delay (ms).  Locked at 2 ms — empirical sweep with
// cross-verified PC iperf2 + firmware-side counters (2026-04-28):
// delay=2ms yields ~454 KB/s (3.70 Mbps) which is the peak; delay=1ms
// is over-aggressive (334 KB/s); delay=3-5ms tie at ~415 KB/s.
#define IPERF2_ACTIVE_DELAY_MS  2U

// Static TX/RX buffers — keep off task stacks.  Sized to the larger of TCP/UDP.
// 4-byte aligned: HandleUdpRecvFrom casts gRxBuf to Iperf2_PktInfo* (which has
// int32_t/uint32_t fields).  MIPS would fault on unaligned 32-bit access.
static uint8_t gTxBuf[IPERF2_UDP_BUF_SIZE] __attribute__((aligned(4)));
static uint8_t gRxBuf[IPERF2_UDP_BUF_SIZE] __attribute__((aligned(4)));

// Handle for the dedicated Iperf2 task — used by the SOCKET_MSG_SEND
// callback to wake the task immediately on slot completion.  Set in
// Iperf2_StartTask, NULL until then.
static TaskHandle_t gIperf2TaskHandle = NULL;

static void FillTxBuffer(void) {
    // Counter pattern — defeats PC-side TCP coalescing optimizations and gives
    // a predictable wire stream.  Pre-filled at init.
    for (uint16_t i = 0; i < sizeof(gTxBuf); i++) {
        gTxBuf[i] = (uint8_t)(i & 0xFFU);
    }
}

static void ResetContext(void) {
    gCtx.mode = IPERF2_MODE_IDLE;
    gCtx.listen_sock = -1;
    gCtx.data_sock = -1;
    gCtx.client_connected = false;
    gCtx.pending_tx = 0;
    gCtx.start_tick = 0;
    gCtx.deadline_tick = 0;
    gCtx.duration_ms = 0;
    gCtx.bytes_transferred = 0;
    gCtx.bytes_confirmed = 0;
    memset(&gCtx.udp_remote, 0, sizeof(gCtx.udp_remote));
    gCtx.udp_id = 0;
    gCtx.udp_total_pkt = 0;
    gCtx.udp_lost_pkt = 0;
    gCtx.udp_outoforder = 0;
    gCtx.udp_fin_count = 0;
    gCtx.abort_pending = false;
    // Stale-cache cleanup: STAT? returns last_stats when gCtx.mode==IDLE.
    // last_stats.mode was set by the previous FinalizeStats to whatever
    // mode was running, which is misleading after we ResetContext —
    // user sees "Mode=2" and thinks a TCP client is still active.
    // Other last_stats fields (bytes, kbps) stay valid — they describe
    // what the last test transferred — but Mode should reflect "no
    // current operation".
    gCtx.last_stats.mode = IPERF2_MODE_IDLE;
}

static void FinalizeStats(void) {
    TickType_t elapsed = xTaskGetTickCount() - gCtx.start_tick;
    uint32_t elapsed_ms = elapsed * portTICK_PERIOD_MS;
    gCtx.last_stats.mode = gCtx.mode;
    gCtx.last_stats.bytes_transferred = gCtx.bytes_confirmed;
    gCtx.last_stats.duration_ms = elapsed_ms;
    gCtx.last_stats.kbps = (elapsed_ms > 0)
        ? (uint32_t)((gCtx.bytes_confirmed * 1000ULL) / (uint64_t)elapsed_ms / 1024ULL)
        : 0U;
    gCtx.last_stats.udp_total_pkt = gCtx.udp_total_pkt;
    gCtx.last_stats.udp_lost_pkt = gCtx.udp_lost_pkt;
    gCtx.last_stats.udp_outoforder = gCtx.udp_outoforder;
    gCtx.last_stats.active = false;
    gCtx.last_stats.completed = true;
}

// Refuse to call socket()/connect()/bind() unless the WINC driver is fully
// started.  Calling sockets against a chip in WIFI_STATE_INIT (transitional)
// leaves stuck state on the PIC32 side that survives
// WDRV_WINC_Deinitialize/Init cycles (#383).
//
// RequireWifiConnected — strict check for client modes (we need to send out).
// RequireWifiReadyForSockets — looser check for server modes; SoftAP with no
// connected station is still a valid configuration to listen on.
//
// Both also gate on m2m_wifi_get_state() == WIFI_STATE_START so we don't even
// start the socket stack mid-init (where GetWiFiStatus may briefly return
// DISCONNECTED but the WINC isn't actually up yet).
static bool RequireWifiConnected(const char* where) {
    if (wifi_manager_GetWiFiStatus() != WIFI_STATUS_CONNECTED) {
        LOG_E("iperf2: %s refused — WiFi not CONNECTED", where);
        return false;
    }
    if (m2m_wifi_get_state() != WIFI_STATE_START) {
        LOG_E("iperf2: %s refused — WINC not started", where);
        return false;
    }
    return true;
}

static bool RequireWifiReadyForSockets(const char* where) {
    if (wifi_manager_GetWiFiStatus() == WIFI_STATUS_DISABLED) {
        LOG_E("iperf2: %s refused — WiFi DISABLED", where);
        return false;
    }
    if (m2m_wifi_get_state() != WIFI_STATE_START) {
        LOG_E("iperf2: %s refused — WINC not started", where);
        return false;
    }
    return true;
}

static void CloseAll(void) {
    if (gCtx.data_sock >= 0) {
        shutdown(gCtx.data_sock);
        gCtx.data_sock = -1;
    }
    if (gCtx.listen_sock >= 0) {
        shutdown(gCtx.listen_sock);
        gCtx.listen_sock = -1;
    }
}

// ----------------------------------------------------------------------------
// Public API
// ----------------------------------------------------------------------------

void Iperf2_Initialize(void) {
    ResetContext();
    FillTxBuffer();
    memset(&gCtx.last_stats, 0, sizeof(gCtx.last_stats));
}

bool Iperf2_StartTcpServer(uint16_t port) {
    if (!RequireWifiReadyForSockets("TCP server")) return false;
    if (gCtx.mode != IPERF2_MODE_IDLE) {
        LOG_E("iperf2: already running (mode=%d)", (int)gCtx.mode);
        return false;
    }

    gCtx.listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (gCtx.listen_sock < 0) {
        LOG_E("iperf2: TCP socket() failed");
        return false;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = _htons(port);
    addr.sin_addr.s_addr = 0;  // INADDR_ANY

    int rc = bind(gCtx.listen_sock, (struct sockaddr*)&addr, sizeof(addr));
    if (rc != 0) {
        LOG_E("iperf2: TCP bind() rc=%d", rc);
        CloseAll();
        return false;
    }

    gCtx.mode = IPERF2_MODE_TCP_SERVER;
    gCtx.start_tick = xTaskGetTickCount();
    gCtx.last_stats.active = true;
    gCtx.last_stats.completed = false;
    LOG_I("iperf2: TCP server listening on port %u", (unsigned)port);
    return true;
}

bool Iperf2_StartUdpServer(uint16_t port) {
    if (!RequireWifiReadyForSockets("UDP server")) return false;
    if (gCtx.mode != IPERF2_MODE_IDLE) {
        LOG_E("iperf2: already running (mode=%d)", (int)gCtx.mode);
        return false;
    }

    gCtx.data_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (gCtx.data_sock < 0) {
        LOG_E("iperf2: UDP socket() failed");
        return false;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = _htons(port);
    addr.sin_addr.s_addr = 0;

    int rc = bind(gCtx.data_sock, (struct sockaddr*)&addr, sizeof(addr));
    if (rc != 0) {
        LOG_E("iperf2: UDP bind() rc=%d", rc);
        CloseAll();
        return false;
    }

    gCtx.mode = IPERF2_MODE_UDP_SERVER;
    gCtx.start_tick = xTaskGetTickCount();
    gCtx.last_stats.active = true;
    gCtx.last_stats.completed = false;
    LOG_I("iperf2: UDP server listening on port %u", (unsigned)port);
    return true;
}

bool Iperf2_StartTcpClient(const char* remote_ip, uint16_t remote_port,
                           uint32_t duration_sec) {
    if (!RequireWifiConnected("TCP client")) return false;
    if (gCtx.mode != IPERF2_MODE_IDLE) {
        LOG_E("iperf2: already running (mode=%d)", (int)gCtx.mode);
        return false;
    }
    if (duration_sec == 0 || duration_sec > IPERF2_MAX_DURATION_S) {
        LOG_E("iperf2: invalid duration %u s", (unsigned)duration_sec);
        return false;
    }

    gCtx.data_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (gCtx.data_sock < 0) {
        LOG_E("iperf2: TCP client socket() failed");
        return false;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = _htons(remote_port);
    addr.sin_addr.s_addr = inet_addr((char*)remote_ip);
    // WINC inet_addr returns 0 on parse failure; also reject 0xFFFFFFFFU
    // (POSIX INADDR_NONE / "255.255.255.255") — not a valid iperf target.
    if (addr.sin_addr.s_addr == 0 || addr.sin_addr.s_addr == 0xFFFFFFFFU) {
        LOG_E("iperf2: bad remote IP %s", remote_ip);
        CloseAll();
        return false;
    }

    int rc = connect(gCtx.data_sock, (struct sockaddr*)&addr, sizeof(addr));
    if (rc != 0) {
        LOG_E("iperf2: TCP connect() rc=%d", rc);
        CloseAll();
        return false;
    }

    gCtx.mode = IPERF2_MODE_TCP_CLIENT;
    gCtx.client_connected = false;
    gCtx.duration_ms = duration_sec * 1000U;
    gCtx.start_tick = xTaskGetTickCount();  // baseline for connect timeout
    gCtx.last_stats.active = true;
    gCtx.last_stats.completed = false;
    LOG_I("iperf2: TCP client connecting to %s:%u for %u s",
          remote_ip, (unsigned)remote_port, (unsigned)duration_sec);
    return true;
}

bool Iperf2_StartUdpClient(const char* remote_ip, uint16_t remote_port,
                           uint32_t duration_sec) {
    if (!RequireWifiConnected("UDP client")) return false;
    if (gCtx.mode != IPERF2_MODE_IDLE) {
        LOG_E("iperf2: already running (mode=%d)", (int)gCtx.mode);
        return false;
    }
    if (duration_sec == 0 || duration_sec > IPERF2_MAX_DURATION_S) {
        LOG_E("iperf2: invalid duration %u s", (unsigned)duration_sec);
        return false;
    }

    gCtx.data_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (gCtx.data_sock < 0) {
        LOG_E("iperf2: UDP client socket() failed");
        return false;
    }

    gCtx.udp_remote.sin_family = AF_INET;
    gCtx.udp_remote.sin_port = _htons(remote_port);
    gCtx.udp_remote.sin_addr.s_addr = inet_addr((char*)remote_ip);
    if (gCtx.udp_remote.sin_addr.s_addr == 0 ||
        gCtx.udp_remote.sin_addr.s_addr == 0xFFFFFFFFU) {
        LOG_E("iperf2: bad remote IP %s", remote_ip);
        CloseAll();
        return false;
    }

    // UDP is connectionless — start sending immediately from Iperf2_Tasks
    gCtx.mode = IPERF2_MODE_UDP_CLIENT;
    gCtx.client_connected = true;
    gCtx.duration_ms = duration_sec * 1000U;
    gCtx.start_tick = xTaskGetTickCount();
    gCtx.deadline_tick = gCtx.start_tick + pdMS_TO_TICKS(gCtx.duration_ms);
    gCtx.udp_id = 0;
    gCtx.udp_fin_count = 0;
    gCtx.last_stats.active = true;
    gCtx.last_stats.completed = false;
    LOG_I("iperf2: UDP client → %s:%u for %u s", remote_ip,
          (unsigned)remote_port, (unsigned)duration_sec);
    return true;
}

void Iperf2_Stop(void) {
    if (gCtx.mode == IPERF2_MODE_IDLE) {
        return;
    }
    LOG_I("iperf2: stop requested (mode=%d, bytes=%llu)",
          (int)gCtx.mode, (unsigned long long)gCtx.bytes_confirmed);
    FinalizeStats();
    CloseAll();
    ResetContext();
}

void Iperf2_GetStats(Iperf2_Stats* out) {
    if (out == NULL) return;
    // bytes_transferred / bytes_confirmed are 64-bit and updated by the
    // socket-event task; reading them needs a critical section so we don't
    // get torn 32-bit halves.  See PIC32MZ atomicity rules in CLAUDE.md.
    taskENTER_CRITICAL();
    if (gCtx.mode != IPERF2_MODE_IDLE && gCtx.last_stats.active) {
        // Update on-the-fly so STATS? during a run shows progress
        TickType_t elapsed = xTaskGetTickCount() - gCtx.start_tick;
        uint32_t elapsed_ms = elapsed * portTICK_PERIOD_MS;
        out->mode = gCtx.mode;
        out->bytes_transferred = gCtx.bytes_confirmed;
        out->duration_ms = elapsed_ms;
        out->kbps = (elapsed_ms > 0)
            ? (uint32_t)((gCtx.bytes_confirmed * 1000ULL) / (uint64_t)elapsed_ms / 1024ULL)
            : 0U;
        out->udp_total_pkt = gCtx.udp_total_pkt;
        out->udp_lost_pkt = gCtx.udp_lost_pkt;
        out->udp_outoforder = gCtx.udp_outoforder;
        out->active = true;
        out->completed = false;
    } else {
        *out = gCtx.last_stats;
    }
    taskEXIT_CRITICAL();
}

// ----------------------------------------------------------------------------
// Socket event dispatch
// ----------------------------------------------------------------------------

static void HandleTcpRecv(tstrSocketRecvMsg* m) {
    if (m && m->s16BufferSize > 0) {
        // 64-bit RMW + paired with USB-priority Iperf2_GetStats reader
        taskENTER_CRITICAL();
        gCtx.bytes_transferred += (uint64_t)m->s16BufferSize;
        gCtx.bytes_confirmed += (uint64_t)m->s16BufferSize;
        taskEXIT_CRITICAL();
        recv(gCtx.data_sock, gRxBuf, IPERF2_TCP_BUF_SIZE, 0);
    } else {
        // Disconnect or error.  Defer cleanup to Iperf2_Tasks() — calling
        // shutdown(sock) from inside the socket's own callback corrupts
        // shared SPI4/DMA state with USB CDC (same wedge we hit on the
        // SEND callback abort path).
        LOG_I("iperf2: TCP peer closed (size=%d), scheduling abort",
              m ? m->s16BufferSize : 0);
        gCtx.abort_pending = true;
    }
}

static void HandleUdpRecvFrom(tstrSocketRecvMsg* m) {
    if (m == NULL || m->pu8Buffer == NULL || m->s16BufferSize <= 0) {
        // Re-arm for next datagram
        if (gCtx.data_sock >= 0) {
            recvfrom(gCtx.data_sock, gRxBuf, sizeof(gRxBuf), 0);
        }
        return;
    }

    // Reject runt datagrams: a valid iperf2 UDP packet has at least the
    // 12-byte Iperf2_PktInfo header.  Without this guard, casting gRxBuf
    // to Iperf2_PktInfo* would read past the actual payload.
    if ((size_t)m->s16BufferSize < sizeof(Iperf2_PktInfo)) {
        recvfrom(gCtx.data_sock, gRxBuf, sizeof(gRxBuf), 0);
        return;
    }

    // Capture remote on first packet (where peer port comes from)
    if (gCtx.bytes_transferred == 0) {
        gCtx.udp_remote = m->strRemoteAddr;
    }

    Iperf2_PktInfo* pkt = (Iperf2_PktInfo*)gRxBuf;
    int32_t id = (int32_t)_ntohl((uint32_t)pkt->id);

    if (id >= 0) {
        // Normal packet — track sequence/loss.  RMW + 64-bit, paired with
        // USB-priority Iperf2_GetStats reader.
        taskENTER_CRITICAL();
        if (gCtx.udp_id != id) {
            // Gap or out-of-order — only count gaps as lost (forward-only count)
            if (id > gCtx.udp_id) {
                gCtx.udp_lost_pkt += (uint32_t)(id - gCtx.udp_id);
                gCtx.udp_id = id + 1;
            } else {
                gCtx.udp_outoforder++;
            }
        } else {
            gCtx.udp_id += 1;
        }
        gCtx.udp_total_pkt++;
        gCtx.bytes_transferred += (uint64_t)m->s16BufferSize;
        gCtx.bytes_confirmed += (uint64_t)m->s16BufferSize;
        taskEXIT_CRITICAL();
    } else {
        // Negative ID = end-of-test marker.  Defer cleanup to Iperf2_Tasks()
        // — same in-callback teardown safety as TCP RECV/SEND.
        LOG_I("iperf2: UDP end-of-test (id=%d, %u pkts, %u lost, %u oo), scheduling abort",
              (int)id, (unsigned)gCtx.udp_total_pkt,
              (unsigned)gCtx.udp_lost_pkt, (unsigned)gCtx.udp_outoforder);
        gCtx.abort_pending = true;
        return;
    }

    // Re-arm
    recvfrom(gCtx.data_sock, gRxBuf, sizeof(gRxBuf), 0);
}

bool Iperf2_HandleSocketEvent(SOCKET sock, uint8_t msg_type, void* pMessage) {
    if (gCtx.mode == IPERF2_MODE_IDLE) {
        return false;
    }
    // Only handle sockets we own
    if (sock != gCtx.listen_sock && sock != gCtx.data_sock) {
        return false;
    }

    switch (msg_type) {
        case SOCKET_MSG_BIND: {
            tstrSocketBindMsg* m = (tstrSocketBindMsg*)pMessage;
            if (m && m->status == 0) {
                if (gCtx.mode == IPERF2_MODE_TCP_SERVER && sock == gCtx.listen_sock) {
                    listen(gCtx.listen_sock, 0);
                } else if (gCtx.mode == IPERF2_MODE_UDP_SERVER && sock == gCtx.data_sock) {
                    // UDP: bind succeeded, start recv loop
                    recvfrom(gCtx.data_sock, gRxBuf, sizeof(gRxBuf), 0);
                }
            } else {
                // A/B verified 2026-04-28 (issue #385): in-callback Iperf2_Stop
                // gives 5/5 stable trials and 0 FinWait2 leaks; deferred-abort
                // gave 3/5 stable + 8 FinWait2 leaks across 5 trials. Stay
                // synchronous on the listener-error / connect-error paths.
                // The SEND-callback path (line 542 below) keeps deferred
                // teardown — that's still required to avoid USB CDC wedge
                // when the data socket is being torn down mid-send.
                LOG_E("iperf2: bind status=%d", m ? m->status : -1);
                Iperf2_Stop();
            }
            return true;
        }
        case SOCKET_MSG_LISTEN: {
            tstrSocketListenMsg* m = (tstrSocketListenMsg*)pMessage;
            if (m && m->status == 0 && sock == gCtx.listen_sock) {
                accept(gCtx.listen_sock, NULL, NULL);
            } else {
                LOG_E("iperf2: listen status=%d", m ? m->status : -1);
                Iperf2_Stop();
            }
            return true;
        }
        case SOCKET_MSG_ACCEPT: {
            tstrSocketAcceptMsg* m = (tstrSocketAcceptMsg*)pMessage;
            if (m && m->sock >= 0 && sock == gCtx.listen_sock) {
                if (gCtx.data_sock >= 0) {
                    shutdown(gCtx.data_sock);
                }
                gCtx.data_sock = m->sock;
                gCtx.start_tick = xTaskGetTickCount();
                gCtx.bytes_transferred = 0;
                gCtx.bytes_confirmed = 0;
                LOG_I("iperf2: TCP server accepted connection (sock=%d)",
                      (int)m->sock);
                recv(gCtx.data_sock, gRxBuf, IPERF2_TCP_BUF_SIZE, 0);
            } else {
                LOG_E("iperf2: accept failed");
                Iperf2_Stop();
            }
            return true;
        }
        case SOCKET_MSG_CONNECT: {
            tstrSocketConnectMsg* m = (tstrSocketConnectMsg*)pMessage;
            if (m && m->s8Error == 0 && sock == gCtx.data_sock) {
                gCtx.client_connected = true;
                gCtx.start_tick = xTaskGetTickCount();
                gCtx.deadline_tick = gCtx.start_tick +
                                     pdMS_TO_TICKS(gCtx.duration_ms);
                // Send iperf2 client header (24 bytes)
                Iperf2_ClientHdr hdr;
                memset(&hdr, 0, sizeof(hdr));
                hdr.flags = _htonl((uint32_t)IPERF2_HEADER_VERSION1);
                hdr.numThreads = _htonl((uint32_t)1U);
                // mPort = 0 → tell iperf2 server to NOT attempt dual-mode
                // reverse connection back to us. We don't service the
                // reverse direction (port 5001 server). Setting mPort=5001
                // (default) caused the server to connect back, fail to
                // reach us at 5001, time out at 10 s, and abort the inbound
                // session — which capped all our tests at 10 s and
                // triggered SOCK_ERR_CONN_ABORTED. See #385.
                hdr.mPort = _htonl((uint32_t)0U);
                hdr.bufferlen = _htonl((uint32_t)IPERF2_TCP_BUF_SIZE);
                hdr.mWinBand = _htonl((uint32_t)0U);
                // iperf2 mAmount is in 10ms units (negative = duration mode)
                int32_t signed_amount = -(int32_t)(gCtx.duration_ms / 10U);
                hdr.mAmount = _htonl((uint32_t)signed_amount);
                int rc = send(gCtx.data_sock, (char*)&hdr, sizeof(hdr), 0);
                if (rc == SOCK_ERR_NO_ERROR) {
                    gCtx.pending_tx = 1;
                    LOG_I("iperf2: TCP client connected, header queued");
                } else {
                    LOG_E("iperf2: header send rc=%d", rc);
                    Iperf2_Stop();
                }
            } else {
                LOG_E("iperf2: connect err=%d", m ? m->s8Error : -1);
                Iperf2_Stop();
            }
            return true;
        }
        case SOCKET_MSG_RECV: {
            HandleTcpRecv((tstrSocketRecvMsg*)pMessage);
            return true;
        }
        case SOCKET_MSG_RECVFROM: {
            HandleUdpRecvFrom((tstrSocketRecvMsg*)pMessage);
            return true;
        }
        case SOCKET_MSG_SEND:
        case SOCKET_MSG_SENDTO: {
            int16_t sent = (pMessage != NULL) ? *(int16_t*)pMessage : -1;
            // 64-bit + 8-bit RMW, paired with USB-priority reader / driver-task writer
            taskENTER_CRITICAL();
            if (sent > 0) {
                gCtx.bytes_confirmed += (uint64_t)sent;
                if (gCtx.pending_tx > 0) gCtx.pending_tx--;
            } else {
                if (gCtx.pending_tx > 0) gCtx.pending_tx--;
            }
            taskEXIT_CRITICAL();
            // Event-driven wake: when WINC frees a HIF slot, kick the
            // dedicated Iperf2 task so it refills immediately instead of
            // waiting for the next IPERF2_ACTIVE_DELAY_MS tick. Calling
            // TasksTcpClient() directly here would be unsafe — TasksTcpClient
            // calls FinalizeStats/CloseAll/ResetContext on deadline-reached,
            // and synchronous teardown from inside a WINC callback is what
            // the deferred-abort design is meant to avoid (see SEND-error
            // path comment below + #385 A/B verdict).
            if (sent > 0 && !gCtx.abort_pending && gIperf2TaskHandle != NULL) {
                xTaskNotifyGive(gIperf2TaskHandle);
            }
            // For TCP, a negative send callback (e.g. SOCK_ERR_INVALID = -9)
            // means the underlying connection is dead.  Don't tear down the
            // socket from inside its own callback — WINC's HIF event loop may
            // still be referencing it and shutdown() here can wedge USB CDC
            // (observed: serial write hangs after iperf2 test completes).
            // Set a flag instead and let Iperf2_Tasks() do the cleanup
            // outside the callback context.
            if (sent <= 0) {
                LOG_E("iperf2: send cb err=%d, scheduling abort", (int)sent);
                if (gCtx.mode == IPERF2_MODE_TCP_CLIENT) {
                    gCtx.abort_pending = true;
                }
            }
            return true;
        }
        default:
            return false;
    }
}

// ----------------------------------------------------------------------------
// Driver-task hook for *_CLIENT mode
// ----------------------------------------------------------------------------

// Time we'll wait for the WINC SOCKET_MSG_CONNECT callback before bailing.
// Connect attempts to a closed/firewalled host normally hang forever
// because TasksTcpClient early-returns until client_connected is set.
#define IPERF2_CONNECT_TIMEOUT_MS  10000U

static void TasksTcpClient(void) {
    if (!gCtx.client_connected) {
        // Bail if connect has been pending too long.  start_tick is set in
        // Iperf2_StartTcpClient() before connect(), updated again in the
        // CONNECT callback if it succeeds.
        TickType_t elapsed = xTaskGetTickCount() - gCtx.start_tick;
        if (elapsed > pdMS_TO_TICKS(IPERF2_CONNECT_TIMEOUT_MS)) {
            LOG_E("iperf2: TCP connect timeout (%u ms), aborting",
                  (unsigned)(elapsed * portTICK_PERIOD_MS));
            FinalizeStats();
            CloseAll();
            ResetContext();
        }
        return;
    }

    if ((int32_t)(xTaskGetTickCount() - gCtx.deadline_tick) >= 0) {
        // Deadline reached — finalize unconditionally.  Don't gate on
        // pending_tx==0 because the previous design could deadlock if a
        // send-callback was lost.
        LOG_I("iperf2: TCP client TX done, %llu bytes sent",
              (unsigned long long)gCtx.bytes_confirmed);
        FinalizeStats();
        CloseAll();
        ResetContext();
        return;
    }

    // Drain WINC HIF up to IPERF2_MAX_PENDING_TX (4, matching WINC chip's
    // internal HIF queue depth — same cap as wifi_tcp_server's
    // WIFI_TCP_MAX_IN_FLIGHT). Going over this risks pushing past WINC's
    // accept-without-error threshold even before BUFFER_FULL fires, which
    // can corrupt internal state.
    while (gCtx.pending_tx < IPERF2_MAX_PENDING_TX) {
        int rc = send(gCtx.data_sock, (char*)gTxBuf, IPERF2_TCP_BUF_SIZE, 0);
        if (rc == SOCK_ERR_NO_ERROR) {
            taskENTER_CRITICAL();
            gCtx.bytes_transferred += IPERF2_TCP_BUF_SIZE;
            gCtx.pending_tx++;
            taskEXIT_CRITICAL();
        } else if (rc == SOCK_ERR_BUFFER_FULL) {
            break;  // WINC HIF full — let WINC drain, retry next tick.
        } else {
            LOG_E("iperf2: TCP send err rc=%d, aborting", rc);
            gCtx.abort_pending = true;
            break;
        }
    }
}

static void TasksUdpClient(void) {
    if (!gCtx.client_connected) return;

    Iperf2_PktInfo* pkt = (Iperf2_PktInfo*)gTxBuf;
    bool past_deadline = ((int32_t)(xTaskGetTickCount() - gCtx.deadline_tick) >= 0);

    if (!past_deadline) {
        // Drain UDP up to IPERF2_MAX_PENDING_TX (matches WINC HIF depth).
        while (gCtx.pending_tx < IPERF2_MAX_PENDING_TX) {
            pkt->id = (int32_t)_htonl((uint32_t)gCtx.udp_id);
            pkt->tv_sec = 0;
            pkt->tv_usec = 0;
            int rc = sendto(gCtx.data_sock, (char*)gTxBuf, IPERF2_UDP_BUF_SIZE, 0,
                            (struct sockaddr*)&gCtx.udp_remote,
                            sizeof(gCtx.udp_remote));
            if (rc == SOCK_ERR_NO_ERROR) {
                taskENTER_CRITICAL();
                gCtx.bytes_transferred += IPERF2_UDP_BUF_SIZE;
                gCtx.pending_tx++;
                gCtx.udp_id++;
                taskEXIT_CRITICAL();
            } else if (rc == SOCK_ERR_BUFFER_FULL) {
                break;
            } else {
                LOG_E("iperf2: UDP sendto err rc=%d, aborting", rc);
                gCtx.abort_pending = true;
                return;
            }
        }
    } else {
        // FIN phase — send final packet with negated ID 10× to ensure server
        // sees end-of-test.  Once done, finalize.
        if (gCtx.udp_fin_count >= IPERF2_FIN_RETRANSMITS) {
            LOG_I("iperf2: UDP client TX done, %u pkts sent",
                  (unsigned)gCtx.udp_id);
            FinalizeStats();
            CloseAll();
            ResetContext();
            return;
        }
        if (gCtx.pending_tx < IPERF2_MAX_PENDING_TX) {
            pkt->id = (int32_t)_htonl((uint32_t)(-gCtx.udp_id));
            pkt->tv_sec = 0;
            pkt->tv_usec = 0;
            int rc = sendto(gCtx.data_sock, (char*)gTxBuf, IPERF2_UDP_BUF_SIZE, 0,
                            (struct sockaddr*)&gCtx.udp_remote,
                            sizeof(gCtx.udp_remote));
            if (rc == SOCK_ERR_NO_ERROR) {
                // 8-bit RMW; same critical-section pattern as the TX-phase
                // path above for consistency.
                taskENTER_CRITICAL();
                gCtx.pending_tx++;
                gCtx.udp_fin_count++;
                taskEXIT_CRITICAL();
            }
        }
    }
}

// Task body — runs Iperf2_Tasks then sleeps until either notified by a
// SOCKET_MSG_SEND callback (instant wake on HIF slot completion) or the
// fallback timeout fires (IPERF2_ACTIVE_DELAY_MS in client mode, 50 ms
// idle).  See the define for the empirical sweep that picked 2 ms.
static void Iperf2TaskMain(void* arg) {
    (void)arg;
    for (;;) {
        Iperf2_Tasks();
        bool active = (gCtx.mode == IPERF2_MODE_TCP_CLIENT) ||
                      (gCtx.mode == IPERF2_MODE_UDP_CLIENT);
        ulTaskNotifyTake(pdTRUE,
                         pdMS_TO_TICKS(active ? IPERF2_ACTIVE_DELAY_MS : 50U));
    }
}

void Iperf2_StartTask(void) {
    if (gIperf2TaskHandle != NULL) {
        return;  // already created — re-using static taskTcb/taskStack would corrupt
    }
    static StaticTask_t taskTcb;
    static StackType_t  taskStack[512];
    gIperf2TaskHandle = xTaskCreateStatic(Iperf2TaskMain, "Iperf2",
                                          (uint32_t)(sizeof(taskStack) / sizeof(taskStack[0])),
                                          NULL, 5, taskStack, &taskTcb);
    if (gIperf2TaskHandle == NULL) {
        LOG_E("iperf2: failed to create dedicated task");
    }
}

void Iperf2_Tasks(void) {
    // Process any deferred abort from a SOCKET_MSG_SEND callback first.  We
    // can't tear down the socket from inside the WINC's event handler — do
    // it here from app_WifiTask context, after the event loop returns.
    if (gCtx.abort_pending) {
        LOG_I("iperf2: deferred abort (mode=%d, %llu bytes)",
              (int)gCtx.mode, (unsigned long long)gCtx.bytes_confirmed);
        FinalizeStats();
        CloseAll();
        ResetContext();
        return;
    }

    switch (gCtx.mode) {
        case IPERF2_MODE_TCP_CLIENT: TasksTcpClient(); break;
        case IPERF2_MODE_UDP_CLIENT: TasksUdpClient(); break;
        default: break;  // IDLE / *_SERVER are pure callback-driven
    }
}
