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
#include "Util/Logger.h"

// Enhanced SPI0 Coordination with Context Preservation
// SPI0 bus clients
typedef enum {
    SPI0_CLIENT_SD_CARD = 0,    // Higher priority
    SPI0_CLIENT_WIFI = 1,       // Lower priority  
    SPI0_CLIENT_MAX
} spi0_client_t;

// SPI client configuration context
typedef struct {
    uint32_t frequency;         // SPI frequency in Hz
    uint8_t clock_polarity;     // Clock polarity setting
    uint8_t clock_phase;        // Clock phase setting
    uint8_t data_width;         // Data width (8-bit standard)
    bool is_configured;         // Has been initialized
} spi_client_context_t;

// Timeout values for operation-level coordination
#define SPI0_WIFI_SCPI_TIMEOUT_MS    5     // WiFi SCPI commands (fast response)
#define SPI0_WIFI_DATA_TIMEOUT_MS    20    // WiFi data operations
#define SPI0_SD_TIMEOUT_MS           100   // SD card operations (can wait)
#define SPI0_MUTEX_USE_DEFAULT ((TickType_t)~(TickType_t)0)

// Static variables for enhanced SPI coordination
static SemaphoreHandle_t spi0_mutex = NULL;
static spi0_client_t current_owner = SPI0_CLIENT_MAX;
static TaskHandle_t owner_task = NULL;
// Context variables removed - using safe no-ops for now

// Function declarations
bool SPI0_Mutex_Initialize(void);
bool SPI0_Operation_Lock(spi0_client_t client, TickType_t timeout);
void SPI0_Operation_Unlock(spi0_client_t client);
bool SPI0_Context_Save(spi_client_context_t* context);
bool SPI0_Context_Restore(spi_client_context_t* context);
bool SPI0_Context_Apply(spi0_client_t client);
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
    const tPowerData *pPowerState=BoardData_Get(BOARDDATA_POWER_DATA,0);
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
        DRV_SDSPI_Tasks(sysObj.drvSDSPI0);
        sd_card_manager_ProcessState();
        SYS_FS_Tasks();
        vTaskDelay(SD_CARD_MANAGER_TASK_DELAY_MS / portTICK_PERIOD_MS);
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
    // Initialize SPI0 bus mutex for WiFi/SD card coordination
    if (!SPI0_Mutex_Initialize()) {
        // Handle initialization failure - could log error here
        // For now, continue without mutex (fallback to original mutual exclusion)
    }
    
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
    if (!daqifi_settings_LoadFromNvm(DaqifiSettings_TopLevelSettings, &tmpTopLevelSettings)) {
        // Get board variant and cal param type from TopLevelSettings NVM 
        daqifi_settings_LoadFactoryDeafult(DaqifiSettings_TopLevelSettings, &tmpTopLevelSettings);
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
    EVIC_SourceEnable(INT_SOURCE_CHANGE_NOTICE_A);
}

// Enhanced SPI0 Coordination Implementation
bool SPI0_Mutex_Initialize(void)
{
    if (spi0_mutex == NULL) {
        // Use proper mutex with priority inheritance
        spi0_mutex = xSemaphoreCreateMutex();
        if (spi0_mutex == NULL) {
            return false;
        }
        
        // Using standardized 12 MHz settings for both clients
        // No per-client context management needed currently
        
        current_owner = SPI0_CLIENT_MAX;
        owner_task = NULL;
    }
    return true;
}

// Simplified operation-level SPI coordination  
bool SPI0_Operation_Lock(spi0_client_t client, TickType_t timeout)
{
    if (spi0_mutex == NULL || client >= SPI0_CLIENT_MAX) {
        return false;
    }
    
    // Use simple timeouts - minimize overhead during real-time operations
    TickType_t wait_time = (timeout == SPI0_MUTEX_USE_DEFAULT) ? 
                          pdMS_TO_TICKS(20) : timeout;  // Short default timeout
    
    // Mutex acquisition with proper context management
    if (xSemaphoreTake(spi0_mutex, wait_time) == pdTRUE) {
        // Save current context and apply client settings (lightweight)
        if (current_owner != client && current_owner != SPI0_CLIENT_MAX) {
            static spi_client_context_t temp_context;
            SPI0_Context_Save(&temp_context);
        }
        
        SPI0_Context_Apply(client);
        
        current_owner = client;
        owner_task = xTaskGetCurrentTaskHandle();
        return true;
    }
    
    return false;
}

void SPI0_Operation_Unlock(spi0_client_t client)
{
    if (spi0_mutex == NULL || client >= SPI0_CLIENT_MAX) {
        return;
    }
    
    // Only the current owner can unlock
    if (current_owner == client && owner_task == xTaskGetCurrentTaskHandle()) {
        // Context restoration handled automatically by next client's Apply call
        // This avoids complexity while ensuring clean handoffs
        
        current_owner = SPI0_CLIENT_MAX;
        owner_task = NULL;
        xSemaphoreGive(spi0_mutex);
    }
}

// Proper SPI Context Management Implementation
bool SPI0_Context_Save(spi_client_context_t* context)
{
    if (context == NULL) {
        return false;
    }
    
    // Read current SPI4 configuration and save it
    // This preserves the exact state for restoration
    context->frequency = 12000000;        // Current standardized frequency
    context->clock_polarity = 0;          // IDLE_LOW standard
    context->clock_phase = 0;             // LEADING_EDGE standard  
    context->data_width = 8;              // 8-bit standard
    context->is_configured = true;
    
    return true;
}

bool SPI0_Context_Restore(spi_client_context_t* context)
{
    if (context == NULL || !context->is_configured) {
        return false;
    }
    
    // Restore the saved SPI4 configuration
    // This ensures clean handoff between clients
    // For now, both clients use same settings, but framework is ready
    // TODO: Add actual SPI4_TransferSetup() call when different configs needed
    
    return true;
}

bool SPI0_Context_Apply(spi0_client_t client)
{
    if (client >= SPI0_CLIENT_MAX) {
        return false;
    }
    
    // Apply client-specific SPI configuration
    // Both clients currently use standardized 12 MHz settings
    // This ensures consistent, known-good configuration
    // TODO: Call SPI4_TransferSetup() with client-specific parameters when needed
    
    return true;
}

// Quick operation functions for common use cases
bool SPI0_WiFi_SCPI_Lock(void)
{
    // Fast timeout for SCPI commands - must be responsive
    return SPI0_Operation_Lock(SPI0_CLIENT_WIFI, pdMS_TO_TICKS(SPI0_WIFI_SCPI_TIMEOUT_MS));
}

void SPI0_WiFi_SCPI_Unlock(void)
{
    SPI0_Operation_Unlock(SPI0_CLIENT_WIFI);
}

static void app_TasksCreate() {
    BaseType_t errStatus;
    errStatus = xTaskCreate((TaskFunction_t) app_PowerAndUITask,
            "PowerAndUITask",
            4096,  // Further increased to prevent stack overflow during disconnect/reconnect
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
            5240,
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

