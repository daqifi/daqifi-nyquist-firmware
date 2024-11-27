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
#define WIFI_WBUFFER_SIZE ((SOCKET_BUFFER_MAX_LENGTH/2)-1)
#define WIFI_CIRCULAR_BUFF_SIZE SOCKET_BUFFER_MAX_LENGTH*4

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


    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _WIFI_TCP_SERVER_H */

/* *****************************************************************************
 End of File
 */
