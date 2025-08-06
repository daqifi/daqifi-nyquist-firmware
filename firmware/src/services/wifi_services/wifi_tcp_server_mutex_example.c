// Example showing how to update wifi_tcp_server.c with the new mutex helpers
// This file shows the before and after for documentation purposes

// BEFORE (current code with repetitive mutex handling):
size_t wifi_tcp_server_GetWriteBuffFreeSize() {
    if (gpServerData->client.clientSocket < 0) {
        return 0;
    }

    // BEFORE: Manual mutex handling
    xSemaphoreTake(gpServerData->client.wMutex, portMAX_DELAY);
    size_t freeSize = CircularBuf_NumBytesFree(&gpServerData->client.wCirbuf);
    xSemaphoreGive(gpServerData->client.wMutex);
    
    return freeSize;
}

// AFTER (using helper function):
size_t wifi_tcp_server_GetWriteBuffFreeSize_NEW() {
    if (gpServerData->client.clientSocket < 0) {
        return 0;
    }

    // AFTER: Clean one-liner with mutex handling built-in
    return CircularBuf_GetFreeSize_Safe_SizeT(&gpServerData->client.wCirbuf, 
                                              gpServerData->client.wMutex);
}

// BEFORE (complex write with race condition):
size_t wifi_tcp_server_WriteBuffer(const char* data, size_t len) {
    size_t bytesAdded = 0;

    if (gpServerData->client.clientSocket < 0) {
        return 0;
    }

    if (len == 0)return 0;

    // BEFORE: Complex waiting loop with potential race condition
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

// AFTER (atomic operation, no race condition):
size_t wifi_tcp_server_WriteBuffer_NEW(const char* data, size_t len) {
    if (gpServerData->client.clientSocket < 0) {
        return 0;
    }

    if (len == 0) return 0;

    // AFTER: Single function call handles waiting, checking, and writing atomically
    return CircularBuf_TryAddBytes_Safe(&gpServerData->client.wCirbuf, 
                                       gpServerData->client.wMutex,
                                       (uint8_t*) data, len,
                                       portMAX_DELAY,  // No timeout
                                       10);            // 10ms wait interval
}

// BEFORE (transmit with separate check and process):
bool wifi_tcp_server_TransmitBufferedData() {
    int ret;
    UNUSED(ret);
    if (gpServerData->client.tcpSendPending != 0) {
        return false;
    }
  
    // BEFORE: Separate check and process with potential race
    if (CircularBuf_NumBytesAvailable(&gpServerData->client.wCirbuf) > 0) {
        xSemaphoreTake(gpServerData->client.wMutex, portMAX_DELAY);
        CircularBuf_ProcessBytes(&gpServerData->client.wCirbuf, NULL, WIFI_WBUFFER_SIZE, &ret);
        xSemaphoreGive(gpServerData->client.wMutex);
    }
    return true;
}

// AFTER (atomic check-and-process):
bool wifi_tcp_server_TransmitBufferedData_NEW() {
    int ret;
    UNUSED(ret);
    if (gpServerData->client.tcpSendPending != 0) {
        return false;
    }
  
    // AFTER: Atomic check-and-process prevents race conditions
    CircularBuf_ProcessIfAvailable_Safe(&gpServerData->client.wCirbuf,
                                        gpServerData->client.wMutex,
                                        NULL, WIFI_WBUFFER_SIZE, &ret);
    return true;
}