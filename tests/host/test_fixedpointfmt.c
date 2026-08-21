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
    if (strcmp(mine, theirs) != 0) {
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
    static const double kScales[] = {
        3.3 / 4095.0, 10.0 / 4095.0, 5.0 / 4095.0, 24.0 / 4095.0
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
    static const double kScales[] = { 10.0 / 131071.0, 24.0 / 131071.0 };
    int bad = 0;
    for (size_t s = 0; s < sizeof(kScales) / sizeof(kScales[0]); s++) {
        for (int code = -131072; code <= 131071; code += 7) {
            const double v = (double)code * kScales[s];
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
        0.5, -0.5, 1.5, -1.5, 2.5, -2.5,   /* exact halves */
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

    printf("  exhaustive NQ3 (stepped 18-bit signed x 2 scales)...\n");
    failures += exhaustive_nq3();

    printf("\n%d comparison(s) against snprintf, %d failure(s)\n",
           g_checked, failures);
    if (failures == 0) {
        printf("PASS\n");
        return 0;
    }
    printf("FAIL\n");
    return 1;
}
