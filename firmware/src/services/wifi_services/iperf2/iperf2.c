#define LOG_LVL LOG_LEVEL_WIFI
#define LOG_MODULE LOG_MODULE_WIFI

#include "iperf2.h"
#include "Util/Logger.h"
#include "FreeRTOS.h"
#include "task.h"
#include "socket.h"
#include "m2m_wifi.h"
#include <string.h>

// ============================================================================
// iperf2 — minimal TCP iperf 2 protocol over WINC m2m sockets, control via
// SCPI.  Server mode: listen, accept, recv-and-count-bytes until disconnect,
// emit summary.  Client mode: connect, send tClientHdr, then 1400-byte
// chunks for `duration_sec`, close.
// ============================================================================

#define IPERF2_TXFER_SIZE       1400U   // matches WINC SOCKET_BUFFER_MAX_LENGTH
#define IPERF2_RXFER_SIZE       1400U
#define IPERF2_MAX_DURATION_S   3600U   // 1h cap
#define IPERF2_MAX_PENDING_TX   2U      // WINC HIF queue cap (mirror tcp_server)

typedef struct {
    Iperf2_Mode    mode;
    SOCKET         listen_sock;     // server only
    SOCKET         data_sock;       // active TCP connection (server: accepted; client: connected)
    bool           server_running;
    bool           client_connect_pending;
    bool           client_connected;
    bool           tx_in_flight;    // single outstanding send slot for client
    uint8_t        pending_tx;      // count of m2m_send not yet ACKed
    TickType_t     start_tick;
    TickType_t     deadline_tick;   // client mode TX deadline
    uint32_t       duration_ms;     // requested duration
    uint64_t       bytes_transferred;
    uint64_t       bytes_confirmed; // server: rcv'd; client: ACKed by m2m_send callback
    Iperf2_Stats   last_stats;
} Iperf2_Context;

static Iperf2_Context gCtx;

// Static TX/RX buffers — keep off task stacks.  TX buffer carries a synthetic
// counter pattern so PC-side iperf can't compress; RX buffer is just a sink.
static uint8_t gTxBuf[IPERF2_TXFER_SIZE];
static uint8_t gRxBuf[IPERF2_RXFER_SIZE];

static void FillTxBuffer(void) {
    // Counter pattern — one byte per index — defeats PC-side TCP coalescing
    // optimizations and gives a predictable wire stream.  Pre-filled at init.
    for (uint16_t i = 0; i < IPERF2_TXFER_SIZE; i++) {
        gTxBuf[i] = (uint8_t)(i & 0xFFU);
    }
}

static void ResetContext(void) {
    gCtx.mode = IPERF2_MODE_IDLE;
    gCtx.listen_sock = -1;
    gCtx.data_sock = -1;
    gCtx.server_running = false;
    gCtx.client_connect_pending = false;
    gCtx.client_connected = false;
    gCtx.tx_in_flight = false;
    gCtx.pending_tx = 0;
    gCtx.start_tick = 0;
    gCtx.deadline_tick = 0;
    gCtx.duration_ms = 0;
    gCtx.bytes_transferred = 0;
    gCtx.bytes_confirmed = 0;
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
    gCtx.last_stats.active = false;
    gCtx.last_stats.completed = true;
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

void Iperf2_Initialize(void) {
    ResetContext();
    FillTxBuffer();
    memset(&gCtx.last_stats, 0, sizeof(gCtx.last_stats));
}

bool Iperf2_StartServer(uint16_t port) {
    if (gCtx.mode != IPERF2_MODE_IDLE) {
        LOG_E("iperf2: already running (mode=%d)", (int)gCtx.mode);
        return false;
    }

    gCtx.listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (gCtx.listen_sock < 0) {
        LOG_E("iperf2: socket() failed");
        return false;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = _htons(port);
    addr.sin_addr.s_addr = 0;  // INADDR_ANY

    int rc = bind(gCtx.listen_sock, (struct sockaddr*)&addr, sizeof(addr));
    if (rc != 0) {
        LOG_E("iperf2: bind() rc=%d", rc);
        CloseAll();
        return false;
    }

    gCtx.mode = IPERF2_MODE_SERVER;
    gCtx.server_running = true;
    gCtx.start_tick = xTaskGetTickCount();
    gCtx.bytes_transferred = 0;
    gCtx.bytes_confirmed = 0;
    gCtx.last_stats.active = true;
    gCtx.last_stats.completed = false;
    LOG_I("iperf2: TCP server listening on port %u", (unsigned)port);
    return true;
}

bool Iperf2_StartClient(const char* remote_ip, uint16_t remote_port,
                        uint32_t duration_sec) {
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
        LOG_E("iperf2: client socket() failed");
        return false;
    }

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = _htons(remote_port);
    addr.sin_addr.s_addr = inet_addr((char*)remote_ip);
    if (addr.sin_addr.s_addr == 0) {
        LOG_E("iperf2: bad remote IP %s", remote_ip);
        CloseAll();
        return false;
    }

    int rc = connect(gCtx.data_sock, (struct sockaddr*)&addr, sizeof(addr));
    if (rc != 0) {
        LOG_E("iperf2: connect() rc=%d", rc);
        CloseAll();
        return false;
    }

    gCtx.mode = IPERF2_MODE_CLIENT;
    gCtx.client_connect_pending = true;
    gCtx.client_connected = false;
    gCtx.duration_ms = duration_sec * 1000U;
    gCtx.bytes_transferred = 0;
    gCtx.bytes_confirmed = 0;
    gCtx.last_stats.active = true;
    gCtx.last_stats.completed = false;
    LOG_I("iperf2: TCP client connecting to %s:%u for %u s",
          remote_ip, (unsigned)remote_port, (unsigned)duration_sec);
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
        out->active = true;
        out->completed = false;
    } else {
        *out = gCtx.last_stats;
    }
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
            if (m && m->status == 0 && sock == gCtx.listen_sock) {
                listen(gCtx.listen_sock, 0);
            } else {
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
                LOG_I("iperf2: server accepted connection (sock=%d)",
                      (int)m->sock);
                recv(gCtx.data_sock, gRxBuf, IPERF2_RXFER_SIZE, 0);
            } else {
                LOG_E("iperf2: accept failed");
                Iperf2_Stop();
            }
            return true;
        }
        case SOCKET_MSG_CONNECT: {
            tstrSocketConnectMsg* m = (tstrSocketConnectMsg*)pMessage;
            if (m && m->s8Error == 0 && sock == gCtx.data_sock) {
                gCtx.client_connect_pending = false;
                gCtx.client_connected = true;
                gCtx.start_tick = xTaskGetTickCount();
                gCtx.deadline_tick = gCtx.start_tick +
                                     pdMS_TO_TICKS(gCtx.duration_ms);
                // Send client header first per iperf2 protocol
                Iperf2_ClientHdr hdr;
                memset(&hdr, 0, sizeof(hdr));
                hdr.flags = _htonl((uint32_t)IPERF2_HEADER_VERSION1);
                hdr.numThreads = _htonl((uint32_t)1U);
                hdr.mPort = _htonl((uint32_t)IPERF2_DEFAULT_PORT);
                hdr.bufferlen = _htonl((uint32_t)IPERF2_TXFER_SIZE);
                hdr.mWinBand = _htonl((uint32_t)0U);
                // negative mAmount = duration in -100us units (iperf2 quirk)
                int32_t signed_amount = -(int32_t)(gCtx.duration_ms * 10U);
                hdr.mAmount = _htonl((uint32_t)signed_amount);
                int rc = send(gCtx.data_sock, (char*)&hdr, sizeof(hdr), 0);
                if (rc == SOCK_ERR_NO_ERROR) {
                    gCtx.pending_tx = 1;
                    LOG_I("iperf2: client connected, header queued");
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
            tstrSocketRecvMsg* m = (tstrSocketRecvMsg*)pMessage;
            if (m && m->s16BufferSize > 0) {
                gCtx.bytes_transferred += (uint64_t)m->s16BufferSize;
                gCtx.bytes_confirmed += (uint64_t)m->s16BufferSize;
                // Re-arm recv to keep the receive pump going
                recv(gCtx.data_sock, gRxBuf, IPERF2_RXFER_SIZE, 0);
            } else {
                // Disconnect or error → finalize
                LOG_I("iperf2: peer closed (size=%d), finalizing",
                      m ? m->s16BufferSize : 0);
                FinalizeStats();
                CloseAll();
                ResetContext();
            }
            return true;
        }
        case SOCKET_MSG_SEND: {
            // Returns int16_t = bytes sent (negative = error).  Decrement
            // pending_tx so Iperf2_Tasks knows it can queue another chunk.
            int16_t sent = (pMessage != NULL) ? *(int16_t*)pMessage : -1;
            if (sent > 0) {
                gCtx.bytes_confirmed += (uint64_t)sent;
                if (gCtx.pending_tx > 0) gCtx.pending_tx--;
            } else {
                LOG_E("iperf2: send cb err=%d", (int)sent);
                if (gCtx.pending_tx > 0) gCtx.pending_tx--;
            }
            return true;
        }
        default:
            return false;
    }
}

void Iperf2_Tasks(void) {
    if (gCtx.mode != IPERF2_MODE_CLIENT) return;
    if (!gCtx.client_connected) return;

    // Deadline check: stop sending once duration expires + drain in-flight
    if ((int32_t)(xTaskGetTickCount() - gCtx.deadline_tick) >= 0) {
        if (gCtx.pending_tx == 0) {
            LOG_I("iperf2: client TX done, %llu bytes sent",
                  (unsigned long long)gCtx.bytes_confirmed);
            FinalizeStats();
            CloseAll();
            ResetContext();
        }
        return;
    }

    // Top up the in-flight cap: queue until WINC HIF is full
    while (gCtx.pending_tx < IPERF2_MAX_PENDING_TX) {
        int rc = send(gCtx.data_sock, (char*)gTxBuf, IPERF2_TXFER_SIZE, 0);
        if (rc == SOCK_ERR_NO_ERROR) {
            gCtx.bytes_transferred += IPERF2_TXFER_SIZE;
            gCtx.pending_tx++;
        } else if (rc == SOCK_ERR_BUFFER_FULL) {
            // WINC out of staging — back off until callback fires
            break;
        } else {
            LOG_E("iperf2: send err rc=%d, aborting", rc);
            FinalizeStats();
            CloseAll();
            ResetContext();
            return;
        }
    }
}
