/* ==========================================================================
 * test_ad7609_scale.c — host test for HAL/ADC/AD7609Scale.h (#889)
 *
 * #889 made AD7609_ConvertToVoltage apply the per-channel user calibration it
 * had been discarding (`UNUSED(runtimeConfig)`), so the entire NQ2/NQ3
 * calibration surface — CONF:ADC:chanCALM / chanCALB / SAVEcal / LOADcal /
 * USECal — went from inert to live. There is no NQ3 on the bench, so the
 * change lands source-verified; this suite is what makes "source-verified"
 * mean something more than reading the diff.
 *
 * It can exist at all because the arithmetic was split into AD7609Scale.h,
 * which includes nothing but <stdint.h>. The functions below are compiled
 * from the SAME text XC32 compiles into the firmware — no stubs, no mock of
 * AInConfig.h, no reimplementation. (AD7609.h itself is unusable here: it
 * pulls in state/board/AInConfig.h -> Harmony configuration.h / definitions.h
 * and the whole driver tree.)
 *
 * Two claims are under test, and they pull in opposite directions:
 *
 *   1. NOTHING CHANGES on an uncalibrated board. The shipped defaults are
 *      CalM = 1, CalB = 0 (NQ3RuntimeDefaults.c), so the new expression must
 *      be BIT-identical to the old one — checked exhaustively over all 262144
 *      codes, not sampled.
 *
 *   2. CALIBRATION ACTUALLY APPLIES, in MC12b's shape: gain multiplies the
 *      scaled value, offset is added LAST. The order matters — the same SCPI
 *      commands feed both converters, so an NQ1 and an NQ3 calibrated by one
 *      procedure have to agree. `order_is_gain_then_offset` pins this with
 *      hand-computed values that DIFFER between the two orderings, so it
 *      cannot be satisfied by a reference expression that merely copies the
 *      implementation.
 *
 * Mutation proof (required by #889): deleting `+ calB` from
 * AD7609_ScaleToVolts must fail this suite. It fails
 * cal_sweep_matches_reference, order_is_gain_then_offset and
 * offset_moves_the_result. Deleting `* calM` fails the first two and
 * gain_moves_the_result.
 *
 * Run: make -C tests/host run
 * ========================================================================== */
#include <stdio.h>
#include <stdint.h>

#include "test_framework.h"
#include "AD7609Scale.h"   /* real header (via -I firmware/src/HAL/ADC) */

/* The two module ranges the AD7609 is configured for (AInModuleRuntimeConfig
 * .Range, set from the board config). 10.0 is the default the converter falls
 * back to when the runtime module array is unreadable. */
static const double kFullScales[] = { 10.0, 5.0 };
#define N_FULLSCALES ((int)(sizeof kFullScales / sizeof kFullScales[0]))

/* Powers of two on purpose: scaled * calM is then EXACT, so the reference and
 * the implementation agree bit-for-bit whether or not the compiler contracts
 * the multiply-add into an fma. A non-power-of-two gain would make this suite
 * depend on -ffp-contract, which is not what it is trying to measure. */
static const double kCalM[] = { 0.5, 1.0, 2.0 };
static const double kCalB[] = { -1.0, 0.0, 1.0 };
#define N_CALM ((int)(sizeof kCalM / sizeof kCalM[0]))
#define N_CALB ((int)(sizeof kCalB / sizeof kCalB[0]))

/* The signed 18-bit code space, in full. */
#define CODE_MIN (-131072)
#define CODE_MAX ( 131071)

/* Reference for the pre-#889 (uncalibrated) expression, exactly as it read in
 * AD7609.c before the change: raw / maxCode * fullScale. */
static double reference_uncalibrated(int32_t code, double fullScale)
{
    return ((double)code / (double)AD7609_MAX_VALUE) * fullScale;
}

/* ---------------------------------------------------------------------------
 * Decode: every raw 18-bit pattern maps to the right signed value, and bits
 * above [17:0] are masked off rather than corrupting the sign.
 * ------------------------------------------------------------------------- */
TEST(decode_covers_the_whole_code_space)
{
    int bad = 0;

    for (uint32_t raw = 0; raw <= 0x3FFFFu; raw++) {
        /* Independent reference: two's complement over 18 bits. */
        int32_t expect = (raw < 0x20000u) ? (int32_t)raw
                                          : (int32_t)raw - 0x40000;
        int32_t got = AD7609_DecodeSignedCode(raw);

        if (got != expect) {
            if (bad < 5) {
                printf("    decode(0x%05X) = %ld, expected %ld\n",
                       (unsigned)raw, (long)got, (long)expect);
            }
            bad++;
        }

        /* Upper bits must be ignored, not sign-corrupting. A value that was
         * already sign-extended once and stored in a 32-bit field (as
         * AD7609_ReadSamples does) round-trips through here. */
        if (AD7609_DecodeSignedCode(raw | 0xFFFC0000u) != expect) {
            bad++;
        }
    }

    ASSERT_EQ(bad, 0);
    ASSERT_EQ(AD7609_DecodeSignedCode(0u), 0);
    ASSERT_EQ(AD7609_DecodeSignedCode((uint32_t)AD7609_MAX_VALUE), CODE_MAX);
    ASSERT_EQ(AD7609_DecodeSignedCode((uint32_t)AD7609_SIGN_BIT), CODE_MIN);
    ASSERT_EQ(AD7609_DecodeSignedCode(0x3FFFFu), -1);
}

/* ---------------------------------------------------------------------------
 * Claim 1: with the shipped defaults the fix is a no-op, exactly.
 *
 * Exhaustive over the code space and both module ranges. Bit-identity is the
 * bar, so the comparison is `==` with no tolerance: x * 1.0 is exact for
 * every finite x, and code 0 scales to +0.0 (never -0.0), which is the only
 * value for which `+ 0.0` would be observable.
 * ------------------------------------------------------------------------- */
TEST(identity_cal_is_bit_identical_to_the_old_expression)
{
    int bad = 0;
    long checked = 0;

    for (int f = 0; f < N_FULLSCALES; f++) {
        for (int32_t code = CODE_MIN; code <= CODE_MAX; code++) {
            double want = reference_uncalibrated(code, kFullScales[f]);
            double got  = AD7609_ScaleToVolts(code, kFullScales[f], 1.0, 0.0);
            checked++;

            if (got != want) {
                if (bad < 5) {
                    printf("    fs=%g code=%ld: got %.17g want %.17g\n",
                           kFullScales[f], (long)code, got, want);
                }
                bad++;
            }
        }
    }

    printf("    (%ld code/range pairs checked)\n", checked);
    ASSERT_EQ(bad, 0);
}

/* ---------------------------------------------------------------------------
 * Claim 2a: the calibrated result matches the intended formula across the
 * whole code space and the full {gain} x {offset} grid.
 * ------------------------------------------------------------------------- */
TEST(cal_sweep_matches_reference)
{
    int bad = 0;
    long checked = 0;

    for (int f = 0; f < N_FULLSCALES; f++) {
        for (int m = 0; m < N_CALM; m++) {
            for (int b = 0; b < N_CALB; b++) {
                for (int32_t code = CODE_MIN; code <= CODE_MAX; code++) {
                    double want = reference_uncalibrated(code, kFullScales[f])
                                  * kCalM[m] + kCalB[b];
                    double got  = AD7609_ScaleToVolts(code, kFullScales[f],
                                                      kCalM[m], kCalB[b]);
                    checked++;

                    if (got != want) {
                        if (bad < 5) {
                            printf("    fs=%g m=%g b=%g code=%ld: "
                                   "got %.17g want %.17g\n",
                                   kFullScales[f], kCalM[m], kCalB[b],
                                   (long)code, got, want);
                        }
                        bad++;
                    }
                }
            }
        }
    }

    printf("    (%ld combinations checked)\n", checked);
    ASSERT_EQ(bad, 0);
}

/* ---------------------------------------------------------------------------
 * Claim 2b: THE ORDER. gain * scaled + offset, not (scaled + offset) * gain.
 *
 * Hand-computed, and deliberately NOT derived from the implementation's own
 * expression — a reference that copies the code under test cannot detect a
 * transposed order. Codes are chosen so the intermediate is exact:
 * code = +/-AD7609_MAX_VALUE scales to exactly +/-fullScale.
 *
 * Both orderings are shown so the difference is visible in the source:
 *   fs=10, m=2, b=1, code=+MAX -> 10*2+1 = 21   vs   (10+1)*2 = 22
 *                     code=-MAX -> -10*2+1 = -19 vs  (-10+1)*2 = -18
 *                     code=0    -> 0*2+1 = 1     vs   (0+1)*2 = 2
 * ------------------------------------------------------------------------- */
TEST(order_is_gain_then_offset)
{
    /* Exactly representable, so `==` is the right comparison. */
    ASSERT_TRUE(AD7609_ScaleToVolts(CODE_MAX, 10.0, 2.0, 1.0) == 21.0);
    ASSERT_TRUE(AD7609_ScaleToVolts(-CODE_MAX, 10.0, 2.0, 1.0) == -19.0);
    ASSERT_TRUE(AD7609_ScaleToVolts(0, 10.0, 2.0, 1.0) == 1.0);

    /* The transposed order would give 22 / -18 / 2 respectively. */
    ASSERT_TRUE(AD7609_ScaleToVolts(CODE_MAX, 10.0, 2.0, 1.0) != 22.0);
    ASSERT_TRUE(AD7609_ScaleToVolts(-CODE_MAX, 10.0, 2.0, 1.0) != -18.0);
    ASSERT_TRUE(AD7609_ScaleToVolts(0, 10.0, 2.0, 1.0) != 2.0);

    /* Halving gain, negative offset, the 5 V range. */
    ASSERT_TRUE(AD7609_ScaleToVolts(CODE_MAX, 5.0, 0.5, -1.0) == 1.5);
    ASSERT_TRUE(AD7609_ScaleToVolts(-CODE_MAX, 5.0, 0.5, -1.0) == -3.5);
}

/* ---------------------------------------------------------------------------
 * Mutation sentinels. These are the tests that fail loudly if a future edit
 * drops a term — the failure mode #889 exists to fix in the first place.
 * ------------------------------------------------------------------------- */
TEST(offset_moves_the_result)
{
    /* Deleting `+ calB` makes each pair below equal. */
    for (int32_t code = CODE_MIN; code <= CODE_MAX; code += 4099) {
        double zero_b = AD7609_ScaleToVolts(code, 10.0, 1.0, 0.0);
        ASSERT_TRUE(AD7609_ScaleToVolts(code, 10.0, 1.0, 1.0) != zero_b);
        ASSERT_TRUE(AD7609_ScaleToVolts(code, 10.0, 1.0, -1.0) != zero_b);
    }
}

TEST(gain_moves_the_result)
{
    /* Deleting `* calM` makes each pair below equal (code 0 excepted, where
     * the scaled value is 0 and any gain gives the same answer). */
    for (int32_t code = CODE_MIN; code <= CODE_MAX; code += 4099) {
        if (code == 0) {
            continue;
        }
        double unity_m = AD7609_ScaleToVolts(code, 10.0, 1.0, 0.0);
        ASSERT_TRUE(AD7609_ScaleToVolts(code, 10.0, 2.0, 0.0) != unity_m);
        ASSERT_TRUE(AD7609_ScaleToVolts(code, 10.0, 0.5, 0.0) != unity_m);
    }
}

int main(void)
{
    printf("AD7609Scale.h — #889 calibration arithmetic\n");
    printf("---------------------------------------------\n");
    RUN(decode_covers_the_whole_code_space);
    RUN(identity_cal_is_bit_identical_to_the_old_expression);
    RUN(cal_sweep_matches_reference);
    RUN(order_is_gain_then_offset);
    RUN(offset_moves_the_result);
    RUN(gain_moves_the_result);
    return TEST_SUMMARY();
}
