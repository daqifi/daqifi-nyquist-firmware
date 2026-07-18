/**
 * @file UserIC.h
 * @brief Hardware frequency / period / pulse-width / duty measurement on the
 *        DIO terminal via the PIC32MZ Input Capture units (#666, epic #664).
 *
 * The PIC32MZ has 9 input-capture units (IC1-9). Each timestamps edges of one
 * PPS-selected RPn pin into a 4-deep hardware FIFO, clocked by Timer2 (the IC
 * timebase; Timer3 is owned by DIO PWM, so IC rides TMR2 — a single 16-bit
 * counter extended to 48 bits by a software epoch bumped in the TMR2 rollover
 * ISR). This gives ~11.9 ns resolution. Usable range ~0.4 Hz to ~100 kHz with
 * the default timeouts; FREQuency reaches lower with a larger gate_ms (up to a
 * 60 s clamp). The upper end is bounded by FIFO overflow (rejected, not faked).
 *
 * Measurements are ONE-SHOT and serialized (one active unit at a time under a
 * mutex): claim the pin + a reachable free IC unit, route PPS, capture over a
 * bounded window, compute, release. No signal within the timeout returns a
 * SCPI error via the caller (never a fake value — project standing rule).
 *
 * IC-reachable DIO pins (PPS input groups): DIO 5/7/15 (IC3/IC7), 2/4/6/14
 * (IC4/IC8), 3/12 (IC2/IC5/IC9), 0/11 (IC1/IC6). DIO 1/8/9/10/13 are NOT
 * IC-reachable and are rejected.
 *
 * Register semantics are FRM/DS-verified (DS60001320H §17 + the device header):
 * single 32-bit ICxCON (no CON1/CON2, no SYNCSEL on this MZ EF part); ICM=0b011
 * captures every rising edge (period/frequency), ICM=0b110 + FEDGE=1 captures
 * every edge rising-first (pulse-width/duty).
 */
#ifndef USER_IC_H
#define USER_IC_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define USER_IC_UNITS          9u        /*!< IC1..IC9 */
#define USER_IC_TIMER_HZ       84000000u /*!< PBCLK3 feeds TMR2 (11.905 ns/count @ 1:1) */

/** Boot-time init: parks all 9 IC units (disabled, capture IRQ off, priority set)
 *  and clears the epoch. Safe to call before Timer2 is running. */
void UserIC_Initialize(void);

/**
 * Measure signal frequency (Hz) on @p dio by averaging rising-edge periods over
 * a bounded window. @p gate_ms bounds the averaging window (0 = default 100 ms,
 * clamped to 60 s); the no-signal timeout scales with the gate, so slow signals
 * need a larger gate_ms (e.g. ~7000 ms reaches ~0.3 Hz).
 * @return false (reason in @p err) if @p dio is not IC-reachable, no IC unit is
 *   free, the pin can't be claimed, or no/too-slow signal within the timeout.
 */
bool UserIC_MeasureFrequency(uint8_t dio, uint32_t gate_ms, double* hz,
                             const char** err);

/** Measure the rising-to-rising period (microseconds) on @p dio.
 *  @return false (reason in @p err) on the same conditions as MeasureFrequency. */
bool UserIC_MeasurePeriod(uint8_t dio, double* us, const char** err);

/**
 * Measure pulse width (microseconds) on @p dio. @p polarity 1 = high-time,
 * 0 = low-time. Uses both-edge capture with GPIO-level parity anchoring.
 * @return false (reason in @p err) as above, or if a full edge triple wasn't
 *   captured in time.
 */
bool UserIC_MeasurePulseWidth(uint8_t dio, uint8_t polarity, double* us,
                              const char** err);

/** Measure duty cycle (percent high, 0..100) on @p dio via both-edge capture.
 *  @return false (reason in @p err) as above. */
bool UserIC_MeasureDuty(uint8_t dio, double* percent, const char** err);

/** Per-unit capture ISR body, called from IC1..IC9_Capture_Handler
 *  (interrupts.c). @p unit is 0..8. Drains the FIFO into the active-measurement
 *  buffer; no cross-module coupling, no float, no blocking. */
void UserIC_IsrCapture(uint8_t unit);

/** Timer2 rollover ISR body — increments the 16->48-bit software epoch. Called
 *  from the TMR2 period-match handler while an IC measurement is active. */
void UserIC_IsrRollover(void);

#ifdef __cplusplus
}
#endif

#endif /* USER_IC_H */
