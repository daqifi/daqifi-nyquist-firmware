/* ************************************************************************** */
/** Descriptive File Name

  @Company
    Company Name

  @File Name
    filename.c

  @Summary
    Brief description of the file.

  @Description
    Describe the purpose of this file.
 */
#include <string.h>
#include "Util/Logger.h"
#include "SDCard.h"
#define SDCARD_MOUNT_NAME    "/mnt/Daqifi"
#define SDCARD_DEV_NAME      "/dev/mmcblka1"


SDCard_data_t gSdCardData;
SDCard_Settings_t *gpSdCardSettings;

static int SDCardFlush() {
    int writeLen = -1;
    if (gSdCardData.fileHandle == SYS_FS_HANDLE_INVALID) {
        goto __exit;
    }    
    SYS_FS_FileWrite(gSdCardData.fileHandle,
            (const void *) (gSdCardData.writeBuffer + gSdCardData.sdCardWriteBufferOffset),
            gSdCardData.writeBufferLength);
    if(SYS_FS_FileSync(gSdCardData.fileHandle)!=SYS_FS_RES_SUCCESS){
        writeLen=-1;
    }
__exit:
    return writeLen;
}

static int CircularBufferToSDWrite(uint8_t* buf, uint16_t len) {
    if (len>sizeof (gSdCardData.writeBuffer))
        return false;
    memcpy(gSdCardData.writeBuffer, buf, len);
    gSdCardData.writeBufferLength = len;
    gSdCardData.sdCardWriteBufferOffset = 0;
    return SDCardFlush();
}

bool SDCard_Init(SDCard_Settings_t *pSettings) {
    static bool isInitDone = false;
    if (!isInitDone) {
        CircularBuf_Init(&gSdCardData.wCirbuf,
                CircularBufferToSDWrite,
                (SD_CARD_CONF_WBUFFER_SIZE * 4));
        gSdCardData.wMutex = xSemaphoreCreateMutex();
        xSemaphoreGive(gSdCardData.wMutex);
        isInitDone = true;
        gpSdCardSettings = pSettings;
        gSdCardData.fileHandle = SYS_FS_HANDLE_INVALID;
        gSdCardData.currentProcessState = SD_CARD_STATE_INIT;
    }
    return true;
}

void SDCard_ProcessState() {
    /* Check the application's current state. */

    switch (gSdCardData.currentProcessState) {
        case SD_CARD_STATE_INIT:
            if (gpSdCardSettings->enable &&
                    strlen(gpSdCardSettings->directory) > 0 &&
                    strlen(gpSdCardSettings->directory) <= SD_CARD_CONF_DIR_NAME_LEN_MAX &&
                    strlen(gpSdCardSettings->file) > 0 &&
                    strlen(gpSdCardSettings->directory) <= SD_CARD_CONF_FILE_NAME_LEN_MAX) {
                gSdCardData.currentProcessState = SD_CARD_STATE_MOUNT_DISK;
            } else if (gpSdCardSettings->enable) {
                LOG_E("[%s:%d]Invalid SD Card Directory or file name", __FILE__, __LINE__);
            }
            break;
        case SD_CARD_STATE_MOUNT_DISK:
            gSdCardData.sdCardWritePending = 0;
            gSdCardData.writeBufferLength = 0;
            gSdCardData.sdCardWriteBufferOffset = 0;
            if (SYS_FS_Mount(SDCARD_DEV_NAME, SDCARD_MOUNT_NAME, FAT, 0, NULL) != 0) {
                /* The disk could not be mounted. Try
                 * mounting again until success. */
                gSdCardData.currentProcessState = SD_CARD_STATE_MOUNT_DISK;
            } else {
                /* Mount was successful. Unmount the disk, for testing. */
                gSdCardData.currentProcessState = SD_CARD_STATE_SET_CURRENT_DRIVE;
            }
            break;

        case SD_CARD_STATE_UNMOUNT_DISK:
            if (gSdCardData.fileHandle != SYS_FS_HANDLE_INVALID) {
                SYS_FS_FileClose(gSdCardData.fileHandle);
                gSdCardData.fileHandle = SYS_FS_HANDLE_INVALID;
            }
            if (SYS_FS_Unmount(SDCARD_MOUNT_NAME) != 0) {
                /* The disk could not be un mounted. Try
                 * un mounting again untill success. */
                gSdCardData.currentProcessState = SD_CARD_STATE_UNMOUNT_DISK;
            } else {
                if (!gpSdCardSettings->enable) {
                    gSdCardData.currentProcessState = SD_CARD_STATE_INIT;
                } else {
                    gSdCardData.currentProcessState = SD_CARD_STATE_MOUNT_DISK;
                }
            }
            break;

        case SD_CARD_STATE_SET_CURRENT_DRIVE:
            if (SYS_FS_CurrentDriveSet(SDCARD_MOUNT_NAME) == SYS_FS_RES_FAILURE) {
                /* Error while setting current drive */
                LOG_E("[%s:%d]Error Setting SD Card drive", __FILE__, __LINE__);
                gSdCardData.currentProcessState = SD_CARD_STATE_ERROR;
            } else {
                /* Open a file for reading. */
                gSdCardData.currentProcessState = SD_CARD_STATE_CREATE_DIRECTORY;
            }
            break;

        case SD_CARD_STATE_CREATE_DIRECTORY:
            if (SYS_FS_DirectoryMake(gpSdCardSettings->directory) == SYS_FS_RES_FAILURE) {
                if (SYS_FS_Error() == SYS_FS_ERROR_EXIST) {
                    gSdCardData.currentProcessState = SD_CARD_STATE_OPEN_FILE;
                } else {
                    gSdCardData.currentProcessState = SD_CARD_STATE_ERROR;
                    LOG_E("[%s:%d]Invalid SD Card Directory name", __FILE__, __LINE__);
                }
                /* Error while creating a new drive */
            } else {
                /* Open a second file for writing. */
                gSdCardData.currentProcessState = SD_CARD_STATE_OPEN_FILE;
            }
            break;

        case SD_CARD_STATE_OPEN_FILE:
            memset(gSdCardData.filePath, 0, sizeof (gSdCardData.filePath));
            snprintf(gSdCardData.filePath, SD_CARD_FILE_PATH_LEN_MAX, "%s/%s",
                    gpSdCardSettings->directory, gpSdCardSettings->file);
            if (gpSdCardSettings->mode == SD_CARD_MODE_WRITE) {
                gSdCardData.fileHandle = SYS_FS_FileOpen(gSdCardData.filePath,
                        (SYS_FS_FILE_OPEN_APPEND_PLUS));
                gSdCardData.currentProcessState = SD_CARD_STATE_WRITE_TO_FILE;
            } else if (gpSdCardSettings->mode == SD_CARD_MODE_READ) {
                gSdCardData.fileHandle = SYS_FS_FileOpen(gSdCardData.filePath,
                        (SYS_FS_FILE_OPEN_READ));
                gSdCardData.currentProcessState = SD_CARD_STATE_READ_FROM_FILE;
            } else if (gpSdCardSettings->mode == SD_CARD_MODE_NONE) {
                gSdCardData.fileHandle = SYS_FS_FileOpen(gSdCardData.filePath,
                        (SYS_FS_FILE_OPEN_READ));
                gSdCardData.currentProcessState = SD_CARD_STATE_IDLE;
            }
            if (gSdCardData.fileHandle == SYS_FS_HANDLE_INVALID) {
                /* Could not open the file. Error out*/
                gSdCardData.currentProcessState = SD_CARD_STATE_ERROR;
                LOG_E("[%s:%d]Invalid SD Card file name", __FILE__, __LINE__);
            }
            break;
        case SD_CARD_STATE_WRITE_TO_FILE:
        {
            /* If read was success, try writing to the new file */
            int writeLen = -2;
            if (CircularBuf_NumBytesAvailable(&gSdCardData.wCirbuf) > 0
                    && gSdCardData.sdCardWritePending != 1) {
                xSemaphoreTake(gSdCardData.wMutex, portMAX_DELAY);
                gSdCardData.sdCardWritePending = 1;
                CircularBuf_ProcessBytes(&gSdCardData.wCirbuf, NULL, SD_CARD_CONF_WBUFFER_SIZE, &writeLen);
                xSemaphoreGive(gSdCardData.wMutex);
            } else if (gSdCardData.sdCardWritePending == 1) {
                writeLen = SDCardFlush();
            }

            if (writeLen >= gSdCardData.writeBufferLength) {
                gSdCardData.sdCardWritePending = 0;
                gSdCardData.writeBufferLength = 0;
                gSdCardData.sdCardWriteBufferOffset = 0;
            } else if (writeLen >= 0) {
                gSdCardData.writeBufferLength -= writeLen;
                gSdCardData.sdCardWriteBufferOffset = writeLen;
            } else {
                gSdCardData.currentProcessState = SD_CARD_STATE_ERROR;
                LOG_E("[%s:%d]Error Writing to SD Card", __FILE__, __LINE__);
            }
        }
            break;
        case SD_CARD_STATE_READ_FROM_FILE:
            break;
        case SD_CARD_STATE_IDLE:
            /* The application comes here when the demo has completed
             * successfully. Glow LED1. */
            //LED_ON();
            break;
        case SD_CARD_STATE_DEINIT:
            gSdCardData.currentProcessState = SD_CARD_STATE_UNMOUNT_DISK;
            break;
        case SD_CARD_STATE_ERROR:
            /* The application comes here when the demo has failed. */
            gSdCardData.currentProcessState = SD_CARD_STATE_UNMOUNT_DISK;
            break;

        default:
            break;
    }
}

size_t SDCard_WriteToBuffer(const char* pData, size_t len) {
    size_t bytesAdded = 0;
    if (len == 0)return 0;
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

bool SDCard_Deinit() {
    gSdCardData.currentProcessState = SD_CARD_STATE_DEINIT;
    gpSdCardSettings->enable = 0;
    return true;
}

bool SDCard_UpdateSettings(SDCard_Settings_t *pSettings) {
    if (pSettings != NULL && gpSdCardSettings != NULL) {
        memcpy(gpSdCardSettings, pSettings, sizeof (SDCard_Settings_t));
    }
    gSdCardData.currentProcessState = SD_CARD_STATE_DEINIT;
    return true;
}