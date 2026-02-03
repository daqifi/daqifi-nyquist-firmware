/**
 * @file SPIConfigEnums.h
 * @brief SPI configuration enums for DAQiFi HAL abstraction layer
 *
 * Provides symbolic constants for SPI peripheral configuration.
 * All constants use HAL_ prefix for consistency. Where Harmony v3
 * provides equivalent enums, we alias them; otherwise we define custom values.
 */

#ifndef SPI_CONFIG_ENUMS_H
#define SPI_CONFIG_ENUMS_H

#include "config/default/driver/spi/drv_spi_definitions.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// SPI Module ID (custom - maps to SPI peripheral number)
// =============================================================================

typedef enum {
    HAL_SPI_ID_1 = 1,
    HAL_SPI_ID_2 = 2,
    HAL_SPI_ID_3 = 3,
    HAL_SPI_ID_4 = 4,
    HAL_SPI_ID_5 = 5,
    HAL_SPI_ID_6 = 6,
} HAL_SPI_MODULE_ID;

// =============================================================================
// SPI Clock Source (custom - maps to baud rate clock selection)
// =============================================================================

typedef enum {
    HAL_SPI_CLOCK_PBCLK = 1,  // Peripheral bus clock
} HAL_SPI_CLOCK_SOURCE;

// =============================================================================
// Peripheral Bus Clock ID (custom - which peripheral bus)
// =============================================================================

typedef enum {
    HAL_CLK_BUS_PERIPHERAL_1 = 1,
    HAL_CLK_BUS_PERIPHERAL_2 = 2,
    HAL_CLK_BUS_PERIPHERAL_3 = 3,
    HAL_CLK_BUS_PERIPHERAL_4 = 4,
    HAL_CLK_BUS_PERIPHERAL_5 = 5,
} HAL_CLK_BUS_ID;

// =============================================================================
// SPI Clock Polarity (aliased from Harmony v3 for consistency)
// =============================================================================

#define HAL_SPI_CLOCK_POLARITY_IDLE_LOW   DRV_SPI_CLOCK_POLARITY_IDLE_LOW   // = 0
#define HAL_SPI_CLOCK_POLARITY_IDLE_HIGH  DRV_SPI_CLOCK_POLARITY_IDLE_HIGH  // = 1

// =============================================================================
// SPI Clock Phase / Input Sampling (aliased from Harmony v3 for consistency)
// =============================================================================

#define HAL_SPI_CLOCK_PHASE_TRAILING_EDGE  DRV_SPI_CLOCK_PHASE_VALID_TRAILING_EDGE  // = 0
#define HAL_SPI_CLOCK_PHASE_LEADING_EDGE   DRV_SPI_CLOCK_PHASE_VALID_LEADING_EDGE   // = 1

// =============================================================================
// SPI Data Width (custom - number of bits per transfer)
// Note: Our HAL uses actual bit count (8, 16, 32), not Harmony's index values
// =============================================================================

typedef enum {
    HAL_SPI_WIDTH_8BITS  = 8,
    HAL_SPI_WIDTH_16BITS = 16,
    HAL_SPI_WIDTH_32BITS = 32,
} HAL_SPI_DATA_WIDTH;

// =============================================================================
// SPI Output Data Phase (custom - when data changes relative to clock)
// =============================================================================

typedef enum {
    HAL_SPI_OUTPUT_PHASE_IDLE_TO_ACTIVE = 0,  // Data changes on idle-to-active clock transition
    HAL_SPI_OUTPUT_PHASE_ACTIVE_TO_IDLE = 1,  // Data changes on active-to-idle clock transition
} HAL_SPI_OUTPUT_PHASE;

#ifdef __cplusplus
}
#endif

#endif // SPI_CONFIG_ENUMS_H
