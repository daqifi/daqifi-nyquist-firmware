#include "wifi_tcp_server.h"
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
wifi_tcp_server_context_t *gpServerData;
//// Function Prototypes

/**
 * @brief Initializes the Wi-Fi TCP server.
 *
 * This function sets up the server context, initializes the console,
 * SCPI context, and circular buffer for data transmission.
 * It should be called once before using other server functions.
 *
 * @param pServerData Pointer to the Wi-Fi TCP server context data structure.
 */
void wifi_tcp_server_Initialize(wifi_tcp_server_context_t *pServerData);

/**
 * @brief Transmits any buffered data over the TCP connection.
 *
 * If there is data available in the write circular buffer,
 * this function will process and send it over the TCP connection.
 *
 * @return True if the function executes successfully.
 */
bool wifi_tcp_server_TransmitBufferedData(void);

/**
 * @brief Retrieves the amount of free space in the write buffer.
 *
 * This function returns the number of bytes available in the write circular buffer,
 * which can be used to determine how much data can be written without blocking.
 *
 * @return The number of bytes available in the write buffer.
 *         Returns 0 if the client socket is not connected.
 */
size_t wifi_tcp_server_GetWriteBuffFreeSize(void);

/**
 * @brief Writes data to the server's write buffer.
 *
 * This function attempts to write the specified data to the server's write circular buffer.
 * If the buffer does not have enough free space, the function will block until space becomes available.
 *
 * @param data Pointer to the data to write.
 * @param len Length of the data to write.
 *
 * @return The number of bytes actually written to the buffer.
 *         Returns 0 if the client socket is not connected or if an error occurs.
 */
size_t wifi_tcp_server_WriteBuffer(const char* data, size_t len);

/**
 * @brief Closes the server and client sockets and resets the server state.
 *
 * This function shuts down the server and client sockets, resets the server context,
 * and clears any pending data in the buffers.
 */
void wifi_tcp_server_CloseSocket(void);

/**
 * @brief Opens the server socket on the specified port.
 *
 * This function creates a TCP server socket and binds it to the specified port,
 * allowing it to accept incoming client connections.
 *
 * @param port The port number on which to open the server socket.
 */
void wifi_tcp_server_OpenSocket(uint16_t port);

/**
 * @brief Processes received data from the client.
 *
 * This function processes the data received in the client's read buffer,
 * passing each character to the console input handler.
 *
 * @return True if the data is processed successfully.
 */
bool wifi_tcp_server_ProcessReceivedBuff(void);

/**
 * @brief Closes the client socket and resets the client state.
 *
 * This function shuts down the client socket, resets client-related data,
 * and clears any pending data in the client's buffers.
 */
void wifi_tcp_server_CloseClientSocket(void);
static bool TcpServerFlush() {
    int16_t sockRet;
    bool funRet = false;
    if (gpServerData->client.clientSocket < 0) {
        return false;
    }
    if (gpServerData->client.writeBufferLength >WIFI_WBUFFER_SIZE) {
        gpServerData->client.writeBufferLength = WIFI_WBUFFER_SIZE;
    } else if (gpServerData->client.writeBufferLength == 0) {
        return true;
    }

    do {
        //sockRet = send(gpServerData->client.clientSocket, (char*) gpServerData->client.writeBuffer, gpServerData->client.writeBufferLength, 0); 
        sockRet = send(gpServerData->client.clientSocket, (char*) gpServerData->client.writeBuffer, gpServerData->client.writeBufferLength, 0); 
        if (sockRet == SOCK_ERR_BUFFER_FULL) {
            vTaskDelay(TCPSERVER_EWOULDBLOCK_ERROR_TIMEOUT);
        }

    } while (sockRet != SOCK_ERR_NO_ERROR && sockRet != SOCK_ERR_CONN_ABORTED);


    if (sockRet == SOCK_ERR_CONN_ABORTED) {
        funRet = false;
    } else if (sockRet == SOCK_ERR_NO_ERROR) {
        gpServerData->client.tcpSendPending = 1;
        gpServerData->client.writeBufferLength = 0;
        funRet = true;
    }

    return funRet;
}

/**
 * Gets the TcpClientData associated with the SCPI context
 * @param context The context to lookup
 * @return A pointer to the data, or NULL if the context is not bound to a TCP client
 */
static wifi_tcp_server_clientContext_t* SCPI_TCP_GetClient(scpi_t * context) {
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
    return wifi_tcp_server_WriteBuffer(data, len);
}

/**
 * Callback from libscpi: Implements the flush operation
 * @param context The scpi context
 * @return always SCPI_RES_OK
 */
static scpi_result_t SCPI_TCP_Flush(scpi_t * context) {

    wifi_tcp_server_clientContext_t* client = SCPI_TCP_GetClient(context);
    UNUSED(client);
    return SCPI_RES_OK;
    //    if(client==NULL){
    //        return SCPI_RES_ERR;
    //    }
    //    if (TcpServerFlush(client)) {
    //        return SCPI_RES_OK;
    //    } else {
    //        return SCPI_RES_ERR;
    //    }
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
static wifi_tcp_server_clientContext_t* microrl_GetClient(microrl_t* context) {
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
    wifi_tcp_server_WriteBuffer(text, textLen);
}

/**
 * Called to process a completed command
 * @param context The console theat made this call
 * @param commandLen The length of the command
 * @param command The command to process
 * @return The result of processing the command, or -1 if the command is invalid;
 */
static int microrl_commandComplete(microrl_t* context, size_t commandLen, const char* command) {
    wifi_tcp_server_clientContext_t* client = microrl_GetClient(context);

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

    if (len>sizeof (gpServerData->client.writeBuffer))
        return false;
    memcpy(gpServerData->client.writeBuffer, buf, len);
    gpServerData->client.writeBufferLength = len;
    return TcpServerFlush(&gpServerData->client);
}
//==========================External Apis==========================

void wifi_tcp_server_Initialize(wifi_tcp_server_context_t *pServerData) {
    static bool isInitDone = false;
    if (!isInitDone) {
        gpServerData = pServerData;
        gpServerData->client.readBufferLength = 0;
        gpServerData->client.writeBufferLength = 0;
        gpServerData->serverSocket = -1;
        gpServerData->client.clientSocket = -1;
        gpServerData->client.tcpSendPending = 0;
        microrl_init(&gpServerData->client.console, microrl_echo);
        microrl_set_echo(&gpServerData->client.console, false);
        microrl_set_execute_callback(&gpServerData->client.console, microrl_commandComplete);
        gpServerData->client.scpiContext = CreateSCPIContext(&scpi_interface, &gpServerData->client);
        CircularBuf_Init(&gpServerData->client.wCirbuf,
                CircularBufferToTcpWrite,
                (WIFI_CIRCULAR_BUFF_SIZE));
        gpServerData->client.wMutex = xSemaphoreCreateMutex();
        xSemaphoreGive(gpServerData->client.wMutex);
        isInitDone = true;
    }
}

void wifi_tcp_server_OpenSocket(uint16_t port) {
    // Init server params
    if (gpServerData->serverSocket == -1) {
        gpServerData->serverSocket = socket(AF_INET, SOCK_STREAM, SOCKET_CONFIG_SSL_OFF);
        if (gpServerData->serverSocket >= 0) {
            struct sockaddr_in addr;
            addr.sin_family = AF_INET;
            addr.sin_port = _htons(port);
            addr.sin_addr.s_addr = 0;
            bind(gpServerData->serverSocket, (struct sockaddr*) &addr, sizeof (struct sockaddr_in));
        }
    }
}

void wifi_tcp_server_CloseSocket() {

    shutdown(gpServerData->client.clientSocket);
    shutdown(gpServerData->serverSocket);
    gpServerData->serverSocket = -1;
    gpServerData->client.clientSocket = -1;
    gpServerData->client.readBufferLength = 0;
    gpServerData->client.writeBufferLength = 0;
    gpServerData->client.tcpSendPending = 0;
    CircularBuf_Reset(&gpServerData->client.wCirbuf);
}

void wifi_tcp_server_CloseClientSocket() {
    shutdown(gpServerData->client.clientSocket);
    gpServerData->client.clientSocket = -1;
    gpServerData->client.readBufferLength = 0;
    gpServerData->client.writeBufferLength = 0;
    gpServerData->client.tcpSendPending = 0;
    CircularBuf_Reset(&gpServerData->client.wCirbuf);
}

size_t wifi_tcp_server_GetWriteBuffFreeSize() {
    if (gpServerData->client.clientSocket < 0) {
        return 0;
    }

    return CircularBuf_NumBytesFree(&gpServerData->client.wCirbuf);
}

size_t wifi_tcp_server_WriteBuffer(const char* data, size_t len) {
    size_t bytesAdded = 0;

    if (gpServerData->client.clientSocket < 0) {
        return 0;
    }

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

bool wifi_tcp_server_ProcessReceivedBuff() {
    size_t j = 0;
    for (j = 0; j < gpServerData->client.readBufferLength; ++j) {
        microrl_insert_char(&gpServerData->client.console, gpServerData->client.readBuffer[j]);
    }
    gpServerData->client.readBufferLength = 0;
    gpServerData->client.readBuffer[gpServerData->client.readBufferLength] = '\0';
    return true;
}

bool wifi_tcp_server_TransmitBufferedData() {
    int ret;
    UNUSED(ret);
    if (gpServerData->client.tcpSendPending != 0) {
        return false;
    }
  
    if (CircularBuf_NumBytesAvailable(&gpServerData->client.wCirbuf) > 0) {
        xSemaphoreTake(gpServerData->client.wMutex, portMAX_DELAY);
        CircularBuf_ProcessBytes(&gpServerData->client.wCirbuf, NULL, WIFI_WBUFFER_SIZE, &ret);
        xSemaphoreGive(gpServerData->client.wMutex);
    }
    return true;
}
