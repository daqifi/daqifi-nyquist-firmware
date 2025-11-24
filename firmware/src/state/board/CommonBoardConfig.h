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
 * DIO channels configuration - identical across all board variants.
 * 16 channels with same port mappings, bit positions, and PPS assignments.
 *
 * NOTE: DIO_0 has conditional compilation for DIO_TIMING_TEST mode.
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

#ifdef __cplusplus
}
#endif
