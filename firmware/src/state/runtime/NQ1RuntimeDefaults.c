#include "BoardRuntimeConfig.h"
#include "CommonRuntimeDefaults.h"
#include "../board/CommonMonitoringChannels.h"

/**
 * Default WiFi Configuration for NQ1 Board:
 * - Mode: Access Point (AP) mode
 * - SSID: "DAQiFi" (open network, no password)
 * - Hostname: "NYQUIST1"
 * - TCP Port: 9760
 * - IP addresses: Set to 0 for DHCP/automatic configuration
 * This configuration is used when no WiFi settings are stored in NVM.
 */
const tBoardRuntimeConfig g_NQ1BoardRuntimeConfig = {
    COMMON_DIO_RUNTIME_DEFAULTS,
    .AInModules =
    {
        .Data =
        {
            {.IsEnabled = true, .Range = 5.0},
        },
        .Size = 1,
    },
    .AInChannels =
    {
        .Data =
        {
            // Public Internal ADC
            {.IsEnabled = false, .IsDifferential = false, .Frequency = 0, .CalM = 1, .CalB = 0},
            {false, false, 0, 1, 0},
            {false, false, 0, 1, 0},
            {false, false, 0, 1, 0},
            {false, false, 0, 1, 0},
            {false, false, 0, 1, 0},
            {false, false, 0, 1, 0},
            {false, false, 0, 1, 0},
            {false, false, 0, 1, 0}, // Ch 0
            {false, false, 0, 1, 0},
            {false, false, 0, 1, 0}, // Ch 1
            {false, false, 0, 1, 0},
            {false, false, 0, 1, 0}, // Ch 2
            {false, false, 0, 1, 0},
            {false, false, 0, 1, 0}, // Ch 3
            {false, false, 0, 1, 0},

            // Internal monitoring channels (from CommonMonitoringChannels.h)
            COMMON_MONITORING_CHANNELS_RUNTIME
        },
        .Size = 24
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
    return &g_NQ1BoardRuntimeConfig;
}
