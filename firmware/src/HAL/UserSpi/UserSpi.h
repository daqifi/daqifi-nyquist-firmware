/**
 * @file UserSpi.h
 * @brief User SPI1 master on the DIO terminal (#665, part of epic #664).
 *
 * Exposes the otherwise-unused SPI1 peripheral as a general-purpose master
 * on the 16-pin user DIO terminal so digital sensors / MCUs / SBCs can be
 * driven with `SYST:COMM:SPI:*`.
 *
 * Pin model (verified against the DFP PPS tables and DIO.SchDoc):
 *   - SCK  = DIO0 (RD1) — SCK1 is a fixed/dedicated SPI1 function, not PPS
 *            remappable. Enabling SPI1 master drives it; the competing
 *            RPD1R output mux (PWM ch0 / OC1) is cleared so only SCK1 drives.
 *   - MOSI = SDO1, PPS output — one of DIO {2,4,5,6,7,14,15} (RP<pin>R = 5).
 *   - MISO = SDI1, PPS input (group 1 only) — one of DIO {5,7,15}
 *            (SDI1R = the pin's RPn code).
 *   - CS   = any free DIO driven as a software GPIO (active low), or none.
 *
 * Electrical: outputs (SCK/MOSI/CS) swing at the +5V_D buffer rail (mind
 * 3.3 V-only slaves). MISO returns through the terminal's 100K series read
 * resistor (~1-2 us RC), so full-duplex reads are bounded to roughly
 * 100-500 kHz SCK regardless of the requested baud — write-only devices can
 * clock faster. This driver ships the mechanism; the client picks the rate.
 *
 * Transfers are polled and bounded (no ISR) — sufficient for the
 * command/response sensors this targets. The pins are claimed through the
 * DIO ownership registry (DIO_ClaimChannel), so streaming and DIO:PORt
 * won't stomp the bus and PWM on an overlapping channel is refused.
 */
#ifndef USER_SPI_H
#define USER_SPI_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** SCK is always DIO0 (fixed SCK1). */
#define USER_SPI_SCK_DIO    0u
/** Sentinel for an unused MOSI/MISO/CS data line. */
#define USER_SPI_PIN_NONE   0xFFu

/** Default SCK when a config omits a baud (RC-safe for MISO reads). */
#define USER_SPI_DEFAULT_BAUD_HZ   100000u

/** Requested-baud bounds. Silicon max is 42 MHz (BRG=0); the practical
 *  floor is PBCLK2/(2*8192). The client is responsible for staying within
 *  the MISO RC limit when a MISO pin is used. */
#define USER_SPI_MIN_BAUD_HZ   6000u
#define USER_SPI_MAX_BAUD_HZ   42000000u

/** Immutable-per-session SPI configuration. */
typedef struct {
    uint8_t  mosiDio;   /**< MOSI DIO channel, or USER_SPI_PIN_NONE */
    uint8_t  misoDio;   /**< MISO DIO channel, or USER_SPI_PIN_NONE */
    uint8_t  csDio;     /**< active-low CS DIO channel, or USER_SPI_PIN_NONE */
    uint32_t baudHz;    /**< requested SCK frequency */
    uint8_t  mode;      /**< SPI mode 0..3 (CPOL/CPHA) */
    bool     lsbFirst;  /**< true = LSB-first (software bit-reversed) */
} UserSpiConfig_t;

/**
 * Validate and store a configuration. Does NOT touch hardware — apply it
 * with UserSpi_Enable. Rejected (returns false, with a reason in @p err if
 * non-NULL) if a pin is out of range, MOSI/MISO is not PPS-capable for that
 * role, pins collide, the mode is >3, or the baud is out of range.
 * @param cfg configuration to validate/store (copied internally).
 * @param err optional out: static reason string on failure.
 */
bool UserSpi_Configure(const UserSpiConfig_t* cfg, const char** err);

/** Copy the current stored configuration. @return false if none set yet. */
bool UserSpi_GetConfig(UserSpiConfig_t* out);

/**
 * Apply the stored configuration: claim the pins, PPS-map SDO1/SDI1, clear
 * the SCK output-mux contention, set the buffer directions, and start SPI1.
 * Fails (returns false, reason in @p err) if no config is set, a pin is
 * already owned/probe-claimed, or PWM is active on a needed channel.
 */
bool UserSpi_Enable(const char** err);

/** Stop SPI1, unmap PPS, release the pins, and restore their DIO state. */
bool UserSpi_Disable(void);

/** True while SPI1 is enabled and owns its pins. */
bool UserSpi_IsEnabled(void);

/**
 * Full-duplex transfer of @p len bytes. CS (if configured) is asserted for
 * the whole frame. @p rx may be NULL (write-only) or alias @p tx (in place);
 * @p tx may be NULL (read-only — 0xFF clocked out). MISO with no slave reads
 * 0x00 (terminal pulled down through 180K).
 * @return false on a per-byte hardware timeout or if not enabled.
 */
bool UserSpi_Transfer(const uint8_t* tx, uint8_t* rx, uint16_t len);

/** Actual SCK frequency after BRG rounding (0 if not enabled). */
uint32_t UserSpi_GetActualBaud(void);

#ifdef __cplusplus
}
#endif

#endif /* USER_SPI_H */
