#ifndef _WIFI_TCP_SERVER_H    /* Guard against multiple inclusion */
#define _WIFI_TCP_SERVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "configuration.h"
#include "definitions.h"
#include "libraries/microrl/src/microrl.h"
#include "libraries/scpi/libscpi/inc/scpi/scpi.h"
#include "Util/CircularBuffer.h"
#include "wdrv_winc_client_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_MAX_CLIENT 1
#define WIFI_RBUFFER_SIZE ((SOCKET_BUFFER_MAX_LENGTH/2)-1)
#define WIFI_WBUFFER_SIZE SOCKET_BUFFER_MAX_LENGTH  // Use full WINC1500 buffer capacity (1400 bytes)
#define WIFI_CIRCULAR_BUFF_SIZE SOCKET_BUFFER_MAX_LENGTH*10

// Max simultaneous m2m_send packets queued at WINC.  hif_send is synchronous
// (writes to WINC SPI before returning), so our local writeBuffer is free
// right after send() returns — the only constraint is WINC's internal HIF
// queue depth.  Microchip's reference iperf uses 2 for UDP TX queue.
// Empirically N=3 lifts 1-channel WiFi PB ceiling from ~14 kHz → ~17 kHz on
// Tesla AP (no further gain at N=4); the previous N=2 was the WiFi
// pipelining bottleneck.  Multi-channel ceilings (8ch+) are unchanged
// because they're sample-queue-limited upstream of WiFi.
#define WIFI_TCP_MAX_IN_FLIGHT 4

/**
 * Data for a particular TCP client
 */
typedef struct s_tcpClientContext
{
    SOCKET clientSocket;
    /** Client read buffer */
    uint8_t readBuffer[WIFI_RBUFFER_SIZE+1];

    /** The current length of the read buffer */
    size_t readBufferLength;

    /** Client write buffer */
    uint8_t writeBuffer[WIFI_WBUFFER_SIZE+1];

    /** The current length of the write buffer */
    size_t writeBufferLength;

    CircularBuf_t wCirbuf;
    SemaphoreHandle_t wMutex;
    
    /** The Microrl console */
    microrl_t console;

    /** The associated SCPI context */
    scpi_t scpiContext;
    
    /** Count of m2m_send calls queued at WINC, decremented by SOCKET_MSG_SEND
     *  callback.  Caps at WIFI_TCP_MAX_IN_FLIGHT.  Replaces the prior
     *  `bool tcpSendPending` (which capped at 1).  Updated under
     *  taskENTER_CRITICAL — concurrent writers are streaming_Task (priority 6)
     *  and WDRV_WINC_Tasks (DRV_WIFI_WINC_RTOS_TASK_PRIORITY, currently 1 per
     *  PR #492 — formerly 2).  Reference the macro rather than hard-coding to
     *  avoid stale comment drift if priority changes again. */
    volatile uint8_t tcpInFlight;

    /** #437: pending CircularBuf_Reset deferred from a context that
     *  couldn't acquire wMutex (notably WINC driver task running
     *  CloseClientSocket from SocketEventCallback).  Drained by the
     *  next wMutex holder before its operation, ensuring the reset
     *  happens under lock without ever blocking the WINC driver task. */
    volatile bool pendingBufferReset;

    /** Bytes handed to radio send() successfully (64-bit for long sessions) */
    uint64_t wifiTcpBytesSent;
    /** Bytes confirmed by radio send callback (64-bit for long sessions) */
    uint64_t wifiTcpBytesConfirmed;
    /** Radio send errors (negative sentBytes in callback — real failures) */
    uint32_t wifiTcpSendErrors;
    /** Partial sends (callback confirmed fewer bytes than requested — normal TCP segmentation) */
    uint32_t wifiTcpPartialSends;
    /** #367 diagnostics: cumulative byte shortfall (sendSize - sentBytes) across all partial sends */
    uint32_t wifiPartialBytesMissing;
    /** #371 diagnostics: count of wifi_tcp_server_WriteBuffer calls that returned 0
     *  because the circular buffer didn't have enough free space.  Streaming task
     *  charges the same packet to wifiDroppedBytes; if these don't match the
     *  per-call mismatch is a bug in the silent-loss path. */
    uint32_t wifiWriteBufferRejectedCalls;
    uint32_t wifiWriteBufferRejectedBytes;
    /** FIFO ring of in-flight send sizes — one slot per concurrent send.
     *  Producer (TcpServerFlush) writes at inflightHead, consumer (SOCKET_MSG_SEND
     *  callback) reads at inflightTail.  Both indices wrap mod WIFI_TCP_MAX_IN_FLIGHT.
     *  Replaces the prior scalar `lastSendSize` which raced under N>1 concurrent sends. */
    volatile uint16_t inflightSizes[WIFI_TCP_MAX_IN_FLIGHT];
    volatile uint8_t inflightHead;
    volatile uint8_t inflightTail;

    /** #599: monotonically-increasing accepted-connection counter.  Bumped
     *  once per SOCKET_MSG_ACCEPT that installs a client (single writer:
     *  SocketEventCallback on the WINC driver task — same pattern as
     *  acceptRefused/acceptFails, so a plain ++ is safe; the 32-bit read is
     *  atomic).  Lets an in-flight async SD GET/LIST reply verify it still
     *  targets the connection that issued it, so a later client inheriting
     *  the single slot can never receive it. */
    volatile uint32_t connGeneration;
} wifi_tcp_server_clientContext_t;

/**
 * Tracks TCP Server Data
 */
typedef struct s_tcpServerContext
{
    //TcpServerState state;

    SOCKET serverSocket;

    wifi_tcp_server_clientContext_t client;

    /* #560/#475 listener-health observability (Opt 0).  uint32_t counters
     * surfaced in SYST:STReam:STATS? (read there under taskENTER_CRITICAL).
     * They PERSIST across streaming sessions (deliberately NOT reset at stream
     * start) so the slow PATH-1 listen-slot leak stays visible cross-session;
     * zeroed at boot (wifi_manager_BootInit memset) and on operator STATS:CLEar.
     *
     * Writer model / atomicity (PIC32MZ):
     *  - socketOpenFails is written from TWO tasks: wifi_tcp_server_OpenSocket()
     *    on app_WifiTask (pri 2) AND SocketEventCallback()'s SOCKET_MSG_BIND
     *    handler on the WINC driver task lWDRV_WINC_Tasks (pri 1).  Cross-task
     *    RMW → guarded by taskENTER_CRITICAL at every write site (matches the
     *    team pattern in #223/#451).
     *  - listenFails / acceptFails / acceptRefused are written ONLY from
     *    SocketEventCallback (single task) — plain ++ is safe; the 32-bit read
     *    is atomic and the SCPI reader's critical section keeps the snapshot
     *    coherent.
     *  - clientForceClosed / listenReopens / listenHardResets: self-heal action
     *    counts, single-writer (0 until Opt 1/2/3 land). */
    uint32_t socketOpenFails;   /* socket()/bind() HIF-send failure in OpenSocket — nonzero = WINC TCP-table exhaustion (the H2 smoking gun). CROSS-TASK: guard with taskENTER_CRITICAL */
    uint32_t listenFails;       /* SOCKET_MSG_LISTEN reported status != 0 (single-writer: SocketEventCallback) */
    uint32_t acceptFails;       /* SOCKET_MSG_ACCEPT arrived with a NULL message (single-writer: SocketEventCallback) */
    uint32_t acceptRefused;     /* one-client policy refused a 2nd connect — climbing = PATH-2 zombie churn (single-writer: SocketEventCallback) */
    uint32_t clientForceClosed; /* self-heal: dead client force-closed (0 until Opt 1) */
    uint32_t listenReopens;     /* self-heal: host re-listen count (0 until Opt 2) */
    uint32_t listenHardResets;  /* self-heal: WINC HardReset escalations (0 until Opt 3) */
} wifi_tcp_server_context_t;

/**
 * Resize the WiFi TCP circular write buffer.
 * Must only be called when streaming is stopped (no data in flight).
 * @param newSize New buffer size in bytes (minimum WIFI_WBUFFER_SIZE)
 * @return true if resized, false if failed or same size
 */
bool wifi_tcp_server_ResizeWriteBuffer(uint32_t newSize);

/** Queue bytes to the TCP client write buffer (drained by WifiTask).
 *  Returns bytes accepted (0 = buffer full or no client - see #371 counters). */
size_t wifi_tcp_server_WriteBuffer(const char* data, size_t len);

/** #598: true when the given SCPI context is the WiFi TCP console's. */
bool wifi_tcp_server_ContextIsTcp(const scpi_t* context);

/** #599: current accepted-connection generation (0 if no client has ever
 *  been accepted).  Captured at SD GET/LIST time to bind an async reply. */
uint32_t wifi_tcp_server_GetConnGeneration(void);

/** #599: true only if a client is currently connected AND its generation
 *  equals `generation` — i.e. the originating connection still owns the
 *  single TCP slot.  False after that client disconnects, or once a
 *  different client has taken the slot.  Backpressure (buffer full while the
 *  same client stays connected) does NOT flip this false. */
bool wifi_tcp_server_ConnIsCurrent(uint32_t generation);

/**
 * Swap the WiFi TCP circular write buffer to pool-managed memory.
 * Resets the buffer (discards pending data). Called by StreamingBufferPool
 * after partitioning.
 * @param buf Pointer to buffer memory (must remain valid)
 * @param size Buffer size in bytes
 */
void wifi_tcp_server_SetWriteBuffer(uint8_t* buf, uint32_t size);

/**
 * True when a TCP client is currently connected (socket open).
 * Used by the WINC idle-gate (#331) to skip pacing when the control
 * plane is in use.
 */
bool wifi_tcp_server_HasActiveClient(void);

/**
 * Returns the current count of bytes sitting in the WiFi TCP write
 * circular buffer (queued for send() but not yet drained).
 * Used by Streaming_Stop to capture session-end "tail" bytes for the
 * #367 accounting reconciliation. Returns 0 if not initialized.
 */
uint32_t wifi_tcp_server_GetCircularBufferAvailable(void);

    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _WIFI_TCP_SERVER_H */

/* *****************************************************************************
 End of File
 */
