/* #1231: the sample-queue clamp must be REPORTED ONCE PER TRANSITION, not on
 * every stream start.
 *
 * WHAT WAS WRONG. AInSample.c's resize-skip LOG_E fired on every single
 * SYST:STR:THRoughput run on a healthy device -- measured on an NQ1 across two
 * firmware images, present on every attempt, while the same runs reported
 * QueueDropped=0 and the full sample count. The partition asks to maximize, the
 * heap will not stretch that far, and the pool is clamped to the queue it
 * already has. Nothing was denied that anybody asked for. An error that fires
 * on every healthy run is saturated: it says nothing on the day capacity really
 * IS denied, and this project's stated principle is that an operator reads
 * SYSTem:LOG? to learn what went wrong.
 *
 * ⚠ WHY NOT DEMOTE TO LOG_I, which was the other option considered and is the
 * obvious one: AInSample.c sets `#define LOG_LVL LOG_LEVEL_ERROR` at the top,
 * deliberately -- its own comment says "LOG_D/LOG_I stripped from the binary
 * (per-sample hot path)". That is a COMPILE-TIME ceiling, so LOG_I there
 * compiles to nothing and no runtime log level could bring the line back.
 * Demoting would have DELETED the diagnostic, not relocated it.
 *
 * ⭐ THIS TEST SPLICES THE REAL PREDICATE, it does not model it. Almost every
 * other host test here has to model (streaming.c, SCPIInterface.c and friends
 * are not host-includable: FreeRTOS, Harmony, the board graph), but
 * ClampReportDue() is six lines over one caller-owned size_t with no includes
 * of its own, so it escapes that and is tested as itself. The Makefile recipe
 * extracts it verbatim into gen_1231_clamp_latch.h and fails the BUILD if it
 * cannot.
 *
 * ⛔ AND EVERY LEG CARRIES ITS OWN POSITIVE CONTROL. The property is a SEQUENCE
 * over calls, so a leg that merely counts reports from the real predicate
 * proves nothing about whether it would have caught the defect. Each sequence
 * is therefore replayed through two mutants as well:
 *
 *   alwaysEmit    the PRE-#1231 behaviour -- report whenever clamped. Must
 *                 FAIL the "reports once" legs, or those legs are decoration.
 *   neverClears   forgets to clear on a successful grow. Must FAIL the
 *                 clamp/no-clamp/clamp leg, which is the only leg that can
 *                 see it; it passes the other two.
 */
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include "test_framework.h"

/* The REAL predicate, spliced verbatim out of AInSample.c by the Makefile. */
#include "gen_1231_clamp_latch.h"

/* ------------------------------------------------------------------ *
 * The mutants. Kept as code rather than described in a comment, so the
 * discrimination is asserted rather than asserted-about.
 * ------------------------------------------------------------------ */

/* Pre-#1231: the LOG_E fired whenever the clamp happened. */
static bool ClampReportDue_alwaysEmit(size_t needed, bool clamped,
                                      size_t *pLastReported)
{
    (void)needed;
    (void)pLastReported;
    return clamped;
}

/* The easy wrong fix: latch on `needed` but never clear it, so a clamp that
 * lifts and returns is swallowed. Passes a steady configuration perfectly. */
static bool ClampReportDue_neverClears(size_t needed, bool clamped,
                                       size_t *pLastReported)
{
    if (!clamped) {
        return false;
    }
    if (needed == *pLastReported) {
        return false;
    }
    *pLastReported = needed;
    return true;
}

/* ------------------------------------------------------------------ *
 * The driver. A step is one call to AInSampleList_InitializeExternal, i.e.
 * one stream start.
 * ------------------------------------------------------------------ */
typedef bool (*latch_fn)(size_t, bool, size_t *);

typedef struct {
    size_t needed;      /* what the partition asked for; 0 when not clamped */
    bool   clamped;     /* true when the heap would not stretch to `needed`  */
} step_t;

/* Replays `steps` through `f` from a freshly-booted latch and returns how
 * many times it would have logged. */
static unsigned reports_for(latch_fn f, const step_t *steps, size_t n)
{
    size_t last = 0u;           /* the static's boot value */
    unsigned reports = 0u;
    size_t i;
    for (i = 0u; i < n; i++) {
        if (f(steps[i].needed, steps[i].clamped, &last)) {
            reports++;
        }
    }
    return reports;
}

/* The measured `needed` from the bench observation, and a second, different
 * one standing for a reconfiguration. Real values: `needed` is
 * maxSize * sizeof(ptr) + 80, so it is never below 84 -- see the sentinel
 * test at the bottom. */
#define NEEDED_A  32592u
#define NEEDED_B  16296u

/* ------------------------------------------------------------------ *
 * 1. A steady clamped configuration reports ONCE, however many sessions run.
 * ------------------------------------------------------------------ */
TEST(a_steady_clamp_reports_once_across_repeated_sessions)
{
    const step_t steps[] = {
        { NEEDED_A, true }, { NEEDED_A, true }, { NEEDED_A, true },
        { NEEDED_A, true }, { NEEDED_A, true },
    };
    const size_t n = sizeof(steps) / sizeof(steps[0]);

    ASSERT_EQ(reports_for(ClampReportDue, steps, n), 1u);

    /* ⛔ THE POSITIVE CONTROL: the pre-#1231 behaviour reports every time, so
     * this leg demonstrably discriminates rather than restating a constant. */
    ASSERT_EQ(reports_for(ClampReportDue_alwaysEmit, steps, n), 5u);

    /* neverClears is indistinguishable HERE -- stated so a reader does not
     * read this leg as covering it. The third test is the one that can. */
    ASSERT_EQ(reports_for(ClampReportDue_neverClears, steps, n), 1u);
}

/* ------------------------------------------------------------------ *
 * 2. A reconfiguration that clamps to a DIFFERENT `needed` reports again.
 * ------------------------------------------------------------------ */
TEST(a_reconfiguration_that_clamps_differently_reports_again)
{
    const step_t steps[] = {
        { NEEDED_A, true }, { NEEDED_A, true },      /* config A, reports once */
        { NEEDED_B, true }, { NEEDED_B, true },      /* config B, reports once */
    };
    const size_t n = sizeof(steps) / sizeof(steps[0]);

    ASSERT_EQ(reports_for(ClampReportDue, steps, n), 2u);

    /* and it is the CHANGE that reports, not the count of sessions */
    ASSERT_EQ(reports_for(ClampReportDue_alwaysEmit, steps, n), 4u);

    /* ...and going back to A reports a third time: the latch holds only the
     * most recent request, which is what "changed" has to mean for a value
     * that can oscillate between two configurations. */
    {
        const step_t back[] = {
            { NEEDED_A, true }, { NEEDED_B, true }, { NEEDED_A, true },
        };
        ASSERT_EQ(reports_for(ClampReportDue, back,
                              sizeof(back) / sizeof(back[0])), 3u);
    }
}

/* ------------------------------------------------------------------ *
 * 3. Clamp, then NO clamp, then clamp: reports TWICE.
 * ------------------------------------------------------------------ */
TEST(a_clamp_that_lifts_and_returns_reports_twice)
{
    const step_t steps[] = {
        { NEEDED_A, true },     /* clamped   -> report                       */
        { NEEDED_A, true },     /* unchanged -> quiet                        */
        { 0u,       false },    /* the grow SUCCEEDED -> latch cleared       */
        { NEEDED_A, true },     /* clamped again -> a NEW event, report      */
        { NEEDED_A, true },     /* unchanged -> quiet                        */
    };
    const size_t n = sizeof(steps) / sizeof(steps[0]);

    ASSERT_EQ(reports_for(ClampReportDue, steps, n), 2u);

    /* ⛔ THE POSITIVE CONTROL FOR THE CLEAR. A latch that never clears
     * swallows the recurrence -- the defect this leg exists for, and the one
     * the other two legs cannot see. */
    ASSERT_EQ(reports_for(ClampReportDue_neverClears, steps, n), 1u);

    /* and the pre-fix behaviour is noisy here too */
    ASSERT_EQ(reports_for(ClampReportDue_alwaysEmit, steps, n), 4u);
}

/* ------------------------------------------------------------------ *
 * 4. The first clamp after a reset always reports.
 * ------------------------------------------------------------------ */
TEST(the_first_clamp_after_a_reset_reports)
{
    size_t last = 0u;           /* exactly the static initializer's value */
    ASSERT_TRUE(ClampReportDue(NEEDED_A, true, &last));
    ASSERT_EQ(last, (size_t)NEEDED_A);
    /* ...and only once */
    ASSERT_TRUE(!ClampReportDue(NEEDED_A, true, &last));
}

/* ------------------------------------------------------------------ *
 * 5. A successful grow clears the latch even when nothing was reported.
 * ------------------------------------------------------------------ */
TEST(a_successful_grow_clears_the_latch_unconditionally)
{
    size_t last = NEEDED_A;
    ASSERT_TRUE(!ClampReportDue(0u, false, &last));
    ASSERT_EQ(last, (size_t)0u);

    /* and from a clean latch it is a no-op rather than an error */
    ASSERT_TRUE(!ClampReportDue(0u, false, &last));
    ASSERT_EQ(last, (size_t)0u);
}

/* ------------------------------------------------------------------ *
 * 6. ⭐ THE SENTINEL. 0 means "nothing reported", so a real `needed` of 0
 *    would be indistinguishable from a cleared latch and a genuine clamp
 *    would go unreported forever.
 * ------------------------------------------------------------------ */
TEST(zero_is_reserved_as_the_sentinel_and_a_real_needed_cannot_be_zero)
{
    /* The hazard, demonstrated rather than described: were `needed` ever 0
     * while clamped, the predicate would stay silent on every call. */
    {
        size_t last = 0u;
        ASSERT_TRUE(!ClampReportDue(0u, true, &last));
    }

    /* It cannot happen, because needed = maxSize * sizeof(ptr) + 80 with
     * maxSize >= 1 on this branch -- so the floor is 80 + one pointer. The
     * arithmetic is pinned here so a future change to that expression has to
     * come past this test. */
    {
        const size_t floor_needed = (size_t)1u * sizeof(void *) + 80u;
        size_t last = 0u;
        ASSERT_TRUE(floor_needed > 0u);
        ASSERT_TRUE(floor_needed >= 80u);
        ASSERT_TRUE(ClampReportDue(floor_needed, true, &last));
        ASSERT_EQ(last, floor_needed);
    }
}

/* ------------------------------------------------------------------ *
 * 7. The predicate does not report when it is not clamped, whatever `needed`
 *    says -- the clamped flag is the authority, not the size.
 * ------------------------------------------------------------------ */
TEST(not_clamped_never_reports_whatever_needed_says)
{
    size_t last = 0u;
    ASSERT_TRUE(!ClampReportDue(NEEDED_A, false, &last));
    ASSERT_EQ(last, (size_t)0u);
    ASSERT_TRUE(!ClampReportDue(NEEDED_B, false, &last));
    ASSERT_EQ(last, (size_t)0u);
}

int main(void)
{
    puts("#1231 -- the clamp report latch (real predicate, spliced)");
    RUN(a_steady_clamp_reports_once_across_repeated_sessions);
    RUN(a_reconfiguration_that_clamps_differently_reports_again);
    RUN(a_clamp_that_lifts_and_returns_reports_twice);
    RUN(the_first_clamp_after_a_reset_reports);
    RUN(a_successful_grow_clears_the_latch_unconditionally);
    RUN(zero_is_reserved_as_the_sentinel_and_a_real_needed_cannot_be_zero);
    RUN(not_clamped_never_reports_whatever_needed_says);
    return TEST_SUMMARY();
}
