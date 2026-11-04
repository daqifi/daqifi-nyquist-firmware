/* ==========================================================================
 * SampleEmit.h — the one place that decides whether a converted sample may be
 * emitted as a number, and how a double becomes an int32 without UB (#1199)
 *
 * WHY THIS EXISTS. The streaming encoders convert a raw ADC code to a voltage
 * and then print it. Nothing on that path guarded the result against
 * non-finite values, and two different things went wrong:
 *
 *   1. `Json_Encode` printed the voltage with "%.*f" and no finiteness check,
 *      so `inf` or `nan` could go on the wire. Neither is valid JSON -- there
 *      is no such literal in RFC 8259 -- so one bad sample made the whole
 *      message unparseable for every client, not just that channel.
 *
 *   2. The precision-0 paths in both the JSON and CSV encoders cast the
 *      voltage to int32_t. Both ALREADY clamped the range:
 *
 *          if (v > (double)INT32_MAX)      mv = INT32_MAX;
 *          else if (v < (double)INT32_MIN) mv = INT32_MIN;
 *          else mv = (int32_t)(v >= 0.0 ? v + 0.5 : v - 0.5);
 *
 *      ⛔ and NaN slipped straight through it, because EVERY comparison with
 *      NaN is false. So NaN took the `else` and hit the cast, which is
 *      undefined behaviour (C11 6.3.1.4: the behaviour is undefined when the
 *      truncated value cannot be represented). The range check looked like it
 *      covered this and did not -- the hole is the shape of the guard, not its
 *      absence.
 *
 * REACHABLE WITH FINITE CALIBRATION. This is not a "somebody stored NaN in
 * NVM" scenario (though that is also possible): `CalM = 1e307` is a perfectly
 * finite coefficient that overflows `MC12b_ConvertToVoltage` to `inf` on NQ1.
 * The encoders see a non-finite value produced from finite inputs.
 *
 * ⚠ DELIBERATE BEHAVIOUR CHANGE for the infinities. Before this header, +inf
 * compared greater than INT32_MAX and so emitted as `2147483647`, and -inf as
 * `-2147483648` -- plausible-looking numbers that were never measured. They
 * now take the non-numeric path instead (`null` in JSON, an empty field in
 * CSV), because a wrong number is worse than an absent one: a client can see
 * and handle a null, but cannot tell a clamped infinity from a real
 * full-scale reading.
 *
 * WHERE THE POLICY LIVES. The DECISION is shared (here); the RENDERING is not.
 * JSON must emit the literal `null` and CSV must emit an empty field, so each
 * encoder keeps its own fallback text at the emitter, where the format is
 * known. Putting the text here would mean this header knowing about two wire
 * formats, and the CSV case is not a token at all -- it is the absence of one.
 *
 * Header-only and dependency-free ON PURPOSE, for the same reason as
 * FixedPointFmt.h next to it: the correctness bar is the behaviour at NaN,
 * ±inf and the int32 boundaries, which is only credible if it can be checked
 * exhaustively on a host. Keeping it out of the encoders (which pull in the
 * board config, ADC and FreeRTOS) lets tests/host include it directly with no
 * stubs. See tests/host/test_1199_sample_emit.c.
 * ========================================================================== */
#ifndef SAMPLEEMIT_H
#define SAMPLEEMIT_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

/* May `v` be emitted as a JSON/CSV number at all?
 *
 * For the decimal emitters, which print the double directly and need no
 * conversion. False for NaN and both infinities; the caller emits its own
 * non-numeric form.
 *
 * This is `isfinite` and is named anyway, so that the two encoders cannot
 * drift to different notions of "emittable" and so that the one place to
 * change the policy is this file. */
static inline bool sample_emit_is_numeric(double v) {
    return isfinite(v) ? true : false;
}

/* Saturating, NaN-safe double -> int32_t for the precision-0 emitters.
 *
 * Returns false and leaves *out untouched when `v` is non-finite -- there is
 * no int32 that represents NaN or an infinity, and inventing one is what the
 * old clamp did. Otherwise clamps to the int32 range, rounds half away from
 * zero (matching what the encoders did before), writes *out and returns true.
 *
 * ⛔ The finiteness test MUST come first. Ordering it after the range
 * comparisons is exactly the defect this replaces: NaN fails `>` and `<`
 * alike, so it reaches the cast.
 *
 * No overflow in the cast itself: the clamps bound |v| <= 2^31, so the
 * rounded intermediate is at most 2147483647.5 / at least -2147483648.5, and
 * a cast truncates TOWARD ZERO, landing inside int32 either way. */
static inline bool sample_emit_to_int32(double v, int32_t *out) {
    if (!isfinite(v)) {
        return false;
    }
    if (v > (double)INT32_MAX) {
        *out = INT32_MAX;
        return true;
    }
    if (v < (double)INT32_MIN) {
        *out = INT32_MIN;
        return true;
    }
    *out = (int32_t)(v >= 0.0 ? v + 0.5 : v - 0.5);
    return true;
}

#endif /* SAMPLEEMIT_H */
