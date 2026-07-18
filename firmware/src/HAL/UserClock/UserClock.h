/**
 * @file UserClock.h
 * @brief Programmable clock outputs (REFCLKO) on the DIO terminal (#668, epic #664).
 *
 * Exposes the PIC's three PPS-reachable reference-clock outputs on user DIO pins
 * so a sensor board / downstream MCU can be fed a programmable reference, or so a
 * clock-out pin can loop into an input-capture pin as a self-validating timing rig.
 *
 * Unit -> pin-group map (verified DFP PPS output codes):
 *   - REFCLKO4 (RPnR=13): DIO 5, 7, 15        (group 1)
 *   - REFCLKO1 (RPnR=15): DIO 2, 4, 6, 14     (group 2)
 *   - REFCLKO3 (RPnR=15): DIO 3, 12           (group 3)
 *   DIO 0/11 (group 4) have no REFCLKO -- not clock-capable (use PWM there).
 *
 * Source: POSC (24 MHz crystal). Erratum #1 (DS80000663) bars dividing inputs
 * >100 MHz, so SYSCLK/SPLL (252 MHz) are illegal sources -- we only ever use POSC
 * (24 MHz, compliant), so the erratum can't be tripped. Divider: 15-bit RODIV +
 * 9-bit ROTRIM fractional (x/512): Fout = Fsrc / (2 x (RODIV + ROTRIM/512)), or
 * Fsrc passthrough at RODIV=0. Achievable set: Fsrc (24 MHz, passthrough) and the
 * divided range ~366 Hz .. Fsrc/2 (12 MHz); the (12 MHz, 24 MHz) span is NOT
 * reachable (smallest non-passthrough divisor is /2) and requests there clamp to
 * 12 MHz. The fractional divider quantizes elsewhere too, so the ACHIEVED Hz
 * (always reported) differs from the request. Below ~366 Hz belongs to PWM. Output
 * swings at the +5V_D buffer rail; usable bandwidth through the terminal buffer +
 * RC is a characterization item.
 *
 * Pins are claimed through the DIO ownership registry (DIO_OWNER_CLOCK); DIO:PORt
 * / PWM on a claimed pin is rejected and names the owner.
 */
#ifndef USER_CLOCK_H
#define USER_CLOCK_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** REFCLKO source: POSC (24 MHz). */
#define USER_CLOCK_SRC_HZ    24000000u

/** Achievable output bounds. Floor = Fsrc/(2*RODIVmax) = 24 MHz/(2*32767) ~= 366 Hz.
 *  Ceiling = Fsrc passthrough; the terminal buffer limits usable bandwidth well
 *  below this (characterize) -- the achieved Hz is always reported. */
#define USER_CLOCK_MIN_HZ         366u
#define USER_CLOCK_MAX_HZ    24000000u

/**
 * Validate and store a clock config for @p dio: pick the REFCLKO unit for the
 * pin's group, compute RODIV/ROTRIM for @p hz, and return the achieved frequency
 * in @p actualHz. Does NOT touch hardware -- apply with UserClock_Enable.
 * @return false (reason in @p err if non-NULL) if the pin is not clock-capable or
 *         @p hz is out of [MIN, MAX].
 */
bool UserClock_Configure(uint8_t dio, uint32_t hz, uint32_t* actualHz, const char** err);

/**
 * Apply/tear down the clock on @p dio: claim the pin, drive it as the REFCLKO
 * output (PPS + buffer), start/stop the reference oscillator. @return false
 * (reason in @p err) if no config is set for the pin, the pin is already owned,
 * or the reference output failed to start.
 */
bool UserClock_Enable(uint8_t dio, bool on, const char** err);

/** Achieved output frequency on @p dio (Hz), or 0 if not enabled (0 doubles as the
 *  "off" indicator, so no separate IsEnabled query is needed). */
uint32_t UserClock_GetActualHz(uint8_t dio);

#ifdef __cplusplus
}
#endif

#endif /* USER_CLOCK_H */
