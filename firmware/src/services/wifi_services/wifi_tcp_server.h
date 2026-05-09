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
     *  and WDRV_WINC_Tasks (priority 2). */
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
} wifi_tcp_server_clientContext_t;

/**
 * Tracks TCP Server Data
 */
typedef struct s_tcpServerContext
{
    //TcpServerState state;

    SOCKET serverSocket;

    wifi_tcp_server_clientContext_t client;
} wifi_tcp_server_context_t;

/**
 * Resize the WiFi TCP circular write buffer.
 * Must only be called when streaming is stopped (no data in flight).
 * @param newSize New buffer size in bytes (minimum WIFI_WBUFFER_SIZE)
 * @return true if resized, false if failed or same size
 */
bool wifi_tcp_server_ResizeWriteBuffer(uint32_t newSize);

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
