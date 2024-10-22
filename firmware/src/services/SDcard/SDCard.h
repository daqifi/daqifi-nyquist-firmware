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

#define SD_CARD_CONF_RBUFFER_SIZE 512
#define SD_CARD_CONF_WBUFFER_SIZE 1200
#define SD_CARD_CONF_CIRCULAR_BUFFER_SIZE (SD_CARD_CONF_WBUFFER_SIZE*4)
#define SD_CARD_CONF_DIR_NAME_LEN_MAX 40
#define SD_CARD_CONF_FILE_NAME_LEN_MAX 40

//non configurable definitions
#define SD_CARD_FILE_PATH_LEN_MAX (SYS_FS_FILE_NAME_LEN*2)


/* Provide C++ Compatibility */
#ifdef __cplusplus
extern "C" {
#endif

    typedef enum {
        SD_CARD_PROCESS_STATE_INIT,
        SD_CARD_PROCESS_STATE_MOUNT_DISK,
        SD_CARD_PROCESS_STATE_UNMOUNT_DISK,
        SD_CARD_PROCESS_STATE_CURRENT_DRIVE,
        SD_CARD_PROCESS_STATE_CREATE_DIRECTORY,
        SD_CARD_PROCESS_STATE_OPEN_FILE,
        SD_CARD_PROCESS_STATE_WRITE_TO_FILE,
        SD_CARD_PROCESS_STATE_READ_FROM_FILE,
        SD_CARD_PROCESS_STATE_LIST_DIR,
        SD_CARD_PROCESS_STATE_DEINIT,
        SD_CARD_PROCESS_STATE_IDLE,
        SD_CARD_PROCESS_STATE_ERROR,
    }SDCard_processState_t;
    typedef enum{
        SD_CARD_MODE_NONE,
        SD_CARD_MODE_READ,
        SD_CARD_MODE_WRITE,           
        SD_CARD_MODE_LIST_DIRECTORY,
    }SDCard_mode_t;

    

    /**
     * Data for a particular TCP client
     */
    typedef struct {
        SDCard_processState_t currentProcessState;
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
        uint16_t totalBytesFlushPending;
        uint64_t lastFlushMillis;
        bool discMounted;
    } SDCard_data_t;
    
    typedef struct{
        bool enable;
        SDCard_mode_t mode;
        char directory[SD_CARD_CONF_DIR_NAME_LEN_MAX+1];
        char file[SD_CARD_CONF_FILE_NAME_LEN_MAX+1];
    }SDCard_RuntimeConfig_t;
    
    
    bool SDCard_Init(SDCard_RuntimeConfig_t *pSettings);
    bool SDCard_Deinit();
    bool SDCard_UpdateSettings(SDCard_RuntimeConfig_t *pSettings);
    void SDCard_ProcessState();
    size_t SDCard_WriteToBuffer(const char* pData, size_t len);
    size_t SDCard_WriteBuffFreeSize();
    void SDCard_DataReadyCB(SDCard_mode_t mode, uint8_t *pDataBuff, size_t dataLen);
    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _SD_CARD_H */

/* *****************************************************************************
 End of File
 */
