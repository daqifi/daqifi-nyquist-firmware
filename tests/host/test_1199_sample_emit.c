/* ==========================================================================
 * test_1199_sample_emit.c — host test for Util/SampleEmit.h (#1199)
 *
 * SCOPE, stated up front because the issue's acceptance line is broader than
 * what a host test can honestly cover. #1199 asks for "a host test [that]
 * feeds inf, -inf, nan and values beyond int32 range through every encoder,
 * and every output parses." The encoders themselves are NOT host-includable --
 * JSON_Encoder.c and csv_encoder.c pull in the board config, the ADC HAL and
 * FreeRTOS -- so "through every encoder" cannot run here without a stub tree
 * large enough to be its own source of error.
 *
 * So the acceptance is split, and each half is checked where it can actually
 * be proven:
 *
 *   1. THIS FILE proves the decision: every non-finite input is refused and
 *      every finite one is converted correctly, including at the int32
 *      boundaries. Dependency-free, so it includes the real header with no
 *      stubs on the include path at all.
 *   2. A WIRING CHECK (check_1199_emitter_wiring.py) proves the encoders
 *      actually route through it -- a correct predicate nobody calls is
 *      worthless, and that is a separate claim from the predicate's logic.
 *   3. THE COMPANION SUITE TEST proves the wire output parses, on hardware,
 *      against the real encoders.
 *
 * ⭐ The control test below is the one worth reading: it reproduces the OLD
 * guard's comparison sequence and shows NaN falling through it. That
 * demonstrates the defect's mechanism without executing the undefined cast.
 *
 * Run: make -C tests/host run
 * ========================================================================== */
#include <stdio.h>
#include <stdint.h>
#include <math.h>

#include "test_framework.h"
#include "SampleEmit.h"        /* real header (via -I firmware/src/Util) */

/* Built, not written as a literal: a NaN literal is not portable C and
 * 0.0/0.0 invites a compile-time diagnostic. */
static double make_nan(void)  { return nan(""); }
static double make_inf(void)  { return INFINITY; }

/* --------------------------------------------------------------------------
 * The defect's mechanism, shown rather than asserted.
 * -------------------------------------------------------------------------- */

TEST(the_old_clamp_let_nan_reach_the_cast)
{
    /* This is the exact comparison sequence the encoders used before #1199:
     *
     *     if (v > (double)INT32_MAX)      mv = INT32_MAX;
     *     else if (v < (double)INT32_MIN) mv = INT32_MIN;
     *     else mv = (int32_t)(...);        <-- NaN arrived HERE
     *
     * Both comparisons are FALSE for NaN -- that is the IEEE-754 rule for
     * every relational operator on a NaN operand -- so the `else` ran and the
     * cast was reached. The cast is not performed here: it is the undefined
     * behaviour under test, and invoking it to prove it exists would make this
     * test's own result meaningless. */
    const double v = make_nan();
    ASSERT_FALSE(v > (double)INT32_MAX);
    ASSERT_FALSE(v < (double)INT32_MIN);
    /* ... so the old code's third arm was selected. The range check LOOKED
     * like it covered the value and did not. */

    /* And the infinities DID satisfy a comparison, which is why they were
     * silently clamped to a plausible-looking number instead of crashing. */
    ASSERT_TRUE(make_inf() > (double)INT32_MAX);
    ASSERT_TRUE(-make_inf() < (double)INT32_MIN);
}

/* --------------------------------------------------------------------------
 * sample_emit_is_numeric — the decimal emitters' gate
 * -------------------------------------------------------------------------- */

TEST(is_numeric_refuses_every_non_finite)
{
    ASSERT_FALSE(sample_emit_is_numeric(make_nan()));
    ASSERT_FALSE(sample_emit_is_numeric(-make_nan()));
    ASSERT_FALSE(sample_emit_is_numeric(make_inf()));
    ASSERT_FALSE(sample_emit_is_numeric(-make_inf()));
}

TEST(is_numeric_accepts_ordinary_and_extreme_finites)
{
    ASSERT_TRUE(sample_emit_is_numeric(0.0));
    ASSERT_TRUE(sample_emit_is_numeric(-0.0));
    ASSERT_TRUE(sample_emit_is_numeric(1.0));
    ASSERT_TRUE(sample_emit_is_numeric(-1.0));
    ASSERT_TRUE(sample_emit_is_numeric(1e-300));
    ASSERT_TRUE(sample_emit_is_numeric(1e300));
    /* The route the issue names as reachable from a FINITE calibration value:
     * CalM = 1e307 overflows the conversion to inf on NQ1. 1e307 itself is
     * finite and emittable -- it is the PRODUCT that is not. Both must behave,
     * or the guard would reject legitimate large readings. */
    ASSERT_TRUE(sample_emit_is_numeric(1e307));
}

/* --------------------------------------------------------------------------
 * sample_emit_to_int32 — the precision-0 emitters' conversion
 * -------------------------------------------------------------------------- */

TEST(to_int32_refuses_every_non_finite_and_writes_nothing)
{
    const int32_t sentinel = 0x5A5A5A5A;
    int32_t out;

    const double bad[] = { make_nan(), -make_nan(), make_inf(), -make_inf() };
    for (unsigned i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        out = sentinel;
        ASSERT_FALSE(sample_emit_to_int32(bad[i], &out));
        /* The contract says *out is untouched on refusal. A caller that
         * emitted `out` anyway would otherwise print whatever was there. */
        ASSERT_EQ(out, sentinel);
    }
}

TEST(to_int32_clamps_finite_values_beyond_the_range)
{
    int32_t out = 0;

    /* Finite but unrepresentable -- these must CLAMP (and say so), not be
     * refused: they are real readings, merely saturated. */
    ASSERT_TRUE(sample_emit_to_int32(1e300, &out));
    ASSERT_EQ(out, INT32_MAX);

    ASSERT_TRUE(sample_emit_to_int32(-1e300, &out));
    ASSERT_EQ(out, INT32_MIN);

    ASSERT_TRUE(sample_emit_to_int32((double)INT32_MAX * 2.0, &out));
    ASSERT_EQ(out, INT32_MAX);

    ASSERT_TRUE(sample_emit_to_int32((double)INT32_MIN * 2.0, &out));
    ASSERT_EQ(out, INT32_MIN);
}

TEST(to_int32_is_exact_at_the_boundaries)
{
    int32_t out = 0;

    /* Exactly representable endpoints pass through unclamped. The cast's
     * safety here is the subtle part: the rounding step adds 0.5, so the
     * intermediate is 2147483647.5, and only because a cast truncates TOWARD
     * ZERO does it land back on INT32_MAX rather than overflowing. */
    ASSERT_TRUE(sample_emit_to_int32((double)INT32_MAX, &out));
    ASSERT_EQ(out, INT32_MAX);

    ASSERT_TRUE(sample_emit_to_int32((double)INT32_MIN, &out));
    ASSERT_EQ(out, INT32_MIN);

    /* Just inside, where the clamp must NOT fire. */
    ASSERT_TRUE(sample_emit_to_int32((double)INT32_MAX - 1.0, &out));
    ASSERT_EQ(out, INT32_MAX - 1);

    ASSERT_TRUE(sample_emit_to_int32((double)INT32_MIN + 1.0, &out));
    ASSERT_EQ(out, INT32_MIN + 1);
}

TEST(to_int32_rounds_half_away_from_zero_as_before)
{
    int32_t out = 0;
    struct { double in; int32_t want; } cases[] = {
        {  0.0,   0 }, { -0.0,   0 },
        {  0.4,   0 }, { -0.4,   0 },
        {  0.5,   1 }, { -0.5,  -1 },
        {  0.6,   1 }, { -0.6,  -1 },
        {  1.5,   2 }, { -1.5,  -2 },
        {  2.4,   2 }, { -2.4,  -2 },
        {  2.5,   3 }, { -2.5,  -3 },
        { 12.0,  12 }, { -12.0, -12 },
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        out = 0;
        ASSERT_TRUE(sample_emit_to_int32(cases[i].in, &out));
        ASSERT_EQ(out, cases[i].want);
    }
}

TEST(the_two_helpers_agree_on_what_is_emittable)
{
    /* They are separate entry points for separate emitters, so nothing in the
     * types stops them drifting apart. Any value one refuses, the other must
     * refuse. Swept across magnitudes rather than spot-checked. */
    const double vals[] = {
        make_nan(), -make_nan(), make_inf(), -make_inf(),
        0.0, -0.0, 1e-300, -1e-300, 1.0, -1.0,
        1e9, -1e9, 1e300, -1e300, 1e307, -1e307,
        (double)INT32_MAX, (double)INT32_MIN,
    };
    int32_t out;
    for (unsigned i = 0; i < sizeof(vals) / sizeof(vals[0]); i++) {
        ASSERT_EQ(sample_emit_is_numeric(vals[i]),
                  sample_emit_to_int32(vals[i], &out));
    }
}

int main(void)
{
    printf("SampleEmit.h — #1199 non-finite emitter guard\n");
    printf("-----------------------------------------------\n");
    RUN(the_old_clamp_let_nan_reach_the_cast);
    RUN(is_numeric_refuses_every_non_finite);
    RUN(is_numeric_accepts_ordinary_and_extreme_finites);
    RUN(to_int32_refuses_every_non_finite_and_writes_nothing);
    RUN(to_int32_clamps_finite_values_beyond_the_range);
    RUN(to_int32_is_exact_at_the_boundaries);
    RUN(to_int32_rounds_half_away_from_zero_as_before);
    RUN(the_two_helpers_agree_on_what_is_emittable);
    return TEST_SUMMARY();
}
