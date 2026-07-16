/**
 * @file UserI2c.h
 * @brief User I2C master on the DIO terminal (#15, part of epic #664).
 *
 * Exposes the PIC's **I2C2** peripheral (SDA2 = RA3, SCL2 = RA2 -- dedicated
 * I2C pins, no PPS) as a general-purpose master through the on-board
 * **PCA9516A 5-channel I2C hub** (U27), so digital sensors on the 16-pin user
 * DIO terminal can be driven with `SYST:COMM:I2C:*`.
 *
 * Hardware topology (verified against Nyquist I v2.0.0: I2C.SchDoc + top sheet):
 *   - PIC I2C2 (RA2/RA3, 910 R pull-ups on the upstream segment) -> PCA9516A hub.
 *   - **Segment 1**: SCL -> terminal pin 10 (fw DIO10/RE4), SDA -> terminal
 *     pin 11 (fw DIO11/RC2). Hub enable **EN1 = RH14**.
 *   - **Segment 2**: SCL -> terminal pin 8 (fw DIO8/RJ6), SDA -> terminal
 *     pin 9 (fw DIO9/RE1). Hub enable **EN2 = RA14**.
 *   - The PCA9516A is a hub/repeater (NOT an isolating mux): every enabled
 *     segment + the upstream form ONE logical bus / one address space. The
 *     enables only isolate pin pairs (capacitance, hot-plug, hung-segment
 *     recovery).
 *
 * Buffer model: the segment nets tie to the terminal (DIOHV) side of the DIO
 * buffers, so an enabled segment claims its two DIO channels through the
 * ownership registry (DIO_OWNER_I2C) and sets them to peripheral-INPUT
 * (DIO_SetChannelPeripheralInput -> buffer OE off, terminal high-Z) so the
 * open-drain hub owns the terminal pin. The PIC pins still observe the segment
 * through their 100 K series read resistors (harmless; handy for debug).
 * DIO:PORt / PWM on a claimed channel is rejected and names I2C as the owner.
 *
 * Phase 1 transfers are polled/blocking-with-timeout, run from SCPI (task)
 * context, and are bounded so a stuck bus can't hang the task -- a stuck-SDA
 * recovery (9 SCL pulses + STOP) is wired into the ENAble off/on cycle. This
 * driver ships the mechanism; the client owns the sensor's register protocol.
 *
 * Errata honored: #37 (SCL tLOW out of I2C spec at >=400 kHz, no workaround --
 * frequencies >=400 kHz are rejected); #6 (A1/A3 I2C misbehaves >100 kHz or
 * >500 B continuous, SW-recoverable -- default 100 kHz, keep transfers modest).
 */
#ifndef USER_I2C_H
#define USER_I2C_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Default bus frequency (Hz). */
#define USER_I2C_DEFAULT_FREQ_HZ   100000u

/** Requested-frequency bounds. Upper bound is EXCLUSIVE of 400 kHz: erratum
 *  #37 makes SCL tLOW out of spec at >=400 kHz with no workaround, so a request
 *  of 400 kHz or more is rejected. The floor keeps BRG in range. */
#define USER_I2C_MIN_FREQ_HZ        10000u
#define USER_I2C_MAX_FREQ_HZ       400000u   /* rejected AT and above this */

/** Number of hub segments (PCA9516A stuffed channels 1 and 2). */
#define USER_I2C_SEGMENT_COUNT          2u

/** Largest 7-bit address sweep result the scanner can report. */
#define USER_I2C_MAX_SCAN_DEVICES     112u    /* 0x08..0x77 general-call/reserved-trimmed */

/**
 * Initialize I2C2 (BRG for the current frequency, module ON), claim the DIO
 * channels of every currently-enabled segment (peripheral-input), and raise
 * those segment enables. Idempotent. Fails (returns false, reason in @p err if
 * non-NULL) if a required DIO channel is already owned by another peripheral.
 */
bool UserI2c_Enable(const char** err);

/** Lower all segment enables, release + restore the claimed DIO channels, and
 *  turn the I2C2 module off. Idempotent. */
bool UserI2c_Disable(void);

/** True while the I2C master is enabled. */
bool UserI2c_IsEnabled(void);

/**
 * Enable/disable one PCA9516A segment (1 or 2). When enabling while the master
 * is on, claims that segment's DIO pair (peripheral-input) and raises its hub
 * enable; when disabling, lowers the enable and releases+restores the pair.
 * While the master is off, only the desired state is recorded (applied at
 * Enable). @return false (reason in @p err) on a bad segment or a pin already
 * owned by another peripheral.
 */
bool UserI2c_SetSegment(uint8_t segment, bool on, const char** err);

/** Query a segment's enabled state. @return false if @p segment is invalid. */
bool UserI2c_GetSegment(uint8_t segment, bool* on);

/**
 * Set the bus frequency (Hz). Rejected (returns false, reason in @p err) if
 * out of [MIN, MAX) -- in particular >=400 kHz (erratum #37). The BRG is rounded
 * so the ACTUAL frequency never exceeds the request. Takes effect immediately
 * if enabled, else stored for the next Enable.
 */
bool UserI2c_SetFrequency(uint32_t hz, const char** err);

/** Requested bus frequency (Hz). */
uint32_t UserI2c_GetFrequency(void);

/** Actual bus frequency after BRG rounding (Hz), or 0 if never computed. */
uint32_t UserI2c_GetActualFrequency(void);

/**
 * 7-bit address sweep (0x08..0x77). Writes each address (R/W=0) and records the
 * ones that ACK into @p addrs (up to @p maxAddrs). @return the number found, or
 * 0 if not enabled. Diagnostic only -- issue this off the streaming path.
 */
uint8_t UserI2c_Scan(uint8_t* addrs, uint8_t maxAddrs);

/**
 * Combined transfer to a 7-bit @p addr7: write @p wlen bytes, then (if
 * @p rlen > 0) a repeated-START read of @p rlen bytes into @p rdata.
 *   - wlen==0 -> pure read;  rlen==0 -> pure write.
 * @return false (reason in @p err) if not enabled, a param is invalid, the
 * address is not ACKed, or the bus times out. On a bus timeout the master
 * issues a STOP and flags the transfer failed.
 */
bool UserI2c_Transfer(uint8_t addr7, const uint8_t* wdata, uint16_t wlen,
                      uint8_t* rdata, uint16_t rlen, const char** err);

/**
 * Phase-0 boot hygiene: drive both PCA9516A hub enables (RH14 = EN1,
 * RA14 = EN2) LOW as outputs so the segments are isolated from boot. Harmony's
 * GPIO_Initialize leaves both floating. Safe to call before the scheduler;
 * independent of the I2C master. Call once from board/system init.
 */
void UserI2c_InitEnablesLow(void);

#ifdef __cplusplus
}
#endif

#endif /* USER_I2C_H */
