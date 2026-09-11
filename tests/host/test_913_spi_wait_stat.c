/* ==========================================================================
 * test_913_spi_wait_stat.c -- issue #913
 *
 * WHAT IS UNDER TEST
 *
 * `spi_XferByte` (firmware/src/HAL/UserSpi/UserSpi.c) used to wait for a
 * completed SPI1 byte with a pure register-poll spin bounded only by a loop
 * counter (`USER_SPI_XFER_TIMEOUT`, 2,000,000 iterations) -- zero
 * `vTaskDelay` in the file. `SYST:COMM:SPI:TRANsfer?` runs on the SCPI task
 * (USB: app_USBDeviceTask, priority 7 -- above the streaming encoder (6), the
 * USB device stack (6) and SD (5)). At the SCPI-reachable minimum baud
 * (USER_SPI_MIN_BAUD_HZ = 6000 Hz) a 237-byte frame is ~316 ms of
 * uninterrupted priority-7 spin, during which the priority-9 deferred
 * streaming task keeps filling the sample pool and nothing drains it --
 * `QueueDroppedSamples` accrues for the whole window.
 *
 * The fix (#913, design routed to and specified by an opus-tier review, see
 * the PR description) adds `spi_WaitStat`, modelled on the sibling drivers'
 * spin-then-yield pattern (`uart_WaitSta` in UserUart.c, `i2c_WaitMif` in
 * UserI2c.c): a bounded tight spin covers the fast path, then `vTaskDelay(1)`
 * against a wall-clock deadline lets streaming/SD/WiFi run while the SPI1
 * module finishes clocking the byte on its own.
 *
 * UNLIKE uart_WriteLocked (which shares ONE deadline across its whole write),
 * spi_XferByte gives every byte a FRESH deadline -- SPI is master-clocked
 * with no clock-stretching, so a byte's wire time is deterministic
 * (8 bits / USER_SPI_MIN_BAUD_HZ = 1.33 ms worst case), and the caller
 * (`spi_TransferLocked`) already `break`s at the first byte that fails. A
 * per-byte budget therefore both (a) is far more generous relative to the
 * legitimate 1.33 ms case (20 ms budget, USER_SPI_BYTE_TIMEOUT_MS) and
 * (b) reports a stuck bus after ONE budget rather than a whole-frame one.
 * See the "Verdict" section of the PR description for the full A-vs-B
 * reasoning against UART's shared-budget model.
 *
 * HOW IT IS TESTED
 *
 * UserSpi.c is not a host-test candidate -- it pulls in FreeRTOS, Harmony's
 * definitions.h/configuration.h, and the DIO ownership registry. So this file
 * re-implements `spi_WaitStat`'s loop SHAPE line-for-line against an injected
 * mock hardware-condition flag and mock tick clock (same technique as
 * `test_943_bench_stall_bound.c`'s SD stall-bound extraction), and pins the
 * two firmware constants it depends on (the fast-spin count and the timeout
 * budget) against the real source via the Makefile's grep guard, so a drift
 * in either fails the build loudly instead of silently invalidating this file.
 *
 * The property this file cares most about is the CHECK-BEFORE-DEADLINE
 * ordering: the condition is tested (inner spin + one more check) BEFORE the
 * deadline is ever consulted, in every outer-loop pass. That makes the wait
 * immune to scheduling latency -- a byte that completed while this task was
 * preempted is reported as success no matter how late it is observed, and
 * only a condition still unmet at the moment the deadline is checked can
 * produce a timeout. `bit_ready_exactly_at_deadline_still_succeeds` is the
 * test that would fail if that ordering were ever reversed (deadline checked
 * before the post-spin re-check).
 *
 * FIDELITY -- what this does NOT cover
 *
 * 1. The real SPI1STAT/SPI1BUF register semantics, the module reset-on-error
 *    path, the RX drain loop (deliberately NOT converted to this pattern --
 *    see the comment above `drainGuard` in UserSpi.c), or CS assert/deassert
 *    timing. Those need the bench (acceptance criteria in issue #913) or are
 *    argued from source (the RX-drain exemption).
 * 2. configTICK_RATE_HZ is 1000 and TickType_t is 32-bit
 *    (firmware/src/config/default/FreeRTOSConfig.h:58,126), so pdMS_TO_TICKS
 *    is the identity and one tick is one simulated millisecond here.
 * 3. The mock clock advances only inside the mock vTaskDelay -- it does not
 *    model real elapsed wall-clock time passing during the register-poll
 *    spin itself (no host analog for that duration exists; the real spin's
 *    cost is a few tens of microseconds, see the sizing comment on
 *    USER_SPI_BYTE_TIMEOUT_MS in UserSpi.c).
 * ========================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "test_framework.h"

/* --------------------------------------------------------------------------
 * Firmware constants, mirrored. Pinned against the real source by the
 * Makefile's grep guard -- if UserSpi.c changes either value the
 * $(SPI913_BIN) target fails with a message naming this file.
 * ------------------------------------------------------------------------ */
#define FW_SPIN_COUNT        8000u   /* the `s < 8000u` fast-spin bound     */
#define FW_BYTE_TIMEOUT_MS     20u   /* USER_SPI_BYTE_TIMEOUT_MS            */

/* configTICK_RATE_HZ == 1000, so pdMS_TO_TICKS is the identity here. */
#define MS_TO_TICKS(ms) ((uint32_t)(ms))

/* ==========================================================================
 * Mock environment: injected clock + injected "SPI1STAT bit" condition
 * ========================================================================== */

typedef struct {
    uint32_t now;               /* stands in for xTaskGetTickCount()        */
    uint32_t delayCalls;        /* how many times the wait slept            */
    uint32_t conditionChecks;   /* how many times the condition was polled  */
    bool     conditionMet;      /* current ((SPI1STAT & mask) != 0) == want */
    /* The condition flips true the instant one of these thresholds is hit
     * (0 = that trigger is disabled). At most one is used per test. */
    uint32_t setTrueAfterChecks;
    uint32_t setTrueAfterDelays;
} MockEnv;

static void mock_init(MockEnv *env, bool startTrue)
{
    env->now                 = 0;
    env->delayCalls          = 0;
    env->conditionChecks     = 0;
    env->conditionMet        = startTrue;
    env->setTrueAfterChecks  = 0;
    env->setTrueAfterDelays  = 0;
}

/* Stands in for `((SPI1STAT & mask) != 0u) == want`. */
static bool mock_condition_met(MockEnv *env)
{
    env->conditionChecks++;
    if (env->setTrueAfterChecks != 0u &&
        env->conditionChecks >= env->setTrueAfterChecks) {
        env->conditionMet = true;
    }
    return env->conditionMet;
}

/* Stands in for xTaskGetTickCount(). */
static uint32_t mock_tick_count(const MockEnv *env)
{
    return env->now;
}

/* Stands in for vTaskDelay(1) -- advances the mock clock by exactly one
 * simulated tick (no preemption-stretch model here, unlike #943's SD-stall
 * mock: spi_WaitStat's deadline is a per-byte fault budget, not a
 * total-runtime bound under contention, so the property under test is the
 * check-before-deadline ordering at an EXACT tick count, not a stretch
 * sensitivity). */
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
 * The loop shape, extracted line-for-line from spi_WaitStat (UserSpi.c) with
 * the SPI1STAT read / xTaskGetTickCount / vTaskDelay(1) replaced by their
 * mock counterparts. Same structure: inner bounded spin, one more check,
 * THEN the deadline test, THEN the yield.
 * ========================================================================== */
static bool spi_wait_stat_shape(MockEnv *env, uint32_t start, uint32_t timeoutTicks)
{
    for (;;) {
        for (uint32_t s = 0; s < FW_SPIN_COUNT; ++s) {
            if (mock_condition_met(env)) { return true; }
        }
        if (mock_condition_met(env)) { return true; }
        if ((uint32_t)(mock_tick_count(env) - start) >= timeoutTicks) { return false; }
        mock_delay_1_tick(env);
    }
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* Happy path, default baud: the condition is already met when the wait
 * begins (a normal fast byte at 100 kHz completes well inside the time it
 * takes to even reach the first check). Must return immediately with no
 * clock consulted and no yield -- this is the case that must never regress
 * into an unconditional yield. */
TEST(condition_already_met_returns_immediately_no_yield)
{
    MockEnv env;
    mock_init(&env, true);

    ASSERT_TRUE(spi_wait_stat_shape(&env, mock_tick_count(&env),
                                    MS_TO_TICKS(FW_BYTE_TIMEOUT_MS)));
    ASSERT_EQ(env.delayCalls, 0);
    ASSERT_EQ(env.conditionChecks, 1);   /* caught on the very first check */
    ASSERT_EQ(env.now, 0);
}

/* The condition becomes true partway through the fast spin (well inside
 * FW_SPIN_COUNT). Still no yield -- this is the "no context switch on the
 * common path" property the sizing comment on FW_SPIN_COUNT depends on. */
TEST(condition_met_mid_spin_returns_without_yield)
{
    MockEnv env;
    mock_init(&env, false);
    env.setTrueAfterChecks = 4000u;   /* inside the 8000-iteration fast spin */

    ASSERT_TRUE(spi_wait_stat_shape(&env, mock_tick_count(&env),
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

    ASSERT_TRUE(spi_wait_stat_shape(&env, mock_tick_count(&env),
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

    ASSERT_TRUE(spi_wait_stat_shape(&env, mock_tick_count(&env),
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

    ASSERT_TRUE(spi_wait_stat_shape(&env, mock_tick_count(&env),
                                    MS_TO_TICKS(FW_BYTE_TIMEOUT_MS)));
    ASSERT_EQ(env.delayCalls, 1);
    ASSERT_EQ(env.conditionChecks, FW_SPIN_COUNT + 2u);
}

/* The headline timeout case: the condition never becomes true. Must return
 * false at exactly FW_BYTE_TIMEOUT_MS elapsed -- neither early (which would
 * spuriously fail a legitimate slow byte) nor late by more than the 1-tick
 * poll granularity (which would widen the hardware-fault detection window
 * beyond what the sizing comment in UserSpi.c documents). */
TEST(condition_never_met_times_out_at_exactly_the_budget)
{
    MockEnv env;
    mock_init(&env, false);

    ASSERT_FALSE(spi_wait_stat_shape(&env, mock_tick_count(&env),
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
 * the order in spi_WaitStat (deadline check before the post-spin re-check)
 * would flip this test's result to false and leave the previous test
 * unchanged -- the two together isolate the ordering, not just the timing. */
TEST(bit_ready_exactly_at_deadline_still_succeeds)
{
    MockEnv env;
    mock_init(&env, false);
    env.setTrueAfterDelays = FW_BYTE_TIMEOUT_MS;   /* flips true on the Nth delay */

    ASSERT_TRUE(spi_wait_stat_shape(&env, mock_tick_count(&env),
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

    ASSERT_FALSE(spi_wait_stat_shape(&env, mock_tick_count(&env),
                                     MS_TO_TICKS(FW_BYTE_TIMEOUT_MS)));
    ASSERT_EQ(env.delayCalls, FW_BYTE_TIMEOUT_MS);
    ASSERT_EQ(env.now, FW_BYTE_TIMEOUT_MS);
}

/* Rollover safety: the unsigned (now - start) subtraction must still read as
 * the true elapsed count across a TickType_t wrap. Starting 16 ticks below
 * UINT32_MAX puts the wrap inside the timeout window. Mirrors
 * new_bound_survives_tick_counter_wrap in test_943_bench_stall_bound.c. */
TEST(deadline_survives_tick_counter_wrap)
{
    MockEnv env;
    uint32_t startTick = 0xFFFFFFF0u;   /* 16 below UINT32_MAX */
    mock_init(&env, false);
    env.now = startTick;

    ASSERT_FALSE(spi_wait_stat_shape(&env, startTick, MS_TO_TICKS(FW_BYTE_TIMEOUT_MS)));
    ASSERT_EQ(env.delayCalls, FW_BYTE_TIMEOUT_MS);
    ASSERT_TRUE(env.now < startTick);   /* the counter really did wrap */
    ASSERT_EQ((uint32_t)(env.now - startTick), FW_BYTE_TIMEOUT_MS);
}

int main(void)
{
    printf("#913 -- SPI yield-spin bound (spi_WaitStat loop shape)\n");
    printf("---------------------------------------------\n");
    RUN(condition_already_met_returns_immediately_no_yield);
    RUN(condition_met_mid_spin_returns_without_yield);
    RUN(condition_met_on_last_spin_iteration_no_yield);
    RUN(condition_met_on_post_spin_check_no_yield);
    RUN(condition_met_just_after_post_spin_check_takes_one_yield);
    RUN(condition_never_met_times_out_at_exactly_the_budget);
    RUN(bit_ready_exactly_at_deadline_still_succeeds);
    RUN(bit_ready_one_tick_after_deadline_times_out);
    RUN(deadline_survives_tick_counter_wrap);
    return TEST_SUMMARY();
}
