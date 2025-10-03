#include "BoardRuntimeConfig.h"
#include "HAL/BQ24297/BQ24297.h"
#include "services/sd_card_services/sd_card_manager.h"
#include <string.h>  // For strlen

// Compile-time assertions to ensure WiFi string constants fit within their buffers
_Static_assert(sizeof(DEFAULT_WIFI_AP_SSID) <= WDRV_WINC_MAX_SSID_LEN + 1, "WiFi SSID too long");
_Static_assert(sizeof(DEFAULT_WIFI_WPA_PSK_PASSKEY) <= WDRV_WINC_PSK_LEN + 1, "WiFi passkey too long");
_Static_assert(sizeof(DEFAULT_NETWORK_HOST_NAME) <= WIFI_MANAGER_DNS_CLIENT_MAX_HOSTNAME_LEN + 1, "Hostname too long");

// The default board configuration
// TODO: It would be handly if this was at a special place in memory so we could flash just the board config (vs recompiling the firmware w/ a different configuration)

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

            // Private Internal ADC
            {.IsEnabled = true, .IsDifferential = false, .Frequency = 0, .CalM = 1, .CalB = 0},
            {true, false, 0, 1, 0},
            {true, false, 0, 1, 0},
            {true, false, 0, 1, 0},
            {true, false, 0, 1, 0},
            {false, false, 0, 1, 0}, //TODO(Daqifi): Enabling this channel causes hard fault
            {true, false, 0, 1, 0},
            {true, false, 0, 1, 0},
        },
        .Size = 24
    },
    .PowerWriteVars = {
       .EN_3_3V_Val = true,     // 3.3V rail on
       .EN_5_10V_Val = false,   // 5V rail off
       .EN_12V_Val = true,      // 12V rail off (inverse logic)
       .EN_Vref_Val = false,    // Vref rail off
       /* OTG Mode Configuration:
        * OTG mode IS REQUIRED for battery operation on this board!
        * While BQ24297 has automatic power path, it only passes battery voltage to VSYS.
        * The 3.3V regulator needs VSYS > ~4V for proper operation (3.3V + dropout).
        * OTG mode enables the boost converter to provide 5V on VSYS from battery.
        * 
        * IMPORTANT: OTG mode prevents accurate USB detection by BQ24297.
        * We use the microcontroller's VBUS detection instead (see PowerApi.c).
        */
       .BQ24297WriteVars.OTG_Val = true,
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
