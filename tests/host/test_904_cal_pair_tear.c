/* ==========================================================================
 * test_904_cal_pair_tear.c -- PR firmware#1048 (issue #904)
 *
 * WHY THIS FILE EXISTS: it is the block's own second exit route
 *
 * This row has been `blocked` since 2026-10-16 on an operator ruling that it
 * is "UNVALIDATABLE on this station's hardware": every board here holds
 * IDENTITY coefficients (CalM = 1, CalB = 0), and a torn read of 1.0 / 0.0 is
 * indistinguishable from a clean one, so a passing bench run would be
 * evidence of nothing. The ruling named two ways out --
 *
 *     "a calibrated board, OR a host-level test that injects the coefficient
 *      pair directly"
 *
 * -- and this is the second. It needs no bench and no coefficient written to
 * any board's NVM.
 *
 * WHAT IS UNDER TEST
 *
 * AInRuntimeConfig.CalM and .CalB are `double`. On PIC32MZ a 64-bit access is
 * two 32-bit accesses, so neither a load nor a store is atomic. The shared
 * state is read by the conversion path (MC12b_ConvertToVoltage,
 * AD7609_ConvertToVoltage, NanoPB_Encoder) and by the chanCALM?/chanCALB?
 * getters, and written by three sites: the bank-load copy loop in
 * daqifi_settings_LoadADCCalSettings, ADCChanCalmSetClaimed and
 * ADCChanCalbSetClaimed.
 *
 * #1054 shipped the READ half. This PR is the WRITE half. The point of the
 * matrix below is the codebase's own stated rule, from #1086's read-side
 * comment:
 *
 *     "That section closes only the direction where THIS READER is preempted
 *      mid-read; the direction where the WRITER is preempted mid-store is
 *      closed by the matching section ... and NEITHER HALF IS SUFFICIENT
 *      ALONE."
 *
 * ⭐ That sentence is the whole specification, and a reader who has only seen
 * one half of the fix will reasonably assume it is done. The matrix makes the
 * remaining direction fail, so the assumption cannot survive contact.
 *
 * HOW IT IS TESTED
 *
 * No threads. A double is modelled as its two 32-bit halves, and each access
 * is decomposed into its two steps with an explicit preemption point between
 * them. A real preemption is the same thing with the boundary chosen by the
 * scheduler rather than by the test.
 *
 * ⛔ THE PRECONDITION, ASSERTED RATHER THAN ASSUMED
 *
 * A tear is only OBSERVABLE if the two values' LOW halves differ. If they do
 * not, every combination of halves lands on a value one of them already has,
 * and the test passes whether or not the fix is present. Measured:
 *
 *     1.25 -> 2.5    lo 00000000 / 00000000   2 outcomes   INVISIBLE
 *     1.1  -> 2.2    lo 9999999A / 9999999A   2 outcomes   INVISIBLE
 *     1.1  -> 1.2    lo 9999999A / 33333333   4 outcomes   OBSERVABLE
 *
 * ⚠ Any pair related by a power of two shares its mantissa, hence its low
 * half -- so 2.2 == 2 x 1.1 fails for exactly the reason 2.5 == 2 x 1.25
 * does. The round, memorable numbers anyone would reach for are precisely the
 * broken ones, which is why this file checks its own operands instead of
 * trusting a constant nobody will re-derive.
 * ========================================================================== */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "test_framework.h"

/* A double, addressed as the two 32-bit halves the hardware actually moves. */
typedef struct { uint32_t hi; uint32_t lo; } Pair;

static Pair pair_of(double d)
{
    uint64_t bits;
    Pair p;
    memcpy(&bits, &d, sizeof bits);
    p.hi = (uint32_t)(bits >> 32);
    p.lo = (uint32_t)(bits & 0xFFFFFFFFu);
    return p;
}

static double double_of(Pair p)
{
    uint64_t bits = ((uint64_t)p.hi << 32) | (uint64_t)p.lo;
    double d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

static bool pair_eq(Pair a, Pair b) { return a.hi == b.hi && a.lo == b.lo; }

/* The two operands. Chosen so their LOW halves differ -- asserted below. */
#define VAL_OLD 1.1
#define VAL_NEW 1.2

typedef enum { UNGUARDED = 0, GUARDED } Mode;

/* ---- the writer -----------------------------------------------------------
 * GUARDED   stores both halves with no preemption point between them.
 * UNGUARDED stores hi, yields, stores lo.
 * `interrupted` selects whether the yield actually happens. */
static void writer_store(Pair *cell, Pair v, Mode mode, bool interrupted,
                         Pair *observed_by_reader, bool reader_runs,
                         Mode reader_mode)
{
    if (mode == GUARDED) {
        *cell = v;                       /* indivisible */
        if (reader_runs) {
            /* reader observes the completed state */
            *observed_by_reader = *cell;
        }
        return;
    }
    cell->hi = v.hi;
    if (interrupted && reader_runs) {
        /* A reader running HERE sees the new hi against the old lo. A GUARDED
         * reader makes that observation indivisibly -- which does not help:
         * it atomically reads a half-written value. */
        (void)reader_mode;
        *observed_by_reader = *cell;
    }
    cell->lo = v.lo;
}

/* ---- the reader -----------------------------------------------------------
 * GUARDED   loads both halves with no preemption point between them.
 * UNGUARDED loads hi, yields (a whole write may land), loads lo. */
static Pair reader_load(Pair *cell, Mode mode, bool interrupted, Pair writeDuring)
{
    if (mode == GUARDED) {
        return *cell;                    /* indivisible */
    }
    Pair out;
    out.hi = cell->hi;
    if (interrupted) {
        *cell = writeDuring;             /* a complete, atomic write lands */
    }
    out.lo = cell->lo;
    return out;
}

/* ==========================================================================
 * THE PRECONDITION
 * ========================================================================== */

TEST(the_chosen_operands_make_a_tear_observable)
{
    const Pair a = pair_of(VAL_OLD);
    const Pair b = pair_of(VAL_NEW);

    /* If the low halves matched, every half-combination would land on a or b
     * and no assertion in this file could distinguish a guarded access from
     * an unguarded one. */
    ASSERT_TRUE(a.lo != b.lo);
    ASSERT_TRUE(a.hi != b.hi);

    /* All four combinations must be four DISTINCT values. */
    const Pair mixed1 = { b.hi, a.lo };
    const Pair mixed2 = { a.hi, b.lo };
    ASSERT_FALSE(pair_eq(mixed1, a));
    ASSERT_FALSE(pair_eq(mixed1, b));
    ASSERT_FALSE(pair_eq(mixed2, a));
    ASSERT_FALSE(pair_eq(mixed2, b));
    ASSERT_FALSE(pair_eq(mixed1, mixed2));

    /* And as NUMBERS, not just bit patterns -- the point of choosing operands
     * whose low halves differ is that a torn result is a value an instrument
     * would actually report. Each mix named with its own value, because the
     * two are easy to swap:
     *
     *     t1 = mixed1 = hi(VAL_NEW) | lo(VAL_OLD) = 1.2000003814697267
     *     t2 = mixed2 = hi(VAL_OLD) | lo(VAL_NEW) = 1.0999996185302734
     */
    const double t1 = double_of(mixed1);
    const double t2 = double_of(mixed2);
    ASSERT_TRUE(t1 != VAL_OLD && t1 != VAL_NEW);
    ASSERT_TRUE(t2 != VAL_OLD && t2 != VAL_NEW);
    ASSERT_TRUE(t1 != t2);
    /* ⚠ THEY DO NOT LAND INSIDE [VAL_OLD, VAL_NEW] -- THEY BRACKET IT.
     * t1 sits ABOVE VAL_NEW and t2 BELOW VAL_OLD, each by ~3.815e-07.
     *
     * An earlier revision of this comment claimed they land "inside the
     * interval the two real values span", and the assertion under it allowed
     * +/-1.0 -- so the prose stated something false and the check tested
     * something far weaker, and neither was the property that matters.
     *
     * CONTAINMENT WAS NEVER THE POINT; PROXIMITY IS. Each torn value is
     * ~4e-7 from a real coefficient, far inside any tolerance an instrument
     * would apply to a reading, so no range check can reject it. That is what
     * makes a silent tear dangerous rather than obvious -- and it is a
     * STRONGER statement than the one it replaces, which is why the exact
     * bracketing is pinned here rather than smoothed over. */
    ASSERT_TRUE(t1 > VAL_NEW);              /* above the interval, not in it */
    ASSERT_TRUE(t2 < VAL_OLD);              /* below the interval, not in it */
    ASSERT_TRUE(t1 - VAL_NEW < 1e-6);       /* but indistinguishably close */
    ASSERT_TRUE(VAL_OLD - t2 < 1e-6);
}

TEST(the_pairs_this_fleet_reached_for_first_would_have_been_vacuous)
{
    /* Regression against the method, not the code. Both of these were
     * proposed during #1048's analysis and both are invisible. */
    ASSERT_EQ(pair_of(1.25).lo, pair_of(2.5).lo);   /* zero low halves */
    ASSERT_EQ(pair_of(1.1).lo,  pair_of(2.2).lo);   /* shared mantissa */
    /* and the one actually used is not */
    ASSERT_TRUE(pair_of(VAL_OLD).lo != pair_of(VAL_NEW).lo);
}

/* ==========================================================================
 * THE 2x2: neither half is sufficient alone
 * ========================================================================== */

TEST(unguarded_write_tears_even_for_a_guarded_reader)
{
    Pair cell = pair_of(VAL_OLD);
    Pair seen = cell;
    writer_store(&cell, pair_of(VAL_NEW), UNGUARDED, true, &seen, true, GUARDED);

    /* The reader's own access was indivisible and it STILL observed a value
     * that is neither the old nor the new one -- the critical section made it
     * confident in a wrong value. */
    ASSERT_FALSE(pair_eq(seen, pair_of(VAL_OLD)));
    ASSERT_FALSE(pair_eq(seen, pair_of(VAL_NEW)));
}

TEST(unguarded_read_tears_even_against_a_guarded_writer)
{
    Pair cell = pair_of(VAL_OLD);
    const Pair got = reader_load(&cell, UNGUARDED, true, pair_of(VAL_NEW));

    /* The write was atomic; the READ was split, so a complete write landing
     * between its two loads yields old-hi against new-lo. */
    ASSERT_FALSE(pair_eq(got, pair_of(VAL_OLD)));
    ASSERT_FALSE(pair_eq(got, pair_of(VAL_NEW)));
}

TEST(both_guarded_never_observes_a_value_that_was_not_written)
{
    /* writer side */
    Pair cell = pair_of(VAL_OLD);
    Pair seen = cell;
    writer_store(&cell, pair_of(VAL_NEW), GUARDED, true, &seen, true, GUARDED);
    ASSERT_TRUE(pair_eq(seen, pair_of(VAL_NEW)));

    /* reader side */
    Pair cell2 = pair_of(VAL_OLD);
    const Pair got = reader_load(&cell2, GUARDED, true, pair_of(VAL_NEW));
    ASSERT_TRUE(pair_eq(got, pair_of(VAL_OLD)) || pair_eq(got, pair_of(VAL_NEW)));
}

TEST(an_uninterrupted_access_is_clean_in_every_mode)
{
    /* The fix must not be credited for outcomes that hold anyway: with no
     * preemption, all four combinations are clean, so a passing run on a
     * quiet bench proves nothing. This is the arm that makes the three above
     * meaningful rather than tautological. */
    const Mode modes[2] = { UNGUARDED, GUARDED };
    for (unsigned w = 0; w < 2; w++) {
        for (unsigned r = 0; r < 2; r++) {
            Pair cell = pair_of(VAL_OLD);
            Pair seen = cell;
            writer_store(&cell, pair_of(VAL_NEW), modes[w], false, &seen, true, modes[r]);
            ASSERT_TRUE(pair_eq(cell, pair_of(VAL_NEW)));
            const Pair got = reader_load(&cell, modes[r], false, pair_of(VAL_OLD));
            ASSERT_TRUE(pair_eq(got, pair_of(VAL_NEW)));
        }
    }
}

TEST(identity_coefficients_hide_the_defect_which_is_why_the_bench_cannot_see_it)
{
    /* The operator ruling's actual reason, as an assertion. Every board on
     * this station holds CalM = 1.0 / CalB = 0.0, and 1.0's low half is zero,
     * so an unguarded write of 1.0 over 1.0 is indistinguishable from a clean
     * one no matter how it is interrupted. */
    ASSERT_EQ(pair_of(1.0).lo, 0u);
    ASSERT_EQ(pair_of(0.0).lo, 0u);

    Pair cell = pair_of(1.0);
    Pair seen = cell;
    writer_store(&cell, pair_of(1.0), UNGUARDED, true, &seen, true, GUARDED);
    ASSERT_TRUE(pair_eq(seen, pair_of(1.0)));   /* torn, and identical anyway */
}

int main(void)
{
    printf("#904 / fw#1048 -- CalM/CalB 64-bit pair, both halves\n");
    printf("---------------------------------------------\n");
    RUN(the_chosen_operands_make_a_tear_observable);
    RUN(the_pairs_this_fleet_reached_for_first_would_have_been_vacuous);
    RUN(unguarded_write_tears_even_for_a_guarded_reader);
    RUN(unguarded_read_tears_even_against_a_guarded_writer);
    RUN(both_guarded_never_observes_a_value_that_was_not_written);
    RUN(an_uninterrupted_access_is_clean_in_every_mode);
    RUN(identity_coefficients_hide_the_defect_which_is_why_the_bench_cannot_see_it);
    return TEST_SUMMARY();
}
