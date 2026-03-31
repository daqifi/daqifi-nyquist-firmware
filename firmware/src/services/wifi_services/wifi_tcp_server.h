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
    
    bool tcpSendPending;

    /** Bytes handed to radio send() successfully (64-bit for long sessions) */
    uint64_t wifiTcpBytesSent;
    /** Bytes confirmed by radio send callback (64-bit for long sessions) */
    uint64_t wifiTcpBytesConfirmed;
    /** Radio send errors (negative sentBytes in callback — real failures) */
    uint32_t wifiTcpSendErrors;
    /** Partial sends (callback confirmed fewer bytes than requested — normal TCP segmentation) */
    uint32_t wifiTcpPartialSends;
    /** Pending send size (to compare against callback confirmation).
     *  Written by streaming task (TcpServerFlush), read by WiFi task (callback). */
    volatile uint16_t lastSendSize;
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

    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _WIFI_TCP_SERVER_H */

/* *****************************************************************************
 End of File
 */
