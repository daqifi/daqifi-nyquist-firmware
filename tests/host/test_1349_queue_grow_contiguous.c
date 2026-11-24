/* #1349: the sample-queue grow must check the largest CONTIGUOUS free block,
 * not only total free heap, BEFORE it deletes the old queue.
 *
 * WHAT WAS WRONG (by reading; never observed on a board). AInSample.c checked
 * xPortGetFreeHeapSize() (the total), deleted the old queue, then called
 * xQueueCreate. A failed create reaches vApplicationMallocFailedHook, which
 * disables interrupts and loops, so the board stops until reset. A heap split
 * into pieces that are each too small to hold the new queue passes a total
 * check and still halts.
 *
 * THIS TEST SPLICES THE REAL PREDICATE (QueueGrowFits, extracted verbatim by
 * the Makefile, which fails the build if it cannot). It does not model it.
 *
 * Provoking a split heap on a board is out of scope for the ticket, so the
 * decision is what is tested here. Two mutants are kept as code, each of which
 * must FAIL the leg that exists to catch it:
 *
 *   totalOnly   the pre-fix predicate (total free heap only)
 *   noRounding  compares against the unrounded block, one alignment step short
 */
#include <stdio.h>
#include <stdbool.h>
#include <stddef.h>
#include "test_framework.h"

#include "gen_1349_queue_grow.h"

/* The queue's items are pointers, as in AInSample.c (sizeof pointer there). */
#define SLOT ((size_t)4u)

static bool QueueGrowFits_totalOnly(size_t slots, size_t slotBytes,
                                    size_t freeHeap, size_t largestFreeBlock)
{
    (void)largestFreeBlock;
    return freeHeap >= slots * slotBytes + 80u + 1024u;
}

static bool QueueGrowFits_noRounding(size_t slots, size_t slotBytes,
                                     size_t freeHeap, size_t largestFreeBlock)
{
    size_t needed = slots * slotBytes + 80u;
    return (freeHeap >= needed + 1024u) && (largestFreeBlock >= needed + 8u);
}

/* Block for a queue of n 4-byte slots: 80 + 4n + 8, rounded up to 8. */
static size_t block_of(size_t n)
{
    size_t b = 80u + n * SLOT + 8u;
    return (b + 7u) & ~(size_t)7u;
}

/* 1. The measured case from the ticket: 1101 slots need a 4496-byte block. */
TEST(the_block_size_matches_the_measured_queue)
{
    ASSERT_EQ(block_of(1100u), (size_t)4488u);
    ASSERT_EQ(block_of(1101u), (size_t)4496u);
}

/* 2. Boundary: largest block exactly the needed size grants. */
TEST(a_largest_block_exactly_the_needed_size_grants)
{
    const size_t n = 1101u;
    const size_t free_total = 100000u;
    ASSERT_TRUE(QueueGrowFits(n, SLOT, free_total, block_of(n)));
}

/* 3. One 8-byte step less refuses -- and the mutants discriminate. */
TEST(one_alignment_step_less_refuses)
{
    const size_t n = 1101u;
    const size_t free_total = 100000u;
    ASSERT_TRUE(!QueueGrowFits(n, SLOT, free_total, block_of(n) - 8u));
    /* a split heap: total is ample, no contiguous block fits */
    ASSERT_TRUE(!QueueGrowFits(n, SLOT, free_total, 2048u));
    /* positive controls */
    ASSERT_TRUE(QueueGrowFits_totalOnly(n, SLOT, free_total, 2048u));
    ASSERT_TRUE(QueueGrowFits_noRounding(n, SLOT, free_total, block_of(n) - 8u + 4u)
                != QueueGrowFits(n, SLOT, free_total, block_of(n) - 8u + 4u));
}

/* 4. The existing total-heap check stays: a big contiguous block does not
 *    waive the 1024-byte total margin. */
TEST(the_total_heap_margin_is_kept)
{
    const size_t n = 1101u;
    const size_t needed = n * SLOT + 80u;
    ASSERT_TRUE(!QueueGrowFits(n, SLOT, needed + 1023u, 1000000u));
    ASSERT_TRUE(QueueGrowFits(n, SLOT, needed + 1024u, 1000000u));
}

/* 5. A sizeof-pointer slot other than 4 still rounds correctly (host is 8). */
TEST(the_slot_size_is_a_parameter)
{
    const size_t n = 100u;
    const size_t b = (n * 8u + 80u + 8u + 7u) & ~(size_t)7u;
    ASSERT_TRUE(QueueGrowFits(n, 8u, 100000u, b));
    ASSERT_TRUE(!QueueGrowFits(n, 8u, 100000u, b - 8u));
}

int main(void)
{
    puts("#1349 -- sample-queue grow needs a contiguous fit (real predicate, spliced)");
    RUN(the_block_size_matches_the_measured_queue);
    RUN(a_largest_block_exactly_the_needed_size_grants);
    RUN(one_alignment_step_less_refuses);
    RUN(the_total_heap_margin_is_kept);
    RUN(the_slot_size_is_a_parameter);
    return TEST_SUMMARY();
}
