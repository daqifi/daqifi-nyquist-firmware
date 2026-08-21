/* ==========================================================================
 * FixedPointFmt.h — decimal formatting for the CSV streaming hot path (#250)
 *
 * Replaces snprintf("%.*f", p, v) for the value column. That call is the
 * measured bottleneck on the path every NQ1 ships with: at 5 channels, CSV to
 * SD, 6 kHz, NOCAP, precision 0 (which already takes an integer fast path)
 * streams every sample at ~500 KB/s while precision 4 -- the NQ1 default --
 * drops 42% and achieves only ~300 KB/s. NQ3's default of 6 drops 54%.
 * Bandwidth is excluded as the cause: the fast arm pushes the MOST bytes per
 * second, and bytes/sample rises only ~17% across the range while loss goes
 * 0% -> 54%. Full method and figures on issue #250.
 *
 * Header-only and dependency-free ON PURPOSE. The correctness bar here is
 * byte-identity with snprintf, which is only credible if it can be checked
 * exhaustively on a host; keeping this out of csv_encoder.c (which pulls in
 * the board config, ADC and FreeRTOS) lets tests/host include it directly with
 * no stubs. See tests/host/test_fixedpointfmt.c.
 *
 * SCOPE: this is NOT a general printf replacement. It handles finite values at
 * a precision the caller has already validated; the caller keeps snprintf for
 * everything else (see FIXEDFMT_MAX_PRECISION and fixedfmt_can_format).
 * ========================================================================== */
#ifndef FIXEDPOINTFMT_H
#define FIXEDPOINTFMT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <math.h>

/* Highest precision this formatter is allowed to handle.
 *
 * Not an arbitrary cap. The scaled intermediate is v * 10^p, and the rounding
 * must land on the same decimal digit snprintf would choose from the double's
 * exact binary value. The higher p goes, the closer that multiply runs to the
 * ~15-17 significant decimal digits a double actually carries, and the more
 * likely scale-then-round disagrees with a correctly-rounded conversion.
 *
 * The value here is set from the exhaustive host differential in
 * tests/host/test_fixedpointfmt.c -- raise it only if that test still passes.
 * Above this the caller falls back to snprintf, which costs nothing in
 * practice: the shipped defaults are NQ1 4, NQ3 6, NQ2 7. */
#define FIXEDFMT_MAX_PRECISION 9u

/* Largest magnitude accepted. Beyond this the scaled value risks exceeding
 * the uint64 intermediate at high precision, and the integer part stops being
 * exactly representable anyway. Real channel voltages are single/double digit;
 * this is a guard, not a working range. */
#define FIXEDFMT_MAX_ABS 1.0e9

/**
 * @brief True if fixedfmt_to_str can be trusted for this value/precision.
 *
 * The caller MUST consult this and fall back to snprintf when it returns
 * false. NaN and infinity are rejected here rather than mishandled: printf
 * spells them "nan"/"inf" and reproducing that is not this function's job.
 */
static inline bool fixedfmt_can_format(double v, unsigned precision) {
    if (precision == 0u || precision > FIXEDFMT_MAX_PRECISION) {
        return false;
    }
    if (!isfinite(v)) {
        return false;
    }
    return (fabs(v) < FIXEDFMT_MAX_ABS);
}

/**
 * @brief Formats @p v with exactly @p precision decimals, like "%.*f".
 *
 * Writes without a NUL terminator (the CSV encoder appends its own separators)
 * and returns the new write pointer, or NULL if @p rem is too small -- the same
 * contract as uint32_to_str/int_to_str in csv_encoder.c.
 *
 * Rounding is half-away-from-zero on the scaled value, which is what printf
 * produces for every input the differential test covers. Exact ties -- where
 * the double sits precisely on a half at the requested precision -- are the
 * only place a correctly-rounded conversion could differ, and they are what
 * FIXEDFMT_MAX_PRECISION is calibrated against.
 *
 * @pre fixedfmt_can_format(v, precision) is true.
 */
static inline char* fixedfmt_to_str(double v, unsigned precision,
                                    char* buf, size_t rem) {
    static const uint64_t kPow10[FIXEDFMT_MAX_PRECISION + 1u] = {
        1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull,
        1000000ull, 10000000ull, 100000000ull, 1000000000ull
    };

    if (buf == NULL || precision == 0u || precision > FIXEDFMT_MAX_PRECISION) {
        return NULL;
    }

    /* Sign is taken from the value, not from the rounded result: -0.0004 at
     * precision 3 must print "-0.000", exactly as printf does. Rounding first
     * and testing the integer would lose that minus. */
    const bool neg = signbit(v);
    const double mag = fabs(v);
    const uint64_t scale = kPow10[precision];

    /* round() is half-away-from-zero, matching the intent above. */
    const double scaledF = round(mag * (double)scale);
    if (!(scaledF >= 0.0) || scaledF > 1.8e19) {   /* NaN-safe bound check */
        return NULL;
    }
    const uint64_t scaled = (uint64_t)scaledF;

    const uint64_t whole = scaled / scale;
    const uint64_t frac = scaled % scale;

    /* Render the integer part into a scratch buffer first so the required
     * width is known before anything is written -- a partial write into the
     * caller's buffer would corrupt the row rather than cleanly failing. */
    char tmp[20];
    int wlen = 0;
    uint64_t w = whole;
    if (w == 0ull) {
        tmp[wlen++] = '0';
    } else {
        while (w > 0ull) {
            tmp[wlen++] = (char)('0' + (int)(w % 10ull));
            w /= 10ull;
        }
    }

    const size_t need = (size_t)(neg ? 1 : 0) + (size_t)wlen + 1u
                      + (size_t)precision;
    if (need > rem) {
        return NULL;
    }

    if (neg) {
        *buf++ = '-';
    }
    while (wlen > 0) {
        *buf++ = tmp[--wlen];
    }
    *buf++ = '.';

    /* Fractional digits, most significant first, zero-padded to `precision`
     * -- emitted by repeated division so a leading-zero fraction such as
     * 0.0004 keeps its zeros instead of printing as "4". */
    uint64_t divisor = scale / 10ull;
    for (unsigned i = 0u; i < precision; i++) {
        *buf++ = (char)('0' + (int)((frac / divisor) % 10ull));
        divisor /= 10ull;
    }

    return buf;
}

#endif /* FIXEDPOINTFMT_H */
