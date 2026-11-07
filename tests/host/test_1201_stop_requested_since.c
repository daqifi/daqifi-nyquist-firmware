/* ==========================================================================
 * #1201 -- StopRequestedSince(): the one stop-request predicate.
 *
 * ⭐ THIS COMPILES THE REAL FUNCTION, NOT A MODEL. The Makefile recipe splices
 * StopRequestedSince() verbatim out of SCPIInterface.c into
 * gen_1201_stop_predicate.h and this file includes it. Almost every other
 * SCPIInterface.c test in this directory is a MODEL, because that file is
 * 10,000+ lines and pulls in FreeRTOS, Harmony, the WINC driver, nanopb and
 * libscpi, so it is not host-includable. This predicate is the rare piece that
 * escapes that: six lines, two file-scope globals, no includes of its own. So
 * it is tested as itself, and a model would have been a choice to test
 * something weaker when the real thing was available.
 *
 * WHAT IS WORTH PINNING HERE. The function's whole content is a three-term
 * disjunction, and its comment claims each term covers a case THE OTHER TWO
 * CANNOT. That claim is the reason the function is shaped the way it is, and
 * until now it was only ever asserted in prose -- in three separate places,
 * because the test was copy-pasted. The isolation tests below are that claim
 * made checkable: for each term, a state where ONLY that term fires and the
 * predicate must still answer "a stop was requested", plus the paired state
 * with that one term withdrawn where it must answer "no". A term that was
 * deleted, or ORed in the wrong direction, fails its own pair.
 *
 * ⚠ WHAT THIS CANNOT CHECK, so it is not read as more than it is: the
 * predicate's correctness depends on its CALLERS holding a critical section
 * and on WHERE in each caller it sits -- neither of which is visible from
 * here, because a host test cannot reach a preemption between two adjacent
 * instructions. Those are pinned at source level by the Makefile recipe (no
 * critical section inside the predicate; each of the four call sites calls it
 * and carries no private copy of the three terms) and exercised on hardware by
 * the companion test in daqifi-python-test-suite. This file checks the
 * predicate's LOGIC. Three instruments, three different properties.
 * ========================================================================== */
#include <stdbool.h>
#include <stdint.h>
#include "test_framework.h"

/* The two globals the real predicate reads. `volatile` is kept because the
 * firmware's definitions are volatile and dropping it here would compile the
 * spliced text under a weaker type than it ships with. Not `static volatile`
 * on separate declarations the way the firmware has them -- the firmware needs
 * tentative forward declarations because the predicate is defined textually
 * ahead of the initialized definitions; here there is nothing to forward-
 * declare past. */
static volatile uint32_t gStreamStopGen;
static volatile uint32_t gStreamStopsActive;

/* THE REAL StopRequestedSince(), spliced verbatim. See the Makefile recipe --
 * it fails loudly if the function's signature moves, rather than letting this
 * test compile against nothing. */
#include "gen_1201_stop_predicate.h"

/* ---------------------------------------------------------------- helpers */

typedef struct {
    bool requested;
    bool inFlight;
} Answer;

/* Set the live globals, then ask. `inFlight` is POISONED to the opposite of
 * whatever a lazy implementation would leave behind, so a path that forgets to
 * write the out-param cannot pass by accident. */
static Answer ask(uint32_t pinnedGen, bool pinnedActive,
                  uint32_t liveGen, uint32_t liveActive, bool poison)
{
    Answer a;
    gStreamStopGen     = liveGen;
    gStreamStopsActive = liveActive;
    a.inFlight  = poison;
    a.requested = StopRequestedSince(pinnedGen, pinnedActive, &a.inFlight);
    return a;
}

/* ------------------------------------------------------------------ tests */

TEST(a_quiet_window_is_not_a_stop)
{
    /* Nothing pinned active, nothing running, generation unmoved. This is the
     * overwhelmingly common case: it must not refuse an arm. */
    Answer a = ask(7u, false, 7u, 0u, true);
    ASSERT_FALSE(a.requested);
    ASSERT_FALSE(a.inFlight);

    /* And it is quiet at every generation value, not just a small one --
     * including zero, which is the post-boot state. */
    ASSERT_FALSE(ask(0u, false, 0u, 0u, true).requested);
    ASSERT_FALSE(ask(0xFFFFFFFFu, false, 0xFFFFFFFFu, 0u, true).requested);
}

TEST(term1_pinned_active_alone_is_a_stop)
{
    /* THE CASE THE OTHER TWO TERMS PROVABLY CANNOT SEE, and the reason this
     * term exists at all: a stop was ALREADY RUNNING when the command pinned,
     * and finished before this check. The live count is back to zero, and the
     * generation was bumped BEFORE the pin -- so it compares EQUAL. Both of
     * the other terms are silent here.
     *
     * Its teardown ran straight through this command's setup, so arming on top
     * of it is arming over a torn-down session. */
    Answer fires = ask(7u, true, 7u, 0u, false);
    ASSERT_TRUE(fires.requested);
    ASSERT_TRUE(fires.inFlight);

    /* The paired state with ONLY this term withdrawn. If it answers the same,
     * the term above was not what produced the answer and this test proves
     * nothing -- which is the whole point of asserting the pair. */
    Answer withdrawn = ask(7u, false, 7u, 0u, true);
    ASSERT_FALSE(withdrawn.requested);
    ASSERT_FALSE(withdrawn.inFlight);
}

TEST(term2_live_count_alone_is_a_stop)
{
    /* A stop is running RIGHT NOW, so it will clear IsEnabled after an arm
     * publishes it. Nothing was pinned active and the generation has not moved
     * yet (the bump and the count rise together, but a pin taken after the
     * bump sees no delta). */
    Answer fires = ask(7u, false, 7u, 1u, false);
    ASSERT_TRUE(fires.requested);
    ASSERT_TRUE(fires.inFlight);

    Answer withdrawn = ask(7u, false, 7u, 0u, true);
    ASSERT_FALSE(withdrawn.requested);
    ASSERT_FALSE(withdrawn.inFlight);
}

TEST(term3_generation_alone_is_a_stop_but_not_in_flight)
{
    /* A stop began and ended entirely inside the window -- the original #861
     * case. Nothing is running now and nothing was running at the pin.
     *
     * ⭐ THE ONE STATE WHERE THE TWO OUTPUTS DIVERGE, which is exactly what
     * both arm sites' log messages key on to say "a stop is still in flight"
     * versus "a stop was issued and completed". A predicate that collapsed
     * inFlight into requested would still refuse the right arms and would
     * mislabel every one of those messages. */
    Answer fires = ask(7u, false, 8u, 0u, true);
    ASSERT_TRUE(fires.requested);
    ASSERT_FALSE(fires.inFlight);

    Answer withdrawn = ask(7u, false, 7u, 0u, true);
    ASSERT_FALSE(withdrawn.requested);
    ASSERT_FALSE(withdrawn.inFlight);
}

TEST(the_whole_truth_table)
{
    /* All eight combinations, against the two properties stated independently
     * of the implementation's expression:
     *     requested == any term fired
     *     inFlight  == a stop is, or was at the pin, RUNNING
     * Written as a loop over the cases rather than eight hand-written
     * expectations, so the expectation cannot drift per-row. */
    for (int pinnedActive = 0; pinnedActive <= 1; pinnedActive++) {
        for (int liveActive = 0; liveActive <= 1; liveActive++) {
            for (int genMoved = 0; genMoved <= 1; genMoved++) {
                const uint32_t pinnedGen = 100u;
                const uint32_t liveGen   = genMoved ? 101u : 100u;
                Answer a = ask(pinnedGen, (bool)pinnedActive,
                               liveGen, (uint32_t)liveActive,
                               /* poison = */ !(pinnedActive || liveActive));

                ASSERT_EQ(a.inFlight, (pinnedActive || liveActive) ? 1 : 0);
                ASSERT_EQ(a.requested,
                          (pinnedActive || liveActive || genMoved) ? 1 : 0);
            }
        }
    }
}

TEST(the_in_flight_out_param_is_written_on_every_path)
{
    /* Including the all-quiet path, where the answer is "no". A caller that
     * received a stale or uninitialised value here would label its log line
     * from garbage -- and the firmware's callers declare the variable once and
     * reuse it across a sweep's many steps, so "stale" is the realistic shape,
     * not "uninitialised". Poisoned TRUE and required to come back FALSE. */
    Answer quiet = ask(7u, false, 7u, 0u, /* poison = */ true);
    ASSERT_FALSE(quiet.inFlight);

    /* And the converse, so this is not just a one-directional check: poisoned
     * FALSE on a path whose answer is TRUE. */
    Answer busy = ask(7u, false, 7u, 3u, /* poison = */ false);
    ASSERT_TRUE(busy.inFlight);
}

TEST(the_live_count_is_a_count_not_a_flag)
{
    /* Both transports can be stopping at once, so this is a counter and ANY
     * non-zero value means a stop is in flight. A `== 1u` test would pass the
     * single-stop case and silently ignore concurrent stops. */
    const uint32_t counts[] = { 1u, 2u, 3u, 255u, 65536u, 0xFFFFFFFFu };
    for (unsigned i = 0; i < sizeof counts / sizeof counts[0]; i++) {
        Answer a = ask(7u, false, 7u, counts[i], false);
        ASSERT_TRUE(a.requested);
        ASSERT_TRUE(a.inFlight);
    }

    /* Zero, and only zero, is quiet. */
    Answer zero = ask(7u, false, 7u, 0u, true);
    ASSERT_FALSE(zero.requested);
}

TEST(a_wrapped_generation_still_reads_as_moved)
{
    /* The generation is compared with !=, not <, so a wrap is detected as
     * movement. That is the SAFE direction: a wrapped generation refuses an
     * arm rather than admitting one. (It would take 2^32 stops inside one
     * start to reach, which is why the firmware's comment calls the wrap
     * harmless -- "harmless" there means "cannot be reached", and this pins
     * that the behaviour if it ever were reached is still to refuse.) */
    Answer wrapped = ask(0xFFFFFFFFu, false, 0u, 0u, true);
    ASSERT_TRUE(wrapped.requested);
    ASSERT_FALSE(wrapped.inFlight);

    /* A single increment at the bottom of the range, and a large jump: the
     * predicate must not care about the DISTANCE, only about inequality. */
    ASSERT_TRUE(ask(0u, false, 1u, 0u, true).requested);
    ASSERT_TRUE(ask(1u, false, 0xFFFFFFFFu, 0u, true).requested);
    ASSERT_TRUE(ask(0xFFFFFFFFu, false, 0xFFFFFFFEu, 0u, true).requested);
}

TEST(the_predicate_does_not_mutate_the_globals)
{
    /* It is called from inside a critical section at three of its four sites,
     * and from a fourth that is judging data it already holds. A read-only
     * predicate is part of why that is safe; a write here would be a side
     * effect inside a masked-interrupt region that nothing documents. */
    gStreamStopGen     = 4242u;
    gStreamStopsActive = 7u;
    bool inFlight = false;
    (void) StopRequestedSince(1u, true, &inFlight);
    ASSERT_EQ(gStreamStopGen, 4242u);
    ASSERT_EQ(gStreamStopsActive, 7u);

    /* Repeated calls are stable -- the same question asked twice of the same
     * state gives the same answer. */
    bool f1 = false, f2 = false;
    bool r1 = StopRequestedSince(1u, false, &f1);
    bool r2 = StopRequestedSince(1u, false, &f2);
    ASSERT_EQ(r1, r2);
    ASSERT_EQ(f1, f2);
}

int main(void)
{
    printf("#1201 -- StopRequestedSince(), the REAL function spliced verbatim\n");
    printf("---------------------------------------------\n");
    RUN(a_quiet_window_is_not_a_stop);
    RUN(term1_pinned_active_alone_is_a_stop);
    RUN(term2_live_count_alone_is_a_stop);
    RUN(term3_generation_alone_is_a_stop_but_not_in_flight);
    RUN(the_whole_truth_table);
    RUN(the_in_flight_out_param_is_written_on_every_path);
    RUN(the_live_count_is_a_count_not_a_flag);
    RUN(a_wrapped_generation_still_reads_as_moved);
    RUN(the_predicate_does_not_mutate_the_globals);
    return TEST_SUMMARY();
}
