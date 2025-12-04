#ifndef _SD_CARD_MANAGER_H    /* Guard against multiple inclusion */
#define _SD_CARD_MANAGER_H

#include <stdlib.h>
#include <string.h>

// Harmony
#include "configuration.h"
#include "definitions.h"
#include "services/daqifi_settings.h"
#include "Util/CircularBuffer.h"

#define SD_CARD_MANAGER_CONF_RBUFFER_SIZE 512   // Small buffer, send directory listings in chunks
#define SD_CARD_MANAGER_CONF_WBUFFER_SIZE 8192  // Increased to 8KB for 5kHz streaming support

#define SD_CARD_MANAGER_CONF_DIR_NAME_LEN_MAX 40
#define SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX 40

// File size limits for automatic splitting (FAT32 protection)
// FAT32 max: 2^32 - 1 bytes (4GB - 1 byte)
// Safety margin: 100MB to account for filesystem metadata and overhead
#define SD_CARD_MANAGER_FAT32_MAX_FILE_SIZE 4294967295ULL  // 4GB - 1 byte
#define SD_CARD_MANAGER_FAT32_SAFETY_MARGIN (100ULL * 1024ULL * 1024ULL)  // 100MB
#define SD_CARD_MANAGER_FAT32_SAFE_MAX_FILE_SIZE (SD_CARD_MANAGER_FAT32_MAX_FILE_SIZE - SD_CARD_MANAGER_FAT32_SAFETY_MARGIN)  // 4GB - 100MB

// Performance tuning parameters
#define SD_CARD_MANAGER_WRITE_TIMEOUT_MS 2000      // Timeout for WriteToBuffer operation
#define SD_CARD_MANAGER_WRITE_WAIT_INTERVAL_MS 1  // Wait interval when buffer is full
#define SD_CARD_MANAGER_MAX_CHUNKS_PER_CYCLE 4     // Max chunks to process per task cycle (4 * 5KB = 20KB)
#define SD_CARD_MANAGER_TASK_DELAY_MS 1            // Task delay for SD card processing (reduced from 5ms)


/* Provide C++ Compatibility */
#ifdef __cplusplus
extern "C" {
#endif

    typedef enum {
        SD_CARD_MANAGER_MODE_NONE,
        SD_CARD_MANAGER_MODE_READ,
        SD_CARD_MANAGER_MODE_WRITE,
        SD_CARD_MANAGER_MODE_LIST_DIRECTORY,
        SD_CARD_MANAGER_MODE_DELETE_FILE,
        SD_CARD_MANAGER_MODE_FORMAT,
    } sd_card_manager_mode_t;

    typedef struct {
        bool enable;
        sd_card_manager_mode_t mode;
        char directory[SD_CARD_MANAGER_CONF_DIR_NAME_LEN_MAX + 1];
        char file[SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX + 1];
        uint64_t maxFileSizeBytes;  // Max file size before auto-split (0 = unlimited)
    } sd_card_manager_settings_t;


    /**
     * @brief Initializes the SD card manager module with the provided settings.
     *
     * This function sets up the SD card manager by initializing internal data structures,
     * configuring the circular buffer, and preparing the module for operation.
     * It should be called before using any other functions in the SD card manager.
     *
     * @param[in] pSettings Pointer to the sd_card_manager_settings_t structure containing
     *                      configuration settings for the SD card manager.
     *
     * @return Returns true if initialization is successful; otherwise, returns false.
     *
     * @note This function is not thread-safe and should be called once during system initialization.
     */
    bool sd_card_manager_Init(sd_card_manager_settings_t *pSettings);

    /**
     * @brief Deinitializes the SD card manager module and releases resources.
     *
     * This function stops any ongoing operations, unmounts the SD card, and cleans up
     * internal data structures. After calling this function, the SD card manager functions
     * should not be used unless re-initialized with sd_card_manager_Init().
     *
     * @return Returns true if deinitialization is successful; otherwise, returns false.
     *
     * @note Ensure that all write operations are completed before calling this function
     *       to prevent data loss.
     */
    bool sd_card_manager_Deinit(void);

    /**
     * @brief Updates the settings of the SD card manager at runtime.
     *
     * This function allows changing the SD card manager's configuration while it is running.
     * It applies the new settings and reinitializes internal states as necessary.
     *
     * @param[in] pSettings Pointer to the sd_card_manager_settings_t structure containing
     *                      the new configuration settings.
     *
     * @return Returns true if the settings are updated successfully; otherwise, returns false.
     *
     * @warning Updating settings during active operations may lead to undefined behavior.
     *          It is recommended to stop ongoing operations before calling this function.
     */
    bool sd_card_manager_UpdateSettings(sd_card_manager_settings_t *pSettings);

    /**
     * @brief Processes the SD card manager's state machine.
     *
     * This function should be called periodically, preferably in the main loop or a dedicated
     * task. It handles state transitions and performs actions such as mounting the SD card,
     * reading/writing files, and handling errors.
     *
     * @note This function is non-blocking and will return immediately if no action is required.
     */
    void sd_card_manager_ProcessState(void);

    /**
     * @brief Writes data to the SD card manager's write buffer.
     *
     * This function adds data to the internal circular buffer for writing to the SD card.
     * If the buffer does not have enough space, the function will block until space becomes available.
     *
     * @param[in] pData Pointer to the data to be written.
     * @param[in] len   Length of the data in bytes.
     *
     * @return The number of bytes successfully added to the buffer.
     *
     * @note This function is thread-safe and can be called from multiple contexts.
     *       Ensure that the SD card manager is initialized and in write mode before calling.
     */
    size_t sd_card_manager_WriteToBuffer(const char* pData, size_t len);

    /**
     * @brief Retrieves the amount of free space available in the write buffer.
     *
     * This function returns the number of bytes currently available in the SD card manager's
     * internal circular write buffer.
     *
     * @return The number of free bytes in the write buffer.
     *
     * @note This function can be used to check if there is enough space before attempting
     *       to write data using sd_card_manager_WriteToBuffer().
     */
    size_t sd_card_manager_GetWriteBuffFreeSize(void);

    /**
     * @brief Checks if the SD card manager is currently idle (not processing any operation).
     *
     * @return true if idle, false if busy processing an operation
     */
    bool sd_card_manager_IsIdle(void);

    /**
     * @brief Waits for the current SD card operation to complete.
     *
     * @param[in] timeoutMs Maximum time to wait in milliseconds (0 = wait forever)
     * @return true if operation completed, false if timeout occurred
     */
    bool sd_card_manager_WaitForCompletion(uint32_t timeoutMs);

    /**
     * @brief Gets the result of the last completed operation.
     *
     * @return true if last operation succeeded, false if it failed
     */
    bool sd_card_manager_GetLastOperationResult(void);

    /**
     * @brief Checks if the SD card manager is busy with an active operation.
     *
     * This should be called before starting any new SD operation to prevent
     * conflicts. Returns true if:
     * - Actively writing/logging to SD card
     * - Delete, format, list, or read operation is in progress
     *
     * @note This function is not fully atomic - it checks mode and state separately.
     *       This is acceptable for the intended use case (pre-operation check) where
     *       occasional false negatives during state transitions are tolerable.
     *
     * @return true if busy, false if available for new operations
     */
    bool sd_card_manager_IsBusy(void);

    /**
     * @brief Callback function invoked when data is ready after read or directory listing operations.
     *
     * This weakly linked function should be implemented by the user to handle data received
     * from read operations or directory listings. It provides the mode of operation, a pointer
     * to the data buffer, and the length of the data.
     *
     * @param[in] mode       The operation mode that triggered the callback (read or list directory).
     * @param[in] pDataBuff  Pointer to the buffer containing the data.
     * @param[in] dataLen    Length of the data in bytes.
     *
     * @note This function is called from the context of the SD card manager's state machine.
     *       Processing within this callback should be kept minimal to prevent blocking the state machine.
     */
    void sd_card_manager_DataReadyCB(sd_card_manager_mode_t mode, uint8_t *pDataBuff, size_t dataLen);

    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _SD_CARD_H */

/* *****************************************************************************
 End of File
 */
