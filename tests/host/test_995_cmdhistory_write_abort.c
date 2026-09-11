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
 * On both transports that write is SCPI_WriteWithRetry (SCPIInterface.c):
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
 * THE FIX (CmdHistoryWrite, SCPIInterface.c) is a self-gating helper every
 * write call site now goes through unconditionally -- no early return, no
 * goto, a single SCPI_ResponseBuf_Give() on the function's one exit path.
 * `writeOk` is a bool local, `true` on entry; the helper no-ops immediately
 * if it is already `false`, and clears it on the first write that does not
 * bound cleanly. Two guards inside the helper, checked in this order:
 *
 *   1. Cumulative deadline (SCPI_CMDHISTORY_WRITE_BUDGET_MS = 2000, checked
 *      BEFORE the transport call, against a startTick sampled once right
 *      after the mutex take). This is the guard that actually BOUNDS the
 *      hold: a write that completes on its very last allowed retry returns
 *      its full length and never looks short, so a transport draining at
 *      exactly the trickle rate that lets every ~1 s-budgeted write juuust
 *      barely finish would defeat a short-write-only check and still reach
 *      ~11 s, one full budget at a time.
 *   2. Short write (after the call). SCPI_WriteWithRetry has no resend path,
 *      so a return shorter than requested has already DROPPED those bytes --
 *      continuing to the next write would just spend another ~1 s producing
 *      a reply the host will never see intact.
 *
 * Once either guard trips, every LATER call in the same invocation is a
 * bool-check no-op: the surrounding for-loop still iterates (it still runs
 * the side-effect-free snprintf calls), but CmdHistoryWrite returns
 * immediately without touching the clock or the transport, so the aggregate
 * time and call-count cost of "trip then keep iterating to completion" is
 * IDENTICAL to "trip then stop iterating" -- the loop shape was chosen for
 * mechanical simplicity (no early-return/goto control flow to get wrong),
 * not because it does less work once latched.
 *
 * HOW IT IS TESTED
 *
 * SCPIInterface.c is not a host-test candidate (libscpi + FreeRTOS + the
 * whole board/driver graph), so -- same approach as
 * test_943_bench_stall_bound.c -- this file re-implements the SHAPE of
 * SCPI_WriteWithRetry and of CmdHistoryWrite/SCPI_GetCommandHistory's write
 * loop (pre- and post-#995) against an injected mock clock and mock
 * transport, and compares their verdicts on identical inputs. What is
 * proven is the ALGEBRA of the two shapes, which is exactly what #995 is
 * about.
 *
 * FIDELITY -- what the extracted functions are NOT
 *
 * 1. mock_write_with_retry mirrors SCPI_WriteWithRetry's LOOP exactly (same
 *    condition, same increment/break placement, same retry decrement), with
 *    writeFn/vTaskDelay replaced by mock counterparts. It does not move any
 *    actual bytes.
 * 2. mock_cmdhistory_write mirrors CmdHistoryWrite line-for-line: the
 *    already-false-latch no-op check first, then guard 1 (cumulative
 *    deadline) before the transport call, then guard 2 (short write) after
 *    it -- same order, same in/out `bool *ok` parameter shape.
 *    new_cmdhistory_write_all then mirrors SCPI_GetCommandHistory's own
 *    for-loop: it calls mock_cmdhistory_write unconditionally on every one
 *    of n_calls iterations (no break, no goto), exactly like the real
 *    for-loop calls CmdHistoryWrite unconditionally at both call sites. It
 *    does not model the reply CONTENT, the SCPI_ResponseBuf_Take/Give pair,
 *    or the LOG_E side effect -- those need the real board and are out of
 *    reach on a host.
 * 3. old_cmdhistory_write_all mirrors the pre-#995 shape: every
 *    context->interface->write() return value was discarded, so the loop
 *    calls all n_calls writes regardless of any individual failure.
 * 4. guard1_only_cmdhistory_write_all is not a real historical shape -- it
 *    is the SPECIFIC mutation "keep the cumulative-deadline guard, delete
 *    the short-write guard". It exists so the fix's own "the short-write
 *    guard is not redundant with the deadline guard" argument is an
 *    executable assertion (short_write_guard_is_load_bearing below) rather
 *    than only prose.
 * 5. configTICK_RATE_HZ is 1000 and TickType_t is 32-bit
 *    (firmware/src/config/default/FreeRTOSConfig.h), so one tick is one
 *    millisecond and pdMS_TO_TICKS is the identity here, same assumption as
 *    #943. The Makefile target for THIS file greps FreeRTOSConfig.h for
 *    both and refuses to build if either has drifted.
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

/* Mirrors CmdHistoryWrite exactly: no-op-if-already-false first, guard 1
 * (cumulative deadline) BEFORE the transport call, guard 2 (short write)
 * AFTER it. `ok` is the in/out latch, matching the real function's `bool
 * *ok` parameter -- the caller owns one `bool` for the whole invocation,
 * same as SCPI_GetCommandHistory's `writeOk` local. */
static void mock_cmdhistory_write(MockEnv *env, uint32_t startTick, int *ok,
                                   size_t len)
{
    if (!*ok) {
        return;
    }
    if ((uint32_t)(mock_tick_count(env) - startTick) >= FW_CMDHISTORY_BUDGET_MS) {
        *ok = 0;
        return;
    }
    size_t w = mock_write_with_retry(env, len);
    if (w != len) {
        *ok = 0;
    }
}

/* POST-#995. Mirrors SCPI_GetCommandHistory's own for-loop: calls
 * mock_cmdhistory_write UNCONDITIONALLY on every one of n_calls iterations
 * -- no break, no goto -- exactly like the real loop calls CmdHistoryWrite
 * unconditionally at both call sites and lets the helper's own latch decide
 * whether anything actually happens. startTick is sampled once, like the
 * real code samples it once right after SCPI_ResponseBuf_Take(). Returns
 * the number of writes that actually completed (bookkeeping only, mirrors
 * `sent` accounting used by the other two shapes above/below for
 * comparison -- the real code has no equivalent counter). */
static uint32_t new_cmdhistory_write_all(MockEnv *env, size_t n_calls, size_t len_per_call)
{
    uint32_t sent = 0;
    uint32_t startTick = mock_tick_count(env);
    int ok = 1;
    size_t i;
    for (i = 0; i < n_calls; i++) {
        int okBefore = ok;
        mock_cmdhistory_write(env, startTick, &ok, len_per_call);
        if (okBefore && ok) {
            sent++;
        }
    }
    return sent;
}

/* NOT a historical shape. This is the specific mutation "keep guard 1
 * (cumulative deadline), delete guard 2 (short write)" -- exists so the
 * fix's own "the short-write guard is not redundant" argument is an
 * assertion, not only prose. Unlike CmdHistoryWrite this still runs every
 * iteration unconditionally (no early stop at all, matching the real loop's
 * shape), it simply never clears `ok` on a short write. */
static uint32_t guard1_only_cmdhistory_write_all(MockEnv *env, size_t n_calls, size_t len_per_call)
{
    uint32_t sent = 0;
    uint32_t startTick = mock_tick_count(env);
    size_t i;
    for (i = 0; i < n_calls; i++) {
        if ((uint32_t)(mock_tick_count(env) - startTick) >= FW_CMDHISTORY_BUDGET_MS) {
            continue;   /* deadline guard still active; just no short-write latch */
        }
        size_t w = mock_write_with_retry(env, len_per_call);
        if (w == len_per_call) sent++;
    }
    return sent;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* Byte-for-byte parity requirement: against a healthy host every write
 * completes on its first attempt, so all three shapes send every write with
 * zero delay. */
TEST(healthy_host_all_shapes_send_every_write_with_zero_delay)
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
 * short-write guard trips on the very first write's short return, so only
 * one ~1 s retry budget is spent -- every later iteration still runs (still
 * calls mock_cmdhistory_write ten more times), but each call is a `!*ok`
 * no-op that touches neither the clock nor the transport. */
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
    ASSERT_EQ(oldEnv.transportCalls, N_WRITES_MAX * FW_WRITE_MAX_RETRIES);

    /* NEW: exactly ONE retry budget's worth of clock/transport activity --
     * the short-write guard latches after the first mock_write_with_retry
     * call, and every one of the remaining 10 loop iterations is a no-op
     * that adds zero delay calls and zero transport calls. */
    ASSERT_EQ(newEnv.delayCalls, FW_WRITE_MAX_RETRIES);
    ASSERT_EQ(newEnv.now, FW_WRITE_MAX_RETRIES * FW_WRITE_RETRY_DELAY_MS);
    ASSERT_EQ(newEnv.now, 1000);
    ASSERT_EQ(newEnv.transportCalls, FW_WRITE_MAX_RETRIES);

    ASSERT_TRUE(newEnv.now <= FW_CMDHISTORY_BUDGET_MS + FW_WRITE_MAX_RETRIES * FW_WRITE_RETRY_DELAY_MS);
    ASSERT_TRUE(newEnv.now < oldEnv.now);
    ASSERT_EQ(oldEnv.now / newEnv.now, 11);   /* the shrink this PR claims */
}

/* The cumulative-deadline guard does not depend on the short-write guard --
 * this is #995's central claim about WHY two guards, made executable. A
 * transport that always eventually accepts every write, but only after
 * consuming most of a retry budget each time (here: 180 of 200 retries,
 * 900 ms), never trips the short-write guard (every write reports full
 * completion) and OLD/guard-1-only(deadline-only, no short-write latch)
 * both still run every write to completion, at ~900 ms each -- essentially
 * the same ~10 s hazard the headline test shows, just reached by a
 * different route. NEW (both guards) is stopped by the deadline alone. */
TEST(trickle_transport_deadline_guard_alone_stops_it)
{
    const uint32_t attemptsNeeded = 180U;                 /* < FW_WRITE_MAX_RETRIES */
    const uint32_t perWriteMs     = attemptsNeeded * FW_WRITE_RETRY_DELAY_MS;  /* 900 */

    MockEnv oldEnv, newEnv;
    mock_init(&oldEnv, XPORT_TRICKLE_ACCEPTS, 0U);
    mock_init(&newEnv, XPORT_TRICKLE_ACCEPTS, 0U);
    oldEnv.trickleAttemptsNeeded = attemptsNeeded;
    newEnv.trickleAttemptsNeeded = attemptsNeeded;

    uint32_t oldSent = old_cmdhistory_write_all(&oldEnv, N_WRITES_MAX, LEN_PER_WRITE);
    uint32_t newSent = new_cmdhistory_write_all(&newEnv, N_WRITES_MAX, LEN_PER_WRITE);

    /* OLD never looks at any return value, so every write "succeeds" (from
     * its own oblivious point of view) and the hazard is exactly the one
     * this issue is about. */
    ASSERT_EQ(oldSent, N_WRITES_MAX);
    ASSERT_EQ(oldEnv.now, N_WRITES_MAX * perWriteMs);
    ASSERT_EQ(oldEnv.now, 9900);          /* 11 * 900 ms -- still the hazard */

    /* NEW: aborts once the cumulative deadline is crossed. Trace: writes
     * land at 900, 1800, 2700 ms; the deadline is checked BEFORE each write,
     * so it passes at 0, 900, 1800 (all < 2000) and lets three writes
     * through, then refuses the fourth at 2700 (>= 2000) -- the short-write
     * guard never even gets a chance to fire in this scenario, since every
     * attempted write DOES eventually complete in full. */
    ASSERT_EQ(newSent, 3);
    ASSERT_EQ(newEnv.now, 2700);
    ASSERT_TRUE(newEnv.now >= FW_CMDHISTORY_BUDGET_MS);
    ASSERT_TRUE(newEnv.now < FW_CMDHISTORY_BUDGET_MS + perWriteMs);
    ASSERT_TRUE(newEnv.now < oldEnv.now);
}

/* The short-write guard is not redundant with the deadline guard either --
 * this is the complementary claim, made executable via the
 * guard1(deadline)-only mutation. A fully stalled host trips the deadline
 * guard only after the FULL budget window elapses one retry-delay at a
 * time (guard1_only_cmdhistory_write_all has no short-write latch, so it
 * keeps calling mock_write_with_retry every iteration the deadline still
 * permits) -- reaching a much higher clock cost than NEW's single retry
 * budget before the short-write guard trips on write #1. */
TEST(short_write_guard_is_load_bearing)
{
    MockEnv guard1OnlyEnv, newEnv;
    mock_init(&guard1OnlyEnv, XPORT_NEVER_ACCEPTS, 0U);
    mock_init(&newEnv, XPORT_NEVER_ACCEPTS, 0U);

    uint32_t guard1Sent = guard1_only_cmdhistory_write_all(&guard1OnlyEnv, N_WRITES_MAX, LEN_PER_WRITE);
    uint32_t newSent    = new_cmdhistory_write_all(&newEnv, N_WRITES_MAX, LEN_PER_WRITE);

    ASSERT_EQ(guard1Sent, 0);
    ASSERT_EQ(newSent, 0);

    /* guard1-only keeps retrying (each mock_write_with_retry call burns a
     * full ~1 s budget since XPORT_NEVER_ACCEPTS never completes) until the
     * 2000 ms deadline check finally refuses a NEW write attempt -- two full
     * ~1 s writes land (0->1000, 1000->2000), and the deadline then refuses
     * the third at t=2000. */
    ASSERT_EQ(guard1OnlyEnv.now, 2 * FW_WRITE_MAX_RETRIES * FW_WRITE_RETRY_DELAY_MS);
    ASSERT_EQ(guard1OnlyEnv.now, 2000);

    /* NEW stops after exactly one retry budget -- the short-write guard
     * trips before the deadline would ever need to. */
    ASSERT_EQ(newEnv.now, FW_WRITE_MAX_RETRIES * FW_WRITE_RETRY_DELAY_MS);
    ASSERT_EQ(newEnv.now, 1000);
    ASSERT_TRUE(newEnv.now < guard1OnlyEnv.now);
}

/* The deadline guard reads the clock, so unlike the pre-#995 code it has a
 * wrap to get right. TickType_t is uint32_t here, the subtraction is
 * unsigned, and CmdHistoryWrite compares only the DIFFERENCE -- so a budget
 * window straddling the ~49-day tick rollover must still trip at the stated
 * budget, not instantly and not never. Same idiom as #943's wrap
 * reasoning. */
TEST(deadline_guard_survives_tick_counter_wrap)
{
    MockEnv env;
    uint32_t startTick = 0xFFFFFFF0U;   /* rolls over partway through the budget window */

    mock_init(&env, XPORT_NEVER_ACCEPTS, startTick);

    uint32_t sent = new_cmdhistory_write_all(&env, N_WRITES_MAX, LEN_PER_WRITE);

    ASSERT_EQ(sent, 0);
    /* Short-write guard fires on the very first write (never-accepts), same
     * as the headline test -- the wrap only matters to the deadline guard,
     * which this env never reaches. Prove the clock actually wrapped, and
     * that the elapsed computation still comes out correct across it. */
    ASSERT_TRUE(env.now < startTick);
    ASSERT_EQ((uint32_t)(env.now - startTick), FW_WRITE_MAX_RETRIES * FW_WRITE_RETRY_DELAY_MS);
}

/* Deadline guard itself, straddling the wrap: force a NEVER-accepting
 * transport to instead take a few trickle-style successes so the
 * short-write guard does not pre-empt the deadline guard, with startTick
 * placed so the 2000 ms budget window crosses UINT32_MAX. */
TEST(deadline_guard_crossing_wrap_still_trips_at_budget)
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
 * reaches CmdHistoryWrite at all; this test records the invariant in the
 * algebra that matters here (n_calls == 0 spends nothing and sends
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
    RUN(healthy_host_all_shapes_send_every_write_with_zero_delay);
    RUN(stalled_host_headline_old_vs_new);
    RUN(trickle_transport_deadline_guard_alone_stops_it);
    RUN(short_write_guard_is_load_bearing);
    RUN(deadline_guard_survives_tick_counter_wrap);
    RUN(deadline_guard_crossing_wrap_still_trips_at_budget);
    RUN(zero_writes_spends_nothing);
    return TEST_SUMMARY();
}
