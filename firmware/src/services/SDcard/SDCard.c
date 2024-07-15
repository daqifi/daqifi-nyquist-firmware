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
#include "SDCard.h"
#include "string.h"
#define SDCARD_MOUNT_NAME    "/mnt/Daqifi"
#define SDCARD_DEV_NAME      "/dev/mmcblka1"


sdCardData_t gSdCardData;
sdCardSettings_t gSdCardSettings;

static int SDCardFlush() {
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
    return SDCardFlush();
}

bool SDCard_Init() {
    static bool isInitDone = false;
    if (!isInitDone) {
        CircularBuf_Init(&gSdCardData.wCirbuf,
                CircularBufferToSDWrite,
                (SD_CARD_CONF_WBUFFER_SIZE * 4));
        gSdCardData.wMutex = xSemaphoreCreateMutex();
        xSemaphoreGive(gSdCardData.wMutex);
        isInitDone = true;
    }
    return true;
}

void SDCard_ProcessState() {
    /* Check the application's current state. */
    while (1) {
        switch (gSdCardData.processState) {
            case SD_CARD_STATE_MOUNT_DISK:
                gSdCardData.sdCardWritePending = 0;
                gSdCardData.writeBufferLength = 0;
                gSdCardData.sdCardWriteBufferOffset = 0;
                if (SYS_FS_Mount(SDCARD_DEV_NAME, SDCARD_MOUNT_NAME, FAT, 0, NULL) != 0) {
                    /* The disk could not be mounted. Try
                     * mounting again until success. */
                    gSdCardData.processState = SD_CARD_STATE_MOUNT_DISK;
                } else {
                    /* Mount was successful. Unmount the disk, for testing. */
                    gSdCardData.processState = SD_CARD_STATE_UNMOUNT_DISK;
                }
                break;

            case SD_CARD_STATE_UNMOUNT_DISK:
                if (SYS_FS_Unmount(SDCARD_MOUNT_NAME) != 0) {
                    /* The disk could not be un mounted. Try
                     * un mounting again untill success. */

                    gSdCardData.processState = SD_CARD_STATE_UNMOUNT_DISK;
                } else {
                    /* UnMount was successful. Mount the disk again */
                    gSdCardData.processState = SD_CARD_STATE_MOUNT_DISK_AGAIN;
                }
                break;

            case SD_CARD_STATE_MOUNT_DISK_AGAIN:
                if (SYS_FS_Mount(SDCARD_DEV_NAME, SDCARD_MOUNT_NAME, FAT, 0, NULL) != 0) {
                    /* The disk could not be mounted. Try
                     * mounting again until success. */
                    gSdCardData.processState = SD_CARD_STATE_MOUNT_DISK_AGAIN;
                } else {
                    /* Mount was successful. Set current drive so that we do not have to use absolute path. */
                    gSdCardData.processState = SD_CARD_STATE_SET_CURRENT_DRIVE;
                }
                break;

            case SD_CARD_STATE_SET_CURRENT_DRIVE:
                if (SYS_FS_CurrentDriveSet(SDCARD_MOUNT_NAME) == SYS_FS_RES_FAILURE) {
                    /* Error while setting current drive */
                    gSdCardData.processState = SD_CARD_STATE_ERROR;
                } else {
                    /* Open a file for reading. */
                    gSdCardData.processState = SD_CARD_STATE_CREATE_DIRECTORY;
                }
                break;

            case SD_CARD_STATE_CREATE_DIRECTORY:
                if (SYS_FS_DirectoryMake(gSdCardSettings.directory) == SYS_FS_RES_FAILURE) {
                    /* Error while creating a new drive */
                    gSdCardData.processState = SD_CARD_STATE_MOUNT_DISK_AGAIN;
                } else {
                    /* Open a second file for writing. */
                    gSdCardData.processState = SD_CARD_STATE_OPEN_FILE;
                }
                break;

            case SD_CARD_STATE_OPEN_FILE:
                memset(gSdCardData.filePath, 0, sizeof (gSdCardData.filePath));
                snprintf(gSdCardData.filePath, SD_CARD_FILE_PATH_LEN_MAX, "%s/%s",
                        gSdCardSettings.directory, gSdCardSettings.file);
                gSdCardData.fileHandle = SYS_FS_FileOpen(gSdCardData.filePath,
                        (SYS_FS_FILE_OPEN_APPEND_PLUS));

                if (gSdCardData.fileHandle == SYS_FS_HANDLE_INVALID) {
                    /* Could not open the file. Error out*/
                    gSdCardData.processState = SD_CARD_STATE_ERROR;
                } else {
                    /* Read from one file and write to another file */
                    gSdCardData.processState = SD_CARD_STATE_WRITE_TO_FILE;
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
                    gSdCardData.processState = SD_CARD_STATE_ERROR;
                }
            }
                break;

            case SD_CARD_STATE_CLOSE_FILE:
                /* Close both files */
                SYS_FS_FileClose(gSdCardData.fileHandle);
                gSdCardData.processState = SD_CARD_STATE_IDLE;
                break;

            case SD_CARD_STATE_IDLE:
                /* The application comes here when the demo has completed
                 * successfully. Glow LED1. */
                //LED_ON();
                break;

            case SD_CARD_STATE_ERROR:
                /* The application comes here when the demo has failed. */
                SYS_FS_FileClose(gSdCardData.fileHandle);
                break;

            default:
                break;
        }
        vTaskDelay(5);
    }
}