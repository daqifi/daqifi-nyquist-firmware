/**
 * @file SPIConfigEnums.h
 * @brief SPI configuration enums for DAQiFi HAL abstraction layer
 *
 * Provides symbolic constants for SPI peripheral configuration.
 * Uses Harmony v3 driver enums where applicable, with custom enums
 * for HAL-specific fields not covered by Harmony.
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
// SPI Data Width (custom - number of bits per transfer)
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

// =============================================================================
// Use Harmony v3 enums directly (already defined in drv_spi_definitions.h)
// =============================================================================

// Clock Polarity: DRV_SPI_CLOCK_POLARITY_IDLE_LOW (0), DRV_SPI_CLOCK_POLARITY_IDLE_HIGH (1)
// Input Sampling Phase: DRV_SPI_CLOCK_PHASE_VALID_TRAILING_EDGE (0), DRV_SPI_CLOCK_PHASE_VALID_LEADING_EDGE (1)

#ifdef __cplusplus
}
#endif

#endif // SPI_CONFIG_ENUMS_H
