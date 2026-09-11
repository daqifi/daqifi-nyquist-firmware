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
 * THE LENGTH GUARD (#1001) -- WHAT IT MEASURES, AND WHY BY GENERATION
 *
 * #986 shortened the quarantine reason because Logger cuts a formatted line
 * and it did not fit: the line arrived cut at "then SYST:STOR:SD:ENAb", losing
 * the command that clears a quarantine from the message whose entire job is to
 * name it. Nothing then stopped that recurring, and THREE attempts at a guard
 * were each defeated in review of PR #983 before the mechanism was withdrawn
 * and the design filed as #1001:
 *
 *   a grep for the reason's own words   -- the words that matter are a
 *                                          substring of the longer string they
 *                                          replaced AND of the comment
 *                                          explaining the replacement, so it
 *                                          passed for exactly the state it was
 *                                          meant to catch
 *   a sha256 of the function's text     -- the pipeline collapsed whitespace,
 *                                          so a reason respaced inside its own
 *                                          literal left the hash unchanged
 *   copies measured against a -D limit  -- the copies can drift from the
 *                                          firmware, which is what the two
 *                                          above existed to prevent
 *
 * Each fix drew the next finding because all three pinned something ABOUT the
 * strings instead of measuring the strings. So the guard measures the real
 * ones. gen_1001_suspend_reason_fixture.py brace-matches
 * SD_SuspendReasonText()'s body in SCPIStorageSD.c and decodes every literal
 * it returns (adjacent-literal concatenation included -- the quarantine reason
 * is spelled across two source lines and the compiler joins them with no extra
 * characters), together with the four LOG_E format strings that interpolate
 * those returns, the longest command mnemonic that can reach each one, and the
 * character ceiling parsed out of Logger.h and Logger.c. It writes
 * gen_1001_suspend_reasons.h, which this file includes and does arithmetic on.
 *
 * Nothing below is a copy, which retires all three defeat classes at once:
 * there are no words to grep for, no hash for a respacing to slip past, and
 * the three reason fixtures the older cases in this file used to declare for
 * themselves ARE the generated strings now (see Fixtures), so they cannot
 * drift from the firmware either. Respacing a reason inside its own literal is
 * measured, not special-cased: the generator decodes the literal and this file
 * takes strlen() of the result, so a changed byte count changes the
 * measurement and an unchanged one provably cannot matter.
 *
 * THE ARITHMETIC, re-derived from Logger.c (LogMessageFormatImpl)
 *
 *     char buffer[LOG_MESSAGE_SIZE];                                 // 128
 *     size = vsnprintf(buffer, LOG_MESSAGE_SIZE - 2, format, args);
 *     size = min((LOG_MESSAGE_SIZE - 3), size);
 *
 * vsnprintf writes at most (LOG_MESSAGE_SIZE - 2) - 1 = 125 characters before
 * its NUL and RETURNS the length it would have written; the clamp then caps
 * what is kept at LOG_MESSAGE_SIZE - 3 = 125. The two agree today, and the
 * generator emits the min of them so a change to either is followed rather
 * than assumed away. A formatted line of L characters therefore survives
 * intact iff L <= 125, and the assertion each (reason x call site) pair has to
 * satisfy is
 *
 *     prefixLen + strlen(reason) <= GEN_1001_LOG_SURVIVING_CHARS
 *
 * where prefixLen is the format's own characters -- its trailing CRLF included
 * -- with the mnemonic substituted and the reason removed.
 *
 * That is the STRICT bound and it is chosen over a two-character-looser one
 * deliberately. At L = 126 or 127 the message TEXT still survives: what gets
 * cut is the format's own CRLF, the newline fixup under the clamp then does
 * not find a "\r\n" at the end and appends one, and the line arrives complete
 * but carrying a stray CR (nothing overflows either -- 127 < LOG_MESSAGE_SIZE,
 * so LogMessageAdd still accepts it). The strict bound is asserted because it
 * is the only one under which the emitted line is exactly the line the format
 * describes, and the two characters of slack buy nothing anyone wants.
 *
 * WHICH CALLERS ARE COVERED -- and the one that cannot be
 *
 * All four LOG_E sites in SCPIStorageSD.c, which is every caller in this file.
 * SD_ArmOrRefuseWithCleanup is the BINDING one: six mnemonics reach it through
 * the SD_ArmOrRefuse wrapper and the longest ("DELete"/"FORmat") leave 82
 * characters, less than SD_RefuseIfSuspended's 86 with "BENCHmark" and less
 * than either fixed-"BENCH" site in SCPI_StorageSDBenchmark (83 and 84).
 *
 * SCPIInterface.c interpolates these same returns at three more sites and is
 * NOT read by this guard (#1001 excludes it; it is also under concurrent edit).
 * Two of those three are looser than the binding site measured here -- 83
 * characters each -- so this file's budget already covers them. The third
 * cannot be covered by any reason string: SCPI_StartStreamingClaimed's
 * arm-raced refusal has an 88-character prefix plus CRLF, leaving 35, so all
 * three reasons are cut there, the shortest included. Shortening reasons
 * cannot fix it; that prefix has to shorten. It is #1000, and it is left out
 * rather than folded in so that a budget nothing can satisfy does not sit in
 * this table looking actionable.
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

/* #1001. GENERATED from SCPIStorageSD.c + Logger.h/Logger.c by
 * gen_1001_suspend_reason_fixture.py, which the Makefile runs as a
 * prerequisite of this binary -- so an edit to any of those three sources
 * regenerates it rather than leaving this file measuring a stale fixture. The
 * include is unconditional on purpose: if the header is missing and cannot be
 * produced, this test fails to COMPILE. It has no way to quietly skip. */
#include "gen_1001_suspend_reasons.h"

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
 * Fixtures
 *
 * The suspend strings ARE SD_SuspendReasonText()'s real returns, extracted
 * from SCPIStorageSD.c into kGen1001Reasons[] rather than declared here. They
 * are inputs only -- this file does not test which of the three that function
 * picks (FIDELITY 5) -- but using the real text means a pass-through that
 * truncated or rewrote the reason would be visible.
 *
 * #1001 changed these three from copies to views. As copies they were the
 * third defeated guard in miniature: correct on the day they were written and
 * silently free to drift from the firmware afterwards. Indices bind meaning to
 * source order, and generated_reasons_are_in_the_expected_order() below fails
 * loudly if that order changes -- an ordering assertion, deliberately separate
 * from the length measurement, so a reordering cannot quietly move which
 * string a case thinks it is exercising.
 * ========================================================================== */
#define GEN_1001_IDX_QUARANTINE   0
#define GEN_1001_IDX_FW_UPDATE    1
#define GEN_1001_IDX_WIFI_STREAM  2

#define kReasonWifiStream  (kGen1001Reasons[GEN_1001_IDX_WIFI_STREAM])
#define kReasonFwUpdate    (kGen1001Reasons[GEN_1001_IDX_FW_UPDATE])
#define kReasonQuarantine  (kGen1001Reasons[GEN_1001_IDX_QUARANTINE])

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
    /* Not `static`: the initialisers are now reads of the generated array
     * (#1001), which is not a constant expression. */
    const char *const reasons[] = {
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
    /* Not `static`: the initialisers are now reads of the generated array
     * (#1001), which is not a constant expression. */
    const char *const reasons[] = {
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
    /* Not `static`: see the note in the sweeps above (#1001). */
    const struct {
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



/* ==========================================================================
 * #1001 -- the length guard
 *
 * Everything these four cases read comes out of gen_1001_suspend_reasons.h,
 * which is regenerated from the firmware on every build. They assert, in
 * order: that the generated fixture still describes the function it was
 * written for; that its per-site prefix arithmetic recomputes; that every
 * reason fits every site that prints it; and that source order still means
 * what the fixture indices above say it means.
 * ========================================================================== */

/* `%s` conversions in a format string. Counted here, in C, rather than taken
 * from the generator, so prefixLen below is recomputed from the format text
 * instead of trusted. */
static int count_string_conversions(const char *fmt)
{
    int n = 0;
    const char *p;

    for (p = fmt; *p != '\0'; p++) {
        if (p[0] == '%' && p[1] == 's') {
            n++;
            p++;
        }
    }
    return n;
}

/* Render a format string with its CRLF visible, so a failure report keeps the
 * format on one line instead of breaking mid-message where the real "\r\n"
 * sits. Truncates rather than grows: these formats are well under the buffer,
 * and a diagnostic must not be the thing that overruns. */
static const char *printable_format(const char *fmt, char *buf, size_t cap)
{
    size_t o = 0;
    size_t i;

    for (i = 0; fmt[i] != '\0' && o + 3 < cap; i++) {
        if (fmt[i] == '\r' || fmt[i] == '\n') {
            buf[o++] = '\\';
            buf[o++] = (fmt[i] == '\r') ? 'r' : 'n';
        } else {
            buf[o++] = fmt[i];
        }
    }
    buf[o] = '\0';
    return buf;
}

/* The fixture describes the function this file was written against.
 *
 * The generator already refuses to emit a fixture whose counts differ -- it
 * exits non-zero and writes nothing, so a changed function fails the BUILD
 * rather than this assertion. These are restated here because the budget
 * reasoning in the header comment (four sites, one of them binding; three
 * reasons) is stated in terms of them, so a fixture that somehow arrived with
 * different counts must not be measured as though the reasoning still held. */
TEST(generated_fixture_still_describes_the_function_it_was_written_for)
{
    /* SD_SuspendReasonText() returns three strings and one NULL. */
    ASSERT_EQ(GEN_1001_REASON_COUNT, 3);
    /* Four LOG_E sites in SCPIStorageSD.c interpolate them. */
    ASSERT_EQ(GEN_1001_SITE_COUNT, 4);

    /* Zero extracted literals must never read as "nothing to check" -- the
     * vacuous pass is the failure mode every earlier attempt at this guard
     * had. The generator fails first; this is the backstop. */
    ASSERT_TRUE(GEN_1001_REASON_COUNT > 0);
    ASSERT_TRUE(GEN_1001_SITE_COUNT > 0);

    /* The ceiling is the tighter of Logger.c's two truncations, re-derived
     * here from the three constants the generator read out of the source. */
    ASSERT_EQ(GEN_1001_LOG_SURVIVING_CHARS,
              (GEN_1001_LOG_MESSAGE_SIZE - GEN_1001_LOG_VSNPRINTF_RESERVE - 1
               < GEN_1001_LOG_MESSAGE_SIZE - GEN_1001_LOG_CLAMP_RESERVE)
              ? GEN_1001_LOG_MESSAGE_SIZE - GEN_1001_LOG_VSNPRINTF_RESERVE - 1
              : GEN_1001_LOG_MESSAGE_SIZE - GEN_1001_LOG_CLAMP_RESERVE);

    /* And every reason is non-empty printable text, so strlen() below is
     * measuring a string and not an accident of extraction. */
    {
        int i;
        for (i = 0; i < GEN_1001_REASON_COUNT; i++) {
            ASSERT_TRUE(kGen1001Reasons[i] != NULL);
            ASSERT_TRUE(strlen(kGen1001Reasons[i]) > 0);
        }
    }
}

/* Each site's prefix cost recomputes from its own format text.
 *
 * prefixLen is the one number in the fixture that is arithmetic rather than
 * extraction, so it is the one number worth checking independently: the
 * formatted line is the format's characters, less two for each `%s` that gets
 * substituted, plus the mnemonic substituted into the first of them. If the
 * generator's arithmetic and this recomputation disagree, one of them is
 * wrong and the budget is not trustworthy either way. */
TEST(generated_prefix_lengths_recompute_from_the_real_formats)
{
    int i;

    for (i = 0; i < GEN_1001_SITE_COUNT; i++) {
        const Gen1001Site *s = &kGen1001Sites[i];
        int convs = count_string_conversions(s->format);
        int expect;

        /* One `%s` for the reason, optionally one before it for the command
         * mnemonic -- and a mnemonic exactly when there are two. */
        ASSERT_TRUE(convs == 1 || convs == 2);
        ASSERT_EQ(strlen(s->worstCmd) > 0, convs == 2);

        expect = (int)strlen(s->format) - 2 * convs + (int)strlen(s->worstCmd);
        if (expect != s->prefixLen) {
            char shown[256];

            printf("    #1001 prefix arithmetic disagrees for %s "
                   "(SCPIStorageSD.c:%d)\n"
                   "          format   = \"%s\" (%d chars, %d x %%s)\n"
                   "          mnemonic = \"%s\" (%d chars)\n"
                   "          expected = %d, fixture says %d\n",
                   s->func, s->line,
                   printable_format(s->format, shown, sizeof shown),
                   (int)strlen(s->format), convs,
                   s->worstCmd, (int)strlen(s->worstCmd), expect,
                   s->prefixLen);
        }
        ASSERT_EQ(s->prefixLen, expect);

        /* A prefix that already fills the line leaves no room for any reason,
         * which is #1000's shape -- and this file must not be the place that
         * discovers it silently. */
        ASSERT_TRUE(s->prefixLen < GEN_1001_LOG_SURVIVING_CHARS);
    }
}

/* THE GUARD. Every reason, against every call site that prints it.
 *
 * This is the assertion #986 needed and did not have. It is a full cross
 * product rather than a check against one precomputed worst case, so the
 * failure report names the site that actually binds -- and so that a NEW site
 * with a tighter prefix is caught by the site it tightens, not by arithmetic
 * done somewhere else. */
TEST(every_reason_fits_every_call_site_that_prints_it)
{
    int i, j;
    int worstHeadroom = GEN_1001_LOG_SURVIVING_CHARS;

    for (i = 0; i < GEN_1001_SITE_COUNT; i++) {
        const Gen1001Site *s = &kGen1001Sites[i];

        for (j = 0; j < GEN_1001_REASON_COUNT; j++) {
            const char *reason = kGen1001Reasons[j];
            int reasonLen = (int)strlen(reason);
            int total = s->prefixLen + reasonLen;
            int headroom = GEN_1001_LOG_SURVIVING_CHARS - total;

            if (headroom < worstHeadroom) {
                worstHeadroom = headroom;
            }
            if (total > GEN_1001_LOG_SURVIVING_CHARS) {
                char shown[256];

                printf("    #1001 TRUNCATION: a suspend reason does not fit "
                       "the line that prints it.\n"
                       "          call site : %s (SCPIStorageSD.c:%d)\n"
                       "          format    : \"%s\"\n"
                       "          mnemonic  : \"%s\" (longest that reaches "
                       "this site)\n"
                       "          prefix    : %d chars (format text + CRLF, "
                       "mnemonic substituted)\n"
                       "          reason    : %d chars -- \"%s\"\n"
                       "          formatted : %d chars, but Logger keeps only "
                       "%d\n"
                       "          OVER BY   : %d chars, which are cut from the "
                       "END of the reason.\n"
                       "          Shorten the reason in "
                       "SD_SuspendReasonText() (SCPIStorageSD.c),\n"
                       "          or shorten this call site's format string. "
                       "See #1001 / #986.\n",
                       s->func, s->line,
                       printable_format(s->format, shown, sizeof shown),
                       s->worstCmd,
                       s->prefixLen, reasonLen, reason, total,
                       GEN_1001_LOG_SURVIVING_CHARS,
                       total - GEN_1001_LOG_SURVIVING_CHARS);
            }
            ASSERT_TRUE(total <= GEN_1001_LOG_SURVIVING_CHARS);
        }
    }

    /* The cross product must have had something to check. 3 x 4 = 12 pairs;
     * an empty fixture would leave worstHeadroom at the ceiling and every
     * assertion above unexecuted, which is exactly how a guard goes quiet. */
    ASSERT_EQ(GEN_1001_SITE_COUNT * GEN_1001_REASON_COUNT, 12);
    ASSERT_TRUE(worstHeadroom < GEN_1001_LOG_SURVIVING_CHARS);

    /* Not asserted as an equality: headroom is expected to move whenever a
     * reason or a format is legitimately reworded, and pinning the number
     * would make every such edit a test failure with nothing wrong. It is
     * printed instead, so a shrinking margin is visible in `make run`'s
     * output before it becomes a truncation. */
    printf("    #1001: %d reason(s) x %d call site(s), tightest margin %d "
           "character(s) (budget %d)\n",
           GEN_1001_REASON_COUNT, GEN_1001_SITE_COUNT, worstHeadroom,
           GEN_1001_REASON_BUDGET);
}

/* Source order still means what the fixture indices claim.
 *
 * The three cases above this section pass kGen1001Reasons[] entries by index
 * (kReasonQuarantine and friends), so a reordering of the function's arms
 * would silently change which string each of them exercises. Keywords are
 * enough to tell the three apart and cannot hide a length change, because
 * length is measured separately and from the decoded bytes -- this is an
 * identity check, deliberately not a content check. */
TEST(generated_reasons_are_in_the_expected_order)
{
    ASSERT_TRUE(strstr(kReasonQuarantine, "quarantined") != NULL);
    ASSERT_TRUE(strstr(kReasonFwUpdate, "firmware update") != NULL);
    ASSERT_TRUE(strstr(kReasonWifiStream, "streaming") != NULL);

    /* Distinct strings, so the three cases above are not all one case. */
    ASSERT_TRUE(strcmp(kReasonQuarantine, kReasonFwUpdate) != 0);
    ASSERT_TRUE(strcmp(kReasonFwUpdate, kReasonWifiStream) != 0);
    ASSERT_TRUE(strcmp(kReasonQuarantine, kReasonWifiStream) != 0);

    /* The quarantine reason is the long one -- it is the string #986 had to
     * shorten, and the one with the least headroom today. If it stops being
     * the longest, the headroom print above is reporting a different string
     * than the prose in this file describes. */
    ASSERT_TRUE(strlen(kReasonQuarantine) > strlen(kReasonFwUpdate));
    ASSERT_TRUE(strlen(kReasonQuarantine) > strlen(kReasonWifiStream));
}


int main(void)
{
    printf("#953 -- SD:BENCHmark file-not-ready diagnosis (extracted cascade)\n");
    printf("#1001 - SD_SuspendReasonText() lengths vs their real callers "
           "(generated fixture)\n");
    printf("---------------------------------------------\n");
    RUN(suspend_during_wait_is_diagnosed_not_blamed_on_the_card);
    RUN(recorded_dir_full_refusal_outranks_a_later_suspend);
    RUN(dir_full_without_suspend_is_unchanged_by_953);
    RUN(no_suspend_no_dir_full_still_reports_the_card);
    RUN(exactly_one_quadrant_moves_and_why_is_sampled_once);

    /* #1001 -- the reason strings measured against their real callers. */
    RUN(generated_fixture_still_describes_the_function_it_was_written_for);
    RUN(generated_prefix_lengths_recompute_from_the_real_formats);
    RUN(every_reason_fits_every_call_site_that_prints_it);
    RUN(generated_reasons_are_in_the_expected_order);
    return TEST_SUMMARY();
}
