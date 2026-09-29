#define LOG_LVL LOG_LEVEL_WIFI
#define LOG_MODULE LOG_MODULE_WIFI
#include "wifi_tcp_server.h"
#include "services/SCPI/SCPIInterface.h"
#include "Util/Logger.h"
#include "Util/StreamingBufferPool.h"
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
/** #1073: how long the owed-close consumer waits for wMutex before giving up
 *  and leaving the close owed for the next app_WifiTask iteration (~5 ms).
 *  Comfortably longer than the only legitimate hold that involves I/O (one
 *  send() HIF transaction) and far shorter than the 250 ms ProcessState mutex
 *  budget it sits inside. */
#define WIFI_TCP_CLOSE_MUTEX_WAIT_MS                10
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
static bool TcpServerFlush(void) {
    int16_t sockRet;
    bool funRet = false;
    if (gpServerData->client.clientSocket < 0) {
        LOG_D("TCP flush: no client connected");
        return false;
    }
    if (gpServerData->client.writeBufferLength >WIFI_WBUFFER_SIZE) {
        gpServerData->client.writeBufferLength = WIFI_WBUFFER_SIZE;
    } else if (gpServerData->client.writeBufferLength == 0) {
        return true;
    }

    // Non-blocking send: try once, return immediately if WINC buffer is full
    // This prevents the streaming task from blocking for multiple milliseconds
    // during high-rate streaming when WiFi bandwidth is saturated
    sockRet = send(gpServerData->client.clientSocket, (char*) gpServerData->client.writeBuffer, gpServerData->client.writeBufferLength, 0);

    if (sockRet == SOCK_ERR_CONN_ABORTED) {
        funRet = false;
    } else if (sockRet == SOCK_ERR_NO_ERROR) {
        // Update stats, ring slot, and inflight counter atomically so
        // the SOCKET_MSG_SEND callback reads consistent values if it
        // fires immediately on the WiFi task as soon as we exit.
        taskENTER_CRITICAL();
        gpServerData->client.inflightSizes[gpServerData->client.inflightHead] =
            gpServerData->client.writeBufferLength;
        gpServerData->client.inflightHead =
            (gpServerData->client.inflightHead + 1) % WIFI_TCP_MAX_IN_FLIGHT;
        gpServerData->client.wifiTcpBytesSent += gpServerData->client.writeBufferLength;
        gpServerData->client.tcpInFlight++;
        taskEXIT_CRITICAL();
        gpServerData->client.writeBufferLength = 0;
        gpServerData->client.lastActivityTick = xTaskGetTickCount(); // #663: TX keeps a streaming client non-idle
        funRet = true;
    } else if (sockRet == SOCK_ERR_BUFFER_FULL) {
        // WINC module buffer full - return false without blocking
        // Data remains in writeBuffer and will be retried on next TransmitBufferedData call
        funRet = false;
    } else {
        // Other error - log for debugging
        static uint32_t errorCount = 0;
        if ((++errorCount % 100) == 0) {
            LOG_E("TcpServerFlush: send() returned error %d (count=%u)", sockRet, (unsigned)errorCount);
        }
        funRet = false;
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
bool wifi_tcp_server_ContextIsTcp(const scpi_t* context) {
    // #598: identify the TCP SCPI context by its user_context (set in
    // CreateSCPIContext below to the TCP client context). USB's context
    // carries &gRunTimeUsbSttings instead.
    return (gpServerData != NULL) &&
           (context != NULL) &&
           (context->user_context == (void*)&gpServerData->client);
}

uint32_t wifi_tcp_server_GetConnGeneration(void) {
    return (gpServerData != NULL) ? gpServerData->client.connGeneration : 0u;
}

bool wifi_tcp_server_ConnIsCurrent(uint32_t generation) {
    // #599: the originating connection still owns the single TCP slot only if
    // a client is currently connected (clientSocket >= 0) AND its generation
    // is unchanged.  A disconnect (clientSocket -> -1) or a different client
    // taking the slot (generation bumped in SOCKET_MSG_ACCEPT) both fail this.
    return (gpServerData != NULL) &&
           (gpServerData->client.clientSocket >= 0) &&
           (gpServerData->client.connGeneration == generation);
}

static size_t SCPI_TCP_Write(scpi_t * context, const char* data, size_t len) {
    // Skip retry loop if client is disconnected — no progress possible
    if (gpServerData == NULL || gpServerData->client.clientSocket < 0) {
        return 0;
    }
    return SCPI_WriteWithRetry(wifi_tcp_server_WriteBuffer, data, len);
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
    if (err != 0) {
        const char *err_str = SCPI_ErrorTranslate(err);
        if (err_str == NULL) {
            err_str = "Unknown";
        }
        snprintf(ip, sizeof(ip), "**ERROR: %d, \"%s\"\r\n", (int32_t) err, err_str);
        context->interface->write(context, ip, strlen(ip));
        // Also log to our Logger so errors appear in SYST:LOG?
        LOG_E("SCPI Error %d: %s\r\n", (int32_t) err, err_str);
    }
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

/* #999: this transport's own SCPI parse buffer + error queue. Must NOT be
 * shared with UsbCdc.c's -- see ScpiContextStorage in SCPIInterface.h for
 * why sharing corrupts both transports' commands and errors. */
static ScpiContextStorage gTcpScpiStorage;

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

static int CircularBufferToTcpWrite(uint8_t* buf, uint32_t len) {
    // Validate context pointer to prevent null dereference
    if (gpServerData == NULL) {
        LOG_E("TCP: server context NULL");
        return -1;  // Not initialized
    }

    // Validate buffer pointer to prevent null dereference
    if (buf == NULL) {
        LOG_E("TCP: write with NULL buffer");
        return -1;
    }

    // Validate length against buffer size to prevent overflow
    if (len > sizeof(gpServerData->client.writeBuffer)) {
        LOG_E("TCP: write length %lu exceeds buffer", (unsigned long)len);
        return -1;  // Error
    }
    memcpy(gpServerData->client.writeBuffer, buf, len);
    gpServerData->client.writeBufferLength = len;

    // Return number of bytes written on success, negative on error
    // Circular buffer expects this API: return >= 0 (bytes written) or < 0 (error)
    bool flushResult = TcpServerFlush();
    return flushResult ? (int)len : -1;
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
        gpServerData->client.tcpInFlight = 0;
        gpServerData->client.pendingBufferReset = false;
        gpServerData->client.wifiPartialBytesMissing = 0;
        gpServerData->client.wifiWriteBufferRejectedCalls = 0;
        gpServerData->client.wifiWriteBufferRejectedBytes = 0;
        gpServerData->client.inflightHead = 0;
        gpServerData->client.inflightTail = 0;
        gpServerData->client.connGeneration = 0;
        gpServerData->client.pendingClose = false;      // #1073: no close owed at boot
        gpServerData->client.pendingCloseGen = 0;
        gpServerData->client.lastActivityTick = xTaskGetTickCount(); // #663 (idleClosed persists across connections — not reset)
        for (uint8_t i = 0; i < WIFI_TCP_MAX_IN_FLIGHT; i++) {
            gpServerData->client.inflightSizes[i] = 0;
        }
        microrl_init(&gpServerData->client.console, microrl_echo);
        microrl_set_echo(&gpServerData->client.console, false);
        microrl_set_execute_callback(&gpServerData->client.console, microrl_commandComplete);
        gpServerData->client.scpiContext = CreateSCPIContext(&scpi_interface,
                                                             &gpServerData->client,
                                                             &gTcpScpiStorage);
        {
            uint8_t* buf; uint32_t len;
            StreamingBufferPool_GetWifi(&buf, &len);
            CircularBuf_InitExternal(&gpServerData->client.wCirbuf,
                CircularBufferToTcpWrite, buf, len);
        }
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
            int8_t bindRc = bind(gpServerData->serverSocket, (struct sockaddr*) &addr, sizeof (struct sockaddr_in));
            if (bindRc != SOCK_ERR_NO_ERROR) {
                // bind() is async over HIF — non-zero return means the HIF
                // command itself failed (e.g. chip busy, command-queue full).
                // The bind STATUS comes back via SOCKET_MSG_BIND and is
                // handled there — but on HIF failure that callback never
                // fires, so we must clean up here or the WINC-side socket
                // resource leaks until the next reset.  #475: surface the
                // sync-side failure and release the descriptor.
                LOG_E("TCP: bind() HIF send failed sock=%d port=%u rc=%d",
                      gpServerData->serverSocket, (unsigned)port, bindRc);
                // #560 Opt 0: socketOpenFails is also written from
                // SocketEventCallback (WINC driver task) — guard the cross-task RMW.
                taskENTER_CRITICAL();
                gpServerData->socketOpenFails++;  // listener could not bind
                taskEXIT_CRITICAL();
                int8_t shutdownRc = shutdown(gpServerData->serverSocket);
                if (shutdownRc != SOCK_ERR_NO_ERROR) {
                    // If bind's HIF send failed, the chip is likely still
                    // mid-failure, so shutdown's HIF send can fail too.  WINC
                    // shutdown() clears LOCAL socket bookkeeping even on
                    // HIF send failure (winc/drv/socket/socket.c:1012), so
                    // we cannot retry — the WINC-side socket resource may
                    // remain allocated until the next chip reset.  Surface
                    // the cleanup failure so the leak is at least logged.
                    LOG_E("TCP: shutdown(failed-bind sock=%d) failed rc=%d",
                          gpServerData->serverSocket, shutdownRc);
                }
                gpServerData->serverSocket = -1;
            }
        } else {
            // #475: socket() returning negative is silent socket-table
            // exhaustion (WINC max 7 TCP sockets) or HIF send failure.
            // Caller checks serverSocket < 0 and emits its own error,
            // but the root-cause distinction (socket vs bind vs listen)
            // matters for diagnosing the heap-pressure case in #475.
            LOG_E("TCP: socket(AF_INET,SOCK_STREAM) failed rc=%d",
                  gpServerData->serverSocket);
            // #560 Opt 0: cross-task RMW (also written from SocketEventCallback) — guard.
            taskENTER_CRITICAL();
            gpServerData->socketOpenFails++;  // nonzero = WINC TCP-table exhaustion (H2 smoking gun)
            taskEXIT_CRITICAL();
            // Normalize to -1 so callers' `< 0` check is consistent even
            // if WINC starts returning a different negative sentinel.
            gpServerData->serverSocket = -1;
        }
    }
}

// #517-class fix (stale-global audit): reset the in-flight send-size ring so
// inflightHead/inflightTail/inflightSizes never survive a client teardown
// desynced.  Always paired with tcpInFlight=0 in the same wMutex-held region,
// so the invariant "tcpInFlight==0 ⇒ head==tail" holds across every reverse
// transition.  Without it, a disconnect mid-send (head advanced, SOCKET_MSG_SEND
// not yet fired) leaves head!=tail, and the partial-send consumer then reads the
// wrong inflightSizes[] slot on the next connection — mis-attributing
// wifiTcpPartialSends / wifiPartialBytesMissing for the rest of the session.
// Caller must hold client.wMutex.
static inline void ResetInflightRing(void) {
    gpServerData->client.inflightHead = 0;
    gpServerData->client.inflightTail = 0;
    for (uint8_t i = 0; i < WIFI_TCP_MAX_IN_FLIGHT; i++) {
        gpServerData->client.inflightSizes[i] = 0;
    }
}

// #1073: the write-side teardown, in ONE place.  Four sites used to carry a
// verbatim copy of these five statements (CloseSocket, CloseClientSocket, the
// deferred-reset drain, and now the owed-close consumer); a change applied to
// one copy and missed on another is the "fix one site, leave the twin" defect
// class this project has a standing rule against.  Caller must hold
// client.wMutex.
static inline void ResetWriteSideLocked(void) {
    gpServerData->client.writeBufferLength = 0;
    gpServerData->client.tcpInFlight = 0;
    ResetInflightRing();  // #517-class: keep send-size ring in sync with tcpInFlight on teardown
    CircularBuf_Reset(&gpServerData->client.wCirbuf);
    gpServerData->client.pendingBufferReset = false;
}

void wifi_tcp_server_CloseSocket() {
    // The WINC driver's shutdown() automatically closes the socket
    if (gpServerData->client.clientSocket != -1) {
        shutdown(gpServerData->client.clientSocket);
        gpServerData->client.clientSocket = -1;
    }

    if (gpServerData->serverSocket != -1) {
        shutdown(gpServerData->serverSocket);
        gpServerData->serverSocket = -1;
    }

    // Read buffer fields are owned by WifiTask drain — no wMutex needed.
    gpServerData->client.readBufferLength = 0;
    // wCirbuf reset must hold wMutex.  Try non-blocking — if a writer
    // currently holds the mutex, defer the reset to the next wMutex
    // holder via pendingBufferReset.  Never block CloseSocket because
    // it may be called from REINIT paths that need to make progress.
    if (gpServerData->client.wMutex != NULL &&
        xSemaphoreTake(gpServerData->client.wMutex, 0) == pdTRUE) {
        ResetWriteSideLocked();
        xSemaphoreGive(gpServerData->client.wMutex);
    } else {
        // Defer: next wMutex holder drains the flag (#437).
        gpServerData->client.pendingBufferReset = true;
    }
    // #1073: the client slot is gone with the listener, so any owed close is
    // moot.  Deliberately NOT cleared here: the owed-close consumer's own
    // staleness test (clientSocket < 0, or a generation that has moved on)
    // drops the record on its next iteration, and letting ONE place decide
    // when the record dies keeps the clearing rule -- "only after the close
    // it describes has completed, or once the connection it named is
    // provably gone" -- stated once instead of three times.
}

// #1073: the body shared by wifi_tcp_server_CloseClientSocket() (any context,
// never blocks) and wifi_tcp_server_ServicePendingClientClose() (app_WifiTask,
// wMutex already held).  Returns true when shutdown() reported a failure, so
// the CALLER logs it -- see the log note at the bottom of this comment.
//
// WHAT MAKES shutdown() SAFE HERE, and why the old gate did not (Qodo PR #1101
// round 4, finding 4).  EVERY send() on clientSocket runs with wMutex held:
// TcpServerFlush() is reached only through CircularBufferToTcpWrite(), which
// exists only as wCirbuf's process callback, which is invoked only from
// CircularBuf_ProcessBytes() inside wifi_tcp_server_TransmitBufferedData()'s
// wMutex region.  So holding wMutex across the shutdown() below is precisely
// what makes a synchronous close mutually exclusive with a synchronous send on
// the same fd -- the #452 HIF re-entrancy in which socket.c:1013 memsets
// gastrSockets[sock] underneath a send that is still reading its u16SessionID.
//
// The two #1073 call sites used to sample Streaming_IsActiveOnWifiInterface()
// and then close.  That is a check-then-act -- a stream can arm between the two
// statements, and streaming_Task (pri 6) preempts both app_WifiTask (pri 2) and
// the WINC driver task (pri 1) -- and it also names only ONE of the producers:
// a TCP SCPI reply, an async SD GET/LIST reply (#599) and the SOCKET_MSG_SEND
// re-arm chain all reach send() with no stream running at all.  wMutex covers
// every one of them, which is why it, and not the streaming predicate, is the
// thing this function relies on.  The predicate survives one level up as
// POLICY ("do not tear down a live stream's transport"), where being one
// instruction stale is harmless.
//
// wMutexHeld == false keeps #437's invariant exactly: the take is a 0-timeout
// poll, so a caller on the WINC driver task never blocks.  Acquiring BEFORE
// the shutdown() rather than after it is what extends the protection to the
// shutdown itself, and it costs nothing, because on a miss we do precisely
// what the old code did.
//
// BE CLEAR ABOUT WHAT THAT MISS MEANS, because it is not a guarantee: a miss
// happens exactly when somebody else holds wMutex, which is exactly when a
// send() may be in flight -- so the fallback shutdown() is as exposed to #452
// as every call on this entry point was before #1073.  That is unchanged, not
// introduced, and it is unavoidable for the ONE caller that remains on this
// entry point and cannot wait: the #663 idle watchdog (which must not stall
// the 5 ms WiFi loop; its own #452 exposure is narrower -- it only fires after
// the client has been observed RX/TX-idle, so a send() racing it requires a
// write to begin in the instant between the watchdog's snapshot and its
// shutdown(), not a send already mid-batch).
//
// PR #1101 round 5 (#1073 bench re-fail) moved the THIRD caller --
// SocketEventCallback's SOCKET_MSG_RECV error branch, the site a real peer
// RST/FIN actually takes -- off this entry point and onto
// wifi_tcp_server_RequestClientClose() instead, joining the accept-time and
// post-batch recv-arm-failure sites in wifi_manager.c that already used it.
// It used to call this function directly from the WINC driver task with no
// gate on the miss case at all, which is precisely the #452 hazard this
// comment describes: the round-4 bench acceptance run reproduced it
// (AcceptRefused climbing 0 -> 1 on the immediate reconnect -- proof the slot
// was never actually freed, not merely slow to free).  The two #1073 sites and
// the SOCKET_MSG_RECV error site do NOT use this entry point: they file an
// owed close and the consumer below waits for the mutex, so their close is
// protected unconditionally.  Any future caller that CAN defer rather than
// close synchronously should join them rather than call this.
//
// No LOG_E in here.  LOG_E can block on the logger mutex (Util/Logger.c), and
// it must run neither while wMutex is held (it would park streaming_Task and
// the WINC driver task behind a log write) nor between publishing
// clientSocket = -1 and finishing teardown (round 4, finding 1).
static bool CloseClientSocketBody(bool wMutexHeld) {
    // #1073: shutdown() travels the same HIF path that a failed recv()
    // arm congests, so it can fail for the same reason.  socket.c's
    // shutdown() memsets the driver's socket entry unconditionally, after
    // remapping a failed SOCKET_REQUEST to SOCK_ERR_INVALID
    // (winc/drv/socket/socket.c:1006-1013) -- so the local entry is gone
    // either way: the close can never be retried, while the WINC-side
    // allocation may still be held.  There is nothing to recover here, but
    // discarding the status left no trail at all: repeated failures would
    // walk the chip's socket table down to nothing while looking, from our
    // side, exactly like a clean close.  Report it, mirroring the check the
    // refused-2nd-client site in wifi_manager.c already performs.
    bool shutdownFailed = false;

    // Snapshotted BEFORE anything can move it, so the owed-close record is
    // cleared at the end only if it still names the connection we just tore
    // down.  connGeneration only advances at SOCKET_MSG_ACCEPT, which the
    // one-client policy holds off while clientSocket >= 0 -- so in practice it
    // can move only after the store below, and the test is what keeps a
    // replacement client's OWN owed close from being swallowed by ours.
    const uint32_t genAtEntry = gpServerData->client.connGeneration;

    bool locked = wMutexHeld;
    if (!locked && gpServerData->client.wMutex != NULL) {
        locked = (xSemaphoreTake(gpServerData->client.wMutex, 0) == pdTRUE);
    }

    if (gpServerData->client.clientSocket != -1) {
        // Qodo (PR #1101 round 2): snapshot the status and clear the shared
        // field BEFORE anything that could block. Every writer gates only on
        // clientSocket >= 0 -- so leaving that field published across a wait
        // left it "live" to a concurrent writer on top of an fd that
        // shutdown() had already invalidated.
        int8_t rc = shutdown(gpServerData->client.clientSocket);
        gpServerData->client.clientSocket = -1;
        shutdownFailed = (rc != SOCK_ERR_NO_ERROR);
    }
    // Qodo (PR #1101 round 4, finding 1): the accept callback treats
    // clientSocket == -1 as "slot free" the instant the store above becomes
    // visible, so a replacement client accepted during a block here would have
    // its OWN readBufferLength / wCirbuf / tcpInFlight reset out from under it
    // once this call resumed.  The invariant: no blocking operation may run
    // between publishing clientSocket = -1 and finishing this teardown.
    // Nothing below blocks -- the mutex decision was already made above, and
    // on a miss the deferred-reset flag (#437) is set instead of waiting.
    gpServerData->client.readBufferLength = 0;
    if (locked) {
        ResetWriteSideLocked();
    } else {
        // #437: deferred-reset pattern — never block the WINC driver task
        // (which calls us from SocketEventCallback on RECV(0) events).  The
        // next wMutex holder resets the buffer under lock.  Future writers
        // bail early on clientSocket == -1 (we just set it), so no new writer
        // will start before the deferred reset drains.
        gpServerData->client.pendingBufferReset = true;
    }

    // #1073: the connection the owed-close record named is now gone, so the
    // record goes with it -- and NOT ONE STATEMENT EARLIER.  Clearing it while
    // the close was still in progress would re-open the same window one level
    // down: the consumer's staleness test would see "nothing owed" and a second
    // request could be filed against a slot that is mid-teardown.  Conditional
    // on the generation for the converse reason: if a replacement client was
    // accepted between the store above and here and filed its own request, that
    // request is for a DIFFERENT connection and must survive.
    taskENTER_CRITICAL();
    if (gpServerData->client.pendingClose &&
        gpServerData->client.pendingCloseGen == genAtEntry) {
        gpServerData->client.pendingClose = false;
    }
    taskEXIT_CRITICAL();

    if (locked && !wMutexHeld) {
        xSemaphoreGive(gpServerData->client.wMutex);
    }
    return shutdownFailed;
}

void wifi_tcp_server_CloseClientSocket() {
    if (CloseClientSocketBody(false)) {
        // Qodo (PR #1101 round 4, finding 9 / rule 244853): this helper is
        // reachable from SocketEventCallback on the WINC deferred-driver
        // task, which requires a single static, non-formatted log literal --
        // no socket number, no rc.  Both were already consumed above for this
        // branch's condition; they are not needed in the message itself.
        LOG_E("TCP: shutdown(client sock) failed - WINC may still hold the socket (#1073)");
    }
}

void wifi_tcp_server_RequestClientClose(void) {
    if (gpServerData == NULL) {
        return;
    }
    // One critical section, three memory accesses, no HIF traffic and no wait:
    // callable from the WINC driver task without violating #437.  A no-op with
    // no client connected -- there is no slot to give back, and recording one
    // against connGeneration would let a LATER client inherit the request.
    taskENTER_CRITICAL();
    if (gpServerData->client.clientSocket >= 0) {
        gpServerData->client.pendingCloseGen = gpServerData->client.connGeneration;
        gpServerData->client.pendingClose = true;   // published LAST
    }
    taskEXIT_CRITICAL();
}

bool wifi_tcp_server_ClientCloseIsPending(void) {
    // Single volatile bool -- an aligned byte load, atomic on PIC32MZ.  The
    // caller uses it only to decide whether the (more expensive) consumer is
    // worth running, and the consumer re-tests everything under lock, so a
    // one-instruction-stale read here costs at most one extra 5 ms iteration.
    return (gpServerData != NULL) && gpServerData->client.pendingClose;
}

bool wifi_tcp_server_ServicePendingClientClose(void) {
    if (gpServerData == NULL || gpServerData->client.wMutex == NULL) {
        return false;
    }
    if (!gpServerData->client.pendingClose) {
        return false;       // nothing owed -- the overwhelmingly common case
    }

    // BOUNDED wait, not portMAX_DELAY.  This runs on app_WifiTask, which is
    // allowed to block, but a wedged holder must not wedge the 5 ms WiFi loop
    // with it; on a miss the record stays owed and the next iteration retries,
    // so the only cost of a miss is 5 ms.  10 ms is generous against the
    // longest legitimate hold, which is one send() HIF transaction.
    if (xSemaphoreTake(gpServerData->client.wMutex,
                       pdMS_TO_TICKS(WIFI_TCP_CLOSE_MUTEX_WAIT_MS)) != pdTRUE) {
        return false;
    }

    // Re-test the record under lock: everything the decision rests on
    // (is a close still owed, does it still name the connection that holds the
    // slot) can have moved since the unlocked pre-test above.
    uint32_t gen = 0;
    bool doClose = false;
    bool dropStale = false;
    taskENTER_CRITICAL();
    if (gpServerData->client.pendingClose) {
        gen = gpServerData->client.pendingCloseGen;
        if (gpServerData->client.clientSocket < 0 ||
            gpServerData->client.connGeneration != gen) {
            // The connection this was owed for is already gone -- released by
            // a peer close (SOCKET_MSG_RECV), the #663 idle watchdog or
            // wifi_tcp_server_CloseSocket, and possibly replaced since.  The
            // record must be dropped rather than executed: executing it would
            // close the NEXT, healthy client.  This is also the answer to "can
            // the record get stuck forever" -- every path that frees the slot
            // lands here within one iteration.
            dropStale = true;
        } else {
            doClose = true;
        }
    }
    taskEXIT_CRITICAL();

    bool shutdownFailed = false;
    if (doClose) {
        shutdownFailed = CloseClientSocketBody(true);   // clears the record itself
    } else if (dropStale) {
        taskENTER_CRITICAL();
        // Same generation condition as the body's clear, for the same reason:
        // a request filed for a NEWER connection in the window above is not
        // ours to drop.
        if (gpServerData->client.pendingClose &&
            gpServerData->client.pendingCloseGen == gen) {
            gpServerData->client.pendingClose = false;
        }
        taskEXIT_CRITICAL();
    }

    xSemaphoreGive(gpServerData->client.wMutex);

    if (shutdownFailed) {
        // Logged only after the mutex is released -- see CloseClientSocketBody.
        LOG_E("TCP: shutdown(client sock) failed - WINC may still hold the socket (#1073)");
    }
    return doClose;
}

// #437: helper called at the start of every wMutex critical section.
// Drains a deferred CircularBuf_Reset that CloseClientSocket /
// CloseSocket couldn't perform from a non-blocking context.  Caller
// must hold gpServerData->client.wMutex.
static inline void DrainPendingBufferReset(void) {
    if (gpServerData->client.pendingBufferReset) {
        ResetWriteSideLocked();
    }
}

// #331: used by the WINC idle-gate to back off pacing when a TCP client
// is actively connected. Returns true when a client socket is open.
bool wifi_tcp_server_HasActiveClient(void) {
    return (gpServerData != NULL) && (gpServerData->client.clientSocket >= 0);
}

// #367 diagnostics: bytes queued in the WiFi TCP write circular buffer
// that haven't been drained to send() yet. Streaming_Stop snapshots this
// to reconcile the accounting gap.
uint32_t wifi_tcp_server_GetCircularBufferAvailable(void) {
    if (gpServerData == NULL) return 0;
    return CircularBuf_NumBytesAvailable(&gpServerData->client.wCirbuf);
}

size_t wifi_tcp_server_GetWriteBuffFreeSize() {
    if (gpServerData->client.clientSocket < 0) {
        return 0;
    }

    // Must protect circular buffer access with mutex
    xSemaphoreTake(gpServerData->client.wMutex, portMAX_DELAY);
    DrainPendingBufferReset();
    size_t freeSize = CircularBuf_NumBytesFree(&gpServerData->client.wCirbuf);
    xSemaphoreGive(gpServerData->client.wMutex);

    return freeSize;
}

size_t wifi_tcp_server_WriteBuffer(const char* data, size_t len) {
    size_t bytesAdded = 0;

    if (gpServerData->client.clientSocket < 0) {
        return 0;
    }

    if (len == 0)return 0;

    // Qodo (PR #1101 round 5, finding 1): snapshot the connection identity
    // BEFORE waiting for wMutex, an unlocked read safe for the same reason
    // wifi_tcp_server_GetConnGeneration()'s callers already rely on it
    // (connGeneration is single-writer, bumped only in SocketEventCallback's
    // SOCKET_MSG_ACCEPT). The #1073 deferred-close consumer
    // (wifi_tcp_server_ServicePendingClientClose) can run to completion --
    // including the buffer reset -- while this call is blocked on the take
    // below. Without a POST-lock re-check, bytes that belonged to the
    // connection which was just torn down would get queued into the
    // freshly-reset buffer, and could later reach a REPLACEMENT client once
    // one connects: TcpServerFlush()'s only guard is clientSocket >= 0,
    // which is true again for any new client, not just this one.
    uint32_t generation = wifi_tcp_server_GetConnGeneration();

    // Non-blocking check for buffer space
    // If buffer is full, return 0 immediately instead of blocking
    // This prevents the streaming task from stalling for 10-60ms
    xSemaphoreTake(gpServerData->client.wMutex, portMAX_DELAY);
    DrainPendingBufferReset();
    if (!wifi_tcp_server_ConnIsCurrent(generation)) {
        // The connection this data belonged to is already gone, or a
        // different client now holds the slot -- discard rather than queue
        // stale bytes for whoever that is. Returns 0 exactly like the
        // existing no-space rejection just below, so every caller's
        // established "0 means try again / count as dropped" handling
        // (e.g. Streaming_WriteWithRetry's bounded retry in streaming.c)
        // applies unchanged. Deliberately NOT counted in
        // wifiWriteBufferRejectedCalls/Bytes -- that pair is #371's
        // no-space diagnostic specifically; this is a different rejection
        // reason and conflating them would misattribute it.
        xSemaphoreGive(gpServerData->client.wMutex);
        return 0;
    }
    if (CircularBuf_NumBytesFree(&gpServerData->client.wCirbuf) < len) {
        // #371: count rejections to verify wifiDroppedBytes accounting is working.
        gpServerData->client.wifiWriteBufferRejectedCalls++;
        gpServerData->client.wifiWriteBufferRejectedBytes += (uint32_t)len;
        xSemaphoreGive(gpServerData->client.wMutex);
        return 0;  // No space available - return immediately
    }
    bytesAdded = CircularBuf_AddBytes(&gpServerData->client.wCirbuf, (uint8_t*) data, len);

    // Proactive flush at 30% of send buffer size (~420 bytes).
    // With callback-driven sends, a higher threshold lets more data
    // accumulate per send while the callback chains the next immediately.
    // Benchmarked: 30% optimal with callback sends (125 KB/s PB16@3k,
    // lowest drops at ceiling). Was 10% before callback optimization.
    size_t bytesInBuffer = CircularBuf_NumBytesAvailable(&gpServerData->client.wCirbuf);
    bool shouldFlush = (bytesInBuffer > (WIFI_WBUFFER_SIZE * 3 / 10));
    xSemaphoreGive(gpServerData->client.wMutex);

    // Trigger flush outside mutex to avoid blocking other writers.
    // #362 Step C: drain when WINC has any free in-flight slot, not just
    // when zero in-flight. Lets streaming task queue a 2nd send while the
    // first is still being radio-TXed by WINC, instead of waiting for
    // SOCKET_MSG_SEND callback delivery latency between every packet.
    if (shouldFlush && gpServerData->client.tcpInFlight < WIFI_TCP_MAX_IN_FLIGHT) {
        wifi_tcp_server_TransmitBufferedData();
    }

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
    // #362 Step C: bail when WINC's HIF queue is at our cap.  Re-entry
    // (from streaming_Task WriteBuffer trigger or WDRV_WINC_Tasks
    // SOCKET_MSG_SEND chain) drains one packet per call and increments
    // tcpInFlight in TcpServerFlush — the callback is what decrements it.
    if (gpServerData->client.tcpInFlight >= WIFI_TCP_MAX_IN_FLIGHT) {
        return false;
    }

    // Check if data available with mutex protection
    xSemaphoreTake(gpServerData->client.wMutex, portMAX_DELAY);
    DrainPendingBufferReset();
    bool hasData = (CircularBuf_NumBytesAvailable(&gpServerData->client.wCirbuf) > 0);
    if (hasData) {
        CircularBuf_ProcessBytes(&gpServerData->client.wCirbuf, NULL, WIFI_WBUFFER_SIZE, &ret);
    }
    xSemaphoreGive(gpServerData->client.wMutex);
    return true;
}

bool wifi_tcp_server_ResizeWriteBuffer(uint32_t newSize) {
    if (gpServerData == NULL) return false;
    if (newSize < WIFI_WBUFFER_SIZE) newSize = WIFI_WBUFFER_SIZE;
    if (gpServerData->client.wCirbuf.buf_size == newSize) return true;

    // Pool-managed — actual swap via wifi_tcp_server_SetWriteBuffer
    return true;
}

void wifi_tcp_server_SetWriteBuffer(uint8_t* buf, uint32_t size) {
    if (gpServerData == NULL || buf == NULL || size == 0) return;

    // Wait for any in-flight TCP sends to drain before swapping buffers
    TickType_t start = xTaskGetTickCount();
    while (gpServerData->client.tcpInFlight) {
        if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(1000)) {
            LOG_E("WiFi buffer swap: TCP send stuck, proceeding anyway");
            break;
        }
        vTaskDelay(1);
    }

    xSemaphoreTake(gpServerData->client.wMutex, portMAX_DELAY);
    // Buffer swap completely replaces wCirbuf state below — any pending
    // deferred reset is implicitly satisfied.  Clear the flag.
    gpServerData->client.pendingBufferReset = false;

    uint32_t oldSize = gpServerData->client.wCirbuf.buf_size;
    gpServerData->client.wCirbuf.buf_ptr = buf;
    gpServerData->client.wCirbuf.buf_size = size;
    gpServerData->client.wCirbuf.insertPtr = buf;
    gpServerData->client.wCirbuf.removePtr = buf;
    gpServerData->client.wCirbuf.producedBytes = 0;
    gpServerData->client.wCirbuf.consumedBytes = 0;
    gpServerData->client.wCirbuf._ownsMemory = false;

    xSemaphoreGive(gpServerData->client.wMutex);

    LOG_I("WiFi circular buffer: %u -> %u bytes", (unsigned)oldSize, (unsigned)size);
}
