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
 * const char* const = immutable pointer to immutable string
 */
extern const char* const COMMON_CSV_CHANNEL_HEADERS_FIRST[16];
extern const char* const COMMON_CSV_CHANNEL_HEADERS_SUBSEQUENT[16];

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
            { DIO_0_PORT, PORTS_BIT_POS_1, DIO_EN_0_PORT, PORTS_BIT_POS_2, false, true, 1, OUTPUT_PIN_RPD1}, \
            { DIO_1_PORT, PORTS_BIT_POS_3, DIO_EN_1_PORT, PORTS_BIT_POS_2, true, false, 0xFF}, \
            { DIO_2_PORT, PORTS_BIT_POS_3, DIO_EN_2_PORT, PORTS_BIT_POS_13, true, false, 0xFF}, \
            { DIO_3_PORT, PORTS_BIT_POS_12, DIO_EN_3_PORT, PORTS_BIT_POS_0, false, true, 8, OUTPUT_PIN_RPD12}, \
            { DIO_4_PORT, PORTS_BIT_POS_0, DIO_EN_4_PORT, PORTS_BIT_POS_7, true, true, 4, OUTPUT_PIN_RPF0}, \
            { DIO_5_PORT, PORTS_BIT_POS_1, DIO_EN_5_PORT, PORTS_BIT_POS_7, false, true, 6, OUTPUT_PIN_RPF1}, \
            { DIO_6_PORT, PORTS_BIT_POS_0, DIO_EN_6_PORT, PORTS_BIT_POS_4, true, true, 7, OUTPUT_PIN_RPG0}, \
            { DIO_7_PORT, PORTS_BIT_POS_1, DIO_EN_7_PORT, PORTS_BIT_POS_5, false, true, 3, OUTPUT_PIN_RPG1}, \
            { DIO_8_PORT, PORTS_BIT_POS_6, DIO_EN_8_PORT, PORTS_BIT_POS_7, false, false, 0xFF}, \
            { DIO_9_PORT, PORTS_BIT_POS_1, DIO_EN_9_PORT, PORTS_BIT_POS_0, true, false, 0xFF}, \
            { DIO_10_PORT, PORTS_BIT_POS_4, DIO_EN_10_PORT, PORTS_BIT_POS_15, false, false, 0xFF}, \
            { DIO_11_PORT, PORTS_BIT_POS_2, DIO_EN_11_PORT, PORTS_BIT_POS_10, true, false, 0xFF}, \
            { DIO_12_PORT, PORTS_BIT_POS_3, DIO_EN_12_PORT, PORTS_BIT_POS_2, true, false, 0xFF}, \
            { DIO_13_PORT, PORTS_BIT_POS_6, DIO_EN_13_PORT, PORTS_BIT_POS_7, false, false, 0xFF}, \
            { DIO_14_PORT, PORTS_BIT_POS_5, DIO_EN_14_PORT, PORTS_BIT_POS_5, true, false, 0xFF}, \
            { DIO_15_PORT, PORTS_BIT_POS_1, DIO_EN_15_PORT, PORTS_BIT_POS_12, false, false, 0xFF}, \
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
