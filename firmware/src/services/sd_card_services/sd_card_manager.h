#ifndef _SD_CARD_MANAGER_H    /* Guard against multiple inclusion */
#define _SD_CARD_MANAGER_H

#include <stdlib.h>
#include <string.h>

// Harmony
#include "configuration.h"
#include "definitions.h"
#include "services/daqifi_settings.h"
#include "Util/CircularBuffer.h"

#define SD_CARD_MANAGER_CONF_RBUFFER_SIZE 512
#define SD_CARD_MANAGER_CONF_WBUFFER_SIZE 1200

#define SD_CARD_MANAGER_CONF_DIR_NAME_LEN_MAX 40
#define SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX 40


/* Provide C++ Compatibility */
#ifdef __cplusplus
extern "C" {
#endif

    typedef enum {
        SD_CARD_MANAGER_MODE_NONE,
        SD_CARD_MANAGER_MODE_READ,
        SD_CARD_MANAGER_MODE_WRITE,
        SD_CARD_MANAGER_MODE_LIST_DIRECTORY,
    } sd_card_manager_mode_t;

    typedef struct {
        bool enable;
        sd_card_manager_mode_t mode;
        char directory[SD_CARD_MANAGER_CONF_DIR_NAME_LEN_MAX + 1];
        char file[SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX + 1];
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
