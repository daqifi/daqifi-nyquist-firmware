/* ==========================================================================
 * test_1057_dac7718_wait_stat.c -- issue #1057 (#913's twin)
 *
 * WHAT IS UNDER TEST
 *
 * `DAC7718_ReadWriteReg` (firmware/src/HAL/DAC7718/DAC7718.c) used to wait
 * for SPI2 register conditions (TX buffer empty, RX buffer not empty, shift
 * register empty) with SIX pure register-poll spins, each bounded only by a
 * loop counter (`DAC7718_SPI_TIMEOUT`, 100,000 iterations) -- zero
 * `vTaskDelay` in the file. The function is reached from the SCPI command
 * path (`SCPIDAC.c`, `SCPI_DACVoltageSet`'s write); USB SCPI runs on
 * app_USBDeviceTask at priority 7 -- above the streaming encoder (6), the
 * USB device stack (6) and SD (5). A stuck SPI2 peripheral could hold that
 * priority-7 task in an unyielding spin for the whole 100,000-iteration
 * budget, once per call site, starving everything below it -- the same
 * mechanism #913 described for UserSpi.c's spi_XferByte.
 *
 * The fix (#1057) adds `dac7718_WaitStat`, modelled directly on
 * UserSpi.c's `spi_WaitStat` (#913): a bounded tight spin covers the fast
 * path (DAC7718's SPI2 is a FIXED ~7 MHz clock -- SPI2BRG=5 off PBCLK2
 * 84 MHz -- so the worst legitimate byte is ~1.14 us, caught by the spin on
 * every real transfer), then `vTaskDelay(1)` against a wall-clock deadline
 * lets streaming/SD/WiFi run while SPI2 finishes on its own. All three of
 * DAC7718's wait shapes (TX-buffer-empty, RX-buffer-not-empty, and
 * shift-register-empty, the last previously read via the PLIB helper
 * `SPI2_IsTransmitterBusy()`) route through this ONE helper, so this file
 * models exactly one shape rather than the three separate ones #913 had to
 * cover for UserSpi/UserUart/UserI2c.
 *
 * HOW IT IS TESTED
 *
 * DAC7718.c is not a host-test candidate -- it pulls in FreeRTOS, Harmony's
 * definitions.h/configuration.h, and the GPIO/SPI2 PLIBs. So this file
 * re-implements `dac7718_WaitStat`'s loop SHAPE line-for-line against an
 * injected mock hardware-condition flag and mock tick clock -- the identical
 * technique test_913_spi_wait_stat.c uses for spi_WaitStat, because the two
 * functions are the same shape by design (#1057 explicitly mirrors #913
 * rather than inventing a second one). The two firmware constants this
 * shape depends on, and the SHAPE itself (see the warning below), are
 * pinned against the real source via the Makefile's grep guards, so drift
 * in either fails the build loudly instead of silently invalidating this
 * file's premise.
 *
 * The property this file cares most about is the same one #913 cares about:
 * the CHECK-BEFORE-DEADLINE ordering. The condition is tested (inner spin +
 * one more check) BEFORE the deadline is ever consulted, in every outer-loop
 * pass, AND ONE MORE TIME, FRESH, at the moment the deadline actually fires
 * -- so a status change that happens while this task was preempted is
 * reported as success no matter how late it is observed, including in the
 * narrow gap between the post-spin check and the deadline test itself. Only
 * a condition still unmet at that final fresh read can produce a timeout.
 * `condition_true_only_in_the_preemption_gap_is_still_caught` is the test
 * that isolates this specifically, against the MODELLED shape below.
 *
 * ⚠️ THIS FILE CANNOT, BY ITSELF, CATCH A REGRESSION IN THE REAL FIRMWARE.
 * `dac7718_wait_stat_shape` below is a hand-written copy of `dac7718_WaitStat`
 * (DAC7718.c is not includable on the host -- see "HOW IT IS TESTED" above),
 * so reverting the real function's deadline branch to the pre-opus-review
 * #913 bug (bare `return false;`, or deleting `dac7718_WaitStat` entirely)
 * leaves every test in THIS file green -- verified directly (opus review of
 * #1057): both mutations built and ran clean against this suite. What
 * catches that class of regression is the Makefile's `$(DAC1057_BIN)` recipe,
 * which greps the real source for the fast-spin loop's shape, the exact
 * fresh-read return statement, and a call-site count (1 definition + 6
 * sites) -- not this file's own assertions. This is the same gap
 * `test_913_spi_wait_stat.c` has for `spi_WaitStat` (its file header makes
 * the identical claim this file used to, and it is equally untrue there);
 * recording it here rather than re-deriving it a third time whenever this
 * shape gets a fourth copy (#1056).
 *
 * FIDELITY -- what this does NOT cover
 *
 * 1. The real SPI2STAT/SPI2BUF register semantics, the DAC7718 command/NOP
 *    framing, CS assert/deassert timing, or the mutex-held-across-yield
 *    interaction with SCPIDAC.c's gDacCommandMutex. Those need the bench
 *    (the falsifier in issue #1057) or are argued from source (the
 *    SPI2_IsTransmitterBusy()-to-register-test substitution, see the
 *    comment above dac7718_WaitStat in DAC7718.c).
 * 2. configTICK_RATE_HZ is 1000 and TickType_t is 32-bit
 *    (firmware/src/config/default/FreeRTOSConfig.h:58,126), so pdMS_TO_TICKS
 *    is the identity and one tick is one simulated millisecond here -- same
 *    assumption as test_913_spi_wait_stat.c.
 * 3. The mock clock advances only inside the mock vTaskDelay -- it does not
 *    model real elapsed wall-clock time passing during the register-poll
 *    spin itself (the real spin's cost is a few tens of microseconds, see
 *    the sizing comment on DAC7718_SPI_BYTE_TIMEOUT_MS in DAC7718.c).
 * ========================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "test_framework.h"

/* Firmware constants, mirrored. Pinned against the real source by the
 * Makefile's grep guard -- if DAC7718.c changes either value the
 * $(DAC1057_BIN) target fails with a message naming this file. */
#define FW_SPIN_COUNT        8000u   /* the `s < DAC7718_SPI_FAST_SPIN_COUNT` fast-spin bound */
#define FW_BYTE_TIMEOUT_MS     20u   /* DAC7718_SPI_BYTE_TIMEOUT_MS */

/* configTICK_RATE_HZ == 1000, so pdMS_TO_TICKS is the identity here. */
#define MS_TO_TICKS(ms) ((uint32_t)(ms))

/* ==========================================================================
 * Mock environment: injected clock + injected "SPI2STAT bit" condition
 * ========================================================================== */

typedef struct {
    uint32_t now;               /* stands in for xTaskGetTickCount()        */
    uint32_t delayCalls;        /* how many times the wait slept            */
    uint32_t conditionChecks;   /* how many times the condition was polled  */
    bool     conditionMet;      /* current ((SPI2STAT & mask) != 0) == want */
    /* The condition flips true the instant one of these thresholds is hit
     * (0 = that trigger is disabled). At most one is used per test. */
    uint32_t setTrueAfterChecks;
    uint32_t setTrueAfterDelays;
    /* Models the preemption gap between the last ordinary check and the
     * deadline test: this task can be preempted for the whole remaining
     * budget in that gap, so the hardware condition can go true without any
     * mock_condition_met() call ever observing it. A synchronous mock has no
     * wall clock independent of explicit check/delay calls, so this flag
     * stands in for "true by the time the deadline branch takes its fresh
     * read" -- consulted ONLY by mock_condition_met_at_deadline(), never by
     * the ordinary spin/post-spin checks, so it cannot be caught any other
     * way. Same technique as test_913_spi_wait_stat.c's trueAtDeadlineCheck. */
    bool     trueAtDeadlineCheck;
} MockEnv;

static void mock_init(MockEnv *env, bool startTrue)
{
    env->now                  = 0;
    env->delayCalls           = 0;
    env->conditionChecks      = 0;
    env->conditionMet         = startTrue;
    env->setTrueAfterChecks   = 0;
    env->setTrueAfterDelays   = 0;
    env->trueAtDeadlineCheck  = false;
}

/* Stands in for `((SPI2STAT & mask) != 0u) == want`. */
static bool mock_condition_met(MockEnv *env)
{
    env->conditionChecks++;
    if (env->setTrueAfterChecks != 0u &&
        env->conditionChecks >= env->setTrueAfterChecks) {
        env->conditionMet = true;
    }
    return env->conditionMet;
}

/* Stands in for the FRESH read dac7718_WaitStat takes at deadline expiry
 * instead of trusting the pre-check above it. See trueAtDeadlineCheck's
 * comment for why this needs its own trigger rather than reusing
 * mock_condition_met(). */
static bool mock_condition_met_at_deadline(MockEnv *env)
{
    env->conditionChecks++;
    return env->conditionMet || env->trueAtDeadlineCheck;
}

/* Stands in for xTaskGetTickCount(). */
static uint32_t mock_tick_count(const MockEnv *env)
{
    return env->now;
}

/* Stands in for vTaskDelay(1) -- advances the mock clock by exactly one
 * simulated tick. */
static void mock_delay_1_tick(MockEnv *env)
{
    env->delayCalls++;
    env->now += 1u;
    if (env->setTrueAfterDelays != 0u &&
        env->delayCalls >= env->setTrueAfterDelays) {
        env->conditionMet = true;
    }
}

/* ==========================================================================
 * The loop shape, extracted line-for-line from dac7718_WaitStat (DAC7718.c)
 * with the SPI2STAT read / xTaskGetTickCount / vTaskDelay(1) replaced by
 * their mock counterparts. The fast spin runs ONCE (a Qodo /improve finding
 * on this PR, applied to DAC7718.c -- the ORIGINAL nested-per-retry shape
 * this mirrored from spi_WaitStat re-spun 8000 iterations after every single
 * vTaskDelay(1) wake, which burns CPU on the fault path for no additional
 * detection value: a level-sensitive status bit is caught by one check per
 * tick exactly as reliably as by a fresh 8000-iteration re-spin). After the
 * one-shot spin, the retry loop does a single check, THEN the deadline test
 * -- which takes a FRESH read rather than trusting that check above it, so a
 * condition that goes true only in the gap between them is still caught --
 * THEN the yield. Every existing test below still exercises the same
 * boundaries: none of them require a SECOND full spin to reach their
 * trigger point, so hoisting the spin out of the retry loop changes no
 * test's expected counts (verified by running the suite unchanged after the
 * source change -- see the file header). */
static bool dac7718_wait_stat_shape(MockEnv *env, uint32_t start, uint32_t timeoutTicks)
{
    for (uint32_t s = 0; s < FW_SPIN_COUNT; ++s) {
        if (mock_condition_met(env)) { return true; }
    }
    for (;;) {
        if (mock_condition_met(env)) { return true; }
        if ((uint32_t)(mock_tick_count(env) - start) >= timeoutTicks) {
            return mock_condition_met_at_deadline(env);
        }
        mock_delay_1_tick(env);
    }
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* Happy path: the condition is already met when the wait begins (DAC7718's
 * fixed ~7 MHz SPI2 clock completes a byte in ~1.14 us, well inside the time
 * it takes to even reach the first check). Must return immediately with no
 * clock consulted and no yield -- this is the case that must never regress
 * into an unconditional yield. */
TEST(condition_already_met_returns_immediately_no_yield)
{
    MockEnv env;
    mock_init(&env, true);

    ASSERT_TRUE(dac7718_wait_stat_shape(&env, mock_tick_count(&env),
                                         MS_TO_TICKS(FW_BYTE_TIMEOUT_MS)));
    ASSERT_EQ(env.delayCalls, 0);
    ASSERT_EQ(env.conditionChecks, 1);   /* caught on the very first check */
    ASSERT_EQ(env.now, 0);
}

/* The condition becomes true partway through the fast spin (well inside
 * FW_SPIN_COUNT). Still no yield -- the "no context switch on the common
 * path" property the sizing comment on DAC7718_SPI_FAST_SPIN_COUNT depends
 * on. */
TEST(condition_met_mid_spin_returns_without_yield)
{
    MockEnv env;
    mock_init(&env, false);
    env.setTrueAfterChecks = 4000u;   /* inside the 8000-iteration fast spin */

    ASSERT_TRUE(dac7718_wait_stat_shape(&env, mock_tick_count(&env),
                                         MS_TO_TICKS(FW_BYTE_TIMEOUT_MS)));
    ASSERT_EQ(env.delayCalls, 0);
    ASSERT_EQ(env.conditionChecks, 4000);
}

/* Pins FW_SPIN_COUNT exactly: the condition becomes true on the LAST
 * iteration of the fast spin (check #8000). Must still be caught without a
 * yield -- an off-by-one that shrinks the spin bound would turn this into a
 * (harmless but silently-slower) yield, which this test converts into a
 * loud assertion failure instead. */
TEST(condition_met_on_last_spin_iteration_no_yield)
{
    MockEnv env;
    mock_init(&env, false);
    env.setTrueAfterChecks = FW_SPIN_COUNT;

    ASSERT_TRUE(dac7718_wait_stat_shape(&env, mock_tick_count(&env),
                                         MS_TO_TICKS(FW_BYTE_TIMEOUT_MS)));
    ASSERT_EQ(env.delayCalls, 0);
    ASSERT_EQ(env.conditionChecks, FW_SPIN_COUNT);
}

/* Pins the "one more check" after the spin: the condition becomes true on
 * check #(FW_SPIN_COUNT + 1) -- past the inner spin, caught by the single
 * post-spin re-check, still with zero yields. */
TEST(condition_met_on_post_spin_check_no_yield)
{
    MockEnv env;
    mock_init(&env, false);
    env.setTrueAfterChecks = FW_SPIN_COUNT + 1u;

    ASSERT_TRUE(dac7718_wait_stat_shape(&env, mock_tick_count(&env),
                                         MS_TO_TICKS(FW_BYTE_TIMEOUT_MS)));
    ASSERT_EQ(env.delayCalls, 0);
    ASSERT_EQ(env.conditionChecks, FW_SPIN_COUNT + 1u);
}

/* One past that (check #FW_SPIN_COUNT + 2): both the inner spin and the
 * post-spin check miss it, so the wait falls through to the deadline test
 * (not yet expired) and takes exactly ONE yield before the next pass's first
 * check catches it. Confirms the outer retry loop actually loops. */
TEST(condition_met_just_after_post_spin_check_takes_one_yield)
{
    MockEnv env;
    mock_init(&env, false);
    env.setTrueAfterChecks = FW_SPIN_COUNT + 2u;

    ASSERT_TRUE(dac7718_wait_stat_shape(&env, mock_tick_count(&env),
                                         MS_TO_TICKS(FW_BYTE_TIMEOUT_MS)));
    ASSERT_EQ(env.delayCalls, 1);
    ASSERT_EQ(env.conditionChecks, FW_SPIN_COUNT + 2u);
}

/* The headline timeout case: the condition never becomes true. Must return
 * false at exactly FW_BYTE_TIMEOUT_MS elapsed -- neither early (which would
 * spuriously fail a legitimate transfer under scheduling jitter) nor late by
 * more than the 1-tick poll granularity (which would widen the
 * hardware-fault detection window beyond what the sizing comment in
 * DAC7718.c documents). */
TEST(condition_never_met_times_out_at_exactly_the_budget)
{
    MockEnv env;
    mock_init(&env, false);

    ASSERT_FALSE(dac7718_wait_stat_shape(&env, mock_tick_count(&env),
                                          MS_TO_TICKS(FW_BYTE_TIMEOUT_MS)));
    ASSERT_EQ(env.delayCalls, FW_BYTE_TIMEOUT_MS);
    ASSERT_EQ(env.now, FW_BYTE_TIMEOUT_MS);   /* exact: 1 tick per poll, no overshoot */
}

/* THE load-bearing ordering property (see the file header). The condition
 * becomes true on exactly the delay call that pushes the clock to the
 * deadline -- i.e. by the time the NEXT pass's checks run, elapsed already
 * equals the timeout. The wait still returns TRUE, because both condition
 * checks in that pass happen before the deadline is re-consulted.
 *
 * Pair this with the previous test: identical delayCalls (FW_BYTE_TIMEOUT_MS)
 * and identical elapsed clock (FW_BYTE_TIMEOUT_MS), but the OUTCOME differs
 * purely on whether the condition ever became true -- which is exactly the
 * "immune to scheduling latency" guarantee the design relies on. Reversing
 * the order in dac7718_WaitStat (deadline check before the post-spin
 * re-check) would flip this test's result to false and leave the previous
 * test unchanged -- the two together isolate the ordering, not just the
 * timing. */
TEST(bit_ready_exactly_at_deadline_still_succeeds)
{
    MockEnv env;
    mock_init(&env, false);
    env.setTrueAfterDelays = FW_BYTE_TIMEOUT_MS;   /* flips true on the Nth delay */

    ASSERT_TRUE(dac7718_wait_stat_shape(&env, mock_tick_count(&env),
                                         MS_TO_TICKS(FW_BYTE_TIMEOUT_MS)));
    ASSERT_EQ(env.delayCalls, FW_BYTE_TIMEOUT_MS);
    ASSERT_EQ(env.now, FW_BYTE_TIMEOUT_MS);
}

/* One delay later than the previous test: the condition becomes true only
 * AFTER the deadline has already been consulted and found expired, so it
 * never gets the chance. Confirms the boundary is exactly one tick wide --
 * the previous test is not accidentally passing because the deadline check
 * is simply never reached. */
TEST(bit_ready_one_tick_after_deadline_times_out)
{
    MockEnv env;
    mock_init(&env, false);
    env.setTrueAfterDelays = FW_BYTE_TIMEOUT_MS + 1u;

    ASSERT_FALSE(dac7718_wait_stat_shape(&env, mock_tick_count(&env),
                                          MS_TO_TICKS(FW_BYTE_TIMEOUT_MS)));
    ASSERT_EQ(env.delayCalls, FW_BYTE_TIMEOUT_MS);
    ASSERT_EQ(env.now, FW_BYTE_TIMEOUT_MS);
}

/* THE property #1057 exists to carry over from #913's opus-review fix: a
 * condition that goes true in the narrow gap between the post-spin check
 * and the deadline test -- unreachable by any mock_condition_met() call, see
 * trueAtDeadlineCheck's comment -- must still be caught, because the
 * deadline branch takes a FRESH read instead of returning false
 * unconditionally. This fails against `dac7718_wait_stat_shape` (this file's
 * OWN model) if that model is hand-copied from the ORIGINAL (pre-opus-fix)
 * #913 shape instead of the corrected one -- exactly the risk of a fourth
 * hand-copy the #1057 issue body warns about. It does NOT fail if the REAL
 * firmware regresses to that shape while this model stays correct -- see the
 * "CANNOT, BY ITSELF, CATCH" warning in the file header; the Makefile's
 * fresh-read grep is what closes that gap. */
TEST(condition_true_only_in_the_preemption_gap_is_still_caught)
{
    MockEnv env;
    mock_init(&env, false);
    env.trueAtDeadlineCheck = true;   /* never seen by an ordinary check */

    ASSERT_TRUE(dac7718_wait_stat_shape(&env, mock_tick_count(&env),
                                         MS_TO_TICKS(FW_BYTE_TIMEOUT_MS)));
    /* Spends the full budget in yields -- the condition is never visible to
     * an ordinary check, only to the deadline branch's fresh read -- then
     * succeeds on that fresh read rather than timing out. */
    ASSERT_EQ(env.delayCalls, FW_BYTE_TIMEOUT_MS);
    ASSERT_EQ(env.now, FW_BYTE_TIMEOUT_MS);
}

/* Companion negative: with trueAtDeadlineCheck left false (mock_init's
 * default), the fresh read at expiry must still return false when the
 * condition genuinely never became true -- the fix closes a false-timeout
 * window, it must not open a false-success one. Same scenario as
 * condition_never_met_times_out_at_exactly_the_budget, restated here to sit
 * next to its positive counterpart. */
TEST(condition_still_false_at_deadline_check_truly_times_out)
{
    MockEnv env;
    mock_init(&env, false);

    ASSERT_FALSE(dac7718_wait_stat_shape(&env, mock_tick_count(&env),
                                          MS_TO_TICKS(FW_BYTE_TIMEOUT_MS)));
    ASSERT_EQ(env.delayCalls, FW_BYTE_TIMEOUT_MS);
    ASSERT_EQ(env.now, FW_BYTE_TIMEOUT_MS);
}

/* Rollover safety: the unsigned (now - start) subtraction must still read as
 * the true elapsed count across a TickType_t wrap. Starting 16 ticks below
 * UINT32_MAX puts the wrap inside the timeout window. Mirrors
 * deadline_survives_tick_counter_wrap in test_913_spi_wait_stat.c. */
TEST(deadline_survives_tick_counter_wrap)
{
    MockEnv env;
    uint32_t startTick = 0xFFFFFFF0u;   /* 16 below UINT32_MAX */
    mock_init(&env, false);
    env.now = startTick;

    ASSERT_FALSE(dac7718_wait_stat_shape(&env, startTick, MS_TO_TICKS(FW_BYTE_TIMEOUT_MS)));
    ASSERT_EQ(env.delayCalls, FW_BYTE_TIMEOUT_MS);
    ASSERT_TRUE(env.now < startTick);   /* the counter really did wrap */
    ASSERT_EQ((uint32_t)(env.now - startTick), FW_BYTE_TIMEOUT_MS);
}

int main(void)
{
    printf("#1057 -- DAC7718 SPI2 yield-spin bound (dac7718_WaitStat loop shape, #913's twin)\n");
    printf("---------------------------------------------\n");
    RUN(condition_already_met_returns_immediately_no_yield);
    RUN(condition_met_mid_spin_returns_without_yield);
    RUN(condition_met_on_last_spin_iteration_no_yield);
    RUN(condition_met_on_post_spin_check_no_yield);
    RUN(condition_met_just_after_post_spin_check_takes_one_yield);
    RUN(condition_never_met_times_out_at_exactly_the_budget);
    RUN(bit_ready_exactly_at_deadline_still_succeeds);
    RUN(bit_ready_one_tick_after_deadline_times_out);
    RUN(condition_true_only_in_the_preemption_gap_is_still_caught);
    RUN(condition_still_false_at_deadline_check_truly_times_out);
    RUN(deadline_survives_tick_counter_wrap);
    return TEST_SUMMARY();
}
