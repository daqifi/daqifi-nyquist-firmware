/*******************************************************************************
  MPLAB Harmony Application Source File
  
  Company:
    Microchip Technology Inc.
  
  File Name:
    app_freertos.c

  Summary:
    This file contains the source code for the MPLAB Harmony application.

  Description:
    This file contains the source code for the MPLAB Harmony application.  It 
    implements the logic of the application's state machine and it may call 
    API routines of other MPLAB Harmony modules in the system, such as drivers,
    system services, and middleware.  However, it does not call any of the
    system interfaces (such as the "Initialize" and "Tasks" functions) of any of
    the modules in the system or make any assumptions about when those functions
    are called.  That is the responsibility of the configuration-specific system
    files.
 *******************************************************************************/

// DOM-IGNORE-BEGIN
/*******************************************************************************
 * Copyright (C) 2018 Microchip Technology Inc. and its subsidiaries.
 *
 * Subject to your compliance with these terms, you may use Microchip software
 * and any derivatives exclusively with Microchip products. It is your
 * responsibility to comply with third party license terms applicable to your
 * use of third party software (including open source software) that may
 * accompany Microchip software.
 *
 * THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
 * EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED
 * WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A
 * PARTICULAR PURPOSE.
 *
 * IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
 * INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
 * WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS
 * BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO THE
 * FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN
 * ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
 * THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
 *******************************************************************************/
// DOM-IGNORE-END


// *****************************************************************************
// *****************************************************************************
// Section: Included Files 
// *****************************************************************************
// *****************************************************************************

#include "app_freertos.h"
#include "wdrv_winc_client_api.h"
#include "queue.h"
#include <inttypes.h>
#include <services/UsbCdc/UsbCdc.h>
#include "services/DaqifiPB/DaqifiOutMessage.pb.h"
#include "services/DaqifiPB/NanoPB_Encoder.h"
#include "services/Wifi/WifiApi.h"
#include "services/SDcard/SDCard.h"
// *****************************************************************************
// *****************************************************************************
// Section: Global Data Definitions
// *****************************************************************************
// *****************************************************************************


// *****************************************************************************
/* Application Data

  Summary:
    Holds application data

  Description:
    This structure holds the application's data.

  Remarks:
    This structure should be initialized by the APP_Initialize function.
    
    Application strings and buffers are be defined outside this structure.
 */


//! Pointer to board data information 
static tBoardData * gpBoardData;
static tBoardRuntimeConfig * gpBoardRuntimeConfig;
extern const NanopbFlagsArray fields_discovery;

static void SystemInit();
static void USBDeviceTask(void* p_arg);
static void WifiTask(void* p_arg);
static void SdCardTask(void* p_arg);

void WifiApi_FormUdpAnnouncePacketCallback(WifiSettings *pSettings, uint8_t* pBuff, uint16_t *len) {
    tBoardData * pBoardData = (tBoardData *) BoardData_Get(
            BOARDDATA_ALL_DATA,
            0);
    pBoardData->wifiSettings.ipAddr.Val = pSettings->ipAddr.Val;
    memcpy(pBoardData->wifiSettings.macAddr.addr, pSettings->macAddr.addr, WDRV_WINC_MAC_ADDR_LEN);
    size_t count = Nanopb_Encode(
            pBoardData,
            &fields_discovery,
            pBuff,*len);
    *len = count;
}

static void USBDeviceTask(void* p_arg) {
    UsbCdc_Initialize();
    while (1) {
        UsbCdc_ProcessState();
        vTaskDelay(5);
    }
}

static void WifiTask(void* p_arg) {
    int count=0;
    WifiApi_Init(&gpBoardData->wifiSettings);
    char buff[100];
    while (1) {
        WifiApi_ProcessState();
        memset(buff,0,100);
        sprintf(buff,"\r\nCount%d",count++);
        SDCard_WriteToBuffer(buff,strlen(buff));
        vTaskDelay(5);
    }
}

static void SdCardTask(void* p_arg) {
    static SDCard_Settings_t sdSettings;
    sdSettings.enable = 1;
    sdSettings.mode = SD_CARD_MODE_WRITE;
    memset(sdSettings.directory, 0, sizeof (sdSettings.directory));
    strncpy(sdSettings.directory, "MyTest1", strlen("MyTest1") + 1);
    memset(sdSettings.file, 0, sizeof (sdSettings.file));
    strncpy(sdSettings.file, "file2.txt", strlen("file2.txt") + 1);
    SDCard_Init(&sdSettings);
    while (1) {
        SDCard_ProcessState();
        vTaskDelay(5);
    }
}

void SystemInit() {
    DaqifiSettings tmpTopLevelSettings;
    DaqifiSettings tmpSettings;

    gpBoardData = BoardData_Get(
            BOARDDATA_ALL_DATA,
            0);

    gpBoardRuntimeConfig = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_ALL_CONFIG);



    // Initialize the variable to 0s
    memset(&tmpTopLevelSettings, 0, sizeof (tmpTopLevelSettings));
    tmpTopLevelSettings.type = DaqifiSettings_TopLevelSettings;

    // Try to load TopLevelSettings from NVM - if this fails, store default 
    // settings to NVM (first run after a program)
    if (!daqifi_settings_LoadFromNvm(
            DaqifiSettings_TopLevelSettings,
            &tmpTopLevelSettings)) {
        // Get board variant and cal param type from TopLevelSettings NVM 
        daqifi_settings_LoadFactoryDeafult(
                DaqifiSettings_TopLevelSettings,
                &tmpTopLevelSettings);
        daqifi_settings_SaveToNvm(&tmpTopLevelSettings);
    }

    // Load board config structures with the correct board variant values
    //    InitBoardConfig(&tmpTopLevelSettings.settings.topLevelSettings);
    InitBoardRuntimeConfig(tmpTopLevelSettings.settings.topLevelSettings.boardVariant);
    InitializeBoardData(gpBoardData);

    // Try to load WiFiSettings from NVM - if this fails, store default 
    // settings to NVM (first run after a program)

    // Initialize the variable to 0s
    memset(&tmpSettings, 0, sizeof (tmpSettings));
    tmpSettings.type = DaqifiSettings_Wifi;

    if (!daqifi_settings_LoadFromNvm(DaqifiSettings_Wifi, &tmpSettings)) {
        // Get board wifi settings from Wifi NVM variable
        daqifi_settings_LoadFactoryDeafult(DaqifiSettings_Wifi, &tmpSettings);
        daqifi_settings_SaveToNvm(&tmpSettings);
    }
    // Move temp variable to global variables
    memcpy(&gpBoardRuntimeConfig->wifiSettings,
            &tmpSettings.settings.wifi,
            sizeof (WifiSettings));
    memcpy(&gpBoardData->wifiSettings,
            &tmpSettings.settings.wifi,
            sizeof (WifiSettings));

    //    // Load factory calibration parameters - if they are not initialized, 
    //    // store them (first run after a program)
    //    if (!LoadADCCalSettings(                                                 
    //                        DaqifiSettings_FactAInCalParams,                    
    //                        &pBoardRuntimeConfig->AInChannels)) {
    //        SaveADCCalSettings(                                                 
    //                        DaqifiSettings_FactAInCalParams,                    
    //                        &pBoardRuntimeConfig->AInChannels);
    //    }
    //    // If calVals has been set to 1 (user cal params), overwrite with user 
    //    // calibration parameters
    //    if (tmpTopLevelSettings.settings.topLevelSettings.calVals) {
    //        LoadADCCalSettings(                                                 
    //                        DaqifiSettings_UserAInCalParams,                    
    //                        &pBoardRuntimeConfig->AInChannels);
    //    }
    //    // Power initialization - enables 3.3V rail by default - other power 
    //    // functions are in power task
    //    Power_Init(&pBoardConfig->PowerConfig,                     
    //                            &pBoardData->PowerData,                         
    //                            &pBoardRuntimeConfig->PowerWriteVars);
    //
    //    UI_Init(&pBoardConfig->UIConfig,                                       
    //             &pBoardData->UIReadVars,                                       
    //             &pBoardData->PowerData);
    //
    //    // Init DIO Hardware
    //    DIO_InitHardware(pBoardConfig, pBoardRuntimeConfig);
    //
    //    // Write initial values
    //    DIO_WriteStateAll();
    //
    //    Streaming_Init(&pBoardConfig->StreamingConfig,                     
    //                        &pBoardRuntimeConfig->StreamingConfig);
    //    Streaming_UpdateState();
    //
    //    ADC_Init(                                                               
    //                pBoardConfig,                                               
    //                pBoardRuntimeConfig,                                        
    //                pBoardData);
}

void APP_FREERTOS_Initialize(void) {


}

void APP_FREERTOS_Tasks(void) {
    static bool blockAppTask = false;
    BaseType_t errStatus;
    SystemInit();
    if (blockAppTask == false) {
        errStatus = xTaskCreate((TaskFunction_t) USBDeviceTask,
                "USBDeviceTask",
                USBDEVICETASK_SIZE,
                NULL,
                2,
                NULL);
        /*Don't proceed if Task was not created...*/
        if (errStatus != pdTRUE) {
            while (1);
        }
        errStatus = xTaskCreate((TaskFunction_t) WifiTask,
                "WifiTask",
                2048,
                NULL,
                2,
                NULL);
        /*Don't proceed if Task was not created...*/
        if (errStatus != pdTRUE) {
            while (1);
        }
        errStatus = xTaskCreate((TaskFunction_t) SdCardTask,
                "SdCardTask",
                2048,
                NULL,
                2,
                NULL);
        /*Don't proceed if Task was not created...*/
        if (errStatus != pdTRUE) {
            while (1);
        }

        /* The APP_Tasks() function need to exceute only once. Block it now */
        blockAppTask = true;
    }
    vTaskDelay(50);
}


/*******************************************************************************
 End of File
 */



//void wifi_task(void* p_arg) {
//
//    /* Check the application's current state. */
//    typedef enum {
//        /* Application's state machine's initial state. */
//        APP_WIFI_STATE_INIT = 0,
//        APP_WIFI_STATE_WDRV_INIT_READY,
//        APP_WIFI_STATE_START_SCAN,
//        APP_WIFI_STATE_SCANNING,
//        APP_WIFI_STATE_GET_SCAN_RESULT
//    } APP_WIFI_STATES;
//    APP_WIFI_STATES state = APP_WIFI_STATE_INIT;
//    DRV_HANDLE wdrvHandle;
//    WDRV_WINC_STATUS status;
//    static char UsbStr[2000];
//    bool foundBSS = false;
//    while (1) {
//        switch (state) {
//            case APP_WIFI_STATE_INIT:
//            {
//
//                if (SYS_STATUS_READY == WDRV_WINC_Status(sysObj.drvWifiWinc)) {
//                    state = APP_WIFI_STATE_WDRV_INIT_READY;
//                }
//
//                break;
//            }
//
//            case APP_WIFI_STATE_WDRV_INIT_READY:
//            {
//                wdrvHandle = WDRV_WINC_Open(0, 0);
//
//                if (DRV_HANDLE_INVALID != wdrvHandle) {
//                    WDRV_WINC_IPUseDHCPSet(wdrvHandle, &APP_ExampleDHCPAddressEventCallback);
//                    state = APP_WIFI_STATE_START_SCAN;
//                }
//                break;
//            }
//
//            case APP_WIFI_STATE_START_SCAN:
//            {
//
//
//                /* Start a BSS find operation on all channels. */
//
//                if (WDRV_WINC_STATUS_OK == WDRV_WINC_BSSFindFirst(wdrvHandle, WDRV_WINC_ALL_CHANNELS, true, NULL, NULL)) {
//                    state = APP_WIFI_STATE_SCANNING;
//                    foundBSS = false;
//                }
//                break;
//            }
//            case APP_WIFI_STATE_SCANNING:
//            {
//                /* Wait for BSS find operation to complete, then report the number
//                 of results found. */
//
//                if (false == WDRV_WINC_BSSFindInProgress(wdrvHandle)) {
//
//                    memset(UsbStr, 0, sizeof (UsbStr));
//                    int count = WDRV_WINC_BSSFindGetNumBSSResults(wdrvHandle);
//                    sprintf(UsbStr, "\r\nfound %d\r\n", count);
//                    
//                    state = APP_WIFI_STATE_GET_SCAN_RESULT;
//                    
//                }
//                break;
//            }
//            case APP_WIFI_STATE_GET_SCAN_RESULT:
//            {
//                WDRV_WINC_BSS_INFO BSSInfo;
//
//                /* Request the current BSS find results. */
//
//                if (WDRV_WINC_STATUS_OK == WDRV_WINC_BSSFindGetInfo(wdrvHandle, &BSSInfo)) {
//                   
//                    sprintf(UsbStr+strlen(UsbStr), "\r\nAP found: RSSI: %d %s\r\n", BSSInfo.rssi, BSSInfo.ctx.ssid.name);
//                   
//
//                    /* Check if this SSID matches the search target SSID. */
//
//                    if (((sizeof (WLAN_SSID) - 1) == BSSInfo.ctx.ssid.length) && (0 == memcmp(BSSInfo.ctx.ssid.name, WLAN_SSID, BSSInfo.ctx.ssid.length))) {
//                        foundBSS = true;
//                    }
//
//                    /* Request the next set of BSS find results. */
//
//                    status = WDRV_WINC_BSSFindNext(wdrvHandle, NULL);
//
//                    if (WDRV_WINC_STATUS_BSS_FIND_END == status) {
//                        /* If there are no more results available check if the target
//                         SSID has been found. */
//                       
//                        sprintf(UsbStr+strlen(UsbStr), "AP found: RSSI: %d %s\r\n", BSSInfo.rssi, BSSInfo.ctx.ssid.name);
//                        if (true == foundBSS) {
//                            sprintf(UsbStr+strlen(UsbStr), "Target AP found, trying to connect\r\n");
//                            state = APP_WIFI_STATE_START_SCAN;
//                        } else {
//                            sprintf(UsbStr+strlen(UsbStr), "Target BSS not found\r\n\r\n");
//                            state = APP_WIFI_STATE_START_SCAN;
//                        }
//                        USB_DEVICE_CDC_Write
//                                (
//                                USB_DEVICE_CDC_INDEX_0,
//                                &COM1Write_Handle,
//                                UsbStr,
//                                strlen(UsbStr),
//                                USB_DEVICE_CDC_TRANSFER_FLAGS_DATA_COMPLETE
//                                );
//                    } else if ((WDRV_WINC_STATUS_NOT_OPEN == status) || (WDRV_WINC_STATUS_INVALID_ARG == status)) {
//                        /* An error occurred requesting results. */
//
//                        state = APP_WIFI_STATE_START_SCAN;
//                    }
//                }
//                break;
//            }
//            default:
//            {
//                /* TODO: Handle error in application's state machine. */
//                break;
//            }
//        }
//        vTaskDelay(5);
//    }
//}


//void SD_Task(void* p_arg) {
//    /* Check the application's current state. */
//#define SDCARD_MOUNT_NAME    "/mnt/mydrive"
//#define SDCARD_DEV_NAME      "/dev/mmcblka1"
//#define SDCARD_FILE_NAME     "FILE_TOO_LONG_NAME_EXAMPLE_123.JPG"
//#define SDCARD_DIR_NAME      "Dir3"
//#define SD_DATA_LEN         512
//
//    enum {
//        SD_MOUNT_DISK,
//        SD_UNMOUNT_DISK,
//        SD_MOUNT_DISK_AGAIN,
//        SD_SET_CURRENT_DRIVE,
//        SD_OPEN_FIRST_FILE,
//        SD_CREATE_DIRECTORY,
//        SD_OPEN_SECOND_FILE,
//        SD_READ_WRITE_TO_FILE,
//        SD_CLOSE_FILE,
//        SD_IDLE,
//        SD_ERROR,
//
//    };
//    uint8_t state = SD_MOUNT_DISK;
//    SYS_FS_HANDLE fileHandle = SYS_FS_HANDLE_INVALID;
//    SYS_FS_HANDLE fileHandle1 = SYS_FS_HANDLE_INVALID;
//    int32_t nBytesRead;
//    uint8_t readWriteBuffer[SD_DATA_LEN];
//    while (1) {
//        switch (state) {
//            case SD_MOUNT_DISK:
//                if (SYS_FS_Mount(SDCARD_DEV_NAME, SDCARD_MOUNT_NAME, FAT, 0, NULL) != 0) {
//                    /* The disk could not be mounted. Try
//                     * mounting again until success. */
//                    state = SD_MOUNT_DISK;
//                } else {
//                    /* Mount was successful. Unmount the disk, for testing. */
//                    state = SD_UNMOUNT_DISK;
//                }
//                break;
//
//            case SD_UNMOUNT_DISK:
//                if (SYS_FS_Unmount(SDCARD_MOUNT_NAME) != 0) {
//                    /* The disk could not be un mounted. Try
//                     * un mounting again untill success. */
//
//                    state = SD_UNMOUNT_DISK;
//                } else {
//                    /* UnMount was successful. Mount the disk again */
//                    state = SD_MOUNT_DISK_AGAIN;
//                }
//                break;
//
//            case SD_MOUNT_DISK_AGAIN:
//                if (SYS_FS_Mount(SDCARD_DEV_NAME, SDCARD_MOUNT_NAME, FAT, 0, NULL) != 0) {
//                    /* The disk could not be mounted. Try
//                     * mounting again until success. */
//                    state = SD_MOUNT_DISK_AGAIN;
//                } else {
//                    /* Mount was successful. Set current drive so that we do not have to use absolute path. */
//                    state = SD_SET_CURRENT_DRIVE;
//                }
//                break;
//
//            case SD_SET_CURRENT_DRIVE:
//                if (SYS_FS_CurrentDriveSet(SDCARD_MOUNT_NAME) == SYS_FS_RES_FAILURE) {
//                    /* Error while setting current drive */
//                    state = SD_ERROR;
//                } else {
//                    /* Open a file for reading. */
//                    state = SD_OPEN_FIRST_FILE;
//                }
//                break;
//
//            case SD_OPEN_FIRST_FILE:
//                fileHandle = SYS_FS_FileOpen(SDCARD_FILE_NAME, (SYS_FS_FILE_OPEN_READ));
//                if (fileHandle == SYS_FS_HANDLE_INVALID) {
//                    /* Could not open the file. Error out*/
//                    state = SD_ERROR;
//                } else {
//                    /* Create a directory. */
//                    state = SD_CREATE_DIRECTORY;
//                }
//                break;
//
//            case SD_CREATE_DIRECTORY:
//                if (SYS_FS_DirectoryMake(SDCARD_DIR_NAME) == SYS_FS_RES_FAILURE) {
//                    /* Error while creating a new drive */
//                    state = SD_MOUNT_DISK_AGAIN;
//                } else {
//                    /* Open a second file for writing. */
//                    state = SD_OPEN_SECOND_FILE;
//                }
//                break;
//
//            case SD_OPEN_SECOND_FILE:
//                /* Open a second file inside "Dir1" */
//                fileHandle1 = SYS_FS_FileOpen(SDCARD_DIR_NAME"/"SDCARD_FILE_NAME,
//                        (SYS_FS_FILE_OPEN_WRITE));
//
//                if (fileHandle1 == SYS_FS_HANDLE_INVALID) {
//                    /* Could not open the file. Error out*/
//                    state = SD_ERROR;
//                } else {
//                    /* Read from one file and write to another file */
//                    state = SD_READ_WRITE_TO_FILE;
//                }
//                break;
//            case SD_READ_WRITE_TO_FILE:
//
//                nBytesRead = SYS_FS_FileRead(fileHandle, (void *) readWriteBuffer, SD_DATA_LEN);
//
//                if (nBytesRead == -1) {
//                    /* There was an error while reading the file.
//                     * Close the file and error out. */
//                    SYS_FS_FileClose(fileHandle);
//                    state = SD_ERROR;
//                } else {
//                    /* If read was success, try writing to the new file */
//                    if (SYS_FS_FileWrite(fileHandle1, (const void *) readWriteBuffer, nBytesRead) == -1) {
//                        /* Write was not successful. Close the file
//                         * and error out.*/
//                        SYS_FS_FileClose(fileHandle1);
//                        state = SD_ERROR;
//                    } else if (SYS_FS_FileEOF(fileHandle) == 1) /* Test for end of file */ {
//                        /* Continue the read and write process, until the end of file is reached */
//                        state = SD_CLOSE_FILE;
//                    }
//                }
//                break;
//
//            case SD_CLOSE_FILE:
//                /* Close both files */
//                SYS_FS_FileClose(fileHandle);
//                SYS_FS_FileClose(fileHandle1);
//
//                /* The test was successful. Lets idle. */
//                state = SD_IDLE;
//                break;
//
//            case SD_IDLE:
//                /* The application comes here when the demo has completed
//                 * successfully. Glow LED1. */
//                //LED_ON();
//                break;
//
//            case SD_ERROR:
//                /* The application comes here when the demo has failed. */
//                break;
//
//            default:
//                break;
//        }
//        vTaskDelay(5);
//    }
//}