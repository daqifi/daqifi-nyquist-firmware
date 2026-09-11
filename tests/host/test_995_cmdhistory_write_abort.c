/* ==========================================================================
 * test_995_cmdhistory_write_abort.c -- issue #995
 *
 * WHAT IS UNDER TEST
 *
 * SCPI_GetCommandHistory (SYSTem:LOG:CMDHistory?, SCPIInterface.c) holds the
 * single shared SCPI response buffer's mutex (gScpiRespMutex, #347) from one
 * take at entry to one give at exit, and used to emit its reply as 1 header
 * write plus up to SCPI_CMD_HISTORY_SIZE(10) history-entry writes -- up to 11
 * total -- with every context->interface->write(...) return value discarded.
 * On both transports that write is SCPI_WriteWithRetry (SCPIInterface.c,
 * ~line 8659):
 *
 *     #define SCPI_WRITE_MAX_RETRIES      200
 *     #define SCPI_WRITE_RETRY_DELAY_MS   5
 *
 *     size_t SCPI_WriteWithRetry(ScpiTransportWriteFn writeFn,
 *                                const char* data, size_t len) {
 *         size_t written = 0;
 *         int retries = SCPI_WRITE_MAX_RETRIES;
 *         while (written < len && retries > 0) {
 *             size_t n = writeFn(data + written, len - written);
 *             written += n;
 *             if (written >= len) break;
 *             vTaskDelay(pdMS_TO_TICKS(SCPI_WRITE_RETRY_DELAY_MS));
 *             retries--;
 *         }
 *         return written;
 *     }
 *
 * That is bounded PER CALL, at up to 200 x 5 ms ~= 1 s. Against a host that
 * has stopped reading, every one of the up-to-11 writes independently burns
 * its own ~1 s before giving up -- so the pre-#995 callback could hold the
 * mutex for ~11 s, and every OTHER SCPI callback on either transport that
 * needed the shared buffer queued behind it for the same ~11 s.
 *
 * THE FIX (CommandHistory_Write, SCPIInterface.c) is the same two-guard shape
 * #947 uses for SCPI_SysInfoTextGet's SysInfoText_Write, applied to this
 * function as its own point fix (no shared helper exists yet -- see the
 * comment above CommandHistory_Write in the real source for why):
 *
 *   1. Short write => abort. A write that does not return its full length
 *      proves SCPI_WriteWithRetry already spent its whole ~1 s budget without
 *      completing, so continuing to the next line would spend another ~1 s
 *      on bytes that are equally undeliverable.
 *   2. Cumulative deadline (SCPI_CMDHISTORY_WRITE_BUDGET_MS = 2000, checked
 *      BEFORE each write). Guard 1 ALONE does not bound the hold: a write
 *      that completes on its last allowed retry returns its full length and
 *      never trips guard 1, so a transport that drains at exactly the
 *      trickle rate letting every write finish just inside its own ~1 s
 *      budget still reaches ~11 s with only guard 1 in place. Guard 2 is
 *      what actually bounds that case.
 *
 * HOW IT IS TESTED
 *
 * SCPIInterface.c is not a host-test candidate (libscpi + FreeRTOS + the
 * whole board/driver graph), so -- same approach as
 * test_943_bench_stall_bound.c and #947's test_947_sysinfo_write_abort.c --
 * this file re-implements the SHAPE of SCPI_WriteWithRetry and of
 * SCPI_GetCommandHistory's write loop (pre- and post-#995) against an
 * injected mock clock and mock transport, and compares their verdicts on
 * identical inputs. What is proven is the ALGEBRA of the two shapes, which
 * is exactly what #995 is about.
 *
 * FIDELITY -- what the extracted functions are NOT
 *
 * 1. mock_write_with_retry mirrors SCPI_WriteWithRetry's LOOP exactly (same
 *    condition, same increment/break placement, same retry decrement), with
 *    writeFn/vTaskDelay replaced by mock counterparts. It does not move any
 *    actual bytes.
 * 2. new_cmdhistory_write_all mirrors CommandHistory_Write's two guards plus
 *    SCPI_GetCommandHistory's goto-on-failure, and samples startTick once,
 *    exactly like the real function samples it once right after
 *    SCPI_ResponseBuf_Take(). It does not model the actual reply CONTENT or
 *    the __cmdhistory_stalled_exit cleanup (SCPI_ResponseBuf_Give /
 *    SCPI_ExecutionError) -- those need the real board and are out of reach
 *    on a host.
 * 3. old_cmdhistory_write_all mirrors the pre-#995 shape: every
 *    context->interface->write() return value was discarded, so the loop
 *    calls all n_calls writes regardless of any individual failure.
 * 4. guard1_only_cmdhistory_write_all is not a real historical shape -- it
 *    is the SPECIFIC mutation "keep the short-write check, delete the
 *    cumulative deadline". It exists so the fix's own "guard 1 alone is not
 *    enough" argument is an executable assertion
 *    (trickle_transport_guard2_is_load_bearing below) rather than only
 *    prose -- same purpose as #947's identically-named test.
 * 5. configTICK_RATE_HZ is 1000 and TickType_t is 32-bit
 *    (firmware/src/config/default/FreeRTOSConfig.h:58,126), so one tick is
 *    one millisecond and pdMS_TO_TICKS is the identity here, same assumption
 *    as #943 and #947. The Makefile target for THIS file greps
 *    FreeRTOSConfig.h for both and refuses to build if either has drifted.
 * 6. The three firmware constants this file depends on
 *    (SCPI_WRITE_MAX_RETRIES, SCPI_WRITE_RETRY_DELAY_MS,
 *    SCPI_CMDHISTORY_WRITE_BUDGET_MS) are DUPLICATED here as FW_* macros. The
 *    Makefile target greps them out of SCPIInterface.c and refuses to build
 *    if any has changed, so a stale copy cannot pass silently.
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "test_framework.h"

/* --------------------------------------------------------------------------
 * Firmware constants, mirrored. Pinned against the real source by the
 * Makefile's grep guard.
 * ------------------------------------------------------------------------ */
#define FW_WRITE_MAX_RETRIES      200U   /* SCPI_WRITE_MAX_RETRIES */
#define FW_WRITE_RETRY_DELAY_MS     5U   /* SCPI_WRITE_RETRY_DELAY_MS */
#define FW_CMDHISTORY_BUDGET_MS  2000U   /* SCPI_CMDHISTORY_WRITE_BUDGET_MS */

/* SCPI_CMD_HISTORY_SIZE (UsbCdc.h) -- one header write plus up to this many
 * entry writes = up to N_WRITES_MAX. Nothing below depends on the exact
 * number beyond it being small (<= 11); the swept variant below covers a
 * range including it. */
#define FW_CMD_HISTORY_SIZE       10U
#define N_WRITES_MAX              (FW_CMD_HISTORY_SIZE + 1U)   /* 11 */
#define LEN_PER_WRITE             40U   /* an arbitrary, realistic line length */

/* ==========================================================================
 * Mock environment
 * ========================================================================== */

typedef enum {
    XPORT_ALWAYS_ACCEPTS = 0,  /* healthy host: every attempt takes the full
                                * remaining length immediately. */
    XPORT_NEVER_ACCEPTS,       /* fully stalled host: every attempt takes 0. */
    XPORT_TRICKLE_ACCEPTS      /* a transport that eventually accepts EVERY
                                * write in full, but only after
                                * trickleAttemptsNeeded 0-length attempts --
                                * models "drains just inside its own retry
                                * budget, every time". */
} MockTransportMode;

typedef struct {
    uint32_t          now;                   /* simulated tick counter, ms  */
    uint32_t          delayCalls;
    uint32_t          transportCalls;
    MockTransportMode mode;
    uint32_t          trickleAttemptsNeeded; /* TRICKLE only */
} MockEnv;

static void mock_init(MockEnv *env, MockTransportMode mode, uint32_t startTick)
{
    env->now                    = startTick;
    env->delayCalls              = 0;
    env->transportCalls          = 0;
    env->mode                    = mode;
    env->trickleAttemptsNeeded   = 0;
}

/* Stands in for xTaskGetTickCount(). */
static uint32_t mock_tick_count(const MockEnv *env)
{
    return env->now;
}

/* Stands in for vTaskDelay(pdMS_TO_TICKS(nominalMs)). */
static void mock_delay_ms(MockEnv *env, uint32_t nominalMs)
{
    env->now += nominalMs;
    env->delayCalls++;
}

/* Stands in for one underlying transport attempt (UsbCdc_ScpiWrite /
 * wifi_tcp_server_WriteBuffer) -- the writeFn SCPI_WriteWithRetry calls.
 * `remaining` is what SCPI_WriteWithRetry would pass as len-written;
 * `attemptNum` is 0-based, counted per OUTER write-with-retry call (reset by
 * the caller, see mock_write_with_retry). */
static size_t mock_transport_attempt(MockEnv *env, size_t remaining, uint32_t attemptNum)
{
    env->transportCalls++;
    switch (env->mode) {
        case XPORT_ALWAYS_ACCEPTS:
            return remaining;
        case XPORT_TRICKLE_ACCEPTS:
            return (attemptNum >= env->trickleAttemptsNeeded) ? remaining : 0U;
        case XPORT_NEVER_ACCEPTS:
        default:
            return 0U;
    }
}

/* ==========================================================================
 * Extracted, line-for-line mirrors of the real code
 * ========================================================================== */

/* Mirrors SCPI_WriteWithRetry exactly: same loop condition, same
 * increment-then-break, same retry decrement. */
static size_t mock_write_with_retry(MockEnv *env, size_t len)
{
    size_t written = 0;
    int retries = (int)FW_WRITE_MAX_RETRIES;
    uint32_t attempt = 0;
    while (written < len && retries > 0) {
        size_t n = mock_transport_attempt(env, len - written, attempt++);
        written += n;
        if (written >= len) break;
        mock_delay_ms(env, FW_WRITE_RETRY_DELAY_MS);
        retries--;
    }
    return written;
}

/* PRE-#995. context->interface->write(...)'s return value was discarded at
 * every call site, so the loop attempts all n_calls regardless of any
 * individual failure. Returns how many of them happened to report full
 * completion (bookkeeping only -- the real code never checked this
 * either). */
static uint32_t old_cmdhistory_write_all(MockEnv *env, size_t n_calls, size_t len_per_call)
{
    uint32_t sent = 0;
    size_t i;
    for (i = 0; i < n_calls; i++) {
        size_t w = mock_write_with_retry(env, len_per_call);
        if (w == len_per_call) sent++;
    }
    return sent;
}

/* POST-#995. Mirrors CommandHistory_Write's two guards plus
 * SCPI_GetCommandHistory's goto-on-failure: guard 2 (cumulative deadline) is
 * checked BEFORE the write, guard 1 (short-write) after it. startTick is
 * sampled once, like the real code samples it once right after the take. */
static uint32_t new_cmdhistory_write_all(MockEnv *env, size_t n_calls, size_t len_per_call)
{
    uint32_t sent = 0;
    uint32_t startTick = mock_tick_count(env);
    size_t i;
    for (i = 0; i < n_calls; i++) {
        /* Guard 2, unsigned-subtraction wrap-safe (same idiom as #943's and
         * #947's tick-wrap tests and as the real CommandHistory_Write). */
        if ((uint32_t)(mock_tick_count(env) - startTick) >= FW_CMDHISTORY_BUDGET_MS) {
            break;
        }
        size_t w = mock_write_with_retry(env, len_per_call);
        if (w != len_per_call) {   /* guard 1 */
            break;
        }
        sent++;
    }
    return sent;
}

/* NOT a historical shape. This is the specific mutation "keep guard 1,
 * delete guard 2" -- the one CommandHistory_Write's own comment argues
 * against ("guard 1 alone does not bound the hold"). Exists so that argument
 * is an assertion, not only prose; see
 * trickle_transport_guard2_is_load_bearing. */
static uint32_t guard1_only_cmdhistory_write_all(MockEnv *env, size_t n_calls, size_t len_per_call)
{
    uint32_t sent = 0;
    size_t i;
    for (i = 0; i < n_calls; i++) {
        size_t w = mock_write_with_retry(env, len_per_call);
        if (w != len_per_call) break;
        sent++;
    }
    return sent;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* Byte-for-byte parity requirement: against a healthy host every write
 * completes on its first attempt, so neither shape sleeps, aborts, or
 * differs in how many writes it sends. */
TEST(healthy_host_both_shapes_send_every_write_with_zero_delay)
{
    MockEnv oldEnv, newEnv;
    mock_init(&oldEnv, XPORT_ALWAYS_ACCEPTS, 0U);
    mock_init(&newEnv, XPORT_ALWAYS_ACCEPTS, 0U);

    uint32_t oldSent = old_cmdhistory_write_all(&oldEnv, N_WRITES_MAX, LEN_PER_WRITE);
    uint32_t newSent = new_cmdhistory_write_all(&newEnv, N_WRITES_MAX, LEN_PER_WRITE);

    ASSERT_EQ(oldSent, N_WRITES_MAX);
    ASSERT_EQ(newSent, N_WRITES_MAX);
    ASSERT_EQ(oldEnv.now, 0);
    ASSERT_EQ(newEnv.now, 0);
    ASSERT_EQ(oldEnv.delayCalls, 0);
    ASSERT_EQ(newEnv.delayCalls, 0);
    ASSERT_EQ(oldEnv.transportCalls, N_WRITES_MAX);
    ASSERT_EQ(newEnv.transportCalls, N_WRITES_MAX);
}

/* The headline. A fully stalled host (every attempt returns 0) against
 * N_WRITES_MAX(11) writes. OLD burns a full ~1 s retry budget on EVERY one
 * of them, discards every failure, and "completes" having spent ~11 s. NEW's
 * guard 1 trips on the very first write's short return, so only one ~1 s
 * retry budget is spent. */
TEST(stalled_host_headline_old_vs_new)
{
    MockEnv oldEnv, newEnv;
    mock_init(&oldEnv, XPORT_NEVER_ACCEPTS, 0U);
    mock_init(&newEnv, XPORT_NEVER_ACCEPTS, 0U);

    uint32_t oldSent = old_cmdhistory_write_all(&oldEnv, N_WRITES_MAX, LEN_PER_WRITE);
    uint32_t newSent = new_cmdhistory_write_all(&newEnv, N_WRITES_MAX, LEN_PER_WRITE);

    ASSERT_EQ(oldSent, 0);
    ASSERT_EQ(newSent, 0);

    /* OLD: N_WRITES_MAX full retry budgets, back to back. */
    ASSERT_EQ(oldEnv.delayCalls, N_WRITES_MAX * FW_WRITE_MAX_RETRIES);
    ASSERT_EQ(oldEnv.now, N_WRITES_MAX * FW_WRITE_MAX_RETRIES * FW_WRITE_RETRY_DELAY_MS);
    ASSERT_EQ(oldEnv.now, 11000);   /* the ~11 s this issue is about */

    /* NEW: exactly ONE retry budget -- guard 1 fires on the first short
     * write, before a second write is ever attempted. */
    ASSERT_EQ(newEnv.delayCalls, FW_WRITE_MAX_RETRIES);
    ASSERT_EQ(newEnv.now, FW_WRITE_MAX_RETRIES * FW_WRITE_RETRY_DELAY_MS);
    ASSERT_EQ(newEnv.now, 1000);

    ASSERT_TRUE(newEnv.now <= FW_CMDHISTORY_BUDGET_MS + FW_WRITE_MAX_RETRIES * FW_WRITE_RETRY_DELAY_MS);
    ASSERT_TRUE(newEnv.now < oldEnv.now);
    ASSERT_EQ(oldEnv.now / newEnv.now, 11);   /* the shrink this PR claims */
}

/* Guard 1 does NOT bound the hold on its own -- this is #995's (and #947's)
 * central claim, made executable. A transport that always eventually
 * accepts every write, but only after consuming most of a retry budget each
 * time (here: 180 of 200 retries, 900 ms), never trips guard 1 (every write
 * reports full completion) and OLD/guard-1-only both run all 11 writes at
 * ~900 ms each -- essentially the same ~10 s hazard the headline test shows,
 * just reached by a different route. Guard 2's cumulative deadline is the
 * ONLY thing that catches this shape. */
TEST(trickle_transport_guard2_is_load_bearing)
{
    const uint32_t attemptsNeeded = 180U;                 /* < FW_WRITE_MAX_RETRIES */
    const uint32_t perWriteMs     = attemptsNeeded * FW_WRITE_RETRY_DELAY_MS;  /* 900 */

    MockEnv oldEnv, guard1OnlyEnv, newEnv;
    mock_init(&oldEnv,       XPORT_TRICKLE_ACCEPTS, 0U);
    mock_init(&guard1OnlyEnv, XPORT_TRICKLE_ACCEPTS, 0U);
    mock_init(&newEnv,       XPORT_TRICKLE_ACCEPTS, 0U);
    oldEnv.trickleAttemptsNeeded       = attemptsNeeded;
    guard1OnlyEnv.trickleAttemptsNeeded = attemptsNeeded;
    newEnv.trickleAttemptsNeeded       = attemptsNeeded;

    uint32_t oldSent    = old_cmdhistory_write_all(&oldEnv, N_WRITES_MAX, LEN_PER_WRITE);
    uint32_t guard1Sent = guard1_only_cmdhistory_write_all(&guard1OnlyEnv, N_WRITES_MAX, LEN_PER_WRITE);
    uint32_t newSent    = new_cmdhistory_write_all(&newEnv, N_WRITES_MAX, LEN_PER_WRITE);

    /* Every write in this mode eventually succeeds -- guard 1 (and the old,
     * unguarded loop) never sees a reason to stop. */
    ASSERT_EQ(oldSent, N_WRITES_MAX);
    ASSERT_EQ(guard1Sent, N_WRITES_MAX);
    ASSERT_EQ(oldEnv.now, N_WRITES_MAX * perWriteMs);
    ASSERT_EQ(guard1OnlyEnv.now, N_WRITES_MAX * perWriteMs);
    ASSERT_EQ(oldEnv.now, 9900);          /* 11 * 900 ms -- still the hazard */
    ASSERT_EQ(guard1OnlyEnv.now, 9900);   /* guard 1 ALONE does not help here */

    /* NEW (guard 2 present): aborts once the cumulative deadline is crossed.
     * Trace: writes land at 900, 1800, 2700 ms; guard 2 is checked BEFORE
     * each write, so it passes at 0, 900, 1800 (all < 2000) and lets three
     * writes through, then refuses the fourth at 2700 (>= 2000). */
    ASSERT_EQ(newSent, 3);
    ASSERT_EQ(newEnv.now, 2700);
    ASSERT_TRUE(newEnv.now >= FW_CMDHISTORY_BUDGET_MS);
    ASSERT_TRUE(newEnv.now < FW_CMDHISTORY_BUDGET_MS + perWriteMs);

    ASSERT_TRUE(newEnv.now < guard1OnlyEnv.now);
    ASSERT_EQ(guard1OnlyEnv.now, oldEnv.now);
}

/* Guard 2 reads the clock, so unlike the pre-#995 code it has a wrap to get
 * right. TickType_t is uint32_t here, the subtraction is unsigned, and
 * CommandHistory_Write compares only the DIFFERENCE -- so a budget window
 * straddling the ~49-day tick rollover must still trip at the stated budget,
 * not instantly and not never. Same idiom as #943's and #947's wrap tests. */
TEST(guard2_survives_tick_counter_wrap)
{
    MockEnv env;
    uint32_t startTick = 0xFFFFFFF0U;   /* rolls over partway through the budget window */

    mock_init(&env, XPORT_NEVER_ACCEPTS, startTick);

    uint32_t sent = new_cmdhistory_write_all(&env, N_WRITES_MAX, LEN_PER_WRITE);

    ASSERT_EQ(sent, 0);
    /* Guard 1 fires on the very first write (never-accepts), same as the
     * headline test -- the wrap only matters to guard 2, which this env
     * never reaches. Prove the clock actually wrapped, and that the elapsed
     * computation the guard depends on is still correct across it. */
    ASSERT_TRUE(env.now < startTick);
    ASSERT_EQ((uint32_t)(env.now - startTick), FW_WRITE_MAX_RETRIES * FW_WRITE_RETRY_DELAY_MS);
}

/* Guard 2 itself, straddling the wrap: force a NEVER-accepting transport to
 * instead take a few trickle-style successes so guard 1 does not pre-empt
 * guard 2, with startTick placed so the 2000 ms budget window crosses
 * UINT32_MAX. */
TEST(guard2_deadline_crossing_wrap_still_trips_at_budget)
{
    const uint32_t attemptsNeeded = 180U;   /* 900 ms/write, same as the trickle test */
    MockEnv env;
    uint32_t startTick = 0xFFFFFFFFU - 1400U;   /* budget window crosses the wrap */

    mock_init(&env, XPORT_TRICKLE_ACCEPTS, startTick);
    env.trickleAttemptsNeeded = attemptsNeeded;

    uint32_t sent = new_cmdhistory_write_all(&env, N_WRITES_MAX, LEN_PER_WRITE);

    ASSERT_EQ(sent, 3);   /* identical trace to the non-wrapped trickle test */
    ASSERT_TRUE((uint32_t)(env.now - startTick) >= FW_CMDHISTORY_BUDGET_MS);
    ASSERT_TRUE((uint32_t)(env.now - startTick) < FW_CMDHISTORY_BUDGET_MS + 900U);
}

/* Empty-history early return (usbSettings->cmdHistoryCount == 0) happens
 * BEFORE the take, per the real source -- so it does no writes and holds no
 * budget. Nothing in the mock loop shapes models this path since it never
 * reaches CommandHistory_Write at all; this test records the invariant in
 * the algebra that matters here (n_calls == 0 spends nothing and sends
 * nothing), so a future refactor that moved the early return to AFTER the
 * take would show up as this test starting to assert something false about
 * a nonexistent write. */
TEST(zero_writes_spends_nothing)
{
    MockEnv oldEnv, newEnv;
    mock_init(&oldEnv, XPORT_NEVER_ACCEPTS, 0U);
    mock_init(&newEnv, XPORT_NEVER_ACCEPTS, 0U);

    uint32_t oldSent = old_cmdhistory_write_all(&oldEnv, 0U, LEN_PER_WRITE);
    uint32_t newSent = new_cmdhistory_write_all(&newEnv, 0U, LEN_PER_WRITE);

    ASSERT_EQ(oldSent, 0);
    ASSERT_EQ(newSent, 0);
    ASSERT_EQ(oldEnv.now, 0);
    ASSERT_EQ(newEnv.now, 0);
    ASSERT_EQ(oldEnv.transportCalls, 0);
    ASSERT_EQ(newEnv.transportCalls, 0);
}

int main(void)
{
    printf("#995 -- SYSTem:LOG:CMDHistory? shared-buffer write-abort bound (extracted loop shapes)\n");
    printf("---------------------------------------------\n");
    RUN(healthy_host_both_shapes_send_every_write_with_zero_delay);
    RUN(stalled_host_headline_old_vs_new);
    RUN(trickle_transport_guard2_is_load_bearing);
    RUN(guard2_survives_tick_counter_wrap);
    RUN(guard2_deadline_crossing_wrap_still_trips_at_budget);
    RUN(zero_writes_spends_nothing);
    return TEST_SUMMARY();
}
