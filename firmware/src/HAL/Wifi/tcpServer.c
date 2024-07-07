#include "tcpServer.h"
#include "services/SCPI/SCPIInterface.h"
#include "Util/Logger.h"
#include "socket.h"

#ifndef min
#define min(x,y) x <= y ? x : y
#endif // min

#ifndef max
#define max(x,y) x >= y ? x : y
#endif // min

#define UNUSED(x) (void)(x)
//! Timeout for waiting when WiFi device is full and returning EWOULDBLOCK error
#define TCPSERVER_EWOULDBLOCK_ERROR_TIMEOUT         1
TcpServerData *gpServerData;
//// Function Prototypes
bool TcpServer_Flush();
size_t TcpServer_WriteBuffer(const char* data, size_t len);
void TcpServer_CloseSocket();
void TcpServer_OpenSocket();

/**
 * Gets the TcpClientData associated with the SCPI context
 * @param context The context to lookup
 * @return A pointer to the data, or NULL if the context is not bound to a TCP client
 */
static TcpClientData* SCPI_TCP_GetClient(scpi_t * context) {
    return &gpServerData->client;
}

/**
 * Callback from libscpi: Implements the write operation
 * @param context The scpi context
 * @param data The data to write
 * @param len The length of 'data'
 * @return The number of characters written
 */
static size_t SCPI_TCP_Write(scpi_t * context, const char* data, size_t len) {
    //TcpClientData* client = SCPI_TCP_GetClient(context);
    return TcpServer_WriteBuffer(data, len);
}

/**
 * Callback from libscpi: Implements the flush operation
 * @param context The scpi context
 * @return always SCPI_RES_OK
 */
static scpi_result_t SCPI_TCP_Flush(scpi_t * context) {
    TcpClientData* client = SCPI_TCP_GetClient(context);
    if (TcpServer_Flush(client)) {
        return SCPI_RES_OK;
    } else {
        return SCPI_RES_ERR;
    }
}

/**
 * Callback from libscpi: Implements the error operation
 * @param context The scpi context
 * @param err The scpi error code
 * @return always 0
 */
static int SCPI_TCP_Error(scpi_t * context, int_fast16_t err) {
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
static scpi_result_t SCPI_TCP_Control(scpi_t * context, scpi_ctrl_name_t ctrl, scpi_reg_val_t val) {
    UNUSED(context);
    UNUSED(val);
    if (SCPI_CTRL_SRQ == ctrl) {
        //fprintf(stderr, "**SRQ: 0x%X (%d)\r\n", val, val);
    } else {
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
static TcpClientData* microrl_GetClient(microrl_t* context) {
    return &gpServerData->client;
}

/**
 * Called to echo commands to the console
 * @param context The console theat made this call
 * @param textLen The length of the text to echo
 * @param text The text to echo
 */
static void microrl_echo(microrl_t* context, size_t textLen, const char* text) {
    //TcpClientData* client = microrl_GetClient(context);
    TcpServer_WriteBuffer(text, textLen);
}

/**
 * Called to process a completed command
 * @param context The console theat made this call
 * @param commandLen The length of the command
 * @param command The command to process
 * @return The result of processing the command, or -1 if the command is invalid;
 */
static int microrl_commandComplete(microrl_t* context, size_t commandLen, const char* command) {
    TcpClientData* client = microrl_GetClient(context);

    if (client == NULL) {
        SYS_DEBUG_MESSAGE(SYS_ERROR_ERROR, "Could not find client for provided console.");
        return -1;
    }

    if (command != NULL && commandLen > 0) {
        return SCPI_Input(&client->scpiContext, command, commandLen);
    }

    SYS_DEBUG_MESSAGE(SYS_ERROR_ERROR, "NULL or zero length command.");
    return -1;
}

static int CircularBufferToTcpWrite(uint8_t* buf, uint16_t len) {
    xSemaphoreTake(gpServerData->client.wMutex, portMAX_DELAY);
    if (len>sizeof (gpServerData->client.writeBuffer))
        return false;
    memcpy(gpServerData->client.writeBuffer, buf, len);
    gpServerData->client.writeBufferLength = len;
    xSemaphoreGive(gpServerData->client.wMutex);
    return TcpServer_Flush(&gpServerData->client);
}
//==========================External Apis==========================

void TcpServer_Initialize(TcpServerData *pServerData) {
    static bool isInitDone = false;
    if (!isInitDone) {
        gpServerData=pServerData;
        gpServerData->client.readBufferLength = 0;
        gpServerData->client.writeBufferLength = 0;
        gpServerData->serverSocket = -1;
        gpServerData->client.clientSocket = -1;
        microrl_init(&gpServerData->client.console, microrl_echo);
        microrl_set_echo(&gpServerData->client.console, false);
        microrl_set_execute_callback(&gpServerData->client.console, microrl_commandComplete);
        gpServerData->client.scpiContext = CreateSCPIContext(&scpi_interface, &gpServerData->client);
        CircularBuf_Init(&gpServerData->client.wCirbuf,
                CircularBufferToTcpWrite,
                (WIFI_RBUFFER_SIZE * 4));
        gpServerData->client.wMutex = xSemaphoreCreateMutex();
        xSemaphoreGive(gpServerData->client.wMutex);
        isInitDone = true;        
    }
}

void TcpServer_OpenSocket() {
    // Init server params
    if (gpServerData->serverSocket == -1) {
        gpServerData->serverSocket = socket(AF_INET, SOCK_STREAM, SOCKET_CONFIG_SSL_OFF);
    }
}

void TcpServer_CloseSocket() {
    
    shutdown(gpServerData->serverSocket);
    shutdown(gpServerData->serverSocket);
    gpServerData->serverSocket = -1;
    gpServerData->client.clientSocket = -1;
    gpServerData->client.readBufferLength = 0;
    gpServerData->client.writeBufferLength = 0;
    CircularBuf_Reset(&gpServerData->client.wCirbuf);
}

/**
 * Writes data to the output buffere
 * @param client The client to write to
 * @param data The data to write
 * @param len The length of the data
 * @return The number of characters written
 */
size_t TcpServer_WriteBuffer(const char* data, size_t len) {
    size_t bytesAdded = 0;

    if (len == 0)return 0;

    while (CircularBuf_NumBytesFree(&gpServerData->client.wCirbuf) < len) {
        vTaskDelay(10);
    }

    // if the data to write can't fit into the buffer entirely, discard it. 
    if (CircularBuf_NumBytesFree(&gpServerData->client.wCirbuf) < len) {
        return 0;
    }

    //Obtain ownership of the mutex object
    xSemaphoreTake(gpServerData->client.wMutex, portMAX_DELAY);
    bytesAdded = CircularBuf_AddBytes(&gpServerData->client.wCirbuf, (uint8_t*) data, len);
    xSemaphoreGive(gpServerData->client.wMutex);

    return bytesAdded;
}

/**
 * Flushes data from the provided client
 * @param client The client to flush
 * @return  True if data is flushed, false otherwise
 */
bool TcpServer_Flush() {
    int16_t sockRet;
    bool funRet = false;
    if(gpServerData->client.clientSocket<=0){
        return false;
    }
    do {
        sockRet = send(gpServerData->client.clientSocket, (char*)gpServerData->client.writeBuffer, gpServerData->client.writeBufferLength, 0);
        if (sockRet == SOCK_ERR_BUFFER_FULL) {
            vTaskDelay(TCPSERVER_EWOULDBLOCK_ERROR_TIMEOUT);
        }

    } while (sockRet != SOCK_ERR_NO_ERROR || sockRet != SOCK_ERR_CONN_ABORTED);


    if (sockRet == SOCK_ERR_CONN_ABORTED) {
        //        shutdown(gServerData.serverSocket);
        //        TcpServer_InitializeClient(client);
        funRet = false;
    } else if (sockRet == SOCK_ERR_NO_ERROR) {
        gpServerData->client.writeBufferLength = 0;
        funRet = true;
    }
    return funRet;
}
//void TcpServer_ProcessState()
//{
//    DaqifiSettings * pWifiSettings = BoardData_Get(                         
//                            BOARDDATA_WIFI_SETTINGS,                        
//                            0); 
//    
//    TcpServerData * pRunTimeServerData = BoardRunTimeConfig_Get(            
//                        BOARDRUNTIME_SERVER_DATA);
//    
//    switch (pRunTimeServerData->state)
//    {
//    case IP_SERVER_INITIALIZE:
//        TcpServer_Initialize();
//        pRunTimeServerData->state = IP_SERVER_CONNECT;
//        break;
//    case IP_SERVER_CONNECT:
//        pRunTimeServerData->serverSocket = socket(AF_INET, SOCK_STREAM, SOCKET_CONFIG_SSL_OFF);
//        if (pRunTimeServerData->serverSocket >=0)
//        {
//            pRunTimeServerData->state = IP_SERVER_BIND;
//        }
//
//        break;
//    case IP_SERVER_BIND:
//    {
//        struct sockaddr_in addr;
//        int addrlen = sizeof(struct sockaddr_in);
//        addr.sin_port = _htons(pWifiSettings->settings.wifi.tcpPort);
//        addr.sin_addr.s_addr = 0;
//        if( bind(pRunTimeServerData->serverSocket,(struct sockaddr*)&addr, addrlen ) != SOCK_ERR_NO_ERROR )
//        {
//            pRunTimeServerData->state = IP_SERVER_LISTEN;
//        }
//        break;
//    }
//    case IP_SERVER_LISTEN:
//        if(listen(pRunTimeServerData->serverSocket, WIFI_MAX_CLIENT) == 0)
//        {
//             pRunTimeServerData->state = IP_SERVER_PROCESS;
//        }
//        
//        break;
//    case IP_SERVER_PROCESS:
//    {
//        struct sockaddr_in addRemote;
//        int addrlen = sizeof(struct sockaddr_in);
//            
//        uint8_t i = 0;
//        for (i=0; i<WIFI_MAX_CLIENT; ++i)
//        {
//            // Accept incoming connections
//            TcpClientData* client = &pRunTimeServerData->clients[i];
//            if(client->client == -1)
//            {
//                client->client = accept(pRunTimeServerData->serverSocket,(struct sockaddr*)&addRemote,&addrlen);
//                if(client->client == -1)
//                {
//                    // EMFILE indicates that no incoming connections are available. All others indicate that we somehow lost the server connection
//                    switch(errno)
//                    {
//                    case EMFILE: // Server is listening, but there are no incoming connections
//                        break;
//                    case EFAULT:
//                        SYS_DEBUG_MESSAGE(SYS_ERROR_ERROR, "Invalid IP address. Resetting.");
//                        pRunTimeServerData->state = IP_SERVER_DISCONNECT;
//                        return;  
//                    case EOPNOTSUPP:
//                        SYS_DEBUG_MESSAGE(SYS_ERROR_ERROR, "Not a streaming connection. Resetting.");
//                        pRunTimeServerData->state = IP_SERVER_DISCONNECT;
//                        return;
//                    case EINVAL:
//                        SYS_DEBUG_MESSAGE(SYS_ERROR_WARNING, "TcpServer Disconnected. Resetting.");
//                        pRunTimeServerData->state = IP_SERVER_DISCONNECT;
//                        return;
//                    default:
//                        SYS_DEBUG_PRINT(SYS_ERROR_ERROR, "Unhandled errno: %d", errno);
//                        break;
//                    }
//
//                    // No incoming connections
//                    continue;
//                }
//            }
//            
//            if (client->readBufferLength < WIFI_RBUFFER_SIZE)
//            {
//                int length = recv(client->client, (char*)client->readBuffer + client->readBufferLength, WIFI_RBUFFER_SIZE - client->readBufferLength, 0);
//                if (length == SOCKET_ERROR)
//                {
//                    switch(errno)
//                    {
//                    case EWOULDBLOCK:
//                        // No action
//                        break;
//                    case ENOTCONN: // Disconnect
//                    case ECONNRESET:
//                        closesocket(client->client);
//                        TcpServer_InitializeClient(client);
//                        continue;
//                    case EFAULT: // Bad IP address        
//                    case EBADF: // i > socket count
//                    case EINVAL: // i > socket count
//                    default:
//                        SYS_DEBUG_PRINT(SYS_ERROR_ERROR, "Unhandled errno: %d", errno);
//                        continue;
//                    }
//                }
//                else if ( length == SOCKET_CNXN_IN_PROGRESS )
//                {
//                    // No action
//                    continue;
//                }
//                else if ( length == SOCKET_DISCONNECTED ||
//                          length == 0) // Disconnect
//                {
//                    closesocket(client->client);
//                    TcpServer_InitializeClient(client);
//                    continue;
//                }
//                else if(length > 0) // Valid Data
//                {
//                    // Pass Data to the console for processing
//                    client->readBufferLength += length;
//                    client->readBuffer[client->readBufferLength] = '\0';
//
//                    size_t j = 0;
//                    for (j=0; j<client->readBufferLength; ++j)
//                    {
//                        microrl_insert_char (&client->console, client->readBuffer[j]);
//                    }
//
//                    client->readBufferLength = 0;
//                    client->readBuffer[client->readBufferLength] = '\0';
//                }
//                else
//                {
//                     SYS_DEBUG_PRINT(SYS_ERROR_ERROR, "Unknown error when reading from socket %d: Length=%d", (int)i, length);
//                }
//            }
//            
//            // Send data out
//            if (client->writeBufferLength > 0)
//            {
//                TcpServer_Flush(client);
//            }
//        }
//        
//        break;
//    }
//    case IP_SERVER_DISCONNECT:
//    {
//        uint8_t i = 0;
//        for (i=0; i<WIFI_MAX_CLIENT; ++i)
//        {
//            TcpClientData* client = &pRunTimeServerData->clients[i];
//            if(client->client != INVALID_SOCKET)
//            {
//                closesocket(client->client);
//                client->client = INVALID_SOCKET;
//            }
//        }
//        
//        if (pRunTimeServerData->serverSocket != INVALID_SOCKET)
//        {
//            closesocket(pRunTimeServerData->serverSocket);
//            pRunTimeServerData->serverSocket = INVALID_SOCKET;
//        }
//        
//        pRunTimeServerData->state = IP_SERVER_INITIALIZE;
//        
//        break;
//    }
//    default:
//        LogMessage("TCPIP State error. TcpServer.c ln 502\n\r");
//        pRunTimeServerData->state = IP_SERVER_DISCONNECT;
//        break;
//    }
//}
