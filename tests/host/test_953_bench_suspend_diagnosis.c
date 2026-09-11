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
 * THE TRANSIENT QUADRANT (review round 1)
 *
 * The table above reads `suspended` as one value, which silently assumes the
 * condition is whatever it is at the timeout. It is not: the wait is five
 * seconds long and a suspension can start AND END inside it. That case is not
 * benign. app_SDCard_GracefulShutdown() stores MODE_NONE over the benchmark's
 * MODE_WRITE arm on the way into APP_SD_STATE_SUSPENDED (app_freertos.c), and
 * nothing restores it when the suspension lifts, while
 * sd_card_manager_IsWriteReady() requires MODE_WRITE -- so the arm is dead for
 * the rest of the wait and the wait can only end at its full 5 s. By then
 * SD_SuspendReasonText() answers NULL (it speaks only while
 * app_SDCard_SpiOwnedByWifi() or SpiBusHealth_IsSdSuspended() holds), so a
 * cascade that samples only at the timeout lands in the card arm -- the same
 * mis-diagnosis #953 is about, in the quadrant a single late sample cannot
 * see.
 *
 * So the wait latches the first reason it observes and the cascade falls back
 * to it when nothing is in force at the timeout. Three shapes are modelled
 * below, not two, and the middle one is what this round changed:
 *
 *   suspension live only DURING the wait | OLD    ROUND-0          ROUND-1
 *   -------------------------------------------------------------------------
 *   reason at poll k, gone by timeout    | card   card  <- the gap  reason
 *
 * Live still beats latched when both exist: a reason in force NOW is what the
 * operator has to clear before a retry can work.
 *
 * FIDELITY -- what the extracted functions are NOT
 *
 * 1. The DECISION CASCADE is modelled, and so is ONE property of the
 *    `readyWait < 500` polling loop ahead of it: which suspend reason the
 *    loop latches. This item used to say the loop was entirely out of scope
 *    because #953 changed none of it. Review round 1 changed it -- see THE
 *    TRANSIENT QUADRANT above -- so the sentence is no longer true and is
 *    replaced rather than left standing. What remains out of scope is
 *    everything the loop does that #953 still does not touch: the vTaskDelay
 *    cadence, the 5 s bound, the `readyWait` count itself, and
 *    IsWriteReady()'s own transitions. The loop model below has one job, to
 *    produce the latched reason a real 500-iteration poll would have
 *    produced, and it is not a timing model.
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
 * ROUND-1: the wait loop's latch, and the cascade that falls back to it.
 *
 * Extracted from the same call site. The real loop is
 *
 *     const char *whySeen = NULL;
 *     while (!IsWriteReady() && readyWait < 500) {
 *         if (StartupDirFull()) break;                  // #690 early-exit
 *         if (whySeen == NULL) whySeen = SuspendReasonText();
 *         vTaskDelay(10 ms); readyWait++;
 *     }
 *
 * and the model keeps exactly three things from it: the #690 break comes
 * FIRST, the sample is taken only while nothing is latched, and the latch is
 * never overwritten. Ticks stand in for iterations; how long a tick is does
 * not enter any assertion (FIDELITY 1).
 * ========================================================================== */
#define BENCH_WAIT_MAX_POLLS 8

typedef struct {
    /* What SD_SuspendReasonText() would answer at each poll; NULL = nothing
     * owns the bus at that instant. The loop only ever compares this against
     * NULL -- see model_wait_latch() -- so WHICH reason appears here changes
     * nothing, which is the point of the bool. */
    const char *sample[BENCH_WAIT_MAX_POLLS];
    /* What sd_card_manager_StartupDirFull() would answer at each poll. The
     * loop breaks on the first true, which is why a reason later in the
     * timeline can go unlatched -- that is the real behaviour, not a
     * shortcut. */
    bool        dirFullAt[BENCH_WAIT_MAX_POLLS];
    size_t      polls;
    /* How many times the LOOP consulted SD_SuspendReasonText(). Bounded by
     * the latch: once it holds a reason the loop stops asking. */
    unsigned    calls;
} BenchWait;

static bool model_wait_latch(BenchWait *w)
{
    bool saw = false;
    size_t i;

    for (i = 0; i < w->polls; i++) {
        if (w->dirFullAt[i]) {
            break;
        }
        if (!saw) {
            w->calls++;
            saw = (w->sample[i] != NULL);
        }
    }
    return saw;
}

/* What the fallback says. It names no owner, and that is the second half of
 * the round-1 change: SD_SuspendReasonText() admits on aggregate ownership and
 * then re-reads the specific causes to choose which to name, so a cause that
 * ends between those reads is reported as a different one. Retaining a STRING
 * would make one such misread stick for the rest of the wait. Retaining the
 * fact cannot be misattributed -- and once the suspension has lifted, "retry"
 * is the whole of the action left anyway. */
static const char *const kReasonTornDownDuringWait =
    "the SD task was suspended during the wait and this benchmark's write "
    "was torn down with it - retry";

/* POST-#953 with the round-1 fallback. Identical to
 * new_bench_not_ready_diagnosis() except that a NULL live reason defers to the
 * latched FACT -- the arms, and their order, are untouched. */
static BenchVerdict latched_bench_not_ready_diagnosis(BenchEnv *env,
                                                      bool sawSuspension)
{
    BenchVerdict v;
    const char *why = mock_suspend_reason_text(env);
    if (why == NULL && sawSuspension) {
        why = kReasonTornDownDuringWait;
    }
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

static void wait_init(BenchWait *w, size_t polls)
{
    size_t i;
    for (i = 0; i < BENCH_WAIT_MAX_POLLS; i++) {
        w->sample[i]    = NULL;
        w->dirFullAt[i] = false;
    }
    w->polls = polls;
    w->calls = 0;
}

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

/* ROUND 1 -- THE TRANSIENT QUADRANT. A suspension that is present at poll 2
 * and gone by the timeout. The arm it destroyed does not come back, so the
 * wait still runs its full length; the only question is what gets blamed.
 *
 * Three shapes, and the middle one is the point: ROUND-0 -- the shape this PR
 * shipped before review -- reaches the SAME card advisory as the pre-#953 code
 * it replaced. Sampling once at the end cannot see a condition that has
 * lifted. So this test is not "the fix still works"; it is the assertion that
 * would have caught the gap.
 *
 * Swept over all three causes for the same reason quadrant (a) is: a fallback
 * wired to one of them would pass a single-case version. */
TEST(a_suspension_that_lifts_before_the_timeout_is_still_named)
{
    static const char *const reasons[] = {
        kReasonWifiStream, kReasonFwUpdate, kReasonQuarantine
    };
    size_t i;

    for (i = 0; i < sizeof(reasons) / sizeof(reasons[0]); i++) {
        BenchEnv oldEnv, round0Env, round1Env;
        BenchVerdict oldV, round0V, round1V;
        BenchWait wait;
        bool latched;

        wait_init(&wait, 6);
        wait.sample[2] = reasons[i];   /* present mid-wait ... */
        wait.sample[3] = reasons[i];
        /* ... and NULL at 4, 5 and at the timeout: it lifted. */
        latched = model_wait_latch(&wait);

        /* The live reading every shape makes at the timeout is NULL. */
        env_init(&oldEnv, false, NULL);
        env_init(&round0Env, false, NULL);
        env_init(&round1Env, false, NULL);

        oldV    = old_bench_not_ready_diagnosis(&oldEnv);
        round0V = new_bench_not_ready_diagnosis(&round0Env);
        round1V = latched_bench_not_ready_diagnosis(&round1Env, latched);

        /* The loop saw it. */
        ASSERT_TRUE(latched);

        /* Pre-#953 blames the card -- expected, it has no suspend arm. */
        ASSERT_EQ(oldV.diag, DIAG_GENERIC_TIMEOUT);
        /* ROUND-0 blames the card TOO. That is the reviewed gap, and this
         * line is what fails if the fallback is removed. */
        ASSERT_EQ(round0V.diag, DIAG_GENERIC_TIMEOUT);
        ASSERT_EQ(round0V.diag, oldV.diag);

        /* ROUND-1 reports the suspension instead of the card. */
        ASSERT_EQ(round1V.diag, DIAG_SUSPEND_REASON);
        ASSERT_TRUE(round1V.diag != round0V.diag);

        /* And it does NOT name an owner. That is not a shortcut: by the
         * timeout there is no owner left, and the label that WOULD be
         * retained is the one SD_SuspendReasonText() can misattribute when a
         * cause ends between its aggregate admission and its specific
         * re-reads. The sweep is what pins it -- the verdict is the same
         * string for all three causes, so nothing about it can be wrong about
         * WHICH one it was. */
        ASSERT_TRUE(round1V.text == kReasonTornDownDuringWait);
        ASSERT_TRUE(round1V.text != reasons[i]);
        if (round1V.text != NULL) {
            ASSERT_TRUE(strcmp(round1V.text, kReasonTornDownDuringWait) == 0);
            ASSERT_EQ(strlen(round1V.text), strlen(kReasonTornDownDuringWait));
        }
    }
}

/* The latch is a FALLBACK, not an override. When something owns the bus at the
 * timeout, that is what the operator must clear before a retry can work, so it
 * wins over whatever was in force earlier in the wait.
 *
 * Delete the `if (why == NULL)` guard -- make the latch unconditional -- and
 * this is the test that fails. */
TEST(a_live_reason_outranks_the_latched_one)
{
    BenchEnv env;
    BenchVerdict v;
    BenchWait wait;
    bool latched;

    wait_init(&wait, 4);
    wait.sample[0] = kReasonWifiStream;      /* earlier owner */
    latched = model_wait_latch(&wait);
    ASSERT_TRUE(latched);

    env_init(&env, false, kReasonQuarantine); /* different owner, live now */
    v = latched_bench_not_ready_diagnosis(&env, latched);

    ASSERT_EQ(v.diag, DIAG_SUSPEND_REASON);
    ASSERT_TRUE(v.text == kReasonQuarantine);
    ASSERT_TRUE(v.text != kReasonTornDownDuringWait);
    /* Still exactly one live sample in the cascade -- the round-0 property
     * this round must not have broken. */
    ASSERT_EQ(env.suspendReasonCalls, 1);
}

/* The latch records a FACT, not a label, and then stops asking.
 *
 * The timeline hands it three DIFFERENT causes in sequence -- the shape that
 * would expose a retained string as the wrong one -- and the verdict is the
 * same owner-free message it would be for any of them.
 *
 * Measured, not asserted: a mutant that latches
 * `whySeen = SD_SuspendReasonText()` and reports it fails exactly TWO tests,
 * this one and the transient sweep above, and passes the other eight. An
 * earlier draft of this comment claimed it failed only this one; it does not,
 * because the sweep pins the reported text as well as the arm.
 *
 * The call count is the second half: a loop that kept polling after latching
 * would pay up to 500 calls for an answer it already had. */
TEST(the_latch_records_a_fact_not_a_label_and_then_stops_asking)
{
    BenchEnv env;
    BenchVerdict v;
    BenchWait wait;
    bool latched;

    wait_init(&wait, 6);
    wait.sample[1] = kReasonFwUpdate;     /* first ... */
    wait.sample[2] = kReasonWifiStream;   /* ... then a different owner ... */
    wait.sample[3] = kReasonQuarantine;   /* ... then a third. */
    latched = model_wait_latch(&wait);

    ASSERT_TRUE(latched);
    /* Polls 0 and 1 asked; from 2 on the latch held, so nothing else did. */
    ASSERT_EQ(wait.calls, 2);

    env_init(&env, false, NULL);
    v = latched_bench_not_ready_diagnosis(&env, latched);
    ASSERT_EQ(v.diag, DIAG_SUSPEND_REASON);
    ASSERT_TRUE(v.text == kReasonTornDownDuringWait);
    /* None of the three causes is named, so none can be named wrongly. */
    ASSERT_TRUE(v.text != kReasonFwUpdate);
    ASSERT_TRUE(v.text != kReasonWifiStream);
    ASSERT_TRUE(v.text != kReasonQuarantine);
}

/* The ordering survives the fallback. A recorded #689/#690 refusal still
 * outranks a latched reason exactly as it outranks a live one -- otherwise
 * this round would have done by the back door what the ticket's proposed
 * ordering was rejected for doing at the front.
 *
 * Second half: the #690 early-exit `break` comes BEFORE the sample, so a
 * refusal recorded at poll 1 leaves nothing latched even though the timeline
 * carries a reason later on. That is the real loop's behaviour and it is
 * asserted, not assumed. */
TEST(a_recorded_refusal_still_outranks_the_latched_reason)
{
    BenchEnv env;
    BenchVerdict v;
    BenchWait wait;
    bool latched;

    /* (i) suspension seen in the wait, dirFull true at the timeout. */
    wait_init(&wait, 5);
    wait.sample[0] = kReasonWifiStream;
    latched = model_wait_latch(&wait);
    ASSERT_TRUE(latched);

    env_init(&env, true, NULL);
    v = latched_bench_not_ready_diagnosis(&env, latched);
    ASSERT_EQ(v.diag, DIAG_STARTUP_DIR_FULL);
    ASSERT_TRUE(v.text == kRefuseBucketsExhausted);
    ASSERT_TRUE(v.text != kReasonTornDownDuringWait);

    /* (ii) the refusal lands first, so the loop breaks and latches nothing. */
    wait_init(&wait, 6);
    wait.dirFullAt[1] = true;
    wait.sample[3]    = kReasonWifiStream;
    latched = model_wait_latch(&wait);
    ASSERT_TRUE(!latched);
    ASSERT_EQ(wait.calls, 1);   /* poll 0 only; poll 1 broke out */

    env_init(&env, true, NULL);
    v = latched_bench_not_ready_diagnosis(&env, latched);
    ASSERT_EQ(v.diag, DIAG_STARTUP_DIR_FULL);
}

/* The card diagnosis has to survive the fallback too. Nothing owned the bus at
 * any point in the wait and nothing was recorded: the SD task was running, the
 * open still never completed, and that IS the "reads/LIST work but writes
 * hang" signature the wiki page is about.
 *
 * This is the guard against the lazy version of this round -- latching
 * something, anything, so the transient test passes. */
TEST(a_wait_with_no_suspension_at_all_still_reports_the_card)
{
    BenchEnv env;
    BenchVerdict v;
    BenchWait wait;
    bool latched;

    wait_init(&wait, BENCH_WAIT_MAX_POLLS);
    latched = model_wait_latch(&wait);

    ASSERT_TRUE(!latched);
    ASSERT_EQ(wait.calls, BENCH_WAIT_MAX_POLLS);  /* never latched, so it kept asking */

    env_init(&env, false, NULL);
    v = latched_bench_not_ready_diagnosis(&env, latched);
    ASSERT_EQ(v.diag, DIAG_GENERIC_TIMEOUT);
    ASSERT_TRUE(v.text == NULL);
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
    RUN(a_suspension_that_lifts_before_the_timeout_is_still_named);
    RUN(a_live_reason_outranks_the_latched_one);
    RUN(the_latch_records_a_fact_not_a_label_and_then_stops_asking);
    RUN(a_recorded_refusal_still_outranks_the_latched_reason);
    RUN(a_wait_with_no_suspension_at_all_still_reports_the_card);
    return TEST_SUMMARY();
}
