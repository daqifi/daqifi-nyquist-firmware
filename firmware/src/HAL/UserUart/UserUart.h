/**
 * @file UserUart.h
 * @brief User UART on the DIO terminal (#16, part of epic #664).
 *
 * Exposes one of the otherwise-unused UART peripherals (U1/U2/U3/U5/U6 — U4 is
 * the debug UART) as a general-purpose async serial port on the 16-pin user
 * DIO terminal so digital sensors / MCUs / SBCs can be driven with
 * `SYST:COMM:UART:*`. The firmware auto-selects the hardware UxART from the
 * requested RX/TX DIO pair via the PPS capability table, so the SCPI surface
 * never names a silicon instance.
 *
 * Pin model (verified against the DFP PPS tables and DIO.SchDoc):
 *   - TX = UxTX, PPS output (RP<pin>R = the UART's TX function code).
 *   - RX = UxRX, PPS input  (UxRXR = the pin's RPn input-select code).
 *   - Either direction may be omitted (-1) for a write-only / read-only port.
 *
 *   Reachable pairs (RX group -> TX group):
 *     U1: RX {5,7,15}   TX {2,4,6,14}     U5: RX {5,7,15}   TX {2,4,6,14}
 *     U3: RX {2,4,6,14} TX {5,7,15}       U2: RX {3,12}     TX {0,11}
 *     U6: RX {0,11}     TX {3,12}
 *
 * Electrical: TX swings at the +5V_D buffer rail (mind 3.3 V-only devices).
 * RX returns through the terminal's 100K series read resistor (~1-2 us RC), so
 * clean reception is bounded to roughly 100-250 kbit/s regardless of the
 * requested baud — 115200 is the validated default; characterize upward before
 * promising more. (The 921600 debug UART is on a direct PIC pin, not behind the
 * terminal read path — don't transfer that expectation.)
 *
 * RX (Phase 1) is polled: UserUart_Read drains the hardware RX FIFO on demand
 * and clears/counts an OERR overrun (erratum #8). The FIFO is a few bytes deep,
 * so a client must read promptly for bursts longer than the FIFO — an
 * interrupt-fed BSS ring is a tracked follow-up (needs a vector-table entry).
 * TX is polled/blocking-with-timeout. The pins are claimed through the DIO
 * ownership registry (DIO_ClaimChannel), so streaming and DIO:PORt won't stomp
 * the bus and PWM on an overlapping channel is refused. This driver ships the
 * mechanism; the client picks the framing.
 */
#ifndef USER_UART_H
#define USER_UART_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Sentinel for an unused RX/TX line. */
#define USER_UART_PIN_NONE     0xFFu

/** Default baud (RC-safe for the terminal RX read path). */
#define USER_UART_DEFAULT_BAUD_HZ   115200u

/** Requested-baud bounds. Silicon TX max is multi-Mbaud; the practical floor is
 *  PBCLK2/(16*65536) (16-bit BRG, BRGH=0). The client owns staying within the
 *  ~100-250 kbit/s terminal RX RC limit when an RX pin is used. */
#define USER_UART_MIN_BAUD_HZ    300u
#define USER_UART_MAX_BAUD_HZ    3000000u

/** RX ring capacity (bytes). Power-of-two so the index math is a mask. */
#define USER_UART_RX_RING_SIZE   1024u

/** Parity encodings (match the SCPI CONFig arg). */
typedef enum {
    USER_UART_PARITY_NONE = 0,
    USER_UART_PARITY_EVEN = 1,
    USER_UART_PARITY_ODD  = 2,
} UserUartParity_t;

/** Immutable-per-session UART configuration. */
typedef struct {
    uint8_t  rxDio;     /**< RX DIO channel, or USER_UART_PIN_NONE */
    uint8_t  txDio;     /**< TX DIO channel, or USER_UART_PIN_NONE */
    uint32_t baudHz;    /**< requested baud */
    uint8_t  dataBits;  /**< 7 or 8 */
    uint8_t  parity;    /**< UserUartParity_t */
    uint8_t  stopBits;  /**< 1 or 2 */
    bool     rxInv;     /**< invert RX idle polarity (RXINV) */
    bool     txInv;     /**< invert TX idle polarity (UTXINV) */
} UserUartConfig_t;

/**
 * Validate and store a configuration. Does NOT touch hardware — apply it with
 * UserUart_Enable. Rejected (returns false, reason in @p err if non-NULL) if a
 * pin is out of range, the RX/TX pair maps to no hardware UART, pins collide,
 * data/parity/stop are invalid, or the baud is out of range.
 * @param cfg configuration to validate/store (copied internally).
 * @param err optional out: static reason string on failure.
 */
bool UserUart_Configure(const UserUartConfig_t* cfg, const char** err);

/** Copy the current stored configuration. @return false if none set yet. */
bool UserUart_GetConfig(UserUartConfig_t* out);

/**
 * Apply the stored configuration: claim the pins, PPS-map UxRX/UxTX, set the
 * buffer directions (TX pre-driven to its idle level before the buffer is
 * enabled, so no start-bit glitch), power the UART, and start the RX ISR.
 * Fails (returns false, reason in @p err) if no config is set or a pin is
 * already owned.
 */
bool UserUart_Enable(const char** err);

/** Stop the UART, unmap PPS, release the pins, and restore their DIO state. */
bool UserUart_Disable(void);

/** True while the UART is enabled and owns its pins. */
bool UserUart_IsEnabled(void);

/**
 * Change the RX/TX inversion at runtime (takes effect immediately if enabled,
 * else stored for the next Enable). @return false if not configured.
 */
bool UserUart_SetInvert(bool rxInv, bool txInv);

/**
 * Transmit @p len bytes, blocking with a per-byte timeout.
 * @return false if not enabled, TX not configured, or a byte timed out.
 */
bool UserUart_Write(const uint8_t* data, uint16_t len);

/**
 * Copy up to @p maxLen bytes from the RX ring into @p out.
 * @return the number of bytes copied (0 if none pending / not enabled).
 */
uint16_t UserUart_Read(uint8_t* out, uint16_t maxLen);

/** Bytes currently pending in the RX ring. */
uint16_t UserUart_RxPending(void);

/** RX overflow events since Enable (ring-full drops + erratum-#8 OERR). */
uint32_t UserUart_RxOverflowCount(void);

/** Actual baud after BRG rounding (0 if not enabled). */
uint32_t UserUart_GetActualBaud(void);

#ifdef __cplusplus
}
#endif

#endif /* USER_UART_H */
