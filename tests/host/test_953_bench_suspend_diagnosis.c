/* ==========================================================================
 * test_953_bench_suspend_diagnosis.c -- issue #953
 *
 * WHAT IS UNDER TEST
 *
 * SYST:STOR:SD:BENCHmark (SCPI_StorageSDBenchmark, SCPIStorageSD.c) arms an SD
 * write and then waits for the SD task to open the file. When that wait ends
 * with the file still not ready, one branch has to say WHY. Until #953 it had
 * two arms (SCPIStorageSD.c, pre-fix):
 *
 *     if (!sd_card_manager_IsWriteReady()) {
 *         if (sd_card_manager_StartupDirFull()) {
 *             LOG_E("SD:BENCH refused (#689): %s\r\n",
 *                   sd_card_manager_WriteRefuseText());
 *         } else {
 *             LOG_E("SD:BENCH - File not ready after timeout\r\n");
 *             LOG_E("SD:BENCH - ... the card is likely SPI-mode incompatible"
 *                   " (wiki: SD-Card-Compatibility)\r\n");
 *         }
 *         ...
 *     }
 *
 * The arm SUCCEEDING and the SD task being suspended AFTERWARDS is a reachable
 * combination: the #589 gate app_SDCardTask uses (app_SDCard_SpiOwnedByWifi(),
 * a WiFi FW update, or the #925 jam quarantine) can raise at any instant, and
 * once it does that task pumps neither DRV_SDSPI_Tasks() nor
 * sd_card_manager_ProcessState(). IsWriteReady() then can never become true,
 * the wait runs to its full 5 s, and the pre-fix cascade blames the CARD --
 * "likely SPI-mode incompatible" -- for a task that simply stopped running.
 * The arm-time twin of this (a suspend already present when the arm is
 * attempted) was fixed by #936/#955 and is a different branch, a few dozen
 * lines above; this is the one after it.
 *
 * The fix adds a third arm, and the ORDER of the three is the substance of it:
 *
 *     const char *why = SD_SuspendReasonText();
 *     if (sd_card_manager_StartupDirFull())  -> #689/#690 refuse text
 *     else if (why != NULL)                  -> the live suspend reason
 *     else                                   -> the card diagnosis
 *
 * StartupDirFull stays FIRST, which is NOT what #953's ticket proposed. It is
 * a recorded verdict and necessarily this request's own: the callback clears
 * it synchronously just before arming, and its only writer is the SD task's
 * OPEN_FILE refusal (sd_card_manager.c:2070), which cannot run while that task
 * is suspended. So a `true` proves the SD task ran, attempted the open and
 * refused it for a named reason -- strictly more than "something owns SPI4
 * right now". Testing the suspend first would have re-diagnosed that case
 * whenever a suspend merely landed AFTER the refusal, sending the operator to
 * stop streaming for a card that will refuse the open identically once
 * streaming stops: the same mis-diagnosis #953 is about, pointed the other
 * way, and a silent narrowing of #690. See CASES below.
 *
 * HOW IT IS TESTED
 *
 * SCPIStorageSD.c is not host-compilable -- libscpi, FreeRTOS and the whole SD
 * manager -- so this file follows test_943_bench_stall_bound.c: it
 * re-implements the two cascade SHAPES (pre-fix and post-fix) as pure
 * functions over injected values, and compares their verdicts on identical
 * inputs. What is proven is the DECISION ALGEBRA, which is all #953 changes.
 *
 * A bench test is not the alternative here, it is not available. The
 * daqifi-python-test-suite's test_589_sd_suspended_contract.py already records
 * -- for the identical race shape in the sibling ticket #936 -- that "arm
 * succeeds, then a suspend lands inside the wait window before the file opens"
 * is not reproducible on demand from a test script: it needs a
 * higher-priority event to land inside a window a test cannot aim at. #936's
 * own fix was verified by source reading for that reason, and the #953 ticket
 * header says `Bench: none`. A deterministic host test of the decision is the
 * strictly stronger thing available.
 *
 * CASES -- the four quadrants of (suspended? x startupDirFull?)
 *
 *   suspended  dirFull | OLD              NEW              agree?
 *   ----------------------------------------------------------------
 *   true       false   | GENERIC_TIMEOUT  SUSPEND_REASON   NO  <- #953
 *   true       true    | STARTUP_DIR_FULL STARTUP_DIR_FULL  yes <- ordering
 *   false      true    | STARTUP_DIR_FULL STARTUP_DIR_FULL  yes <- #690 guard
 *   false      false   | GENERIC_TIMEOUT  GENERIC_TIMEOUT   yes <- card guard
 *
 * Exactly one quadrant moves. That is the property the chosen ordering buys
 * and the ticket's proposed ordering would not have: under "suspend first",
 * row 2 would also move, taking #690's recorded refusal with it.
 *
 * THE TRANSIENT QUADRANT, AND WHY IT IS STILL OPEN
 *
 * The table above reads `suspended` as one value, which assumes the condition
 * is whatever it is at the timeout. It is not: the wait is five seconds long
 * and a suspension can start AND END inside it. That case is not benign.
 * app_SDCard_GracefulShutdown() stores MODE_NONE over the benchmark's
 * MODE_WRITE arm on the way into APP_SD_STATE_SUSPENDED (app_freertos.c),
 * nothing restores it when the suspension lifts, and
 * sd_card_manager_IsWriteReady() requires MODE_WRITE -- so the arm is dead for
 * the rest of the wait and the cascade lands in the card arm with
 * SD_SuspendReasonText() already back to NULL.
 *
 * This PR tried three times to close that by latching what the poll observed,
 * and each round found the same defect from a new angle: an ambient condition
 * is not evidence about THIS request. A live WiFi owner can be sampled and then
 * vanish without the SD task ever suspending -- the streaming task's
 * dead-transport auto-stop runs at priority 6 and beats the SD task at 5 to its
 * own ownership check -- and a latch set from that blames a suspension for a
 * genuine card fault. That is worse than the advisory it replaces, because it
 * is confidently wrong.
 *
 * So the latch is NOT in this PR. The quadrant stays open, with the right fix
 * described in #988: latch that this arm's MODE_WRITE was torn down, which is
 * true of every teardown cause including the power-state path that bypasses
 * APP_SD_STATE_SUSPENDED entirely. Half-done was the worse option.
 *
 * FIDELITY -- what the extracted functions are NOT
 *
 * 1. Only the DECISION CASCADE after the wait loop is modelled. The
 *    `readyWait < 500` polling loop before it, its #690 early-exit `break`,
 *    the vTaskDelay cadence and the 5 s bound are all out of scope -- #953
 *    changes none of them, and the ticket puts them out of scope explicitly.
 *    (An intermediate revision of this file modelled the loop's latch as
 *    well. That mechanism was removed; see THE TRANSIENT QUADRANT above. If
 *    #988 reinstates a latch, this item moves again.)
 * 2. IsWriteReady() is not a parameter: the branch under test is the body of
 *    `if (!sd_card_manager_IsWriteReady())`, so within it that predicate is
 *    false by construction. Modelling it would only add a quadrant in which
 *    neither shape is reached.
 * 3. The tail shared by all three arms -- SCPI_ErrorPush, the
 *    `mode = MODE_NONE` store, sd_card_manager_UpdateSettings() and the goto
 *    -- is unchanged by #953 and runs whichever arm fires, so it is not
 *    modelled. (It is also not a no-op under a suspend: the #589 arm gate is
 *    `mode != MODE_NONE && suspended` and NONE is deliberately exempt, so the
 *    teardown proceeds. See the comment at that call site.)
 * 4. The log TEXT of each arm is not asserted verbatim, only which arm fires
 *    and that the suspend arm passes its reason string through unaltered --
 *    the real LOG_E interpolates `why` directly, so a dropped or truncated
 *    pointer is the failure mode worth pinning.
 * 5. SD_SuspendReasonText()'s own three-way choice (quarantine / FW update /
 *    WiFi streaming) is NOT retested here -- it is unchanged by #953 and its
 *    output is opaque to this cascade, which only tests it against NULL. The
 *    real strings are used as inputs so a caller-visible truncation would
 *    still show up.
 * ========================================================================== */

#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "test_framework.h"

/* ==========================================================================
 * The three diagnoses the branch can reach.
 * ========================================================================== */
typedef enum {
    /* LOG_E("SD:BENCH refused (#689): %s", WriteRefuseText()) -- #689/#690. */
    DIAG_STARTUP_DIR_FULL = 0,
    /* LOG_E("SD:BENCH - could not complete the arm: %s", why) -- #953, new. */
    DIAG_SUSPEND_REASON,
    /* "File not ready after timeout" + the SPI-mode-incompatible card hint. */
    DIAG_GENERIC_TIMEOUT
} BenchDiag;

/* What the branch decided, plus the string it would interpolate. `text` is the
 * pointer the arm passes to LOG_E: WriteRefuseText() for DIR_FULL, `why` for
 * SUSPEND_REASON, NULL for the generic arm (which has no %s). */
typedef struct {
    BenchDiag   diag;
    const char *text;
} BenchVerdict;

/* ==========================================================================
 * Injected environment. Stands in for the three firmware calls the cascade
 * makes; IsWriteReady() is absent on purpose (see FIDELITY 2).
 * ========================================================================== */
typedef struct {
    /* sd_card_manager_StartupDirFull() */
    bool        startupDirFull;
    /* SD_SuspendReasonText() -- NULL means "not suspended". */
    const char *suspendReason;
    /* sd_card_manager_WriteRefuseText() -- never NULL in the firmware; its
     * SD_REFUSE_NONE arm returns "no refusal recorded". */
    const char *writeRefuseText;
    /* How many times the cascade consulted SD_SuspendReasonText(). The real
     * code samples it ONCE so the arm that logs cannot print a reason other
     * than the one that steered the branch. */
    unsigned    suspendReasonCalls;
} BenchEnv;

static bool mock_startup_dir_full(BenchEnv *env)
{
    return env->startupDirFull;
}

static const char *mock_suspend_reason_text(BenchEnv *env)
{
    env->suspendReasonCalls++;
    return env->suspendReason;
}

static const char *mock_write_refuse_text(BenchEnv *env)
{
    return env->writeRefuseText;
}

/* ==========================================================================
 * The two cascade shapes, extracted from SCPIStorageSD.c with the three
 * firmware calls replaced by their mocks. Nothing else changed: same tests,
 * same order, same arms.
 * ========================================================================== */

/* PRE-#953: two arms. A suspend is invisible to it, so a suspended SD task
 * lands in the `else` and is reported as a suspect card. */
static BenchVerdict old_bench_not_ready_diagnosis(BenchEnv *env)
{
    BenchVerdict v;
    if (mock_startup_dir_full(env)) {
        v.diag = DIAG_STARTUP_DIR_FULL;
        v.text = mock_write_refuse_text(env);
    } else {
        v.diag = DIAG_GENERIC_TIMEOUT;
        v.text = NULL;
    }
    return v;
}

/* POST-#953: three arms. `why` is sampled once, ahead of the cascade; the
 * recorded #689/#690 verdict still wins over the live suspend condition. */
static BenchVerdict new_bench_not_ready_diagnosis(BenchEnv *env)
{
    BenchVerdict v;
    const char *why = mock_suspend_reason_text(env);
    if (mock_startup_dir_full(env)) {
        v.diag = DIAG_STARTUP_DIR_FULL;
        v.text = mock_write_refuse_text(env);
    } else if (why != NULL) {
        v.diag = DIAG_SUSPEND_REASON;
        v.text = why;
    } else {
        v.diag = DIAG_GENERIC_TIMEOUT;
        v.text = NULL;
    }
    return v;
}

/* ==========================================================================
 * What the arm actually prints, and how much of it survives.
 * ========================================================================== */

/* The prefix the mid-wait arm interpolates a reason into (SCPIStorageSD.c).
 * The Makefile fails the build if this literal is no longer in the source, so
 * the length measured below is the length of the line the DEVICE formats. */
static const char *const kMidWaitLogPrefix =
    "SD:BENCH - could not complete the arm: ";

/* What Logger will actually keep. Logger.c formats with
 *
 *     vsnprintf(buffer, LOG_MESSAGE_SIZE - 2, format, args);
 *
 * and vsnprintf writes at most n-1 characters plus a NUL, so a formatted line
 * longer than LOG_MESSAGE_SIZE - 3 is cut -- silently, and on the device only,
 * where no assertion that compares a constant against itself can see it.
 * FW_LOG_MESSAGE_SIZE is grepped out of Logger.h by the Makefile rather than
 * copied here, so a change to the buffer re-derives this instead of quietly
 * invalidating it. */
#ifndef FW_LOG_MESSAGE_SIZE
#error "FW_LOG_MESSAGE_SIZE must come from the Makefile (grepped from Logger.h)"
#endif
#define LOG_LINE_MAX  (FW_LOG_MESSAGE_SIZE - 3)


/* ==========================================================================
 * Fixtures
 *
 * The suspend strings are SD_SuspendReasonText()'s real returns
 * (SCPIStorageSD.c). They are inputs only -- this file does not test which of
 * the three that function picks (FIDELITY 5) -- but using the real text means
 * a pass-through that truncated or rewrote the reason would be visible.
 * ========================================================================== */
static const char *const kReasonWifiStream =
    "WiFi streaming owns SPI4 - SYST:STR:STOP first";
static const char *const kReasonFwUpdate =
    "a WiFi firmware update owns SPI4 - retry when it completes";
static const char *const kReasonQuarantine =
    "SD quarantined after a bus jam - reseat or remove the card, "
    "then SYST:STOR:SD:ENAble 1 to retry";

/* sd_card_manager_WriteRefuseText()'s SD_REFUSE_BUCKETS_EXHAUSTED arm. */
static const char *const kRefuseBucketsExhausted =
    "every directory bucket is full - use a different directory "
    "or clear the card";

static void env_init(BenchEnv *env, bool dirFull, const char *suspendReason)
{
    env->startupDirFull     = dirFull;
    env->suspendReason      = suspendReason;
    env->writeRefuseText    = kRefuseBucketsExhausted;
    env->suspendReasonCalls = 0;
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* Quadrant (a) -- THE BUG. The arm succeeded, then a suspend landed inside the
 * wait window, so the SD task never reached OPEN_FILE and never recorded
 * anything: startupDirFull is false because the callback cleared it just
 * before arming and the only thing that could set it is the task that stopped
 * running.
 *
 * OLD reports the card as SPI-mode incompatible -- a wrong, expensive
 * diagnosis that sends the operator to swap hardware. NEW reports the reason
 * the bus is unavailable. This is the assertion that screams if the fix is
 * reverted.
 *
 * Swept over all three suspend causes: nothing about the fix is specific to
 * WiFi streaming, and a shape that only handled one would pass a single-case
 * version of this. */
TEST(suspend_during_wait_is_diagnosed_not_blamed_on_the_card)
{
    static const char *const reasons[] = {
        kReasonWifiStream, kReasonFwUpdate, kReasonQuarantine
    };
    size_t i;

    for (i = 0; i < sizeof(reasons) / sizeof(reasons[0]); i++) {
        BenchEnv oldEnv, newEnv;
        BenchVerdict oldV, newV;

        env_init(&oldEnv, false, reasons[i]);
        env_init(&newEnv, false, reasons[i]);

        oldV = old_bench_not_ready_diagnosis(&oldEnv);
        newV = new_bench_not_ready_diagnosis(&newEnv);

        /* The defect: the shipped code blames the card. */
        ASSERT_EQ(oldV.diag, DIAG_GENERIC_TIMEOUT);
        ASSERT_TRUE(oldV.text == NULL);

        /* The fix: it names the owner of SPI4 instead. */
        ASSERT_EQ(newV.diag, DIAG_SUSPEND_REASON);
        ASSERT_TRUE(newV.diag != oldV.diag);

        /* The reason must reach LOG_E intact -- same pointer, and byte-equal
         * over its WHOLE length. Pointer identity alone would pass a shape
         * that logged a different string; strcmp alone would pass one that
         * copied into a buffer and truncated at some length matching a
         * prefix. Both, and the length, pin it.
         *
         * The content checks sit behind a NULL guard rather than inside the
         * assertions: a mutant that reverts the fix outright reaches here with
         * text == NULL, and this suite must REPORT that (the ASSERT_TRUE above
         * does) rather than segfault in strlen and lose its own summary. */
        ASSERT_TRUE(newV.text == reasons[i]);
        if (newV.text != NULL) {
            ASSERT_TRUE(strcmp(newV.text, reasons[i]) == 0);
            ASSERT_EQ(strlen(newV.text), strlen(reasons[i]));
        }
    }
}

/* Quadrant (b) -- THE ORDERING DECISION, and the only reason this case exists.
 *
 * Both conditions true: the SD task DID run, DID attempt the open and refused
 * it for a #689 reason (that is the only way startupDirFull becomes true), and
 * a suspend landed afterwards. The refusal is the causally-prior, recorded,
 * request-specific fact; the suspend is a later ambient condition. Report the
 * refusal.
 *
 * Under the ticket's proposed "suspend first" ordering this row would return
 * DIAG_SUSPEND_REASON, telling the operator to stop streaming for a card that
 * will refuse the open identically once streaming stops -- and silently
 * narrowing #690 in the process. Flip the two arms in
 * new_bench_not_ready_diagnosis and this test is what fails.
 *
 * It also pins the corollary that makes the ordering safe to state: OLD and
 * NEW agree here, so #953 leaves this quadrant exactly as #690 left it. */
TEST(recorded_dir_full_refusal_outranks_a_later_suspend)
{
    static const char *const reasons[] = {
        kReasonWifiStream, kReasonFwUpdate, kReasonQuarantine
    };
    size_t i;

    for (i = 0; i < sizeof(reasons) / sizeof(reasons[0]); i++) {
        BenchEnv oldEnv, newEnv;
        BenchVerdict oldV, newV;

        env_init(&oldEnv, true, reasons[i]);
        env_init(&newEnv, true, reasons[i]);

        oldV = old_bench_not_ready_diagnosis(&oldEnv);
        newV = new_bench_not_ready_diagnosis(&newEnv);

        ASSERT_EQ(newV.diag, DIAG_STARTUP_DIR_FULL);
        ASSERT_TRUE(newV.diag != DIAG_SUSPEND_REASON);
        ASSERT_EQ(oldV.diag, newV.diag);

        /* And it carries the #689 refuse text, not the suspend reason. */
        ASSERT_TRUE(newV.text == kRefuseBucketsExhausted);
        ASSERT_TRUE(newV.text != reasons[i]);
        ASSERT_TRUE(oldV.text == newV.text);
    }
}

/* Quadrant (c) -- regression guard on #690/#689. No suspend, a recorded
 * refusal: unchanged, and it must stay that way. This is the case #690 was
 * filed for, and #953 must not have moved it. */
TEST(dir_full_without_suspend_is_unchanged_by_953)
{
    BenchEnv oldEnv, newEnv;
    BenchVerdict oldV, newV;

    env_init(&oldEnv, true, NULL);
    env_init(&newEnv, true, NULL);

    oldV = old_bench_not_ready_diagnosis(&oldEnv);
    newV = new_bench_not_ready_diagnosis(&newEnv);

    ASSERT_EQ(oldV.diag, DIAG_STARTUP_DIR_FULL);
    ASSERT_EQ(newV.diag, DIAG_STARTUP_DIR_FULL);
    ASSERT_TRUE(oldV.text == newV.text);
    ASSERT_TRUE(newV.text == kRefuseBucketsExhausted);
}

/* Quadrant (d) -- regression guard on the genuine card diagnosis. No suspend,
 * no recorded refusal: the SD task was running, recorded nothing, and the open
 * still never completed. That IS the "reads/LIST work but writes hang"
 * signature the wiki page is about, and #953 must not have swallowed it.
 *
 * Together with (a) this is what stops the lazy mutation "always take the new
 * suspend arm", which would satisfy (a) while destroying the one diagnosis
 * this branch got right all along. */
TEST(no_suspend_no_dir_full_still_reports_the_card)
{
    BenchEnv oldEnv, newEnv;
    BenchVerdict oldV, newV;

    env_init(&oldEnv, false, NULL);
    env_init(&newEnv, false, NULL);

    oldV = old_bench_not_ready_diagnosis(&oldEnv);
    newV = new_bench_not_ready_diagnosis(&newEnv);

    ASSERT_EQ(oldV.diag, DIAG_GENERIC_TIMEOUT);
    ASSERT_EQ(newV.diag, DIAG_GENERIC_TIMEOUT);
    ASSERT_TRUE(oldV.text == NULL);
    ASSERT_TRUE(newV.text == NULL);
}

/* The whole truth table in one place, asserted as a table rather than as four
 * hand-written cases -- so "exactly one quadrant moves" is a checked property
 * of the pair of shapes and not a claim in a comment.
 *
 * It also pins the single-sample rule: the real code calls
 * SD_SuspendReasonText() once, ahead of the cascade. Calling it again inside
 * the logging arm would let the printed reason differ from the one that chose
 * the branch (the owner can change between the two reads), and would make the
 * suspend arm's message a second, independently-raced observation. */
TEST(exactly_one_quadrant_moves_and_why_is_sampled_once)
{
    static const struct {
        bool        dirFull;
        const char *suspend;
        BenchDiag   expectOld;
        BenchDiag   expectNew;
        bool        expectAgree;
    } table[] = {
        /* (a) */ { false, kReasonWifiStream, DIAG_GENERIC_TIMEOUT,
                    DIAG_SUSPEND_REASON,   false },
        /* (b) */ { true,  kReasonWifiStream, DIAG_STARTUP_DIR_FULL,
                    DIAG_STARTUP_DIR_FULL, true  },
        /* (c) */ { true,  NULL,             DIAG_STARTUP_DIR_FULL,
                    DIAG_STARTUP_DIR_FULL, true  },
        /* (d) */ { false, NULL,             DIAG_GENERIC_TIMEOUT,
                    DIAG_GENERIC_TIMEOUT,  true  },
    };
    size_t i;
    unsigned moved = 0;

    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        BenchEnv oldEnv, newEnv;
        BenchVerdict oldV, newV;

        env_init(&oldEnv, table[i].dirFull, table[i].suspend);
        env_init(&newEnv, table[i].dirFull, table[i].suspend);

        oldV = old_bench_not_ready_diagnosis(&oldEnv);
        newV = new_bench_not_ready_diagnosis(&newEnv);

        ASSERT_EQ(oldV.diag, table[i].expectOld);
        ASSERT_EQ(newV.diag, table[i].expectNew);
        ASSERT_EQ(oldV.diag == newV.diag, table[i].expectAgree);
        if (oldV.diag != newV.diag) {
            moved++;
        }

        /* Sampled exactly once per evaluation, on every path -- including the
         * two where the value is then discarded because the recorded #689
         * verdict wins. */
        ASSERT_EQ(newEnv.suspendReasonCalls, 1);
        /* And the old shape never consulted it at all, which is the defect. */
        ASSERT_EQ(oldEnv.suspendReasonCalls, 0);
    }

    /* The headline property: #953 is surgical. */
    ASSERT_EQ(moved, 1);
}

/* THE MESSAGE HAS TO SURVIVE THE LOGGER, and this is the only place that can
 * say so. Every other test in this file compares a constant against itself,
 * which is true of the string and says nothing about the line the operator
 * reads: Logger cuts at LOG_MESSAGE_SIZE - 3, silently, and on the device only.
 *
 * This arm interpolates whatever SD_SuspendReasonText() returns, so the three
 * reason strings are what must fit. Two do. The QUARANTINE one does not -- 136
 * bytes here, and 137 through the arm-refusal twin that has shipped since #936,
 * losing "... then SYST:STOR:SD:ENAble 1 to retry", which is the command that
 * clears a quarantine and the most actionable string in the #589 family.
 *
 * That is pre-existing and out of this PR's reach: four test-suite scripts
 * match on that string, so shortening it is a cross-repo change. It is filed
 * as #986 and asserted here as STILL OVER -- deliberately, so that the fix for
 * #986 fails this build and whoever writes it promotes the string into the
 * fitting set above rather than leaving a stale carve-out behind. An assertion
 * that a defect still exists is only honest while its ticket is open, and this
 * is how it gets closed.
 *
 * The case that put this test here was a 98-character fallback message this PR
 * carried for one round: with the prefix and the CRLF that was 139 bytes
 * against 125, cut mid-word so it lost the "- retry" that was its entire point.
 * The audit caught it; the mechanism that message belonged to was then removed
 * (see the file header), but the measurement is what should have existed all
 * along and it stays. */
TEST(every_reason_this_arm_can_print_survives_the_logger)
{
    static const char *const liveReasons[] = {
        kReasonWifiStream, kReasonFwUpdate
    };
    size_t i;
    size_t prefix = strlen(kMidWaitLogPrefix);

    /* +2 for the CRLF the format string carries. */
    for (i = 0; i < sizeof(liveReasons) / sizeof(liveReasons[0]); i++) {
        ASSERT_TRUE(prefix + strlen(liveReasons[i]) + 2 <= LOG_LINE_MAX);
    }

    /* #986, asserted as still open. See the comment above before "fixing"
     * this line by deleting it. */
    ASSERT_TRUE(prefix + strlen(kReasonQuarantine) + 2 > LOG_LINE_MAX);
}

int main(void)
{
    printf("#953 -- SD:BENCHmark file-not-ready diagnosis (extracted cascade)\n");
    printf("---------------------------------------------\n");
    RUN(suspend_during_wait_is_diagnosed_not_blamed_on_the_card);
    RUN(recorded_dir_full_refusal_outranks_a_later_suspend);
    RUN(dir_full_without_suspend_is_unchanged_by_953);
    RUN(no_suspend_no_dir_full_still_reports_the_card);
    RUN(exactly_one_quadrant_moves_and_why_is_sampled_once);
    RUN(every_reason_this_arm_can_print_survives_the_logger);
    return TEST_SUMMARY();
}
