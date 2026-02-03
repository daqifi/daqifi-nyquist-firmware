#pragma once

#include "BoardRuntimeConfig.h"
#include "HAL/BQ24297/BQ24297.h"
#include "services/sd_card_services/sd_card_manager.h"
#include <string.h>

/**
 * @file CommonRuntimeDefaults.h
 * @brief Common runtime default configurations shared across all Nyquist board variants
 *
 * This file provides macros for configuration sections that are identical across NQ1, NQ2, NQ3.
 * Reduces duplication and ensures consistency when updating defaults.
 */

#ifdef __cplusplus
extern "C" {
#endif

// Compile-time assertions to ensure WiFi string constants fit within their buffers
// (Shared across all variants)
_Static_assert(sizeof(DEFAULT_WIFI_AP_SSID) <= WDRV_WINC_MAX_SSID_LEN + 1, "WiFi SSID too long");
_Static_assert(sizeof(DEFAULT_WIFI_WPA_PSK_PASSKEY) <= WDRV_WINC_PSK_LEN + 1, "WiFi passkey too long");
_Static_assert(sizeof(DEFAULT_NETWORK_HOST_NAME) <= WIFI_MANAGER_DNS_CLIENT_MAX_HOSTNAME_LEN + 1, "Hostname too long");

/**
 * Common DIO channel runtime defaults (16 channels, all configured as inputs)
 * Identical across all board variants
 */
#define COMMON_DIO_RUNTIME_DEFAULTS \
    .DIOChannels = { \
        .Data = { \
            {.IsInput = true, .IsReadOnly = false, .Value = false, .IsPwmActive=false, .PwmFrequency=0, .PwmDutyCycle=0}, \
            {true, false, false, false, 0, 0}, \
            {true, false, false, false, 0, 0}, \
            {true, false, false, false, 0, 0}, \
            {true, false, false, false, 0, 0}, \
            {true, false, false, false, 0, 0}, \
            {true, false, false, false, 0, 0}, \
            {true, false, false, false, 0, 0}, \
            {true, false, false, false, 0, 0}, \
            {true, false, false, false, 0, 0}, \
            {true, false, false, false, 0, 0}, \
            {true, false, false, false, 0, 0}, \
            {true, false, false, false, 0, 0}, \
            {true, false, false, false, 0, 0}, \
            {true, false, false, false, 0, 0}, \
            {true, false, false, false, 0, 0}, \
        }, \
        .Size = 16, \
    }, \
    .DIOGlobalEnable = false

/**
 * Common power configuration defaults
 * Identical across all board variants
 */
#define COMMON_POWER_RUNTIME_DEFAULTS \
    .PowerWriteVars = { \
       .EN_3_3V_Val = true,     /* 3.3V rail on */ \
       .EN_5_10V_Val = false,   /* 5V rail off initially */ \
       .EN_12V_Val = true,      /* 12V rail on (NQ3 supports 12V, NQ1 has inverse logic) */ \
       .EN_Vref_Val = false,    /* Vref rail off initially */ \
       /* BQ24297 Default Configuration (All Variants): \
        * These values are currently unused - actual OTG/CE control is done directly: \
        * - OTG: Controlled via GPIO (BATT_MAN_OTG/RK5) - DEFAULTS TO OFF on all boards \
        *        Hardware init sets LOW (disabled) to allow charging \
        *        Can be enabled via SCPI: SYST:BATT:OTG ON \
        * - CE: Controlled via I2C register REG01 bit 4 - defaults to enabled \
        */ \
       .BQ24297WriteVars = { \
           .OTG_Val = false,  /* OTG OFF by default - enables battery charging */ \
           .CE_Val = false,   /* Reserved/unused (charge enable via I2C only) */ \
       }, \
    }

/**
 * Common UI configuration defaults
 * Identical across all board variants
 */
#define COMMON_UI_RUNTIME_DEFAULTS \
    .UIWriteVars = { \
        .LED1 = false, \
        .LED2 = false, \
    }

/**
 * Common streaming configuration defaults
 * Identical across all board variants
 */
#define COMMON_STREAMING_RUNTIME_DEFAULTS \
    .StreamingConfig = { \
        .IsEnabled = false, \
        .Running = false, \
        .ClockPeriod = 130,   /* default 3k hz (15khz is the max) */ \
        .Frequency = 30000,   /* Default 30kHz (limited by active channel count in SCPI) */ \
        .ChannelScanFreqDiv = 1, /* All channels scan together */ \
        .Encoding = Streaming_ProtoBuffer, \
        .TSClockPeriod = 0xFFFFFFFF,   /* maximum */ \
        .ActiveInterface = StreamingInterface_USB,  /* Default: stream to single interface (USB) */ \
    }

/**
 * Common WiFi settings defaults
 * Identical across all board variants
 */
#define COMMON_WIFI_RUNTIME_DEFAULTS \
    .wifiSettings = { \
        .isEnabled = true, \
        .isWifiFirmwareUpdateModeEnabled = false, \
        .networkMode = DEFAULT_WIFI_NETWORK_MODE, \
        .securityMode = DEFAULT_WIFI_AP_SECURITY_MODE, \
        .ssid = DEFAULT_WIFI_AP_SSID, \
        .passKey = DEFAULT_WIFI_WPA_PSK_PASSKEY, \
        .passKeyLength = strlen(DEFAULT_WIFI_WPA_PSK_PASSKEY), \
        .hostName = DEFAULT_NETWORK_HOST_NAME, \
        .tcpPort = DEFAULT_TCP_PORT, \
        .rssi_percent = 0, \
        .macAddr = {{0}}, \
        .ipAddr = {.Val = 0}, \
        .ipMask = {.Val = 0}, \
        .gateway = {.Val = 0}, \
    }

/**
 * Common SD card configuration defaults
 * Identical across all board variants
 */
#define COMMON_SDCARD_RUNTIME_DEFAULTS \
    .sdCardConfig = { \
        .enable = false, \
        .directory = "DAQiFi", \
        .file = "default.bin", \
        .mode = SD_CARD_MANAGER_MODE_NONE, \
        .maxFileSizeBytes = SD_CARD_MANAGER_FAT32_SAFE_MAX_FILE_SIZE, \
    }

#ifdef __cplusplus
}
#endif
