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
#include "HAL/DIO.h"
#include "services/streaming.h"
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
static tBoardConfig * gpBoardConfig;
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
            pBuff, *len);
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
    int count = 0;
    WifiApi_Init(&gpBoardData->wifiSettings);
    char buff[100];
    while (1) {
        WifiApi_ProcessState();
        memset(buff, 0, 100);
        sprintf(buff, "\r\nCount%d", count++);
        SDCard_WriteToBuffer(buff, strlen(buff));
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

    gpBoardConfig = BoardConfig_Get(
            BOARDCONFIG_ALL_CONFIG,
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
    InitBoardConfig(&tmpTopLevelSettings.settings.topLevelSettings);
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
    // Init DIO Hardware
    DIO_InitHardware(gpBoardConfig, gpBoardRuntimeConfig);

    // Write initial values
    DIO_WriteStateAll();
    DIO_TIMING_TEST_INIT();
    Streaming_Init(&gpBoardConfig->StreamingConfig,
            &gpBoardRuntimeConfig->StreamingConfig);
    //    Streaming_UpdateState();
    //
    //    ADC_Init(                                                               
    //                pBoardConfig,                                               
    //                pBoardRuntimeConfig,                                        
    //                pBoardData);
}

void APP_FREERTOS_Initialize(void) {
    
}
static void TasksCreate(){
    static bool blockAppTask = false;
    BaseType_t errStatus;

    if (blockAppTask == false) {
        SystemInit();
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
}
void APP_FREERTOS_Tasks(void) {
    TasksCreate();
    Streaming_Tasks(gpBoardRuntimeConfig,gpBoardData);    
    vTaskDelay(1/portTICK_PERIOD_MS);
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

