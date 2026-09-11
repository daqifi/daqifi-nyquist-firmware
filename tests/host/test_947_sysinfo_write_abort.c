/* ==========================================================================
 * test_947_sysinfo_write_abort.c -- issue #947
 *
 * WHAT IS UNDER TEST
 *
 * SCPI_SysInfoTextGet (SYSTem:INFo?, SCPIInterface.c) holds the single shared
 * SCPI response buffer's mutex (gScpiRespMutex, #347) from one take at entry
 * to one give at exit, and used to emit its reply as ~90 separate
 * context->interface->write(...) calls in between -- ~37 written out at the
 * source, plus up to one per enabled ADC channel and one per DIO bit -- with
 * every return value discarded. On both transports that write is
 * SCPI_WriteWithRetry (SCPIInterface.c, ~line 8781):
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
 * has stopped reading, every one of the ~90 writes independently burns its
 * own ~1 s before giving up -- so the pre-#947 callback could hold the mutex
 * for ~90 s, and every OTHER SCPI callback on either transport that needed
 * the shared buffer queued behind it for the same ~90 s.
 *
 * THE FIX (SysInfoText_Write, SCPIInterface.c) adds two guards around every
 * write in SCPI_SysInfoTextGet, in this order:
 *
 *   1. Short write => abort. A write that does not return its full length
 *      proves SCPI_WriteWithRetry already spent its whole ~1 s budget without
 *      completing, so continuing to the next section would spend another ~1 s
 *      on bytes that are equally undeliverable.
 *   2. Cumulative deadline (SCPI_SYSINFO_WRITE_BUDGET_MS = 2000, checked
 *      BEFORE each write). Guard 1 ALONE does not bound the hold: a write
 *      that completes on its last allowed retry returns its full length and
 *      never trips guard 1, so a transport that drains at exactly the
 *      trickle rate letting every write finish just inside its own ~1 s
 *      budget still reaches ~90 s with only guard 1 in place. Guard 2 is what
 *      actually bounds that case.
 *
 * HOW IT IS TESTED
 *
 * SCPIInterface.c is not a host-test candidate (libscpi + FreeRTOS + the
 * whole board/driver graph), so -- same approach as
 * test_943_bench_stall_bound.c -- this file re-implements the SHAPE of
 * SCPI_WriteWithRetry and of SCPI_SysInfoTextGet's write loop (pre- and
 * post-#947) against an injected mock clock and mock transport, and compares
 * their verdicts on identical inputs. What is proven is the ALGEBRA of the
 * two shapes, which is exactly what #947 is about.
 *
 * FIDELITY -- what the extracted functions are NOT
 *
 * 1. mock_write_with_retry mirrors SCPI_WriteWithRetry's LOOP exactly (same
 *    condition, same increment/break placement, same retry decrement), with
 *    writeFn/vTaskDelay replaced by mock counterparts. It does not move any
 *    actual bytes.
 * 2. new_sysinfo_write_all mirrors SysInfoText_Write's two guards plus the
 *    SYSINFO_WRITE_OR_ABORT macro's early-exit-on-failure, and samples
 *    startTick once, exactly like SCPI_SysInfoTextGet samples it once right
 *    after SCPI_ResponseBuf_Take(). It does not model the ~90 individual
 *    write call sites, the reply CONTENT, or the __stalled_exit cleanup
 *    (SCPI_ResponseBuf_Give / SCPI_ExecutionError) -- those need the real
 *    board and are out of reach on a host.
 * 3. old_sysinfo_write_all mirrors the pre-#947 shape: every
 *    context->interface->write() return value was discarded, so the loop
 *    calls all N writes regardless of any individual failure.
 * 4. guard1_only_sysinfo_write_all is not a real historical shape -- it is
 *    the SPECIFIC mutation "keep the short-write check, delete the
 *    cumulative deadline" that the fix's own comment (SysInfoText_Write)
 *    argues against. It exists so that argument is an executable assertion
 *    (trickle_transport_guard2_is_load_bearing below) rather than only prose.
 * 5. configTICK_RATE_HZ is 1000 and TickType_t is 32-bit
 *    (firmware/src/config/default/FreeRTOSConfig.h:58,126), so one tick is
 *    one millisecond and pdMS_TO_TICKS is the identity here, same as #943.
 * 6. The three firmware constants this file depends on
 *    (SCPI_WRITE_MAX_RETRIES, SCPI_WRITE_RETRY_DELAY_MS,
 *    SCPI_SYSINFO_WRITE_BUDGET_MS) are DUPLICATED here as FW_* macros. The
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
#define FW_SYSINFO_BUDGET_MS     2000U   /* SCPI_SYSINFO_WRITE_BUDGET_MS */

/* Representative call count. The real function does ~37 source-level writes
 * plus up to 16 per-channel and 24 per-DIO-bit writes (issue text: "~90").
 * Nothing below depends on this exact number -- see the swept variant. */
#define N_WRITES_REPRESENTATIVE   90U
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

/* Stands in for vTaskDelay(pdMS_TO_TICKS(nominalMs)). No preemption-stretch
 * modelling here (that is #943's concern for a different loop) -- 1:1 is
 * enough to prove #947's algebra. */
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

/* Mirrors SCPI_WriteWithRetry (SCPIInterface.c ~8781) exactly: same loop
 * condition, same increment-then-break, same retry decrement. */
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

/* PRE-#947. context->interface->write(...)'s return value was discarded at
 * every one of the ~90 call sites, so the loop attempts all n_calls
 * regardless of any individual failure. Returns how many of them happened to
 * report full completion (bookkeeping only -- the real code never checked
 * this either). */
static uint32_t old_sysinfo_write_all(MockEnv *env, size_t n_calls, size_t len_per_call)
{
    uint32_t sent = 0;
    size_t i;
    for (i = 0; i < n_calls; i++) {
        size_t w = mock_write_with_retry(env, len_per_call);
        if (w == len_per_call) sent++;
    }
    return sent;
}

/* POST-#947. Mirrors SysInfoText_Write's two guards plus
 * SYSINFO_WRITE_OR_ABORT's early exit: guard 2 (cumulative deadline) is
 * checked BEFORE the write, guard 1 (short-write) after it. startTick is
 * sampled once, like the real code samples it once right after the take. */
static uint32_t new_sysinfo_write_all(MockEnv *env, size_t n_calls, size_t len_per_call)
{
    uint32_t sent = 0;
    uint32_t startTick = mock_tick_count(env);
    size_t i;
    for (i = 0; i < n_calls; i++) {
        /* Guard 2, unsigned-subtraction wrap-safe (same idiom as #943's
         * tick-wrap test and as the real SysInfoText_Write). */
        if ((uint32_t)(mock_tick_count(env) - startTick) >= FW_SYSINFO_BUDGET_MS) {
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

/* NOT a historical shape. This is the specific mutation "keep guard 1, delete
 * guard 2" -- the one SysInfoText_Write's own comment argues against ("(1)
 * alone does not actually bound the hold"). Exists so that argument is an
 * assertion, not only prose; see trickle_transport_guard2_is_load_bearing. */
static uint32_t guard1_only_sysinfo_write_all(MockEnv *env, size_t n_calls, size_t len_per_call)
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
 * differs in how many writes it sends. This is what "no reply content change
 * for a healthy host" means at the algebra level -- the OLD and NEW loops
 * produce the identical write count and zero elapsed time. */
TEST(healthy_host_both_shapes_send_every_write_with_zero_delay)
{
    MockEnv oldEnv, newEnv;
    mock_init(&oldEnv, XPORT_ALWAYS_ACCEPTS, 0U);
    mock_init(&newEnv, XPORT_ALWAYS_ACCEPTS, 0U);

    uint32_t oldSent = old_sysinfo_write_all(&oldEnv, N_WRITES_REPRESENTATIVE, LEN_PER_WRITE);
    uint32_t newSent = new_sysinfo_write_all(&newEnv, N_WRITES_REPRESENTATIVE, LEN_PER_WRITE);

    ASSERT_EQ(oldSent, N_WRITES_REPRESENTATIVE);
    ASSERT_EQ(newSent, N_WRITES_REPRESENTATIVE);
    ASSERT_EQ(oldEnv.now, 0);
    ASSERT_EQ(newEnv.now, 0);
    ASSERT_EQ(oldEnv.delayCalls, 0);
    ASSERT_EQ(newEnv.delayCalls, 0);
    /* One transport attempt per write, both shapes: no retries needed. */
    ASSERT_EQ(oldEnv.transportCalls, N_WRITES_REPRESENTATIVE);
    ASSERT_EQ(newEnv.transportCalls, N_WRITES_REPRESENTATIVE);
}

/* The headline. A fully stalled host (every attempt returns 0) against
 * N_WRITES_REPRESENTATIVE(90) writes. OLD burns a full ~1 s retry budget on
 * EVERY one of them, discards every failure, and "completes" having spent
 * ~90 s. NEW's guard 1 trips on the very first write's short return, so only
 * one ~1 s retry budget is spent -- if the fix regressed to the pre-#947
 * shape this is the assertion that screams. */
TEST(stalled_host_headline_old_vs_new)
{
    MockEnv oldEnv, newEnv;
    mock_init(&oldEnv, XPORT_NEVER_ACCEPTS, 0U);
    mock_init(&newEnv, XPORT_NEVER_ACCEPTS, 0U);

    uint32_t oldSent = old_sysinfo_write_all(&oldEnv, N_WRITES_REPRESENTATIVE, LEN_PER_WRITE);
    uint32_t newSent = new_sysinfo_write_all(&newEnv, N_WRITES_REPRESENTATIVE, LEN_PER_WRITE);

    ASSERT_EQ(oldSent, 0);
    ASSERT_EQ(newSent, 0);

    /* OLD: N_WRITES_REPRESENTATIVE full retry budgets, back to back. */
    ASSERT_EQ(oldEnv.delayCalls, N_WRITES_REPRESENTATIVE * FW_WRITE_MAX_RETRIES);
    ASSERT_EQ(oldEnv.now, N_WRITES_REPRESENTATIVE * FW_WRITE_MAX_RETRIES * FW_WRITE_RETRY_DELAY_MS);
    ASSERT_EQ(oldEnv.now, 90000);   /* the ~90 s this issue is about */

    /* NEW: exactly ONE retry budget -- guard 1 fires on the first short
     * write, before a second write is ever attempted. */
    ASSERT_EQ(newEnv.delayCalls, FW_WRITE_MAX_RETRIES);
    ASSERT_EQ(newEnv.now, FW_WRITE_MAX_RETRIES * FW_WRITE_RETRY_DELAY_MS);
    ASSERT_EQ(newEnv.now, 1000);

    /* The bound SysInfoText_Write's own comment claims: budget + one retry
     * budget. Guard 2 never even gets a chance to fire here (the very first
     * write is still inside the budget window), so this is purely guard 1 --
     * and it is already far inside the combined ceiling. */
    ASSERT_TRUE(newEnv.now <= FW_SYSINFO_BUDGET_MS + FW_WRITE_MAX_RETRIES * FW_WRITE_RETRY_DELAY_MS);
    ASSERT_TRUE(newEnv.now < oldEnv.now);
    ASSERT_EQ(oldEnv.now / newEnv.now, 90);   /* the shrink this PR claims */
}

/* Guard 1 does NOT bound the hold on its own -- this is #947's central claim,
 * made executable. A transport that always eventually accepts every write,
 * but only after consuming most of a retry budget each time (here: 180 of
 * 200 retries, 900 ms), never trips guard 1 (every write reports full
 * completion) and OLD/guard-1-only both run all 90 writes at ~900 ms each --
 * essentially the same ~90 s hazard the headline test shows, just reached by
 * a different route. Guard 2's cumulative deadline is the ONLY thing that
 * catches this shape. */
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

    uint32_t oldSent       = old_sysinfo_write_all(&oldEnv, N_WRITES_REPRESENTATIVE, LEN_PER_WRITE);
    uint32_t guard1Sent     = guard1_only_sysinfo_write_all(&guard1OnlyEnv, N_WRITES_REPRESENTATIVE, LEN_PER_WRITE);
    uint32_t newSent       = new_sysinfo_write_all(&newEnv, N_WRITES_REPRESENTATIVE, LEN_PER_WRITE);

    /* Every write in this mode eventually succeeds -- guard 1 (and the old,
     * unguarded loop) never sees a reason to stop. */
    ASSERT_EQ(oldSent, N_WRITES_REPRESENTATIVE);
    ASSERT_EQ(guard1Sent, N_WRITES_REPRESENTATIVE);
    ASSERT_EQ(oldEnv.now, N_WRITES_REPRESENTATIVE * perWriteMs);
    ASSERT_EQ(guard1OnlyEnv.now, N_WRITES_REPRESENTATIVE * perWriteMs);
    ASSERT_EQ(oldEnv.now, 81000);          /* ~81 s -- still #947's hazard */
    ASSERT_EQ(guard1OnlyEnv.now, 81000);   /* guard 1 ALONE does not help here */

    /* NEW (guard 2 present): aborts once the cumulative deadline is crossed.
     * Trace: writes land at 900, 1800, 2700 ms; guard 2 is checked BEFORE
     * each write, so it passes at 0, 900, 1800 (all < 2000) and lets three
     * writes through, then refuses the fourth at 2700 (>= 2000). */
    ASSERT_EQ(newSent, 3);
    ASSERT_EQ(newEnv.now, 2700);
    ASSERT_TRUE(newEnv.now >= FW_SYSINFO_BUDGET_MS);
    ASSERT_TRUE(newEnv.now < FW_SYSINFO_BUDGET_MS + perWriteMs);

    /* The point of this test, stated as numbers: guard 2 is worth ~30x here,
     * and guard 1 alone is worth nothing. */
    ASSERT_TRUE(newEnv.now < guard1OnlyEnv.now);
    ASSERT_EQ(guard1OnlyEnv.now, oldEnv.now);
}

/* Guard 2 reads the clock, so unlike the pre-#947 code it has a wrap to get
 * right. TickType_t is uint32_t here, the subtraction is unsigned, and
 * SysInfoText_Write compares only the DIFFERENCE -- so a budget window
 * straddling the ~49-day tick rollover must still trip at the stated budget,
 * not instantly and not never. Same idiom as #943's wrap test. */
TEST(guard2_survives_tick_counter_wrap)
{
    MockEnv env;
    uint32_t startTick = 0xFFFFFFF0U;   /* rolls over partway through the budget window */

    mock_init(&env, XPORT_NEVER_ACCEPTS, startTick);

    uint32_t sent = new_sysinfo_write_all(&env, N_WRITES_REPRESENTATIVE, LEN_PER_WRITE);

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

    uint32_t sent = new_sysinfo_write_all(&env, N_WRITES_REPRESENTATIVE, LEN_PER_WRITE);

    ASSERT_EQ(sent, 3);   /* identical trace to the non-wrapped trickle test */
    ASSERT_TRUE((uint32_t)(env.now - startTick) >= FW_SYSINFO_BUDGET_MS);
    ASSERT_TRUE((uint32_t)(env.now - startTick) < FW_SYSINFO_BUDGET_MS + 900U);
}

int main(void)
{
    printf("#947 -- SYSTem:INFo? shared-buffer write-abort bound (extracted loop shapes)\n");
    printf("---------------------------------------------\n");
    RUN(healthy_host_both_shapes_send_every_write_with_zero_delay);
    RUN(stalled_host_headline_old_vs_new);
    RUN(trickle_transport_guard2_is_load_bearing);
    RUN(guard2_survives_tick_counter_wrap);
    RUN(guard2_deadline_crossing_wrap_still_trips_at_budget);
    return TEST_SUMMARY();
}
