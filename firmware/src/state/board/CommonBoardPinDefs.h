#pragma once

#include "../../config/default/peripheral/gpio/plib_gpio.h"
#include "../../config/default/peripheral/gpio/pin_definitions.h"

/**
 * @file CommonBoardPinDefs.h
 * @brief Common pin and port definitions shared across all Nyquist board variants (NQ1, NQ2, NQ3)
 *
 * This file contains GPIO port and pin definitions that are identical across all board variants.
 * Extracting these to a common file reduces duplication and ensures consistency.
 */

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// DIO Port Definitions (Identical across all variants)
// =============================================================================

#define DIO_0_PORT GPIO_PORT_D
#define DIO_1_PORT GPIO_PORT_J
#define DIO_2_PORT GPIO_PORT_D
#define DIO_3_PORT GPIO_PORT_D
#define DIO_4_PORT GPIO_PORT_F
#define DIO_5_PORT GPIO_PORT_F
#define DIO_6_PORT GPIO_PORT_G
#define DIO_7_PORT GPIO_PORT_G
#define DIO_8_PORT GPIO_PORT_J
#define DIO_9_PORT GPIO_PORT_E
#define DIO_10_PORT GPIO_PORT_E
#define DIO_11_PORT GPIO_PORT_C
#define DIO_12_PORT GPIO_PORT_E
#define DIO_13_PORT GPIO_PORT_E
#define DIO_14_PORT GPIO_PORT_E
#define DIO_15_PORT GPIO_PORT_C

#define DIO_EN_0_PORT GPIO_PORT_D
#define DIO_EN_1_PORT GPIO_PORT_J
#define DIO_EN_2_PORT GPIO_PORT_D
#define DIO_EN_3_PORT GPIO_PORT_J
#define DIO_EN_4_PORT GPIO_PORT_D
#define DIO_EN_5_PORT GPIO_PORT_K
#define DIO_EN_6_PORT GPIO_PORT_J
#define DIO_EN_7_PORT GPIO_PORT_J
#define DIO_EN_8_PORT GPIO_PORT_J
#define DIO_EN_9_PORT GPIO_PORT_E
#define DIO_EN_10_PORT GPIO_PORT_G
#define DIO_EN_11_PORT GPIO_PORT_J
#define DIO_EN_12_PORT GPIO_PORT_E
#define DIO_EN_13_PORT GPIO_PORT_E
#define DIO_EN_14_PORT GPIO_PORT_A
#define DIO_EN_15_PORT GPIO_PORT_J

// =============================================================================
// Power Management Port Definitions (Identical across all variants)
// =============================================================================

#define PWR_3_3V_EN_PORT GPIO_PORT_H
#define PWR_VREF_EN_PORT GPIO_PORT_J
#define PWR_5V_EN_PORT GPIO_PORT_D
#define PWR_12V_EN_PORT GPIO_PORT_H
#define USB_DP_MON_PORT GPIO_PORT_H
#define USB_DN_MON_PORT GPIO_PORT_H
#define BATT_MAN_INT_PORT GPIO_PORT_A
#define BATT_MAN_OTG_PORT GPIO_PORT_K
#define BATT_MAN_STAT_PORT GPIO_PORT_H
#define LED_WHITE_PORT GPIO_PORT_C
#define LED_BLUE_PORT GPIO_PORT_B
#define BUTTON_PORT GPIO_PORT_J

// =============================================================================
// PPS Output Pin Remapping Enum (Identical across all variants)
// =============================================================================

typedef enum {
    OUTPUT_PIN_RPA14 = 0,
    OUTPUT_PIN_RPA15 = 1,
    OUTPUT_PIN_RPB0 = 2,
    OUTPUT_PIN_RPB1 = 3,
    OUTPUT_PIN_RPB2 = 4,
    OUTPUT_PIN_RPB3 = 5,
    OUTPUT_PIN_RPB5 = 7,
    OUTPUT_PIN_RPB6 = 8,
    OUTPUT_PIN_RPB7 = 9,
    OUTPUT_PIN_RPB8 = 10,
    OUTPUT_PIN_RPB9 = 11,
    OUTPUT_PIN_RPB10 = 12,
    OUTPUT_PIN_RPB14 = 16,
    OUTPUT_PIN_RPB15 = 17,
    OUTPUT_PIN_RPC1 = 19,
    OUTPUT_PIN_RPC2 = 20,
    OUTPUT_PIN_RPC3 = 21,
    OUTPUT_PIN_RPC4 = 22,
    OUTPUT_PIN_RPC13 = 31,
    OUTPUT_PIN_RPC14 = 32,
    OUTPUT_PIN_RPD0 = 34,
    OUTPUT_PIN_RPD1 = 35,
    OUTPUT_PIN_RPD2 = 36,
    OUTPUT_PIN_RPD3 = 37,
    OUTPUT_PIN_RPD4 = 38,
    OUTPUT_PIN_RPD5 = 39,
    OUTPUT_PIN_RPD6 = 40,
    OUTPUT_PIN_RPD7 = 41,
    OUTPUT_PIN_RPD9 = 43,
    OUTPUT_PIN_RPD10 = 44,
    OUTPUT_PIN_RPD11 = 45,
    OUTPUT_PIN_RPD12 = 46,
    OUTPUT_PIN_RPD14 = 48,
    OUTPUT_PIN_RPD15 = 49,
    OUTPUT_PIN_RPE3 = 53,
    OUTPUT_PIN_RPE5 = 55,
    OUTPUT_PIN_RPE8 = 58,
    OUTPUT_PIN_RPE9 = 59,
    OUTPUT_PIN_RPF0 = 66,
    OUTPUT_PIN_RPF1 = 67,
    OUTPUT_PIN_RPF2 = 68,
    OUTPUT_PIN_RPF3 = 69,
    OUTPUT_PIN_RPF4 = 70,
    OUTPUT_PIN_RPF5 = 71,
    OUTPUT_PIN_RPF8 = 74,
    OUTPUT_PIN_RPF12 = 78,
    OUTPUT_PIN_RPF13 = 79,
    OUTPUT_PIN_RPG0 = 82,
    OUTPUT_PIN_RPG1 = 83,
    OUTPUT_PIN_RPG6 = 88,
    OUTPUT_PIN_RPG7 = 89,
    OUTPUT_PIN_RPG8 = 90,
    OUTPUT_PIN_RPG9 = 91
} PORTS_REMAP_OUTPUT_PIN;

#ifdef __cplusplus
}
#endif
