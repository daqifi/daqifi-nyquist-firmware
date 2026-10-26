/* ==========================================================================
 * test_908_cal_source_seqlock.c -- PR firmware#1077 round 8 (issue #908)
 *
 * WHAT IS UNDER TEST
 *
 * CONF:CAP:JSON?'s identity.cal.source answers "which coefficients are IN
 * EFFECT", and that is a COMPOSITE of two facts: whether every live CalM/CalB
 * pair is identity, and which bank the live values were last loaded from. The
 * emitter asks daqifi_settings_GetCalSource() for both at once.
 *
 * The writer is daqifi_settings_LoadADCCalSettings(), which does:
 *
 *     copy every channel's CalM/CalB from the bank   <- fact 1 changes
 *     gActiveCalBank = type                          <- fact 2 changes, LAST
 *
 * The bank is published LAST on purpose -- that is round 3's fix, so the bank
 * can never name a load that did not reach the runtime array. ⭐ But it is
 * exactly what makes an IN-PROGRESS load invisible to a bank comparison, and
 * that tension is what this file exists to pin.
 *
 * THREE READER SHAPES, and the middle one is the interesting failure
 *
 *   NAIVE     scan, then read the bank. Two independent samples.
 *   BANKCMP   read bank, scan, read bank, require unchanged. This was round
 *             6's fix. It detects a load that COMPLETED under the scan, and is
 *             BLIND to one still in progress: a reader pre-empting between the
 *             copy loop and the bank store sees the OLD bank twice while
 *             scanning the NEW coefficients, so both samples agree and it
 *             reports a source the device never had.
 *   SEQLOCK   require an EVEN generation, scan, read bank, require the
 *             generation UNCHANGED. The writer increments before the copy and
 *             again after publishing the bank, so the whole composite update
 *             is bracketed and the in-progress window is odd.
 *
 * HOW IT IS TESTED
 *
 * No threads and no scheduler: a model, in the shape of
 * test_1112_scan_list_snapshot_race.c. The writer is decomposed into its
 * observable steps and a "preemption point" selects how far it has progressed
 * when the reader runs. The reader then samples that frozen intermediate
 * state. A real preemption is the same thing with the step boundary chosen by
 * the scheduler rather than by the test.
 *
 * ⚠ What is NOT under test: tearing of an individual CalM or CalB. That is
 * #904's 64-bit pair hazard, owned by #1048 under operator ruling 8 = A, and
 * the coefficients here are modelled as indivisible values deliberately -- so
 * that a failure in this file means "the composite was incoherent", never "a
 * double tore". Conflating the two is how a fix for one gets credited to the
 * other.
 *
 * ⚠ Also not under test: staleness. Every shape is a snapshot; the state can
 * change the instant after it is taken, and the reply is then merely out of
 * date. What is under test is that the reported source is always a pair the
 * device ACTUALLY HAD, never a hybrid of two.
 * ========================================================================== */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "test_framework.h"

#define MODEL_CHANNELS 4

/* Mirrors DaqifiCalSource. */
typedef enum { SRC_NONE = 0, SRC_FACTORY, SRC_USER } CalSource;
typedef enum { BANK_FACTORY = 0, BANK_USER } Bank;
typedef enum { SHAPE_NAIVE = 0, SHAPE_BANKCMP, SHAPE_SEQLOCK } ReaderShape;

/* The shared state a reader can observe. Coefficients are indivisible here --
 * see the note above about not conflating this with #904's tear. */
typedef struct {
    bool     identity[MODEL_CHANNELS];   /* true = CalM 1 / CalB 0 */
    Bank     bank;
    uint32_t generation;
} CalState;

/* ---- the writer, decomposed ------------------------------------------------
 * progress 0            = not started
 * progress 1..N         = N channels copied, bank NOT yet published
 * progress N+1          = bank published, update complete
 * `bracket` selects whether the writer maintains the generation counter. */
static void writer_run(CalState *st, Bank target, const bool *newIdentity,
                       unsigned progress, bool bracket)
{
    if (progress == 0u) {
        return;
    }
    if (bracket) {
        st->generation++;              /* -> ODD: update in flight */
    }
    for (unsigned i = 0; i < MODEL_CHANNELS && i < progress; i++) {
        st->identity[i] = newIdentity[i];
    }
    if (progress > MODEL_CHANNELS) {
        st->bank = target;
        if (bracket) {
            st->generation++;          /* -> EVEN: both facts now agree */
        }
    }
}

static bool all_identity(const CalState *st)
{
    for (unsigned i = 0; i < MODEL_CHANNELS; i++) {
        if (!st->identity[i]) {
            return false;
        }
    }
    return true;
}

static CalSource classify(bool isIdentity, Bank bank)
{
    if (isIdentity) {
        return SRC_NONE;
    }
    return (bank == BANK_USER) ? SRC_USER : SRC_FACTORY;
}

/* The reader. `retries` mirrors the firmware's 3-attempt bound; because the
 * model's state is frozen, a retry cannot succeed where the first attempt
 * failed, which is exactly why the bounded fallback matters. */
static CalSource reader_run(const CalState *st, ReaderShape shape)
{
    for (unsigned attempt = 0; attempt < 3u; attempt++) {
        if (shape == SHAPE_NAIVE) {
            const bool ident = all_identity(st);
            return classify(ident, st->bank);
        }
        if (shape == SHAPE_BANKCMP) {
            const Bank b0 = st->bank;
            const bool ident = all_identity(st);
            const Bank b1 = st->bank;
            if (b0 != b1) {
                continue;
            }
            return classify(ident, b1);
        }
        /* SHAPE_SEQLOCK */
        const uint32_t g0 = st->generation;
        if ((g0 & 1u) != 0u) {
            continue;
        }
        const bool ident = all_identity(st);
        const Bank b = st->bank;
        if (st->generation != g0) {
            continue;
        }
        return classify(ident, b);
    }
    return SRC_NONE;   /* understating direction, as in the firmware */
}

/* Factory bank is identity (an uncalibrated board); the user bank is real. */
static void state_init_factory_identity(CalState *st)
{
    for (unsigned i = 0; i < MODEL_CHANNELS; i++) {
        st->identity[i] = true;
    }
    st->bank = BANK_FACTORY;
    st->generation = 0u;
}

static const bool kUserNonIdentity[MODEL_CHANNELS] = { false, false, false, false };

/* ==========================================================================
 * The defect: a LOADcal to the USER bank, observed part-way
 * ========================================================================== */

TEST(bankcmp_reports_factory_while_scanning_user_coefficients)
{
    /* The whole point. At progress 1..N the coefficients are (partly) the
     * USER bank's, but the bank still reads FACTORY -- and reads FACTORY
     * BOTH times, so round 6's comparison is satisfied and returns. */
    int fictions = 0;
    for (unsigned progress = 1; progress <= MODEL_CHANNELS; progress++) {
        CalState st;
        state_init_factory_identity(&st);
        writer_run(&st, BANK_USER, kUserNonIdentity, progress, true);

        const CalSource got = reader_run(&st, SHAPE_BANKCMP);
        /* Non-identity coefficients attributed to the FACTORY bank, when the
         * only non-identity values in play came from the USER bank. */
        if (got == SRC_FACTORY) {
            fictions++;
        }
    }
    ASSERT_TRUE(fictions > 0);
}

TEST(naive_reports_factory_while_scanning_user_coefficients)
{
    CalState st;
    state_init_factory_identity(&st);
    writer_run(&st, BANK_USER, kUserNonIdentity, MODEL_CHANNELS, true);
    ASSERT_EQ(reader_run(&st, SHAPE_NAIVE), SRC_FACTORY);
}

TEST(seqlock_never_reports_a_pair_the_device_did_not_have)
{
    /* Every observable instant of the writer, including every partial copy. */
    for (unsigned progress = 0; progress <= MODEL_CHANNELS + 1u; progress++) {
        CalState st;
        state_init_factory_identity(&st);
        writer_run(&st, BANK_USER, kUserNonIdentity, progress, true);

        const CalSource got = reader_run(&st, SHAPE_SEQLOCK);

        if (progress <= MODEL_CHANNELS) {
            /* Mid-update: the generation is ODD, so the reader must refuse to
             * answer from it and fall back -- never SRC_FACTORY. */
            ASSERT_EQ(got, SRC_NONE);
        } else {
            /* Complete: user bank, non-identity coefficients. */
            ASSERT_EQ(got, SRC_USER);
        }
    }
}

TEST(seqlock_agrees_with_the_others_when_nothing_is_in_flight)
{
    CalState st;
    state_init_factory_identity(&st);
    writer_run(&st, BANK_USER, kUserNonIdentity, MODEL_CHANNELS + 1u, true);
    ASSERT_EQ(reader_run(&st, SHAPE_SEQLOCK),  SRC_USER);
    ASSERT_EQ(reader_run(&st, SHAPE_BANKCMP),  SRC_USER);
    ASSERT_EQ(reader_run(&st, SHAPE_NAIVE),    SRC_USER);
}

TEST(an_untouched_uncalibrated_board_reads_none_in_every_shape)
{
    CalState st;
    state_init_factory_identity(&st);
    ASSERT_EQ(reader_run(&st, SHAPE_SEQLOCK), SRC_NONE);
    ASSERT_EQ(reader_run(&st, SHAPE_BANKCMP), SRC_NONE);
    ASSERT_EQ(reader_run(&st, SHAPE_NAIVE),   SRC_NONE);
}

/* ==========================================================================
 * The mutation the audit asked for: drop the PRE-COPY increment
 * ========================================================================== */

TEST(dropping_the_pre_copy_increment_reopens_the_defect)
{
    /* `bracket = false` models a writer that publishes the bank and bumps
     * nothing -- i.e. the generation never goes odd, so the in-flight window
     * is invisible and SEQLOCK degrades to exactly BANKCMP's blindness.
     *
     * This is the discriminating arm: if this test ever passes while the one
     * above also passes, the seqlock is not doing the work its comment claims. */
    int fictions = 0;
    for (unsigned progress = 1; progress <= MODEL_CHANNELS; progress++) {
        CalState st;
        state_init_factory_identity(&st);
        writer_run(&st, BANK_USER, kUserNonIdentity, progress, false);
        if (reader_run(&st, SHAPE_SEQLOCK) == SRC_FACTORY) {
            fictions++;
        }
    }
    ASSERT_TRUE(fictions > 0);
}

TEST(the_bound_is_three_and_the_fallback_understates)
{
    /* The model's state is frozen, so an odd generation can never resolve.
     * The reader must therefore exhaust its bound and return the understating
     * value rather than naming a bank -- a false provenance claim is the one
     * outcome #908 exists to prevent. */
    CalState st;
    state_init_factory_identity(&st);
    st.generation = 1u;               /* stuck mid-update */
    for (unsigned i = 0; i < MODEL_CHANNELS; i++) {
        st.identity[i] = false;       /* real calibration present */
    }
    st.bank = BANK_USER;
    ASSERT_EQ(reader_run(&st, SHAPE_SEQLOCK), SRC_NONE);
}

/* ==========================================================================
 * r8 finding 2: TWO setters manufacture a view that held at no instant
 * ========================================================================== */

/* An early-exiting identity scan, decomposed so a setter can land between two
 * channel reads. Returns what the scan CONCLUDES. */
static bool scan_with_interleaved_setters(CalState *st, bool bracket)
{
    /* channel 0 identity, channel 2 NOT -- so a clean scan concludes
     * "not all identity". */
    st->identity[0] = true;
    st->identity[1] = true;
    st->identity[2] = false;
    st->identity[3] = true;

    bool allIdentity = true;
    for (unsigned i = 0; i < MODEL_CHANNELS; i++) {
        if (i == 2u) {
            /* Two independent setters land here, between channel reads:
             * one CLEARS channel 2, the other SETS channel 0. */
            if (bracket) { st->generation++; }
            st->identity[2] = true;      /* setter A: clear a later channel */
            if (bracket) { st->generation++; }
            if (bracket) { st->generation++; }
            st->identity[0] = false;     /* setter B: set an earlier one */
            if (bracket) { st->generation++; }
        }
        if (!st->identity[i]) {
            allIdentity = false;
            break;                        /* early exit, as the firmware does */
        }
    }
    return allIdentity;
}

TEST(two_unbracketed_setters_make_the_scan_conclude_a_state_that_never_held)
{
    CalState st;
    state_init_factory_identity(&st);
    st.bank = BANK_FACTORY;

    const bool concluded = scan_with_interleaved_setters(&st, false);

    /* The scan says "all identity"... */
    ASSERT_TRUE(concluded);
    /* ...but channel 0 is non-identity by the time it finishes, and channel 2
     * was non-identity when it started. "All identity" held at no instant. */
    ASSERT_FALSE(st.identity[0]);
}

TEST(bracketing_both_setters_lets_the_reader_detect_the_interleave)
{
    CalState st;
    state_init_factory_identity(&st);
    st.bank = BANK_FACTORY;

    const uint32_t g0 = st.generation;
    (void)scan_with_interleaved_setters(&st, true);

    /* Four increments -- two setters, bracketed -- so a seqlock reader that
     * sampled g0 before the scan sees a changed generation and discards the
     * conclusion rather than reporting it. That is the only mechanism that
     * can reject it: the conclusion itself looks perfectly ordinary. */
    ASSERT_TRUE(st.generation != g0);
    ASSERT_EQ((st.generation - g0) % 2u, 0u);   /* all brackets closed */
}

TEST(one_setter_alone_really_is_harmless_which_is_why_this_looked_exempt)
{
    /* The r7 argument, preserved as a test so the reasoning is visible: with a
     * SINGLE setter, whatever the scan sees is a state the device genuinely
     * had. The r8 finding is not that the argument was wrong -- it is that it
     * does not extend to two. */
    CalState st;
    state_init_factory_identity(&st);
    st.identity[2] = false;

    bool allIdentity = true;
    for (unsigned i = 0; i < MODEL_CHANNELS; i++) {
        if (i == 2u) { st.identity[3] = false; }   /* one setter, later channel */
        if (!st.identity[i]) { allIdentity = false; break; }
    }
    /* Concludes "not all identity", which was true before AND after. */
    ASSERT_FALSE(allIdentity);
}

int main(void)
{
    printf("#908 round 8 -- identity.cal.source composite coherence\n");
    printf("---------------------------------------------\n");
    RUN(naive_reports_factory_while_scanning_user_coefficients);
    RUN(bankcmp_reports_factory_while_scanning_user_coefficients);
    RUN(seqlock_never_reports_a_pair_the_device_did_not_have);
    RUN(seqlock_agrees_with_the_others_when_nothing_is_in_flight);
    RUN(an_untouched_uncalibrated_board_reads_none_in_every_shape);
    RUN(dropping_the_pre_copy_increment_reopens_the_defect);
    RUN(the_bound_is_three_and_the_fallback_understates);
    RUN(two_unbracketed_setters_make_the_scan_conclude_a_state_that_never_held);
    RUN(bracketing_both_setters_lets_the_reader_detect_the_interleave);
    RUN(one_setter_alone_really_is_harmless_which_is_why_this_looked_exempt);
    return TEST_SUMMARY();
}
