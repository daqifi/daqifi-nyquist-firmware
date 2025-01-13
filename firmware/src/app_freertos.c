#include "app_freertos.h"
#include "wdrv_winc_client_api.h"
#include "queue.h"
#include <inttypes.h>
#include <services/UsbCdc/UsbCdc.h>
#include "services/DaqifiPB/DaqifiOutMessage.pb.h"
#include "services/DaqifiPB/NanoPB_Encoder.h"
#include "services/wifi_services/wifi_manager.h"
#include "services/sd_card_services/sd_card_manager.h"
#include "HAL/DIO.h"
#include "HAL/ADC.h"
#include "services/streaming.h"
#include "HAL/UI/UI.h"
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


static void app_SystemInit();
static void app_USBDeviceTask(void* p_arg);
static void app_WifiTask(void* p_arg);
static void app_SdCardTask(void* p_arg);

void wifi_manager_FormUdpAnnouncePacketCB(const wifi_manager_settings_t *pWifiSettings, uint8_t *pBuffer, uint16_t *pPacketLen) {
    tBoardData * pBoardData = (tBoardData *) BoardData_Get(
            BOARDDATA_ALL_DATA,
            0);
    pBoardData->wifiSettings.ipAddr.Val = pWifiSettings->ipAddr.Val;
    memcpy(pBoardData->wifiSettings.macAddr.addr, pWifiSettings->macAddr.addr, WDRV_WINC_MAC_ADDR_LEN);
    size_t count = Nanopb_Encode(
            pBoardData,
            &fields_discovery,
            pBuffer, *pPacketLen);
    *pPacketLen = count;
}

void sd_card_manager_DataReadyCB(sd_card_manager_mode_t mode, uint8_t *pDataBuff, size_t dataLen) {
    size_t transferredLength = 0;
    int retryCount = 0;
    const int maxRetries = 100;

    while (transferredLength < dataLen) {
        size_t bytesWritten = UsbCdc_WriteToBuffer(
                NULL,
                (const char *) pDataBuff + transferredLength,
                dataLen - transferredLength
                );

        if (bytesWritten > 0) {
            transferredLength += bytesWritten;
            retryCount = 0;
        } else {
            retryCount++;
            if (retryCount >= maxRetries) {
                break;
            }
            vTaskDelay(5 / portTICK_PERIOD_MS);
        }
    }
}

static void app_USBDeviceTask(void* p_arg) {
    UsbCdc_Initialize();
    UsbCdcData_t* pUsbCdcContext = UsbCdc_GetSettings();
    while (1) {
        UsbCdc_ProcessState();
        if (pUsbCdcContext->isTransparentModeActive) {
            taskYIELD();
        } else {
            vTaskDelay(5 / portTICK_PERIOD_MS);
        }
    }
}

static void app_WifiTask(void* p_arg) {
    enum{
        APP_WIFI_STATE_WAIT_POWER_UP=0,
        APP_WIFI_STATE_PROCESS=1,
    };
    const tPowerData *pPowerState=BoardData_Get(BOARDATA_POWER_DATA,0);
    uint8_t state = APP_WIFI_STATE_WAIT_POWER_UP;
    while (1) {     
        switch (state) {
            case APP_WIFI_STATE_WAIT_POWER_UP:
            {
                if (NULL != pPowerState &&
                        pPowerState->powerState != POWERED_UP &&
                        pPowerState->powerState != POWERED_UP_EXT_DOWN) {
                    state = APP_WIFI_STATE_WAIT_POWER_UP;
                } else {
                    wifi_manager_Init(&gpBoardData->wifiSettings);
                    state = APP_WIFI_STATE_PROCESS;
                }
            }
                break;
            case APP_WIFI_STATE_PROCESS:
            {
                wifi_manager_ProcessState();
                if (NULL != pPowerState &&
                        pPowerState->powerState != POWERED_UP &&
                        pPowerState->powerState != POWERED_UP_EXT_DOWN) {
                    wifi_manager_Deinit();
                    state = APP_WIFI_STATE_WAIT_POWER_UP;
                }
            }
                break;
        }
        vTaskDelay(5 / portTICK_PERIOD_MS);  
    }
}

static void app_SdCardTask(void* p_arg) {
    sd_card_manager_Init(&gpBoardRuntimeConfig->sdCardConfig);
    while (1) {
        sd_card_manager_ProcessState();
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
}

void app_PowerAndUITask(void) {
    StreamingRuntimeConfig * pRunTimeStreamConf = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);

    portTASK_USES_FLOATING_POINT();
    while (1) {
        Button_Tasks();
        LED_Tasks(pRunTimeStreamConf->IsEnabled);
        Power_Tasks();
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

void app_SystemInit() {
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
    memset(&tmpSettings, 0, sizeof (tmpSettings));


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


    tmpSettings.type = DaqifiSettings_Wifi;
    if (!daqifi_settings_LoadFromNvm(DaqifiSettings_Wifi, &tmpSettings)) {
        // Get board wifi settings from Wifi NVM variable
        daqifi_settings_LoadFactoryDeafult(DaqifiSettings_Wifi, &tmpSettings);
        daqifi_settings_SaveToNvm(&tmpSettings);
        memset(&tmpSettings, 0, sizeof (tmpSettings));
        daqifi_settings_LoadFromNvm(DaqifiSettings_Wifi, &tmpSettings);
    }
    // Move temp variable to global variables
    tmpSettings.settings.wifi.isOtaModeEnabled = false;
    memcpy(&gpBoardRuntimeConfig->wifiSettings,
            &tmpSettings.settings.wifi,
            sizeof (wifi_manager_settings_t));
    memcpy(&gpBoardData->wifiSettings,
            &tmpSettings.settings.wifi,
            sizeof (wifi_manager_settings_t));

    // Load factory calibration parameters - if they are not initialized, 
    // store them (first run after a program)
    if (!daqifi_settings_LoadADCCalSettings(
            DaqifiSettings_FactAInCalParams,
            &gpBoardRuntimeConfig->AInChannels)) {
        daqifi_settings_SaveADCCalSettings(
                DaqifiSettings_FactAInCalParams,
                &gpBoardRuntimeConfig->AInChannels);
    }
    // If calVals has been set to 1 (user cal params), overwrite with user 
    // calibration parameters
    if (tmpTopLevelSettings.settings.topLevelSettings.calVals) {
        daqifi_settings_LoadADCCalSettings(
                DaqifiSettings_UserAInCalParams,
                &gpBoardRuntimeConfig->AInChannels);
    }
    // Power initialization - enables 3.3V rail by default - other power 
    // functions are in power task
    Power_Init(&gpBoardConfig->PowerConfig,
            &gpBoardData->PowerData,
            &gpBoardRuntimeConfig->PowerWriteVars);

    UI_Init(&gpBoardConfig->UIConfig,
            &gpBoardData->UIReadVars,
            &gpBoardData->PowerData);

    // Init DIO Hardware
    DIO_InitHardware(gpBoardConfig, gpBoardRuntimeConfig);

    // Write initial values
    DIO_WriteStateAll();
    DIO_TIMING_TEST_INIT();
    Streaming_Init(&gpBoardConfig->StreamingConfig,
            &gpBoardRuntimeConfig->StreamingConfig);
    Streaming_UpdateState();

    ADC_Init(
            gpBoardConfig,
            gpBoardRuntimeConfig,
            gpBoardData);
}

static void app_TasksCreate() {
    BaseType_t errStatus;
    errStatus = xTaskCreate((TaskFunction_t) app_PowerAndUITask,
            "PowerAndUITask",
            2048,
            NULL,
            2,
            NULL);
    /*Don't proceed if Task was not created...*/
    if (errStatus != pdTRUE) {
        while (1);
    }

    errStatus = xTaskCreate((TaskFunction_t) app_USBDeviceTask,
            "USBDeviceTask",
            USBDEVICETASK_SIZE,
            NULL,
            2,
            NULL);
    /*Don't proceed if Task was not created...*/
    if (errStatus != pdTRUE) {
        while (1);
    }
    errStatus = xTaskCreate((TaskFunction_t) app_WifiTask,
            "WifiTask",
            3000,
            NULL,
            2,
            NULL);
    /*Don't proceed if Task was not created...*/
    if (errStatus != pdTRUE) {
        while (1);
    }
    errStatus = xTaskCreate((TaskFunction_t) app_SdCardTask,
            "SdCardTask",
            5000,
            NULL,
            2,
            NULL);
    /*Don't proceed if Task was not created...*/
    if (errStatus != pdTRUE) {
        while (1);
    }
}

void APP_FREERTOS_Initialize(void) {
    /*
     * This cannot be used for initialization 
     * because the NVIC is initialized after this
     * function call
     */
}

void APP_FREERTOS_Tasks(void) {
    app_SystemInit();
    app_TasksCreate();
    while (true) {
        ADC_Tasks();
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}

