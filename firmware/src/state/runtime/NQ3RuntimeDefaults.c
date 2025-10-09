#include "BoardRuntimeConfig.h"
#include "HAL/BQ24297/BQ24297.h"
#include "services/sd_card_services/sd_card_manager.h"
#include <string.h>  // For strlen

// Compile-time assertions to ensure WiFi string constants fit within their buffers
_Static_assert(sizeof(DEFAULT_WIFI_AP_SSID) <= WDRV_WINC_MAX_SSID_LEN + 1, "WiFi SSID too long");
_Static_assert(sizeof(DEFAULT_WIFI_WPA_PSK_PASSKEY) <= WDRV_WINC_PSK_LEN + 1, "WiFi passkey too long");
_Static_assert(sizeof(DEFAULT_NETWORK_HOST_NAME) <= WIFI_MANAGER_DNS_CLIENT_MAX_HOSTNAME_LEN + 1, "Hostname too long");

/**
 * Default WiFi Configuration for NQ3 Board:
 * - Mode: Access Point (AP) mode
 * - SSID: "DAQiFi" (open network, no password)
 * - Hostname: "NYQUIST3"
 * - TCP Port: 9760
 * - IP addresses: Set to 0 for DHCP/automatic configuration
 * This configuration is used when no WiFi settings are stored in NVM.
 */
const tBoardRuntimeConfig g_NQ3BoardRuntimeConfig = {
    .DIOChannels = {
        .Data = {
            {.IsInput = true, .IsReadOnly = false, .Value = false,.IsPwmActive=false,.PwmFrequency=0,.PwmDutyCycle=0},
            {true, false, false,false,0,0},
            {true, false, false,false,0,0},
            {true, false, false,false,0,0},
            {true, false, false,false,0,0},
            {true, false, false,false,0,0},
            {true, false, false,false,0,0},
            {true, false, false,false,0,0},
            {true, false, false,false,0,0},
            {true, false, false,false,0,0},
            {true, false, false,false,0,0},
            {true, false, false,false,0,0},
            {true, false, false,false,0,0},
            {true, false, false,false,0,0},
            {true, false, false,false,0,0},
            {true, false, false,false,0,0},
        },
        .Size = 16,
    },
    .DIOGlobalEnable = false,
    .AInModules =
    {
        .Data =
        {
            {.IsEnabled = true, .Range = 5.0},  // MC12bADC module
            {.IsEnabled = true, .Range = 10.0}, // AD7609 module (±10V range)
        },
        .Size = 2,
    },
    .AInChannels =
    {
        .Data =
        {
            // AD7609 channels 0-7 (user-accessible)
            {.IsEnabled = false, .IsDifferential = false, .Frequency = 0, .CalM = 1, .CalB = 0},
            {false, false, 0, 1, 0},
            {false, false, 0, 1, 0},
            {false, false, 0, 1, 0},
            {false, false, 0, 1, 0},
            {false, false, 0, 1, 0},
            {false, false, 0, 1, 0},
            {false, false, 0, 1, 0},
            // Internal monitoring channels (MC12bADC) - always enabled at 1Hz for system health
            {true, false, 1, 1, 0}, // ADC_CHANNEL_3_3V - 1Hz monitoring
            {true, false, 1, 1, 0}, // ADC_CHANNEL_2_5VREF - 1Hz monitoring  
            {true, false, 1, 1, 0}, // ADC_CHANNEL_VBATT - 1Hz monitoring
            {true, false, 1, 1, 0}, // ADC_CHANNEL_5V - 1Hz monitoring
            {true, false, 1, 1, 0}, // ADC_CHANNEL_10V - 1Hz monitoring
            {true, false, 1, 1, 0}, // ADC_CHANNEL_TEMP - 1Hz monitoring
            {true, false, 1, 1, 0}, // ADC_CHANNEL_5VREF - 1Hz monitoring
            {true, false, 1, 1, 0}, // ADC_CHANNEL_VSYS - 1Hz monitoring
        },
        .Size = 16,
    },
    .AOutChannels =
    {
        .Data =
        {
            // DAC7718 channels 0-7 (user-accessible analog outputs, fixed 0-10V range)
            {.IsEnabled = true, .OutputVoltage = 0.0, .CalibrationM = 1.0, .CalibrationB = 0.0, .FactoryCalibrationM = 1.0, .FactoryCalibrationB = 0.0, .UseUserCalibration = false},
            {.IsEnabled = true, .OutputVoltage = 0.0, .CalibrationM = 1.0, .CalibrationB = 0.0, .FactoryCalibrationM = 1.0, .FactoryCalibrationB = 0.0, .UseUserCalibration = false},
            {.IsEnabled = true, .OutputVoltage = 0.0, .CalibrationM = 1.0, .CalibrationB = 0.0, .FactoryCalibrationM = 1.0, .FactoryCalibrationB = 0.0, .UseUserCalibration = false},
            {.IsEnabled = true, .OutputVoltage = 0.0, .CalibrationM = 1.0, .CalibrationB = 0.0, .FactoryCalibrationM = 1.0, .FactoryCalibrationB = 0.0, .UseUserCalibration = false},
            {.IsEnabled = true, .OutputVoltage = 0.0, .CalibrationM = 1.0, .CalibrationB = 0.0, .FactoryCalibrationM = 1.0, .FactoryCalibrationB = 0.0, .UseUserCalibration = false},
            {.IsEnabled = true, .OutputVoltage = 0.0, .CalibrationM = 1.0, .CalibrationB = 0.0, .FactoryCalibrationM = 1.0, .FactoryCalibrationB = 0.0, .UseUserCalibration = false},
            {.IsEnabled = true, .OutputVoltage = 0.0, .CalibrationM = 1.0, .CalibrationB = 0.0, .FactoryCalibrationM = 1.0, .FactoryCalibrationB = 0.0, .UseUserCalibration = false},
            {.IsEnabled = true, .OutputVoltage = 0.0, .CalibrationM = 1.0, .CalibrationB = 0.0, .FactoryCalibrationM = 1.0, .FactoryCalibrationB = 0.0, .UseUserCalibration = false},
        },
        .Size = 8,
    },
    .PowerWriteVars = {
       .EN_3_3V_Val = true,     // 3.3V rail on
       .EN_5_10V_Val = false,   // 5V rail off initially
       .EN_12V_Val = true,      // 12V rail on (NQ3 supports 12V)
       .EN_Vref_Val = false,    // Vref rail off initially
       .BQ24297WriteVars.OTG_Val = true, // OTG mode for battery operation
    },
    .UIWriteVars = {
        .LED1 = false,
        .LED2 = false,
    },
    .StreamingConfig = {
        .IsEnabled = false,
        .Running = false,
        .ClockPeriod = 130,   // default 3k hz (15khz is the max)
        .Frequency = 30000,   // Default 30kHz (limited by active channel count in SCPI)
        .ChannelScanFreqDiv = 3, //max channel scan frequency should be 1000 hz
        .Encoding = Streaming_ProtoBuffer,
        .TSClockPeriod = 0xFFFFFFFF,   // maximum
    },
    .wifiSettings = {
        .isEnabled = true,
        .isOtaModeEnabled = false,
        .networkMode = DEFAULT_WIFI_NETWORK_MODE,
        .securityMode = DEFAULT_WIFI_AP_SECURITY_MODE,
        .ssid = DEFAULT_WIFI_AP_SSID,
        .passKey = DEFAULT_WIFI_WPA_PSK_PASSKEY,
        .passKeyLength = strlen(DEFAULT_WIFI_WPA_PSK_PASSKEY),  // Safer than sizeof
        .hostName = DEFAULT_NETWORK_HOST_NAME,
        .tcpPort = DEFAULT_TCP_PORT,
        .rssi_percent = 0,                // Signal strength (not applicable for AP mode)
        .macAddr = {{0}},                 // Will be populated from hardware
        .ipAddr = {.Val = 0},            // DHCP/automatic configuration
        .ipMask = {.Val = 0},            // DHCP/automatic configuration
        .gateway = {.Val = 0},           // DHCP/automatic configuration
    },
    //.usbSettings = {0},
    //.serverData = {0},
    .sdCardConfig={
        .enable=false,
        .directory="DAQiFi",
        .file="default.bin",
        .mode=SD_CARD_MANAGER_MODE_NONE,
    },

};

/*! This function is used for getting the board runtime configuration defaults
 * @return Pointer to Board Runtime Configuration structure
 */
const tBoardRuntimeConfig* NqBoardRuntimeConfig_GetDefaults(void) {
    return &g_NQ3BoardRuntimeConfig;
}
