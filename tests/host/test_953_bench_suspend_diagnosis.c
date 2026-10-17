/* ==========================================================================
 * test_953_bench_suspend_diagnosis.c -- issues #953 and #988
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
 * #953 adds a third arm, and #988 a fourth. The ORDER of the four is the
 * substance of both:
 *
 *     const char *why = SD_SuspendReasonText();
 *     if (sd_card_manager_StartupDirFull())  -> #689/#690 refuse text
 *     else if (why != NULL)                  -> the live suspend reason  (#953)
 *     else if (armTornDown)                  -> "the write arm was torn down"
 *                                                                        (#988)
 *     else                                   -> the card diagnosis
 *
 * `armTornDown` is latched by the ready-wait poll loop the instant it observes
 * `pSDCardRuntimeConfig->mode != SD_CARD_MANAGER_MODE_WRITE` -- a transition
 * away from the arm THIS callback published a few lines earlier, which nothing
 * in this callback ever restores. That is the #988 distinction: a fact about
 * THIS request, not an ambient condition. See THE TRANSIENT QUADRANT below for
 * the three rounds of #983 that tried the ambient version and withdrew it.
 *
 * It has TWO writers, and the second is the SAMPLING BOUNDARY the first cannot
 * reach. The loop samples `mode` at the top of its body and then yields 10 ms
 * before its condition is re-tested, so its last observation and its exit are
 * different instants: on the iteration where readyWait is 499 the body samples,
 * delays, increments to 500, and the bound ends the loop with no further sample.
 * A teardown inside that delay -- or between the `while` condition's
 * IsWriteReady() and the separate call on the line below it -- is latched by
 * nobody, and the cascade walks past arm 3 into the card advisory for an arm
 * that is just as dead. So the failure block opens with a RECONCILIATION READ:
 *
 *     armTornDown = armTornDown ||
 *                   (pSDCardRuntimeConfig->mode != MODE_WRITE);
 *
 * one final sample, ORed in, with no gap left after it. The `||` is load-bearing
 * -- an assignment would erase the loop's finding whenever a different caller
 * had re-armed WRITE in the meantime -- and the arm ORDER is untouched: this
 * changes only when `armTornDown` may become true, never which arm reads it.
 * `torn_down_after_the_last_poll_sample_is_still_a_teardown` and
 * `the_reconciliation_ors_it_does_not_overwrite_the_latch` are those two
 * properties.
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
 * #988 KEEPS ITS OWN ARM BELOW THE SUSPEND ARM, which is the one ordering
 * decision that is not obvious. The latch knows the stronger FACT -- the arm is
 * provably dead, where a suspend reason only says who owns SPI4 right now -- so
 * the tempting order is dirFull -> tornDown -> suspend. It is wrong, and the
 * reason is that the arms are ranked by what the operator can DO with the
 * message, not by the certainty of the fact behind it. The suspend arm is the
 * only one that can name an owner AND the command that clears it; the
 * torn-down arm can only say "something killed it, retry". And where both fire
 * they almost always describe ONE event: the WiFi / FW-update / quarantine
 * teardown kills the arm and publishes the suspension through the SAME
 * app_SDCard_GracefulShutdown() call (app_freertos.c), which stores MODE_NONE
 * on its way into APP_SD_STATE_SUSPENDED. So the latch is ALWAYS set in #953's
 * own headline case, and hoisting it above the suspend arm would have silenced
 * #953 entirely while claiming to extend it. `tornDown_with_a_live_cause_
 * still_names_the_cause` is the test that fails if anyone tries.
 *
 * HOW IT IS TESTED
 *
 * SCPIStorageSD.c is not host-compilable -- libscpi, FreeRTOS and the whole SD
 * manager -- so this file follows test_943_bench_stall_bound.c: it
 * re-implements the cascade SHAPES (pre-#953, post-#953, post-#988) as pure
 * functions over injected values, and compares their verdicts on identical
 * inputs. What is proven is the DECISION ALGEBRA, which is all #953 and #988
 * change.
 *
 * #988 ADDS ONE MORE LAYER: the ready-wait POLL LOOP, modelled as a script of
 * per-iteration observations. It has to be, because the two scenarios #988's
 * acceptance criteria name -- "torn down with no observable cause" and "torn
 * down by a cause that has already lifted" -- present the CASCADE with
 * identical inputs (dirFull false, why NULL, tornDown true). What separates
 * them is when `why` was non-NULL during the wait, which only a time series can
 * express. That also settles a question the cascade alone cannot: the latch is
 * set by the loop, so the loop is where it can be got wrong.
 *
 * A bench test is not the alternative here, it is not available. The
 * daqifi-python-test-suite's test_589_sd_suspended_contract.py already records
 * -- for the identical race shape in the sibling ticket #936 -- that "arm
 * succeeds, then a suspend lands inside the wait window before the file opens"
 * is not reproducible on demand from a test script: it needs a
 * higher-priority event to land inside a window a test cannot aim at. #936's
 * own fix was verified by source reading for that reason, and both the #953 and
 * the #988 ticket headers say `Bench: none`. A deterministic host test of the
 * decision is the strictly stronger thing available.
 *
 * #988 makes one variant of that race look closer to reachable than the rest,
 * and it is worth writing down why it is still not a regression test. The
 * POWER-STATE teardown can be provoked deliberately -- `SYSTem:POWer:STATe 0`
 * from a second connection while a `SYST:STOR:SD:BENCHmark` is inside its five
 * second wait -- so unlike the WiFi case there is a command that causes it. But
 * the window that must be hit is not the five seconds: it is between the
 * `mode = MODE_WRITE` arm and the SD task opening the file, which on a healthy
 * card is a few tens of milliseconds, and a test that lands outside it either
 * gets refused at the arm (a different branch) or benchmarks successfully. The
 * pass/fail of the run would be a property of SCPI scheduling jitter, not of
 * the firmware -- and the way to make it land reliably is to make the open slow,
 * which is the card fault the fourth arm exists to NOT be confused with. Noted
 * as a possible follow-up rather than done here.
 *
 * CASES -- #953, the four quadrants of (suspended? x startupDirFull?)
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
 * CASES -- #988, the eight cells of (suspended? x dirFull? x tornDown?)
 *
 *   The same property, one dimension wider. NEW is blind to `tornDown`, so it
 *   is evaluated on the other two and compared against LATCHED on all three:
 *
 *   dirFull  suspended  tornDown | NEW              LATCHED          agree?
 *   -------------------------------------------------------------------------
 *   false    false      true     | GENERIC_TIMEOUT  ARM_TORN_DOWN    NO  <-#988
 *   false    false      false    | GENERIC_TIMEOUT  GENERIC_TIMEOUT   yes
 *   false    true       true     | SUSPEND_REASON   SUSPEND_REASON    yes <-ord.
 *   false    true       false    | SUSPEND_REASON   SUSPEND_REASON    yes
 *   true     false      true     | STARTUP_DIR_FULL STARTUP_DIR_FULL  yes <-#690
 *   true     false      false    | STARTUP_DIR_FULL STARTUP_DIR_FULL  yes
 *   true     true       true     | STARTUP_DIR_FULL STARTUP_DIR_FULL  yes
 *   true     true       false    | STARTUP_DIR_FULL STARTUP_DIR_FULL  yes
 *
 * Exactly one CELL moves, and it is the one the ticket was filed for. So the
 * file's original framing survives rather than being replaced: each shape moves
 * exactly one cell of its own input space -- #953 one of four, #988 one of
 * eight. Row 3 is the one that would ALSO move under "tornDown before suspend",
 * taking #953's whole fix with it; row 7 is the one that would move under
 * "tornDown before dirFull", taking #690's with it. Both are asserted.
 *
 * Note row 5 and row 7: `dirFull && tornDown` is not a hypothetical
 * combination, it is the NORMAL shape of the #690 path. sd_card_manager.c's
 * OPEN_FILE bucket refusal (:2164-2170) raises startupDirFull and then stores
 * MODE_NONE, so every real #690 refusal sets both. That is what makes arm 1's
 * primacy load-bearing a second time.
 *
 * WHAT THIS FILE DELIBERATELY DOES NOT MEASURE
 *
 * The lengths of the strings the cascade prints. #986 shortened the quarantine
 * reason because Logger cuts a formatted line at LOG_MESSAGE_SIZE - 3 and it
 * did not fit; a regression guard for that belongs here, and THREE attempts at
 * one were each defeated in review before the mechanism was withdrawn:
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
 * Each fix drew the next finding, which is the signal to stop adding
 * machinery. The guard needs to measure the REAL strings -- extracted from the
 * source, not copied -- and that is a design, not a patch. #1001.
 *
 * THE TRANSIENT QUADRANT, AND HOW #988 CLOSED IT
 *
 * The #953 table reads `suspended` as one value, which assumes the condition is
 * whatever it is at the timeout. It is not: the wait is five seconds long and a
 * suspension can start AND END inside it. That case is not benign.
 * app_SDCard_GracefulShutdown() stores MODE_NONE over the benchmark's
 * MODE_WRITE arm on the way into APP_SD_STATE_SUSPENDED (app_freertos.c),
 * nothing restores it when the suspension lifts, and
 * sd_card_manager_IsWriteReady() requires MODE_WRITE -- so the arm is dead for
 * the rest of the wait and the pre-#988 cascade lands in the card arm with
 * SD_SuspendReasonText() already back to NULL.
 *
 * #953's PR tried three times to close that by latching what the poll OBSERVED,
 * and each round found the same defect from a new angle: an ambient condition
 * is not evidence about THIS request. A live WiFi owner can be sampled and then
 * vanish without the SD task ever suspending -- the streaming task's
 * dead-transport auto-stop runs at priority 6 and beats the SD task at 5 to its
 * own ownership check -- and a latch set from that blames a suspension for a
 * genuine card fault. That is worse than the advisory it replaces, because it
 * is confidently wrong. So #953 shipped without a latch and left the quadrant
 * open, with the right fix described in #988.
 *
 * #988 IS THAT FIX, AND THE DIFFERENCE IS WHAT IS LATCHED. Not "a suspension
 * was observed" but "`mode` left the MODE_WRITE this callback published". Only
 * this callback could have put WRITE there, and nothing restores it, so a
 * transition away from it is evidence about THIS request and cannot be someone
 * else's weather. It is also true of EVERY teardown cause, including the
 * power-state path, which bypasses APP_SD_STATE_SUSPENDED entirely (the SD task
 * goes to APP_SD_STATE_WAIT_POWER_UP and the flag it publishes is `state ==
 * SUSPENDED || app_SDCard_SpiOwnedByWifi()`, false in both terms) and was
 * therefore invisible to #953's arm as well as to the withdrawn latch.
 *
 * Two tests below are that quadrant, one for each half of #988's acceptance
 * criteria: `torn_down_with_no_observable_cause_is_not_blamed_on_the_card` and
 * `torn_down_by_a_cause_that_has_lifted_is_still_a_teardown`. Both fail against
 * `new_bench_not_ready_diagnosis` -- the shipped pre-#988 shape -- which is the
 * point.
 *
 * FIDELITY -- what the extracted functions are NOT
 *
 * 1. #988 MOVED THIS ITEM, as the previous revision predicted it would. THREE
 *    things are modelled now: the DECISION CASCADE after the wait loop (as
 *    before); for the #988 cases only, the parts of the `readyWait < 500`
 *    poll loop that DECIDE the latch -- the order of its two in-loop tests (the
 *    #690 early-exit `break` first, the `mode != MODE_WRITE` latch second) and
 *    the fact that the latch is sticky; and the RECONCILIATION READ between the
 *    two (bench_reconcile), which is a separate step here because it is a
 *    separate step in the firmware and because its sample is strictly later than
 *    the loop's last one. Still out of scope, because neither #953 nor #988
 *    changes them: the vTaskDelay cadence, the 5 s bound, and the
 *    iteration count -- a poll script's length stands in for the 500, and no
 *    test here asserts anything about how long the wait takes.
 *
 *    The reconciliation's TIMING is therefore not modelled either, only its
 *    position in the data flow: `bench_reconcile`'s argument is "what `mode`
 *    reads at the cascade", and which real-world delay produced that value --
 *    the final vTaskDelay, the gap between the two IsWriteReady() calls -- is
 *    the same input to this model. Both are named at the firmware site; the
 *    model's job is that a LATER read exists at all and is ORed rather than
 *    assigned.
 *
 *    The latch must also not `break`, and it is worth being precise about how
 *    far that is covered. Breaking would end the wait at the instant the
 *    teardown becomes visible, which is also the instant the cause that did it
 *    is still visible -- so
 *    `torn_down_by_a_cause_that_has_lifted_is_still_a_teardown` could never be
 *    reached in the firmware at all. Adding a `break` to bench_run_wait() below
 *    turns that test and `dir_full_that_also_tore_the_arm_down_keeps_its_
 *    verdict` RED (mutation-proven), so the MODEL's no-break shape is pinned:
 *    nobody can quietly reshape it to match a firmware that breaks. What
 *    nothing here can see is the firmware breaking while the model does not --
 *    that is held by the comment at the firmware site and by review. See the
 *    Makefile target's own note for why a grep for it was tried and rejected.
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
 *    pointer is the failure mode worth pinning. #988's arm therefore carries
 *    `text == NULL` in this model even though the real one interpolates two
 *    strings (sd_card_manager_GetStateName() / GetModeName()): those are
 *    breadcrumbs sampled at log time, they do not steer the branch, and
 *    asserting them would be asserting the log text this note excludes. The
 *    arm is distinguished from the generic one by its DIAG value, which is
 *    what the cascade actually decides.
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
 * The diagnoses the branch can reach. Appended to, never renumbered -- the
 * #953 tests below read these by name, and a renumber would make a stale
 * object file compare green against the wrong arm.
 * ========================================================================== */
typedef enum {
    /* LOG_E("SD:BENCH refused (#689): %s", WriteRefuseText()) -- #689/#690. */
    DIAG_STARTUP_DIR_FULL = 0,
    /* LOG_E("SD:BENCH - could not complete the arm: %s", why) -- #953. */
    DIAG_SUSPEND_REASON,
    /* "File not ready after timeout" + the SPI-mode-incompatible card hint. */
    DIAG_GENERIC_TIMEOUT,
    /* LOG_E("SD:BENCH - the write arm was torn down before the file opened
     * (SD now state=%s mode=%s) - retry") -- #988, new. */
    DIAG_ARM_TORN_DOWN
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
    /* #988: the `armTornDown` local the ready-wait poll loop latches. Not a
     * firmware CALL like the three above -- it is a value the loop computed,
     * which is why bench_run_wait() below exists to compute it rather than
     * letting every test assert it into place by hand. The two pure-cascade
     * shapes (old / new) are blind to it; only `latched_` reads it. */
    bool        armTornDown;
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

/* POST-#988: four arms. The torn-down latch sits between the live suspend
 * reason and the card diagnosis -- BELOW the suspend arm, not above it, for the
 * reason set out in the file header. `why` is still sampled exactly once.
 *
 * text is NULL on this arm: the real one interpolates GetStateName() /
 * GetModeName(), which are breadcrumbs sampled at log time and not part of the
 * decision (FIDELITY 4). */
static BenchVerdict latched_bench_not_ready_diagnosis(BenchEnv *env)
{
    BenchVerdict v;
    const char *why = mock_suspend_reason_text(env);
    if (mock_startup_dir_full(env)) {
        v.diag = DIAG_STARTUP_DIR_FULL;
        v.text = mock_write_refuse_text(env);
    } else if (why != NULL) {
        v.diag = DIAG_SUSPEND_REASON;
        v.text = why;
    } else if (env->armTornDown) {
        v.diag = DIAG_ARM_TORN_DOWN;
        v.text = NULL;
    } else {
        v.diag = DIAG_GENERIC_TIMEOUT;
        v.text = NULL;
    }
    return v;
}

/* ==========================================================================
 * #988: the ready-wait POLL LOOP, as a script of per-iteration observations.
 *
 * Extracted from SCPIStorageSD.c with the same discipline as the cascades
 * above -- same tests, same order:
 *
 *     while (!sd_card_manager_IsWriteReady() && readyWait < 500) {
 *         if (sd_card_manager_StartupDirFull())            { break; }
 *         if (pSDCardRuntimeConfig->mode != MODE_WRITE) { armTornDown = true; }
 *         vTaskDelay(pdMS_TO_TICKS(10));
 *         readyWait++;
 *     }
 *
 * IsWriteReady() is absent for the same reason it is absent from the cascade
 * (FIDELITY 2): every script here describes a wait in which the file never
 * opens, which is the only branch under test. The script's LENGTH stands in for
 * the `readyWait < 500` bound -- running off the end of the script is "the wait
 * timed out" -- so nothing here asserts a duration.
 * ========================================================================== */
typedef struct {
    /* sd_card_manager_StartupDirFull() as this iteration would read it. */
    bool        startupDirFull;
    /* pSDCardRuntimeConfig->mode == SD_CARD_MANAGER_MODE_WRITE, i.e. is THIS
     * request's arm still standing at this iteration? */
    bool        modeIsWrite;
    /* SD_SuspendReasonText() as it would read at this iteration. The loop
     * never calls it -- only the cascade does, once, at the end -- so this is
     * consumed from the LAST iteration the loop reached. */
    const char *suspendReason;
} BenchPoll;

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
    "SD quarantined after a bus jam - reseat the card, "
    "then SYST:STOR:SD:ENAble 1";

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
    /* #988: the #953 cases below predate the latch and are blind to it, so
     * they get the value that makes the fourth arm unreachable. That keeps the
     * five original tests byte-for-byte the regression guards they were. */
    env->armTornDown        = false;
}

/* #988: same, plus the latch -- for the pure-cascade cube, where the three
 * inputs are enumerated directly rather than derived from a poll script. */
static void env_init_torn(BenchEnv *env, bool dirFull, const char *suspendReason,
                          bool armTornDown)
{
    env_init(env, dirFull, suspendReason);
    env->armTornDown = armTornDown;
}

/* Runs the extracted poll loop over `polls`, then fills `out` with what the
 * cascade would sample immediately afterwards: startupDirFull and the suspend
 * reason as they read at the instant the wait ended, plus the latched
 * armTornDown.
 *
 * "The instant the wait ended" is polls[last]: the #690 break exits ON the
 * iteration that observed the flag, and a run to the end of the script exits
 * with the final iteration's state still current. */
static void bench_run_wait(const BenchPoll *polls, size_t n, BenchEnv *out)
{
    bool   armTornDown = false;
    size_t last = 0;
    size_t i;

    if (n == 0u) {
        /* No caller does this -- every script below is a non-empty literal --
         * but without the guard an empty one would read polls[0]. A wait that
         * polled nothing observed nothing. */
        env_init(out, false, NULL);
        return;
    }

    for (i = 0; i < n; i++) {
        last = i;
        if (polls[i].startupDirFull) {
            break;                              /* #690 early-exit */
        }
        if (!polls[i].modeIsWrite) {
            armTornDown = true;                 /* #988 latch -- and no break */
        }
    }

    env_init_torn(out, polls[last].startupDirFull, polls[last].suspendReason,
                  armTornDown);
}

/* #988: the RECONCILIATION READ, as its own step -- which is what it is in the
 * firmware too:
 *
 *     if (!sd_card_manager_IsWriteReady()) {
 *         armTornDown = armTornDown ||
 *                       (pSDCardRuntimeConfig->mode != MODE_WRITE);
 *         const char *why = SD_SuspendReasonText();
 *         ...
 *
 * It is NOT folded into bench_run_wait() on purpose. The loop's last sample and
 * the loop's EXIT are different instants -- the body samples `mode`, then yields
 * 10 ms, and the bound can expire in that delay -- so `modeIsWrite` at the
 * cascade is a genuinely later observation than polls[last].modeIsWrite and has
 * to be supplied separately. A model that reused polls[last] for it could not
 * express the boundary case at all, which is precisely how the defect survived
 * the first round of #988.
 *
 * Calling this is the POST-reconciliation shape; not calling it is the shape
 * #988 originally shipped. The two new tests below run the same script through
 * both, which is the whole proof.
 *
 * The OR is modelled, not an assignment, because the firmware's is an OR and
 * the difference is load-bearing -- see `the_reconciliation_ors_it_does_not_
 * overwrite_the_latch`. */
static void bench_reconcile(BenchEnv *env, bool modeIsWriteAtCascade)
{
    if (!modeIsWriteAtCascade) {
        env->armTornDown = true;
    }
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

/* ==========================================================================
 * #988 -- the transient quadrant, closed.
 *
 * These five drive the POLL LOOP (bench_run_wait) rather than asserting the
 * cascade's inputs into place, because two of them are only distinguishable as
 * time series: at the cascade both present (dirFull=false, why=NULL,
 * tornDown=true).
 * ========================================================================== */

/* #988 (a) -- THE POWER-STATE GAP, which is the case the ticket was filed for.
 *
 * app_SDCardTask's APP_SD_STATE_PROCESS sees the power state leave POWERED_UP,
 * calls app_SDCard_GracefulShutdown(3000, "power state dropped") -- which
 * stores MODE_NONE over this benchmark's arm -- and goes to
 * APP_SD_STATE_WAIT_POWER_UP. It never enters APP_SD_STATE_SUSPENDED, and the
 * flag it publishes each iteration is `state == SUSPENDED ||
 * app_SDCard_SpiOwnedByWifi()`, so SD_SuspendReasonText() observes NOTHING on
 * this path at any point. The arm is just as dead as under a WiFi suspend.
 *
 * Pre-#988 that lands in the card arm: "likely SPI-mode incompatible", for a
 * device that powered down. This is the assertion that screams if #988 is
 * reverted. */
TEST(torn_down_with_no_observable_cause_is_not_blamed_on_the_card)
{
    static const BenchPoll kPolls[] = {
        /* dirFull, modeIsWrite, suspendReason */
        { false,    true,        NULL },   /* armed, waiting for the open      */
        { false,    true,        NULL },
        { false,    false,       NULL },   /* GracefulShutdown stored MODE_NONE */
        { false,    false,       NULL },   /* nothing restores it              */
        { false,    false,       NULL },   /* ... and no owner is ever nameable */
    };
    const size_t n = sizeof(kPolls) / sizeof(kPolls[0]);
    BenchEnv oldEnv, newEnv, latchedEnv;
    BenchVerdict oldV, newV, latchedV;

    bench_run_wait(kPolls, n, &oldEnv);
    bench_run_wait(kPolls, n, &newEnv);
    bench_run_wait(kPolls, n, &latchedEnv);

    /* The loop is what establishes the fact; assert it did, so a later failure
     * cannot be mistaken for a cascade bug when it is a loop bug. */
    ASSERT_TRUE(latchedEnv.armTornDown);
    ASSERT_FALSE(latchedEnv.startupDirFull);
    ASSERT_TRUE(latchedEnv.suspendReason == NULL);

    oldV     = old_bench_not_ready_diagnosis(&oldEnv);
    newV     = new_bench_not_ready_diagnosis(&newEnv);
    latchedV = latched_bench_not_ready_diagnosis(&latchedEnv);

    /* Both shipped shapes blame the card -- #953 included, because there is no
     * suspension here for its arm to see. */
    ASSERT_EQ(oldV.diag, DIAG_GENERIC_TIMEOUT);
    ASSERT_EQ(newV.diag, DIAG_GENERIC_TIMEOUT);

    /* #988 says what actually happened. */
    ASSERT_EQ(latchedV.diag, DIAG_ARM_TORN_DOWN);
    ASSERT_TRUE(latchedV.diag != newV.diag);

    /* Still exactly one sample of the reason, on the arm that discards it. */
    ASSERT_EQ(latchedEnv.suspendReasonCalls, 1);
}

/* #988 (b) -- THE TRANSIENT QUADRANT ITSELF: torn down by a cause that has
 * since lifted. A WiFi stream takes SPI4 mid-wait, the SD task tears the arm
 * down on its way into APP_SD_STATE_SUSPENDED, the stream then stops and the
 * task resumes -- so by the time the cascade samples SD_SuspendReasonText()
 * there is nothing left to name, while the arm stays dead (nothing restores
 * MODE_WRITE).
 *
 * This is the case the #953 PR could not reach and documented as open. At the
 * cascade it is indistinguishable from (a) -- which is exactly why the latch
 * has to be computed by the LOOP and not injected: the two differ only in when
 * `why` was non-NULL.
 *
 * It also pins the no-break property from the outside: if the loop broke on
 * the latch it would exit at poll 2, where the reason is still live, and this
 * would return DIAG_SUSPEND_REASON. */
TEST(torn_down_by_a_cause_that_has_lifted_is_still_a_teardown)
{
    static const BenchPoll kPolls[] = {
        /* dirFull, modeIsWrite, suspendReason */
        { false,    true,        NULL              },  /* armed, bus is ours   */
        { false,    true,        kReasonWifiStream },  /* WiFi claims SPI4     */
        { false,    false,       kReasonWifiStream },  /* SD task tears down   */
        { false,    false,       kReasonWifiStream },  /* ... and suspends     */
        { false,    false,       NULL              },  /* stream stops,        */
        { false,    false,       NULL              },  /*   task resumes       */
    };
    const size_t n = sizeof(kPolls) / sizeof(kPolls[0]);
    BenchEnv newEnv, latchedEnv;
    BenchVerdict newV, latchedV;

    bench_run_wait(kPolls, n, &newEnv);
    bench_run_wait(kPolls, n, &latchedEnv);

    /* The wait ran to its end -- no break -- so the cascade sees the LIFTED
     * state, not the state at the teardown. */
    ASSERT_TRUE(latchedEnv.armTornDown);
    ASSERT_TRUE(latchedEnv.suspendReason == NULL);

    newV     = new_bench_not_ready_diagnosis(&newEnv);
    latchedV = latched_bench_not_ready_diagnosis(&latchedEnv);

    ASSERT_EQ(newV.diag, DIAG_GENERIC_TIMEOUT);       /* the open quadrant */
    ASSERT_EQ(latchedV.diag, DIAG_ARM_TORN_DOWN);     /* closed by #988    */
    ASSERT_TRUE(latchedV.diag != newV.diag);
}

/* #988 (c) -- THE ORDERING DECISION AGAINST ARM 2, and the only reason this
 * case exists.
 *
 * Same teardown, except the cause is still present when the wait ends. The
 * latch is set (it always is on this path -- app_SDCard_GracefulShutdown
 * stores MODE_NONE on its way into APP_SD_STATE_SUSPENDED), and so is the
 * suspend reason. #953's arm must still win: it is the only one that can name
 * the owner and the command that clears it.
 *
 * Hoist `armTornDown` above `why != NULL` in latched_bench_not_ready_diagnosis
 * and this test is what fails -- and with it #953, in #953's own headline
 * case. Swept over all three causes for the same reason (a) is. */
TEST(torn_down_with_a_live_cause_still_names_the_cause)
{
    static const char *const reasons[] = {
        kReasonWifiStream, kReasonFwUpdate, kReasonQuarantine
    };
    size_t i;

    for (i = 0; i < sizeof(reasons) / sizeof(reasons[0]); i++) {
        const BenchPoll polls[] = {
            { false, true,  NULL        },
            { false, false, reasons[i] },   /* torn down, cause still live */
            { false, false, reasons[i] },
        };
        BenchEnv newEnv, latchedEnv;
        BenchVerdict newV, latchedV;

        bench_run_wait(polls, sizeof(polls) / sizeof(polls[0]), &newEnv);
        bench_run_wait(polls, sizeof(polls) / sizeof(polls[0]), &latchedEnv);

        ASSERT_TRUE(latchedEnv.armTornDown);
        ASSERT_TRUE(latchedEnv.suspendReason == reasons[i]);

        newV     = new_bench_not_ready_diagnosis(&newEnv);
        latchedV = latched_bench_not_ready_diagnosis(&latchedEnv);

        /* #953 is untouched by #988 here -- same arm, same string, same
         * pointer, on both shapes. */
        ASSERT_EQ(latchedV.diag, DIAG_SUSPEND_REASON);
        ASSERT_TRUE(latchedV.diag != DIAG_ARM_TORN_DOWN);
        ASSERT_EQ(newV.diag, latchedV.diag);
        ASSERT_TRUE(latchedV.text == reasons[i]);
        ASSERT_TRUE(newV.text == latchedV.text);
    }
}

/* #988 (d) -- THE ORDERING DECISION AGAINST ARM 1, on the shape the #690 path
 * really has.
 *
 * sd_card_manager.c's OPEN_FILE bucket refusal (:2164-2170) raises
 * startupDirFull AND stores MODE_NONE, in that order, as one refusal. So every
 * real #690 case sets the latch too -- `dirFull && tornDown` is the normal
 * shape, not a corner. The recorded verdict must still win: it names WHY the
 * open was refused, where the latch can only say that something ended the arm.
 *
 * The loop's #690 early-exit fires on that iteration, so the latch is not even
 * set here; the cascade's re-read of startupDirFull is what makes the answer
 * independent of which of the two the poll happened to see first. The next
 * case covers the other interleaving. */
TEST(dir_full_that_also_tore_the_arm_down_keeps_its_verdict)
{
    static const BenchPoll kPolls[] = {
        { false, true,  NULL },
        { true,  false, NULL },   /* the #690 refusal: both, one iteration */
        { true,  false, NULL },
    };
    const size_t n = sizeof(kPolls) / sizeof(kPolls[0]);
    BenchEnv oldEnv, newEnv, latchedEnv;
    BenchVerdict oldV, newV, latchedV;

    bench_run_wait(kPolls, n, &oldEnv);
    bench_run_wait(kPolls, n, &newEnv);
    bench_run_wait(kPolls, n, &latchedEnv);

    ASSERT_TRUE(latchedEnv.startupDirFull);

    oldV     = old_bench_not_ready_diagnosis(&oldEnv);
    newV     = new_bench_not_ready_diagnosis(&newEnv);
    latchedV = latched_bench_not_ready_diagnosis(&latchedEnv);

    ASSERT_EQ(latchedV.diag, DIAG_STARTUP_DIR_FULL);
    ASSERT_TRUE(latchedV.diag != DIAG_ARM_TORN_DOWN);
    ASSERT_EQ(oldV.diag, latchedV.diag);
    ASSERT_EQ(newV.diag, latchedV.diag);
    ASSERT_TRUE(latchedV.text == kRefuseBucketsExhausted);

    /* The other interleaving: the teardown became visible to the poll BEFORE
     * the flag did. The latch is set, the flag is set, and arm 1 still wins --
     * because the cascade re-reads the flag rather than trusting the loop's
     * exit. This is the case #690 would lose if the latch were hoisted. */
    {
        static const BenchPoll kSplit[] = {
            { false, true,  NULL },
            { false, false, NULL },   /* mode cleared, flag not yet observed */
            { true,  false, NULL },   /* flag observed on the next poll      */
        };
        BenchEnv splitEnv;
        BenchVerdict splitV;

        bench_run_wait(kSplit, sizeof(kSplit) / sizeof(kSplit[0]), &splitEnv);
        ASSERT_TRUE(splitEnv.armTornDown);
        ASSERT_TRUE(splitEnv.startupDirFull);

        splitV = latched_bench_not_ready_diagnosis(&splitEnv);
        ASSERT_EQ(splitV.diag, DIAG_STARTUP_DIR_FULL);
        ASSERT_TRUE(splitV.text == kRefuseBucketsExhausted);
    }
}

/* #988 (e) -- THE CARD DIAGNOSIS SURVIVES, which is what stops the lazy
 * mutation "latch unconditionally" (or "latch on the first poll").
 *
 * A genuinely SPI-incompatible card is the one failure that does NOT clear
 * `mode`: sd_card_manager.c:2358-2365 takes a failed WRITE-mode
 * SYS_FS_FileOpen to PROCESS_STATE_ERROR and leaves the arm standing, so the
 * machine loops ERROR -> UNMOUNT -> INIT -> ... with mode still MODE_WRITE for
 * the whole five seconds. The latch must stay false and the wiki hint must
 * still be what the operator gets. */
TEST(a_card_that_never_opens_the_file_does_not_latch)
{
    static const BenchPoll kPolls[] = {
        { false, true, NULL },
        { false, true, NULL },
        { false, true, NULL },
        { false, true, NULL },
    };
    const size_t n = sizeof(kPolls) / sizeof(kPolls[0]);
    BenchEnv newEnv, latchedEnv;
    BenchVerdict newV, latchedV;

    bench_run_wait(kPolls, n, &newEnv);
    bench_run_wait(kPolls, n, &latchedEnv);

    ASSERT_FALSE(latchedEnv.armTornDown);

    newV     = new_bench_not_ready_diagnosis(&newEnv);
    latchedV = latched_bench_not_ready_diagnosis(&latchedEnv);

    ASSERT_EQ(latchedV.diag, DIAG_GENERIC_TIMEOUT);
    ASSERT_EQ(newV.diag, latchedV.diag);
    ASSERT_TRUE(latchedV.text == NULL);
}

/* #988 (f) -- THE SAMPLING BOUNDARY: a teardown the LOOP could never have seen.
 *
 * The loop samples `mode` at the top of its body and then yields 10 ms before
 * its condition is re-tested, so its last observation and its exit are different
 * instants. On the iteration where readyWait is 499 the body samples, delays,
 * increments to 500 -- and `readyWait < 500` ends the loop with no further
 * sample. app_SDCard_GracefulShutdown() running on the SD task inside that final
 * delay stores MODE_NONE over this benchmark's arm and is observed by nobody.
 * (The same gap swallows a teardown that lands between the `while` condition's
 * IsWriteReady() call and the separate one on the line below it.)
 *
 * The arm is exactly as dead as in case (a) -- nothing restores MODE_WRITE --
 * but `armTornDown` is false, so the shape #988 shipped walks past its own arm
 * into the card advisory. That is the defect this reconciliation closes, and it
 * is the assertion that screams if the reconciliation read is removed.
 *
 * Note what the script says: EVERY poll observes the arm standing. There is no
 * iteration to which the teardown could be attributed, which is what makes this
 * a different case from (a)-(e) rather than a re-spelling of (a). */
TEST(torn_down_after_the_last_poll_sample_is_still_a_teardown)
{
    static const BenchPoll kPolls[] = {
        /* dirFull, modeIsWrite, suspendReason */
        { false,    true,        NULL },   /* armed, waiting for the open      */
        { false,    true,        NULL },
        { false,    true,        NULL },   /* the wait's LAST sample: still ours */
    };
    const size_t n = sizeof(kPolls) / sizeof(kPolls[0]);
    BenchEnv preEnv, postEnv;
    BenchVerdict preV, postV;

    /* Same script into both shapes; the reconciliation is the only difference. */
    bench_run_wait(kPolls, n, &preEnv);
    bench_run_wait(kPolls, n, &postEnv);

    /* The loop latched nothing -- in BOTH -- which is the premise of the case.
     * Assert it, so a failure below cannot be mistaken for a loop bug. */
    ASSERT_FALSE(preEnv.armTornDown);
    ASSERT_FALSE(postEnv.armTornDown);

    /* ...and then the teardown lands in the final delay: MODE_NONE by the time
     * the cascade reads it. */
    bench_reconcile(&postEnv, false);
    ASSERT_TRUE(postEnv.armTornDown);

    preV  = latched_bench_not_ready_diagnosis(&preEnv);
    postV = latched_bench_not_ready_diagnosis(&postEnv);

    /* The shipped #988 shape blames the card for a teardown that happened. */
    ASSERT_EQ(preV.diag, DIAG_GENERIC_TIMEOUT);
    /* The reconciliation says what actually happened. */
    ASSERT_EQ(postV.diag, DIAG_ARM_TORN_DOWN);
    ASSERT_TRUE(postV.diag != preV.diag);

    /* Still one sample of the reason, on an arm that discards it. */
    ASSERT_EQ(postEnv.suspendReasonCalls, 1);

    /* THE CARD GUARD SURVIVES. The reconciliation must not fire merely because
     * it exists: a genuinely SPI-incompatible card never clears `mode`
     * (sd_card_manager.c takes a failed WRITE-mode open to PROCESS_STATE_ERROR
     * with the arm standing), so the read finds MODE_WRITE and the operator
     * still gets the card hint. This is what stops the lazy mutation
     * "reconcile unconditionally". */
    {
        BenchEnv cardEnv;
        BenchVerdict cardV;

        bench_run_wait(kPolls, n, &cardEnv);
        bench_reconcile(&cardEnv, true);      /* arm still standing at the cascade */
        ASSERT_FALSE(cardEnv.armTornDown);

        cardV = latched_bench_not_ready_diagnosis(&cardEnv);
        ASSERT_EQ(cardV.diag, DIAG_GENERIC_TIMEOUT);
        ASSERT_TRUE(cardV.text == NULL);
    }

    /* THE ARM ORDER IS UNTOUCHED, both directions. The reconciliation changes
     * only WHEN armTornDown may become true, never which arm consumes it -- so
     * on the two inputs that outrank it, the verdict must be identical to what
     * it was before. Reconciling `false` (the strongest possible push toward arm
     * 3) must still lose to a recorded #690 verdict and to a live suspend
     * reason. Hoist the reconciliation's effect above either arm and these two
     * blocks are what fail. */
    {
        static const BenchPoll kDirFull[] = {
            { false, true, NULL },
            { true,  true, NULL },        /* the #690 refusal is recorded */
        };
        BenchEnv dirEnv;
        BenchVerdict dirV;

        bench_run_wait(kDirFull, sizeof(kDirFull) / sizeof(kDirFull[0]), &dirEnv);
        bench_reconcile(&dirEnv, false);
        ASSERT_TRUE(dirEnv.armTornDown);
        ASSERT_TRUE(dirEnv.startupDirFull);

        dirV = latched_bench_not_ready_diagnosis(&dirEnv);
        ASSERT_EQ(dirV.diag, DIAG_STARTUP_DIR_FULL);
        ASSERT_TRUE(dirV.text == kRefuseBucketsExhausted);
    }
    {
        static const BenchPoll kSuspended[] = {
            { false, true, NULL },
            { false, true, kReasonWifiStream },   /* cause live at the cascade */
        };
        BenchEnv suspEnv;
        BenchVerdict suspV;

        bench_run_wait(kSuspended, sizeof(kSuspended) / sizeof(kSuspended[0]),
                       &suspEnv);
        bench_reconcile(&suspEnv, false);
        ASSERT_TRUE(suspEnv.armTornDown);
        ASSERT_TRUE(suspEnv.suspendReason == kReasonWifiStream);

        suspV = latched_bench_not_ready_diagnosis(&suspEnv);
        ASSERT_EQ(suspV.diag, DIAG_SUSPEND_REASON);
        ASSERT_TRUE(suspV.text == kReasonWifiStream);
    }
}

/* #988 (g) -- THE RECONCILIATION IS AN `||`, AND THAT IS THE POINT OF IT.
 *
 * Writing it as `armTornDown = (mode != MODE_WRITE)` passes case (f) -- and
 * silently destroys the loop's own finding on the interleaving that most needs
 * it. The loop's latch records a FACT about this request ("mode left the
 * MODE_WRITE this callback published"); nothing makes that fact untrue later.
 * But `mode` can read MODE_WRITE again at the cascade, because after the
 * teardown released the manager a DIFFERENT caller is free to claim and arm its
 * own operation (sd_card_manager_TryClaim succeeds once mode is NONE and the
 * machine is idle). An assignment would then overwrite `true` with `false` and
 * hand that request the card advisory -- reintroducing the #988 bug through the
 * fix for it.
 *
 * This is the test that goes red on that mutation, and it is why the firmware
 * line reads `armTornDown = armTornDown || (...)` and not `armTornDown = (...)`. */
TEST(the_reconciliation_ors_it_does_not_overwrite_the_latch)
{
    static const BenchPoll kPolls[] = {
        /* dirFull, modeIsWrite, suspendReason */
        { false,    true,        NULL },   /* armed                            */
        { false,    false,       NULL },   /* teardown -- the LOOP sees it     */
        { false,    false,       NULL },
    };
    const size_t n = sizeof(kPolls) / sizeof(kPolls[0]);
    BenchEnv env;
    BenchVerdict v;

    bench_run_wait(kPolls, n, &env);
    ASSERT_TRUE(env.armTornDown);          /* the loop established the fact */

    /* Another caller has since armed its own WRITE, so the reconciliation's
     * sample reads MODE_WRITE. The latch must survive it. */
    bench_reconcile(&env, true);
    ASSERT_TRUE(env.armTornDown);

    v = latched_bench_not_ready_diagnosis(&env);
    ASSERT_EQ(v.diag, DIAG_ARM_TORN_DOWN);
    ASSERT_TRUE(v.diag != DIAG_GENERIC_TIMEOUT);
}

/* The #988 cube, asserted as a table for the same reason #953's quadrants are:
 * so "exactly one cell moves" is a checked property of the pair of shapes and
 * not a claim in a comment.
 *
 * NEW is blind to `tornDown`, so it is evaluated on the other two inputs and
 * compared against LATCHED on all three. The one cell that moves is the one
 * #988 was filed for. Rows 3 and 4 are what a "tornDown before suspend"
 * reorder would break (taking #953 with it); rows 5-8 are what a "tornDown
 * before dirFull" reorder would break (taking #690 with it). */
TEST(the_latch_moves_exactly_one_cell_of_the_eight)
{
    static const struct {
        bool        dirFull;
        const char *suspend;
        bool        tornDown;
        BenchDiag   expectNew;
        BenchDiag   expectLatched;
        bool        expectAgree;
    } table[] = {
        { false, NULL,              true,  DIAG_GENERIC_TIMEOUT,
          DIAG_ARM_TORN_DOWN,    false },                      /* <- #988   */
        { false, NULL,              false, DIAG_GENERIC_TIMEOUT,
          DIAG_GENERIC_TIMEOUT,  true  },
        { false, kReasonWifiStream, true,  DIAG_SUSPEND_REASON,
          DIAG_SUSPEND_REASON,   true  },                      /* <- order  */
        { false, kReasonWifiStream, false, DIAG_SUSPEND_REASON,
          DIAG_SUSPEND_REASON,   true  },
        { true,  NULL,              true,  DIAG_STARTUP_DIR_FULL,
          DIAG_STARTUP_DIR_FULL, true  },                      /* <- #690   */
        { true,  NULL,              false, DIAG_STARTUP_DIR_FULL,
          DIAG_STARTUP_DIR_FULL, true  },
        { true,  kReasonWifiStream, true,  DIAG_STARTUP_DIR_FULL,
          DIAG_STARTUP_DIR_FULL, true  },
        { true,  kReasonWifiStream, false, DIAG_STARTUP_DIR_FULL,
          DIAG_STARTUP_DIR_FULL, true  },
    };
    size_t i;
    unsigned moved = 0;

    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        BenchEnv newEnv, latchedEnv;
        BenchVerdict newV, latchedV;

        env_init_torn(&newEnv, table[i].dirFull, table[i].suspend,
                      table[i].tornDown);
        env_init_torn(&latchedEnv, table[i].dirFull, table[i].suspend,
                      table[i].tornDown);

        newV     = new_bench_not_ready_diagnosis(&newEnv);
        latchedV = latched_bench_not_ready_diagnosis(&latchedEnv);

        ASSERT_EQ(newV.diag, table[i].expectNew);
        ASSERT_EQ(latchedV.diag, table[i].expectLatched);
        ASSERT_EQ(newV.diag == latchedV.diag, table[i].expectAgree);
        if (newV.diag != latchedV.diag) {
            moved++;
        }

        /* #988 inherits #953's single-sample rule -- one call on every path,
         * including the two arms that then discard the value. */
        ASSERT_EQ(latchedEnv.suspendReasonCalls, 1);
    }

    /* The headline property, one dimension wider: #988 is surgical too. */
    ASSERT_EQ(moved, 1);
}


int main(void)
{
    printf("#953/#988 -- SD:BENCHmark file-not-ready diagnosis "
           "(extracted cascade + poll loop)\n");
    printf("---------------------------------------------\n");
    RUN(suspend_during_wait_is_diagnosed_not_blamed_on_the_card);
    RUN(recorded_dir_full_refusal_outranks_a_later_suspend);
    RUN(dir_full_without_suspend_is_unchanged_by_953);
    RUN(no_suspend_no_dir_full_still_reports_the_card);
    RUN(exactly_one_quadrant_moves_and_why_is_sampled_once);
    RUN(torn_down_with_no_observable_cause_is_not_blamed_on_the_card);
    RUN(torn_down_by_a_cause_that_has_lifted_is_still_a_teardown);
    RUN(torn_down_with_a_live_cause_still_names_the_cause);
    RUN(dir_full_that_also_tore_the_arm_down_keeps_its_verdict);
    RUN(a_card_that_never_opens_the_file_does_not_latch);
    RUN(torn_down_after_the_last_poll_sample_is_still_a_teardown);
    RUN(the_reconciliation_ors_it_does_not_overwrite_the_latch);
    RUN(the_latch_moves_exactly_one_cell_of_the_eight);
    return TEST_SUMMARY();
}
