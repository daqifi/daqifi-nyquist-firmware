#pragma once

#include "BoardConfig.h"
#include "CommonBoardPinDefs.h"

/**
 * @file CommonBoardConfig.h
 * @brief Common board configuration data shared across all Nyquist variants
 *
 * Contains actual const data and initialization macros for configuration
 * sections that are identical across NQ1, NQ2, NQ3 board variants.
 */

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// Common CSV Column Headers (shared across all variants)
// =============================================================================

/**
 * Common CSV column headers for analog input channels.
 * All variants use 16 channels with "ain" prefix and timestamp,value pairs.
 */
extern const char* COMMON_CSV_CHANNEL_HEADERS_FIRST[16];
extern const char* COMMON_CSV_CHANNEL_HEADERS_SUBSEQUENT[16];

// =============================================================================
// Common DIO Channels Configuration Macro
// =============================================================================

/**
 * DIO channels configuration data - identical across all board variants.
 * 16 channels with same port mappings, bit positions, and PPS assignments.
 *
 * NOTE: DIO_0 has conditional compilation for DIO_TIMING_TEST mode.
 * NOTE: The .DIOChannels section (with .Size field) cannot be fully extracted
 *       to a macro due to #ifdef preprocessor limitations. Each variant must
 *       include the section with the #ifdef DIO_TIMING_TEST conditional.
 */
#define COMMON_DIO_CHANNELS_CONFIG_DATA \
    { \
        { .Port = DIO_0_PORT, .BitPos = PORTS_BIT_POS_1, .EnPort = DIO_EN_0_PORT, .EnBitPos = PORTS_BIT_POS_2, .IsInputCapable = false, .HasPwm = true, .PwmIndex = 1, .PpsOutput = OUTPUT_PIN_RPD1 }, \
        { .Port = DIO_1_PORT, .BitPos = PORTS_BIT_POS_3, .EnPort = DIO_EN_1_PORT, .EnBitPos = PORTS_BIT_POS_2, .IsInputCapable = true,  .HasPwm = false, .PwmIndex = 0xFF, .PpsOutput = (PORTS_REMAP_OUTPUT_PIN)0xFF }, \
        { .Port = DIO_2_PORT, .BitPos = PORTS_BIT_POS_3, .EnPort = DIO_EN_2_PORT, .EnBitPos = PORTS_BIT_POS_13, .IsInputCapable = true, .HasPwm = false, .PwmIndex = 0xFF, .PpsOutput = (PORTS_REMAP_OUTPUT_PIN)0xFF }, \
        { .Port = DIO_3_PORT, .BitPos = PORTS_BIT_POS_12, .EnPort = DIO_EN_3_PORT, .EnBitPos = PORTS_BIT_POS_0, .IsInputCapable = false, .HasPwm = true, .PwmIndex = 8, .PpsOutput = OUTPUT_PIN_RPD12 }, \
        { .Port = DIO_4_PORT, .BitPos = PORTS_BIT_POS_0, .EnPort = DIO_EN_4_PORT, .EnBitPos = PORTS_BIT_POS_7, .IsInputCapable = true, .HasPwm = true, .PwmIndex = 4, .PpsOutput = OUTPUT_PIN_RPF0 }, \
        { .Port = DIO_5_PORT, .BitPos = PORTS_BIT_POS_1, .EnPort = DIO_EN_5_PORT, .EnBitPos = PORTS_BIT_POS_7, .IsInputCapable = false, .HasPwm = true, .PwmIndex = 6, .PpsOutput = OUTPUT_PIN_RPF1 }, \
        { .Port = DIO_6_PORT, .BitPos = PORTS_BIT_POS_0, .EnPort = DIO_EN_6_PORT, .EnBitPos = PORTS_BIT_POS_4, .IsInputCapable = true, .HasPwm = true, .PwmIndex = 7, .PpsOutput = OUTPUT_PIN_RPG0 }, \
        { .Port = DIO_7_PORT, .BitPos = PORTS_BIT_POS_1, .EnPort = DIO_EN_7_PORT, .EnBitPos = PORTS_BIT_POS_5, .IsInputCapable = false, .HasPwm = true, .PwmIndex = 3, .PpsOutput = OUTPUT_PIN_RPG1 }, \
        { .Port = DIO_8_PORT, .BitPos = PORTS_BIT_POS_6, .EnPort = DIO_EN_8_PORT, .EnBitPos = PORTS_BIT_POS_7, .IsInputCapable = false, .HasPwm = false, .PwmIndex = 0xFF, .PpsOutput = (PORTS_REMAP_OUTPUT_PIN)0xFF }, \
        { .Port = DIO_9_PORT, .BitPos = PORTS_BIT_POS_1, .EnPort = DIO_EN_9_PORT, .EnBitPos = PORTS_BIT_POS_0, .IsInputCapable = true, .HasPwm = false, .PwmIndex = 0xFF, .PpsOutput = (PORTS_REMAP_OUTPUT_PIN)0xFF }, \
        { .Port = DIO_10_PORT, .BitPos = PORTS_BIT_POS_4, .EnPort = DIO_EN_10_PORT, .EnBitPos = PORTS_BIT_POS_15, .IsInputCapable = false, .HasPwm = false, .PwmIndex = 0xFF, .PpsOutput = (PORTS_REMAP_OUTPUT_PIN)0xFF }, \
        { .Port = DIO_11_PORT, .BitPos = PORTS_BIT_POS_2, .EnPort = DIO_EN_11_PORT, .EnBitPos = PORTS_BIT_POS_10, .IsInputCapable = true, .HasPwm = false, .PwmIndex = 0xFF, .PpsOutput = (PORTS_REMAP_OUTPUT_PIN)0xFF }, \
        { .Port = DIO_12_PORT, .BitPos = PORTS_BIT_POS_3, .EnPort = DIO_EN_12_PORT, .EnBitPos = PORTS_BIT_POS_2, .IsInputCapable = true, .HasPwm = false, .PwmIndex = 0xFF, .PpsOutput = (PORTS_REMAP_OUTPUT_PIN)0xFF }, \
        { .Port = DIO_13_PORT, .BitPos = PORTS_BIT_POS_6, .EnPort = DIO_EN_13_PORT, .EnBitPos = PORTS_BIT_POS_7, .IsInputCapable = false, .HasPwm = false, .PwmIndex = 0xFF, .PpsOutput = (PORTS_REMAP_OUTPUT_PIN)0xFF }, \
        { .Port = DIO_14_PORT, .BitPos = PORTS_BIT_POS_5, .EnPort = DIO_EN_14_PORT, .EnBitPos = PORTS_BIT_POS_5, .IsInputCapable = true, .HasPwm = false, .PwmIndex = 0xFF, .PpsOutput = (PORTS_REMAP_OUTPUT_PIN)0xFF }, \
        { .Port = DIO_15_PORT, .BitPos = PORTS_BIT_POS_1, .EnPort = DIO_EN_15_PORT, .EnBitPos = PORTS_BIT_POS_12, .IsInputCapable = false, .HasPwm = false, .PwmIndex = 0xFF, .PpsOutput = (PORTS_REMAP_OUTPUT_PIN)0xFF }, \
    }

// =============================================================================
// Common PowerConfig, UIConfig (NQ1 LED patterns), StreamingConfig
// =============================================================================

#define COMMON_POWER_CONFIG { \
    .EN_Vref_Ch = PWR_VREF_EN_PORT, .EN_Vref_Bit = PORTS_BIT_POS_15, \
    .EN_3_3V_Ch = PWR_3_3V_EN_PORT, .EN_3_3V_Bit = PORTS_BIT_POS_12, \
    .EN_5_10V_Ch = PWR_5V_EN_PORT, .EN_5_10V_Bit = PORTS_BIT_POS_0, \
    .EN_12V_Ch = PWR_12V_EN_PORT, .EN_12V_Bit = PORTS_BIT_POS_15, \
    .USB_Dp_Ch = USB_DP_MON_PORT, .USB_Dp_Bit = PORTS_BIT_POS_9, \
    .USB_Dn_Ch = USB_DN_MON_PORT, .USB_Dn_Bit = PORTS_BIT_POS_10, \
    .BQ24297Config = { \
        .INT_Ch = BATT_MAN_INT_PORT, .INT_Bit = PORTS_BIT_POS_4, \
        .OTG_Ch = BATT_MAN_OTG_PORT, .OTG_Bit = PORTS_BIT_POS_5, \
        .STAT_Ch = BATT_MAN_STAT_PORT, .STAT_Bit = PORTS_BIT_POS_11, \
        .I2C_Index = DRV_I2C_INDEX_0, .I2C_Address = 0xD6>>1, \
    }, \
}

#define COMMON_UI_CONFIG { \
    .LED1_Pin = LED_WHITE_PIN, .LED2_Pin = LED_BLUE_PIN, .button_Pin = BUTTON_PIN, \
    .LED1_Ind = { \
        .patterns = {{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,1,1,0,0,1,1},{0,0,0,0,0,0,0,0},{1,1,1,1,1,1,1,1},{0,1,1,1,1,1,1,1},{1,1,1,1,1,1,1,1},{0,1,1,1,1,1,1,1},{1,0,0,0,0,0,0,0},{1,0,0,0,0,0,0,0},{1,0,1,0,0,0,0,0},{1,0,1,0,0,0,0,0}}, \
        .period = {2,0,2,2,2,2,2,2,2,2,2,2}, \
    }, \
    .LED2_Ind = { \
        .patterns = {{0,0,0,0,0,0,0,0},{1,0,1,0,1,0,1,0},{1,1,0,0,1,1,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{1,0,0,0,0,0,0,0},{1,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{1,0,0,0,0,0,0,0},{0,0,0,0,0,0,0,0},{1,0,0,0,0,0,0,0}}, \
        .period = {2,1,2,2,2,2,2,2,2,2,2,2}, \
    }, \
}

#define COMMON_STREAMING_CONFIG {.TimerIndex = TMR_INDEX_4, .TSTimerIndex = TMR_INDEX_6}

#ifdef __cplusplus
}
#endif
