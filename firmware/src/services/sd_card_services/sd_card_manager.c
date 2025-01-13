#include <string.h>
#include "Util/Logger.h"
#include "sd_card_manager.h"

#define SD_CARD_MANAGER_CIRCULAR_BUFFER_SIZE (SD_CARD_MANAGER_CONF_WBUFFER_SIZE*4)
#define SD_CARD_MANAGER_FILE_PATH_LEN_MAX (SYS_FS_FILE_NAME_LEN*2)
#define SD_CARD_MANAGER_DISK_MOUNT_NAME    "/mnt/Daqifi"
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
    SD_CARD_MANAGER_PROCESS_STATE_DEINIT,
    SD_CARD_MANAGER_PROCESS_STATE_IDLE,
    SD_CARD_MANAGER_PROCESS_STATE_ERROR,
} sd_card_manager_processState_t;

typedef struct {
    sd_card_manager_processState_t currentProcessState;
    /** Client read buffer */
    uint8_t readBuffer[SD_CARD_MANAGER_CONF_RBUFFER_SIZE];

    /** The current length of the read buffer */
    size_t readBufferLength;

    /** Client write buffer */
    uint8_t writeBuffer[SD_CARD_MANAGER_CONF_WBUFFER_SIZE];

    /** The current length of the write buffer */
    size_t writeBufferLength;

    CircularBuf_t wCirbuf;

    SemaphoreHandle_t wMutex;

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

static int CircularBufferToSDWrite(uint8_t* buf, uint16_t len) {
    if (len>sizeof (gSdCardData.writeBuffer))
        return false;
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
static size_t ListFilesInDirectory(const char* dirPath, uint8_t *pStrBuff, size_t strBuffSize) {
    SYS_FS_FSTAT stat;
    size_t strBuffIndex = 0;
    SYS_FS_HANDLE dirHandle;
    char newPath[SD_CARD_MANAGER_FILE_PATH_LEN_MAX + 1];

    memset(newPath, 0, sizeof (newPath));
    memset(&stat, 0, sizeof (stat));
    dirHandle = SYS_FS_DirOpen(dirPath);
    if (dirHandle == SYS_FS_HANDLE_INVALID) {
        SYS_FS_ERROR err = SYS_FS_Error();
        strBuffIndex += snprintf((char *) pStrBuff + strBuffIndex, strBuffSize - strBuffIndex,
                "\r\n[Error:%d]Failed to open directory [%s]\r\n", err, dirPath);
        return strBuffIndex;
    }
    while (true) {
        if (SYS_FS_DirRead(dirHandle, &stat) == SYS_FS_RES_FAILURE) {
            SYS_FS_ERROR err = SYS_FS_Error();
            strBuffIndex += snprintf((char *) pStrBuff + strBuffIndex, strBuffSize - strBuffIndex,
                    "\r\n[Error:%d]Failed to read directory\r\n", err);
            break;
        }

        if (stat.fname[0] == '\0') {
            break;
        }
        if (strcmp(stat.fname, ".") == 0 || strcmp(stat.fname, "..") == 0) {
            continue;
        }
        snprintf(newPath, SD_CARD_MANAGER_FILE_PATH_LEN_MAX, "%s/%s", dirPath, stat.fname);
        if (stat.fattrib & SYS_FS_ATTR_DIR) {

            size_t bytesWritten = ListFilesInDirectory(newPath, pStrBuff + strBuffIndex, strBuffSize - strBuffIndex);
            strBuffIndex += bytesWritten;
            if (strBuffIndex >= strBuffSize) {
                break;
            }
        } else {
            int n = snprintf((char *) pStrBuff + strBuffIndex, strBuffSize - strBuffIndex,
                    "%s\r\n", newPath);
            if (n < 0 || (size_t) n >= strBuffSize - strBuffIndex) {
                break;
            }
            strBuffIndex += n;
        }
    }

    if (SYS_FS_DirClose(dirHandle) != SYS_FS_RES_SUCCESS) {
        SYS_FS_ERROR err = SYS_FS_Error();
        strBuffIndex += snprintf((char *) pStrBuff + strBuffIndex, strBuffSize - strBuffIndex,
                "\r\n[Error:%d]Failed to close directory\r\n", err);
    }

    return strBuffIndex;
}

bool sd_card_manager_Init(sd_card_manager_settings_t *pSettings) {
    static bool isInitDone = false;
    if (!isInitDone) {
        CircularBuf_Init(&gSdCardData.wCirbuf,
                CircularBufferToSDWrite,
                SD_CARD_MANAGER_CIRCULAR_BUFFER_SIZE);
        gSdCardData.wMutex = xSemaphoreCreateMutex();
        xSemaphoreGive(gSdCardData.wMutex);
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
            if (gpSdCardSettings->enable &&
                    strlen(gpSdCardSettings->directory) > 0 &&
                    strlen(gpSdCardSettings->directory) <= SD_CARD_MANAGER_CONF_DIR_NAME_LEN_MAX &&
                    strlen(gpSdCardSettings->file) > 0 &&
                    strlen(gpSdCardSettings->directory) <= SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX) {
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_MOUNT_DISK;
            } else if (gpSdCardSettings->enable) {
                LOG_E("[%s:%d]Invalid SD Card Directory or file name", __FILE__, __LINE__);
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
                SYS_FS_FileClose(gSdCardData.fileHandle);
                gSdCardData.fileHandle = SYS_FS_HANDLE_INVALID;
            }
            if (SYS_FS_Unmount(SD_CARD_MANAGER_DISK_MOUNT_NAME) == 0) {
                gSdCardData.discMounted = false;
            }
            if (gSdCardData.discMounted == true) {
                /* The disk could not be un mounted. Try
                 * un mounting again untill success. */
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK;
            } else {
                if (!gpSdCardSettings->enable) {
                    gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_INIT;
                } else {
                    gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_MOUNT_DISK;
                }
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
                    gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_OPEN_FILE;
                } else {
                    gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                    LOG_E("[%s:%d]Invalid SD Card Directory name", __FILE__, __LINE__);
                }
                /* Error while creating a new drive */
            } else {
                /* Open a file for writing. */
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_OPEN_FILE;
            }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_OPEN_FILE:
            memset(gSdCardData.filePath, 0, sizeof (gSdCardData.filePath));
            snprintf(gSdCardData.filePath, SD_CARD_MANAGER_FILE_PATH_LEN_MAX, "%s/%s",
                    gpSdCardSettings->directory, gpSdCardSettings->file);
            if (gpSdCardSettings->mode == SD_CARD_MANAGER_MODE_WRITE) {
                gSdCardData.fileHandle = SYS_FS_FileOpen(gSdCardData.filePath,
                        (SYS_FS_FILE_OPEN_APPEND_PLUS));
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_WRITE_TO_FILE;
                gSdCardData.totalBytesFlushPending = 0;
                gSdCardData.lastFlushMillis = xTaskGetTickCount() * portTICK_PERIOD_MS;
            } else if (gpSdCardSettings->mode == SD_CARD_MANAGER_MODE_READ) {
                gSdCardData.fileHandle = SYS_FS_FileOpen(gSdCardData.filePath,
                        (SYS_FS_FILE_OPEN_READ));
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_READ_FROM_FILE;
            } else if (gpSdCardSettings->mode == SD_CARD_MANAGER_MODE_LIST_DIRECTORY) {
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_LIST_DIR;
                break;
            } else if (gpSdCardSettings->mode == SD_CARD_MANAGER_MODE_NONE) {
                gSdCardData.fileHandle = SYS_FS_FileOpen(gSdCardData.filePath,
                        (SYS_FS_FILE_OPEN_READ));
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
            }
            if (gSdCardData.fileHandle == SYS_FS_HANDLE_INVALID) {
                /* Could not open the file. Error out*/
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                LOG_E("[%s:%d]Invalid SD Card file name", __FILE__, __LINE__);
            }
            break;
        case SD_CARD_MANAGER_PROCESS_STATE_WRITE_TO_FILE:
        {
            /* If read was success, try writing to the new file */
            int writeLen = -2;
            if (CircularBuf_NumBytesAvailable(&gSdCardData.wCirbuf) > 0
                    && gSdCardData.sdCardWritePending != 1) {
                xSemaphoreTake(gSdCardData.wMutex, portMAX_DELAY);
                gSdCardData.sdCardWritePending = 1;
                CircularBuf_ProcessBytes(&gSdCardData.wCirbuf, NULL, SD_CARD_MANAGER_CONF_WBUFFER_SIZE, &writeLen);
                gSdCardData.totalBytesFlushPending += gSdCardData.writeBufferLength;
                xSemaphoreGive(gSdCardData.wMutex);
            } else if (gSdCardData.sdCardWritePending == 1) {
                writeLen = SDCardWrite();
            }
            if (writeLen >= gSdCardData.writeBufferLength) {
                gSdCardData.sdCardWritePending = 0;
                gSdCardData.writeBufferLength = 0;
                gSdCardData.sdCardWriteBufferOffset = 0;
            } else if (writeLen >= 0) {
                gSdCardData.writeBufferLength -= writeLen;
                gSdCardData.sdCardWriteBufferOffset = writeLen;
            } else if (writeLen == -1) {
                gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                LOG_E("[%s:%d]Error Writing to SD Card", __FILE__, __LINE__);
                break;
            }
            uint64_t currentMillis = xTaskGetTickCount() * portTICK_PERIOD_MS;

            if ((currentMillis - gSdCardData.lastFlushMillis > 5000 ||
                    gSdCardData.totalBytesFlushPending > 2000)) {
                if (gSdCardData.totalBytesFlushPending > 0) {
                    if (SYS_FS_FileSync(gSdCardData.fileHandle) == -1) {
                        gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                        LOG_E("[%s:%d]Error flushing to SD Card", __FILE__, __LINE__);
                    }
                    gSdCardData.totalBytesFlushPending = 0;
                    gSdCardData.lastFlushMillis = currentMillis;
                }
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
            size_t readLen = 0;
            gSdCardData.readBuffer[0] = '\r';
            gSdCardData.readBuffer[1] = '\n';
            readLen = ListFilesInDirectory(
                    gpSdCardSettings->directory,
                    gSdCardData.readBuffer + 2,
                    SD_CARD_MANAGER_CONF_RBUFFER_SIZE - 1 - 2);
            readLen += 2;
            if (readLen > 0 && readLen < SD_CARD_MANAGER_CONF_RBUFFER_SIZE) {
                gSdCardData.readBufferLength = readLen;
                gSdCardData.readBuffer[readLen] = '\0';
            } else if (readLen >= SD_CARD_MANAGER_CONF_RBUFFER_SIZE) {
                gSdCardData.readBufferLength = SD_CARD_MANAGER_CONF_RBUFFER_SIZE - 1;
                gSdCardData.readBuffer[readLen] = '\0';
            } else {
                gSdCardData.readBufferLength = 0;
            }
            sd_card_manager_DataReadyCB(SD_CARD_MANAGER_MODE_LIST_DIRECTORY,
                    gSdCardData.readBuffer,
                    gSdCardData.readBufferLength);
            gSdCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
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
    while (CircularBuf_NumBytesFree(&gSdCardData.wCirbuf) < len) {
        vTaskDelay(10);
    }
    // if the data to write can't fit into the buffer entirely, discard it. 
    if (CircularBuf_NumBytesFree(&gSdCardData.wCirbuf) < len) {
        return 0;
    }
    //Obtain ownership of the mutex object
    xSemaphoreTake(gSdCardData.wMutex, portMAX_DELAY);
    bytesAdded = CircularBuf_AddBytes(&gSdCardData.wCirbuf, (uint8_t*) pData, len);
    xSemaphoreGive(gSdCardData.wMutex);
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

size_t sd_card_manager_GetWriteBuffFreeSize() {
    if (gpSdCardSettings->enable != 1 || gpSdCardSettings->mode != SD_CARD_MANAGER_MODE_WRITE) {
        return 0;
    }
    return CircularBuf_NumBytesFree(&gSdCardData.wCirbuf);
}


