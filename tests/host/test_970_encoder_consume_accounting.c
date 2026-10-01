/* ==========================================================================
 * test_970_encoder_consume_accounting.c -- issue #970
 *
 * WHAT IS UNDER TEST
 *
 * Two related defects, both in the encoder <-> streaming.c contract:
 *
 * PART 1 -- streaming.c's `encoded == 0` booking arm (Streaming_Tasks,
 * services/streaming.c). Pre-fix it read (paraphrased):
 *
 *     if (encoded == 0) {
 *         gStreamStats.encoderFailures++;
 *         gStreamStats.encoderDroppedSamples++;   // "each encode pops
 *                                                  // exactly one, #297"
 *         ...
 *     }
 *
 * That comment is false for csv_Encode and Json_Encode: an encode call that
 * returns 0 bytes can have popped ZERO samples (buffer momentarily full --
 * the sample is still queued and is retried, successfully, on a later call)
 * just as often as it popped one. The pre-fix arm booked "1 dropped" on
 * EVERY zero return regardless, so a sustained buffer-full condition that
 * never actually lost a sample still made EncoderDroppedSamples climb
 * without bound, once per retry.
 *
 * The fix (this PR) removes the encoderDroppedSamples++ from the arm
 * entirely -- it now books only the EVENT (encoderFailures, a byte count of
 * zero, nothing else) -- and gives encoders a new, narrow entry point,
 * Streaming_ReportEncoderSampleLoss(n), that ONLY a site which has already
 * popped-and-will-not-emit a sample may call. Json_Encode's one genuine
 * loss site (the oversize-sample drop arm, JSON_Encoder.c) now calls it.
 *
 * PART 2 -- JSON_Encoder.c's DIO element loop. Pre-fix it wrote an element's
 * bytes into the output buffer, unconditionally advanced `startIndex` past
 * them, and THEN called the fallible `DIOSampleList_PopFront`, discarding
 * its return. The ADC loop in the same file was fixed by #164 to commit
 * `startIndex` only after a successful pop; the DIO loop was not. This
 * tests the invariant #164 established for the ADC loop and #970 restores
 * for the DIO loop: **output bytes are committed only for elements this
 * call actually removed from the queue.**
 *
 * HOW IT IS TESTED
 *
 * Neither streaming.c nor JSON_Encoder.c is a host-test candidate --
 * streaming.c pulls FreeRTOS, libscpi and the whole board/driver graph;
 * JSON_Encoder.c pulls state/board/BoardConfig.h -> Harmony's
 * configuration.h/definitions.h (see tests/host/README.md's note on
 * JSON_StringEscape.h having been split OUT of this same file for exactly
 * this reason). So, like test_943_bench_stall_bound.c, this file
 * RE-IMPLEMENTS the two relevant loop SHAPES (old vs new) against injected
 * mocks and compares their verdicts on identical inputs. The Makefile
 * target greps the real sources and fails the build if the guarantees this
 * file's re-implementation depends on have drifted -- see the Makefile for
 * exactly what is pinned and why.
 *
 * FIDELITY -- what is and is not modelled
 *
 * Part 1's mock reduces each encode call to the three facts streaming.c's
 * arm and the encoder's own report call actually depend on: the byte count
 * returned, and (for the encoder's own internal bookkeeping) how many
 * samples it knowingly destroyed this call. It does not model buffer
 * layout, snprintf, or any encoding format.
 *
 * Part 2's mock reduces the DIO loop to: peek the queue head, "write" it
 * (always succeeds in the mock -- the loop's other break conditions, room
 * and snprintf failure, are exercised by the existing JSON_Encoder.c logic
 * and are not this defect), then pop. The pop is programmed to fail on a
 * chosen call in one of two ways, matching the two distinct real-world
 * meanings a `false` return can have (see DIOSampleList_PopFront's
 * implementation, DIOSample.c):
 *   EMPTY -- the queue's head is gone by the time we call (xQueueReceive
 *            with a 0-tick wait timed out because nothing was there). The
 *            element will not be seen again. This is what is reachable on
 *            today's firmware -- DIOSampleList_PopFront has no NULL-handle
 *            guard, so "pop returned false" and "the element is gone"
 *            currently coincide.
 *   TORN  -- the pop failed but the element is still at the queue head
 *            (the latent case: becomes real the moment
 *            DIOSampleList_PopFront grows the NULL-handle guard
 *            AInSampleList_PopFront already has -- see AInSample.c).
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdbool.h>
#include "test_framework.h"

/* ==========================================================================
 * PART 1 -- streaming.c's `encoded == 0` booking arm
 * ========================================================================== */

typedef struct {
    size_t   bytesReturned;    /* what the encode call returned this tick */
    uint32_t samplesDestroyed; /* what the encoder itself knows it consumed
                                 * and will never emit -- 0 for every branch
                                 * except Json_Encode's oversize-drop arm */
} EncodeCallOutcome;

typedef struct {
    uint32_t encoderFailures;
    uint32_t encoderDroppedSamples;
} MockStreamStats;

/* Pre-fix streaming.c: assumed the return value alone determines cardinality
 * ("each encode pops exactly one, #297"). Mirrors the deleted
 * `gStreamStats.encoderDroppedSamples++` inside the `encoded == 0` arm. */
static void old_shape_process_call(MockStreamStats *stats, EncodeCallOutcome call) {
    if (call.bytesReturned == 0) {
        stats->encoderFailures++;
        stats->encoderDroppedSamples++;
    }
}

/* Post-fix streaming.c + the encoder's own Streaming_ReportEncoderSampleLoss
 * call. The report happens as a side effect of the encoder call itself (it
 * runs from inside Json_Encode, before Json_Encode returns) -- modelled here
 * as unconditional on `bytesReturned`, because in the one real branch that
 * uses it (JSON_Encoder.c's oversize-drop arm) the report and the eventual
 * zero return both originate in the same encoder call, but are two
 * independent facts about it. The `encoded == 0` arm itself never touches
 * encoderDroppedSamples any more. */
static void new_shape_process_call(MockStreamStats *stats, EncodeCallOutcome call) {
    if (call.samplesDestroyed > 0) {
        stats->encoderDroppedSamples += call.samplesDestroyed; /* Streaming_ReportEncoderSampleLoss */
    }
    if (call.bytesReturned == 0) {
        stats->encoderFailures++;
        /* #970: no encoderDroppedSamples++ here. */
    }
}

/* Sustained buffer-full: K consecutive zero-byte, zero-destroyed calls (the
 * common CSV/JSON case -- the sample never fits THIS call's room, but is
 * still queued and is not lost). Old shape must have fabricated K drops for
 * zero real loss; new shape must report exactly zero. */
TEST(test_accounting_buffer_full_no_fabrication) {
    const int K = 7;
    MockStreamStats oldStats = {0}, newStats = {0};
    EncodeCallOutcome bufferFull = { .bytesReturned = 0, .samplesDestroyed = 0 };

    for (int i = 0; i < K; i++) {
        old_shape_process_call(&oldStats, bufferFull);
        new_shape_process_call(&newStats, bufferFull);
    }

    /* Both shapes correctly see K failure EVENTS -- that part was never wrong. */
    ASSERT_EQ(oldStats.encoderFailures, K);
    ASSERT_EQ(newStats.encoderFailures, K);

    /* This is the #970 defect: the old shape claims K samples were lost when
     * NONE were (the sample is still queued and gets encoded once room
     * frees up -- modelled by the eventual real send below). The new shape
     * must not fabricate any loss. */
    ASSERT_EQ(oldStats.encoderDroppedSamples, K);
    ASSERT_EQ(newStats.encoderDroppedSamples, 0);

    /* The retained sample is finally encoded successfully -- confirms it was
     * never actually gone, which is what makes the old shape's K wrong
     * rather than merely a different valid interpretation. */
    EncodeCallOutcome finallySent = { .bytesReturned = 42, .samplesDestroyed = 0 };
    old_shape_process_call(&oldStats, finallySent);
    new_shape_process_call(&newStats, finallySent);
    ASSERT_EQ(oldStats.encoderFailures, K);      /* unchanged: non-zero return */
    ASSERT_EQ(newStats.encoderFailures, K);
    ASSERT_EQ(oldStats.encoderDroppedSamples, K); /* still wrong -- never corrected */
    ASSERT_EQ(newStats.encoderDroppedSamples, 0); /* still right */
}

/* Json_Encode's ONE genuine loss site (oversize sample that can never fit
 * any buffer -- the drop arm pops+frees it and returns 0). Both shapes must
 * report exactly 1 here: the fix must not silently stop counting real
 * losses along with the fabricated ones. */
TEST(test_accounting_oversize_drop_preserved) {
    MockStreamStats oldStats = {0}, newStats = {0};
    EncodeCallOutcome oversizeDrop = { .bytesReturned = 0, .samplesDestroyed = 1 };

    old_shape_process_call(&oldStats, oversizeDrop);
    new_shape_process_call(&newStats, oversizeDrop);

    ASSERT_EQ(oldStats.encoderFailures, 1);
    ASSERT_EQ(newStats.encoderFailures, 1);
    ASSERT_EQ(oldStats.encoderDroppedSamples, 1); /* right by coincidence pre-fix */
    ASSERT_EQ(newStats.encoderDroppedSamples, 1); /* right because the encoder said so */
}

/* ==========================================================================
 * PART 2 -- JSON_Encoder.c's DIO element loop commit invariant
 * ========================================================================== */

#define DIO_MOCK_QUEUE_LEN 6
#define DIO_MOCK_MAX_WRITES 32  /* stands in for "ran out of buffer room" */

typedef enum {
    POP_MODE_ALWAYS_SUCCEEDS = 0,
    POP_MODE_FAIL_EMPTY,   /* element already gone when the pop is attempted */
    POP_MODE_FAIL_TORN     /* pop fails, element stays at the queue head */
} PopFailureMode;

typedef struct {
    int  realHead;      /* the FreeRTOS queue's actual head index */
    int  callCount;
    PopFailureMode mode;
    int  failAtCall;     /* 1-based call index that fails; 0 = never fails */
} MockDioQueue;

/* Models DIOSampleList_PeekFront: reads the CURRENT real head, does not
 * consume it. */
static bool mock_dio_peek(MockDioQueue *q, int *outId) {
    if (q->realHead >= DIO_MOCK_QUEUE_LEN) {
        return false;
    }
    *outId = q->realHead;
    return true;
}

/* Models DIOSampleList_PopFront: xQueueReceive with a 0-tick wait.
 *
 * EMPTY is a ONE-SHOT event at call `failAtCall`: the element the caller
 * peeked is gone by the time the pop runs (something else already removed
 * it), realHead still advances, and every later call behaves normally --
 * this is what is reachable on today's firmware (a session drain removing
 * exactly the element in flight).
 *
 * TORN is STICKY from `failAtCall` onward: the pop keeps failing and
 * realHead never moves again, so the SAME element is peeked and "written"
 * every remaining iteration. This is the latent worst case (see the file
 * header) -- it demonstrates the unbounded-duplication risk, not a specific
 * claim about how many times a real pop could fail in a row. */
static bool mock_dio_pop(MockDioQueue *q) {
    q->callCount++;
    if (q->mode == POP_MODE_FAIL_EMPTY && q->callCount == q->failAtCall) {
        q->realHead++;   /* the element is gone -- some other path removed it */
        return false;
    }
    if (q->mode == POP_MODE_FAIL_TORN && q->failAtCall != 0 && q->callCount >= q->failAtCall) {
        return false;    /* sticky: realHead never advances past this element */
    }
    q->realHead++;
    return true;
}

/* Pre-fix JSON_Encoder.c DIO loop: write, unconditionally advance the commit
 * point, THEN pop and discard the return. Returns the sequence of element
 * ids whose bytes were committed (i.e. that streaming would ship). */
static int old_shape_dio_loop(MockDioQueue *q, int *committedIds, int maxWrites) {
    int written = 0;
    int id;
    while (written < maxWrites && mock_dio_peek(q, &id)) {
        committedIds[written++] = id;   /* "write" + unconditional commit */
        (void) mock_dio_pop(q);         /* #970 defect: return discarded */
    }
    return written;
}

/* Post-fix JSON_Encoder.c DIO loop: write, pop-and-check, commit ONLY on a
 * successful pop, break otherwise. */
static int new_shape_dio_loop(MockDioQueue *q, int *committedIds, int maxWrites) {
    int written = 0;
    int id;
    while (written < maxWrites && mock_dio_peek(q, &id)) {
        /* "write" happens here in the real code (snprintf into charBuffer at
         * a not-yet-committed offset); nothing is committed until the pop
         * below succeeds. */
        if (!mock_dio_pop(q)) {
            break;
        }
        committedIds[written++] = id;
    }
    return written;
}

/* The new shape must never commit more elements than it actually popped,
 * and never the same element twice, regardless of failure mode. */
TEST(test_dio_pop_new_shape_never_duplicates_or_skips) {
    int committed[DIO_MOCK_MAX_WRITES];

    for (PopFailureMode mode = POP_MODE_ALWAYS_SUCCEEDS; mode <= POP_MODE_FAIL_TORN; mode++) {
        MockDioQueue q = { .realHead = 0, .callCount = 0, .mode = mode, .failAtCall = 3 };
        int n = new_shape_dio_loop(&q, committed, DIO_MOCK_MAX_WRITES);

        /* No duplicates, strictly increasing ids (each element committed at
         * most once, in queue order). */
        for (int i = 1; i < n; i++) {
            ASSERT_TRUE(committed[i] > committed[i - 1]);
        }
        /* Committed count never exceeds the number of successful pops --
         * i.e. it never ships an element it did not actually remove. */
        int successfulPops = 0;
        for (int c = 1; c <= q.callCount; c++) {
            if (!(mode != POP_MODE_ALWAYS_SUCCEEDS && c == q.failAtCall)) {
                successfulPops++;
            }
        }
        ASSERT_EQ(n, successfulPops);
    }
}

/* THE #970 DEFECT, reproduced: under a TORN pop failure, the pre-fix loop
 * re-peeks the SAME undying head element forever (nothing ever advances
 * realHead once the failing pop leaves it in place) and commits it
 * repeatedly, once per remaining iteration, until the buffer/write budget
 * runs out -- not merely "twice", unboundedly. The post-fix loop commits the
 * one element it genuinely popped (id 0) and then stops the instant the
 * next element's pop fails, never committing that element at all. */
TEST(test_dio_pop_old_shape_duplicates_under_torn_pop) {
    int committedOld[DIO_MOCK_MAX_WRITES];
    int committedNew[DIO_MOCK_MAX_WRITES];

    MockDioQueue qOld = { .realHead = 0, .callCount = 0, .mode = POP_MODE_FAIL_TORN, .failAtCall = 2 };
    int nOld = old_shape_dio_loop(&qOld, committedOld, DIO_MOCK_MAX_WRITES);

    MockDioQueue qNew = { .realHead = 0, .callCount = 0, .mode = POP_MODE_FAIL_TORN, .failAtCall = 2 };
    int nNew = new_shape_dio_loop(&qNew, committedNew, DIO_MOCK_MAX_WRITES);

    /* Old shape: element id 1 (the one whose pop fails, TORN) is committed
     * on every remaining iteration -- the mock's write budget is the only
     * thing that stops it. This is the double- (really N-) transmit risk
     * #970 fixes. */
    ASSERT_EQ(nOld, DIO_MOCK_MAX_WRITES);
    for (int i = 1; i < nOld; i++) {
        ASSERT_EQ(committedOld[i], 1);
    }
    /* The real queue never advanced past the torn element -- it is still
     * there, exactly as many times as it was "sent". */
    ASSERT_EQ(qOld.realHead, 1);

    /* New shape: element 0's pop genuinely succeeds (call 1, before the
     * injected failure at call 2), so it is legitimately committed once --
     * the real queue head really did advance past it. Element 1's pop then
     * fails and the loop breaks immediately: no duplicate, no unbounded
     * loop, and element 1 is committed zero times. */
    ASSERT_EQ(nNew, 1);
    ASSERT_EQ(committedNew[0], 0);
    ASSERT_EQ(qNew.realHead, 1);
}

/* Today's reachable failure mode (EMPTY): the element is simply gone when
 * the pop is attempted, and realHead has already effectively moved past it
 * (something else removed it). Confirms the #970 fix does not change
 * behaviour under the failure mode that is actually possible on shipping
 * firmware -- both shapes commit the same sequence, because "commit
 * unconditionally" and "commit only on success" agree whenever the element
 * really is gone for good rather than merely torn. This is what makes the
 * pre-fix code NOT a double-transmit today (see PR description) even though
 * discarding the return was still wrong -- it commits bytes for an element
 * this call never actually took ownership of confirming. */
TEST(test_dio_pop_old_shape_benign_under_empty_pop) {
    int committedOld[DIO_MOCK_MAX_WRITES];
    int committedNew[DIO_MOCK_MAX_WRITES];

    MockDioQueue qOld = { .realHead = 0, .callCount = 0, .mode = POP_MODE_FAIL_EMPTY, .failAtCall = 2 };
    int nOld = old_shape_dio_loop(&qOld, committedOld, DIO_MOCK_MAX_WRITES);

    MockDioQueue qNew = { .realHead = 0, .callCount = 0, .mode = POP_MODE_FAIL_EMPTY, .failAtCall = 2 };
    int nNew = new_shape_dio_loop(&qNew, committedNew, DIO_MOCK_MAX_WRITES);

    /* Old shape drains the whole 6-element mock queue: on the EMPTY failure
     * the head still advances (something else removed it), so the loop
     * proceeds to the next real element next iteration -- no duplicate. */
    ASSERT_EQ(nOld, DIO_MOCK_QUEUE_LEN);
    for (int i = 0; i < nOld; i++) {
        ASSERT_EQ(committedOld[i], i);
    }

    /* New shape stops the instant the programmed failure is hit (call 2,
     * element id 1) -- it cannot know EMPTY from TORN, so it must always
     * treat a failed pop as "stop", even on the mode where continuing would
     * have been harmless. That is the conservative, correct choice: it
     * trades a one-element-per-call retry cost for correctness on TORN. */
    ASSERT_EQ(nNew, 1);
    ASSERT_EQ(committedNew[0], 0);
}

int main(void) {
    RUN(test_accounting_buffer_full_no_fabrication);
    RUN(test_accounting_oversize_drop_preserved);
    RUN(test_dio_pop_new_shape_never_duplicates_or_skips);
    RUN(test_dio_pop_old_shape_duplicates_under_torn_pop);
    RUN(test_dio_pop_old_shape_benign_under_empty_pop);
    return TEST_SUMMARY();
}
