/* ************************************************************************** */
/** Descriptive File Name

  @Company
    Company Name

  @File Name
    filename.h

  @Summary
    Brief description of the file.

  @Description
    Describe the purpose of this file.
 */
/* ************************************************************************** */

#ifndef _SD_CARD_H    /* Guard against multiple inclusion */
#define _SD_CARD_H

#include <stdlib.h>
#include <string.h>

// Harmony
#include "configuration.h"
#include "definitions.h"
#include "services/daqifi_settings.h"
#include "Util/CircularBuffer.h"

#define SD_CARD_CONF_RBUFFER_SIZE 500
#define SD_CARD_CONF_WBUFFER_SIZE 1024
#define SD_CARD_CONF_DIR_NAME_LEN_MAX 10
#define SD_CARD_CONF_FILE_NAME_LEN_MAX 10

//non configurable definitions
#define SD_CARD_FILE_PATH_LEN_MAX (SD_CARD_CONF_DIR_NAME_LEN_MAX+SD_CARD_CONF_FILE_NAME_LEN_MAX+2)


/* Provide C++ Compatibility */
#ifdef __cplusplus
extern "C" {
#endif

    typedef enum {
        SD_CARD_STATE_MOUNT_DISK,
        SD_CARD_STATE_UNMOUNT_DISK,
        SD_CARD_STATE_MOUNT_DISK_AGAIN,
        SD_CARD_STATE_SET_CURRENT_DRIVE,
        SD_CARD_STATE_CREATE_DIRECTORY,
        SD_CARD_STATE_OPEN_FILE,
        SD_CARD_STATE_WRITE_TO_FILE,
        SD_CARD_STATE_READ_FROM_FILE,
        SD_CARD_STATE_CLOSE_FILE,
        SD_CARD_STATE_IDLE,
        SD_CARD_STATE_ERROR,
    }sdCardState_t;

    /**
     * Data for a particular TCP client
     */
    typedef struct {
        sdCardState_t processState;
        /** Client read buffer */
        uint8_t readBuffer[SD_CARD_CONF_RBUFFER_SIZE];

        /** The current length of the read buffer */
        size_t readBufferLength;

        /** Client write buffer */
        uint8_t writeBuffer[SD_CARD_CONF_WBUFFER_SIZE];

        /** The current length of the write buffer */
        size_t writeBufferLength;

        CircularBuf_t wCirbuf;

        SemaphoreHandle_t wMutex;
        
        char filePath[SD_CARD_FILE_PATH_LEN_MAX+1];
        
        SYS_FS_HANDLE fileHandle;

        bool sdCardWritePending;
        uint16_t sdCardWriteBufferOffset;
    } sdCardData_t;
    typedef struct{
        char directory[SD_CARD_CONF_DIR_NAME_LEN_MAX+1];
        char file[SD_CARD_CONF_FILE_NAME_LEN_MAX+1];
    }sdCardSettings_t;
    bool SDCard_Init();
    bool SDCard_Deinit();
    void SDCard_ProcessState();
    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _SD_CARD_H */

/* *****************************************************************************
 End of File
 */
