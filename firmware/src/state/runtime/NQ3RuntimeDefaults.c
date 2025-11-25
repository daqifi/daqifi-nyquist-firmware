#include "BoardRuntimeConfig.h"
#include "CommonRuntimeDefaults.h"
#include "../board/CommonMonitoringChannels.h"

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
    COMMON_DIO_RUNTIME_DEFAULTS,
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
            // Internal monitoring channels (from CommonMonitoringChannels.h)
            COMMON_MONITORING_CHANNELS_RUNTIME
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
    COMMON_POWER_RUNTIME_DEFAULTS,
    COMMON_UI_RUNTIME_DEFAULTS,
    COMMON_STREAMING_RUNTIME_DEFAULTS,
    COMMON_WIFI_RUNTIME_DEFAULTS,
    COMMON_SDCARD_RUNTIME_DEFAULTS,

};

/*! This function is used for getting the board runtime configuration defaults
 * @return Pointer to Board Runtime Configuration structure
 */
const tBoardRuntimeConfig* NqBoardRuntimeConfig_GetDefaults(void) {
    return &g_NQ3BoardRuntimeConfig;
}
