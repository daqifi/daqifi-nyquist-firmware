/* ************************************************************************** */
/** Descriptive File Name

  @Company
    Company Name

  @File Name
    filename.h

  @Summary
    Brief description of the file.

  @Description
    Describe the purpose of this file.
 */
/* ************************************************************************** */

#ifndef _TCP_SERVER_H    /* Guard against multiple inclusion */
#define _TCP_SERVER_H

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
#define WIFI_RBUFFER_SIZE SOCKET_BUFFER_MAX_LENGTH
#define WIFI_WBUFFER_SIZE SOCKET_BUFFER_MAX_LENGTH
#define WIFI_CIRCULAR_BUFF_SIZE SOCKET_BUFFER_MAX_LENGTH*4
/**
 * Tracks the client state
 */
//typedef enum e_TcpClientState
//{
//    IP_CLIENT_CONNECT,
//    IP_CLIENT_PROCESS,
//    IP_CLIENT_DISCONNECT
//} TcpClientState;
//typedef enum e_TcpServerState
//{
//    IP_SERVER_INITIALIZE,
//    IP_SERVER_CONNECT,
//    IP_SERVER_BIND,
//    IP_SERVER_LISTEN,
//    IP_SERVER_PROCESS,
//    IP_SERVER_DISCONNECT,
//} TcpServerState;
/**
 * Data for a particular TCP client
 */
typedef struct s_TcpClientData
{
    SOCKET clientSocket;
    /** Client read buffer */
    uint8_t readBuffer[WIFI_RBUFFER_SIZE];

    /** The current length of the read buffer */
    size_t readBufferLength;

    /** Client write buffer */
    uint8_t writeBuffer[WIFI_WBUFFER_SIZE];

    /** The current length of the write buffer */
    size_t writeBufferLength;

    CircularBuf_t wCirbuf;
    SemaphoreHandle_t wMutex;
    
    /** The Microrl console */
    microrl_t console;

    /** The associated SCPI context */
    scpi_t scpiContext;
    
    bool tcpSendPending;
} TcpClientData;

/**
 * Tracks TCP Server Data
 */
typedef struct s_TcpServerData
{
    //TcpServerState state;

    SOCKET serverSocket;

    TcpClientData client;
} TcpServerData;

    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _TCP_SERVER_H */

/* *****************************************************************************
 End of File
 */
