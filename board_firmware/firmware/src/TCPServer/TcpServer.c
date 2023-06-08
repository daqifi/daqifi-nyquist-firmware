#include "TcpServer.h"

// System
extern __attribute__((section(".bss.errno"))) int errno;
#include <sys/errno.h>
#include <string.h>
#include "tcpip/tcpip.h"

// 3rd party
#include "scpi/scpi.h"

// App
#include "SCPI/SCPIInterface.h"
#include "state/data/BoardData.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "Util/Logger.h"

#if defined(TCPIP_IF_MRF24WN) /* Wi-Fi Interface */
#define WIFI_INTERFACE_NAME "MRF24WN"
#elif defined(TCPIP_IF_WINC1500)
#define WIFI_INTERFACE_NAME "WINC1500"
#endif

//! Timeout for waiting when WiFi device is full and returning EWOULDBLOCK error
#define TCPSERVER_EWOULDBLOCK_ERROR_TIMEOUT         1

#define UNUSED(x) (void)(x)

static uint8_t tcpServerBlocked = 0;

// Function Prototypes
static void TcpServer_InitializeClient(TcpClientData* client);
static bool TcpServer_Flush(TcpClientData* client);
static size_t TcpServer_Write(TcpClientData* client, const char* data, size_t len);

/**
 * Writes data to the output buffere
 * @param client The client to write to
 * @param data The data to write
 * @param len The length of the data
 * @return The number of characters written
 */
static size_t TcpServer_Write(TcpClientData* client, const char* data, size_t len)
{
    if (client == NULL)
    {
        SYS_DEBUG_MESSAGE(SYS_ERROR_ERROR, "Could not find client for provided context");
        return 0;
    }
    
    size_t startIndex = 0;
    while (startIndex < len)
    {
        size_t remainder = WIFI_WBUFFER_SIZE - client->writeBufferLength - 1;
        size_t size = min(remainder, len - startIndex);

        if (data != NULL && size > 0)
        {
            uint8_t* start = client->writeBuffer + client->writeBufferLength;
            size_t i;
            for (i=0; i<size; ++i)
            {
                start[i] = data[startIndex + i];
            }

            client->writeBufferLength += size;
            client->writeBuffer[client->writeBufferLength] = '\0';
        }

        startIndex += size;
        if (startIndex < len)
        {
            if (!TcpServer_Flush(client))
            {
                SYS_DEBUG_PRINT(SYS_ERROR_ERROR, "String exceeds available length. Truncating. Actual=%d, Required=%d", size, len);
                break;
            }
        }
    }
    
    return startIndex;
}

/**
 * Flushes data from the provided client
 * @param client The client to flush
 * @return  True if data is flushed, false otherwise
 */
static bool TcpServer_Flush(TcpClientData* client)
{
    int length;
    
    tcpServerBlocked = 1;
    do{
        length = send(client->client, (char*)client->writeBuffer, client->writeBufferLength, 0);
        if( ( errno == EWOULDBLOCK ) && (length == SOCKET_ERROR) ){
            vTaskDelay( TCPSERVER_EWOULDBLOCK_ERROR_TIMEOUT );
        }

    }while( ( errno == EWOULDBLOCK ) && (length == SOCKET_ERROR) );
    tcpServerBlocked = 0;
    if (length == SOCKET_ERROR)
    {
        switch(errno)
        {
        case EWOULDBLOCK:
            // No action
            break;
        case ENOTCONN: // Disconnect
        case ECONNRESET:
            closesocket(client->client);
            TcpServer_InitializeClient(client);
            break;
        case EFAULT: // Bad IP address        
        case EBADF: // i > socket count
        case EINVAL: // i > socket count
        default:
            SYS_DEBUG_PRINT(SYS_ERROR_ERROR, "Unhandled errno: %d", errno);
            break;
        }
    }
    else if ( length == SOCKET_CNXN_IN_PROGRESS )
    {
        // No action
    }
    else if ( length == SOCKET_DISCONNECTED ||
              length == 0) // Disconnect
    {
        closesocket(client->client);
        TcpServer_InitializeClient(client);
    }
    else if(length > 0) // Valid Data
    {
        // netHandle = Function_???(client->client); // Read from BSDSocketArray
        // NET_PRES_SocketFlush(netHandle);
        if (length == client->writeBufferLength)
        {
            client->writeBufferLength = 0;
            client->writeBuffer[client->writeBufferLength] = '\0';
        }
        else
        {
            // Overflow
            SYS_DEBUG_PRINT(SYS_ERROR_ERROR, "Could not write packet. actual=%d, expected=%d.", length, client->writeBufferLength);
            int remainder = client->writeBufferLength - length;
            memcpy(client->writeBuffer, client->writeBuffer + length, remainder);
            client->writeBufferLength = remainder;
            client->writeBuffer[client->writeBufferLength] = '\0';
        }
        
        return true;
    }
    else
    {
        // Unknown error
        SYS_DEBUG_PRINT(SYS_ERROR_ERROR, "Unknown error when writing to socket %d: Length=%d", (int)client->client, length);
    }
    
    return false;
}

/**
 * Gets the TcpClientData associated with the SCPI context
 * @param context The context to lookup
 * @return A pointer to the data, or NULL if the context is not bound to a TCP client
 */
static TcpClientData* SCPI_TCP_GetClient(scpi_t * context)
{
    uint8_t i = 0;
    
    TcpServerData * pRunTimeServerData = BoardRunTimeConfig_Get(            \
                        BOARDRUNTIME_SERVER_DATA);
    
    for (i=0; i<WIFI_MAX_CLIENT; ++i)
    {
        if (&pRunTimeServerData->clients[i].scpiContext == context)
        {
             return &pRunTimeServerData->clients[i];
        }
    }
    
    return NULL;
}

/**
 * Callback from libscpi: Implements the write operation
 * @param context The scpi context
 * @param data The data to write
 * @param len The length of 'data'
 * @return The number of characters written
 */
static size_t SCPI_TCP_Write(scpi_t * context, const char* data, size_t len)
{
    TcpClientData* client = SCPI_TCP_GetClient(context);
    return TcpServer_Write(client, data, len);
}

/**
 * Callback from libscpi: Implements the flush operation
 * @param context The scpi context
 * @return always SCPI_RES_OK
 */
static scpi_result_t SCPI_TCP_Flush(scpi_t * context)
{
    TcpClientData* client = SCPI_TCP_GetClient(context);
    if (TcpServer_Flush(client))
    {
        return SCPI_RES_OK;
    }
    else
    {
        return SCPI_RES_ERR;
    }
}

/**
 * Callback from libscpi: Implements the error operation
 * @param context The scpi context
 * @param err The scpi error code
 * @return always 0
 */
static int SCPI_TCP_Error(scpi_t * context, int_fast16_t err)
{
    char ip[100];
    // If we wanted to do something in response to an error, we could do so here.
    // I'm expecting the client to call 'SYSTem:ERRor?' if they want error information

    sprintf(ip, "**ERROR: %d, \"%s\"\r\n", (int32_t) err, SCPI_ErrorTranslate(err));
    context->interface->write(context, ip, strlen(ip));
    return 0;
}

/**
 * Callback from libscpi: Implements the control operation
 * @param context The scpi context
 * @param ctrl The control name
 * @param val The new value for the control
 * @return alwasy SCPI_RES_OK
 */
static scpi_result_t SCPI_TCP_Control(scpi_t * context, scpi_ctrl_name_t ctrl, scpi_reg_val_t val)
{
    UNUSED(context);
    UNUSED(val);
    if (SCPI_CTRL_SRQ == ctrl)
    {
        //fprintf(stderr, "**SRQ: 0x%X (%d)\r\n", val, val);
    }
    else
    {
        //fprintf(stderr, "**CTRL %02x: 0x%X (%d)\r\n", ctrl, val, val);
    }
    return SCPI_RES_OK;
}

static scpi_interface_t scpi_interface = {
    .write = SCPI_TCP_Write,
    .error = SCPI_TCP_Error,
    .reset = NULL,
    .control = SCPI_TCP_Control,
    .flush = SCPI_TCP_Flush,
};

/**
 * Gets the TcpClientData associated with the microrl context
 * @param context The context to lookup
 * @return A pointer to the data, or NULL if the context is not bound to a TCP client
 */
static TcpClientData* microrl_GetClient(microrl_t* context)
{
    uint8_t i = 0;
    
    TcpServerData * pRunTimeServerData = BoardRunTimeConfig_Get(            \
                        BOARDRUNTIME_SERVER_DATA);
    
    for (i=0; i<WIFI_MAX_CLIENT; ++i)
    {
        if (&pRunTimeServerData->clients[i].console == context)
        {
             return &pRunTimeServerData->clients[i];
        }
    }
    
    return NULL;
}

/**
 * Called to echo commands to the console
 * @param context The console theat made this call
 * @param textLen The length of the text to echo
 * @param text The text to echo
 */
static void microrl_echo(microrl_t* context, size_t textLen, const char* text)
{
        TcpClientData* client = microrl_GetClient(context);
        TcpServer_Write(client, text, textLen);
}

/**
 * Called to process a completed command
 * @param context The console theat made this call
 * @param commandLen The length of the command
 * @param command The command to process
 * @return The result of processing the command, or -1 if the command is invalid;
 */
static int microrl_commandComplete(microrl_t* context, size_t commandLen, const char* command)
{
    TcpClientData* client = microrl_GetClient(context);
    
    if (client == NULL)
    {
        SYS_DEBUG_MESSAGE(SYS_ERROR_ERROR, "Could not find client for provided console.");
        return -1;
    }
    
    if (command != NULL && commandLen > 0)
    {
        return SCPI_Input(&client->scpiContext, command, commandLen);
    }
    
    SYS_DEBUG_MESSAGE(SYS_ERROR_ERROR, "NULL or zero length command.");
    return -1;
}

/**
 * Initializes the given client
 */
static void TcpServer_InitializeClient(TcpClientData* client)
{
    client->client = INVALID_SOCKET;
    client->readBuffer[0] = '\0';
    client->readBufferLength = 0;
    client->writeBuffer[0] = '\0';
    microrl_init(&client->console, microrl_echo);
    microrl_set_echo(&client->console, false);
    microrl_set_execute_callback(&client->console, microrl_commandComplete);
    client->scpiContext = CreateSCPIContext(&scpi_interface, client);
}

void TcpServer_Initialize()
{
    TcpServerData * pRunTimeServerData = BoardRunTimeConfig_Get(            \
                        BOARDRUNTIME_SERVER_DATA);
    
    // Init server params
    pRunTimeServerData->serverSocket = INVALID_SOCKET;
    pRunTimeServerData->state = IP_SERVER_INITIALIZE;
    pRunTimeServerData->hInterface =                                        \
                        TCPIP_STACK_NetHandleGet(WIFI_INTERFACE_NAME);
    
    // Init client params
    uint8_t i;
    for (i=0; i<WIFI_MAX_CLIENT; ++i)
    {
        TcpServer_InitializeClient(&pRunTimeServerData->clients[i]);
    }
}

void TcpServer_ProcessState()
{
    DaqifiSettings * pWifiSettings = BoardData_Get(                         \
                            BOARDDATA_WIFI_SETTINGS,                        \
                            0); 
    
    TcpServerData * pRunTimeServerData = BoardRunTimeConfig_Get(            \
                        BOARDRUNTIME_SERVER_DATA);
    
    switch (pRunTimeServerData->state)
    {
    case IP_SERVER_INITIALIZE:
        TcpServer_Initialize();
        pRunTimeServerData->state = IP_SERVER_WAIT;
        break;
    case IP_SERVER_WAIT:
        if (TCPIP_STACK_NetIsUp(pRunTimeServerData->hInterface))
        {
            pRunTimeServerData->state = IP_SERVER_CONNECT;
        }
        break;
    case IP_SERVER_CONNECT:
        pRunTimeServerData->serverSocket =                                  \
                            socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (pRunTimeServerData->serverSocket != SOCKET_ERROR)
        {
            pRunTimeServerData->state = IP_SERVER_BIND;
        }

        break;
    case IP_SERVER_BIND:
    {
        struct sockaddr_in addr;
        int addrlen = sizeof(struct sockaddr_in);
        addr.sin_port = pWifiSettings->settings.wifi.tcpPort;
        addr.sin_addr.S_un.S_addr = IP_ADDR_ANY;
        if( bind(pRunTimeServerData->serverSocket,              \
                (struct sockaddr*)&addr, addrlen ) != SOCKET_ERROR )
        {
            pRunTimeServerData->state = IP_SERVER_LISTEN;
        }
        break;
    }
    case IP_SERVER_LISTEN:
        if(listen(pRunTimeServerData->serverSocket, WIFI_MAX_CLIENT) == 0)
        {
             pRunTimeServerData->state = IP_SERVER_PROCESS;
        }
        
        break;
    case IP_SERVER_PROCESS:
    {
        struct sockaddr_in addRemote;
        int addrlen = sizeof(struct sockaddr_in);
            
        uint8_t i = 0;
        for (i=0; i<WIFI_MAX_CLIENT; ++i)
        {
            // Accept incoming connections
            TcpClientData* client = &pRunTimeServerData->clients[i];
            if(client->client == INVALID_SOCKET)
            {
                client->client = accept(                                    \
                        pRunTimeServerData->serverSocket,                   \
                        (struct sockaddr*)&addRemote,                       \
                        &addrlen);
                if(client->client == INVALID_SOCKET)
                {
                    // EMFILE indicates that no incoming connections are available. All others indicate that we somehow lost the server connection
                    switch(errno)
                    {
                    case EMFILE: // Server is listening, but there are no incoming connections
                        break;
                    case EFAULT:
                        SYS_DEBUG_MESSAGE(SYS_ERROR_ERROR, "Invalid IP address. Resetting.");
                        pRunTimeServerData->state = IP_SERVER_DISCONNECT;
                        return;  
                    case EOPNOTSUPP:
                        SYS_DEBUG_MESSAGE(SYS_ERROR_ERROR, "Not a streaming connection. Resetting.");
                        pRunTimeServerData->state = IP_SERVER_DISCONNECT;
                        return;
                    case EINVAL:
                        SYS_DEBUG_MESSAGE(SYS_ERROR_WARNING, "TcpServer Disconnected. Resetting.");
                        pRunTimeServerData->state = IP_SERVER_DISCONNECT;
                        return;
                    default:
                        SYS_DEBUG_PRINT(SYS_ERROR_ERROR, "Unhandled errno: %d", errno);
                        break;
                    }

                    // No incoming connections
                    continue;
                }
            }
            
            if (client->readBufferLength < WIFI_RBUFFER_SIZE)
            {
                int length = recv(client->client, (char*)client->readBuffer + client->readBufferLength, WIFI_RBUFFER_SIZE - client->readBufferLength, 0);
                if (length == SOCKET_ERROR)
                {
                    switch(errno)
                    {
                    case EWOULDBLOCK:
                        // No action
                        break;
                    case ENOTCONN: // Disconnect
                    case ECONNRESET:
                        closesocket(client->client);
                        TcpServer_InitializeClient(client);
                        continue;
                    case EFAULT: // Bad IP address        
                    case EBADF: // i > socket count
                    case EINVAL: // i > socket count
                    default:
                        SYS_DEBUG_PRINT(SYS_ERROR_ERROR, "Unhandled errno: %d", errno);
                        continue;
                    }
                }
                else if ( length == SOCKET_CNXN_IN_PROGRESS )
                {
                    // No action
                    continue;
                }
                else if ( length == SOCKET_DISCONNECTED ||
                          length == 0) // Disconnect
                {
                    closesocket(client->client);
                    TcpServer_InitializeClient(client);
                    continue;
                }
                else if(length > 0) // Valid Data
                {
                    // Pass Data to the console for processing
                    client->readBufferLength += length;
                    client->readBuffer[client->readBufferLength] = '\0';

                    size_t j = 0;
                    for (j=0; j<client->readBufferLength; ++j)
                    {
                        microrl_insert_char (&client->console, client->readBuffer[j]);
                    }

                    client->readBufferLength = 0;
                    client->readBuffer[client->readBufferLength] = '\0';
                }
                else
                {
                     SYS_DEBUG_PRINT(SYS_ERROR_ERROR, "Unknown error when reading from socket %d: Length=%d", (int)i, length);
                }
            }
            
            // Send data out
            if (client->writeBufferLength > 0)
            {
                TcpServer_Flush(client);
            }
        }
        
        break;
    }
    case IP_SERVER_DISCONNECT:
    {
        uint8_t i = 0;
        for (i=0; i<WIFI_MAX_CLIENT; ++i)
        {
            TcpClientData* client = &pRunTimeServerData->clients[i];
            if(client->client != INVALID_SOCKET)
            {
                closesocket(client->client);
                client->client = INVALID_SOCKET;
            }
        }
        
        if (pRunTimeServerData->serverSocket != INVALID_SOCKET)
        {
            closesocket(pRunTimeServerData->serverSocket);
            pRunTimeServerData->serverSocket = INVALID_SOCKET;
        }
        
        pRunTimeServerData->state = IP_SERVER_INITIALIZE;
        
        break;
    }
    default:
        LogMessage("TCPIP State error. TcpServer.c ln 502\n\r");
        pRunTimeServerData->state = IP_SERVER_DISCONNECT;
        break;
    }
}

/*! Used for knowing if TCP Server is trying to flush data
* so we should not try yo put additional data on the buffer
* @return 1 When bloked, 0 when it is not
*/
uint8_t TCP_Server_Is_Blocked( void ){
    return tcpServerBlocked;
}
