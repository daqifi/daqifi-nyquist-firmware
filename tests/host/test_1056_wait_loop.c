/* ==========================================================================
 * test_1056_wait_loop.c -- issue #1056 (the durable fix for #913's test),
 * extended by #1108/#1109 for WaitLoop_HoistedSpinThenYield
 *
 * WHAT IS UNDER TEST
 *
 * firmware/src/HAL/WaitLoop.h's WaitLoop_SpinThenYield AND
 * WaitLoop_HoistedSpinThenYield -- THE REAL ONES. This file `#include`s the
 * shipped header and calls the shipped functions; the only things mocked are
 * the three things the drivers inject (the status read, the budget test, the
 * yield). There is no re-implementation here, and therefore nothing to drift.
 * WaitLoop_SpinThenYield's coverage is below; WaitLoop_HoistedSpinThenYield's
 * is in its own block further down, with the #1109 regression story and its
 * own mutation coverage.
 *
 * #913 gave UserSpi.c's spi_XferByte a bounded spin-then-vTaskDelay(1) wait so
 * `SYST:COMM:SPI:TRANsfer?` would stop busy-spinning at the SCPI task's
 * priority (USB: app_USBDeviceTask, priority 7 -- above the streaming encoder
 * (6), the USB device stack (6) and SD (5)) while the SPI1 module clocked a
 * slow byte. At the SCPI-reachable minimum baud (USER_SPI_MIN_BAUD_HZ = 6000)
 * a 237-byte frame was ~316 ms of uninterrupted priority-7 spin, during which
 * the priority-9 deferred streaming task kept filling the sample pool and
 * nothing drained it.
 *
 * WHY THIS FILE REPLACED test_913_spi_wait_stat.c (#1056)
 *
 * That file could not include UserSpi.c -- Harmony PLIBs, FreeRTOS, the DIO
 * ownership registry -- so it re-implemented spi_WaitStat's loop line-for-line
 * and pinned a handful of constants with Makefile greps. It therefore proved
 * the shape's BEHAVIOUR and nothing about whether the firmware still had that
 * shape: delete the fresh register read at expiry, or move the deadline test
 * after the yield, and every one of its tests stayed green. A textual shape
 * checker was built for exactly that and reached five generations before being
 * deleted (the block above $(WAITLOOP_BIN) in the Makefile records why).
 *
 * #1056 took #889's route instead -- split the logic into a header the host
 * test compiles for real, the way AD7609Scale.h came out of AD7609.h. The loop
 * now exists once, and the three drivers (spi_WaitStat, uart_WaitSta,
 * i2c_WaitMif) are thin wrappers that inject their own callbacks. So the
 * falsifier the ticket asked for holds: mutate WaitLoop.h and this suite goes
 * RED. Verified by actually doing it -- see MUTATION COVERAGE below.
 *
 * MUTATION COVERAGE. Each of these was applied to the real WaitLoop.h, the
 * suite re-run, and the mutation reverted (2026-09-16, gcc 13.3). The named
 * test is the one that isolates that mutation; the count is how many of the
 * 15 went red in total.
 *
 *   1. Delete the fresh status read at expiry (`return statusMet(ctx);` ->
 *      `return false;`) -- the exact defect an opus review of #913 caught.
 *        => status_true_only_in_the_preemption_gap_is_still_caught FAILS.
 *           (4 failed)
 *   2. Move the budget test AFTER the yield.
 *        => budget_already_spent_on_entry_never_yields FAILS -- it sleeps once
 *           before noticing it had no budget left. (4 failed)
 *   3. Drop the yield entirely.
 *        => the mock clock never advances, so the loop cannot terminate. The
 *           runaway valve (MOCK_MAX_STATUS_READS) breaks it and trips
 *           `runaway`, which EVERY test asserts against -- a red suite rather
 *           than a hung one. (9 failed)
 *   4. Drop the single post-spin status read.
 *        => status_met_on_post_spin_check_no_yield FAILS -- it takes a yield.
 *           (6 failed)
 *   5. Consult the budget BEFORE the post-spin status read.
 *        => budget_already_spent_on_entry_never_yields FAILS on its read count
 *           (SPIN + 1 instead of SPIN + 2). This is the only mutation that
 *           changes nothing but that one count -- both readings still take a
 *           fresh read at expiry and still never yield. (4 failed)
 *   6. Off-by-one the spin bound, one iteration LONGER (`s <= spinCount`).
 *        => status_met_just_after_post_spin_check_takes_one_yield FAILS -- the
 *           extra iteration swallows the read that should have cost a yield.
 *           NOT status_met_on_last_spin_iteration_no_yield, and NOT
 *           status_met_on_post_spin_check_no_yield: a read that used to land
 *           on the post-spin check now lands on the extra spin iteration, at
 *           the same read NUMBER and still with no yield. A boundary test one
 *           side of the bound cannot see a bound that only grew; it takes the
 *           one two past it. (7 failed)
 *   7. The same off-by-one one iteration SHORTER (`s + 1u < spinCount`).
 *        => status_met_on_post_spin_check_no_yield FAILS -- the read that
 *           should have been caught by the post-spin check now needs a yield
 *           first. So the two neighbouring boundary tests cover one direction
 *           each, and a spin bound HALVED (`s < spinCount / 2u`) fails eight.
 *           (5 failed)
 *
 * WHAT THIS FILE DELIBERATELY DOES NOT COVER
 *
 * 1. Each driver's own callbacks -- the real SPI1STAT / UxSTA / IFS4 reads,
 *    xTaskGetTickCount vs _CP0_GET_COUNT, and each driver's spin bound and
 *    budget constant. Those live in files a host cannot compile, and their
 *    values are ordinary firmware tuning, no longer copied into this file.
 *    The old Makefile greps that pinned them are gone with the copy they
 *    guarded (#1056 acceptance).
 * 2. Real elapsed time. The mock clock advances only inside the mock yield;
 *    there is no host analogue for the wall-clock cost of a register-poll
 *    spin. One mock tick stands for whatever one call to the driver's yield
 *    costs it.
 * 3. That any driver still CALLS WaitLoop_SpinThenYield. The compiler does
 *    that for the firmware build (spi_WaitStat et al. would not compile
 *    otherwise), not this suite; the Makefile keeps all four drivers
 *    (dac7718_WaitStat joined in #1108) as prerequisites so an edit to any
 *    one still re-runs this.
 * ========================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "test_framework.h"

/* THE REAL HEADER. Compiled by XC32 into the firmware, compiled by gcc here.
 * -I../../firmware/src/HAL, from the Makefile -- no stubs on the path. */
#include "WaitLoop.h"

/* --------------------------------------------------------------------------
 * Scenario sizes. These are the TEST's numbers, not the firmware's: with the
 * loop shared, a driver's spin bound and budget are arguments, so there is no
 * constant here to keep in sync with anything. SPIN is small enough to write
 * exact read counts by hand; BUDGET mirrors the order of magnitude of a real
 * one (USER_SPI_BYTE_TIMEOUT_MS is 20 ms) purely for readability.
 *
 * real_driver_spin_counts_are_honoured re-runs the boundary cases at the
 * values the drivers pass TODAY (8000 for spi/i2c/dac7718 -- the last joined
 * in #1108 -- 4000 for uart) so the arithmetic is exercised at the real
 * magnitudes too. Nothing pins those either -- if a driver retunes its spin,
 * that is a driver decision and this suite has no opinion about it.
 * ------------------------------------------------------------------------ */
#define SPIN                 5u
#define BUDGET              20u
#define SPI_I2C_SPIN      8000u
#define UART_SPIN         4000u

/* Runaway valve. The largest legitimate run below reads the status
 * SPI_I2C_SPIN + 2 times; anything past this means the loop is not
 * terminating (e.g. the yield was removed, so the mock clock is frozen). */
#define MOCK_MAX_STATUS_READS  1000000u

/* ==========================================================================
 * The mock driver: injected clock, injected status, injected yield.
 * ========================================================================== */

typedef struct {
    /* --- clock, owned entirely by this mock (the loop never sees it) --- */
    uint32_t now;                 /* stands in for xTaskGetTickCount() /
                                   * _CP0_GET_COUNT()                      */
    uint32_t start;               /* what the driver captured at entry      */
    uint32_t budget;              /* the driver's timeout, in mock ticks    */

    /* --- observations --- */
    uint32_t statusReads;         /* every statusMet() call: spin, post-spin
                                   * and the fresh read at expiry           */
    uint32_t budgetChecks;        /* every budgetSpent() call               */
    uint32_t yieldCalls;          /* every yield() call                     */

    /* --- injected behaviour --- */
    bool     statusValue;         /* what an ordinary read returns now      */
    uint32_t setTrueAfterReads;   /* flip true on the Nth read  (0 = off)   */
    uint32_t setTrueAfterYields;  /* flip true on the Nth yield (0 = off)   */
    /* Models the preemption gap an opus review of #913 identified: this task
     * can be preempted between the post-spin status read and the budget test,
     * for longer than the whole remaining budget, so the hardware status can
     * go true without any ordinary read ever observing it. The mock opens
     * that gap at the one instant the real one opens -- when budgetSpent()
     * returns true, i.e. immediately before the loop takes its fresh read --
     * so nothing but that fresh read can see it.                            */
    bool     trueInPreemptionGap;

    /* --- safety valve --- */
    bool     budgetIsSpent;       /* latched by budgetSpent() saying yes     */
    bool     runaway;             /* the loop never terminated on its own    */
} WaitMock;

static void mock_init(WaitMock *m, bool startTrue, uint32_t start, uint32_t budget)
{
    m->now                 = start;
    m->start               = start;
    m->budget              = budget;
    m->statusReads         = 0;
    m->budgetChecks        = 0;
    m->yieldCalls          = 0;
    m->statusValue         = startTrue;
    m->setTrueAfterReads   = 0;
    m->setTrueAfterYields  = 0;
    m->trueInPreemptionGap = false;
    m->budgetIsSpent       = false;
    m->runaway             = false;
}

/* Stands in for `((SPI1STAT & mask) != 0u) == want` and its two twins. */
static bool mock_status_met(void *ctx)
{
    WaitMock *m = (WaitMock *)ctx;
    m->statusReads++;

    if (m->statusReads >= MOCK_MAX_STATUS_READS) {
        /* The loop is not going to stop by itself. Force it to, loudly. */
        m->runaway    = true;
        m->statusValue = true;
    }
    if (m->setTrueAfterReads != 0u && m->statusReads >= m->setTrueAfterReads) {
        m->statusValue = true;
    }
    /* Only reachable by the fresh read at expiry -- see trueInPreemptionGap. */
    if (m->budgetIsSpent && m->trueInPreemptionGap) {
        return true;
    }
    return m->statusValue;
}

/* Stands in for each driver's own elapsed-vs-budget test. Rollover-safe the
 * same way all three are: unsigned (now - start) is the true elapsed count
 * even across a counter wrap. */
static bool mock_budget_spent(void *ctx)
{
    WaitMock *m = (WaitMock *)ctx;
    m->budgetChecks++;
    bool spent = ((uint32_t)(m->now - m->start) >= m->budget);
    if (spent) {
        m->budgetIsSpent = true;   /* opens the preemption gap, see above */
    }
    return spent;
}

/* Stands in for vTaskDelay(1): one mock tick of elapsed time. */
static void mock_yield(void *ctx)
{
    WaitMock *m = (WaitMock *)ctx;
    m->yieldCalls++;
    m->now += 1u;
    if (m->setTrueAfterYields != 0u && m->yieldCalls >= m->setTrueAfterYields) {
        m->statusValue = true;
    }
}

/* One call into the REAL loop. */
static bool run_wait(WaitMock *m, uint32_t spinCount)
{
    return WaitLoop_SpinThenYield(mock_status_met, mock_budget_spent,
                                  mock_yield, m, spinCount);
}

/* One call into the REAL hoisted-spin loop (#1108/#1109 -- see the block
 * below the original tests for what this proves and why it is a separate
 * function under test rather than another argument to run_wait above). */
static bool run_wait_hoisted(WaitMock *m, uint32_t spinCount)
{
    return WaitLoop_HoistedSpinThenYield(mock_status_met, mock_budget_spent,
                                         mock_yield, m, spinCount);
}

/* Reads per pass that does NOT return: spinCount spin reads + one post-spin
 * read. The final pass adds the fresh read at expiry. */
#define READS_PER_PASS(spin)   ((spin) + 1u)

/* Hoisted-shape equivalents. The spin runs ONCE, outside the retry loop, so
 * a non-terminal retry pass costs exactly ONE status read (not spin + 1) --
 * that is the whole property under test. TIMEOUT adds the initial spin, one
 * read per pass across (budget + 1) passes, and the fresh read at expiry.
 * READY_AT_EXPIRY has one fewer pass (the pass that observes the flipped
 * status returns without a budget check) and no fresh read. */
#define HOISTED_READS_TIMEOUT(spin, budget)         ((spin) + (budget) + 2u)
#define HOISTED_READS_READY_AT_EXPIRY(spin, budget) ((spin) + (budget) + 1u)

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* Happy path: the status is already met when the wait begins (a normal byte
 * at a driver's default baud completes well inside the time it takes to reach
 * the first read). Returns immediately -- no budget consulted, no yield. This
 * is the case that must never regress into an unconditional sleep. */
TEST(status_already_met_returns_immediately_no_yield)
{
    WaitMock m;
    mock_init(&m, true, 0u, BUDGET);

    ASSERT_TRUE(run_wait(&m, SPIN));
    ASSERT_EQ(m.statusReads, 1);      /* caught on the very first read */
    ASSERT_EQ(m.yieldCalls, 0);
    ASSERT_EQ(m.budgetChecks, 0);     /* the clock was never even read */
    ASSERT_EQ(m.now, 0);
    ASSERT_FALSE(m.runaway);
}

/* The status becomes met partway through the tight spin. Still no yield --
 * this is the "no context switch on the common path" property every driver's
 * spin-bound sizing depends on. */
TEST(status_met_mid_spin_returns_without_yield)
{
    WaitMock m;
    mock_init(&m, false, 0u, BUDGET);
    m.setTrueAfterReads = SPIN - 2u;   /* comfortably inside the spin */

    ASSERT_TRUE(run_wait(&m, SPIN));
    ASSERT_EQ(m.statusReads, SPIN - 2u);
    ASSERT_EQ(m.yieldCalls, 0);
    ASSERT_FALSE(m.runaway);
}

/* Met on the LAST spin iteration (read #SPIN): still caught inside the spin,
 * with the clock never consulted. Note what this does NOT do on its own: a
 * bound off by exactly one in either direction still catches read #SPIN with
 * no yield (one iteration shorter and the post-spin check takes it; one
 * longer and the spin still has room), so this test is the no-yield anchor,
 * not the pin. The next two tests cover one direction each, and the exact
 * read TOTALS below pin the bound outright. */
TEST(status_met_on_last_spin_iteration_no_yield)
{
    WaitMock m;
    mock_init(&m, false, 0u, BUDGET);
    m.setTrueAfterReads = SPIN;

    ASSERT_TRUE(run_wait(&m, SPIN));
    ASSERT_EQ(m.statusReads, SPIN);
    ASSERT_EQ(m.yieldCalls, 0);
    ASSERT_EQ(m.budgetChecks, 0);
    ASSERT_FALSE(m.runaway);
}

/* Pins the single post-spin read: met on read #(SPIN + 1), past the spin,
 * caught by the extra read that happens BEFORE the budget is consulted --
 * still with zero yields. Delete that read and this test takes a yield; so
 * does a spin bound one iteration SHORT, which is the direction the next test
 * cannot see. */
TEST(status_met_on_post_spin_check_no_yield)
{
    WaitMock m;
    mock_init(&m, false, 0u, BUDGET);
    m.setTrueAfterReads = SPIN + 1u;

    ASSERT_TRUE(run_wait(&m, SPIN));
    ASSERT_EQ(m.statusReads, SPIN + 1u);
    ASSERT_EQ(m.yieldCalls, 0);
    ASSERT_FALSE(m.runaway);
}

/* One read further (#SPIN + 2): the spin and the post-spin read both miss it,
 * so the wait falls through to the budget test (which has room), yields
 * exactly ONCE, and catches it on the next pass's first read. Confirms the
 * outer loop actually loops -- and it is the test that sees a spin bound one
 * iteration LONG, which would swallow this read and cost no yield. */
TEST(status_met_just_after_post_spin_check_takes_one_yield)
{
    WaitMock m;
    mock_init(&m, false, 0u, BUDGET);
    m.setTrueAfterReads = SPIN + 2u;

    ASSERT_TRUE(run_wait(&m, SPIN));
    ASSERT_EQ(m.statusReads, SPIN + 2u);
    ASSERT_EQ(m.yieldCalls, 1);
    ASSERT_EQ(m.budgetChecks, 1);
    ASSERT_FALSE(m.runaway);
}

/* The headline timeout case: the status is never met. Returns false at
 * exactly BUDGET elapsed -- neither early (which would spuriously fail a
 * legitimate slow operation) nor late by more than the one-tick yield
 * granularity.
 *
 * The exact read total is asserted too, and that is what makes this test
 * sensitive to the loop's SHAPE rather than just its timing: every pass reads
 * the status SPIN + 1 times before touching the clock, there are BUDGET + 1
 * such passes, and the last one adds the fresh read at expiry. */
TEST(status_never_met_times_out_at_exactly_the_budget)
{
    WaitMock m;
    mock_init(&m, false, 0u, BUDGET);

    ASSERT_FALSE(run_wait(&m, SPIN));
    ASSERT_EQ(m.yieldCalls, BUDGET);
    ASSERT_EQ(m.now, BUDGET);          /* exact: one tick per yield */
    ASSERT_EQ(m.budgetChecks, BUDGET + 1u);
    ASSERT_EQ(m.statusReads, READS_PER_PASS(SPIN) * (BUDGET + 1u) + 1u);
    ASSERT_FALSE(m.runaway);
}

/* THE load-bearing ordering property. The status becomes met on exactly the
 * yield that pushes the clock to expiry -- so by the time the next pass runs,
 * elapsed already equals the budget. The wait still returns TRUE, because the
 * status is read (spin + post-spin) before the budget is ever consulted.
 *
 * Pair it with the previous test: identical yield count and identical elapsed
 * clock, opposite verdicts, differing only in whether the status ever became
 * met. That is the "immune to scheduling latency" guarantee each driver's
 * budget sizing rests on -- the budget bounds WIRE time, not scheduler
 * latency. */
TEST(status_ready_exactly_at_budget_expiry_still_succeeds)
{
    WaitMock m;
    mock_init(&m, false, 0u, BUDGET);
    m.setTrueAfterYields = BUDGET;

    ASSERT_TRUE(run_wait(&m, SPIN));
    ASSERT_EQ(m.yieldCalls, BUDGET);
    ASSERT_EQ(m.now, BUDGET);
    /* Caught by the first read of the pass after that last yield. */
    ASSERT_EQ(m.statusReads, READS_PER_PASS(SPIN) * BUDGET + 1u);
    ASSERT_FALSE(m.runaway);
}

/* One yield later than the previous test: the status becomes met only after
 * the budget has already been found spent, so it never gets the chance.
 * Confirms that boundary is exactly one tick wide -- i.e. the previous test
 * is not passing merely because the budget test is never reached. */
TEST(status_ready_one_tick_past_expiry_times_out)
{
    WaitMock m;
    mock_init(&m, false, 0u, BUDGET);
    m.setTrueAfterYields = BUDGET + 1u;

    ASSERT_FALSE(run_wait(&m, SPIN));
    ASSERT_EQ(m.yieldCalls, BUDGET);
    ASSERT_EQ(m.now, BUDGET);
    ASSERT_FALSE(m.runaway);
}

/* MUTATION 1's tripwire (see the header): a status that goes true only in the
 * gap between the post-spin read and the budget test -- unreachable by any
 * ordinary read, see trueInPreemptionGap -- must still be caught, because the
 * loop takes a FRESH read at expiry instead of returning a bare false.
 *
 * That gap is reachable in the firmware, not theoretical: on the WiFi SCPI
 * path (app_WifiTask, priority 2 -- below the encoder, SD, USB and both
 * priority-9 deferred tasks) a preemption longer than i2c's ~5.5 ms budget is
 * ordinary under streaming load. Returning false there reports a COMPLETED
 * transfer as a timeout, which fires i2c_BusRecover() on a healthy bus. */
TEST(status_true_only_in_the_preemption_gap_is_still_caught)
{
    WaitMock m;
    mock_init(&m, false, 0u, BUDGET);
    m.trueInPreemptionGap = true;      /* never visible to an ordinary read */

    ASSERT_TRUE(run_wait(&m, SPIN));
    /* Spends the whole budget in yields -- nothing ordinary ever sees the
     * status -- then succeeds on the fresh read rather than timing out. */
    ASSERT_EQ(m.yieldCalls, BUDGET);
    ASSERT_EQ(m.now, BUDGET);
    ASSERT_EQ(m.statusReads, READS_PER_PASS(SPIN) * (BUDGET + 1u) + 1u);
    ASSERT_FALSE(m.runaway);
}

/* Companion negative: with trueInPreemptionGap left false, the fresh read at
 * expiry must still return false when the status genuinely never became met.
 * The fresh read closes a false-TIMEOUT window; it must not open a
 * false-SUCCESS one. */
TEST(status_still_false_at_expiry_truly_times_out)
{
    WaitMock m;
    mock_init(&m, false, 0u, BUDGET);

    ASSERT_FALSE(run_wait(&m, SPIN));
    ASSERT_EQ(m.yieldCalls, BUDGET);
    ASSERT_FALSE(m.runaway);
}

/* MUTATION 2's tripwire. A wait entered with its budget ALREADY spent must
 * not sleep: it reads the status (spin + post-spin), finds the budget gone,
 * takes its fresh read and returns. Move the budget test after the yield and
 * it sleeps once first.
 *
 * This is a real firmware case, not a contrived one: uart_WriteLocked shares
 * ONE 15 s budget across a whole write, passing the same start/timeout to
 * uart_WaitSta for every byte, so a later byte can enter with nothing left.
 *
 * The exact read count also pins the ORDER of the post-spin read and the
 * budget test (MUTATION 5): consult the budget first and this is SPIN + 1. */
TEST(budget_already_spent_on_entry_never_yields)
{
    WaitMock m;
    mock_init(&m, false, 0u, BUDGET);
    m.now = BUDGET;                    /* elapsed == budget before we start */

    ASSERT_FALSE(run_wait(&m, SPIN));
    ASSERT_EQ(m.yieldCalls, 0);
    ASSERT_EQ(m.now, BUDGET);          /* no time passed inside the wait */
    ASSERT_EQ(m.budgetChecks, 1);
    ASSERT_EQ(m.statusReads, SPIN + 2u);   /* spin + post-spin + fresh */
    ASSERT_FALSE(m.runaway);
}

/* The same entry condition with the status already met: an exhausted budget
 * must not turn a COMPLETED operation into a failure. Returns true on the
 * first read, before the clock is consulted at all. */
TEST(budget_already_spent_on_entry_still_reports_a_met_status)
{
    WaitMock m;
    mock_init(&m, true, 0u, BUDGET);
    m.now = BUDGET;

    ASSERT_TRUE(run_wait(&m, SPIN));
    ASSERT_EQ(m.statusReads, 1);
    ASSERT_EQ(m.budgetChecks, 0);
    ASSERT_EQ(m.yieldCalls, 0);
    ASSERT_FALSE(m.runaway);
}

/* Degenerate spin bound. With spinCount 0 the tight spin does nothing, but
 * the post-spin read still happens before the budget is consulted -- so the
 * ordering guarantee survives even when the fast path is compiled out. One
 * read per pass, plus the fresh read at expiry. */
TEST(zero_spin_count_still_checks_status_before_yielding)
{
    WaitMock m;
    mock_init(&m, false, 0u, BUDGET);

    ASSERT_FALSE(run_wait(&m, 0u));
    ASSERT_EQ(m.yieldCalls, BUDGET);
    ASSERT_EQ(m.statusReads, READS_PER_PASS(0u) * (BUDGET + 1u) + 1u);
    ASSERT_FALSE(m.runaway);
}

/* Rollover safety: unsigned (now - start) must still read as the true elapsed
 * count across a counter wrap. Starting 16 below UINT32_MAX puts the wrap
 * inside the budget window. Both time bases wrap -- TickType_t is 32-bit
 * (FreeRTOSConfig.h), and the CP0 core timer is a free-running 32-bit counter
 * -- so this is the one property both of them need. */
TEST(budget_survives_counter_wrap)
{
    WaitMock m;
    const uint32_t startTick = 0xFFFFFFF0u;   /* 16 below UINT32_MAX */
    mock_init(&m, false, startTick, BUDGET);

    ASSERT_FALSE(run_wait(&m, SPIN));
    ASSERT_EQ(m.yieldCalls, BUDGET);
    ASSERT_TRUE(m.now < startTick);                        /* it really wrapped */
    ASSERT_EQ((uint32_t)(m.now - startTick), BUDGET);
    ASSERT_FALSE(m.runaway);
}

/* #1114: WaitLoop_BudgetSpent is the boundary predicate i2c_WaitBudgetSpent
 * (UserI2c.c) now calls instead of inlining its own comparison -- it used to
 * be a strict '>' there, carried over unexamined through #1056's and #1108's
 * folds, where spi_WaitStat/uart_WaitSta's inline '>=' (textually identical
 * to this function's body) never changed. This pins the boundary itself:
 * every test above uses mock_budget_spent, which has always modelled '>='
 * and so cannot distinguish the decision from the bug it replaces.
 *
 * What this test alone does NOT prove: that i2c_WaitBudgetSpent still CALLS
 * this function rather than reverting to its own inline comparison (a driver
 * doing that would still pass this test, unchanged, since it tests the
 * shared function directly -- the exact blind spot the WAITLOOP_BIN Makefile
 * rule's DAC7718 guards exist for on that driver). The Makefile rule for
 * this binary closes it the same way: a grep that UserI2c.c still calls
 * 'WaitLoop_BudgetSpent('. */
TEST(budget_spent_boundary_matches_all_drivers)
{
    ASSERT_FALSE(WaitLoop_BudgetSpent(BUDGET - 1u, BUDGET));  /* one short: not yet */
    ASSERT_TRUE(WaitLoop_BudgetSpent(BUDGET, BUDGET));        /* exactly at budget: spent */
    ASSERT_TRUE(WaitLoop_BudgetSpent(BUDGET + 1u, BUDGET));   /* past budget: spent */
    ASSERT_FALSE(WaitLoop_BudgetSpent(0u, BUDGET));           /* fresh start: not spent */
}

/* The boundary cases again at the magnitudes the drivers pass today -- 8000
 * (spi_WaitStat, i2c_WaitMif) and 4000 (uart_WaitSta). Nothing here PINS
 * those: with the loop shared, a spin bound is an argument, and retuning one
 * is a driver decision this suite has no opinion about. What it checks is
 * that the arithmetic holds at a real magnitude and not just at SPIN = 5. */
static void check_spin_bound(uint32_t spin)
{
    WaitMock m;

    /* Met on the last spin read: no yield. */
    mock_init(&m, false, 0u, BUDGET);
    m.setTrueAfterReads = spin;
    ASSERT_TRUE(run_wait(&m, spin));
    ASSERT_EQ(m.statusReads, spin);
    ASSERT_EQ(m.yieldCalls, 0);
    ASSERT_FALSE(m.runaway);

    /* Two reads later: exactly one yield. */
    mock_init(&m, false, 0u, BUDGET);
    m.setTrueAfterReads = spin + 2u;
    ASSERT_TRUE(run_wait(&m, spin));
    ASSERT_EQ(m.statusReads, spin + 2u);
    ASSERT_EQ(m.yieldCalls, 1);
    ASSERT_FALSE(m.runaway);
}

TEST(real_driver_spin_counts_are_honoured)
{
    check_spin_bound(SPI_I2C_SPIN);
    check_spin_bound(UART_SPIN);
}

/* ==========================================================================
 * WaitLoop_HoistedSpinThenYield (#1108/#1109) -- DAC7718's shape
 *
 * #1057 deliberately hoisted DAC7718's fast spin OUTSIDE its retry loop; the
 * first #1108 attempt folded dac7718_WaitStat onto WaitLoop_SpinThenYield
 * above as though it shared the other three drivers' RE-spinning shape. It
 * does not, and PR #1109 shipped that regression uncaught: every existing
 * test above stayed green, because they prove WaitLoop_SpinThenYield's shape
 * correctly and say nothing about which shape a DIFFERENT driver should be
 * calling. Acceptance item 5, added when #1108 was corrected: a test that
 * counts status-read invocations across a multi-tick wait and fails if the
 * count scales with the number of yields --
 * hoisted_spin_status_reads_do_not_scale_with_yield_count below is that test.
 *
 * The other tests in this block are the same boundary/ordering coverage the
 * re-spinning shape gets above, adjusted for the one-read-per-pass shape (see
 * HOISTED_READS_TIMEOUT/HOISTED_READS_READY_AT_EXPIRY above) -- #913's
 * ordering guarantee (status before budget, fresh read at expiry, the
 * preemption-gap case) is IDENTICAL between the two shapes and just as
 * load-bearing here, so it is proven here too rather than assumed to carry
 * over.
 *
 * MUTATION COVERAGE (2026-09-16, gcc 13.3): WaitLoop_HoistedSpinThenYield's
 * body was edited to move its `for (uint32_t s = 0; s < spinCount; ++s)`
 * spin INSIDE the retry loop -- i.e. made it byte-for-byte
 * WaitLoop_SpinThenYield's shape, which is exactly what #1109 shipped by
 * calling the wrong function. Re-run, then reverted:
 *   hoisted_spin_status_reads_do_not_scale_with_yield_count FAILS
 *     (readDelta went from 27 to 216027 = 27 * (8000 + 1), i.e. the mutation
 *     this test exists to catch, and the assertion it exists to make: reads
 *     scale with SPIN per yield, not 1:1). Also FAILS:
 *     hoisted_status_never_met_times_out_at_exactly_the_budget,
 *     hoisted_status_ready_exactly_at_budget_expiry_still_succeeds and
 *     hoisted_status_true_only_in_the_preemption_gap_is_still_caught, whose
 *     exact multi-pass read counts assume one read per non-terminal pass (4
 *     of 14 failed; the single-pass boundary tests at SPIN/SPIN+1/SPIN+2
 *     reads can't distinguish the two shapes on their own -- a re-spun first
 *     pass reads the same counts a hoisted spin plus one retry pass does --
 *     which is exactly why the scaling test above them is the one #1108
 *     asked for, not another boundary count).
 * ========================================================================== */

/* Happy path: mirrors status_already_met_returns_immediately_no_yield. */
TEST(hoisted_status_already_met_returns_immediately_no_yield)
{
    WaitMock m;
    mock_init(&m, true, 0u, BUDGET);

    ASSERT_TRUE(run_wait_hoisted(&m, SPIN));
    ASSERT_EQ(m.statusReads, 1);
    ASSERT_EQ(m.yieldCalls, 0);
    ASSERT_EQ(m.budgetChecks, 0);
    ASSERT_EQ(m.now, 0);
    ASSERT_FALSE(m.runaway);
}

/* Met partway through the one-time hoisted spin: still no yield. */
TEST(hoisted_status_met_mid_spin_returns_without_yield)
{
    WaitMock m;
    mock_init(&m, false, 0u, BUDGET);
    m.setTrueAfterReads = SPIN - 2u;

    ASSERT_TRUE(run_wait_hoisted(&m, SPIN));
    ASSERT_EQ(m.statusReads, SPIN - 2u);
    ASSERT_EQ(m.yieldCalls, 0);
    ASSERT_FALSE(m.runaway);
}

/* Met on the LAST hoisted-spin iteration (read #SPIN): still caught inside
 * the spin, clock never consulted. */
TEST(hoisted_status_met_on_last_spin_iteration_no_yield)
{
    WaitMock m;
    mock_init(&m, false, 0u, BUDGET);
    m.setTrueAfterReads = SPIN;

    ASSERT_TRUE(run_wait_hoisted(&m, SPIN));
    ASSERT_EQ(m.statusReads, SPIN);
    ASSERT_EQ(m.yieldCalls, 0);
    ASSERT_EQ(m.budgetChecks, 0);
    ASSERT_FALSE(m.runaway);
}

/* Met at read #(SPIN + 1): the hoisted spin misses, but the retry loop's OWN
 * first status check -- its ordinary first pass, not a dedicated post-spin
 * read -- catches it with zero yields and zero budget checks. */
TEST(hoisted_status_met_on_first_retry_check_no_yield)
{
    WaitMock m;
    mock_init(&m, false, 0u, BUDGET);
    m.setTrueAfterReads = SPIN + 1u;

    ASSERT_TRUE(run_wait_hoisted(&m, SPIN));
    ASSERT_EQ(m.statusReads, SPIN + 1u);
    ASSERT_EQ(m.yieldCalls, 0);
    ASSERT_EQ(m.budgetChecks, 0);
    ASSERT_FALSE(m.runaway);
}

/* Met at read #(SPIN + 2): the hoisted spin and the retry loop's first check
 * both miss, so the budget is consulted (room left), the loop yields exactly
 * ONCE, and the retry loop's SECOND pass catches it on its first read. This
 * is the read count the re-spinning shape reaches the same way but would
 * keep re-paying SPIN reads for on every later yield -- see the scaling test
 * below, which is the same property at driver-realistic magnitude. */
TEST(hoisted_status_met_after_one_yield)
{
    WaitMock m;
    mock_init(&m, false, 0u, BUDGET);
    m.setTrueAfterReads = SPIN + 2u;

    ASSERT_TRUE(run_wait_hoisted(&m, SPIN));
    ASSERT_EQ(m.statusReads, SPIN + 2u);
    ASSERT_EQ(m.yieldCalls, 1);
    ASSERT_EQ(m.budgetChecks, 1);
    ASSERT_FALSE(m.runaway);
}

/* The headline timeout case, exact counts -- sensitive to the loop's SHAPE
 * the same way status_never_met_times_out_at_exactly_the_budget is: every
 * non-terminal pass reads the status ONCE (not SPIN + 1) before touching the
 * clock, there are BUDGET such passes plus one terminal pass, and the
 * terminal pass adds the fresh read at expiry. */
TEST(hoisted_status_never_met_times_out_at_exactly_the_budget)
{
    WaitMock m;
    mock_init(&m, false, 0u, BUDGET);

    ASSERT_FALSE(run_wait_hoisted(&m, SPIN));
    ASSERT_EQ(m.yieldCalls, BUDGET);
    ASSERT_EQ(m.now, BUDGET);
    ASSERT_EQ(m.budgetChecks, BUDGET + 1u);
    ASSERT_EQ(m.statusReads, HOISTED_READS_TIMEOUT(SPIN, BUDGET));
    ASSERT_FALSE(m.runaway);
}

/* THE load-bearing ordering property, same as its re-spinning counterpart:
 * status becomes met on exactly the yield that pushes the clock to expiry,
 * and the wait still returns TRUE because the status is read before the
 * budget is ever consulted on the next pass. */
TEST(hoisted_status_ready_exactly_at_budget_expiry_still_succeeds)
{
    WaitMock m;
    mock_init(&m, false, 0u, BUDGET);
    m.setTrueAfterYields = BUDGET;

    ASSERT_TRUE(run_wait_hoisted(&m, SPIN));
    ASSERT_EQ(m.yieldCalls, BUDGET);
    ASSERT_EQ(m.now, BUDGET);
    ASSERT_EQ(m.statusReads, HOISTED_READS_READY_AT_EXPIRY(SPIN, BUDGET));
    ASSERT_FALSE(m.runaway);
}

/* One yield later: the boundary is exactly one tick wide, same as the
 * re-spinning shape. */
TEST(hoisted_status_ready_one_tick_past_expiry_times_out)
{
    WaitMock m;
    mock_init(&m, false, 0u, BUDGET);
    m.setTrueAfterYields = BUDGET + 1u;

    ASSERT_FALSE(run_wait_hoisted(&m, SPIN));
    ASSERT_EQ(m.yieldCalls, BUDGET);
    ASSERT_EQ(m.now, BUDGET);
    ASSERT_FALSE(m.runaway);
}

/* MUTATION 1's tripwire for the hoisted shape: a status that goes true only
 * in the gap between the retry loop's read and its budget check -- see
 * trueInPreemptionGap -- must still be caught by the fresh read at expiry.
 * Identical guarantee to the re-spinning shape's twin test; proven separately
 * because the hoisted function is its own implementation, not a thin wrapper
 * over the other one. */
TEST(hoisted_status_true_only_in_the_preemption_gap_is_still_caught)
{
    WaitMock m;
    mock_init(&m, false, 0u, BUDGET);
    m.trueInPreemptionGap = true;

    ASSERT_TRUE(run_wait_hoisted(&m, SPIN));
    ASSERT_EQ(m.yieldCalls, BUDGET);
    ASSERT_EQ(m.now, BUDGET);
    ASSERT_EQ(m.statusReads, HOISTED_READS_TIMEOUT(SPIN, BUDGET));
    ASSERT_FALSE(m.runaway);
}

/* MUTATION 2's tripwire for the hoisted shape: entered with the budget
 * already spent, it must not sleep -- it reads the status once (the retry
 * loop's first pass), finds the budget gone, takes its fresh read, returns. */
TEST(hoisted_budget_already_spent_on_entry_never_yields)
{
    WaitMock m;
    mock_init(&m, false, 0u, BUDGET);
    m.now = BUDGET;

    ASSERT_FALSE(run_wait_hoisted(&m, SPIN));
    ASSERT_EQ(m.yieldCalls, 0);
    ASSERT_EQ(m.now, BUDGET);
    ASSERT_EQ(m.budgetChecks, 1);
    ASSERT_EQ(m.statusReads, SPIN + 2u);   /* hoist spin + retry read + fresh */
    ASSERT_FALSE(m.runaway);
}

/* Companion: an exhausted budget must not turn a COMPLETED operation into a
 * failure. Caught on the very first read (still inside the hoisted spin),
 * before the clock is consulted at all. */
TEST(hoisted_budget_already_spent_on_entry_still_reports_a_met_status)
{
    WaitMock m;
    mock_init(&m, true, 0u, BUDGET);
    m.now = BUDGET;

    ASSERT_TRUE(run_wait_hoisted(&m, SPIN));
    ASSERT_EQ(m.statusReads, 1);
    ASSERT_EQ(m.budgetChecks, 0);
    ASSERT_EQ(m.yieldCalls, 0);
    ASSERT_FALSE(m.runaway);
}

/* Degenerate spin bound: with spinCount 0 the hoisted spin does nothing, but
 * the retry loop's ordering guarantee (status before budget) survives. */
TEST(hoisted_zero_spin_count_still_checks_status_before_yielding)
{
    WaitMock m;
    mock_init(&m, false, 0u, BUDGET);

    ASSERT_FALSE(run_wait_hoisted(&m, 0u));
    ASSERT_EQ(m.yieldCalls, BUDGET);
    ASSERT_EQ(m.statusReads, HOISTED_READS_TIMEOUT(0u, BUDGET));
    ASSERT_FALSE(m.runaway);
}

/* THE #1108 acceptance item 5 test -- the one that would have caught #1109
 * before any build. Two runs at DAC7718_SPI_FAST_SPIN_COUNT's real magnitude
 * (8000, a literal per this suite's convention of not pinning driver
 * constants -- see the file header), different budgets, status never met
 * (the fault path this function exists for). If the spin is paid ONCE, the
 * delta in status reads between the two runs equals the delta in YIELDS --
 * one extra register read per extra tick waited, independent of the spin
 * bound entirely. If the spin were re-paid on every wake (#1109's actual
 * defect: dac7718_WaitStat calling WaitLoop_SpinThenYield), the delta would
 * instead scale by (spin + 1) per extra yield -- 27 * 8001 = 216027, not 27. */
TEST(hoisted_spin_status_reads_do_not_scale_with_yield_count)
{
    const uint32_t spin = 8000u;   /* DAC7718_SPI_FAST_SPIN_COUNT, today */
    WaitMock shortWait;
    WaitMock longWait;
    uint32_t yieldDelta;
    uint32_t readDelta;

    mock_init(&shortWait, false, 0u, 3u);
    ASSERT_FALSE(run_wait_hoisted(&shortWait, spin));

    mock_init(&longWait, false, 0u, 30u);
    ASSERT_FALSE(run_wait_hoisted(&longWait, spin));

    yieldDelta = longWait.yieldCalls - shortWait.yieldCalls;
    readDelta  = longWait.statusReads - shortWait.statusReads;

    ASSERT_EQ(yieldDelta, 27u);
    /* THE load-bearing assertion: reads scale with YIELDS 1:1, not with
     * spinCount per yield. */
    ASSERT_EQ(readDelta, yieldDelta);
    ASSERT_TRUE(readDelta < spin);
    ASSERT_FALSE(shortWait.runaway);
    ASSERT_FALSE(longWait.runaway);
}

int main(void)
{
    printf("#1056/#1108/#1109 -- the shared spin-then-yield wait loop (HAL/WaitLoop.h)\n");
    printf("---------------------------------------------\n");
    RUN(status_already_met_returns_immediately_no_yield);
    RUN(status_met_mid_spin_returns_without_yield);
    RUN(status_met_on_last_spin_iteration_no_yield);
    RUN(status_met_on_post_spin_check_no_yield);
    RUN(status_met_just_after_post_spin_check_takes_one_yield);
    RUN(status_never_met_times_out_at_exactly_the_budget);
    RUN(status_ready_exactly_at_budget_expiry_still_succeeds);
    RUN(status_ready_one_tick_past_expiry_times_out);
    RUN(status_true_only_in_the_preemption_gap_is_still_caught);
    RUN(status_still_false_at_expiry_truly_times_out);
    RUN(budget_already_spent_on_entry_never_yields);
    RUN(budget_already_spent_on_entry_still_reports_a_met_status);
    RUN(zero_spin_count_still_checks_status_before_yielding);
    RUN(budget_survives_counter_wrap);
    RUN(budget_spent_boundary_matches_all_drivers);
    RUN(real_driver_spin_counts_are_honoured);
    RUN(hoisted_status_already_met_returns_immediately_no_yield);
    RUN(hoisted_status_met_mid_spin_returns_without_yield);
    RUN(hoisted_status_met_on_last_spin_iteration_no_yield);
    RUN(hoisted_status_met_on_first_retry_check_no_yield);
    RUN(hoisted_status_met_after_one_yield);
    RUN(hoisted_status_never_met_times_out_at_exactly_the_budget);
    RUN(hoisted_status_ready_exactly_at_budget_expiry_still_succeeds);
    RUN(hoisted_status_ready_one_tick_past_expiry_times_out);
    RUN(hoisted_status_true_only_in_the_preemption_gap_is_still_caught);
    RUN(hoisted_budget_already_spent_on_entry_never_yields);
    RUN(hoisted_budget_already_spent_on_entry_still_reports_a_met_status);
    RUN(hoisted_zero_spin_count_still_checks_status_before_yielding);
    RUN(hoisted_spin_status_reads_do_not_scale_with_yield_count);
    return TEST_SUMMARY();
}
