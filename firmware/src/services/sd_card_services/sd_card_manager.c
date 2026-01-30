#define LOG_LVL LOG_LEVEL_SD

#include <string.h>
#include "Util/Logger.h"
#include "sd_card_manager.h"
#include "services/UsbCdc/UsbCdc.h"
#include "services/csv_encoder.h"  // For csv_ResetEncoder on file rotation

#define SD_CARD_MANAGER_CIRCULAR_BUFFER_SIZE (32 * 1024)  // 32KB shared buffer for all SD operations
#define SD_CARD_MANAGER_FILE_PATH_LEN_MAX (SYS_FS_FILE_NAME_LEN*2)
#define SD_CARD_MANAGER_DISK_MOUNT_NAME    "/mnt/DAQiFi"
// SD_CARD_MANAGER_DISK_DEV_NAME is now in header for external use
#define SD_CARD_MANAGER_MAX_SPLIT_FILES    9999

// File read transfer constants
#define SD_READ_MAX_CHUNK_SIZE      16384U  // Maximum read size (16KB) - tested maximum for stability
#define SD_READ_ALIGNMENT_SIZE      4096U   // Chunk alignment (4KB) - matches USB transfer granularity
#define SD_FLUSH_THRESHOLD          4096U   // Minimum bytes to trigger flush before unmount

// =============================================================================
// Debug Timeout Configuration
// =============================================================================
// Long timeouts for detecting hangs without perturbing normal operation.
// These should be many times longer than expected operation duration.
// Only logs error when timeout is hit, indicating a real problem.
#define SD_DEBUG_TIMEOUT_MS         60000U  // 60 seconds - filesystem operations
#define SD_DEBUG_MUTEX_TIMEOUT_MS   30000U  // 30 seconds - mutex acquisition

/**
 * @brief Take a semaphore with timeout, log error if timeout occurs.
 *
 * Wraps xSemaphoreTake with debug timeout. On timeout, logs the location
 * but continues waiting (returns result of infinite wait for compatibility).
 *
 * @param sem       Semaphore handle
 * @param location  String identifying the call location for logging
 * @return pdTRUE if semaphore taken, pdFALSE on failure
 */
static inline BaseType_t SD_TakeMutexDebug(SemaphoreHandle_t sem, const char* location) {
    TickType_t timeout = pdMS_TO_TICKS(SD_DEBUG_MUTEX_TIMEOUT_MS);
    BaseType_t result = xSemaphoreTake(sem, timeout);
    if (result != pdTRUE) {
        LOG_E("[SD] HANG DETECTED: Mutex timeout at %s after %u ms",
              location, SD_DEBUG_MUTEX_TIMEOUT_MS);
        // Continue waiting indefinitely to preserve original behavior
        result = xSemaphoreTake(sem, portMAX_DELAY);
    }
    return result;
}

/**
 * @brief Log if a filesystem operation took longer than expected.
 *
 * Call after FS operation completes to detect slow operations.
 *
 * @param startTick  Tick count before operation started
 * @param operation  String describing the operation
 * @param result     Result code from the operation (-1 typically means error)
 */
static inline void SD_CheckFsOpDuration(TickType_t startTick, const char* operation, int result) {
    uint32_t elapsed = pdTICKS_TO_MS(xTaskGetTickCount() - startTick);
    if (elapsed > SD_DEBUG_TIMEOUT_MS) {
        LOG_E("[SD] HANG DETECTED: %s took %u ms (limit %u ms), result=%d",
              operation, elapsed, SD_DEBUG_TIMEOUT_MS, result);
    } else if (elapsed > 5000) {
        // Also log warning for operations > 5 seconds (unusual but not critical)
        LOG_I("[SD] Slow operation: %s took %u ms, result=%d", operation, elapsed, result);
    }
}

// Shared coherent buffer for all SD operations (write, read, list)
// DMA-safe for SPI transfers. Mutually exclusive operations share this buffer.
// Cache-line aligned for optimal DMA performance.
static __attribute__((coherent, aligned(16))) uint8_t gSdSharedBuffer[SD_CARD_MANAGER_CIRCULAR_BUFFER_SIZE];

// Mutex to serialize SD operations (READ, WRITE, LIST) on shared buffer
// Prevents race conditions from cross-task state changes via UpdateSettings()
static SemaphoreHandle_t gSDOpMutex = NULL;

/**
 * Helper function: Wait for USB buffer to drain before EOF/close
 * Prevents race condition where EOF arrives before last data is processed
 * Waits up to 50ms for USB buffer to be >50% drained
 */
static void sd_wait_usb_drain(void) {
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(50)) {
        // If buffer is mostly drained, assume progress and exit
        if (UsbCdc_WriteBuffFreeSize(NULL) > (USBCDC_WBUFFER_SIZE / 2)) {
            break;
        }
        vTaskDelay(1);
    }
}

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
    /** Control message buffer (for EOF marker, error messages) */
    uint8_t messageBuffer[SD_CARD_MANAGER_CONF_RBUFFER_SIZE] __attribute__((coherent));

    /** The current length of the message buffer */
    size_t messageBufferLength;

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

    // File splitting state
    char baseFilename[SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX + 1];  // Original filename without counter
    uint32_t fileCounter;        // Current file number (1, 2, 3, ...)
    uint64_t currentFileBytes;   // Bytes written to current file
    bool fileSplittingEnabled;   // True if maxFileSizeBytes > 0

    // Operation result tracking
    bool lastOperationSuccess;   // Result of last completed operation
} sd_card_manager_context_t;

sd_card_manager_context_t gSDCardData;
sd_card_manager_settings_t *gpSDCardSettings;

void __attribute__((weak)) sd_card_manager_DataReadyCB(sd_card_manager_mode_t mode, uint8_t *pDataBuff, size_t dataLen) {

}

static int SDCardWrite() {
    int writeLen = -1;
    if (gSDCardData.fileHandle == SYS_FS_HANDLE_INVALID) {
        goto __exit;
    }

    TickType_t startTick = xTaskGetTickCount();
    writeLen = SYS_FS_FileWrite(gSDCardData.fileHandle,
            (const void *) (gSDCardData.writeBuffer + gSDCardData.sdCardWriteBufferOffset),
            gSDCardData.writeBufferLength);
    SD_CheckFsOpDuration(startTick, "FileWrite", writeLen);
__exit:
    return writeLen;
}

static int CircularBufferToSDWrite(uint8_t* buf, uint32_t len) {
    if (len>sizeof (gSDCardData.writeBuffer))
        return -1;  // Error: buffer overflow
    memcpy(gSDCardData.writeBuffer, buf, len);
    gSDCardData.writeBufferLength = len;
    gSDCardData.sdCardWriteBufferOffset = 0;
    // Return length to indicate success - actual write happens in state machine
    // Calling SDCardWrite() here causes duplicate writes!
    return len;
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

            // Now add the filename and size to buffer (space-separated)
            n = snprintf((char *) pStrBuff + strBuffIndex, strBuffSize - strBuffIndex,
                    "%s %u\r\n", newPath, (unsigned)stat.fsize);
            if (n > 0 && (size_t)n < strBuffSize - strBuffIndex) {
                strBuffIndex += (size_t)n;
            } else {
                // Entry doesn't fit - flush buffer and retry with fresh buffer
                if (sendChunk && strBuffIndex > 0) {
                    pStrBuff[strBuffIndex] = '\0';
                    sendChunk(pStrBuff, strBuffIndex);
                    strBuffIndex = 0;

                    // Retry with fresh buffer
                    n = snprintf((char *) pStrBuff, strBuffSize,
                                "%s %u\r\n", newPath, (unsigned)stat.fsize);
                    if (n > 0 && (size_t)n < strBuffSize) {
                        strBuffIndex = (size_t)n;
                    }
                    // If still doesn't fit, entry is too large for buffer (skip it)
                }
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
        // Initialize circular buffer using static coherent memory instead of heap
        gSDCardData.wCirbuf.buf_ptr = gSdSharedBuffer;
        gSDCardData.wCirbuf.buf_size = SD_CARD_MANAGER_CIRCULAR_BUFFER_SIZE;
        gSDCardData.wCirbuf.process_callback = CircularBufferToSDWrite;
        gSDCardData.wCirbuf.insertPtr = gSdSharedBuffer;
        gSDCardData.wCirbuf.removePtr = gSdSharedBuffer;
        gSDCardData.wCirbuf.totalBytes = 0;

        gSDCardData.wMutex = xSemaphoreCreateMutex();
        xSemaphoreGive(gSDCardData.wMutex);
        gSDCardData.opCompleteSemaphore = xSemaphoreCreateBinary();
        gSDCardData.lastOperationSuccess = true;  // Initialize to success

        // Create operation mutex to serialize READ/WRITE/LIST on gSDSharedBuffer
        if (gSDOpMutex == NULL) {
            gSDOpMutex = xSemaphoreCreateMutex();
        }
        isInitDone = true;
        gpSDCardSettings = pSettings;
        gSDCardData.fileHandle = SYS_FS_HANDLE_INVALID;
        gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_INIT;

        // Initialize file splitting state
        memset(gSDCardData.baseFilename, 0, sizeof(gSDCardData.baseFilename));
        gSDCardData.fileCounter = 0;
        gSDCardData.currentFileBytes = 0;
        gSDCardData.fileSplittingEnabled = false;
    }
    return true;
}

/**
 * @brief Extracts base filename (without extension) from given filename.
 *
 * Example: "data.csv" → "data" stored in baseFilename field
 *
 * @param filename Input filename (may include extension)
 */
static void extractBaseFilename(const char* filename) {
    strncpy(gSDCardData.baseFilename, filename, sizeof(gSDCardData.baseFilename) - 1);
    gSDCardData.baseFilename[sizeof(gSDCardData.baseFilename) - 1] = '\0';

    // Find last dot for extension
    char* ext = strrchr(gSDCardData.baseFilename, '.');
    if (ext != NULL) {
        *ext = '\0';  // Remove extension from base filename
    }
}

/**
 * @brief Generates filename with sequential numbering.
 *
 * Format: basename-NNN.ext (e.g., "data-001.csv", "data-002.csv")
 *
 * @param outPath Output buffer for full path
 * @param maxLen Maximum length of output buffer
 * @param counter File counter (1, 2, 3, ...)
 * @param directory Directory path
 * @param baseFilename Base filename (without extension)
 * @param originalFilename Original filename (used to extract extension)
 */
static void generateFilename(char* outPath, size_t maxLen, uint32_t counter,
                             const char* directory, const char* baseFilename,
                             const char* originalFilename) {
    // Extract extension (includes leading dot if present)
    const char* ext = strrchr(originalFilename, '.');
    const char* useExt = (ext != NULL) ? ext : "";

    // Pre-validate length to avoid truncation
    size_t dirLen = strlen(directory);
    size_t baseLen = strlen(baseFilename);
    size_t origLen = strlen(originalFilename);
    if (counter == 0) {
        if (dirLen + 1 + origLen + 1 > maxLen) {
            LOG_E("[%s:%d]Filename too long: dir='%s' file='%s' (max %zu)",
                  __FILE__, __LINE__, directory, originalFilename, maxLen);
            outPath[0] = '\0';
            return;
        }
    } else {
        if (dirLen + 1 + baseLen + 1 + 10 + strlen(useExt) + 1 > maxLen) {
            LOG_E("[%s:%d]Filename too long for split file: dir='%s' base='%s' ext='%s' (max %zu)",
                  __FILE__, __LINE__, directory, baseFilename, useExt, maxLen);
            outPath[0] = '\0';
            return;
        }
    }

    int written;
    if (counter == 0) {
        written = snprintf(outPath, maxLen, "%s/%s", directory, originalFilename);
    } else {
        written = snprintf(outPath, maxLen, "%s/%s-%u%s", directory, baseFilename, counter, useExt);
    }

    if (written < 0 || (size_t)written >= maxLen) {
        LOG_E("[%s:%d]Filename buffer overflow (needed %d bytes, have %zu): dir='%s' base='%s' cnt=%u ext='%s'",
              __FILE__, __LINE__, written, maxLen, directory, baseFilename, counter, useExt);
        outPath[0] = '\0';
    }
}

void sd_card_manager_ProcessState() {
    /* Check the application's current state. */

    switch (gSDCardData.currentProcessState) {
        case SD_CARD_MANAGER_PROCESS_STATE_INIT:
            // Only initialize if SD is enabled AND has a valid operation mode
            // Just enabling SD without setting a mode (WRITE/READ/LIST) is valid - don't spam errors
            if (gpSDCardSettings->enable && gpSDCardSettings->mode != SD_CARD_MANAGER_MODE_NONE) {
                // Validate directory and file settings
                bool dirValid = strlen(gpSDCardSettings->directory) > 0 &&
                               strlen(gpSDCardSettings->directory) <= SD_CARD_MANAGER_CONF_DIR_NAME_LEN_MAX;
                bool fileValid = strlen(gpSDCardSettings->file) > 0 &&
                                strlen(gpSDCardSettings->file) <= SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX;

                // LIST and FORMAT modes don't need a filename
                // DELETE, READ, WRITE need a filename
                bool needsFile = (gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_DELETE_FILE ||
                                 gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_READ ||
                                 gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_WRITE);

                // Reset error flag on valid configuration (allows new errors to be logged after fix)
                static bool errorLogged = false;

                if (dirValid && (!needsFile || fileValid)) {
                    errorLogged = false;  // Reset flag on successful validation
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_MOUNT_DISK;
                } else {
                    // Only log error once per enable, not continuously
                    if (!errorLogged) {
                        LOG_E("[%s:%d]Invalid SD Card Directory or file name (dir='%s', file='%s')",
                              __FILE__, __LINE__,
                              gpSDCardSettings->directory,
                              gpSDCardSettings->file);
                        errorLogged = true;
                    }
                }
            }
            break;
        case SD_CARD_MANAGER_PROCESS_STATE_MOUNT_DISK:
            gSDCardData.sdCardWritePending = 0;
            gSDCardData.writeBufferLength = 0;
            gSDCardData.sdCardWriteBufferOffset = 0;
            {
                TickType_t mountStart = xTaskGetTickCount();
                int mountResult = SYS_FS_Mount(SD_CARD_MANAGER_DISK_DEV_NAME, SD_CARD_MANAGER_DISK_MOUNT_NAME, FAT, 0, NULL);
                SD_CheckFsOpDuration(mountStart, "SYS_FS_Mount", mountResult);
                if (mountResult != 0) {
                    /* The disk could not be mounted. Try
                     * mounting again until success. */
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_MOUNT_DISK;
                } else {
                    /* Mount was successful. Unmount the disk, for testing. */
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_CURRENT_DRIVE;
                    gSDCardData.discMounted = true;
                }
            }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK:
            if (gSDCardData.fileHandle != SYS_FS_HANDLE_INVALID) {
                // Flush any pending data before closing to prevent data loss
                SD_TakeMutexDebug(gSDCardData.wMutex, "unmount_pending_check");
                bool hasPendingData = gSDCardData.totalBytesFlushPending > 0;
                xSemaphoreGive(gSDCardData.wMutex);

                if (hasPendingData) {
                    LOG_D("[SD] Flushing %d bytes before unmount\r\n", (int)gSDCardData.totalBytesFlushPending);
                    TickType_t syncStart = xTaskGetTickCount();
                    int syncResult = SYS_FS_FileSync(gSDCardData.fileHandle);
                    SD_CheckFsOpDuration(syncStart, "FileSync(unmount)", syncResult);
                    if (syncResult != -1) {
                        SD_TakeMutexDebug(gSDCardData.wMutex, "unmount_flush");
                        gSDCardData.totalBytesFlushPending = 0;
                        xSemaphoreGive(gSDCardData.wMutex);
                        LOG_D("[SD] Flushed pending data before unmount\r\n");
                    } else {
                        LOG_E("[%s:%d]Error flushing before unmount", __FILE__, __LINE__);
                    }
                }

                LOG_D("[SD] Closing file '%s'\r\n", gSDCardData.filePath);
                SYS_FS_FileClose(gSDCardData.fileHandle);
                gSDCardData.fileHandle = SYS_FS_HANDLE_INVALID;
            }
            if (SYS_FS_Unmount(SD_CARD_MANAGER_DISK_MOUNT_NAME) == 0) {
                gSDCardData.discMounted = false;
                LOG_D("[SD] Unmounted successfully\r\n");
            }
            if (gSDCardData.discMounted == true) {
                /* The disk could not be un mounted. Try
                 * un mounting again untill success. */
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK;
            } else {
                // Reset file splitting state for next session
                gSDCardData.fileCounter = 0;
                gSDCardData.currentFileBytes = 0;
                memset(gSDCardData.baseFilename, 0, sizeof(gSDCardData.baseFilename));
                gSDCardData.fileSplittingEnabled = false;
                LOG_D("[SD] File splitting state reset for next session\r\n");

                // Always go back to INIT after unmounting
                // INIT will decide whether to mount based on enable flag and mode
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_INIT;
            }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_CURRENT_DRIVE:
            if (SYS_FS_CurrentDriveSet(SD_CARD_MANAGER_DISK_MOUNT_NAME) == SYS_FS_RES_FAILURE) {
                /* Error while setting current drive */
                LOG_E("[%s:%d]Error Setting SD Card drive", __FILE__, __LINE__);
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
            } else {
                /* Open a file for reading. */
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_CREATE_DIRECTORY;
            }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_CREATE_DIRECTORY:
            if (SYS_FS_DirectoryMake(gpSDCardSettings->directory) == SYS_FS_RES_FAILURE) {
                if (SYS_FS_Error() == SYS_FS_ERROR_EXIST) {
                    LOG_D("[SD] Directory '%s' already exists\r\n", gpSDCardSettings->directory);
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_OPEN_FILE;
                } else {
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                    LOG_E("[%s:%d]Invalid SD Card Directory name '%s'", __FILE__, __LINE__, gpSDCardSettings->directory);
                }
                /* Error while creating a new drive */
            } else {
                LOG_D("[SD] Created directory '%s'\r\n", gpSDCardSettings->directory);
                /* Open a file for writing. */
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_OPEN_FILE;
            }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_OPEN_FILE:
            memset(gSDCardData.filePath, 0, sizeof (gSDCardData.filePath));
            LOG_D("[SD] Opening file, mode=%d\r\n", gpSDCardSettings->mode);
            if (gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_WRITE) {
                // Initialize file splitting if enabled
                gSDCardData.fileSplittingEnabled = (gpSDCardSettings->maxFileSizeBytes > 0);

                // Extract base filename and generate actual filename with counter
                if (gSDCardData.fileCounter == 0) {
                    // First time opening - extract base filename
                    extractBaseFilename(gpSDCardSettings->file);
                }

                generateFilename(gSDCardData.filePath, sizeof(gSDCardData.filePath),
                               gSDCardData.fileCounter, gpSDCardSettings->directory,
                               gSDCardData.baseFilename, gpSDCardSettings->file);

                LOG_D("[SD] Opening file '%s' (counter=%u, splitting=%s)\r\n",
                     gSDCardData.filePath, gSDCardData.fileCounter,
                     gSDCardData.fileSplittingEnabled ? "enabled" : "disabled");

                // Clear shared buffer and write buffer to prevent stale data from previous session
                SD_TakeMutexDebug(gSDCardData.wMutex, "open_file_clear_buffer");
                CircularBuf_Reset(&gSDCardData.wCirbuf);
                memset(gSdSharedBuffer, 0, SD_CARD_MANAGER_CIRCULAR_BUFFER_SIZE);
                memset(gSDCardData.writeBuffer, 0, sizeof(gSDCardData.writeBuffer));
                xSemaphoreGive(gSDCardData.wMutex);

                // Use WRITE_PLUS to create/truncate file (overwrite mode)
                gSDCardData.fileHandle = SYS_FS_FileOpen(gSDCardData.filePath,
                        (SYS_FS_FILE_OPEN_WRITE_PLUS));
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_WRITE_TO_FILE;
                gSDCardData.totalBytesFlushPending = 0;
                gSDCardData.currentFileBytes = 0;  // Reset byte counter for new file
                gSDCardData.lastFlushMillis = pdTICKS_TO_MS(xTaskGetTickCount());

                if (gSDCardData.fileHandle == SYS_FS_HANDLE_INVALID) {
                    /* Could not open the file. Error out*/
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                    LOG_E("[%s:%d]Failed to open SD Card file for writing: '%s'", __FILE__, __LINE__, gSDCardData.filePath);
                }
            } else if (gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_READ) {
                // For READ mode, construct filename directly from settings (no splitting)
                snprintf(gSDCardData.filePath, SD_CARD_MANAGER_FILE_PATH_LEN_MAX, "%s/%s",
                        gpSDCardSettings->directory, gpSDCardSettings->file);
                LOG_D("[SD] Opening file for read: '%s'\r\n", gSDCardData.filePath);

                gSDCardData.fileHandle = SYS_FS_FileOpen(gSDCardData.filePath,
                        (SYS_FS_FILE_OPEN_READ));
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_READ_FROM_FILE;

                if (gSDCardData.fileHandle == SYS_FS_HANDLE_INVALID) {
                    /* Could not open the file. Error out*/
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                    LOG_E("[%s:%d]Failed to open SD Card file for reading: '%s'", __FILE__, __LINE__, gSDCardData.filePath);
                }
            } else if (gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_LIST_DIRECTORY) {
                // LIST mode doesn't need to open a file, just list the directory
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_LIST_DIR;
            } else if (gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_DELETE_FILE) {
                // DELETE mode - construct file path and delete the specified file
                // Initialize to false to prevent reading stale success from previous operation
                gSDCardData.lastOperationSuccess = false;
                // Strip trailing slash from directory to avoid double slashes
                size_t dirLen = strlen(gpSDCardSettings->directory);
                while (dirLen > 0 && gpSDCardSettings->directory[dirLen - 1] == '/') {
                    dirLen--;
                }
                int pathLen = snprintf(gSDCardData.filePath, SD_CARD_MANAGER_FILE_PATH_LEN_MAX, "%.*s/%s",
                        (int)dirLen, gpSDCardSettings->directory, gpSDCardSettings->file);

                // Validate path was not truncated
                if (pathLen < 0 || pathLen >= (int)SD_CARD_MANAGER_FILE_PATH_LEN_MAX) {
                    LOG_E("[SD] Delete path too long (need %d, max %d)\r\n", pathLen, SD_CARD_MANAGER_FILE_PATH_LEN_MAX - 1);
                    gSDCardData.lastOperationSuccess = false;
                    gpSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
                    xSemaphoreGive(gSDCardData.opCompleteSemaphore);
                    break;
                }

                // Reject path traversal attempts (e.g., "../" or "/..")
                if (strstr(gSDCardData.filePath, "..") != NULL) {
                    LOG_E("[SD] Delete path rejected (traversal attempt): '%s'\r\n", gSDCardData.filePath);
                    gSDCardData.lastOperationSuccess = false;
                    gpSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
                    xSemaphoreGive(gSDCardData.opCompleteSemaphore);
                    break;
                }

                LOG_D("[SD] Preparing to delete file: '%s'\r\n", gSDCardData.filePath);
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_DELETE_FILE;
            } else if (gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_FORMAT) {
                // FORMAT mode - erase all files on SD card
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_FORMAT;
            } else if (gpSDCardSettings->mode == SD_CARD_MANAGER_MODE_NONE) {
                gSDCardData.fileHandle = SYS_FS_FileOpen(gSDCardData.filePath,
                        (SYS_FS_FILE_OPEN_READ));
                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;

                if (gSDCardData.fileHandle == SYS_FS_HANDLE_INVALID) {
                    /* Could not open the file. Error out*/
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                    LOG_E("[%s:%d]Failed to open SD Card file: '%s'", __FILE__, __LINE__, gSDCardData.filePath);
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
                SD_TakeMutexDebug(gSDCardData.wMutex, "write_loop_check");
                if (CircularBuf_NumBytesAvailable(&gSDCardData.wCirbuf) > 0
                        && gSDCardData.sdCardWritePending != 1) {
                    gSDCardData.sdCardWritePending = 1;
                    CircularBuf_ProcessBytes(&gSDCardData.wCirbuf, NULL, SD_CARD_MANAGER_CONF_WBUFFER_SIZE, &writeLen);
                    gSDCardData.totalBytesFlushPending += gSDCardData.writeBufferLength;
                    xSemaphoreGive(gSDCardData.wMutex);
                    chunksProcessed++;
                } else {
                    // No data available or write already pending, release mutex
                    xSemaphoreGive(gSDCardData.wMutex);
                    
                    // Note: sdCardWritePending check outside mutex is safe here because:
                    // 1. Only this task modifies sdCardWritePending
                    // 2. We're coordinating work within a single task, not between tasks
                    // If this changes in future, this will need mutex protection
                    if (gSDCardData.sdCardWritePending == 1) {
                        writeLen = SDCardWrite();
                        if (writeLen >= gSDCardData.writeBufferLength) {
                            // Track bytes written for file splitting
                            gSDCardData.currentFileBytes += gSDCardData.writeBufferLength;

                            SD_TakeMutexDebug(gSDCardData.wMutex, "write_complete");
                            gSDCardData.sdCardWritePending = 0;
                            gSDCardData.writeBufferLength = 0;
                            gSDCardData.sdCardWriteBufferOffset = 0;
                            xSemaphoreGive(gSDCardData.wMutex);
                        } else if (writeLen >= 0) {
                            // Track partial write
                            gSDCardData.currentFileBytes += writeLen;

                            SD_TakeMutexDebug(gSDCardData.wMutex, "partial_write");
                            gSDCardData.writeBufferLength -= writeLen;
                            gSDCardData.sdCardWriteBufferOffset = writeLen;
                            xSemaphoreGive(gSDCardData.wMutex);
                            break;  // Partial write, don't process more chunks
                        } else if (writeLen == -1) {
                            gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                            LOG_E("[%s:%d]Error Writing to SD Card", __FILE__, __LINE__);
                            break;
                        }
                    } else {
                        break;  // No more data to process
                    }
                }
            }

            // Check if file size limit reached and rotation is needed
            if (gSDCardData.fileSplittingEnabled &&
                gSDCardData.currentFileBytes >= gpSDCardSettings->maxFileSizeBytes) {
                LOG_D("[SD] File size limit reached (%llu >= %llu), rotating to next file\r\n",
                     gSDCardData.currentFileBytes, gpSDCardSettings->maxFileSizeBytes);

                // Drain circular buffer completely before rotation to prevent data loss
                SD_TakeMutexDebug(gSDCardData.wMutex, "drain_buffer_check");
                size_t bufferBytes = CircularBuf_NumBytesAvailable(&gSDCardData.wCirbuf);
                xSemaphoreGive(gSDCardData.wMutex);

                if (bufferBytes > 0) {
                    LOG_D("[SD] Draining %zu bytes from circular buffer before rotation\r\n", bufferBytes);
                    // Process all remaining data in circular buffer
                    while (CircularBuf_NumBytesAvailable(&gSDCardData.wCirbuf) > 0) {
                        int writeLen = -2;
                        SD_TakeMutexDebug(gSDCardData.wMutex, "drain_loop");
                        if (gSDCardData.sdCardWritePending != 1) {
                            gSDCardData.sdCardWritePending = 1;
                            CircularBuf_ProcessBytes(&gSDCardData.wCirbuf, NULL, SD_CARD_MANAGER_CONF_WBUFFER_SIZE, &writeLen);
                            gSDCardData.totalBytesFlushPending += gSDCardData.writeBufferLength;
                            xSemaphoreGive(gSDCardData.wMutex);

                            // Write immediately
                            writeLen = SDCardWrite();
                            if (writeLen >= 0) {
                                gSDCardData.currentFileBytes += writeLen;
                                SD_TakeMutexDebug(gSDCardData.wMutex, "drain_complete");
                                gSDCardData.sdCardWritePending = 0;
                                gSDCardData.writeBufferLength = 0;
                                gSDCardData.sdCardWriteBufferOffset = 0;
                                xSemaphoreGive(gSDCardData.wMutex);
                            } else {
                                LOG_E("[%s:%d]Error draining buffer before rotation", __FILE__, __LINE__);
                                break;
                            }
                        } else {
                            xSemaphoreGive(gSDCardData.wMutex);
                            break;
                        }
                    }
                }

                // Flush and close current file
                if (gSDCardData.fileHandle != SYS_FS_HANDLE_INVALID) {
                    // Always sync before close - ensures all filesystem buffers flushed
                    // Not just our counter, but also FS driver and SD card controller caches
                    TickType_t syncStart = xTaskGetTickCount();
                    int syncResult = SYS_FS_FileSync(gSDCardData.fileHandle);
                    SD_CheckFsOpDuration(syncStart, "FileSync(rotation)", syncResult);
                    if (syncResult == -1) {
                        LOG_E("[%s:%d]Error flushing before file rotation", __FILE__, __LINE__);
                    } else {
                        SD_TakeMutexDebug(gSDCardData.wMutex, "rotation_flush");
                        gSDCardData.totalBytesFlushPending = 0;
                        xSemaphoreGive(gSDCardData.wMutex);
                    }

                    // Close current file with error checking
                    if (SYS_FS_FileClose(gSDCardData.fileHandle) == SYS_FS_RES_FAILURE) {
                        LOG_E("[%s:%d]Error closing file before rotation", __FILE__, __LINE__);
                        gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                        break;
                    }
                    gSDCardData.fileHandle = SYS_FS_HANDLE_INVALID;
                    LOG_D("[SD] Closed file '%s' (wrote %llu bytes)\r\n",
                         gSDCardData.filePath, gSDCardData.currentFileBytes);
                }

                // DO NOT reset CSV encoder - only first file gets header
                // Subsequent split files contain data rows only for cleaner merging
                // and zero latency rotation

                if (gSDCardData.fileCounter < SD_CARD_MANAGER_MAX_SPLIT_FILES) {
                    gSDCardData.fileCounter++;
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_OPEN_FILE;
                } else {
                    LOG_E("[%s:%d]File counter limit reached (%d files). Stopping streaming.",
                          __FILE__, __LINE__, SD_CARD_MANAGER_MAX_SPLIT_FILES);
                    // Cleanly stop: close file if open and signal completion to prevent deadlock
                    if (gSDCardData.fileHandle != SYS_FS_HANDLE_INVALID) {
                        TickType_t syncStart = xTaskGetTickCount();
                        int syncResult = SYS_FS_FileSync(gSDCardData.fileHandle);
                        SD_CheckFsOpDuration(syncStart, "FileSync(limit_stop)", syncResult);
                        SYS_FS_FileClose(gSDCardData.fileHandle);
                        gSDCardData.fileHandle = SYS_FS_HANDLE_INVALID;
                    }
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
                    xSemaphoreGive(gSDCardData.opCompleteSemaphore);
                }
                break;  // Exit to reopen with new filename or error
            }
            uint64_t currentMillis = pdTICKS_TO_MS(xTaskGetTickCount());

            SD_TakeMutexDebug(gSDCardData.wMutex, "periodic_flush_check");
            bool needsFlush = (currentMillis - gSDCardData.lastFlushMillis > 5000 ||
                    gSDCardData.totalBytesFlushPending > SD_FLUSH_THRESHOLD) &&
                    gSDCardData.totalBytesFlushPending > 0;
            if (needsFlush) {
                xSemaphoreGive(gSDCardData.wMutex);

                TickType_t syncStart = xTaskGetTickCount();
                int syncResult = SYS_FS_FileSync(gSDCardData.fileHandle);
                SD_CheckFsOpDuration(syncStart, "FileSync(periodic)", syncResult);
                if (syncResult == -1) {
                    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_ERROR;
                    LOG_E("[%s:%d]Error flushing to SD Card", __FILE__, __LINE__);
                } else {
                    // Only reset counter after successful flush
                    SD_TakeMutexDebug(gSDCardData.wMutex, "periodic_flush_reset");
                    gSDCardData.totalBytesFlushPending = 0;
                    gSDCardData.lastFlushMillis = currentMillis;
                    xSemaphoreGive(gSDCardData.wMutex);
                }
            } else {
                xSemaphoreGive(gSDCardData.wMutex);
            }

        }
            break;
        case SD_CARD_MANAGER_PROCESS_STATE_READ_FROM_FILE:
        {
            // Take mutex to serialize access to gSDSharedBuffer
            if (gSDOpMutex) {
                SD_TakeMutexDebug(gSDOpMutex, "read_operation");
            }

            // Continuous loop for file transfer instead of one chunk per task tick.
            // Yields every 1 second to other tasks. Priority boosted to prevent preemption.
            // Diagnostic logging added for GitHub #146.

            uint32_t totalBytesRead = 0;
            uint32_t readCount = 0;

            // Boost task priority to match USB tasks for balanced time slicing
            TaskHandle_t currentTask = xTaskGetCurrentTaskHandle();
            UBaseType_t originalPriority = uxTaskPriorityGet(currentTask);
            vTaskPrioritySet(currentTask, 7);  // Same as USB tasks for round-robin scheduling

            // Track time for periodic yielding
            TickType_t lastYieldTime = xTaskGetTickCount();
            const TickType_t yieldInterval = pdMS_TO_TICKS(1000);

            // Calculate safe read size based on buffer capacity
            size_t maxRead = sizeof(gSdSharedBuffer);
            if (maxRead > SD_READ_MAX_CHUNK_SIZE) maxRead = SD_READ_MAX_CHUNK_SIZE;
            maxRead = (maxRead / SD_READ_ALIGNMENT_SIZE) * SD_READ_ALIGNMENT_SIZE;
            if (maxRead == 0U) {
                LOG_E("[SD] Buffer too small for read");
                vTaskPrioritySet(currentTask, originalPriority);

                // Release mutex on error path
                if (gSDOpMutex) {
                    xSemaphoreGive(gSDOpMutex);
                }

                gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
                break;
            }

            // EOF marker as literal constant (safer than sprintf)
            static const char eofMarker[] = "__END_OF_FILE__";

            // Read entire file in continuous loop
            while (1) {
                // Abort if file handle became invalid
                if (gSDCardData.fileHandle == SYS_FS_HANDLE_INVALID) {
                    LOG_E("[SD] Transfer ABORTED: file handle invalid");
                    totalBytesRead = 0;
                    readCount = 0;
                    break;
                }

                // Read at maximum rate (backpressure handled by callback retry logic)
                size_t bytesRead = SYS_FS_FileRead(gSDCardData.fileHandle, gSdSharedBuffer, maxRead);

                if (bytesRead == (size_t) - 1) {
                    // Read error - log only, don't send error text through data stream
                    LOG_E("[SD] Transfer ERROR: %u MB, read#%u", totalBytesRead/(1024*1024), readCount);

                    // Wait for USB to drain any pending data before closing
                    sd_wait_usb_drain();

                    // Close file handle to prevent resource leak
                    SYS_FS_FileClose(gSDCardData.fileHandle);
                    gSDCardData.fileHandle = SYS_FS_HANDLE_INVALID;
                    totalBytesRead = 0;
                    readCount = 0;
                    break;

                } else if (bytesRead == 0) {
                    // End of file - wait for USB to drain before sending EOF marker
                    sd_wait_usb_drain();

                    // Send EOF marker using literal constant (safer than sprintf)
                    sd_card_manager_DataReadyCB(SD_CARD_MANAGER_MODE_READ,
                            (uint8_t*)eofMarker,
                            sizeof(eofMarker) - 1);

                    // Close file handle
                    SYS_FS_FileClose(gSDCardData.fileHandle);
                    gSDCardData.fileHandle = SYS_FS_HANDLE_INVALID;

                    totalBytesRead = 0;
                    readCount = 0;
                    break;

                } else {
                    // Data chunk read successfully
                    totalBytesRead += bytesRead;
                    readCount++;

                    sd_card_manager_DataReadyCB(SD_CARD_MANAGER_MODE_READ,
                            gSdSharedBuffer,
                            bytesRead);

                    // Delay every 1 second to allow lower priority tasks to run
                    if ((xTaskGetTickCount() - lastYieldTime) >= yieldInterval) {
                        vTaskDelay(pdMS_TO_TICKS(1));
                        lastYieldTime = xTaskGetTickCount();
                    }
                }
            }

            // Restore original task priority
            vTaskPrioritySet(currentTask, originalPriority);

            // Release mutex - operation complete
            if (gSDOpMutex) {
                xSemaphoreGive(gSDOpMutex);
            }

            gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
        }
            break;
        case SD_CARD_MANAGER_PROCESS_STATE_LIST_DIR:
        {
            // Take mutex to serialize access to gSDSharedBuffer (used in callbacks)
            if (gSDOpMutex) {
                SD_TakeMutexDebug(gSDOpMutex, "list_operation");
            }

            LOG_D("[SD] Listing directory: '%s'\r\n", gpSDCardSettings->directory);

            // List files in chunks using static callback
            ListFilesInDirectoryChunked(
                    gpSDCardSettings->directory,
                    gSDCardData.messageBuffer,
                    SD_CARD_MANAGER_CONF_RBUFFER_SIZE,
                    sd_listdir_send_chunk);

            // Release mutex - operation complete
            if (gSDOpMutex) {
                xSemaphoreGive(gSDOpMutex);
            }

            gSDCardData.lastOperationSuccess = true;  // List always succeeds (may return empty)
            // Reset mode to prevent re-triggering
            gpSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
            gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
            // Signal completion
            xSemaphoreGive(gSDCardData.opCompleteSemaphore);
        }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_DELETE_FILE:
        {
            LOG_D("[SD] Deleting file: '%s'\r\n", gSDCardData.filePath);

            // Delete the file (FAT f_unlink already calls sync_fs internally)
            if (SYS_FS_FileDirectoryRemove(gSDCardData.filePath) == SYS_FS_RES_SUCCESS) {
                LOG_D("[SD] File deleted successfully\r\n");
                gSDCardData.lastOperationSuccess = true;
            } else {
                SYS_FS_ERROR err = SYS_FS_Error();
                LOG_E("[SD] Failed to delete file '%s', error=%d\r\n", gSDCardData.filePath, err);
                gSDCardData.lastOperationSuccess = false;
            }

            // Reset mode to prevent re-triggering
            gpSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
            gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
            // Signal completion
            xSemaphoreGive(gSDCardData.opCompleteSemaphore);
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
                gSDCardData.lastOperationSuccess = true;
            } else {
                SYS_FS_ERROR err = SYS_FS_Error();
                LOG_E("[SD] Format failed, error=%d\r\n", err);
                gSDCardData.lastOperationSuccess = false;
            }

            // Reset mode to prevent re-triggering
            gpSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
            gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
            // Signal completion
            xSemaphoreGive(gSDCardData.opCompleteSemaphore);
        }
            break;

        case SD_CARD_MANAGER_PROCESS_STATE_IDLE:

            break;
        case SD_CARD_MANAGER_PROCESS_STATE_DEINIT:
            gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK;
            break;
        case SD_CARD_MANAGER_PROCESS_STATE_ERROR:
            /* The application comes here when the demo has failed. */
            gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_UNMOUNT_DISK;
            break;

        default:
            break;
    }
}

size_t sd_card_manager_WriteToBuffer(const char* pData, size_t len) {
    size_t bytesAdded = 0;
    if (len == 0)return 0;
    if (gpSDCardSettings->enable != 1 || gpSDCardSettings->mode != SD_CARD_MANAGER_MODE_WRITE) {
        return 0;
    }
    
    // Wait for buffer space with mutex protection and timeout
    bool hasSpace = false;
    TickType_t startTime = xTaskGetTickCount();
    TickType_t timeoutTicks = pdMS_TO_TICKS(SD_CARD_MANAGER_WRITE_TIMEOUT_MS);
    
    while (!hasSpace) {
        // Check buffer space with mutex protection
        SD_TakeMutexDebug(gSDCardData.wMutex, "write_buffer_space_check");
        hasSpace = (CircularBuf_NumBytesFree(&gSDCardData.wCirbuf) >= len);
        xSemaphoreGive(gSDCardData.wMutex);
        
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
    SD_TakeMutexDebug(gSDCardData.wMutex, "write_buffer_add");
    bytesAdded = CircularBuf_AddBytes(&gSDCardData.wCirbuf, (uint8_t*) pData, len);
    xSemaphoreGive(gSDCardData.wMutex);

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
    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_DEINIT;
    gpSDCardSettings->enable = 0;
    return true;
}

bool sd_card_manager_UpdateSettings(sd_card_manager_settings_t *pSettings) {
    if (pSettings != NULL && gpSDCardSettings != NULL) {
        memcpy(gpSDCardSettings, pSettings, sizeof (sd_card_manager_settings_t));
    }
    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_DEINIT;
    return true;
}

bool sd_card_manager_IsIdle() {
    return (gSDCardData.currentProcessState == SD_CARD_MANAGER_PROCESS_STATE_IDLE ||
            gSDCardData.currentProcessState == SD_CARD_MANAGER_PROCESS_STATE_INIT);
}

bool sd_card_manager_WaitForCompletion(uint32_t timeoutMs) {
    if (sd_card_manager_IsIdle()) {
        return true;  // Already idle
    }

    TickType_t timeout = (timeoutMs == 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);

    // Wait for operation complete semaphore
    if (xSemaphoreTake(gSDCardData.opCompleteSemaphore, timeout) == pdTRUE) {
        return true;  // Operation completed
    } else {
        LOG_E("[SD] WaitForCompletion timeout after %u ms\r\n", timeoutMs);
        return false;  // Timeout
    }
}

bool sd_card_manager_GetLastOperationResult(void) {
    // If SD manager hasn't been initialized yet, report failure
    if (gpSDCardSettings == NULL) {
        return false;
    }
    return gSDCardData.lastOperationSuccess;
}

bool sd_card_manager_IsBusy(void) {
    // If SD manager hasn't been initialized yet, treat as busy/unavailable
    if (gpSDCardSettings == NULL) {
        return true;
    }

    // Note: This function is not fully atomic (no mutex). The two checks below
    // could see inconsistent state if modified by another task between them.
    // This is acceptable for pre-operation checks where false negatives during
    // brief state transitions are tolerable.

    // Busy if any operation mode is active (WRITE, READ, LIST, DELETE, FORMAT)
    // Mode is set when operation starts and cleared when complete
    if (gpSDCardSettings->mode != SD_CARD_MANAGER_MODE_NONE) {
        return true;
    }

    // Also check state machine for transient states during initialization
    switch (gSDCardData.currentProcessState) {
        case SD_CARD_MANAGER_PROCESS_STATE_IDLE:
        case SD_CARD_MANAGER_PROCESS_STATE_INIT:
        case SD_CARD_MANAGER_PROCESS_STATE_ERROR:
            return false;
        default:
            return true;
    }
}


size_t sd_card_manager_GetWriteBuffFreeSize() {
    static bool logged = false;
    if (!logged) {
        LOG_D("SD_MGR: GetWriteBuffFreeSize: enable=%d, mode=%d (WRITE=%d)",
              gpSDCardSettings->enable, gpSDCardSettings->mode, SD_CARD_MANAGER_MODE_WRITE);
        logged = true;
    }

    if (gpSDCardSettings->enable != 1 || gpSDCardSettings->mode != SD_CARD_MANAGER_MODE_WRITE) {
        return 0;
    }

    // Must protect circular buffer access with mutex
    SD_TakeMutexDebug(gSDCardData.wMutex, "get_free_size");
    size_t freeSize = CircularBuf_NumBytesFree(&gSDCardData.wCirbuf);
    xSemaphoreGive(gSDCardData.wMutex);

    if (!logged) {
        LOG_D("SD_MGR: Returning freeSize=%u", freeSize);
    }

    return freeSize;
}


