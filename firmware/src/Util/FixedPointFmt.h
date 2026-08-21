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
#include <float.h>

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
static const uint64_t kFixedFmtPow10[FIXEDFMT_MAX_PRECISION + 1u] = {
    1ull, 10ull, 100ull, 1000ull, 10000ull, 100000ull,
    1000000ull, 10000000ull, 100000000ull, 1000000000ull
};

static inline bool fixedfmt_can_format(double v, unsigned precision) {
    if (precision == 0u || precision > FIXEDFMT_MAX_PRECISION) {
        return false;
    }
    if (!isfinite(v)) {
        return false;
    }
    if (!(fabs(v) < FIXEDFMT_MAX_ABS)) {
        return false;
    }

    /* Reject values this method cannot DECIDE, and let the caller's snprintf
     * settle them.
     *
     * The fast path rounds `mag * 10^p`, but that product is ITSELF a rounded
     * double carrying up to half an ULP of error. A true value sitting just
     * below a tie can be lifted exactly ONTO one -- 0.28359374999999998 at
     * p=7 becomes ...37.5 -- and then no tie rule recovers the right answer:
     * printf rounds the true value down, any rounding of the product goes up.
     * Capping FIXEDFMT_MAX_PRECISION does not help; measured against snprintf
     * this bites at EVERY precision from 1 upward, just more often as p grows.
     *
     * So the envelope is defined by decidability, not by precision alone. If
     * the product's fractional part lies within a few ULP of 0.5, the true
     * value's side of that boundary is unknowable from the product and we
     * defer. Anything further away is provably on the side the product shows.
     *
     * Costs one extra multiply per value and defers well under 0.1% of real
     * ADC output, so the hot path keeps its win -- and it is what makes the
     * byte-identity claim provable rather than merely well-tested. */
    const double scaled = fabs(v) * (double)kFixedFmtPow10[precision];
    if (!isfinite(scaled)) {
        return false;
    }
    double ipart;
    const double frac = modf(scaled, &ipart);

    /* Margin sizing, stated honestly.
     *
     * Detecting an EXACT tie (margin 0) is what the evidence shows to be
     * load-bearing: across ~4.96M host comparisons -- uncalibrated NQ1, three
     * calibrated NQ1 gain/offset pairs, and the full 18-bit NQ3 range --
     * `margin = 0.0` produces zero mismatches, and that is the mutation the
     * test actually proves. The theory agrees: k+0.5 is exactly representable
     * below 2^52, so round-to-nearest maps a near-tie product ONTO k+0.5
     * (deferred) or leaves it strictly on the side printf would choose.
     *
     * The few ULP of headroom is therefore NOT claimed as proven-necessary.
     * It is deliberate insurance for the gap between oracle and target: the
     * differential test compares against the HOST's snprintf (glibc), while
     * the device links XC32's musl on a MIPS FPU. Byte-identity is proven
     * against glibc; a hair of margin covers a boundary that a different libm
     * or different codegen might place a hair differently. It defers ~0.004%
     * of additional values, which costs nothing measurable. */
    const double margin = scaled * (DBL_EPSILON * 4.0) + DBL_MIN;
    return (fabs(frac - 0.5) > margin);
}

/**
 * @brief Formats @p v with exactly @p precision decimals, like "%.*f".
 *
 * Writes without a NUL terminator (the CSV encoder appends its own separators)
 * and returns the new write pointer, or NULL if @p rem is too small -- the same
 * contract as uint32_to_str/int_to_str in csv_encoder.c.
 *
 * Rounding is half-to-EVEN on the scaled value (nearbyint under the default
 * FE_TONEAREST), which is what printf does. Exact ties are the only place the
 * two rounding rules can differ, and the ADC generates them routinely because
 * its conversion divides by a power of two -- so this is not a corner case,
 * it is ordinary traffic. See the note at the nearbyint() call.
 *
 * @pre fixedfmt_can_format(v, precision) is true.
 */
static inline char* fixedfmt_to_str(double v, unsigned precision,
                                    char* buf, size_t rem) {
    if (buf == NULL || precision == 0u || precision > FIXEDFMT_MAX_PRECISION) {
        return NULL;
    }

    /* Sign is taken from the value, not from the rounded result: -0.0004 at
     * precision 3 must print "-0.000", exactly as printf does. Rounding first
     * and testing the integer would lose that minus. */
    const bool neg = signbit(v);
    const double mag = fabs(v);
    const uint64_t scale = kFixedFmtPow10[precision];

    /* nearbyint(), NOT round(). This is the whole correctness hinge.
     *
     * round() is half-AWAY-FROM-ZERO. printf("%.*f") is correctly rounded in
     * the current FP rounding mode, which is FE_TONEAREST by default, i.e.
     * half-to-EVEN. They agree everywhere except on an exact tie -- and the
     * ADC produces ties constantly, because MC12b_ConvertToVoltage divides by
     * the module Resolution (4096 on NQ1, MC12bADC.c:257), making every
     * converted voltage a DYADIC rational that can land exactly halfway.
     *
     * Concretely, with round(): raw code 128 at the shipped NQ1 default
     * precision 4 is 0.15625, which printed as "0.1563" where snprintf gives
     * "0.1562" -- one wrong digit, silently, on ~8 of every 4096 codes.
     * nearbyint() under FE_TONEAREST reproduces printf exactly.
     *
     * Found by adversarial audit on PR #819; the differential test missed it
     * because it swept scales over 4095 (adcMax) instead of 4096
     * (Resolution), and 1/4095 is not dyadic so it never generated a tie. The
     * test now uses the firmware's own divisor and carries explicit dyadic
     * tie cases of both parities. */
    const double scaledF = nearbyint(mag * (double)scale);
    if (!(scaledF >= 0.0) || scaledF > 1.8e19) {   /* NaN-safe bound check */
        return NULL;
    }
    const uint64_t scaled = (uint64_t)scaledF;

    const uint64_t whole = scaled / scale;
    const uint64_t frac = scaled % scale;

    /* Both parts are rendered with 32-BIT division below. That is a real
     * saving on this target: MIPS32 has no 64-bit divide instruction, so every
     * `/ 10ull` compiles to a call into the __udivdi3 software routine, and
     * this runs once per value per channel per sample.
     *
     * The narrowing is only sound while both parts fit in 32 bits.
     * fixedfmt_can_format() bounds |v| < FIXEDFMT_MAX_ABS (1e9), which bounds
     * `whole`; and `frac < scale <= 1e9` by construction, so the fraction needs
     * no check. `whole` DOES get one: this function is reachable directly, and
     * an unchecked cast would turn a precondition violation into a silently
     * truncated number instead of a clean failure. Failing closed sends the
     * caller to snprintf, which is exactly the fallback contract. */
    if (whole > 0xFFFFFFFFull) {
        return NULL;
    }

    /* Render the integer part into a scratch buffer first so the required
     * width is known before anything is written -- a partial write into the
     * caller's buffer would corrupt the row rather than cleanly failing.
     * 20 bytes is retained deliberately: it outlives any future widening of
     * the bound above, and a 10-digit uint32 cannot overrun it. */
    char tmp[20];
    int wlen = 0;
    uint32_t w = (uint32_t)whole;
    if (w == 0u) {
        tmp[wlen++] = '0';
    } else {
        while (w > 0u) {
            tmp[wlen++] = (char)('0' + (int)(w % 10u));
            w /= 10u;
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
    uint32_t f32 = (uint32_t)frac;
    /* kFixedFmtPow10[precision - 1] rather than `scale / 10`: precision >= 1
     * guaranteed above, so the index is in range, and it removes one more
     * 64-bit division from the hot path. */
    uint32_t divisor = (uint32_t)kFixedFmtPow10[precision - 1u];
    for (unsigned i = 0u; i < precision; i++) {
        *buf++ = (char)('0' + (int)((f32 / divisor) % 10u));
        divisor /= 10u;
    }

    return buf;
}

#endif /* FIXEDPOINTFMT_H */
