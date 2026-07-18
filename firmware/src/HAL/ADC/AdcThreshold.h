/**
 * @file AdcThreshold.h
 * @brief Hardware analog threshold alarms via the ADCHS digital comparators
 *        (#670, epic #664).
 *
 * The PIC32MZ ADCHS block has 6 digital comparators (ADCCMPCON1-6). Each watches
 * the 12-bit conversion RESULT of one selected AN input in hardware and raises an
 * interrupt the moment a result crosses a programmed limit -- zero per-sample CPU,
 * works during streaming scans (Type 1 dedicated and Type 2 MODULE7-scanned
 * channels), and fires on the very conversion that trips. A trip latches a
 * per-channel flag + counter, sets the QUES "analog limit" status bit, and emits a
 * one-shot log; the client reads it back with CONF:ADC:THREshold?.
 *
 * Limits are RAW ADC codes (0..4095): the ISR path stays float-free; volts->codes
 * is the client's job (firmware = mechanism). NQ1/NQ2 (internal MC12bADC) only --
 * NQ3's external AD7609 has no such block, so the command is capability-gated off.
 *
 * Mode -> comparator condition (all set DCMPLO=lo, DCMPHI=hi; only the enabled
 * quadrant bits differ):
 *   BELOW   result <  lo              (IELOLO)
 *   ABOVE   result >= hi              (IEHIHI)
 *   INSIDE  lo <= result <  hi        (IEHILO)
 *   OUTSIDE result <  lo || result >= hi   (IELOLO | IEHIHI)
 */
#ifndef ADC_THRESHOLD_H
#define ADC_THRESHOLD_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ADC_THRESHOLD_UNITS      6u      /*!< ADCCMP1..6 */
#define ADC_THRESHOLD_MAX_CODE   4095u   /*!< 12-bit ADC full scale */
#define ADC_THRESHOLD_ALL_CH     0xFFu   /*!< AdcThreshold_Clear: clear every unit */

typedef enum {
    ADC_THRESH_OFF     = 0,  /*!< disabled / release the unit */
    ADC_THRESH_BELOW   = 1,  /*!< trip when result <  lo */
    ADC_THRESH_ABOVE   = 2,  /*!< trip when result >= hi */
    ADC_THRESH_INSIDE  = 3,  /*!< trip when lo <= result <  hi (in window) */
    ADC_THRESH_OUTSIDE = 4,  /*!< trip when result <  lo || result >= hi (out of window) */
} AdcThresholdMode;

/** Boot-time init: clears state and parks all 6 comparator interrupts (disabled,
 *  priority set). Safe to call before the ADC is running. */
void AdcThreshold_Initialize(void);

/**
 * Configure (or, with @p mode == OFF, release) a threshold on user ADC channel
 * @p chId, in raw 12-bit codes. Allocates one of the 6 comparator units to the
 * channel; a second Configure on the same channel updates it in place.
 * @return false (reason in @p err if non-NULL) if the board variant has no MC12b
 *   ADC, @p chId is not a valid public channel, @p mode/@p lo/@p hi are out of
 *   range (need lo<=hi<=4095), no comparator unit is free (names the holders), or
 *   -- while streaming -- @p chId is a Type 2 channel not in the active scan.
 */
bool AdcThreshold_Configure(uint8_t chId, AdcThresholdMode mode,
                            uint16_t lo, uint16_t hi, const char** err);

/**
 * Read back the threshold on @p chId. Fills any non-NULL out-params with the
 * configured mode, limits, trip counter, and latched flag.
 * @return true if @p chId has a threshold configured; false (mode = OFF) if not.
 */
bool AdcThreshold_Query(uint8_t chId, AdcThresholdMode* mode,
                        uint16_t* lo, uint16_t* hi,
                        uint32_t* tripCount, bool* latched);

/** Clear the latch + trip counter for @p chId, or for every unit when
 *  @p chId == ADC_THRESHOLD_ALL_CH. The comparator keeps monitoring. */
void AdcThreshold_Clear(uint8_t chId);

/** True if any configured threshold has latched a trip since its last Clear.
 *  Task context; SCPI_SyncQuesBits derives the QUES analog-limit bit from this. */
bool AdcThreshold_AnyLatched(void);

/**
 * Re-validate active thresholds against the scan list applied at stream start:
 * a Type 2 threshold whose channel is not in the session scan can never fire, so
 * disable its comparator and emit a one-shot log (inform on stale data). Called
 * from the streaming-start path after the session CSS is computed. Type 1
 * (dedicated, continuously converting) thresholds are unaffected.
 */
void AdcThreshold_RevalidateForStream(void);

/** Per-unit trip ISR body, called from ADC_DC1..6_Handler (interrupts.c). @p unit
 *  is 0..5. Minimal + self-contained: clears the event, bumps this unit's counter
 *  and latch (ISR is the sole writer), one-shot logs. No cross-module coupling. */
void AdcThreshold_IsrTrip(uint8_t unit);

#ifdef __cplusplus
}
#endif

#endif /* ADC_THRESHOLD_H */
