/* #996: csv_Encode() must evict a PERMANENTLY unfittable row, and only that.
 *
 * csv_encoder.c cannot be host-compiled -- it pulls in board config, the ADC
 * HAL and FreeRTOS -- so this models the eviction DECISION, and
 * check_996_eviction_permanence.py reads the real source to keep the model
 * honest. That pin runs inside this binary's Makefile recipe, so a model
 * that has drifted from the source fails the BUILD rather than passing
 * quietly. The two files are halves of one claim.
 *
 * ⛔ WHAT THIS FILE CANNOT SEE, stated so nobody mistakes a green run here
 * for agreement with the firmware: should_evict() below is a COPY of the
 * condition. Change an operator in csv_encoder.c -- `&&` to `||` -- and
 * every test here still passes, because they execute this copy. That was an
 * r1 MEDIUM on this PR: the pin was searching the real condition for the
 * operand STRINGS and was equally blind, so the pair agreed with each other
 * while disagreeing with the source. The pin now checks the real
 * condition's STRUCTURE (top-level `&&`, each conjunct required); the
 * division of labour is that this file says what the decision should be and
 * only the pin can say the firmware still computes it.
 *
 * ⛔ THE DECISION HAS TWO FAILURE DIRECTIONS AND THEY ARE NOT SYMMETRIC.
 *
 *   evict too late  -> the row sits at the head of a FIFO forever, re-peeked
 *                      and re-rejected, taking every sample behind it. The
 *                      CSV stream silently stops carrying ADC data. This is
 *                      the #996 defect.
 *   evict too early -> a row that a later call WOULD have written is thrown
 *                      away. Silent data loss. This is what #961, the JSON
 *                      twin, shipped in its round 2 and needed a third round
 *                      to undo.
 *
 * So "does not fit the room left in THIS call" and "does not fit ANY buffer,
 * ever" must stay different verdicts. Permanence is three conjuncts:
 *
 *     (hadAIN || hadDIO)   something was actually peeked
 *     && fullCapacityCall  this call was handed the whole configured buffer
 *     && rem == maxRoom    and none of that room is already spent
 *
 * Most of the cases below are the NEGATIVE ones, because over-eviction is
 * the expensive direction and a model that only checks "it evicts" would
 * pass while evicting everything.
 */
#include <stdbool.h>
#include <stddef.h>

#include "test_framework.h"

/* The modelled decision. Mirrors csv_encoder.c's eviction condition; the
 * Makefile pin asserts the real one still has these three conjuncts. */
static bool should_evict(bool hadAIN, bool hadDIO,
                         size_t buffSize, size_t configuredBuffSize,
                         size_t rem, size_t maxRoom)
{
    const bool fullCapacityCall = (buffSize == configuredBuffSize);
    return (hadAIN || hadDIO) && fullCapacityCall && (rem == maxRoom);
}

/* The streaming path's shape: streaming.c resets packetSize to 0 before
 * every batch-build loop, so batchIdx==0 is handed the whole buffer. */
#define CFG_BUFF   1024u
#define MAX_ROOM   (CFG_BUFF - 1u)   /* csv_encoder reserves 1 byte for '\0' */

TEST(a_row_that_can_never_fit_is_evicted)
{
    /* First attempt of a batch: whole configured buffer, nothing spent. A
     * row still too big here is too big for any call that will ever come. */
    ASSERT_TRUE(should_evict(true, true, CFG_BUFF, CFG_BUFF,
                             MAX_ROOM, MAX_ROOM));
}

TEST(an_ain_only_row_that_can_never_fit_is_evicted)
{
    ASSERT_TRUE(should_evict(true, false, CFG_BUFF, CFG_BUFF,
                             MAX_ROOM, MAX_ROOM));
}

TEST(a_dio_only_row_that_can_never_fit_is_evicted)
{
    ASSERT_TRUE(should_evict(false, true, CFG_BUFF, CFG_BUFF,
                             MAX_ROOM, MAX_ROOM));
}

TEST(a_row_that_merely_ran_out_of_room_this_call_is_kept)
{
    /* ⛔ THE #961 INVERSION. Room already spent by earlier rows in this
     * batch, so the next call gets a fresh full buffer and may well fit
     * this row. Evicting here is silent data loss. */
    ASSERT_FALSE(should_evict(true, true, CFG_BUFF, CFG_BUFF,
                             /* rem */ 40u, MAX_ROOM));
}

TEST(a_row_one_byte_short_of_full_room_is_still_kept)
{
    /* The boundary, from the keep side: `rem == maxRoom` and not `>=`. */
    ASSERT_FALSE(should_evict(true, true, CFG_BUFF, CFG_BUFF,
                              MAX_ROOM - 1u, MAX_ROOM));
}

TEST(a_caller_with_its_own_smaller_buffer_never_evicts)
{
    /* A non-streaming caller passing its own buffer cannot conclude
     * permanence from it: the streaming path's buffer may be far larger.
     * Such a caller keeps the old retry-forever behaviour by design. */
    ASSERT_FALSE(should_evict(true, true, /* buffSize */ 128u, CFG_BUFF,
                              127u, 127u));
}

TEST(a_caller_with_a_larger_buffer_also_never_evicts)
{
    /* Equality, not >=: a caller whose buffer is bigger than the configured
     * one is equally unable to speak for the streaming path. */
    ASSERT_FALSE(should_evict(true, true, /* buffSize */ 4096u, CFG_BUFF,
                              4095u, 4095u));
}

TEST(nothing_peeked_means_nothing_to_evict)
{
    /* Empty queues are not an oversize row. Evicting here would pop a
     * queue that had nothing in it and book a loss that never happened. */
    ASSERT_FALSE(should_evict(false, false, CFG_BUFF, CFG_BUFF,
                              MAX_ROOM, MAX_ROOM));
}

TEST(the_three_conjuncts_are_each_necessary)
{
    /* Each dropped in turn, from the one state where eviction is correct:
     * every single relaxation must flip the verdict to keep. */
    ASSERT_TRUE(should_evict(true, true, CFG_BUFF, CFG_BUFF, MAX_ROOM, MAX_ROOM));
    ASSERT_FALSE(should_evict(false, false, CFG_BUFF, CFG_BUFF, MAX_ROOM, MAX_ROOM));
    ASSERT_FALSE(should_evict(true, true, 128u, CFG_BUFF, MAX_ROOM, MAX_ROOM));
    ASSERT_FALSE(should_evict(true, true, CFG_BUFF, CFG_BUFF, MAX_ROOM - 1u, MAX_ROOM));
}

int main(void)
{
    RUN(a_row_that_can_never_fit_is_evicted);
    RUN(an_ain_only_row_that_can_never_fit_is_evicted);
    RUN(a_dio_only_row_that_can_never_fit_is_evicted);
    RUN(a_row_that_merely_ran_out_of_room_this_call_is_kept);
    RUN(a_row_one_byte_short_of_full_room_is_still_kept);
    RUN(a_caller_with_its_own_smaller_buffer_never_evicts);
    RUN(a_caller_with_a_larger_buffer_also_never_evicts);
    RUN(nothing_peeked_means_nothing_to_evict);
    RUN(the_three_conjuncts_are_each_necessary);
    return TEST_SUMMARY();
}
