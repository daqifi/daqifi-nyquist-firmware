#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include "system_config.h"
#include "system_definitions.h"

#include "SCPI/SCPIInterface.h"
#include "microrl.h"
#include "Util/CircularBuffer.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define WIFI_MAX_CLIENT 1 //MAX_BSD_SOCKETS - 1
#define WIFI_BUFFER_SIZE 2048

/**
 * Tracks the client state
 */
typedef enum e_TcpClientState
{
    IP_CLIENT_CONNECT,
    IP_CLIENT_PROCESS,
    IP_CLIENT_DISCONNECT
} TcpClientState;

/**
 * Data for a particular TCP client
 */
typedef struct s_TcpClientData
{
    /** The client socket associated with this client */
    TCP_SOCKET socket;
    bool       sock_was_connected;
    uint32_t   reconnectTick;

    /** Client read buffer */
    uint8_t readBuffer[WIFI_BUFFER_SIZE];

    /** The current length of the read buffer */
    size_t readBufferLength;

    /** Client write buffer */
    uint8_t writeBuffer[WIFI_BUFFER_SIZE];

    /** The current length of the write buffer */
    size_t writeBufferLength;

    /** The Microrl console */
    microrl_t console;

    /** The associated SCPI context */
    scpi_t scpiContext;
} TcpClientData;

/**
 * Tracks the server state
 */
typedef enum e_TcpServerState
{
    IP_SERVER_INITIALIZE,
    IP_SERVER_WAIT,
//    IP_SERVER_CONNECT,
//    IP_SERVER_BIND,
//    IP_SERVER_LISTEN,
//    IP_SERVER_PROCESS,
//    IP_SERVER_DISCONNECT,
            
    IP_SERVER_OPENING_SERVER,
    //IP_SERVER_WAIT_FOR_CONNECTION,
    IP_SERVER_SERVING_CONNECTION,
    IP_SERVER_CLOSING_CONNECTION,
} TcpServerState;

/**
 * Tracks TCP Server Data
 */
typedef struct s_TcpServerData
{
    TcpServerState state;

    SOCKET serverSocket;

    TCPIP_NET_HANDLE hInterface;

    TcpClientData clients[WIFI_MAX_CLIENT];
    //TCP_SOCKET    socket[WIFI_MAX_CLIENT];
    //bool          socketWasConnected[WIFI_MAX_CLIENT];
    
    CircularBuf   streamCirbuf;
    //SemaphoreHandle_t wMutex;
} TcpServerData;


#ifdef	__cplusplus
}
#endif


