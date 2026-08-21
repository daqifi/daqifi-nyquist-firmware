/* ==========================================================================
 * test_fixedpointfmt.c — host differential test for Util/FixedPointFmt.h (#250)
 *
 * The formatter replaces snprintf("%.*f") on the CSV streaming hot path, so
 * the ONLY acceptable standard is byte-identical output. This test therefore
 * compares against snprintf itself rather than against hand-written expected
 * strings: an expectation table would encode my belief about printf, which is
 * exactly the thing in question.
 *
 * Two layers:
 *
 *   1. EXHAUSTIVE over the real domain. The ADC code space is finite -- 4096
 *      codes on NQ1 (12-bit), 262144 on NQ3 (18-bit signed) -- so every value
 *      the device can actually emit is checked at every shipped precision,
 *      across several channel scales. That is a proof for the domain that
 *      matters, not a sample of it.
 *
 *   2. ADVERSARIAL edges. Carry across the decimal point (0.9999), leading
 *      zeros in the fraction (0.0004), negative values that round to zero
 *      (-0.0004 must keep its sign, as printf does), exact halves, and values
 *      near the guards.
 *
 * Run: make -C tests/host run
 * ========================================================================== */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#include "FixedPointFmt.h"     /* real header (via -I firmware/src/Util) */

/* test_framework.h is deliberately NOT used here: its TEST()/ASSERT_EQ shape
 * fits a handful of named cases, whereas this suite is one differential
 * predicate applied ~800k times. Counting mismatches and reporting the first
 * few is more useful than 800k named assertions, and including the framework
 * unused would only add -Wunused warnings. */

/* ---------------------------------------------------------------------------
 * One comparison: format with both, require identical bytes.
 * Returns 1 on mismatch (and prints it), 0 on agreement.
 * ------------------------------------------------------------------------- */
static int g_checked;
static int g_mismatch_shown;
/* Per-precision mismatch histogram. The residual failure mode is
 * scale-then-round: `mag * 10^p` is itself a rounded double, so a value just
 * BELOW a tie can be lifted ONTO one, after which any tie rule rounds it the
 * wrong way. That grows with p, so the histogram is what calibrates
 * FIXEDFMT_MAX_PRECISION -- it shows exactly where exactness ends instead of
 * leaving the ceiling to guesswork. */
static int g_fail_by_prec[FIXEDFMT_MAX_PRECISION + 2];
static int g_checked_by_prec[FIXEDFMT_MAX_PRECISION + 2];

static int differs(double v, unsigned precision)
{
    char mine[64];
    char theirs[64];

    if (!fixedfmt_can_format(v, precision)) {
        return 0;   /* caller falls back to snprintf; nothing to compare */
    }

    char *end = fixedfmt_to_str(v, precision, mine, sizeof(mine) - 1u);
    if (end == NULL) {
        printf("  FAIL fixedfmt_to_str returned NULL for %.17g @ %u\n",
               v, precision);
        return 1;
    }
    *end = '\0';

    int n = snprintf(theirs, sizeof(theirs), "%.*f", (int)precision, v);
    if (n < 0 || (size_t)n >= sizeof(theirs)) {
        return 0;   /* snprintf itself could not render it; out of scope */
    }

    g_checked++;
    if (precision <= FIXEDFMT_MAX_PRECISION) {
        g_checked_by_prec[precision]++;
    }
    if (strcmp(mine, theirs) != 0) {
        if (precision <= FIXEDFMT_MAX_PRECISION) {
            g_fail_by_prec[precision]++;
        }
        if (g_mismatch_shown < 10) {   /* cap the noise, keep the count */
            printf("  MISMATCH v=%.17g p=%u  mine=\"%s\"  snprintf=\"%s\"\n",
                   v, precision, mine, theirs);
            g_mismatch_shown++;
        }
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------------------
 * 1. Exhaustive over every code the hardware can produce.
 *
 * Scales chosen to span the real channel ranges rather than one convenient
 * number: the divisors below give full-scale spans of roughly 3.3 V, 10 V,
 * 5 V and 24 V, which is the spread across NQ1 inputs and the NQ3 bipolar
 * front end.
 * ------------------------------------------------------------------------- */
static int exhaustive_nq1(void)
{
    /* Divisor is the module's Resolution (4096), NOT adcMax (4095). This is
     * the firmware's own arithmetic: MC12b_ConvertToVoltage (MC12bADC.c:257)
     * computes (range * scale * CalM * raw) / Resolution + CalB, and
     * NQ1BoardConfig.c:35 sets Resolution = 4096.
     *
     * The distinction is the whole test. 5/4096 is a DYADIC rational, exactly
     * representable in binary, so code * scale lands on exact decimal ties
     * (raw=128 -> 0.15625, which is a tie at precision 4). 5/4095 is not
     * dyadic and essentially never produces one. An earlier revision of this
     * file used 4095 and was therefore blind to the entire residue class where
     * round-half-away-from-zero and printf's round-half-to-even disagree --
     * 821,880 comparisons all passed while the formatter was wrong on ~8 of
     * every 4096 codes at the shipped NQ1 default precision. */
    static const double kScales[] = {
        3.3 / 4096.0, 10.0 / 4096.0, 5.0 / 4096.0, 24.0 / 4096.0
    };
    int bad = 0;
    for (size_t s = 0; s < sizeof(kScales) / sizeof(kScales[0]); s++) {
        for (int code = 0; code <= 4095; code++) {
            const double v = (double)code * kScales[s];
            for (unsigned p = 1u; p <= FIXEDFMT_MAX_PRECISION; p++) {
                bad += differs(v, p);
            }
        }
    }
    return bad;
}

/* NQ3 / AD7609: 18-bit SIGNED, range -131072..+131071 (see the sign-extension
 * note in AD7609.c). Stepped rather than every code purely to keep the suite
 * quick; the step is deliberately coprime-ish with powers of ten so it does
 * not systematically skip the interesting fractional residues. */
static int exhaustive_nq3(void)
{
    /* Resolution again, not max code: NQ3BoardConfig.c sets 262144. Dyadic,
     * for the same tie-generating reason as the NQ1 sweep above. */
    static const double kScales[] = { 10.0 / 262144.0, 24.0 / 262144.0 };
    int bad = 0;
    for (size_t s = 0; s < sizeof(kScales) / sizeof(kScales[0]); s++) {
        for (int code = -131072; code <= 131071; code++) {
            const double v = (double)code * kScales[s];
            for (unsigned p = 1u; p <= FIXEDFMT_MAX_PRECISION; p++) {
                bad += differs(v, p);
            }
        }
    }
    return bad;
}

/* CALIBRATED sweep -- the configuration that actually ships.
 *
 * The sweeps above use CalM=1, CalB=0, which keeps every value a dyadic
 * rational. That models a factory-default board and NOT a calibrated one:
 * MC12b_ConvertToVoltage (MC12bADC.c:257) is
 *     (Range * InternalScale * CalM * raw) / Resolution + CalB
 * and on a real unit CalM/CalB are arbitrary doubles. The product mag*10^p is
 * then an arbitrary double too, so it can land NEAR a tie without landing ON
 * one -- the case the exact-tie test alone cannot reach, and the reason
 * fixedfmt_can_format uses an ULP margin rather than an equality test.
 *
 * Values chosen to be ordinary, not adversarial: a ~0.2% gain error and a few
 * mV of offset are typical of this hardware. */
static int exhaustive_nq1_calibrated(void)
{
    static const double kCalM[] = { 1.0021734, 0.9987361, 1.0000313 };
    static const double kCalB[] = { -0.0037219, 0.0011947, 0.0 };
    int bad = 0;
    for (size_t c = 0; c < sizeof(kCalM) / sizeof(kCalM[0]); c++) {
        for (int code = 0; code <= 4095; code++) {
            /* 5 V range, InternalScale 1, Resolution 4096 -- NQ1 defaults. */
            const double v = (5.0 * 1.0 * kCalM[c] * (double)code) / 4096.0
                           + kCalB[c];
            for (unsigned p = 1u; p <= FIXEDFMT_MAX_PRECISION; p++) {
                bad += differs(v, p);
            }
        }
    }
    return bad;
}

/* ---------------------------------------------------------------------------
 * 2. Adversarial edges — the cases where a naive implementation breaks.
 * ------------------------------------------------------------------------- */
static int edges(void)
{
    static const double kVals[] = {
        0.0, -0.0,
        0.9999, -0.9999,          /* carry across the decimal point */
        0.99999999, -0.99999999,
        0.0004, -0.0004,          /* leading zeros; negative rounds to -0.000 */
        0.00004, -0.00004,
        0.5, -0.5, 1.5, -1.5, 2.5, -2.5,   /* exact halves at p=0 (not ties p>=1) */
        /* REAL ties at p>=1: dyadic rationals of the form k/2^n that land
         * exactly halfway at the tested precision. These are what the ADC
         * actually produces (5*raw/4096), and they are the only place
         * half-away-from-zero and printf's half-to-even can disagree. Both
         * parities are present on purpose -- ties-to-even rounds DOWN when the
         * preceding digit is even and UP when it is odd, so a formatter that
         * always rounds one way fails half of these whichever way it leans. */
        1.25, -1.25, 0.75, -0.75, 0.25, -0.25,
        0.125, 0.375, 0.625, 0.875,
        -0.125, -0.375, -0.625, -0.875,
        0.15625, -0.15625,      /* raw=128 at 5V/4096, NQ1 default precision 4 */
        0.78125, 1.40625, 2.03125, 2.65625,
        0.0078125, -0.0078125,  /* tie at precision 6 -- NQ3 default */
        0.00390625, 0.001953125,
        3.0517578125e-05,       /* 2^-15: tie deep in the precision range */
        0.05, 0.005, 0.0005,
        1.0, -1.0, 9.0, -9.0,
        3.3, 5.0, 10.0, 24.0, -24.0,
        1.0 / 3.0, -1.0 / 3.0,
        123456.789, -123456.789,
        1e-9, -1e-9, 1e8, -1e8,
    };
    int bad = 0;
    for (size_t i = 0; i < sizeof(kVals) / sizeof(kVals[0]); i++) {
        for (unsigned p = 1u; p <= FIXEDFMT_MAX_PRECISION; p++) {
            bad += differs(kVals[i], p);
        }
    }
    return bad;
}

/* ---------------------------------------------------------------------------
 * 3. Guard behaviour — the caller depends on these to know when to fall back.
 * ------------------------------------------------------------------------- */
static int guards(void)
{
    int bad = 0;
    char buf[64];

    /* precision 0 is the caller's integer fast path, not ours */
    if (fixedfmt_can_format(1.0, 0u)) { printf("  FAIL p=0 accepted\n"); bad++; }
    /* above the calibrated ceiling the caller must use snprintf */
    if (fixedfmt_can_format(1.0, FIXEDFMT_MAX_PRECISION + 1u)) {
        printf("  FAIL p>max accepted\n"); bad++;
    }
    /* non-finite must be refused, not mis-rendered */
    if (fixedfmt_can_format(NAN, 4u))      { printf("  FAIL NaN accepted\n"); bad++; }
    if (fixedfmt_can_format(INFINITY, 4u)) { printf("  FAIL inf accepted\n"); bad++; }
    if (fixedfmt_can_format(FIXEDFMT_MAX_ABS * 2.0, 4u)) {
        printf("  FAIL huge accepted\n"); bad++;
    }

    /* Too-small buffer must fail cleanly rather than writing a partial row.
     * "1.2345" needs 6; give it 5 and require a clean NULL. */
    memset(buf, 0x7f, sizeof(buf));
    if (fixedfmt_to_str(1.2345, 4u, buf, 5u) != NULL) {
        printf("  FAIL short buffer not rejected\n"); bad++;
    }
    for (size_t i = 0; i < 5u; i++) {
        if (buf[i] != 0x7f) {
            printf("  FAIL short-buffer call wrote into the buffer\n");
            bad++;
            break;
        }
    }
    return bad;
}

int main(void)
{
    int failures = 0;

    printf("test_fixedpointfmt (#250) — differential vs snprintf\n");

    printf("  guards...\n");
    failures += guards();

    printf("  adversarial edges...\n");
    failures += edges();

    printf("  exhaustive NQ1 (4096 codes x 4 scales x %u precisions)...\n",
           FIXEDFMT_MAX_PRECISION);
    failures += exhaustive_nq1();

    printf("  exhaustive NQ3 (full 18-bit signed x 2 scales)...\n");
    failures += exhaustive_nq3();

    printf("  exhaustive NQ1 CALIBRATED (4096 codes x 3 cal pairs)...\n");
    failures += exhaustive_nq1_calibrated();

    printf("\n  mismatches by precision:\n");
    for (unsigned p = 1u; p <= FIXEDFMT_MAX_PRECISION; p++) {
        printf("    p=%u: %8d / %8d\n", p, g_fail_by_prec[p],
               g_checked_by_prec[p]);
    }
    printf("\n%d comparison(s) against snprintf, %d failure(s)\n",
           g_checked, failures);
    if (failures == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL\n");
    return 1;
}
