/* ==========================================================================
 * test_943_bench_stall_bound.c -- issue #943, part 1
 *
 * WHAT IS UNDER TEST
 *
 * SYST:STOR:SD:BENCHmark (SCPI_StorageSDBenchmark, SCPIStorageSD.c) writes its
 * test payload to the SD circular buffer in 512 B chunks. Inside each chunk it
 * spins until the SD task has drained enough room to accept the bytes, and it
 * is supposed to give up after a SUSTAINED stall of 10 s with no progress.
 *
 * Until #943 that loop read (SCPIStorageSD.c, pre-fix):
 *
 *     size_t written = 0;
 *     uint32_t stallMs = 0;
 *     while ((written < chunkSize) && (stallMs < 10000U)) {
 *         size_t w = sd_card_manager_WriteToBuffer(...);
 *         if (w == 0U) { vTaskDelay(pdMS_TO_TICKS(5)); stallMs += 5U; }
 *         else         { written += w;                 stallMs  = 0U; }
 *     }
 *
 * vTaskDelay guarantees AT LEAST its argument, never exactly it. The benchmark
 * runs on app_USBDeviceTask (priority 7); the two priority-9 deferred
 * interrupt tasks, and anything else the scheduler prefers, can stretch a
 * nominal 5 ms sleep into materially more real time. `stallMs` did not measure
 * that -- it added the NOMINAL 5 once per iteration, so its exit condition was
 * "2000 iterations", which only reads as "10 seconds" if you assume the task
 * is never preempted. Under stretch the loop's real lifetime grows without any
 * bound the code states. That is the #943 defect: not a wrong number, a
 * quantity that is not the quantity the code claims to bound.
 *
 * The fix samples the tick counter instead:
 *
 *     size_t written = 0;
 *     TickType_t lastProgressTick = xTaskGetTickCount();
 *     while (written < chunkSize) {
 *         size_t w = sd_card_manager_WriteToBuffer(...);
 *         if (w == 0U) {
 *             if ((TickType_t)(xTaskGetTickCount() - lastProgressTick) >=
 *                     pdMS_TO_TICKS(SCPI_SD_BENCH_STALL_TIMEOUT_MS)) break;
 *             vTaskDelay(pdMS_TO_TICKS(SCPI_SD_BENCH_STALL_POLL_MS));
 *         } else {
 *             written += w;
 *             lastProgressTick = xTaskGetTickCount();
 *         }
 *     }
 *
 * HOW IT IS TESTED
 *
 * SCPIStorageSD.c is not a host-test candidate -- it pulls in libscpi,
 * FreeRTOS and the whole SD manager. So this file re-implements the two loop
 * SHAPES against an injected mock clock and mock write, and compares their
 * verdicts on identical inputs. What is proven is the ALGEBRA of the two
 * shapes (one counts iterations, the other counts elapsed ticks), which is
 * exactly what #943 is about. See "FIDELITY" at the bottom of this comment for
 * what that does and does not carry over to the real callback.
 *
 * The mock clock models preemption as a multiplier: a vTaskDelay(N ms) costs
 * N * stretch ms of simulated wall clock. stretch == 1 is the no-preemption
 * case the old code implicitly assumed. The headline case uses 8, and the
 * sweep shows the property is not specific to any multiplier.
 *
 * FIDELITY -- what the extracted functions are NOT
 *
 * 1. They cover the INNER per-chunk write loop only. The outer chunk loop, the
 *    per-chunk SCPI_ResponseBuf_TakeTimeout (which is #943 part 2), the
 *    pattern fill and the post-loop error reporting are not modelled.
 * 2. sd_card_manager_WriteToBuffer's pointer argument is passed through to the
 *    mock (so the call shape matches) but no bytes move.
 * 3. configTICK_RATE_HZ is 1000 and TickType_t is 32-bit
 *    (firmware/src/config/default/FreeRTOSConfig.h:58,126), so one tick is one
 *    millisecond and pdMS_TO_TICKS is the identity. The mock clock is
 *    denominated in ms and ticks interchangeably. On a device with a different
 *    tick rate these functions would still mirror the source, but the numbers
 *    below would need re-deriving.
 * 4. The firmware constants are DUPLICATED here as FW_STALL_TIMEOUT_MS /
 *    FW_STALL_POLL_MS. The Makefile target greps SCPIStorageSD.c and fails the
 *    build if they drift, so a stale copy cannot pass silently.
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include "test_framework.h"

/* --------------------------------------------------------------------------
 * Firmware constants, mirrored. Pinned against the real source by the
 * Makefile's grep guard -- if SCPIStorageSD.c changes either value the
 * $(BENCH_BIN) target fails with a message naming this file.
 * ------------------------------------------------------------------------ */
#define FW_STALL_TIMEOUT_MS 10000U   /* SCPI_SD_BENCH_STALL_TIMEOUT_MS */
#define FW_STALL_POLL_MS        5U   /* SCPI_SD_BENCH_STALL_POLL_MS    */

/* configTICK_RATE_HZ == 1000, so pdMS_TO_TICKS is the identity here. */
#define MS_TO_TICKS(ms) ((uint32_t)(ms))

#define CHUNK_SIZE 512U   /* kTestBufferChunk in SCPIStorageSD.c */

/* ==========================================================================
 * Mock environment: injected clock + injected sd_card_manager_WriteToBuffer
 * ========================================================================== */

typedef enum {
    /* The #943 stall. An OPEN_FILE refusal ("no writable bucket") leaves
     * mode != SD_CARD_MANAGER_MODE_WRITE, and WriteToBuffer then returns 0 for
     * every subsequent call with nothing to transition it back. */
    WRITE_NEVER_ACCEPTS = 0,
    /* Healthy card with room: all-or-nothing full accept, first try. */
    WRITE_ALWAYS_ACCEPTS,
    /* Slow but genuinely draining card: accepts acceptBytes once every gapMs
     * of REAL time, 0 in between. Exercises the reset-on-progress arm. */
    WRITE_PERIODIC_PARTIAL
} MockWriteMode;

typedef struct {
    uint32_t      now;           /* simulated tick counter (1 tick == 1 ms)   */
    uint32_t      stretch;       /* an N ms delay costs N*stretch ms          */
    uint32_t      delayCalls;    /* how many times the loop slept             */
    uint32_t      writeCalls;    /* how many times it called WriteToBuffer    */
    MockWriteMode mode;
    size_t        acceptBytes;   /* PERIODIC: bytes accepted per success      */
    uint32_t      gapMs;         /* PERIODIC: real ms between successes       */
    uint32_t      nextAcceptAt;  /* PERIODIC: absolute tick of next success   */
} MockEnv;

static void mock_init(MockEnv *env, MockWriteMode mode, uint32_t stretch,
                      uint32_t startTick)
{
    env->now          = startTick;
    env->stretch      = stretch;
    env->delayCalls   = 0;
    env->writeCalls   = 0;
    env->mode         = mode;
    env->acceptBytes  = 0;
    env->gapMs        = 0;
    env->nextAcceptAt = 0;
}

/* Stands in for xTaskGetTickCount(). */
static uint32_t mock_tick_count(const MockEnv *env)
{
    return env->now;
}

/* Stands in for vTaskDelay(pdMS_TO_TICKS(nominalMs)).
 *
 * THIS is the whole point of the mock: the caller asks for nominalMs and the
 * clock advances by nominalMs * stretch, because vTaskDelay's contract is a
 * MINIMUM. Deliberately unsigned wraparound-safe -- the wrap test starts the
 * clock just below UINT32_MAX. */
static void mock_delay_ms(MockEnv *env, uint32_t nominalMs)
{
    env->now += nominalMs * env->stretch;
    env->delayCalls++;
}

/* Stands in for sd_card_manager_WriteToBuffer(pData, len). The real callee is
 * all-or-nothing and NON-BLOCKING (sd_card_manager.c:3455), which the first
 * two modes mirror; PERIODIC deliberately does NOT, see the loop comment in
 * SCPIStorageSD.c about keeping the reset arm correct for a partial-accepting
 * callee. */
static size_t mock_write_to_buffer(MockEnv *env, const char *pData, size_t len)
{
    env->writeCalls++;
    ASSERT_TRUE(pData != NULL);   /* the loop must never offer a NULL cursor */
    if (len == 0U) {
        return 0U;
    }

    switch (env->mode) {
        case WRITE_ALWAYS_ACCEPTS:
            return len;

        case WRITE_PERIODIC_PARTIAL:
            /* Only ever used with a clock starting at 0 and a run far shorter
             * than 2^32 ticks, so a plain >= is safe here. */
            if (env->now >= env->nextAcceptAt) {
                env->nextAcceptAt = env->now + env->gapMs;
                return (len < env->acceptBytes) ? len : env->acceptBytes;
            }
            return 0U;

        case WRITE_NEVER_ACCEPTS:
        default:
            return 0U;
    }
}

/* ==========================================================================
 * The two loop shapes, extracted line-for-line from SCPIStorageSD.c with
 * xTaskGetTickCount / vTaskDelay / sd_card_manager_WriteToBuffer replaced by
 * their mock counterparts. Nothing else is changed -- same variable roles,
 * same comparison, same increment and reset placement, same loop condition.
 * ========================================================================== */

/* PRE-#943. Note the deadline lives in the LOOP CONDITION (tested before the
 * write), and that it accumulates the NOMINAL poll interval, never the elapsed
 * one -- so its exit is an iteration count wearing millisecond units. */
static size_t old_stall_bound_iterations(MockEnv *env, const char *testBuffer,
                                         size_t chunkSize)
{
    size_t written = 0;
    uint32_t stallMs = 0;
    while ((written < chunkSize) && (stallMs < FW_STALL_TIMEOUT_MS)) {
        size_t w = mock_write_to_buffer(env, testBuffer + written,
                                        chunkSize - written);
        if (w == 0U) {
            mock_delay_ms(env, FW_STALL_POLL_MS);
            stallMs += FW_STALL_POLL_MS;
        } else {
            written += w;
            stallMs = 0U;
        }
    }
    return written;
}

/* POST-#943. The deadline moved out of the loop condition and into the
 * zero-write arm, and it is now a difference of two tick samples. The cast on
 * the subtraction is the real code's, and is what makes it correct across the
 * ~49-day tick wrap. */
static size_t new_stall_bound_ticks(MockEnv *env, const char *testBuffer,
                                    size_t chunkSize)
{
    size_t written = 0;
    uint32_t lastProgressTick = mock_tick_count(env);
    while (written < chunkSize) {
        size_t w = mock_write_to_buffer(env, testBuffer + written,
                                        chunkSize - written);
        if (w == 0U) {
            if ((uint32_t)(mock_tick_count(env) - lastProgressTick) >=
                    MS_TO_TICKS(FW_STALL_TIMEOUT_MS)) {
                break;  /* leaves written < chunkSize -> stall error */
            }
            mock_delay_ms(env, FW_STALL_POLL_MS);
        } else {
            written += w;
            lastProgressTick = mock_tick_count(env);
        }
    }
    return written;
}

/* ==========================================================================
 * Helpers
 * ========================================================================== */

static char gTestBuffer[CHUNK_SIZE];  /* stands in for the shared SCPI buffer */

/* Real ms one poll costs at a given stretch. */
static uint32_t poll_cost_ms(uint32_t stretch)
{
    return FW_STALL_POLL_MS * stretch;
}

/* The new shape samples the deadline once per poll, so it exits at the first
 * poll boundary at or after the deadline: ceil(timeout / pollCost) polls. */
static uint32_t expected_new_polls(uint32_t stretch)
{
    uint32_t p = poll_cost_ms(stretch);
    return (FW_STALL_TIMEOUT_MS + p - 1U) / p;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* The headline. One stretch factor, hard numbers, no formulas -- if the fix is
 * reverted this is the assertion that screams.
 *
 * stretch = 8 means a nominal 5 ms sleep really costs 40 ms, i.e. the
 * benchmark task gets ~12.5% of the CPU across the stall window. That figure
 * is a MODELLING CHOICE, not a measured one (tag: I, not E) -- nothing here
 * claims 8x is what the bench device does. It only has to be big enough that
 * the two verdicts cannot be confused, and 80 s vs 10 s is not a marginal
 * difference. The sweep below shows the property holds for every multiplier,
 * so no single value is load-bearing. */
TEST(stall_headline_8x_preemption_stretch)
{
    MockEnv oldEnv, newEnv;
    size_t oldWritten, newWritten;
    uint32_t oldElapsed, newElapsed;

    mock_init(&oldEnv, WRITE_NEVER_ACCEPTS, 8U, 0U);
    mock_init(&newEnv, WRITE_NEVER_ACCEPTS, 8U, 0U);

    oldWritten = old_stall_bound_iterations(&oldEnv, gTestBuffer, CHUNK_SIZE);
    newWritten = new_stall_bound_ticks(&newEnv, gTestBuffer, CHUNK_SIZE);

    oldElapsed = oldEnv.now;   /* started at 0 */
    newElapsed = newEnv.now;

    /* Both correctly refuse to complete the chunk -- neither is "broken", the
     * defect is purely in HOW LONG the refusal takes. */
    ASSERT_EQ(oldWritten, 0);
    ASSERT_EQ(newWritten, 0);

    /* OLD: exactly 10000/5 = 2000 polls, because that is what its condition
     * counts. At 40 ms of real time each, 2000 polls is 80,000 ms -- the
     * "10 second" bound took 80 seconds. */
    ASSERT_EQ(oldEnv.delayCalls, 2000);
    ASSERT_EQ(oldElapsed, 80000);

    /* NEW: 10000 / 40 = 250 polls, the 250th landing exactly on the deadline.
     * Real elapsed 10,000 ms -- the bound the code states. */
    ASSERT_EQ(newEnv.delayCalls, 250);
    ASSERT_EQ(newElapsed, 10000);

    /* The divergence, stated directly. */
    ASSERT_EQ(oldElapsed, 8U * newElapsed);
    ASSERT_EQ(oldElapsed - newElapsed, 70000);
}

/* The same comparison swept across stretch factors, which is where the shape
 * of each bound shows up:
 *
 *   OLD  delayCalls is INVARIANT in stretch (always 2000) and its real elapsed
 *        is 10000*stretch -- unbounded in the thing it claims to bound.
 *   NEW  delayCalls SHRINKS as each poll costs more, and its real elapsed
 *        stays inside [10000, 10000 + onePoll) at every stretch.
 *
 * stretch == 1 is the control: with zero preemption the two agree exactly.
 * That is the honest statement of the old code -- not wrong, just only correct
 * under an assumption it never checked. It also kills the lazy mutation
 * "make the new shape bail out sooner", which would break this equality. */
TEST(stall_bound_swept_over_preemption_stretch)
{
    static const uint32_t stretches[] = { 1U, 2U, 7U, 8U, 13U, 40U };
    size_t i;

    for (i = 0; i < sizeof(stretches) / sizeof(stretches[0]); i++) {
        uint32_t s        = stretches[i];
        uint32_t pollCost = poll_cost_ms(s);
        uint32_t expPolls = expected_new_polls(s);
        MockEnv oldEnv, newEnv;

        mock_init(&oldEnv, WRITE_NEVER_ACCEPTS, s, 0U);
        mock_init(&newEnv, WRITE_NEVER_ACCEPTS, s, 0U);

        ASSERT_EQ(old_stall_bound_iterations(&oldEnv, gTestBuffer, CHUNK_SIZE), 0);
        ASSERT_EQ(new_stall_bound_ticks(&newEnv, gTestBuffer, CHUNK_SIZE), 0);

        /* OLD: same iteration count every time; real time scales with stretch. */
        ASSERT_EQ(oldEnv.delayCalls, FW_STALL_TIMEOUT_MS / FW_STALL_POLL_MS);
        ASSERT_EQ(oldEnv.now, FW_STALL_TIMEOUT_MS * s);

        /* NEW: real elapsed pinned to the deadline, within one poll of slack.
         * The deadline is tested once per poll, so the exit can overshoot by
         * up to (pollCost - 1) ms but never by a whole poll, and can never
         * land EARLY -- that lower bound is what stops a mutation from simply
         * bailing out faster and calling it a fix. */
        ASSERT_EQ(newEnv.delayCalls, expPolls);
        ASSERT_EQ(newEnv.now, expPolls * pollCost);
        ASSERT_TRUE(newEnv.now >= FW_STALL_TIMEOUT_MS);
        ASSERT_TRUE(newEnv.now < FW_STALL_TIMEOUT_MS + pollCost);

        /* And the two agree if and only if there was no stretch at all. */
        if (s == 1U) {
            ASSERT_EQ(oldEnv.now, newEnv.now);
        } else {
            ASSERT_TRUE(oldEnv.now > newEnv.now);
        }
    }
}

/* Happy path. A card with room accepts the whole chunk on the first call, so
 * neither shape sleeps at all and no clock is consulted. Guards the mutation
 * "make the new loop always take the break arm", which would pass every stall
 * assertion above while destroying the benchmark. */
TEST(no_stall_completes_immediately_without_sleeping)
{
    static const uint32_t stretches[] = { 1U, 8U };
    size_t i;

    for (i = 0; i < sizeof(stretches) / sizeof(stretches[0]); i++) {
        MockEnv oldEnv, newEnv;

        mock_init(&oldEnv, WRITE_ALWAYS_ACCEPTS, stretches[i], 0U);
        mock_init(&newEnv, WRITE_ALWAYS_ACCEPTS, stretches[i], 0U);

        ASSERT_EQ(old_stall_bound_iterations(&oldEnv, gTestBuffer, CHUNK_SIZE),
                  CHUNK_SIZE);
        ASSERT_EQ(new_stall_bound_ticks(&newEnv, gTestBuffer, CHUNK_SIZE),
                  CHUNK_SIZE);

        ASSERT_EQ(oldEnv.delayCalls, 0);
        ASSERT_EQ(newEnv.delayCalls, 0);
        ASSERT_EQ(oldEnv.writeCalls, 1);
        ASSERT_EQ(newEnv.writeCalls, 1);
        ASSERT_EQ(oldEnv.now, 0);
        ASSERT_EQ(newEnv.now, 0);
    }
}

/* The distinction that matters: the new bound is a STALL bound, not a
 * total-runtime bound. A card that keeps making progress -- here 128 B every
 * 9,000 ms, four times, so the loop lives 36,000 ms, well past the 10,000 ms
 * deadline -- must NOT trip, because every acceptance resets the clock.
 *
 * This is the assertion that separates "correctly bounds a genuine stall" from
 * "just makes everything time out sooner". Delete the `lastProgressTick =
 * mock_tick_count(env)` reset and the new shape trips 10,000 ms into the
 * SECOND gap with 128 of 512 bytes written.
 *
 * The old shape's reset is load-bearing here too, but only at stretch 1: there
 * a 9,000 ms gap costs 1,800 polls = 9,000 NOMINAL ms, so a second un-reset
 * gap would cross 10,000 and trip it.
 *
 * NOTE this mode is partial-accepting, which the real callee is not (it is
 * all-or-nothing, so at most one iteration can make progress and that
 * iteration ends the loop). It models the hypothetical the source comment
 * explicitly keeps the reset arm for. The reset is therefore currently
 * unreachable in the shipped firmware -- exercising it here is what keeps it
 * correct if that callee ever changes. */
TEST(progress_resets_the_deadline_in_both_shapes)
{
    static const uint32_t stretches[] = { 1U, 7U, 8U };
    const uint32_t gapMs       = 9000U;   /* < FW_STALL_TIMEOUT_MS */
    const size_t   acceptBytes = 128U;    /* 512 / 128 = 4 acceptances */
    size_t i;

    for (i = 0; i < sizeof(stretches) / sizeof(stretches[0]); i++) {
        uint32_t s           = stretches[i];
        uint32_t pollCost    = poll_cost_ms(s);
        uint32_t pollsPerGap = (gapMs + pollCost - 1U) / pollCost;
        uint32_t gaps        = (uint32_t)(CHUNK_SIZE / acceptBytes);
        MockEnv oldEnv, newEnv;

        /* Precondition on the fixture itself: the poll granularity must leave
         * the per-gap wait short of the deadline, or this case would be
         * asserting a legitimate trip rather than a spurious one. */
        ASSERT_TRUE(pollsPerGap * pollCost < FW_STALL_TIMEOUT_MS);

        mock_init(&oldEnv, WRITE_PERIODIC_PARTIAL, s, 0U);
        oldEnv.acceptBytes  = acceptBytes;
        oldEnv.gapMs        = gapMs;
        oldEnv.nextAcceptAt = gapMs;
        newEnv = oldEnv;

        ASSERT_EQ(old_stall_bound_iterations(&oldEnv, gTestBuffer, CHUNK_SIZE),
                  CHUNK_SIZE);
        ASSERT_EQ(new_stall_bound_ticks(&newEnv, gTestBuffer, CHUNK_SIZE),
                  CHUNK_SIZE);

        ASSERT_EQ(oldEnv.delayCalls, gaps * pollsPerGap);
        ASSERT_EQ(newEnv.delayCalls, gaps * pollsPerGap);
        ASSERT_EQ(oldEnv.now, gaps * pollsPerGap * pollCost);
        ASSERT_EQ(newEnv.now, gaps * pollsPerGap * pollCost);

        /* The loop outlived the stall deadline several times over and neither
         * shape gave up -- which is the whole point. */
        ASSERT_TRUE(newEnv.now > FW_STALL_TIMEOUT_MS);
    }
}

/* The new shape reads the clock, so unlike the old one it has a wrap to get
 * right. TickType_t is uint32_t here, the subtraction is unsigned, and the
 * code compares only the DIFFERENCE -- so a stall straddling the ~49-day tick
 * rollover must still trip at 10,000 ms, not instantly and not never.
 *
 * Starting 16 ticks below UINT32_MAX puts the rollover inside the first poll.
 * The old shape has nothing to test here: counting nominal milliseconds is
 * wrap-immune by construction, which is the one thing it had going for it. */
TEST(new_bound_survives_tick_counter_wrap)
{
    MockEnv env;
    uint32_t startTick = 0xFFFFFFF0U;

    mock_init(&env, WRITE_NEVER_ACCEPTS, 8U, startTick);

    ASSERT_EQ(new_stall_bound_ticks(&env, gTestBuffer, CHUNK_SIZE), 0);
    ASSERT_EQ(env.delayCalls, 250);
    /* The clock wrapped; the unsigned difference is still the true elapsed. */
    ASSERT_TRUE(env.now < startTick);
    ASSERT_EQ((uint32_t)(env.now - startTick), 10000);
}

int main(void)
{
    printf("#943 -- SD:BENCHmark per-chunk stall bound (extracted loop shapes)\n");
    printf("---------------------------------------------\n");
    RUN(stall_headline_8x_preemption_stretch);
    RUN(stall_bound_swept_over_preemption_stretch);
    RUN(no_stall_completes_immediately_without_sleeping);
    RUN(progress_resets_the_deadline_in_both_shapes);
    RUN(new_bound_survives_tick_counter_wrap);
    return TEST_SUMMARY();
}
