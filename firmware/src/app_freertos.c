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
#include "HAL/Wifi/WifiApi.h"
// *****************************************************************************
// *****************************************************************************
// Section: Global Data Definitions
// *****************************************************************************
// *****************************************************************************

uint8_t __attribute__((aligned(16))) switchPromptUSB[] = "Hello World\r\n";

char CACHE_ALIGN cdcReadBuffer[APP_READ_BUFFER_SIZE];
char CACHE_ALIGN cdcWriteBuffer[APP_READ_BUFFER_SIZE];

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
QueueHandle_t USBDeviceTask_EventQueue_Handle;

APP_DATA appData;



void USBDevice_Task(void* p_arg);

void USBDevice_Task(void* p_arg) {
    UsbCdc_Initialize();
    while (1) {
        UsbCdc_ProcessState();
        vTaskDelay(5);
    }
}
void WifiApi_FormUdpAnnouncePacketCallback(WifiSettings settings, uint8_t* pBuff, uint16_t *len){
}
void wifi_task(void* p_arg){   
    DaqifiSettings LoadSettings={0};
    
    //daqifi_settings_SaveToNvm(&saveSettings);
    daqifi_settings_LoadFromNvm(DaqifiSettings_Wifi, &LoadSettings);
//    uint8_t sampleStr[NVM_FLASH_ROWSIZE+1]="This is a test string i am trying to write into nvm";
//    nvm_WriteRowtoAddr(WIFI_SETTINGS_ADDR,sampleStr);
    WifiApi_Init(&LoadSettings.settings.wifi);
    while (1) {
        WifiApi_Dispatcher();
        vTaskDelay(5);
    }
}

// *****************************************************************************
// *****************************************************************************
// Section: Application Initialization and State Machine Functions
// *****************************************************************************
// *****************************************************************************

/*******************************************************************************
  Function:
    void APP_Initialize ( void )

  Remarks:
    See prototype in app.h.
 */

void APP_FREERTOS_Initialize(void) {


}

/******************************************************************************
  Function:
    void APP_Tasks ( void )

  Remarks:
    See prototype in app.h.
 */
void APP_FREERTOS_Tasks(void) {
    static bool blockAppTask = false;
    BaseType_t errStatus;
    if (blockAppTask == false) {
        errStatus = xTaskCreate((TaskFunction_t) USBDevice_Task,
                "USB_AttachTask",
                USBDEVICETASK_SIZE,
                NULL,
                2,
                NULL);
        /*Don't proceed if Task was not created...*/
        if (errStatus != pdTRUE) {
            while (1);
        }
        //        errStatus = xTaskCreate((TaskFunction_t) SD_Task,
        //                "SD_AttachTask",
        //                USBDEVICETASK_SIZE,
        //                NULL,
        //                USBDEVICETASK_PRIO,
        //                NULL);
        //        /*Don't proceed if Task was not created...*/
        //        if (errStatus != pdTRUE) {
        //            while (1);
        //        }

        errStatus = xTaskCreate((TaskFunction_t) wifi_task,
                "wifi_task",
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

//typedef enum {
//    /* Application's state machine's initial state. */
//    APP_WIFI_STATE_INIT = 0,
//    APP_WIFI_STATE_INIT2,
//
//    APP_WIFI_STATE_SOCKET_LISTENING,
//    APP_WIFI_STATE_AP_STARTED,
//    APP_WIFI_STATE_ERROR,
//} APP_WIFI_STATES;
//APP_WIFI_STATES state = APP_WIFI_STATE_INIT;
//#define UDP_LISTEN_PORT         30303
//#define UDP_BUFFER_SIZE         1460
//static WDRV_WINC_BSS_CONTEXT bssCtx;
//static WDRV_WINC_AUTH_CONTEXT authCtx;
//DRV_HANDLE wdrvHandle;
//#define WLAN_DHCP_SRV_ADDR      "192.168.1.1"
//#define WLAN_DHCP_SRV_NETMASK   "255.255.255.0"
//#define WLAN_SSID           "Daqifi"
//#define WLAN_AUTH_WPA_PSK    M2M_WIFI_SEC_WPA_PSK
//#define WLAN_PSK            "12345678"
//static SOCKET serverSocket = -1;
////static SOCKET udp_client_socket = -1;
//static uint8_t recvBuffer[UDP_BUFFER_SIZE];
//static uint8_t sendBuffer[UDP_BUFFER_SIZE];
//
//extern const NanopbFlagsArray fields_discovery;
////extern size_t Nanopb_Encode(tBoardData* state, const NanopbFlagsArray* fields, uint8_t* buffer, size_t bufferLen);
//
//size_t DAQiFi_TCPIP_ANNOUNCE_Create(uint8_t *buffer, size_t bufferLen) {
//    tBoardData * pBoardData = (tBoardData *)BoardData_Get(                                
//                            BOARDDATA_ALL_DATA,                             
//                            0);
//    size_t count = Nanopb_Encode(                                           
//                        pBoardData,                                         
//                        &fields_discovery,                                  
//                        buffer);
//    return (count);
//}
//
//
//static void APP_ExampleDHCPAddressEventCallback(DRV_HANDLE handle, uint32_t ipAddress) {
//    char s[20];
//    inet_ntop(AF_INET, &ipAddress, s, sizeof (s));
//    uint16_t len = sprintf(cdcWriteBuffer, "DHP Address : %s\r\n", s);
//    UsbCdc_WriteToBuffer(NULL, cdcWriteBuffer, len);
//
//}
//
//static void APP_ExampleSocketEventCallback(SOCKET socket, uint8_t messageType, void *pMessage) {
//    switch (messageType) {
//        case SOCKET_MSG_BIND:
//        {
//            tstrSocketBindMsg *pBindMessage = (tstrSocketBindMsg*) pMessage;
//
//            if ((NULL != pBindMessage) && (0 == pBindMessage->status)) {
//                //SYS_CONSOLE_Print(appData.consoleHandle, "Bind on socket %d successful, server_socket = %d\r\n", socket, serverSocket);
//                //listen(serverSocket, 0);
//                recvfrom(serverSocket, recvBuffer, UDP_BUFFER_SIZE, 0);
//            } else {
//                //SYS_CONSOLE_Print(appData.consoleHandle, "Bind on socket %d failed\r\n", socket);
//
//                shutdown(serverSocket);
//                serverSocket = -1;
//                state = APP_WIFI_STATE_ERROR;
//            }
//            break;
//        }
//        case SOCKET_MSG_RECVFROM:
//        {
//            tstrSocketRecvMsg *pstrRx = (tstrSocketRecvMsg*) pMessage;
//
//            if (pstrRx->s16BufferSize > 0) {
//                //get the remote host address and port number
//                uint16_t u16port = pstrRx->strRemoteAddr.sin_port;
//                uint32_t strRemoteHostAddr = pstrRx->strRemoteAddr.sin_addr.s_addr;
//                char s[20];
//
//                inet_ntop(AF_INET, &strRemoteHostAddr, s, sizeof (s));
//                uint16_t len = sprintf(cdcWriteBuffer, "\r\nReceived frame with size=%d\r\nHost address=%s\r\nPort number = %d\r\n", pstrRx->s16BufferSize, s, u16port);
//                UsbCdc_WriteToBuffer(NULL, cdcWriteBuffer, len);
//                len = sprintf(cdcWriteBuffer, "Frame Data : %.*s\r\n", pstrRx->s16BufferSize, (char*) pstrRx->pu8Buffer);
//                UsbCdc_WriteToBuffer(NULL, cdcWriteBuffer, len);
//                recvfrom(serverSocket, recvBuffer, UDP_BUFFER_SIZE, 0);
//                //len = sprintf((char*) sendBuffer, "echo=>\"%.*s\"", pstrRx->s16BufferSize, (char*) pstrRx->pu8Buffer);
//                struct sockaddr_in addr;
//                addr.sin_family = AF_INET;
//                addr.sin_port = pstrRx->strRemoteAddr.sin_port;
//                addr.sin_addr.s_addr = pstrRx->strRemoteAddr.sin_addr.s_addr;
//
//                DaqifiSettings * pWifiSettings = (DaqifiSettings * )BoardData_Get(                         
//                        BOARDDATA_WIFI_SETTINGS,                            
//                        0);
//                // Store IPv4 Address
//                pWifiSettings->settings.wifi.ipAddr.Val=inet_addr(WLAN_DHCP_SRV_ADDR);
//                // Store subnet mask 
//                pWifiSettings->settings.wifi.ipMask.Val=inet_addr(WLAN_DHCP_SRV_NETMASK);
//                // Store default gateway        
//                pWifiSettings->settings.wifi.gateway.Val=inet_addr(WLAN_DHCP_SRV_ADDR);
//                
//                WDRV_WINC_EthernetAddressGet(wdrvHandle,pWifiSettings->settings.wifi.macAddr.addr);
//                len=DAQiFi_TCPIP_ANNOUNCE_Create(sendBuffer, sizeof(sendBuffer));
//                sendto(serverSocket, sendBuffer, len, 0, (struct sockaddr*) &addr, sizeof (struct sockaddr_in));
//            } else {
//                //printf("Socket recv Error: %d\n",pstrRx->s16BufferSize);
//                shutdown(serverSocket);
//            }
//        }
//            break;
//
//        case SOCKET_MSG_SENDTO:
//        {
//            uint16_t len = sprintf(cdcWriteBuffer, "UDP Send Success\r\n");
//            UsbCdc_WriteToBuffer(NULL, cdcWriteBuffer, len);
//            break;
//        }
//
//        default:
//        {
//            break;
//        }
//    }
//}
//
//static void APP_ExampleAPConnectNotifyCallback(DRV_HANDLE handle, WDRV_WINC_ASSOC_HANDLE assocHandle, WDRV_WINC_CONN_STATE currentState, WDRV_WINC_CONN_ERROR errorCode) {
//    if (WDRV_WINC_CONN_STATE_CONNECTED == currentState) {
//        if (-1 == serverSocket) {
//            serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
//
//            if (serverSocket >= 0) {
//                struct sockaddr_in addr;
//
//                /* Listen on the socket. */
//
//                addr.sin_family = AF_INET;
//                addr.sin_port = _htons(UDP_LISTEN_PORT);
//                addr.sin_addr.s_addr = 0;
//
//                bind(serverSocket, (struct sockaddr*) &addr, sizeof (struct sockaddr_in));
//
//                state = APP_WIFI_STATE_SOCKET_LISTENING;
//            }
//        }
//    } else if (WDRV_WINC_CONN_STATE_DISCONNECTED == currentState) {
//        //SYS_CONSOLE_Print(appData.consoleHandle, "AP Mode: Station disconnected\r\n");
//
//        if (-1 != serverSocket) {
//            shutdown(serverSocket);
//            serverSocket = -1;
//        }
//    }
//}

//void wifi_task(void* p_arg) {
//
//    while (1) {
//        switch (state) {
//            case APP_WIFI_STATE_INIT:
//            {
//                if (SYS_STATUS_READY == WDRV_WINC_Status(sysObj.drvWifiWinc)) {
//                    wdrvHandle = WDRV_WINC_Open(0, 0);
//                    state = APP_WIFI_STATE_INIT2;
//                }
//                break;
//            }
//            case APP_WIFI_STATE_INIT2:
//            {
//                state = APP_WIFI_STATE_ERROR;
//
//                /* Create the BSS context using default values and then set SSID
//                 and channel. */
//
//
//                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetDefaults(&bssCtx)) {
//                    break;
//                }
//
//                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetSSID(&bssCtx, (uint8_t*) WLAN_SSID, strlen(WLAN_SSID))) {
//                    break;
//                }
//
//                if (WDRV_WINC_STATUS_OK != WDRV_WINC_BSSCtxSetChannel(&bssCtx, 1)) {
//                    break;
//                }
//
//
//
//
//                if (WDRV_WINC_STATUS_OK != WDRV_WINC_IPDHCPServerConfigure(wdrvHandle, inet_addr(WLAN_DHCP_SRV_ADDR), inet_addr(WLAN_DHCP_SRV_NETMASK), &APP_ExampleDHCPAddressEventCallback)) {
//                    break;
//                }
//
//                /* Register callback for socket events. */
//
//                WDRV_WINC_SocketRegisterEventCallback(wdrvHandle, &APP_ExampleSocketEventCallback);
//
//                /* Create the AP using the BSS and authentication context. */
//                WDRV_WINC_AuthCtxSetWPA(&authCtx, (uint8_t*) WLAN_PSK, strlen(WLAN_PSK));
//                if (WDRV_WINC_STATUS_OK == WDRV_WINC_APStart(wdrvHandle, &bssCtx, &authCtx, NULL, &APP_ExampleAPConnectNotifyCallback)) {
//                    //UsbCdc_WriteToBuffer(NULL,"",   
//                    state = APP_WIFI_STATE_AP_STARTED;
//                }
//            }
//                break;
//            case APP_WIFI_STATE_AP_STARTED:
//            {
//                /* Create the server socket. */
//
//                serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
//
//                if (serverSocket >= 0) {
//                    struct sockaddr_in addr;
//
//                    /* Listen on the socket. */
//
//                    addr.sin_family = AF_INET;
//                    addr.sin_port = _htons(UDP_LISTEN_PORT);
//                    addr.sin_addr.s_addr = 0;
//
//                    bind(serverSocket, (struct sockaddr*) &addr, sizeof (struct sockaddr_in));
//
//                    state = APP_WIFI_STATE_SOCKET_LISTENING;
//                }
//                break;
//            }
//            case APP_WIFI_STATE_SOCKET_LISTENING:
//            {
//                break;
//            }
//            case APP_WIFI_STATE_ERROR:
//            {
//                break;
//            }
//            default:
//            {
//                break;
//            }
//        }
//        vTaskDelay(5);
//    }
//}

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