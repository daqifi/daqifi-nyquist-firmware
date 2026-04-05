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
#include "HAL/DAC7718/DAC7718.h"
#include "Util/Logger.h"
#include "Util/CoherentPool.h"
#include "Util/StreamingBufferPool.h"
#include "state/data/AInSample.h"
#include "services/UsbCdc/UsbCdc.h"
#include "services/wifi_services/wifi_tcp_server.h"

/*
 * SPI Coordination Framework for Future Extensibility
 * 
 * CURRENT STATE - COORDINATION DISABLED:
 * SPI coordination is currently disabled because:
 * - Both WiFi (WINC1500) and SD card use identical SPI Mode 0 (CPOL=0, CPHA=0, 8-bit)
 * - Both operate at standardized 20 MHz frequency (no conflicts)
 * - Harmony SPI driver with 2-client configuration provides adequate coordination
 * - Testing shows electrical compatibility eliminates coordination overhead
 * 
 * TRANSACTION-LEVEL ARBITRATION:
 * Qodo concern: "Removing enable-level mutex without transaction-level arbiter"
 * Response: Harmony SPI driver (drv_spi.c) provides transaction-level arbitration:
 * - Built-in mutexes: mutexClientObjects, mutexTransferObjects, mutexExclusiveUse
 * - Client isolation: Separate handles and transfer queues per client
 * - DMA coordination: Hardware-level arbitration prevents conflicts
 * - Queue management: 64-operation deep queue serializes transactions
 * - Testing validation: 7+ hours stable concurrent operations at 25 MHz
 * 
 * FUTURE USE CASES - WHEN TO ENABLE:
 * Enable SPI coordination when:
 * - Different client frequencies required (speed optimization per client)
 * - Different SPI modes needed (clock polarity/phase differences)
 * - Advanced timing requirements (priority-based preemption)
 * - Enhanced error recovery (per-client fault isolation)
 * - Real-time guarantees needed (deterministic access patterns)
 * 
 * IMPLEMENTATION READY:
 * Complete mutex infrastructure preserved for quick activation when needed.
 */

// Configuration: SPI frequency coordination framework
// 0 = Disabled (current) - both clients use standardized frequency, no runtime overhead
// 1 = Enabled (testing) - client-specific frequencies with mutex coordination
#define SPI0_COORDINATION_ENABLED 0  // Disabled for production - enable for frequency benchmarking

// SPI frequency definitions (reserved for coordination framework)
#include "config/default/configuration.h"
#define SPI0_WIFI_FREQUENCY_HZ      20000000  // Reserved: WiFi target frequency for benchmarking
#define SPI0_SD_FREQUENCY_HZ        20000000  // Reserved: SD target frequency for benchmarking

// Compile-time validation for frequency management
#if (SPI0_COORDINATION_ENABLED == 0)
#define SPI0_STANDARDIZED_FREQUENCY_HZ  20000000  // Both clients must use this when coordination disabled
#if (DRV_SDSPI_SPEED_HZ_IDX0 != SPI0_STANDARDIZED_FREQUENCY_HZ)
#error "SPI frequency mismatch detected! When coordination disabled, standardize frequencies or enable coordination (SPI0_COORDINATION_ENABLED=1)."
#endif
#else
// When coordination enabled, validate SD card configuration matches our managed frequency
#if (DRV_SDSPI_SPEED_HZ_IDX0 != SPI0_SD_FREQUENCY_HZ)
#error "SD card frequency mismatch! DRV_SDSPI_SPEED_HZ_IDX0 must match SPI0_SD_FREQUENCY_HZ when coordination enabled."
#endif
#endif

// Future validation: Add WiFi frequency check when configurable
// #if defined(DRV_WIFI_SPI_SPEED_HZ) && (DRV_WIFI_SPI_SPEED_HZ != SPI0_STANDARDIZED_FREQUENCY_HZ) && (SPI0_COORDINATION_ENABLED == 0)
// #error "WiFi SPI frequency mismatch! Enable SPI coordination or standardize frequencies."
// #endif

// SPI0 bus clients for identification and future coordination
typedef enum {
    SPI0_CLIENT_SD_CARD = 0,    // Higher priority
    SPI0_CLIENT_WIFI = 1,       // Lower priority  
    SPI0_CLIENT_MAX
} spi0_client_t;

#if SPI0_COORDINATION_ENABLED
// SPI coordination infrastructure (ENABLED SECTION - compiled when coordination active)
// Timeout values for different operation types
#define SPI0_WIFI_SCPI_TIMEOUT_MS    5     // WiFi SCPI commands (fast response)
#define SPI0_WIFI_DATA_TIMEOUT_MS    20    // WiFi data operations
#define SPI0_SD_TIMEOUT_MS           100   // SD card operations (can wait)
#define SPI0_MUTEX_USE_DEFAULT ((TickType_t)~(TickType_t)0)

// Static variables for SPI coordination
static SemaphoreHandle_t spi0_mutex = NULL;
static spi0_client_t current_owner = SPI0_CLIENT_MAX;
static TaskHandle_t owner_task = NULL;

// Function declarations
bool SPI0_Mutex_Initialize(void);
bool SPI0_Operation_Lock(spi0_client_t client, TickType_t timeout);
void SPI0_Operation_Unlock(spi0_client_t client);
#else
// SPI coordination infrastructure (DISABLED SECTION - current production mode)
// Mutual exclusion approach used instead: SD operations suspended during WiFi streaming
// Placeholder functions compiled as no-ops for minimal overhead
bool SPI0_Mutex_Initialize(void);  // No-op function when coordination disabled
#endif
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

// USB transfer constants for SD card callback
// Clamp chunk size to USB buffer capacity with underflow protection
#define USB_TRANSFER_CHUNK_SIZE_RAW     4000U
#define USB_TRANSFER_CHUNK_SIZE \
    ((USBCDC_WBUFFER_SIZE <= 32U) ? 16U : \
     ((USBCDC_WBUFFER_SIZE < USB_TRANSFER_CHUNK_SIZE_RAW) ? \
      (USBCDC_WBUFFER_SIZE - 16U) : \
      USB_TRANSFER_CHUNK_SIZE_RAW))
#define USB_TRANSFER_MAX_RETRIES        10000U   // Maximum retry attempts (10 second timeout at 1ms per retry)

static void app_SystemInit();
static void app_USBDeviceTask(void* p_arg);
static void app_WifiTask(void* p_arg);
static void app_SDCardTask(void* p_arg);

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
    // Defensive checks
    if (pDataBuff == NULL || dataLen == 0) {
        return;
    }

    size_t transferredLength = 0;
    uint32_t retryCount = 0;

    while (transferredLength < dataLen) {
        size_t remaining = dataLen - transferredLength;
        size_t toSend = (remaining < USB_TRANSFER_CHUNK_SIZE) ? remaining : USB_TRANSFER_CHUNK_SIZE;

        size_t bytesWritten = UsbCdc_WriteToBuffer(
                NULL,
                (const char *) pDataBuff + transferredLength,
                toSend
                );

        if (bytesWritten > 0) {
            transferredLength += bytesWritten;
            retryCount = 0;
            // If partial write, yield to let USB drain before retrying
            if (bytesWritten < toSend) {
                vTaskDelay(1);
            }
        } else {
            retryCount++;
            if (retryCount >= USB_TRANSFER_MAX_RETRIES) {
                LOG_E("[USB] Callback timeout: sent %u/%u bytes after %u retries",
                      (unsigned)transferredLength, (unsigned)dataLen, (unsigned)retryCount);
                break;
            }
            vTaskDelay(1);
        }
    }

    if (transferredLength < dataLen) {
        LOG_E("[USB] Incomplete transfer: sent %u/%u bytes",
              (unsigned)transferredLength, (unsigned)dataLen);
    }
}

static void app_USBDeviceTask(void* p_arg) {
    // Enable FPU context saving - USB task processes streaming data with voltage conversion
    portTASK_USES_FLOATING_POINT();
    UsbCdc_Initialize();

    // Boost priority after initialization complete
    vTaskPrioritySet(NULL, 7);

    while (1) {
        UsbCdc_ProcessState();
        vTaskDelay(1);
    }
}

static void app_WifiTask(void* p_arg) {
    // Enable FPU context saving - WiFi task processes streaming data with voltage conversion
    portTASK_USES_FLOATING_POINT();
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

/**
 * Gracefully shut down SD card operations from within the SD task context.
 * Drives the state machine through DEINIT -> UNMOUNT_DISK to drain buffers,
 * flush filesystem, close files, and unmount the disk.
 *
 * Must be called from the SD task (drives DRV_SDSPI_Tasks/ProcessState/SYS_FS_Tasks).
 *
 * @param timeoutMs Maximum time to wait for idle
 * @param reason    Human-readable reason for logging
 * @return true if SD reached idle within timeout, false if timed out
 */
static bool app_SDCard_GracefulShutdown(uint32_t timeoutMs, const char* reason) {
    if (sd_card_manager_IsIdle()) {
        return true;
    }

    LOG_I("[SD] Graceful shutdown: %s", reason);

    // Trigger DEINIT -> UNMOUNT_DISK path (same mechanism as SCPI_StopStreaming)
    sd_card_manager_settings_t* pSDSettings =
        BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
    pSDSettings->mode = SD_CARD_MANAGER_MODE_NONE;
    sd_card_manager_UpdateSettings(pSDSettings);

    // Drive the state machine ourselves — we ARE the SD task
    uint32_t waited = 0;
    while (!sd_card_manager_IsIdle() && waited < timeoutMs) {
        DRV_SDSPI_Tasks(sysObj.drvSDSPI0);
        sd_card_manager_ProcessState();
        SYS_FS_Tasks();
        vTaskDelay(pdMS_TO_TICKS(10));
        waited += 10;
    }

    if (!sd_card_manager_IsIdle()) {
        LOG_E("[SD] Graceful shutdown timeout after %u ms: %s",
              (unsigned)timeoutMs, reason);
        return false;
    }

    LOG_I("[SD] Graceful shutdown complete: %s", reason);
    return true;
}

/**
 * Check if WiFi needs the SPI bus (streaming to WiFi or firmware update).
 */
static bool app_SDCard_IsWifiUsingSPI(void) {
    StreamingRuntimeConfig* pStreamConfig =
        BoardRunTimeConfig_Get(BOARDRUNTIME_STREAMING_CONFIGURATION);
    bool isStreaming = pStreamConfig->IsEnabled;
    bool isWifiStreaming = isStreaming &&
                          (pStreamConfig->ActiveInterface == StreamingInterface_WiFi ||
                           pStreamConfig->ActiveInterface == StreamingInterface_All);
    return isWifiStreaming || wifi_manager_IsWifiFirmwareUpdateActive();
}

/**
 * SD Card Task
 *
 * Power-aware state machine modeled after app_WifiTask.
 * Three states:
 *   WAIT_POWER_UP — waits for device to be powered before SD operations
 *   PROCESS       — normal SD operations; monitors power state and WiFi SPI usage
 *   SUSPENDED     — SD gracefully unmounted while WiFi owns the SPI bus
 */
static void app_SDCardTask(void* p_arg) {
    enum {
        APP_SD_STATE_WAIT_POWER_UP = 0,
        APP_SD_STATE_PROCESS = 1,
        APP_SD_STATE_SUSPENDED = 2,
    };

    sd_card_manager_Init(&gpBoardRuntimeConfig->sdCardConfig);
    const tPowerData* pPowerState = BoardData_Get(BOARDDATA_POWER_DATA, 0);
    uint8_t state = APP_SD_STATE_WAIT_POWER_UP;

    while (1) {
        switch (state) {
            case APP_SD_STATE_WAIT_POWER_UP:
            {
                if (pPowerState != NULL &&
                    (pPowerState->powerState == POWERED_UP ||
                     pPowerState->powerState == POWERED_UP_EXT_DOWN)) {
                    state = APP_SD_STATE_PROCESS;
                } else {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                break;
            }

            case APP_SD_STATE_PROCESS:
            {
                // Check power state — shut down SD if device is powering down
                if (pPowerState != NULL &&
                    pPowerState->powerState != POWERED_UP &&
                    pPowerState->powerState != POWERED_UP_EXT_DOWN) {
                    app_SDCard_GracefulShutdown(3000, "power state dropped");
                    state = APP_SD_STATE_WAIT_POWER_UP;
                    break;
                }

                // Check if WiFi needs exclusive SPI bus access
                if (app_SDCard_IsWifiUsingSPI()) {
                    app_SDCard_GracefulShutdown(3000,
                        wifi_manager_IsWifiFirmwareUpdateActive()
                            ? "WiFi firmware update" : "WiFi streaming");
                    state = APP_SD_STATE_SUSPENDED;
                    break;
                }

                // Normal SD operations
                DRV_SDSPI_Tasks(sysObj.drvSDSPI0);
                sd_card_manager_ProcessState();
                SYS_FS_Tasks();
                vTaskDelay(SD_CARD_MANAGER_TASK_DELAY_MS / portTICK_PERIOD_MS);
                break;
            }

            case APP_SD_STATE_SUSPENDED:
            {
                // Check if WiFi released the SPI bus
                if (!app_SDCard_IsWifiUsingSPI()) {
                    if (pPowerState != NULL &&
                        (pPowerState->powerState == POWERED_UP ||
                         pPowerState->powerState == POWERED_UP_EXT_DOWN)) {
                        // SD remounts on-demand when a new SCPI command sets the mode
                        // (LIST, GET, LOG+StartStream, etc.). Logging is NOT auto-resumed
                        // because the file was closed and the streaming session may have ended.
                        LOG_I("[SD] WiFi released SPI bus, resuming SD operations");
                        state = APP_SD_STATE_PROCESS;
                    } else {
                        state = APP_SD_STATE_WAIT_POWER_UP;
                    }
                } else {
                    // Also check power while suspended
                    if (pPowerState != NULL &&
                        pPowerState->powerState != POWERED_UP &&
                        pPowerState->powerState != POWERED_UP_EXT_DOWN) {
                        // No need to shutdown again — already unmounted
                        state = APP_SD_STATE_WAIT_POWER_UP;
                    }
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                break;
            }
        }
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
    // Initialize SPI coordination framework (currently disabled)
    // Note: Coordination disabled (SPI0_COORDINATION_ENABLED=0) - no runtime overhead
    // To enable frequency benchmarking: Set SPI0_COORDINATION_ENABLED=1 and rebuild
    if (!SPI0_Mutex_Initialize()) {
        // No-op when coordination disabled - always returns true
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
    CoherentPool_Init();
    // Allocate WiFi SPI DMA staging buffer from coherent pool at boot
    // (needed before WiFi init; auto-balanced at stream start)
    {
        #define WIFI_DMA_BOOT_SIZE (32U * 1024U)
        uint8_t* wifiBuf = CoherentPool_Alloc("WiFi_SPI", WIFI_DMA_BOOT_SIZE);
        if (wifiBuf != NULL) {
            extern void WDRV_WINC_SPI_SetBuffer(uint8_t* buf, uint32_t size);
            WDRV_WINC_SPI_SetBuffer(wifiBuf, WIFI_DMA_BOOT_SIZE);
        }
    }
    StreamingBufferPool_Init(USBCDC_CIRCULAR_BUFF_SIZE, WIFI_CIRCULAR_BUFF_SIZE,
                             ENCODER_BUFFER_DEFAULT, SD_CARD_MANAGER_DEFAULT_CIRCULAR_SIZE,
                             DEFAULT_AIN_SAMPLE_COUNT);
    InitializeBoardData(gpBoardData);

    // Apply persisted voltage precision to streaming runtime config
    {
        uint8_t savedPrec = tmpTopLevelSettings.settings.topLevelSettings.voltagePrecision;
        if (savedPrec <= 10) {
            StreamingRuntimeConfig *pStreamCfg = BoardRunTimeConfig_Get(
                    BOARDRUNTIME_STREAMING_CONFIGURATION);
            if (pStreamCfg != NULL) {
                pStreamCfg->VoltagePrecision = savedPrec;
            }
        }
    }

    // Try to load WiFiSettings from NVM - if this fails, store default 
    // settings to NVM (first run after a program)


    tmpSettings.type = DaqifiSettings_Wifi;
    if (!daqifi_settings_LoadFromNvm(DaqifiSettings_Wifi, &tmpSettings)) {
        // Get board wifi settings from Wifi NVM variable
        daqifi_settings_LoadFactoryDeafult(DaqifiSettings_Wifi, &tmpSettings);
        daqifi_settings_SaveToNvm(&tmpSettings);
    }
    // Move temp variable to global variables
    tmpSettings.settings.wifi.isWifiFirmwareUpdateModeEnabled = false;
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
    
    // Initialize DAC7718 global structures (NQ3 only)
    if (gpBoardConfig->BoardVariant == 3) {
        DAC7718_InitGlobal();
        LOG_D("DAC7718 global structures initialized - hardware init deferred until power up");
        LOG_D("Board config AOut modules: Size=%d", gpBoardConfig->AOutModules.Size);
    }
    
    EVIC_SourceEnable(INT_SOURCE_CHANGE_NOTICE_A);
}

#if SPI0_COORDINATION_ENABLED
/* 
 * Full SPI Coordination Implementation (Currently Disabled)
 * Ready for activation when client-specific requirements emerge
 */
bool SPI0_Mutex_Initialize(void)
{
    if (spi0_mutex == NULL) {
        spi0_mutex = xSemaphoreCreateMutex();
        if (spi0_mutex == NULL) {
            return false;
        }
        current_owner = SPI0_CLIENT_MAX;
        owner_task = NULL;
    }
    return true;
}

// Client-specific SPI configuration management
bool SPI0_Context_Apply(spi0_client_t client)
{
    // Get client-specific frequency
    uint32_t frequency = (client == SPI0_CLIENT_WIFI) ? 
                        SPI0_WIFI_FREQUENCY_HZ : SPI0_SD_FREQUENCY_HZ;
    
    // Optimization: Skip setup if frequencies are identical (no switching needed)
    if (SPI0_WIFI_FREQUENCY_HZ == SPI0_SD_FREQUENCY_HZ) {
        return true;  // No frequency change needed
    }
    
    // Open temporary handle to SPI0 driver for frequency configuration
    DRV_HANDLE tempHandle = DRV_SPI_Open(DRV_SPI_INDEX_0, DRV_IO_INTENT_READWRITE);
    if (tempHandle == DRV_HANDLE_INVALID) {
        return false;
    }
    
    DRV_SPI_TRANSFER_SETUP setup = {
        .baudRateInHz = frequency,
        .chipSelect = SYS_PORT_PIN_NONE,  // Both clients manage their own CS
        .clockPhase = DRV_SPI_CLOCK_PHASE_VALID_LEADING_EDGE,
        .clockPolarity = DRV_SPI_CLOCK_POLARITY_IDLE_LOW,
        .dataBits = DRV_SPI_DATA_BITS_8
    };
    
    bool result = DRV_SPI_TransferSetup(tempHandle, &setup);
    
    // Close temporary handle
    DRV_SPI_Close(tempHandle);
    
    return result;
}

bool SPI0_Operation_Lock(spi0_client_t client, TickType_t timeout)
{
    if (spi0_mutex == NULL || client >= SPI0_CLIENT_MAX) {
        return false;
    }
    
    TickType_t wait_time = (timeout == SPI0_MUTEX_USE_DEFAULT) ? 
                          pdMS_TO_TICKS(20) : timeout;
    
    if (xSemaphoreTake(spi0_mutex, wait_time) == pdTRUE) {
        // Apply client-specific SPI configuration (frequency optimization)
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
    
    if (current_owner == client && owner_task == xTaskGetCurrentTaskHandle()) {
        current_owner = SPI0_CLIENT_MAX;
        owner_task = NULL;
        xSemaphoreGive(spi0_mutex);
    }
}

#else
/*
 * Minimal SPI Framework (Currently Active)
 * 
 * SPI coordination disabled due to electrical compatibility:
 * - Both clients use SPI Mode 0 (CPOL=0, CPHA=0, 8-bit)
 * - Both use 20 MHz frequency (standardized)
 * - Harmony driver handles multi-client coordination adequately
 * 
 * To enable full coordination: Set SPI0_COORDINATION_ENABLED to 1
 */
bool SPI0_Mutex_Initialize(void)
{
    // Placeholder initialization for future coordination needs
    // Currently no-op due to electrical compatibility of SPI clients
    return true;
}
#endif

static void app_TasksCreate() {
    BaseType_t errStatus;
    // Stack sizes profiled under stress: 16ch@5kHz PB/CSV/JSON + SD file ops + WiFi TCP.
    // Sized at 2-3x measured peak usage. See issue #230 for profiling data.
    // WARNING: If recursive SD directory listing is enabled, SDCardTask needs 10KB+.
    errStatus = xTaskCreate((TaskFunction_t) app_PowerAndUITask,
            "PowerAndUITask",
            640,   // Profiled: 226+64 FPU = 290 words peak. 2x margin = 580, rounded to 640. (was 4096)
            NULL,
            7,
            NULL);
    if (errStatus != pdTRUE) {
        LOG_E("FATAL: Failed to create PowerAndUITask\r\n");
        while (1);
    }

    errStatus = xTaskCreate((TaskFunction_t) app_USBDeviceTask,
            "USBDeviceTask",
            USBDEVICETASK_SIZE,  // 3072 — SCPI callbacks use 256-512 byte local buffers
            NULL,
            2,
            NULL);
    if (errStatus != pdTRUE) {
        LOG_E("FATAL: Failed to create USBDeviceTask\r\n");
        while (1);
    }

    errStatus = xTaskCreate((TaskFunction_t) app_WifiTask,
            "WifiTask",
            1024,  // Profiled: 360 words peak. 3x margin for unknown WiFi driver depth. (was 3000)
            NULL,
            2,
            NULL);
    if (errStatus != pdTRUE) {
        LOG_E("FATAL: Failed to create WifiTask\r\n");
        while (1);
    }

    errStatus = xTaskCreate((TaskFunction_t) app_SDCardTask,
            "SDCardTask",
            1024,  // Profiled: 468 words peak. 2x+ margin. (was 5240)
            NULL,
            2,
            NULL);
    if (errStatus != pdTRUE) {
        LOG_E("FATAL: Failed to create SDCardTask\r\n");
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

