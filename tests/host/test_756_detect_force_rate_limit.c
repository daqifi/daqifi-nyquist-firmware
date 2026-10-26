/* ==========================================================================
 * test_756_detect_force_rate_limit.c -- #756 round-6 audit MEDIUM (both legs)
 *
 * WHAT IS UNDER TEST
 *
 * DRV_SDSPI_DetectPollExpireNow's rate limiter, in
 * config/default/driver/sdspi/src/drv_sdspi.c. The function samples the time
 * OUTSIDE its critical section and then compares it to a shared published
 * timestamp INSIDE:
 *
 *     const uint64_t nowCount = SYS_TIME_Counter64Get();          // outside
 *     const uint64_t minCount = SYS_TIME_MSToCount(pollingIntervalMs);
 *     OSAL_CRIT_Enter(...);
 *     if (!detectForceEver || (nowCount - detectForceAt) >= minCount) {
 *         detectForceAt = nowCount; detectForceEver = true; detectForceReq++;
 *     }
 *     OSAL_CRIT_Leave(...);
 *
 * The sample is taken outside deliberately: SYS_TIME_Counter64Get masks the
 * timer interrupt and SYS_TIME_MSToCount is a 64-bit multiply-divide, and
 * neither belongs in a critical section. That trade is fine. What it costs is
 * that the sample can go STALE before the lock is taken.
 *
 * ⛔ THE DEFECT. Two SCPI tasks can reach this function. Interleave them:
 *
 *   A: samples nowCount = T1
 *   A: preempted before OSAL_CRIT_Enter
 *   B: samples nowCount = T2 (> T1), takes the lock, passes the gate,
 *      publishes detectForceAt = T2, detectForceReq++
 *   A: resumes, takes the lock, computes T1 - T2
 *
 * T1 - T2 is an UNSIGNED UNDERFLOW -- a value near 2^64 -- so it clears
 * minCount by any margin, A passes a gate that should have refused it, and
 * two exclusive SPI4 card detections run back-to-back with no
 * pollingIntervalMs between them. A then assigns detectForceAt = T1, moving
 * the published timestamp BACKWARDS, so the NEXT caller is mis-gated too.
 *
 * ⚠ AND THE SOURCE COMMENT'S DEFENCE DOES NOT COVER THIS. It argues that if
 * the counter ever ran backwards the unsigned difference would be enormous and
 * the force would be ALLOWED, "which is the safe direction for a gate whose
 * failure mode is refusing a present card". That is an argument about the
 * CLOCK, and it remains true. This is cross-task sample REORDERING: the clock
 * is perfectly monotonic and the stale operand is the problem. A correct
 * statement about one hazard read as coverage of a different one.
 *
 * THE FIX, and why refusing loses nothing: under the lock, if
 * nowCount < detectForceAt, refuse and leave detectForceAt alone.
 * detectForceAt being newer than our own sample MEANS another caller has
 * already published an unconsumed force, so the poll this call wanted is
 * already in flight. Refusing also guarantees the timestamp is monotonic.
 *
 * HOW IT IS TESTED
 *
 * drv_sdspi.c is not host-compilable (Harmony, OSAL, SYS_TIME, the whole
 * driver graph), so this file RE-IMPLEMENTS the gate's two shapes -- old and
 * new -- and runs both against the same scripted interleaving, the same
 * technique test_943_bench_stall_bound.c and test_970_encoder_consume_
 * accounting.c use. The Makefile greps the real source and fails the BUILD if
 * the shape this model mirrors has drifted.
 *
 * Run: make -C tests/host run
 * ========================================================================== */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "test_framework.h"

/* The three fields of DRV_SDSPI_OBJ this gate owns. */
typedef struct {
    uint64_t detectForceAt;
    uint32_t detectForceReq;
    bool     detectForceEver;
} ForceGate;

/* PRE-FIX shape, mirroring drv_sdspi.c before the r6 fix. */
static void gate_old(ForceGate *g, uint64_t nowCount, uint64_t minCount)
{
    /* ... OSAL_CRIT_Enter ... */
    if ((g->detectForceEver == false) ||
        ((nowCount - g->detectForceAt) >= minCount)) {
        g->detectForceAt   = nowCount;
        g->detectForceEver = true;
        g->detectForceReq++;
    }
    /* ... OSAL_CRIT_Leave ... */
}

/* POST-FIX shape: a stale sample is refused, and detectForceAt never moves
 * backwards. */
static void gate_new(ForceGate *g, uint64_t nowCount, uint64_t minCount)
{
    /* ... OSAL_CRIT_Enter ... */
    if (g->detectForceEver && (nowCount < g->detectForceAt)) {
        /* stale sample -- a newer force is already pending; refuse. */
    } else if ((g->detectForceEver == false) ||
               ((nowCount - g->detectForceAt) >= minCount)) {
        g->detectForceAt   = nowCount;
        g->detectForceEver = true;
        g->detectForceReq++;
    }
    /* ... OSAL_CRIT_Leave ... */
}

/* 1 s of poll interval in counter ticks, and two samples 1 ms apart -- i.e.
 * both well inside one interval, so exactly ONE force is correct. */
#define MIN_COUNT   1000000u
#define T1          5000000u
#define T2          5001000u

TEST(two_callers_in_one_interval_force_exactly_once)
{
    /* THE CONTROL: no preemption, A then B in order. Both shapes must allow
     * A's force and refuse B's -- if they disagree here the interleaving
     * result below would be meaningless. */
    ForceGate o = {0}, n = {0};

    gate_old(&o, T1, MIN_COUNT);
    gate_old(&o, T2, MIN_COUNT);
    gate_new(&n, T1, MIN_COUNT);
    gate_new(&n, T2, MIN_COUNT);

    ASSERT_EQ(o.detectForceReq, 1u);
    ASSERT_EQ(n.detectForceReq, 1u);
    ASSERT_EQ((unsigned long long)o.detectForceAt, (unsigned long long)T1);
    ASSERT_EQ((unsigned long long)n.detectForceAt, (unsigned long long)T1);
}

TEST(a_stale_sample_must_not_double_force_or_rewind_the_timestamp)
{
    /* ⛔ THE AUDIT'S INTERLEAVING. A samples T1, is preempted, B samples T2
     * and publishes, then A resumes and reaches the lock with its stale T1. */
    ForceGate o = {0}, n = {0};

    /* B publishes first, because A was preempted before the lock. */
    gate_old(&o, T2, MIN_COUNT);
    gate_new(&n, T2, MIN_COUNT);
    ASSERT_EQ(o.detectForceReq, 1u);
    ASSERT_EQ(n.detectForceReq, 1u);

    /* A now arrives with the OLDER sample. */
    gate_old(&o, T1, MIN_COUNT);
    gate_new(&n, T1, MIN_COUNT);

    /* ⛔ PRE-FIX: T1 - T2 underflows, the gate passes, and we get a second
     * force inside the same interval plus a rewound timestamp. These two
     * assertions DOCUMENT the defect -- they are what the fix changes. */
    ASSERT_EQ(o.detectForceReq, 2u);
    ASSERT_EQ((unsigned long long)o.detectForceAt, (unsigned long long)T1);

    /* ✅ POST-FIX: refused, and the timestamp is untouched. */
    ASSERT_EQ(n.detectForceReq, 1u);
    ASSERT_EQ((unsigned long long)n.detectForceAt, (unsigned long long)T2);

    /* and the timestamp never went backwards, which is the property that
     * keeps the NEXT caller correctly gated */
    ASSERT_TRUE(n.detectForceAt >= (uint64_t)T2);
}

TEST(the_underflow_is_what_makes_the_pre_fix_gate_pass)
{
    /* Name the mechanism rather than leaving it implied: the pre-fix gate does
     * not pass because the interval elapsed -- it passes because the unsigned
     * subtraction wrapped. */
    const uint64_t diff = (uint64_t)T1 - (uint64_t)T2;

    ASSERT_TRUE(T1 < T2);                 /* a genuinely older sample ... */
    ASSERT_TRUE(diff > (uint64_t)MIN_COUNT); /* ... yet "newer" by the compare */
    ASSERT_TRUE(diff > 0xFFFFFFFFFFFF0000ull); /* because it wrapped near 2^64 */
}

TEST(a_genuinely_elapsed_interval_still_forces)
{
    /* The fix must not make the limiter refuse a legitimate second force: a
     * sample a full interval later than the published one is allowed, and it
     * advances the timestamp. This is the arm that would catch a fix that
     * simply refused everything after the first force. */
    ForceGate n = {0};
    const uint64_t later = (uint64_t)T2 + (uint64_t)MIN_COUNT;

    gate_new(&n, T2, MIN_COUNT);
    ASSERT_EQ(n.detectForceReq, 1u);

    gate_new(&n, later, MIN_COUNT);
    ASSERT_EQ(n.detectForceReq, 2u);
    ASSERT_EQ((unsigned long long)n.detectForceAt, (unsigned long long)later);
}

TEST(the_first_force_of_a_session_is_never_suppressed)
{
    /* detectForceEver exists because at boot the counter is near zero, so
     * "now - 0" is small and an unqualified compare would suppress the first
     * force. The new stale-sample arm is guarded on detectForceEver precisely
     * so it cannot reintroduce that: with no force yet published there is no
     * timestamp to be stale against. */
    ForceGate n = {0};

    gate_new(&n, 1u, MIN_COUNT);          /* tiny counter, first ever call */
    ASSERT_EQ(n.detectForceReq, 1u);
    ASSERT_TRUE(n.detectForceEver);
    ASSERT_EQ((unsigned long long)n.detectForceAt, 1ull);
}

int main(void)
{
    printf("=== #756 r6: detect-force rate limiter, stale-sample race ===\n");
    RUN(two_callers_in_one_interval_force_exactly_once);
    RUN(a_stale_sample_must_not_double_force_or_rewind_the_timestamp);
    RUN(the_underflow_is_what_makes_the_pre_fix_gate_pass);
    RUN(a_genuinely_elapsed_interval_still_forces);
    RUN(the_first_force_of_a_session_is_never_suppressed);
    return TEST_SUMMARY();
}
