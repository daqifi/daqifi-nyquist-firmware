/**
 * @file AdcThreshold.h
 * @brief Hardware analog threshold alarms via the ADCHS digital comparators
 *        (#670, epic #664).
 *
 * The PIC32MZ ADCHS block has 6 digital comparators (ADCCMPCON1-6). Each watches
 * the 12-bit conversion RESULT of one selected AN input in hardware and raises an
 * interrupt the moment a result crosses a programmed limit, on both Type 1
 * (dedicated) and Type 2 (MODULE7-scanned) channels. A trip latches a per-channel
 * flag + counter, sets the QUES "analog limit" status bit, and emits a one-shot
 * log; the client reads it back with CONF:ADC:THREshold?.
 *
 * The comparator is LEVEL-evaluated, so a signal parked past the limit would set
 * the event on every conversion. The trip ISR therefore latch-and-masks: it fires
 * ONCE, disables its own interrupt, and is re-armed by AdcThreshold_Clear (or a
 * reconfigure). Cost is one ISR per Clear-cycle, not per conversion -- so it adds
 * no per-sample CPU in steady state even while streaming.
 *
 * Limits are RAW ADC codes (0..4095): the ISR path stays float-free; volts->codes
 * is the client's job (firmware = mechanism). NQ1/NQ2 (internal MC12bADC) only --
 * NQ3's external AD7609 has no such block, so the command is capability-gated off.
 *
 * Mode -> comparator condition (all set DCMPLO=lo, DCMPHI=hi; only the enabled
 * event bits differ). NOTE: the in-window bit is IEBTWN, NOT IEHILO (which is only
 * "result < DCMPHI") -- see the FRM note in AdcThreshold.c:
 *   BELOW   result <  lo                    (IELOLO)
 *   ABOVE   result >= hi                    (IEHIHI)
 *   INSIDE  lo <= result <  hi              (IEBTWN)
 *   OUTSIDE result <  lo || result >= hi    (IELOLO | IEHIHI)
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
 *   ADC, @p chId is not a valid public channel, @p chId's AN input is >= 32
 *   (comparators reach AN0..31 only), @p mode is out of range, @p lo/@p hi exceed
 *   4095 (or lo>hi for the window modes), or no comparator unit is free (names the
 *   holders). The while-streaming rejection is enforced by the SCPI layer, not
 *   here. below uses only lo; above only hi; only the window modes need lo<=hi.
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
 * Inform (do NOT disable) about active thresholds vs the scan list applied at
 * stream start: a Type 2 threshold whose channel is not in the session scan won't
 * see stream-sample conversions this session, so it emits a one-shot log. The unit
 * is left fully armed so it still fires on idle/MEAS conversions and its config +
 * latch survive the session (persistence contract). Called from the streaming-start
 * path after the session CSS is computed. Type 1 thresholds are unaffected.
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
