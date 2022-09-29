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
#define TCPSERVER_EWOULDBLOCK_ERROR_TIMEOUT         100

#define UNUSED(x) (void)(x)

static uint8_t tcpServerBlocked = 0;

// Function Prototypes
static void     TcpServer_InitializeClient(TcpClientData* client);
static bool     TcpServer_Flush(TcpClientData* client);
static size_t   TcpServer_Write(TcpClientData* client, const char* data, size_t len);
static int      TcpServer_ProcessStreamBufferCallback(uint8_t* buf, uint16_t len);
static uint16_t TCPServer_GetTxBytesReady(void);
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
        size_t remainder = WIFI_BUFFER_SIZE - client->writeBufferLength - 1;
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
    if((TCPIP_TCP_IsConnected(client->socket)) 
    && (TCPIP_TCP_PutIsReady (client->socket)>= client->writeBufferLength))
    {
        TCPIP_TCP_ArrayPut(client->socket, client->writeBuffer, client->writeBufferLength);
        client->writeBufferLength = 0;
        client->writeBuffer[client->writeBufferLength] = '\0';
        return true;
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
    for (i=0; i<WIFI_MAX_CLIENT; ++i)
    {
        if (&g_BoardRuntimeConfig.serverData.clients[i].scpiContext == context)
        {
             return &g_BoardRuntimeConfig.serverData.clients[i];
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
    if(len==0)return 0;
    
    TcpClientData* client = SCPI_TCP_GetClient(context);
    
    DEBUG_PRINTF("Tx TCP: %s\r\n", data);
//    return TcpServer_Write(client, data, len);
    if(TCPIP_TCP_IsConnected(client->socket) && (TCPIP_TCP_PutIsReady(client->socket)>=len)){
        return TCPIP_TCP_ArrayPut(client->socket, data, len);
    }
//    
    return 0;
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
    
    
//    if(TCPIP_TCP_IsConnected(client->socket)){
//        TCPIP_TCP_Flush(client->socket);
//        return SCPI_RES_OK;
//    }
//
//    return SCPI_RES_ERR;
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
    .flush = NULL,//SCPI_TCP_Flush,
};

/**
 * Gets the TcpClientData associated with the microrl context
 * @param context The context to lookup
 * @return A pointer to the data, or NULL if the context is not bound to a TCP client
 */
static TcpClientData* microrl_GetClient(microrl_t* context)
{
    uint8_t i = 0;
    for (i=0; i<WIFI_MAX_CLIENT; ++i)
    {
        if (&g_BoardRuntimeConfig.serverData.clients[i].console == context)
        {
             return &g_BoardRuntimeConfig.serverData.clients[i];
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
    if(textLen == 0)return;
    
    TcpClientData* client = microrl_GetClient(context);
//    TcpServer_Write(client, text, textLen);
    if(TCPIP_TCP_IsConnected(client->socket) && (TCPIP_TCP_PutIsReady(client->socket)>=textLen)){
        TCPIP_TCP_ArrayPut(client->socket, text, textLen);
    }
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
        DEBUG_PRINTF("Recv TCP: %s",command);
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
    client->socket = INVALID_SOCKET;
    client->reconnectTick = 0;
    client->readBuffer[0] = '\0';
    client->readBufferLength = 0;
    client->writeBuffer[0] = '\0';
    client->writeBufferLength = 0;
    microrl_init(&client->console, microrl_echo);
    microrl_set_echo(&client->console, false);
    microrl_set_execute_callback(&client->console, microrl_commandComplete);
    client->scpiContext = CreateSCPIContext(&scpi_interface, client);
}

void TcpServer_Initialize()
{
    // Init server params
    g_BoardRuntimeConfig.serverData.serverSocket = INVALID_SOCKET;
    g_BoardRuntimeConfig.serverData.state = IP_SERVER_INITIALIZE;
    g_BoardRuntimeConfig.serverData.hInterface = TCPIP_STACK_NetHandleGet(WIFI_INTERFACE_NAME);
    
    // Initialize transmit buffer. 
    CircularBuf_Init(&g_BoardRuntimeConfig.serverData.streamCirbuf, 
                     TcpServer_ProcessStreamBufferCallback, 
                    (USB_WBUFFER_SIZE*2));
        
    // Init client params
    uint8_t i;
    for (i=0; i<WIFI_MAX_CLIENT; ++i)
    {
        TcpServer_InitializeClient(&g_BoardRuntimeConfig.serverData.clients[i]);
    }
}

void TcpServer_ProcessState()
{
    uint8_t i;
    uint16_t tcpPort;
    TcpServerData* server;
                 
    switch (g_BoardRuntimeConfig.serverData.state)
    {
        case IP_SERVER_INITIALIZE:
        {
            TcpServer_Initialize();
            g_BoardRuntimeConfig.serverData.state = IP_SERVER_WAIT;
        }break;
        
        case IP_SERVER_WAIT:
        {
            if (TCPIP_STACK_NetIsUp(g_BoardRuntimeConfig.serverData.hInterface))
            {
                g_BoardRuntimeConfig.serverData.state = IP_SERVER_OPENING_SERVER;
            }
        }break;

        case IP_SERVER_OPENING_SERVER:
        {  
            tcpPort = g_BoardData.wifiSettings.settings.wifi.tcpPort;
            server  = &g_BoardRuntimeConfig.serverData;
                           
            for (i=0; i<WIFI_MAX_CLIENT; ++i)
            {                
                server->clients[i].sock_was_connected = false;
                server->clients[i].socket = TCPIP_TCP_ServerOpen(IP_ADDRESS_TYPE_IPV4,tcpPort,0);

                if (server->clients[i].socket == INVALID_SOCKET)
                {
                    SYS_CONSOLE_MESSAGE("Couldn't open server socket\r\n");
                    break;
                }
            }

            server->state = IP_SERVER_SERVING_CONNECTION;

        }break;
        
        case IP_SERVER_SERVING_CONNECTION:
        {
            uint16_t bytesToSend, bytesToRead;
            int error;

            tcpPort     = g_BoardData.wifiSettings.settings.wifi.tcpPort;
            server      = &g_BoardRuntimeConfig.serverData;
            bytesToSend = min(TCPServer_GetTxBytesReady(), CircularBuf_NumBytesAvailable(&server->streamCirbuf));
           
            // transfer the data out of our stream buffer and into TCP TX FIFO
            CircularBuf_ProcessBytes(&server->streamCirbuf, NULL, bytesToSend, &error);

            for (i=0; i<WIFI_MAX_CLIENT; i++)
            {
                // previous now action
                // 0        0   do nothing
                // 0        1   read / write to socket
                // 1        0   reinitialize socket
                // 1        1   read / write to socket

                if(TCPIP_TCP_IsConnected(server->clients[i].socket)){
                    // read and write to socket
                    server->clients[i].sock_was_connected = true;
                    
                    bytesToRead = TCPIP_TCP_GetIsReady(server->clients[i].socket);	// Get TCP RX FIFO byte count

                    if(bytesToRead>0){
                               
                        // Transfer the data out of the TCP RX FIFO and into our local processing buffer.
                        TCPIP_TCP_ArrayGet(server->clients[i].socket, 
                                           server->clients[i].readBuffer, 
                                           bytesToRead);
                        
                        size_t j=0;
                        
                        for (j=0; j<bytesToRead; ++j)
                        {
                            microrl_insert_char (&server->clients[i].console, server->clients[i].readBuffer[j]);
                        }
                    }
                }
                else{

                    if(server->clients[i].sock_was_connected){
                        //reinitialized the socket
                          // Close the socket connection.
                        TCPIP_TCP_Close(server->clients[i].socket);
                        server->clients[i].socket = INVALID_SOCKET;
                        server->clients[i].sock_was_connected = false;
                        server->clients[i].socket = TCPIP_TCP_ServerOpen(IP_ADDRESS_TYPE_IPV4, tcpPort, 0);
                    }
                }
            }
        }break;
            
    case IP_SERVER_CLOSING_CONNECTION:
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

bool TCPServer_IsConnected( void )
{
    int i;    
    TcpServerData* server = &g_BoardRuntimeConfig.serverData;
        
    for (i=0; i<WIFI_MAX_CLIENT; i++)
    {
        if(server->clients[i].socket == INVALID_SOCKET)
        continue;
        
        if(TCPIP_TCP_IsConnected(server->clients[i].socket))
        return true;
    }
   
    return false;
}

static uint16_t TCPServer_GetTxBytesReady(void)
{
    int i;
    uint16_t wMaxPut = 0xFFFF;
    uint16_t bytes;
    bool hasConnection = false;
    TcpServerData* server = &g_BoardRuntimeConfig.serverData;
        
    for (i=0; i<WIFI_MAX_CLIENT; i++)
    {
        if(TCPIP_TCP_IsConnected(server->clients[i].socket)){
            
            hasConnection = true;
            bytes = TCPIP_TCP_PutIsReady(server->clients[i].socket);	// Get TCP TX FIFO free space
            
            if(bytes<wMaxPut)
            wMaxPut = bytes;
        }
    }
    
    if(hasConnection == false)
    wMaxPut = 0;
    
    return wMaxPut;
}

static int TcpServer_ProcessStreamBufferCallback(uint8_t* buf, uint16_t len)
{    
    int i;
    TcpServerData* server = &g_BoardRuntimeConfig.serverData;
     
    for (i=0; i<WIFI_MAX_CLIENT; i++)
    {
        server = &g_BoardRuntimeConfig.serverData;
        
        if((TCPIP_TCP_IsConnected(server->clients[i].socket)==false) 
        || (TCPIP_TCP_PutIsReady (server->clients[i].socket)<len))
        continue;
        
        TCPIP_TCP_ArrayPut(server->clients[i].socket, buf, len);
    }
    
    return 0;
}
