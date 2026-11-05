/* ==========================================================================
 * test_1156_double_fmt.c — Util/DoubleFmt.h: a double must read back EXACTLY
 *
 * #1156: libscpi rendered every double at 15 significant digits ("%.15lg"),
 * and a double needs up to 17. So CONFigure:ADC:CHANCALM? answered a ROUNDED
 * coefficient with a clean error queue, making a read-modify-write of
 * calibration silently lossy -- and a client verifying its own restore
 * compared rounded against rounded, so the check could not fail.
 *
 * The central test is a PROPERTY, not a table of literals:
 * text(v) parsed back == v. The issue's four reproduction rows are pinned
 * individually as well, because a property test that happened to miss the
 * 16-digit boundary would still pass.
 *
 * Header-only and dependency-free, so this needs no UUT copy and no stubs.
 * -lm is for the math used to build the test vectors.
 * ========================================================================== */
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "test_framework.h"
#include "DoubleFmt.h"

/* test_framework.h has no string comparison (ASSERT_TRUE/FALSE/EQ/BYTES
 * only). Defined locally rather than widening a header 36 test binaries
 * share, for one PR. Prints both sides, because "the strings differ" without
 * them is a failure you then have to reproduce by hand. */
#define ASSERT_STR_EQ(got, want) do {                                       \
    const char *_g = (got), *_w = (want);                                   \
    g_asserts++;                                                            \
    if (strcmp(_g, _w) != 0) {                                              \
        g_current_failed++;                                                 \
        printf("    ASSERT FAILED [%s:%d]: got \"%s\" want \"%s\"\n",       \
               __func__, __LINE__, _g, _w);                                 \
    }                                                                       \
} while (0)

/* The old behaviour, reproduced exactly, so every claim about what #1156 did
 * is MEASURED here rather than asserted from the issue text. */
static void old_15_digit(double v, char *s, size_t len)
{
    snprintf(s, len, "%.15lg", v);
}

static int old_round_trips(double v)
{
    char buf[64];
    old_15_digit(v, buf, sizeof buf);
    return strtod(buf, NULL) == v;
}

static int new_round_trips(double v, char *out, size_t len)
{
    size_t n = daqifi_double_to_str(v, out, len);
    return n > 0 && strtod(out, NULL) == v;
}

/* ---------------------------------------------------------------------- */

TEST(the_issues_four_reproduction_rows_round_trip_now)
{
    /* Straight from #1156's table. Each needs >15 significant digits. */
    static const double rows[] = {
        9007199254740991.0,      /* 2^53 - 1, the integer limit of a double */
        999999999999999.5,
        0.30000000000000004,     /* 0.1 + 0.2, ordinary arithmetic */
        1.0000000000000002       /* nextafter(1.0, 2.0) */
    };
    size_t i;
    char buf[64];

    for (i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        /* The CONTROL: each of these must FAIL at 15 digits, or this test
         * would pass against the defect it exists to catch. */
        ASSERT_FALSE(old_round_trips(rows[i]));
        ASSERT_TRUE(new_round_trips(rows[i], buf, sizeof buf));
    }
}

TEST(a_value_that_fits_15_digits_is_rendered_IDENTICALLY_to_before)
{
    /* The reason for trying 15 first. Widening unconditionally to "%.17lg"
     * would have changed the wire format of every double-valued getter --
     * 0.1 would answer 0.10000000000000001. These must be byte-identical. */
    static const double ordinary[] = {
        0.0, 1.0, -1.0, 0.1, 0.5, 2.5, 100.0, -273.15,
        3.14159265358979, 1e-3, 1e3, 1e10, 1e-10, 2773.0, 65535.0
    };
    size_t i;
    char now[64], before[64];

    for (i = 0; i < sizeof ordinary / sizeof ordinary[0]; i++) {
        daqifi_double_to_str(ordinary[i], now, sizeof now);
        old_15_digit(ordinary[i], before, sizeof before);
        ASSERT_STR_EQ(now, before);
        /* and they already round-tripped, which is WHY they are unchanged */
        ASSERT_TRUE(old_round_trips(ordinary[i]));
    }
}

TEST(the_calibration_identity_values_are_unchanged)
{
    /* 1.0 and 0.0 are what an uncalibrated board reports, and several tests
     * key on their exact text. They must not grow digits. */
    char buf[64];

    daqifi_double_to_str(1.0, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "1");
    daqifi_double_to_str(0.0, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "0");
    daqifi_double_to_str(-0.0, buf, sizeof buf);
    ASSERT_STR_EQ(buf, "-0");
}

TEST(round_trip_holds_across_the_ordinary_calibration_range)
{
    /* The PROPERTY, over values built the way a real coefficient is: a
     * least-squares slope near 1 and an offset near 0, perturbed by one ULP
     * so they land in the 16/17-digit region on purpose. */
    int i;
    char buf[64];
    int widened = 0;

    for (i = -200; i <= 200; i++) {
        double slope  = nextafter(1.0 + (double) i * 1e-6, 2.0);
        double offset = nextafter((double) i * 1e-4, 1.0);

        ASSERT_TRUE(new_round_trips(slope, buf, sizeof buf));
        if (!old_round_trips(slope)) {
            widened++;
        }
        ASSERT_TRUE(new_round_trips(offset, buf, sizeof buf));
    }
    /* ⭐ Assert the EXACT shape of the improvement, not just that the new
     * code passes: if NOTHING in this sweep needed widening, the sweep is
     * not exercising #1156 and the test above it is decoration. */
    ASSERT_TRUE(widened > 300);
}

TEST(a_non_finite_renders_exactly_as_it_did_before)
{
    /* strtod("nan") does not equal NaN, so a NaN walks all three widths and
     * keeps the last. Every width renders a non-finite identically, so the
     * OUTPUT must be unchanged -- the extra attempts are invisible. The SCPI
     * layer's non-finite policy is #1149's, not this file's. */
    char now[64], before[64];

    daqifi_double_to_str((double) INFINITY, now, sizeof now);
    old_15_digit((double) INFINITY, before, sizeof before);
    ASSERT_STR_EQ(now, before);

    daqifi_double_to_str((double) -INFINITY, now, sizeof now);
    old_15_digit((double) -INFINITY, before, sizeof before);
    ASSERT_STR_EQ(now, before);

    daqifi_double_to_str((double) NAN, now, sizeof now);
    old_15_digit((double) NAN, before, sizeof before);
    ASSERT_STR_EQ(now, before);

    /* and a NaN honestly reports that it does not round trip */
    ASSERT_FALSE(daqifi_double_round_trips((double) NAN));
    ASSERT_TRUE(daqifi_double_round_trips((double) INFINITY));
}

TEST(a_short_buffer_never_yields_a_TRUNCATED_numeral)
{
    /* ⛔ A numeral cut off by a short buffer is a CORRUPT value, strictly
     * worse than a rounded one: "1.2345e+1" is wrong by 10^9 against
     * 1.2345e+10, not by an epsilon. So no rendering may be left truncated.
     * Checked by parsing whatever was produced and requiring it to be within
     * the rounding error of the value -- never off by an exponent. */
    double v = 0.30000000000000004;     /* needs 17 digits */
    size_t len;

    for (len = 2; len <= 32; len++) {
        char small[40];
        size_t n;

        memset(small, '\0', sizeof small);
        n = daqifi_double_to_str(v, small, len);

        ASSERT_TRUE(n < len);                   /* always NUL-terminated */
        ASSERT_EQ((long long) n, (long long) strlen(small));
        if (n > 0) {
            double back = strtod(small, NULL);
            /* within 1% -- a truncated exponent would be orders out */
            ASSERT_TRUE(fabs(back - v) < 0.01 * fabs(v));
        }
    }
}

TEST(the_widest_rendering_fits_the_SCPI_result_buffer)
{
    /* SCPI_ResultDouble gives this a char[32] (parser.c). If the widest
     * rendering did not fit, the fix would silently truncate on the wire --
     * so the bound is asserted here rather than reasoned about. */
    static const double extremes[] = {
        DBL_MAX, -DBL_MAX, DBL_MIN, -DBL_MIN,
        0.30000000000000004, -0.30000000000000004,
        1.0000000000000002, 2.2250738585072014e-308
    };
    size_t i;
    char buf[32];

    ASSERT_TRUE(DAQIFI_DOUBLE_FMT_MIN_BUFFER <= 32);
    for (i = 0; i < sizeof extremes / sizeof extremes[0]; i++) {
        size_t n = daqifi_double_to_str(extremes[i], buf, sizeof buf);
        ASSERT_TRUE(n > 0);
        ASSERT_TRUE(n < sizeof buf);
    }
}

TEST(the_platforms_own_limit_is_STATED_not_assumed)
{
    /* ⚠ #1144 measured this toolchain's %g as unfaithful at the extremes.
     * This test does not assert that DBL_MAX round trips -- it asserts that
     * daqifi_double_round_trips REPORTS whatever is true here, so a platform
     * that cannot carry a value says so instead of pretending.
     *
     * On the host gcc these do round trip. On XC32 DBL_MAX measured as
     * `0e+309` from the device, which does NOT round trip -- and the point of
     * the accessor is that a caller can tell the difference. Either way the
     * two sides below must AGREE, which is the real invariant. */
    static const double extremes[] = { DBL_MAX, -DBL_MAX, DBL_MIN };
    size_t i;

    for (i = 0; i < sizeof extremes / sizeof extremes[0]; i++) {
        char buf[64];
        size_t n = daqifi_double_to_str(extremes[i], buf, sizeof buf);
        int reported = daqifi_double_round_trips(extremes[i]);
        int actual   = (n > 0) && (strtod(buf, NULL) == extremes[i]);

        ASSERT_EQ(reported, actual);
    }
}

TEST(a_null_or_zero_length_buffer_is_refused_not_written)
{
    char buf[8] = { 'x', 0 };

    ASSERT_EQ((long long) daqifi_double_to_str(1.5, NULL, 8), 0LL);
    ASSERT_EQ((long long) daqifi_double_to_str(1.5, buf, 0), 0LL);
    ASSERT_EQ(buf[0], 'x');            /* untouched when len is 0 */
}

int main(void)
{
    printf("Util/DoubleFmt.h — #1156 double read-back precision\n");
    printf("====================================================\n");
    RUN(the_issues_four_reproduction_rows_round_trip_now);
    RUN(a_value_that_fits_15_digits_is_rendered_IDENTICALLY_to_before);
    RUN(the_calibration_identity_values_are_unchanged);
    RUN(round_trip_holds_across_the_ordinary_calibration_range);
    RUN(a_non_finite_renders_exactly_as_it_did_before);
    RUN(a_short_buffer_never_yields_a_TRUNCATED_numeral);
    RUN(the_widest_rendering_fits_the_SCPI_result_buffer);
    RUN(the_platforms_own_limit_is_STATED_not_assumed);
    RUN(a_null_or_zero_length_buffer_is_refused_not_written);
    return TEST_SUMMARY();
}
