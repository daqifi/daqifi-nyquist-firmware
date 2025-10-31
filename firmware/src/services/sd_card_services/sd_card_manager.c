#define LOG_LVL LOG_LEVEL_SD

#include <string.h>
#include "Util/Logger.h"
#include "sd_card_manager.h"

#define SD_CARD_MANAGER_CIRCULAR_BUFFER_SIZE (64 * 1024)  // 64KB buffer (reduced to free heap for streaming)
#define SD_CARD_MANAGER_FILE_PATH_LEN_MAX (SYS_FS_FILE_NAME_LEN*2)
#define SD_CARD_MANAGER_DISK_MOUNT_NAME    "/mnt/DAQiFi"
#define SD_CARD_MANAGER_DISK_DEV_NAME      "/dev/mmcblka1"

typedef enum {
    SD_CARD_MANAGER_PROCESS_STATE_INIT,
    SD_CARD_MANAGER_PROCESS_STATE_MOUNT_DISK,
    SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK,
    SD_CARD_MANAGER_PROCESS_STATE_CURRENT_DRIVE,
    SD_CARD_MANAGER_PROCESS_STATE_CREATE_DIRECTORY,
    SD_CARD_MANAGER_PROCESS_STATE_OPEN_FILE,
    SD_CARD_MANAGER_PROCESS_STATE_WRITE_TO_FILE,
    SD_CARD_MANAGER_PROCESS_STATE_READ_FROM_FILE,
    SD_CARD_MANAGER_PROCESS_STATE_LIST_DIR,
    SD_CARD_MANAGER_PROCESS_STATE_DELETE_FILE,
    SD_CARD_MANAGER_PROCESS_STATE_FORMAT,
    SD_CARD_MANAGER_PROCESS_STATE_DEINIT,
    SD_CARD_MANAGER_PROCESS_STATE_IDLE,
    SD_CARD_MANAGER_PROCESS_STATE_ERROR,
} sd_card_manager_processState_t;

typedef struct {
    sd_card_manager_processState_t currentProcessState;
    /** Client read buffer */
    uint8_t readBuffer[SD_CARD_MANAGER_CONF_RBUFFER_SIZE] __attribute__((coherent));

    /** The current length of the read buffer */
    size_t readBufferLength;

    /** Client write buffer */
    uint8_t writeBuffer[SD_CARD_MANAGER_CONF_WBUFFER_SIZE]__attribute__((coherent));

    /** The current length of the write buffer */
    size_t writeBufferLength;

    CircularBuf_t wCirbuf;

    SemaphoreHandle_t wMutex;
    SemaphoreHandle_t opCompleteSemaphore;  // Signals when async operations complete

    char filePath[SD_CARD_MANAGER_FILE_PATH_LEN_MAX + 1];

    SYS_FS_HANDLE fileHandle;

    bool sdCardWritePending;
    uint16_t sdCardWriteBufferOffset;
    uint16_t totalBytesFlushPending;
    uint64_t lastFlushMillis;
    bool discMounted;
} sd_card_manager_context_t;

sd_card_manager_context_t gSdCardData;
sd_card_manager_settings_t *gpSdCardSettings;

void __attribute__((weak)) sd_card_manager_DataReadyCB(sd_card_manager_mode_t mode, uint8_t *pDataBuff, size_t dataLen) {

}

static int SDCardWrite() {
    int writeLen = -1;
    if (gSdCardData.fileHandle == SYS_FS_HANDLE_INVALID) {
        goto __exit;
    }
   
    writeLen = SYS_FS_FileWrite(gSdCardData.fileHandle,
            (const void *) (gSdCardData.writeBuffer + gSdCardData.sdCardWriteBufferOffset),
            gSdCardData.writeBufferLength);
__exit:
    return writeLen;
}

static int CircularBufferToSDWrite(uint8_t* buf, uint32_t len) {
    if (len>sizeof (gSdCardData.writeBuffer))
        return -1;  // Error: buffer overflow
    memcpy(gSdCardData.writeBuffer, buf, len);
    gSdCardData.writeBufferLength = len;
    gSdCardData.sdCardWriteBufferOffset = 0;
    return SDCardWrite();
}

/**
 * @brief Recursively lists files and directories, storing the output in a buffer.
 *
 * This function traverses the directory specified by `dirPath`, listing all files and directories
 * within it, including those in subdirectories. The output is formatted and stored in the buffer
 * pointed to by `pStrBuff`. It adjusts the buffer pointer and size during recursive calls to
 * ensure that data is appended correctly without overwriting existing content.
 *
 * @param[in]  dirPath     The path of the directory to list.
 * @param[out] pStrBuff    Pointer to the buffer where the output will be stored.
 *                         The buffer should be large enough to hold the expected output.
 * @param[in]  strBuffSize The total size of the buffer pointed to by `pStrBuff`.
 *
 * @return The total number of bytes written to `pStrBuff`.
 *
 * @note
 * - The function uses recursion to traverse subdirectories.
 * - It maintains a local `strBuffIndex` to keep track of the current position in the buffer.
 * - During recursive calls, the buffer pointer `pStrBuff` and buffer size `strBuffSize` are
 *   adjusted to prevent buffer overflows.
 * - If the buffer becomes full during execution, the function stops writing further data to prevent overflow.
 *
 * @warning
 * - Ensure that `strBuffSize` is sufficient to hold the entire output; otherwise, buffer overflows
 *   or incomplete output may occur.
 * - Be cautious with deeply nested directories, as excessive recursion can lead to stack overflow.
 *
 * @example
 * @code
 * uint8_t outputBuffer[1024];
 * size_t totalBytes = ListFilesInDirectory("/mnt/myDrive", outputBuffer, sizeof(outputBuffer));
 *
 * // Ensure null termination if needed
 * if (totalBytes < sizeof(outputBuffer)) {
 *     outputBuffer[totalBytes] = '\0';
 * } else {
 *     outputBuffer[sizeof(outputBuffer) - 1] = '\0'; // Ensure null termination
 * }
 *
 * printf("%s", outputBuffer);
 * @endcode
 */
// Callback function type for sending directory listing chunks
typedef void (*ListChunkCallback)(const uint8_t* data, size_t len);

// Static callback for sending directory listing chunks
static void sd_listdir_send_chunk(const uint8_t* data, size_t len) {
    if (len > 0) {
        LOG_D("[SD] Sending chunk: %d bytes\r\n", (int)len);
        sd_card_manager_DataReadyCB(SD_CARD_MANAGER_MODE_LIST_DIRECTORY, (uint8_t*)data, len);
    }
}

static void ListFilesInDirectoryChunked(const char* dirPath, uint8_t *pStrBuff, size_t strBuffSize, ListChunkCallback sendChunk) {
    SYS_FS_FSTAT stat;
    size_t strBuffIndex = 0;
    SYS_FS_HANDLE dirHandle;
    char newPath[SD_CARD_MANAGER_FILE_PATH_LEN_MAX + 1];

    memset(newPath, 0, sizeof (newPath));
    memset(&stat, 0, sizeof (stat));
    LOG_D("[SD] ListFiles: Opening directory '%s'\r\n", dirPath);
    dirHandle = SYS_FS_DirOpen(dirPath);
    if (dirHandle == SYS_FS_HANDLE_INVALID) {
        SYS_FS_ERROR err = SYS_FS_Error();
        LOG_E("[SD] ListFiles: Failed to open directory '%s', error=%d\r\n", dirPath, err);
        strBuffIndex += snprintf((char *) pStrBuff + strBuffIndex, strBuffSize - strBuffIndex,
                "\r\n[Error:%d]Failed to open directory [%s]\r\n", err, dirPath);
        if (strBuffIndex > 0 && sendChunk) {
            sendChunk(pStrBuff, strBuffIndex);
        }
        return;
    }

    while (true) {
        if (SYS_FS_DirRead(dirHandle, &stat) == SYS_FS_RES_FAILURE) {
            SYS_FS_ERROR err = SYS_FS_Error();
            strBuffIndex += snprintf((char *) pStrBuff + strBuffIndex, strBuffSize - strBuffIndex,
                    "\r\n[Error:%d]Failed to read directory\r\n", err);
            break;
        }

        if (stat.fname[0] == '\0') {
            LOG_D("[SD] ListFiles: End of directory\r\n");
            break;
        }

        LOG_D("[SD] ListFiles: Read entry '%s'\r\n", stat.fname);

        if (strcmp(stat.fname, ".") == 0 || strcmp(stat.fname, "..") == 0) {
            continue;
        }

        snprintf(newPath, SD_CARD_MANAGER_FILE_PATH_LEN_MAX, "%s/%s", dirPath, stat.fname);

        if (stat.fattrib & SYS_FS_ATTR_DIR) {
            // For subdirectories, recursively list (skip for now to keep simple)
            LOG_D("[SD] ListFiles: Skipping subdirectory '%s'\r\n", newPath);
        } else {
            LOG_D("[SD] ListFiles: Found file '%s'\r\n", newPath);
            int n = snprintf(NULL, 0, "%s\r\n", newPath);  // Calculate length needed

            // Check if buffer is getting full - need space for this entry
            if (n > 0 && (strBuffIndex + n) >= (strBuffSize - 4)) {
                // Send current chunk before adding this entry
                if (sendChunk && strBuffIndex > 0) {
                    pStrBuff[strBuffIndex] = '\0';
                    sendChunk(pStrBuff, strBuffIndex);
                    strBuffIndex = 0;  // Reset buffer for next chunk
                }
            }

            // Now add the filename to buffer (either current or freshly reset)
            n = snprintf((char *) pStrBuff + strBuffIndex, strBuffSize - strBuffIndex,
                    "%s\r\n", newPath);
            if (n > 0 && (size_t)n < strBuffSize - strBuffIndex) {
                strBuffIndex += n;
            }
        }
    }

    // Send final chunk if any data remains
    if (sendChunk && strBuffIndex > 0) {
        // Remove trailing CRLF from final chunk to avoid extra blank line before prompt
        while (strBuffIndex > 0 && (pStrBuff[strBuffIndex - 1] == '\r' || pStrBuff[strBuffIndex - 1] == '\n')) {
            strBuffIndex--;
        }
        if (strBuffIndex > 0) {
            pStrBuff[strBuffIndex] = '\0';
            sendChunk(pStrBuff, strBuffIndex);
        }
    }

    SYS_FS_DirClose(dirHandle);
}

bool sd_card_manager_Init(sd_card_manager_settings_t *pSettings) {
    static bool isInitDone = false;
    if (!isInitDone) {
        CircularBuf_Init(&gSdCardData.wCirbuf,
                CircularBufferToSDWrite,
                SD_CARD_MANAGER_CIRCULAR_BUFFER_SIZE);
        gSdCardData.wMutex = xSemaphoreCreateMutex();
        xSemaphoreGive(gSdCardData.wMutex);
        gSdCardData.opCompleteSemaphore = xSemaphoreCreateBinary();
        isInitDone = true;
        gpSdCardSettings = pSettings;
        gSdCardData.fileHandle = SYS_FS_HANDLE_INVALID;
        gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_INIT;
    }
    return true;
}

void sd_card_manager_ProcessState() {
    /* Check the application's current state. */

    switch (gSdCardData.currentProcessState) {
        case SD_CARD_MANAGER_PROCESS_STATE_INIT:
            // Only initialize if SD is enabled AND has a valid operation mode
            // Just enabling SD without setting a mode (WRITE/READ/LIST) is valid - don't spam errors
            if (gpSdCardSettings->enable && gpSdCardSettings->mode != SD_CARD_MANAGER_MODE_NONE) {
                // Validate directory and file settings
                bool dirValid = strlen(gpSdCardSettings->directory) > 0 &&
                               strlen(gpSdCardSettings->directory) <= SD_CARD_MANAGER_CONF_DIR_NAME_LEN_MAX;
                bool fileValid = strlen(gpSdCardSettings->file) > 0 &&
                                strlen(gpSdCardSettings->file) <= SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX;

                // LIST and FORMAT modes don't need a filename
                // DELETE, READ, WRITE need a filename
                bool needsFile = (gpSdCardSettings->mode == SD_CARD_MANAGER_MODE_DELETE_FILE ||
                                 gpSdCardSettings->mode == SD_CARD_MANAGER_MODE_READ ||
                                 gpSdCardSettings->mode == SD_CARD_MANAGER_MODE_WRITE);

                // Reset error flag on valid configuration (allows new errors to be logged after fix)
                static bool errorLogged = false;

                if (dirValid && (!needsFile || fileValid)) {
                    errorLogged = false;  // Reset flag on successful validation
                    gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_MOUNT_DISK;
                } else {
                    // Only log error once per enable, not continuously
                    if (!errorLogged) {
                        LOG_E("[%s:%d]Invalid SD Card Directory or file name (dir='%s', file='%s')",
                              __FILE__, __LINE__,
                              gpSdCardSettings->directory,
                              gpSdCardSettings->file);
                        errorLogged = true;
                    }
                }
            }
            break;
        case SD_CARD_MANAGER_PROCESS_STATE_MOUNT_DISK:
            gSdCardData.sdCardWritePending = 0;
            gSdCardData.writeBufferLength = 0;
            gSdCardData.sdCardWriteBufferOffset = 0;
            if (SYS_FS_Mount(SD_CARD_MANAGER_DISK_DEV_NAME, SD_CARD_MANAGER_DISK_MOUNT_NAME, FAT, 0, NULL) != 0) {
                /* The disk could not be mounted. Try
                 * mounting again until success. */
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_MOUNT_DISK;
            } else {
                /* Mount was successful. Unmount the disk, for testing. */
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_CURRENT_DRIVE;
                gSdCardData.discMounted = true;
            }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK:
            if (gSdCardData.fileHandle != SYS_FS_HANDLE_INVALID) {
                // Flush any pending data before closing to prevent data loss
                xSemaphoreTake(gSdCardData.wMutex, portMAX_DELAY);
                bool hasPendingData = gSdCardData.totalBytesFlushPending > 0;
                xSemaphoreGive(gSdCardData.wMutex);

                if (hasPendingData) {
                    LOG_D("[SD] Flushing %d bytes before unmount\r\n", (int)gSdCardData.totalBytesFlushPending);
                    if (SYS_FS_FileSync(gSdCardData.fileHandle) != -1) {
                        xSemaphoreTake(gSdCardData.wMutex, portMAX_DELAY);
                        gSdCardData.totalBytesFlushPending = 0;
                        xSemaphoreGive(gSdCardData.wMutex);
                        LOG_D("[SD] Flushed pending data before unmount\r\n");
                    } else {
                        LOG_E("[%s:%d]Error flushing before unmount", __FILE__, __LINE__);
                    }
                }

                LOG_D("[SD] Closing file '%s'\r\n", gSdCardData.filePath);
                SYS_FS_FileClose(gSdCardData.fileHandle);
                gSdCardData.fileHandle = SYS_FS_HANDLE_INVALID;
            }
            if (SYS_FS_Unmount(SD_CARD_MANAGER_DISK_MOUNT_NAME) == 0) {
                gSdCardData.discMounted = false;
                LOG_D("[SD] Unmounted successfully\r\n");
            }
            if (gSdCardData.discMounted == true) {
                /* The disk could not be un mounted. Try
                 * un mounting again untill success. */
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK;
            } else {
                // Always go back to INIT after unmounting
                // INIT will decide whether to mount based on enable flag and mode
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_INIT;
            }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_CURRENT_DRIVE:
            if (SYS_FS_CurrentDriveSet(SD_CARD_MANAGER_DISK_MOUNT_NAME) == SYS_FS_RES_FAILURE) {
                /* Error while setting current drive */
                LOG_E("[%s:%d]Error Setting SD Card drive", __FILE__, __LINE__);
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
            } else {
                /* Open a file for reading. */
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_CREATE_DIRECTORY;
            }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_CREATE_DIRECTORY:
            if (SYS_FS_DirectoryMake(gpSdCardSettings->directory) == SYS_FS_RES_FAILURE) {
                if (SYS_FS_Error() == SYS_FS_ERROR_EXIST) {
                    LOG_D("[SD] Directory '%s' already exists\r\n", gpSdCardSettings->directory);
                    gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_OPEN_FILE;
                } else {
                    gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                    LOG_E("[%s:%d]Invalid SD Card Directory name '%s'", __FILE__, __LINE__, gpSdCardSettings->directory);
                }
                /* Error while creating a new drive */
            } else {
                LOG_D("[SD] Created directory '%s'\r\n", gpSdCardSettings->directory);
                /* Open a file for writing. */
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_OPEN_FILE;
            }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_OPEN_FILE:
            memset(gSdCardData.filePath, 0, sizeof (gSdCardData.filePath));
            snprintf(gSdCardData.filePath, SD_CARD_MANAGER_FILE_PATH_LEN_MAX, "%s/%s",
                    gpSdCardSettings->directory, gpSdCardSettings->file);
            LOG_D("[SD] Opening file: '%s', mode=%d\r\n", gSdCardData.filePath, gpSdCardSettings->mode);
            if (gpSdCardSettings->mode == SD_CARD_MANAGER_MODE_WRITE) {
                // Use WRITE_PLUS to create/truncate file (overwrite mode)
                gSdCardData.fileHandle = SYS_FS_FileOpen(gSdCardData.filePath,
                        (SYS_FS_FILE_OPEN_WRITE_PLUS));
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_WRITE_TO_FILE;
                gSdCardData.totalBytesFlushPending = 0;
                gSdCardData.lastFlushMillis = pdTICKS_TO_MS(xTaskGetTickCount());

                if (gSdCardData.fileHandle == SYS_FS_HANDLE_INVALID) {
                    /* Could not open the file. Error out*/
                    gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                    LOG_E("[%s:%d]Failed to open SD Card file for writing: '%s'", __FILE__, __LINE__, gSdCardData.filePath);
                }
            } else if (gpSdCardSettings->mode == SD_CARD_MANAGER_MODE_READ) {
                gSdCardData.fileHandle = SYS_FS_FileOpen(gSdCardData.filePath,
                        (SYS_FS_FILE_OPEN_READ));
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_READ_FROM_FILE;

                if (gSdCardData.fileHandle == SYS_FS_HANDLE_INVALID) {
                    /* Could not open the file. Error out*/
                    gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                    LOG_E("[%s:%d]Failed to open SD Card file for reading: '%s'", __FILE__, __LINE__, gSdCardData.filePath);
                }
            } else if (gpSdCardSettings->mode == SD_CARD_MANAGER_MODE_LIST_DIRECTORY) {
                // LIST mode doesn't need to open a file, just list the directory
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_LIST_DIR;
            } else if (gpSdCardSettings->mode == SD_CARD_MANAGER_MODE_DELETE_FILE) {
                // DELETE mode - delete the specified file
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_DELETE_FILE;
            } else if (gpSdCardSettings->mode == SD_CARD_MANAGER_MODE_FORMAT) {
                // FORMAT mode - erase all files on SD card
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_FORMAT;
            } else if (gpSdCardSettings->mode == SD_CARD_MANAGER_MODE_NONE) {
                gSdCardData.fileHandle = SYS_FS_FileOpen(gSdCardData.filePath,
                        (SYS_FS_FILE_OPEN_READ));
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;

                if (gSdCardData.fileHandle == SYS_FS_HANDLE_INVALID) {
                    /* Could not open the file. Error out*/
                    gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                    LOG_E("[%s:%d]Failed to open SD Card file: '%s'", __FILE__, __LINE__, gSdCardData.filePath);
                }
            }
            break;
        case SD_CARD_MANAGER_PROCESS_STATE_WRITE_TO_FILE:
        {
            /* If read was success, try writing to the new file */
            int writeLen = -2;
            
            // Process multiple chunks per cycle for better throughput
            int chunksProcessed = 0;
            
            while (chunksProcessed < SD_CARD_MANAGER_MAX_CHUNKS_PER_CYCLE) {
                xSemaphoreTake(gSdCardData.wMutex, portMAX_DELAY);
                if (CircularBuf_NumBytesAvailable(&gSdCardData.wCirbuf) > 0
                        && gSdCardData.sdCardWritePending != 1) {
                    gSdCardData.sdCardWritePending = 1;
                    CircularBuf_ProcessBytes(&gSdCardData.wCirbuf, NULL, SD_CARD_MANAGER_CONF_WBUFFER_SIZE, &writeLen);
                    gSdCardData.totalBytesFlushPending += gSdCardData.writeBufferLength;
                    xSemaphoreGive(gSdCardData.wMutex);
                    chunksProcessed++;
                } else {
                    // No data available or write already pending, release mutex
                    xSemaphoreGive(gSdCardData.wMutex);
                    
                    // Note: sdCardWritePending check outside mutex is safe here because:
                    // 1. Only this task modifies sdCardWritePending
                    // 2. We're coordinating work within a single task, not between tasks
                    // If this changes in future, this will need mutex protection
                    if (gSdCardData.sdCardWritePending == 1) {
                        writeLen = SDCardWrite();
                        if (writeLen >= gSdCardData.writeBufferLength) {
                            xSemaphoreTake(gSdCardData.wMutex, portMAX_DELAY);
                            gSdCardData.sdCardWritePending = 0;
                            gSdCardData.writeBufferLength = 0;
                            gSdCardData.sdCardWriteBufferOffset = 0;
                            xSemaphoreGive(gSdCardData.wMutex);
                        } else if (writeLen >= 0) {
                            xSemaphoreTake(gSdCardData.wMutex, portMAX_DELAY);
                            gSdCardData.writeBufferLength -= writeLen;
                            gSdCardData.sdCardWriteBufferOffset = writeLen;
                            xSemaphoreGive(gSdCardData.wMutex);
                            break;  // Partial write, don't process more chunks
                        } else if (writeLen == -1) {
                            gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                            LOG_E("[%s:%d]Error Writing to SD Card", __FILE__, __LINE__);
                            break;
                        }
                    } else {
                        break;  // No more data to process
                    }
                }
            }
            uint64_t currentMillis = pdTICKS_TO_MS(xTaskGetTickCount());

            xSemaphoreTake(gSdCardData.wMutex, portMAX_DELAY);
            bool needsFlush = (currentMillis - gSdCardData.lastFlushMillis > 5000 ||
                    gSdCardData.totalBytesFlushPending > 4096) && 
                    gSdCardData.totalBytesFlushPending > 0;
            if (needsFlush) {
                xSemaphoreGive(gSdCardData.wMutex);
                
                if (SYS_FS_FileSync(gSdCardData.fileHandle) == -1) {
                    gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                    LOG_E("[%s:%d]Error flushing to SD Card", __FILE__, __LINE__);
                } else {
                    // Only reset counter after successful flush
                    xSemaphoreTake(gSdCardData.wMutex, portMAX_DELAY);
                    gSdCardData.totalBytesFlushPending = 0;
                    gSdCardData.lastFlushMillis = currentMillis;
                    xSemaphoreGive(gSdCardData.wMutex);
                }
            } else {
                xSemaphoreGive(gSdCardData.wMutex);
            }

        }
            break;
        case SD_CARD_MANAGER_PROCESS_STATE_READ_FROM_FILE:
        {
            size_t bytesRead = 0;
            gSdCardData.readBufferLength = 0;
            bytesRead = SYS_FS_FileRead(gSdCardData.fileHandle, gSdCardData.readBuffer, SD_CARD_MANAGER_CONF_RBUFFER_SIZE - 1);

            if (bytesRead == (size_t) - 1) {
                gSdCardData.readBufferLength = sprintf((char*) gSdCardData.readBuffer,
                        "%s", "\r\nError!! Reading SD Card\r\n");
                sd_card_manager_DataReadyCB(SD_CARD_MANAGER_MODE_READ,
                        gSdCardData.readBuffer,
                        gSdCardData.readBufferLength);
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
            } else if (bytesRead == 0) {
                //End of File
                gSdCardData.readBufferLength = sprintf((char*) gSdCardData.readBuffer,
                        "%s", "\r\n\n__END_OF_FILE__\r\n\n");
                sd_card_manager_DataReadyCB(SD_CARD_MANAGER_MODE_READ,
                        gSdCardData.readBuffer,
                        gSdCardData.readBufferLength);
                gSdCardData.readBufferLength = 0;
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;

            } else {
                gSdCardData.readBufferLength = bytesRead;
                sd_card_manager_DataReadyCB(SD_CARD_MANAGER_MODE_READ,
                        gSdCardData.readBuffer,
                        gSdCardData.readBufferLength);

            }
        }
            break;
        case SD_CARD_MANAGER_PROCESS_STATE_LIST_DIR:
        {
            LOG_D("[SD] Listing directory: '%s'\r\n", gpSdCardSettings->directory);

            // List files in chunks using static callback
            ListFilesInDirectoryChunked(
                    gpSdCardSettings->directory,
                    gSdCardData.readBuffer,
                    SD_CARD_MANAGER_CONF_RBUFFER_SIZE,
                    sd_listdir_send_chunk);

            gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
            // Signal completion
            xSemaphoreGive(gSdCardData.opCompleteSemaphore);
        }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_DELETE_FILE:
        {
            LOG_D("[SD] Deleting file: '%s'\r\n", gSdCardData.filePath);

            // Delete the file
            if (SYS_FS_FileDirectoryRemove(gSdCardData.filePath) == SYS_FS_RES_SUCCESS) {
                LOG_D("[SD] File deleted successfully\r\n");
            } else {
                SYS_FS_ERROR err = SYS_FS_Error();
                LOG_E("[SD] Failed to delete file '%s', error=%d\r\n", gSdCardData.filePath, err);
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                // Signal completion even on error
                xSemaphoreGive(gSdCardData.opCompleteSemaphore);
                break;
            }

            gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
            // Signal completion
            xSemaphoreGive(gSdCardData.opCompleteSemaphore);
        }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_FORMAT:
        {
            LOG_D("[SD] Formatting SD card at '%s'\r\n", SD_CARD_MANAGER_DISK_MOUNT_NAME);

            // Format the drive using FAT filesystem
            // Use SYS_FS_FORMAT_ANY to auto-select FAT16/FAT32 based on card size
            SYS_FS_FORMAT_PARAM opt = {
                .fmt = SYS_FS_FORMAT_ANY,
                .au_size = 0  // Auto-select allocation unit size
            };

            // Work buffer for format operation (512 bytes required)
            static uint8_t formatWorkBuffer[512];

            if (SYS_FS_DriveFormat(SD_CARD_MANAGER_DISK_MOUNT_NAME, &opt, formatWorkBuffer, sizeof(formatWorkBuffer)) == SYS_FS_RES_SUCCESS) {
                LOG_D("[SD] Format completed successfully\r\n");
            } else {
                SYS_FS_ERROR err = SYS_FS_Error();
                LOG_E("[SD] Format failed, error=%d\r\n", err);
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                // Signal completion even on error
                xSemaphoreGive(gSdCardData.opCompleteSemaphore);
                break;
            }

            gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
            // Signal completion
            xSemaphoreGive(gSdCardData.opCompleteSemaphore);
        }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_IDLE:

            break;
        case SD_CARD_MANAGER_PROCESS_STATE_DEINIT:
            gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK;
            break;
        case SD_CARD_MANAGER_PROCESS_STATE_ERROR:
            /* The application comes here when the demo has failed. */
            gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK;
            break;

        default:
            break;
    }
}

size_t sd_card_manager_WriteToBuffer(const char* pData, size_t len) {
    size_t bytesAdded = 0;
    if (len == 0)return 0;
    if (gpSdCardSettings->enable != 1 || gpSdCardSettings->mode != SD_CARD_MANAGER_MODE_WRITE) {
        return 0;
    }
    
    // Wait for buffer space with mutex protection and timeout
    bool hasSpace = false;
    TickType_t startTime = xTaskGetTickCount();
    TickType_t timeoutTicks = pdMS_TO_TICKS(SD_CARD_MANAGER_WRITE_TIMEOUT_MS);
    
    while (!hasSpace) {
        // Check buffer space with mutex protection
        xSemaphoreTake(gSdCardData.wMutex, portMAX_DELAY);
        hasSpace = (CircularBuf_NumBytesFree(&gSdCardData.wCirbuf) >= len);
        xSemaphoreGive(gSdCardData.wMutex);
        
        if (!hasSpace) {
            // Check for timeout
            if ((xTaskGetTickCount() - startTime) >= timeoutTicks) {
                LOG_E("SD: WriteToBuffer timeout - buffer full for %u ms", SD_CARD_MANAGER_WRITE_TIMEOUT_MS);
                return 0;
            }
            vTaskDelay(pdMS_TO_TICKS(SD_CARD_MANAGER_WRITE_WAIT_INTERVAL_MS));
        }
    }
    
    //Obtain ownership of the mutex object
    xSemaphoreTake(gSdCardData.wMutex, portMAX_DELAY);
    bytesAdded = CircularBuf_AddBytes(&gSdCardData.wCirbuf, (uint8_t*) pData, len);
    xSemaphoreGive(gSdCardData.wMutex);

    static uint32_t totalWritten = 0;
    static uint32_t writeCount = 0;
    totalWritten += bytesAdded;
    writeCount++;
    if (writeCount % 10 == 0) {  // Log every 10 writes to avoid spam
        LOG_D("[SD] WriteToBuffer: %u writes, %u total bytes\r\n", writeCount, totalWritten);
    }

    return bytesAdded;
}

bool sd_card_manager_Deinit() {
    gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_DEINIT;
    gpSdCardSettings->enable = 0;
    return true;
}

bool sd_card_manager_UpdateSettings(sd_card_manager_settings_t *pSettings) {
    if (pSettings != NULL && gpSdCardSettings != NULL) {
        memcpy(gpSdCardSettings, pSettings, sizeof (sd_card_manager_settings_t));
    }
    gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_DEINIT;
    return true;
}

bool sd_card_manager_IsIdle() {
    return (gSdCardData.currentProcessState == SD_CARD_MANAGER_PROCESS_STATE_IDLE ||
            gSdCardData.currentProcessState == SD_CARD_MANAGER_PROCESS_STATE_INIT);
}

bool sd_card_manager_WaitForCompletion(uint32_t timeoutMs) {
    if (sd_card_manager_IsIdle()) {
        return true;  // Already idle
    }

    TickType_t timeout = (timeoutMs == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);

    // Wait for operation complete semaphore
    if (xSemaphoreTake(gSdCardData.opCompleteSemaphore, timeout) == pdTRUE) {
        return true;  // Operation completed
    } else {
        LOG_E("[SD] WaitForCompletion timeout after %u ms\r\n", timeoutMs);
        return false;  // Timeout
    }
}

size_t sd_card_manager_GetWriteBuffFreeSize() {
    static bool logged = false;
    if (!logged) {
        LOG_D("SD_MGR: GetWriteBuffFreeSize: enable=%d, mode=%d (WRITE=%d)",
              gpSdCardSettings->enable, gpSdCardSettings->mode, SD_CARD_MANAGER_MODE_WRITE);
        logged = true;
    }

    if (gpSdCardSettings->enable != 1 || gpSdCardSettings->mode != SD_CARD_MANAGER_MODE_WRITE) {
        return 0;
    }

    // Must protect circular buffer access with mutex
    xSemaphoreTake(gSdCardData.wMutex, portMAX_DELAY);
    size_t freeSize = CircularBuf_NumBytesFree(&gSdCardData.wCirbuf);
    xSemaphoreGive(gSdCardData.wMutex);

    if (!logged) {
        LOG_D("SD_MGR: Returning freeSize=%u", freeSize);
    }

    return freeSize;
}


