/* ==========================================================================
 * test_1154_adc_cal_coefficient_finite.c -- issue #1154
 *
 * WHAT IS UNDER TEST
 *
 * CONFigure:ADC:chanCALM / chanCALB (SCPIADC.c, ADCChanCalmSetClaimed /
 * ADCChanCalbSetClaimed) accepted ANY double for the calibration
 * coefficient, including one that overflows to +-inf, and stored it with no
 * SCPI error. Measured on an NQ1 (serial 7E2837886201026A):
 * `CONF:ADC:chanCALB 0,1e400` -- a finite-looking literal libscpi's number
 * parser overflows to +inf while lexing -- left `chanCALB? 0` reading back
 * `inf` and `SYST:ERR?` reading `0,"No error"`. volts = m*counts + b is
 * then not computable and nothing tells the client.
 *
 * SCOPE, PER #1154: the ticket is explicit that it does NOT settle what the
 * legitimate coefficient range is (USECal 2 / raw and the factory-vs-user
 * split make the field not purely physical), and this fix does not answer
 * that either -- a finite value, however large (1e300 below), is accepted
 * exactly as before. The ONLY new refusal is for a value with no defensible
 * reading at all: NaN, +inf, -inf. Rejected with -222
 * (SCPI_ERROR_DATA_OUT_OF_RANGE), the same code AdcChannelArgInRange
 * already uses for this file's other bounded setter argument (channel out
 * of range) -- see AdcCalCoefficientFinite in SCPIADC.c.
 *
 * NOT IN SCOPE (named so nobody reads this test as covering them):
 *   - The emitter half of the same underlying defect, fixed in #1149/#1144
 *     (CapJsonDouble, SCPIInterface.c) by spelling a non-finite calibration
 *     double as JSON null in CONF:CAP:JSON? rather than refusing it at the
 *     setter. That fix is not made redundant by this one: #908's boot
 *     re-stamping path (its own ticket/PR at #1077) can still put inf into
 *     CalM/CalB with NO setter call at all, so the emitter still has to
 *     stay robust regardless of this guard.
 *   - The magnitude/range question and the factory-vs-user/USECal=2 path --
 *     #1154 declines to answer those, and so does this change.
 *
 * THE LOAD-PATH QUESTION #1154 ASKS ("does rejecting break an existing
 * client? SAVEcal/LOADcal round-trip through NVM; a value already stored
 * out of range would start failing to load"): answered by reading
 * daqifi_settings.c, not by running code host-side (that file is not
 * host-compilable either -- see below). daqifi_settings_LoadADCCalSettings
 * (daqifi_settings.c:334-370) copies NVM straight into the runtime array
 * with a plain `channelRuntimeConfig->Data[x].CalM = calArray->Data[x].CalM;`
 * field assignment. It never calls AdcCalCoefficientFinite,
 * ADCChanCalmSetClaimed or ADCChanCalbSetClaimed -- so a board that already
 * has inf stored (from before this fix landed, or from #908's boot
 * re-stamping path) keeps LOADing it exactly as before. This guard sits
 * only on the two SCPI setters that parse fresh client input; it is not on
 * the shared NVM load, so it cannot make an already-stored value
 * unloadable. That is a static-reading finding (grep/read, not a host
 * test), reported in the PR body alongside this file rather than modelled
 * here.
 *
 * HOW IT IS TESTED
 *
 * Neither SCPIADC.c nor its AInRuntimeConfig/BoardConfig dependency graph is
 * host-compilable -- FreeRTOS, Harmony's configuration.h/definitions.h,
 * libscpi and the whole board/driver graph (same reason test_1112, test_985,
 * test_953 and test_943 give in their own headers). So this follows their
 * pattern in two parts:
 *
 *   1. Below, the ONE validation step #1154 adds is re-implemented over a
 *      minimal injected "channel" -- SCPI_ParamDouble having already
 *      succeeded, is the coefficient finite? If not: leave the stored value
 *      untouched and record that an error would have been queued. If so:
 *      store it, queue nothing. The channel-argument and index-resolution
 *      steps around it are UNCHANGED by this ticket and are not modelled
 *      here -- they are pre-existing coverage (AdcChannelArgInRange,
 *      test_1112) this ticket does not touch.
 *
 *   2. The Makefile guard for $(CALFINITE_BIN) pins that the REAL SCPIADC.c
 *      source still has that shape: AdcCalCoefficientFinite exists, checks
 *      isfinite(), and pushes SCPI_ERROR_DATA_OUT_OF_RANGE on rejection; and
 *      both ADCChanCalmSetClaimed and ADCChanCalbSetClaimed call it BEFORE
 *      the CalM/CalB write, so a non-finite coefficient can never reach the
 *      runtime array. That guard is what makes this a regression test on
 *      the real firmware source rather than a test of this file's own
 *      restated logic.
 * ========================================================================== */

#include "test_framework.h"
#include <math.h>

/* Mirrors ADCChanCalmSetClaimed / ADCChanCalbSetClaimed's validation order
 * for the ONE step #1154 adds: SCPI_ParamDouble has already succeeded,
 * AdcCalCoefficientFinite runs, and only on success is the coefficient
 * written -- exactly the shape the Makefile guard below pins in the real
 * source. */
typedef struct {
    double coefficient;    /* mirrors AInRuntimeConfig.CalM / .CalB */
    int    errorQueued;    /* mirrors an SCPI_ErrorPush(-222) having fired */
} FakeCalChannel;

static int SetCalCoefficient(FakeCalChannel *ch, double param2) {
    if (!isfinite(param2)) {
        ch->errorQueued = 1;
        return 0; /* SCPI_RES_ERR -- coefficient left exactly as it was */
    }
    ch->coefficient = param2;
    return 1; /* SCPI_RES_OK */
}

TEST(rejects_nan) {
    FakeCalChannel ch = { .coefficient = 1.5, .errorQueued = 0 };
    double before = ch.coefficient;
    int ok = SetCalCoefficient(&ch, NAN);
    ASSERT_FALSE(ok);
    ASSERT_TRUE(ch.errorQueued);
    ASSERT_TRUE(ch.coefficient == before);
}

TEST(rejects_positive_infinity) {
    FakeCalChannel ch = { .coefficient = 1.5, .errorQueued = 0 };
    double before = ch.coefficient;
    int ok = SetCalCoefficient(&ch, INFINITY);
    ASSERT_FALSE(ok);
    ASSERT_TRUE(ch.errorQueued);
    ASSERT_TRUE(ch.coefficient == before);
}

TEST(rejects_negative_infinity) {
    FakeCalChannel ch = { .coefficient = -2.5, .errorQueued = 0 };
    double before = ch.coefficient;
    int ok = SetCalCoefficient(&ch, -INFINITY);
    ASSERT_FALSE(ok);
    ASSERT_TRUE(ch.errorQueued);
    ASSERT_TRUE(ch.coefficient == before);
}

TEST(rejects_the_tickets_own_1e400_literal) {
    /* #1154's own worked example: `CONF:ADC:chanCALB 0,1e400` on real
     * hardware stored `inf`. 1e400 overflows `double` at compile time too
     * (GCC folds it to +inf with a diagnostic, not an error -- no -Werror
     * in this suite), so the same value libscpi's parser produces on the
     * wire is exercised here directly rather than only via INFINITY. */
    FakeCalChannel ch = { .coefficient = 0.0, .errorQueued = 0 };
    int ok = SetCalCoefficient(&ch, 1e400);
    ASSERT_FALSE(ok);
    ASSERT_TRUE(ch.errorQueued);
    ASSERT_TRUE(ch.coefficient == 0.0);
}

TEST(accepts_large_finite_value) {
    /* #1154 explicitly does NOT set a magnitude bound -- that question is
     * left open for a future ticket. 1e300 is finite and must round-trip
     * exactly, same as before this fix (and the same value #1144/#1149's
     * emitter fix used for its own "finite but huge" case). */
    FakeCalChannel ch = { .coefficient = 0.0, .errorQueued = 0 };
    int ok = SetCalCoefficient(&ch, 1e300);
    ASSERT_TRUE(ok);
    ASSERT_FALSE(ch.errorQueued);
    ASSERT_TRUE(ch.coefficient == 1e300);
}

TEST(accepts_an_ordinary_slope) {
    FakeCalChannel ch = { .coefficient = 0.0, .errorQueued = 0 };
    int ok = SetCalCoefficient(&ch, 0.0012207);
    ASSERT_TRUE(ok);
    ASSERT_FALSE(ch.errorQueued);
    ASSERT_TRUE(ch.coefficient == 0.0012207);
}

int main(void) {
    RUN(rejects_nan);
    RUN(rejects_positive_infinity);
    RUN(rejects_negative_infinity);
    RUN(rejects_the_tickets_own_1e400_literal);
    RUN(accepts_large_finite_value);
    RUN(accepts_an_ordinary_slope);
    return TEST_SUMMARY();
}
