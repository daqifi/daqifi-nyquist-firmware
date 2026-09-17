/* ==========================================================================
 * test_1121_start_streaming_arm_cascade.c -- issue #1121
 *
 * WHAT IS UNDER TEST
 *
 * SYST:STR:START (SCPI_StartStreamingClaimed, SCPIInterface.c) arms an SD write
 * for a streaming log and then waits for the SD task to open the file. When
 * that wait ends with the file still not ready, one branch has to say WHY.
 * Until #1121 it had three arms (SCPIInterface.c, pre-fix):
 *
 *     if (!sd_card_manager_IsWriteReady()) {
 *         if (sd_card_manager_StartupDirFull()) {
 *             LOG_E("SD start refused (#689): %s",
 *                   sd_card_manager_WriteRefuseText());
 *         } else if (sd_card_manager_StartupDiskFull()) {
 *             LOG_E("[SD] STR:START refused: %llu B free < %llu B floor", ...);
 *         } else {
 *             LOG_E("SD file not ready after %d ms\r\n", readyWait * 10);
 *         }
 *         ...
 *     }
 *
 * That is the shape SYST:STOR:SD:BENCHmark had before #953 and #988 fixed its
 * twin, plus this site's own extra disk-full arm. `grep readyWait firmware/src`
 * returns exactly two functions, so the sibling set is closed at two and this
 * was the one left uncovered when #988 shipped.
 *
 * #1121 PORTS #953's AND #988's ARMS ONTO THIS SITE. The cascade becomes five
 * arms, and the ORDER is the substance:
 *
 *     armTornDown = armTornDown || (pSDCardSettings->mode != MODE_WRITE);
 *     const char *why = SD_SuspendReasonText();
 *     if (sd_card_manager_StartupDirFull())        -> #689/#690 refuse text
 *     else if (sd_card_manager_StartupDiskFull())  -> #498/#851 free-vs-floor
 *     else if (why != NULL)                        -> live suspend reason (#953)
 *     else if (armTornDown)                        -> "the write arm was torn
 *                                                      down"            (#988)
 *     else                                         -> the generic timeout
 *
 * `armTornDown` is latched by the ready-wait poll loop the instant it observes
 * `pSDCardSettings->mode != SD_CARD_MANAGER_MODE_WRITE` -- a transition away
 * from the arm THIS callback published a few lines earlier (SCPIInterface.c,
 * `pSDCardSettings->mode = SD_CARD_MANAGER_MODE_WRITE` immediately before
 * sd_card_manager_UpdateSettingsForStreamingLog), which nothing in this
 * callback ever restores. That is the #988 distinction, unchanged by the port:
 * a fact about THIS request, not an ambient condition.
 *
 * WHAT IS DIFFERENT AT THIS SITE, AND WHY IT NEEDED ITS OWN TEST
 *
 * The benchmark twin has FOUR arms; this one has FIVE, because
 * SCPI_StartStreamingClaimed also clears and re-reads the #498/#851 disk-full
 * flag. The new arms are inserted BELOW it, and that placement is not a
 * detail -- it is the same argument #953/#988 make for StartupDirFull, applied
 * to a second recorded verdict:
 *
 *   sd_card_manager.c's CHECK_DISK_FULL free-space floor rejection
 *   (:1860-1868) raises startupDiskFull, stores MODE_NONE, and routes to
 *   PROCESS_STATE_ERROR -- in that order, as one refusal. So, exactly like the
 *   #690 OPEN_FILE bucket refusal (:2164-2170), it sets the LATCH too.
 *   `diskFull && tornDown` is the NORMAL shape of the #498 path, not a corner.
 *
 * Put the latch above it and every disk-full refusal is re-diagnosed as "the
 * write arm was torn down ... retry", destroying the one message that carries
 * the numbers (`%llu B free < %llu B floor`). Put the suspend arm above it and
 * a suspension that merely lands AFTER the refusal re-diagnoses it the same
 * way. Both are #953's own mis-diagnosis pointed sideways, and
 * `disk_full_that_also_tore_the_arm_down_keeps_its_verdict` and
 * `recorded_disk_full_refusal_outranks_a_later_suspend` are the tests that
 * fail if anyone tries either.
 *
 * The POLL LOOP differs too: this site's early-exit breaks on
 * `StartupDirFull() || StartupDiskFull()`, where the twin's breaks on
 * StartupDirFull alone. `the_loop_early_exits_on_either_recorded_flag` pins
 * that, and says honestly what it does and does not establish.
 *
 * HOW IT IS TESTED
 *
 * SCPIInterface.c is not host-compilable -- 42 direct includes spanning
 * Harmony PLIB, the USB HS and WINC WiFi drivers, libscpi and FreeRTOS's
 * MIPS-specific port layer (established by test_999, which tried) -- so this
 * file follows test_953_bench_suspend_diagnosis.c: it re-implements the cascade
 * SHAPES (pre-#1121 and post-#1121) and the parts of the poll loop that DECIDE
 * the latch as pure functions over injected values, and compares their verdicts
 * on identical inputs. What is proven is the DECISION ALGEBRA, which is all
 * #1121 changes.
 *
 * The RECONCILIATION READ is modelled as its own step (`start_reconcile`) for
 * the same reason the twin's is: the loop samples `mode` at the top of its body
 * and then yields 10 ms before its `readyWait < 500` bound is re-tested, so a
 * teardown landing in that final delay -- or between the `while` condition's
 * IsWriteReady() and the separate call on the line below it -- is latched by
 * nobody, and the cascade would walk past arm 4 into the generic timeout. Its
 * sample is strictly later than polls[last] and has to be supplied separately.
 *
 * A bench test is not the alternative here; it is not available, for the reason
 * test_953's header sets out at length for the identical race shape (the window
 * that must be hit is between the `mode = MODE_WRITE` arm and the SD task
 * opening the file -- tens of milliseconds on a healthy card -- so the pass/fail
 * of such a run would be a property of SCPI scheduling jitter, and the way to
 * widen it is to make the open slow, which is the card fault the arms exist to
 * NOT be confused with). #1121's own ticket carries the same disposition. A
 * deterministic host test of the decision is the strictly stronger thing
 * available.
 *
 * CASES -- the sixteen cells of (dirFull x diskFull x suspended x tornDown)
 *
 *   dirFull  diskFull  susp  torn | OLD               NEW                move?
 *   ---------------------------------------------------------------------------
 *   false    false     false false| GENERIC_TIMEOUT   GENERIC_TIMEOUT     no
 *   false    false     false true | GENERIC_TIMEOUT   ARM_TORN_DOWN     YES <-#988
 *   false    false     true  false| GENERIC_TIMEOUT   SUSPEND_REASON    YES <-#953
 *   false    false     true  true | GENERIC_TIMEOUT   SUSPEND_REASON    YES <-#953
 *   false    true      *     *    | STARTUP_DISK_FULL STARTUP_DISK_FULL   no (x4)
 *   true     *         *     *    | STARTUP_DIR_FULL  STARTUP_DIR_FULL    no (x8)
 *
 * EXACTLY THREE cells move, and all three sit in the quadrant where nothing was
 * recorded -- the only quadrant #1121 is allowed to touch. The other thirteen
 * are the #689/#690, #498/#851 and genuine-card verdicts, each of which some
 * plausible reordering would take out:
 *
 *   hoist `why != NULL` above diskFull  -> the 2 suspended diskFull cells move
 *   hoist `why != NULL` above dirFull   -> the 4 suspended dirFull cells move
 *   hoist `armTornDown` above `why`     -> cell 4 moves (and #953 is reverted)
 *   hoist `armTornDown` above diskFull  -> the 2 torn-down diskFull cells move
 *   hoist `armTornDown` above dirFull   -> the 4 torn-down dirFull cells move
 *
 * `the_cascade_moves_exactly_three_cells_of_the_sixteen` is the table, and each
 * reorder above also has a named test of its own so a failure says which rule
 * was broken rather than only that the count changed.
 *
 * FIDELITY -- what the extracted functions are NOT
 *
 * 1. IsWriteReady() is not a parameter: the branch under test is the body of
 *    `if (!sd_card_manager_IsWriteReady())`, so within it that predicate is
 *    false by construction. Modelling it would only add cells in which neither
 *    shape is reached.
 * 2. The vTaskDelay cadence, the 5 s / 500-iteration bound and the iteration
 *    count are out of scope -- unchanged by #1121. A poll script's LENGTH
 *    stands in for the bound (running off its end is "the wait timed out") and
 *    no test here asserts anything about how long the wait takes.
 * 3. The tail shared by all five arms -- SCPI_ReleaseSdLoggingArm(),
 *    SCPI_UnpublishStartInterface(), SCPI_ErrorPush(),
 *    SCPI_ClearStreamingOperBits() and the `return SCPI_RES_ERR` -- is
 *    unchanged by #1121 and runs whichever arm fires, so it is not modelled.
 * 4. The log TEXT of each arm is not asserted verbatim, only which arm fires
 *    and that the suspend arm passes its reason string through unaltered -- the
 *    real LOG_E interpolates `why` directly, so a dropped or truncated pointer
 *    is the failure mode worth pinning. The #988 arm carries `text == NULL`
 *    here even though the real one interpolates two strings
 *    (sd_card_manager_GetStateName() / GetModeName()), and so does the
 *    disk-full arm (which formats two uint64_t, not a passed-through pointer):
 *    both are breadcrumbs sampled at log time that do not steer the branch.
 * 5. SD_SuspendReasonText()'s own four-way choice is NOT retested here -- it is
 *    unchanged by #1121, it has its own suite (test_985), and its output is
 *    opaque to this cascade, which only tests it against NULL. Its real strings
 *    are used as inputs so a caller-visible truncation would still show up.
 * 6. The LENGTHS of the strings the cascade prints are deliberately not
 *    measured here, matching test_953's decision and for the same reason: a
 *    guard has to measure the REAL strings rather than copies, and three
 *    attempts at a cheaper mechanism were each defeated in review. That is
 *    #1001, and test_1000_sd_log_arm_budget.c is the one call site it has been
 *    built for so far. The two messages #1121 adds were measured by hand
 *    against Logger's effective 125-byte ceiling (LOG_MESSAGE_SIZE 128,
 *    vsnprintf bound - 2, clamp - 3) when the change was made -- 118 bytes
 *    worst case for the reused "Cannot start SD logging - SD suspended: %s"
 *    prefix with the 76-byte quarantine reason, and 116 for the new torn-down
 *    message with an 8-character state and an 8-character mode (CURDRIVE /
 *    GETSPACE, the longest sd_card_manager_GetStateName() and GetModeName()
 *    can return) -- and those numbers are recorded at the firmware site.
 * ========================================================================== */

#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>
#include "test_framework.h"

/* ==========================================================================
 * The diagnoses the branch can reach. Appended to, never renumbered -- the
 * tests below read these by name, and a renumber would make a stale object
 * file compare green against the wrong arm.
 * ========================================================================== */
typedef enum {
    /* LOG_E("SD start refused (#689): %s", WriteRefuseText()) -- #689/#690. */
    DIAG_STARTUP_DIR_FULL = 0,
    /* LOG_E("[SD] STR:START refused: %llu B free < %llu B floor", ...) and its
     * "space unknown" sibling -- #498/#851. No equivalent in the SD:BENCHmark
     * twin; the free-space floor is a streaming-start gate. */
    DIAG_STARTUP_DISK_FULL,
    /* LOG_E("Cannot start SD logging - SD suspended: %s", why) -- #953. */
    DIAG_SUSPEND_REASON,
    /* LOG_E("SD start refused: the write arm was torn down before the file
     * opened (SD now state=%s mode=%s) - retry") -- #988. */
    DIAG_ARM_TORN_DOWN,
    /* LOG_E("SD file not ready after %d ms", readyWait * 10). */
    DIAG_GENERIC_TIMEOUT
} StartDiag;

/* What the branch decided, plus the string it would interpolate. `text` is the
 * pointer the arm passes to LOG_E: WriteRefuseText() for DIR_FULL, `why` for
 * SUSPEND_REASON, NULL for the three arms whose message interpolates something
 * other than a passed-through pointer (FIDELITY 4). */
typedef struct {
    StartDiag   diag;
    const char *text;
} StartVerdict;

/* ==========================================================================
 * Injected environment. Stands in for the four firmware calls the cascade
 * makes; IsWriteReady() is absent on purpose (FIDELITY 1).
 * ========================================================================== */
typedef struct {
    /* sd_card_manager_StartupDirFull() */
    bool        startupDirFull;
    /* sd_card_manager_StartupDiskFull() */
    bool        startupDiskFull;
    /* SD_SuspendReasonText() -- NULL means "not suspended". */
    const char *suspendReason;
    /* sd_card_manager_WriteRefuseText() -- never NULL in the firmware; its
     * SD_REFUSE_NONE arm returns "no refusal recorded". */
    const char *writeRefuseText;
    /* How many times the cascade consulted SD_SuspendReasonText(). The real
     * code samples it ONCE, ahead of the cascade, so the arm that logs cannot
     * print a reason other than the one that steered the branch. */
    unsigned    suspendReasonCalls;
    /* #1121/#988: the `armTornDown` local the ready-wait poll loop latches.
     * Not a firmware CALL like the others -- it is a value the loop computed,
     * which is why start_run_wait() exists to compute it rather than letting
     * every test assert it into place by hand. The pre-#1121 shape is blind to
     * it. */
    bool        armTornDown;
} StartEnv;

static bool mock_startup_dir_full(StartEnv *env)
{
    return env->startupDirFull;
}

static bool mock_startup_disk_full(StartEnv *env)
{
    return env->startupDiskFull;
}

static const char *mock_suspend_reason_text(StartEnv *env)
{
    env->suspendReasonCalls++;
    return env->suspendReason;
}

static const char *mock_write_refuse_text(StartEnv *env)
{
    return env->writeRefuseText;
}

/* ==========================================================================
 * The two cascade shapes, extracted from SCPIInterface.c with the four
 * firmware calls replaced by their mocks. Nothing else changed: same tests,
 * same order, same arms.
 * ========================================================================== */

/* PRE-#1121: three arms. Neither a suspension nor a teardown is visible to it,
 * so both land in the `else` and are reported as a generic timeout -- the
 * message that says only how long the wait was. */
static StartVerdict old_start_not_ready_diagnosis(StartEnv *env)
{
    StartVerdict v;
    if (mock_startup_dir_full(env)) {
        v.diag = DIAG_STARTUP_DIR_FULL;
        v.text = mock_write_refuse_text(env);
    } else if (mock_startup_disk_full(env)) {
        v.diag = DIAG_STARTUP_DISK_FULL;
        v.text = NULL;
    } else {
        v.diag = DIAG_GENERIC_TIMEOUT;
        v.text = NULL;
    }
    return v;
}

/* POST-#1121: five arms. `why` is sampled once, ahead of the cascade; both
 * recorded verdicts still win over the live suspend condition, and the suspend
 * condition still wins over the torn-down latch -- BELOW it, not above, for the
 * reason set out in the file header and at length in test_953's. */
static StartVerdict new_start_not_ready_diagnosis(StartEnv *env)
{
    StartVerdict v;
    const char *why = mock_suspend_reason_text(env);
    if (mock_startup_dir_full(env)) {
        v.diag = DIAG_STARTUP_DIR_FULL;
        v.text = mock_write_refuse_text(env);
    } else if (mock_startup_disk_full(env)) {
        v.diag = DIAG_STARTUP_DISK_FULL;
        v.text = NULL;
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
 * The ready-wait POLL LOOP, as a script of per-iteration observations.
 *
 * Extracted from SCPIInterface.c with the same discipline as the cascades
 * above -- same tests, same order:
 *
 *     while (!sd_card_manager_IsWriteReady() && readyWait < 500) {
 *         if (sd_card_manager_StartupDirFull() ||
 *             sd_card_manager_StartupDiskFull()) {
 *             break;
 *         }
 *         if (pSDCardSettings->mode != SD_CARD_MANAGER_MODE_WRITE) {
 *             armTornDown = true;
 *         }
 *         vTaskDelay(pdMS_TO_TICKS(10));
 *         readyWait++;
 *     }
 *
 * Note the early-exit tests BOTH recorded flags here, where the SD:BENCHmark
 * twin's tests StartupDirFull alone. That is pre-existing at this site and
 * untouched by #1121, but the model has to match it or the two drift.
 * ========================================================================== */
typedef struct {
    /* sd_card_manager_StartupDirFull() as this iteration would read it. */
    bool        startupDirFull;
    /* sd_card_manager_StartupDiskFull(), likewise. */
    bool        startupDiskFull;
    /* pSDCardSettings->mode == SD_CARD_MANAGER_MODE_WRITE, i.e. is THIS
     * request's arm still standing at this iteration? */
    bool        modeIsWrite;
    /* SD_SuspendReasonText() as it would read at this iteration. The loop never
     * calls it -- only the cascade does, once, at the end -- so this is
     * consumed from the LAST iteration the loop reached. */
    const char *suspendReason;
} StartPoll;

/* ==========================================================================
 * Fixtures
 *
 * The suspend strings are SD_SuspendReasonText()'s real returns
 * (SCPIStorageSD.c). They are inputs only -- this file does not test which of
 * the four that function picks (FIDELITY 5) -- but using the real text means a
 * pass-through that truncated or rewrote the reason would be visible. All FOUR
 * are used, including the #985 "no owner in this snapshot" arm that predates
 * neither #953's test nor this one but is reachable today.
 * ========================================================================== */
static const char *const kReasonWifiStream =
    "WiFi streaming owns SPI4 - SYST:STR:STOP first";
static const char *const kReasonFwUpdate =
    "a WiFi firmware update owns SPI4 - retry when it completes";
static const char *const kReasonQuarantine =
    "SD quarantined after a bus jam - reseat the card, "
    "then SYST:STOR:SD:ENAble 1";
static const char *const kReasonNotResumed =
    "the SD task has not resumed yet - retry shortly";

/* Deliberately the real four, not a representative one -- nothing about either
 * ported fix is specific to a cause, and a shape that only handled one would
 * pass a single-case version of every sweep below. */
static const char *const kAllReasons[] = {
    kReasonWifiStream, kReasonFwUpdate, kReasonQuarantine, kReasonNotResumed
};
#define N_REASONS (sizeof(kAllReasons) / sizeof(kAllReasons[0]))

/* sd_card_manager_WriteRefuseText()'s SD_REFUSE_BUCKETS_EXHAUSTED arm. */
static const char *const kRefuseBucketsExhausted =
    "every directory bucket is full - use a different directory "
    "or clear the card";

static void env_init(StartEnv *env, bool dirFull, bool diskFull,
                     const char *suspendReason)
{
    env->startupDirFull     = dirFull;
    env->startupDiskFull    = diskFull;
    env->suspendReason      = suspendReason;
    env->writeRefuseText    = kRefuseBucketsExhausted;
    env->suspendReasonCalls = 0;
    env->armTornDown        = false;
}

/* Same, plus the latch -- for the pure-cascade table, where the four inputs are
 * enumerated directly rather than derived from a poll script. */
static void env_init_torn(StartEnv *env, bool dirFull, bool diskFull,
                          const char *suspendReason, bool armTornDown)
{
    env_init(env, dirFull, diskFull, suspendReason);
    env->armTornDown = armTornDown;
}

/* Runs the extracted poll loop over `polls`, then fills `out` with what the
 * cascade would sample immediately afterwards: the two recorded flags and the
 * suspend reason as they read at the instant the wait ended, plus the latched
 * armTornDown.
 *
 * "The instant the wait ended" is polls[last]: the early-exit exits ON the
 * iteration that observed a flag, and a run to the end of the script exits with
 * the final iteration's state still current. */
static void start_run_wait(const StartPoll *polls, size_t n, StartEnv *out)
{
    bool   armTornDown = false;
    size_t last = 0;
    size_t i;

    if (n == 0u) {
        /* No caller does this -- every script below is a non-empty literal --
         * but without the guard an empty one would read polls[0]. A wait that
         * polled nothing observed nothing. */
        env_init(out, false, false, NULL);
        return;
    }

    for (i = 0; i < n; i++) {
        last = i;
        if (polls[i].startupDirFull || polls[i].startupDiskFull) {
            break;                              /* #689/#690 + #498 early-exit */
        }
        if (!polls[i].modeIsWrite) {
            armTornDown = true;                 /* #988 latch -- and no break */
        }
    }

    env_init_torn(out, polls[last].startupDirFull, polls[last].startupDiskFull,
                  polls[last].suspendReason, armTornDown);
}

/* The RECONCILIATION READ, as its own step -- which is what it is in the
 * firmware too:
 *
 *     if (!sd_card_manager_IsWriteReady()) {
 *         armTornDown = armTornDown ||
 *                       (pSDCardSettings->mode != SD_CARD_MANAGER_MODE_WRITE);
 *         const char *why = SD_SuspendReasonText();
 *         ...
 *
 * NOT folded into start_run_wait() on purpose: the loop's last sample and the
 * loop's EXIT are different instants, so `modeIsWrite` at the cascade is a
 * genuinely later observation than polls[last].modeIsWrite and has to be
 * supplied separately. A model that reused polls[last] for it could not express
 * the boundary case at all.
 *
 * Calling this is the post-#1121 shape; not calling it is the shape that would
 * ship if only the in-loop latch had been ported. The tests below run the same
 * script through both, which is the whole proof.
 *
 * The OR is modelled, not an assignment, because the firmware's is an OR and
 * the difference is load-bearing -- see
 * `the_reconciliation_ors_it_does_not_overwrite_the_latch`. */
static void start_reconcile(StartEnv *env, bool modeIsWriteAtCascade)
{
    if (!modeIsWriteAtCascade) {
        env->armTornDown = true;
    }
}

/* ==========================================================================
 * Tests -- #953's arm, ported
 * ========================================================================== */

/* THE BUG, half one. The arm succeeded, then a suspend landed inside the wait
 * window, so the SD task never reached OPEN_FILE and never recorded anything:
 * both startup flags are false because the callback cleared them synchronously
 * just before arming (sd_card_manager_ClearStartupDirFull() /
 * ClearStartupDiskFull(), SCPIInterface.c) and the only thing that could set
 * either is the task that stopped running.
 *
 * OLD reports "SD file not ready after 5000 ms" -- true and useless. NEW
 * reports the reason the bus is unavailable, and the command that clears it.
 * This is the assertion that screams if the #953 arm is reverted.
 *
 * Swept over all four suspend causes: nothing about the fix is specific to one,
 * and a shape that only handled one would pass a single-case version. */
TEST(suspend_during_wait_is_not_blamed_on_a_generic_timeout)
{
    size_t i;

    for (i = 0; i < N_REASONS; i++) {
        StartEnv oldEnv, newEnv;
        StartVerdict oldV, newV;

        env_init(&oldEnv, false, false, kAllReasons[i]);
        env_init(&newEnv, false, false, kAllReasons[i]);

        oldV = old_start_not_ready_diagnosis(&oldEnv);
        newV = new_start_not_ready_diagnosis(&newEnv);

        /* The defect: the shipped code says only how long it waited. */
        ASSERT_EQ(oldV.diag, DIAG_GENERIC_TIMEOUT);
        ASSERT_TRUE(oldV.text == NULL);

        /* The fix: it names the owner of SPI4 instead. */
        ASSERT_EQ(newV.diag, DIAG_SUSPEND_REASON);
        ASSERT_TRUE(newV.diag != oldV.diag);

        /* The reason must reach LOG_E intact -- same pointer, and byte-equal
         * over its WHOLE length. Pointer identity alone would pass a shape that
         * logged a different string; strcmp alone would pass one that copied
         * into a buffer and truncated at some length matching a prefix. Both,
         * and the length, pin it.
         *
         * The content checks sit behind a NULL guard rather than inside the
         * assertions: a mutant that reverts the fix outright reaches here with
         * text == NULL, and this suite must REPORT that (the ASSERT_TRUE above
         * does) rather than segfault in strlen and lose its own summary. */
        ASSERT_TRUE(newV.text == kAllReasons[i]);
        if (newV.text != NULL) {
            ASSERT_TRUE(strcmp(newV.text, kAllReasons[i]) == 0);
            ASSERT_EQ(strlen(newV.text), strlen(kAllReasons[i]));
        }
    }
}

/* THE ORDERING DECISION AGAINST ARM 1.
 *
 * Both conditions true: the SD task DID run, DID attempt the open and refused
 * it for a #689 reason (that is the only way startupDirFull becomes true), and
 * a suspend landed afterwards. The refusal is the causally-prior, recorded,
 * request-specific fact; the suspend is a later ambient condition. Report the
 * refusal.
 *
 * Flip the suspend arm above the dir-full arm in
 * new_start_not_ready_diagnosis and this test is what fails. */
TEST(recorded_dir_full_refusal_outranks_a_later_suspend)
{
    size_t i;

    for (i = 0; i < N_REASONS; i++) {
        StartEnv oldEnv, newEnv;
        StartVerdict oldV, newV;

        env_init(&oldEnv, true, false, kAllReasons[i]);
        env_init(&newEnv, true, false, kAllReasons[i]);

        oldV = old_start_not_ready_diagnosis(&oldEnv);
        newV = new_start_not_ready_diagnosis(&newEnv);

        ASSERT_EQ(newV.diag, DIAG_STARTUP_DIR_FULL);
        ASSERT_TRUE(newV.diag != DIAG_SUSPEND_REASON);
        ASSERT_EQ(oldV.diag, newV.diag);

        /* And it carries the #689 refuse text, not the suspend reason. */
        ASSERT_TRUE(newV.text == kRefuseBucketsExhausted);
        ASSERT_TRUE(newV.text != kAllReasons[i]);
        ASSERT_TRUE(oldV.text == newV.text);
    }
}

/* THE ORDERING DECISION AGAINST ARM 2, which the SD:BENCHmark twin has no
 * equivalent of and which is therefore the reason this file exists rather than
 * a case being bolted onto test_953.
 *
 * startupDiskFull is a recorded verdict on exactly the same footing as
 * startupDirFull: the callback clears it synchronously before arming, and the
 * only writer is the SD task's own CHECK_DISK_FULL free-space rejection
 * (sd_card_manager.c:1860-1868), which cannot run while that task is suspended.
 * So a `true` here PROVES the SD task ran, read the card's free space, and
 * refused for a named, numeric reason -- strictly more than "something owns
 * SPI4 right now".
 *
 * Hoist the suspend arm above the disk-full arm and this is what fails, taking
 * the `%llu B free < %llu B floor` message (#851, which exists precisely so the
 * operator gets the numbers rather than a bare verdict) with it. */
TEST(recorded_disk_full_refusal_outranks_a_later_suspend)
{
    size_t i;

    for (i = 0; i < N_REASONS; i++) {
        StartEnv oldEnv, newEnv;
        StartVerdict oldV, newV;

        env_init(&oldEnv, false, true, kAllReasons[i]);
        env_init(&newEnv, false, true, kAllReasons[i]);

        oldV = old_start_not_ready_diagnosis(&oldEnv);
        newV = new_start_not_ready_diagnosis(&newEnv);

        ASSERT_EQ(newV.diag, DIAG_STARTUP_DISK_FULL);
        ASSERT_TRUE(newV.diag != DIAG_SUSPEND_REASON);
        ASSERT_TRUE(newV.diag != DIAG_ARM_TORN_DOWN);
        ASSERT_EQ(oldV.diag, newV.diag);
        /* The arm formats two uint64_t rather than passing a pointer through
         * (FIDELITY 4), so there is no text to compare -- but it must not have
         * picked up the suspend reason either. */
        ASSERT_TRUE(newV.text == NULL);
    }
}

/* The two recorded verdicts against EACH OTHER. Pre-existing precedence, wholly
 * untouched by #1121, asserted so a reorder of the pair while inserting the new
 * arms between them and the generic fallback cannot pass silently. Dir-full
 * first is what shipped and what the #689 message's own wording assumes. */
TEST(dir_full_outranks_disk_full_when_both_are_recorded)
{
    StartEnv oldEnv, newEnv;
    StartVerdict oldV, newV;

    env_init(&oldEnv, true, true, NULL);
    env_init(&newEnv, true, true, NULL);

    oldV = old_start_not_ready_diagnosis(&oldEnv);
    newV = new_start_not_ready_diagnosis(&newEnv);

    ASSERT_EQ(oldV.diag, DIAG_STARTUP_DIR_FULL);
    ASSERT_EQ(newV.diag, DIAG_STARTUP_DIR_FULL);
    ASSERT_TRUE(newV.text == kRefuseBucketsExhausted);
    ASSERT_TRUE(oldV.text == newV.text);
}

/* Regression guard on the honest timeout. No recorded verdict, no suspend, no
 * teardown: the SD task was running, recorded nothing, nothing owned the bus,
 * this request's arm was still standing -- and the open still never completed.
 * That is the one case where "not ready after N ms" is all there is to say, and
 * #1121 must not have swallowed it.
 *
 * Together with the first test this is what stops the lazy mutation "always
 * take the new suspend arm", which would satisfy that one while destroying the
 * only diagnosis this branch got right all along. */
TEST(nothing_recorded_nothing_owned_arm_alive_still_reports_the_timeout)
{
    StartEnv oldEnv, newEnv;
    StartVerdict oldV, newV;

    env_init(&oldEnv, false, false, NULL);
    env_init(&newEnv, false, false, NULL);

    oldV = old_start_not_ready_diagnosis(&oldEnv);
    newV = new_start_not_ready_diagnosis(&newEnv);

    ASSERT_EQ(oldV.diag, DIAG_GENERIC_TIMEOUT);
    ASSERT_EQ(newV.diag, DIAG_GENERIC_TIMEOUT);
    ASSERT_TRUE(oldV.text == NULL);
    ASSERT_TRUE(newV.text == NULL);
}

/* ==========================================================================
 * Tests -- #988's arm, ported. These drive the POLL LOOP (start_run_wait)
 * rather than asserting the cascade's inputs into place, because two of them
 * are only distinguishable as time series: at the cascade both present
 * (dirFull=false, diskFull=false, why=NULL, tornDown=true).
 * ========================================================================== */

/* THE POWER-STATE GAP, which is the case #1121's ticket names first.
 *
 * app_SDCardTask's APP_SD_STATE_PROCESS sees the power state leave POWERED_UP,
 * calls app_SDCard_GracefulShutdown() -- which stores MODE_NONE over this
 * start's arm -- and goes to APP_SD_STATE_WAIT_POWER_UP. It never enters
 * APP_SD_STATE_SUSPENDED, and the flag it publishes each iteration is
 * `state == SUSPENDED || app_SDCard_SpiOwnedByWifi()`, so SD_SuspendReasonText()
 * observes NOTHING on this path at any point. The arm is just as dead as under
 * a WiFi suspend.
 *
 * Pre-#1121 that lands in the generic arm: "SD file not ready after 5000 ms",
 * for a device that powered down. This is the assertion that screams if the
 * #988 arm is reverted. */
TEST(torn_down_with_no_observable_cause_is_not_reported_as_a_timeout)
{
    static const StartPoll kPolls[] = {
        /* dirFull, diskFull, modeIsWrite, suspendReason */
        { false,    false,    true,        NULL },  /* armed, awaiting the open */
        { false,    false,    true,        NULL },
        { false,    false,    false,       NULL },  /* GracefulShutdown -> NONE  */
        { false,    false,    false,       NULL },  /* nothing restores it       */
        { false,    false,    false,       NULL },  /* ...and no owner nameable  */
    };
    const size_t n = sizeof(kPolls) / sizeof(kPolls[0]);
    StartEnv oldEnv, newEnv;
    StartVerdict oldV, newV;

    start_run_wait(kPolls, n, &oldEnv);
    start_run_wait(kPolls, n, &newEnv);

    /* The loop is what establishes the fact; assert it did, so a later failure
     * cannot be mistaken for a cascade bug when it is a loop bug. */
    ASSERT_TRUE(newEnv.armTornDown);
    ASSERT_FALSE(newEnv.startupDirFull);
    ASSERT_FALSE(newEnv.startupDiskFull);
    ASSERT_TRUE(newEnv.suspendReason == NULL);

    oldV = old_start_not_ready_diagnosis(&oldEnv);
    newV = new_start_not_ready_diagnosis(&newEnv);

    ASSERT_EQ(oldV.diag, DIAG_GENERIC_TIMEOUT);
    ASSERT_EQ(newV.diag, DIAG_ARM_TORN_DOWN);
    ASSERT_TRUE(newV.diag != oldV.diag);

    /* Still exactly one sample of the reason, on an arm that discards it. */
    ASSERT_EQ(newEnv.suspendReasonCalls, 1);
    ASSERT_EQ(oldEnv.suspendReasonCalls, 0);
}

/* THE TRANSIENT QUADRANT: torn down by a cause that has since lifted. A WiFi
 * stream takes SPI4 mid-wait, the SD task tears the arm down on its way into
 * APP_SD_STATE_SUSPENDED, the stream then stops and the task resumes -- so by
 * the time the cascade samples SD_SuspendReasonText() there is nothing left to
 * name, while the arm stays dead (nothing restores MODE_WRITE).
 *
 * At the cascade this is indistinguishable from the case above, which is
 * exactly why the latch has to be computed by the LOOP and not injected: the
 * two differ only in when `why` was non-NULL.
 *
 * It also pins the no-break property from the outside: if the loop broke on the
 * latch it would exit at poll 3, where the reason is still live, and this would
 * return DIAG_SUSPEND_REASON. */
TEST(torn_down_by_a_cause_that_has_lifted_is_still_a_teardown)
{
    static const StartPoll kPolls[] = {
        /* dirFull, diskFull, modeIsWrite, suspendReason */
        { false,    false,    true,        NULL              }, /* bus is ours  */
        { false,    false,    true,        kReasonWifiStream }, /* WiFi claims  */
        { false,    false,    false,       kReasonWifiStream }, /* torn down    */
        { false,    false,    false,       kReasonWifiStream }, /* ...suspended */
        { false,    false,    false,       NULL              }, /* stream stops,*/
        { false,    false,    false,       NULL              }, /*  task resumes*/
    };
    const size_t n = sizeof(kPolls) / sizeof(kPolls[0]);
    StartEnv oldEnv, newEnv;
    StartVerdict oldV, newV;

    start_run_wait(kPolls, n, &oldEnv);
    start_run_wait(kPolls, n, &newEnv);

    /* The wait ran to its end -- no break -- so the cascade sees the LIFTED
     * state, not the state at the teardown. */
    ASSERT_TRUE(newEnv.armTornDown);
    ASSERT_TRUE(newEnv.suspendReason == NULL);

    oldV = old_start_not_ready_diagnosis(&oldEnv);
    newV = new_start_not_ready_diagnosis(&newEnv);

    ASSERT_EQ(oldV.diag, DIAG_GENERIC_TIMEOUT);       /* the open quadrant */
    ASSERT_EQ(newV.diag, DIAG_ARM_TORN_DOWN);         /* closed by the port */
    ASSERT_TRUE(newV.diag != oldV.diag);
}

/* THE ORDERING DECISION AGAINST ARM 3 -- the one that is not obvious.
 *
 * Same teardown, except the cause is still present when the wait ends. The
 * latch is set (it always is on this path -- app_SDCard_GracefulShutdown stores
 * MODE_NONE on its way into APP_SD_STATE_SUSPENDED), and so is the suspend
 * reason. #953's arm must still win: it is the only one that can name the owner
 * AND the command that clears it, where the torn-down arm can only say
 * "something killed it, retry".
 *
 * Hoist `armTornDown` above `why != NULL` and this test fails -- and with it
 * #953, in #953's own headline case, because the teardown and the suspension
 * are published by the SAME app_SDCard_GracefulShutdown() call. Swept over all
 * four causes for the same reason the first test is. */
TEST(torn_down_with_a_live_cause_still_names_the_cause)
{
    size_t i;

    for (i = 0; i < N_REASONS; i++) {
        const StartPoll polls[] = {
            { false, false, true,  NULL           },
            { false, false, false, kAllReasons[i] },  /* torn down, cause live */
            { false, false, false, kAllReasons[i] },
        };
        StartEnv env;
        StartVerdict v;

        start_run_wait(polls, sizeof(polls) / sizeof(polls[0]), &env);

        ASSERT_TRUE(env.armTornDown);
        ASSERT_TRUE(env.suspendReason == kAllReasons[i]);

        v = new_start_not_ready_diagnosis(&env);

        ASSERT_EQ(v.diag, DIAG_SUSPEND_REASON);
        ASSERT_TRUE(v.diag != DIAG_ARM_TORN_DOWN);
        ASSERT_TRUE(v.text == kAllReasons[i]);
    }
}

/* THE ORDERING DECISION AGAINST ARM 1, on the shape the #690 path really has.
 *
 * sd_card_manager.c's OPEN_FILE bucket refusal (:2164-2170) raises
 * startupDirFull AND stores MODE_NONE, in that order, as one refusal. So every
 * real #690 case sets the latch too -- `dirFull && tornDown` is the normal
 * shape, not a corner. The recorded verdict must still win: it names WHY the
 * open was refused, where the latch can only say that something ended the arm.
 *
 * The loop's early-exit fires on that iteration, so the latch is not even set
 * in the first script; the cascade's re-read of startupDirFull is what makes
 * the answer independent of which of the two the poll happened to see first,
 * and the second script covers the other interleaving. */
TEST(dir_full_that_also_tore_the_arm_down_keeps_its_verdict)
{
    static const StartPoll kPolls[] = {
        { false, false, true,  NULL },
        { true,  false, false, NULL },   /* the #690 refusal: both, one iteration */
        { true,  false, false, NULL },
    };
    const size_t n = sizeof(kPolls) / sizeof(kPolls[0]);
    StartEnv oldEnv, newEnv;
    StartVerdict oldV, newV;

    start_run_wait(kPolls, n, &oldEnv);
    start_run_wait(kPolls, n, &newEnv);

    ASSERT_TRUE(newEnv.startupDirFull);

    oldV = old_start_not_ready_diagnosis(&oldEnv);
    newV = new_start_not_ready_diagnosis(&newEnv);

    ASSERT_EQ(newV.diag, DIAG_STARTUP_DIR_FULL);
    ASSERT_TRUE(newV.diag != DIAG_ARM_TORN_DOWN);
    ASSERT_EQ(oldV.diag, newV.diag);
    ASSERT_TRUE(newV.text == kRefuseBucketsExhausted);

    /* The other interleaving: the teardown became visible to the poll BEFORE
     * the flag did. The latch is set, the flag is set, and arm 1 still wins --
     * because the cascade re-reads the flag rather than trusting the loop's
     * exit. This is the case #690 would lose if the latch were hoisted. */
    {
        static const StartPoll kSplit[] = {
            { false, false, true,  NULL },
            { false, false, false, NULL },   /* mode cleared, flag not yet seen */
            { true,  false, false, NULL },   /* flag observed on the next poll  */
        };
        StartEnv splitEnv;
        StartVerdict splitV;

        start_run_wait(kSplit, sizeof(kSplit) / sizeof(kSplit[0]), &splitEnv);
        ASSERT_TRUE(splitEnv.armTornDown);
        ASSERT_TRUE(splitEnv.startupDirFull);

        splitV = new_start_not_ready_diagnosis(&splitEnv);
        ASSERT_EQ(splitV.diag, DIAG_STARTUP_DIR_FULL);
        ASSERT_TRUE(splitV.text == kRefuseBucketsExhausted);
    }
}

/* THE ORDERING DECISION AGAINST ARM 2, on the shape the #498 path really has --
 * and the second thing the SD:BENCHmark twin cannot cover for this site.
 *
 * sd_card_manager.c's CHECK_DISK_FULL free-space rejection (:1860-1868) raises
 * startupDiskFull, then stores MODE_NONE, then routes to PROCESS_STATE_ERROR,
 * as one refusal -- structurally identical to the #690 path above. So
 * `diskFull && tornDown` is the NORMAL shape of every real free-space refusal,
 * not a corner, and the recorded verdict must win: it is the only arm that
 * carries the numbers (#851's `%llu B free < %llu B floor`), where the latch
 * can only say "retry".
 *
 * Hoist `armTornDown` above the disk-full arm and every out-of-space start
 * starts telling the operator to retry -- which will fail identically, because
 * the card is still full. Both interleavings are covered, as above. */
TEST(disk_full_that_also_tore_the_arm_down_keeps_its_verdict)
{
    static const StartPoll kPolls[] = {
        { false, false, true,  NULL },
        { false, true,  false, NULL },   /* the #498 refusal: both, one iteration */
        { false, true,  false, NULL },
    };
    const size_t n = sizeof(kPolls) / sizeof(kPolls[0]);
    StartEnv oldEnv, newEnv;
    StartVerdict oldV, newV;

    start_run_wait(kPolls, n, &oldEnv);
    start_run_wait(kPolls, n, &newEnv);

    ASSERT_TRUE(newEnv.startupDiskFull);
    ASSERT_FALSE(newEnv.startupDirFull);

    oldV = old_start_not_ready_diagnosis(&oldEnv);
    newV = new_start_not_ready_diagnosis(&newEnv);

    ASSERT_EQ(newV.diag, DIAG_STARTUP_DISK_FULL);
    ASSERT_TRUE(newV.diag != DIAG_ARM_TORN_DOWN);
    ASSERT_EQ(oldV.diag, newV.diag);

    /* The other interleaving: the teardown was visible to the poll first. */
    {
        static const StartPoll kSplit[] = {
            { false, false, true,  NULL },
            { false, false, false, NULL },   /* mode cleared, flag not yet seen */
            { false, true,  false, NULL },   /* flag observed on the next poll  */
        };
        StartEnv splitEnv;
        StartVerdict splitV;

        start_run_wait(kSplit, sizeof(kSplit) / sizeof(kSplit[0]), &splitEnv);
        ASSERT_TRUE(splitEnv.armTornDown);
        ASSERT_TRUE(splitEnv.startupDiskFull);

        splitV = new_start_not_ready_diagnosis(&splitEnv);
        ASSERT_EQ(splitV.diag, DIAG_STARTUP_DISK_FULL);
    }

    /* And with a live suspend on top of it, the disk-full verdict still wins
     * over BOTH new arms at once -- the combination a reorder is most likely to
     * get wrong, since each new arm looks individually reasonable. */
    {
        static const StartPoll kSuspended[] = {
            { false, false, true,  NULL },
            { false, true,  false, kReasonFwUpdate },
        };
        StartEnv suspEnv;
        StartVerdict suspV;

        start_run_wait(kSuspended, sizeof(kSuspended) / sizeof(kSuspended[0]),
                       &suspEnv);
        ASSERT_TRUE(suspEnv.startupDiskFull);
        ASSERT_TRUE(suspEnv.suspendReason != NULL);

        suspV = new_start_not_ready_diagnosis(&suspEnv);
        ASSERT_EQ(suspV.diag, DIAG_STARTUP_DISK_FULL);
        ASSERT_TRUE(suspV.diag != DIAG_SUSPEND_REASON);
        ASSERT_TRUE(suspV.diag != DIAG_ARM_TORN_DOWN);
    }
}

/* THE TIMEOUT SURVIVES, which is what stops the lazy mutation "latch
 * unconditionally" (or "latch on the first poll").
 *
 * A genuinely failing open is the one failure that does NOT clear `mode`:
 * sd_card_manager.c:2358-2365 takes a failed WRITE-mode SYS_FS_FileOpen to
 * PROCESS_STATE_ERROR and leaves the arm standing, so the machine loops
 * ERROR -> UNMOUNT -> INIT -> ... with mode still MODE_WRITE for the whole five
 * seconds. The latch must stay false and the operator must still get the
 * timeout rather than a spurious "retry". */
TEST(an_open_that_never_completes_does_not_latch)
{
    static const StartPoll kPolls[] = {
        { false, false, true, NULL },
        { false, false, true, NULL },
        { false, false, true, NULL },
        { false, false, true, NULL },
    };
    const size_t n = sizeof(kPolls) / sizeof(kPolls[0]);
    StartEnv oldEnv, newEnv;
    StartVerdict oldV, newV;

    start_run_wait(kPolls, n, &oldEnv);
    start_run_wait(kPolls, n, &newEnv);

    ASSERT_FALSE(newEnv.armTornDown);

    oldV = old_start_not_ready_diagnosis(&oldEnv);
    newV = new_start_not_ready_diagnosis(&newEnv);

    ASSERT_EQ(newV.diag, DIAG_GENERIC_TIMEOUT);
    ASSERT_EQ(oldV.diag, newV.diag);
    ASSERT_TRUE(newV.text == NULL);
}

/* THE SAMPLING BOUNDARY: a teardown the LOOP could never have seen.
 *
 * The loop samples `mode` at the top of its body and then yields 10 ms before
 * its condition is re-tested, so its last observation and its exit are different
 * instants. On the iteration where readyWait is 499 the body samples, delays,
 * increments to 500 -- and `readyWait < 500` ends the loop with no further
 * sample. app_SDCard_GracefulShutdown() running on the SD task inside that final
 * delay stores MODE_NONE over this start's arm and is observed by nobody. (The
 * same gap swallows a teardown landing between the `while` condition's
 * IsWriteReady() call and the separate one on the line below it.)
 *
 * The arm is exactly as dead as in the power-state case -- nothing restores
 * MODE_WRITE -- but `armTornDown` is false, so a port that carried only the
 * in-loop latch walks past its own arm into the generic timeout. That is what
 * the reconciliation read closes, and this is the assertion that screams if it
 * is removed.
 *
 * Note what the script says: EVERY poll observes the arm standing. There is no
 * iteration to which the teardown could be attributed, which is what makes this
 * a different case rather than a re-spelling of the power-state one. */
TEST(torn_down_after_the_last_poll_sample_is_still_a_teardown)
{
    static const StartPoll kPolls[] = {
        /* dirFull, diskFull, modeIsWrite, suspendReason */
        { false,    false,    true,        NULL },  /* armed, awaiting the open  */
        { false,    false,    true,        NULL },
        { false,    false,    true,        NULL },  /* last sample: still ours   */
    };
    const size_t n = sizeof(kPolls) / sizeof(kPolls[0]);
    StartEnv preEnv, postEnv;
    StartVerdict preV, postV;

    /* Same script into both shapes; the reconciliation is the only difference. */
    start_run_wait(kPolls, n, &preEnv);
    start_run_wait(kPolls, n, &postEnv);

    /* The loop latched nothing -- in BOTH -- which is the premise of the case.
     * Assert it, so a failure below cannot be mistaken for a loop bug. */
    ASSERT_FALSE(preEnv.armTornDown);
    ASSERT_FALSE(postEnv.armTornDown);

    /* ...and then the teardown lands in the final delay: MODE_NONE by the time
     * the cascade reads it. */
    start_reconcile(&postEnv, false);
    ASSERT_TRUE(postEnv.armTornDown);

    preV  = new_start_not_ready_diagnosis(&preEnv);
    postV = new_start_not_ready_diagnosis(&postEnv);

    /* Without the reconciliation the port reports a timeout for a teardown. */
    ASSERT_EQ(preV.diag, DIAG_GENERIC_TIMEOUT);
    /* With it, it says what actually happened. */
    ASSERT_EQ(postV.diag, DIAG_ARM_TORN_DOWN);
    ASSERT_TRUE(postV.diag != preV.diag);

    /* Still one sample of the reason, on an arm that discards it. */
    ASSERT_EQ(postEnv.suspendReasonCalls, 1);

    /* THE TIMEOUT GUARD SURVIVES. The reconciliation must not fire merely
     * because it exists: an open that fails without clearing `mode` leaves the
     * read finding MODE_WRITE, and the operator still gets the timeout. This is
     * what stops the lazy mutation "reconcile unconditionally". */
    {
        StartEnv aliveEnv;
        StartVerdict aliveV;

        start_run_wait(kPolls, n, &aliveEnv);
        start_reconcile(&aliveEnv, true);     /* arm still standing at the cascade */
        ASSERT_FALSE(aliveEnv.armTornDown);

        aliveV = new_start_not_ready_diagnosis(&aliveEnv);
        ASSERT_EQ(aliveV.diag, DIAG_GENERIC_TIMEOUT);
        ASSERT_TRUE(aliveV.text == NULL);
    }

    /* THE ARM ORDER IS UNTOUCHED, all three directions. The reconciliation
     * changes only WHEN armTornDown may become true, never which arm consumes
     * it -- so on the three inputs that outrank it, the verdict must be
     * identical to what it was before. Reconciling `false` is the strongest
     * possible push toward arm 4; it must still lose to a recorded #690
     * verdict, to a recorded #498 verdict, and to a live suspend reason. Hoist
     * the reconciliation's effect above any of them and one of these blocks is
     * what fails. */
    {
        static const StartPoll kDirFull[] = {
            { false, false, true, NULL },
            { true,  false, true, NULL },        /* the #690 refusal is recorded */
        };
        StartEnv dirEnv;
        StartVerdict dirV;

        start_run_wait(kDirFull, sizeof(kDirFull) / sizeof(kDirFull[0]), &dirEnv);
        start_reconcile(&dirEnv, false);
        ASSERT_TRUE(dirEnv.armTornDown);
        ASSERT_TRUE(dirEnv.startupDirFull);

        dirV = new_start_not_ready_diagnosis(&dirEnv);
        ASSERT_EQ(dirV.diag, DIAG_STARTUP_DIR_FULL);
        ASSERT_TRUE(dirV.text == kRefuseBucketsExhausted);
    }
    {
        static const StartPoll kDiskFull[] = {
            { false, false, true, NULL },
            { false, true,  true, NULL },        /* the #498 refusal is recorded */
        };
        StartEnv diskEnv;
        StartVerdict diskV;

        start_run_wait(kDiskFull, sizeof(kDiskFull) / sizeof(kDiskFull[0]),
                       &diskEnv);
        start_reconcile(&diskEnv, false);
        ASSERT_TRUE(diskEnv.armTornDown);
        ASSERT_TRUE(diskEnv.startupDiskFull);

        diskV = new_start_not_ready_diagnosis(&diskEnv);
        ASSERT_EQ(diskV.diag, DIAG_STARTUP_DISK_FULL);
    }
    {
        static const StartPoll kSuspended[] = {
            { false, false, true, NULL },
            { false, false, true, kReasonQuarantine },
        };
        StartEnv suspEnv;
        StartVerdict suspV;

        start_run_wait(kSuspended, sizeof(kSuspended) / sizeof(kSuspended[0]),
                       &suspEnv);
        start_reconcile(&suspEnv, false);
        ASSERT_TRUE(suspEnv.armTornDown);
        ASSERT_TRUE(suspEnv.suspendReason != NULL);

        suspV = new_start_not_ready_diagnosis(&suspEnv);
        ASSERT_EQ(suspV.diag, DIAG_SUSPEND_REASON);
        ASSERT_TRUE(suspV.text == suspEnv.suspendReason);
    }
}

/* THE RECONCILIATION IS AN `||`, AND THAT IS THE POINT OF IT.
 *
 * Writing it as `armTornDown = (mode != MODE_WRITE)` passes the case above --
 * and silently destroys the loop's own finding on the interleaving that most
 * needs it. The loop's latch records a FACT about this request ("mode left the
 * MODE_WRITE this callback published"); nothing makes that fact untrue later.
 * But `mode` can read MODE_WRITE again at the cascade, because after the
 * teardown released the manager a DIFFERENT caller is free to claim and arm its
 * own operation (sd_card_manager_TryClaim succeeds once mode is NONE and the
 * machine is idle). An assignment would then overwrite `true` with `false` and
 * hand this request the generic timeout -- reintroducing the bug through the
 * fix for it.
 *
 * This is the test that goes red on that mutation, and it is why the firmware
 * line reads `armTornDown = armTornDown || (...)` and not `armTornDown = (...)`. */
TEST(the_reconciliation_ors_it_does_not_overwrite_the_latch)
{
    static const StartPoll kPolls[] = {
        /* dirFull, diskFull, modeIsWrite, suspendReason */
        { false,    false,    true,        NULL },  /* armed                     */
        { false,    false,    false,       NULL },  /* teardown -- the LOOP sees */
        { false,    false,    false,       NULL },
    };
    const size_t n = sizeof(kPolls) / sizeof(kPolls[0]);
    StartEnv env;
    StartVerdict v;

    start_run_wait(kPolls, n, &env);
    ASSERT_TRUE(env.armTornDown);          /* the loop established the fact */

    /* Another caller has since armed its own WRITE, so the reconciliation's
     * sample reads MODE_WRITE. The latch must survive it. */
    start_reconcile(&env, true);
    ASSERT_TRUE(env.armTornDown);

    v = new_start_not_ready_diagnosis(&env);
    ASSERT_EQ(v.diag, DIAG_ARM_TORN_DOWN);
    ASSERT_TRUE(v.diag != DIAG_GENERIC_TIMEOUT);
}

/* THE LOOP'S EARLY-EXIT TESTS BOTH RECORDED FLAGS, which is where this site's
 * loop differs from the SD:BENCHmark twin's (that one breaks on
 * StartupDirFull alone). Pre-existing and untouched by #1121, pinned here so
 * the model cannot quietly drift from a firmware whose break tests only one.
 *
 * What it asserts is the LATCH, not a verdict, and that is deliberate: because
 * both flags are sticky once the SD task records them, a loop that ran on past
 * a disk-full poll would still hand the cascade diskFull=true and still reach
 * the same arm. The observable difference is the latch -- a loop that does not
 * break keeps polling and picks up the MODE_NONE the same refusal stored. So a
 * break-on-dirFull-only mutation of start_run_wait() turns the first block red,
 * and a break-on-diskFull-only mutation turns the second red, while a verdict
 * assertion would notice neither. */
TEST(the_loop_early_exits_on_either_recorded_flag)
{
    {
        static const StartPoll kDiskFirst[] = {
            { false, false, true,  NULL },
            { false, true,  true,  NULL },  /* recorded: the loop stops HERE */
            { false, true,  false, NULL },  /* never reached -> never latched */
        };
        StartEnv env;

        start_run_wait(kDiskFirst, sizeof(kDiskFirst) / sizeof(kDiskFirst[0]),
                       &env);
        ASSERT_TRUE(env.startupDiskFull);
        ASSERT_FALSE(env.armTornDown);
    }
    {
        static const StartPoll kDirFirst[] = {
            { false, false, true,  NULL },
            { true,  false, true,  NULL },  /* recorded: the loop stops HERE */
            { true,  false, false, NULL },  /* never reached -> never latched */
        };
        StartEnv env;

        start_run_wait(kDirFirst, sizeof(kDirFirst) / sizeof(kDirFirst[0]), &env);
        ASSERT_TRUE(env.startupDirFull);
        ASSERT_FALSE(env.armTornDown);
    }
}

/* The whole truth table in one place, asserted as a table rather than as
 * sixteen hand-written cases -- so "exactly three cells move" is a checked
 * property of the pair of shapes and not a claim in a comment.
 *
 * OLD is blind to both `suspend` and `tornDown`, so it is evaluated on the two
 * recorded flags and compared against NEW on all four inputs. The three cells
 * that move are the ones #1121 was filed for; every other cell is a verdict
 * some plausible reordering would take out, enumerated in the file header. */
TEST(the_cascade_moves_exactly_three_cells_of_the_sixteen)
{
    static const struct {
        bool        dirFull;
        bool        diskFull;
        const char *suspend;
        bool        tornDown;
        StartDiag   expectOld;
        StartDiag   expectNew;
        bool        expectAgree;
    } table[] = {
        /* nothing recorded -- the only quadrant #1121 may touch */
        { false, false, NULL,              false, DIAG_GENERIC_TIMEOUT,
          DIAG_GENERIC_TIMEOUT,   true  },
        { false, false, NULL,              true,  DIAG_GENERIC_TIMEOUT,
          DIAG_ARM_TORN_DOWN,     false },                       /* <- #988   */
        { false, false, kReasonWifiStream, false, DIAG_GENERIC_TIMEOUT,
          DIAG_SUSPEND_REASON,    false },                       /* <- #953   */
        { false, false, kReasonWifiStream, true,  DIAG_GENERIC_TIMEOUT,
          DIAG_SUSPEND_REASON,    false },                       /* <- #953   */

        /* #498/#851 recorded -- must not move, in any combination */
        { false, true,  NULL,              false, DIAG_STARTUP_DISK_FULL,
          DIAG_STARTUP_DISK_FULL, true  },
        { false, true,  NULL,              true,  DIAG_STARTUP_DISK_FULL,
          DIAG_STARTUP_DISK_FULL, true  },
        { false, true,  kReasonWifiStream, false, DIAG_STARTUP_DISK_FULL,
          DIAG_STARTUP_DISK_FULL, true  },
        { false, true,  kReasonWifiStream, true,  DIAG_STARTUP_DISK_FULL,
          DIAG_STARTUP_DISK_FULL, true  },

        /* #689/#690 recorded -- must not move, in any combination */
        { true,  false, NULL,              false, DIAG_STARTUP_DIR_FULL,
          DIAG_STARTUP_DIR_FULL,  true  },
        { true,  false, NULL,              true,  DIAG_STARTUP_DIR_FULL,
          DIAG_STARTUP_DIR_FULL,  true  },
        { true,  false, kReasonWifiStream, false, DIAG_STARTUP_DIR_FULL,
          DIAG_STARTUP_DIR_FULL,  true  },
        { true,  false, kReasonWifiStream, true,  DIAG_STARTUP_DIR_FULL,
          DIAG_STARTUP_DIR_FULL,  true  },
        { true,  true,  NULL,              false, DIAG_STARTUP_DIR_FULL,
          DIAG_STARTUP_DIR_FULL,  true  },
        { true,  true,  NULL,              true,  DIAG_STARTUP_DIR_FULL,
          DIAG_STARTUP_DIR_FULL,  true  },
        { true,  true,  kReasonWifiStream, false, DIAG_STARTUP_DIR_FULL,
          DIAG_STARTUP_DIR_FULL,  true  },
        { true,  true,  kReasonWifiStream, true,  DIAG_STARTUP_DIR_FULL,
          DIAG_STARTUP_DIR_FULL,  true  },
    };
    size_t i;
    unsigned moved = 0;

    /* The table must enumerate the whole cube, or "exactly three move" is a
     * property of whatever subset somebody happened to list. */
    ASSERT_EQ(sizeof(table) / sizeof(table[0]), 16);

    for (i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        StartEnv oldEnv, newEnv;
        StartVerdict oldV, newV;

        env_init_torn(&oldEnv, table[i].dirFull, table[i].diskFull,
                      table[i].suspend, table[i].tornDown);
        env_init_torn(&newEnv, table[i].dirFull, table[i].diskFull,
                      table[i].suspend, table[i].tornDown);

        oldV = old_start_not_ready_diagnosis(&oldEnv);
        newV = new_start_not_ready_diagnosis(&newEnv);

        ASSERT_EQ(oldV.diag, table[i].expectOld);
        ASSERT_EQ(newV.diag, table[i].expectNew);
        ASSERT_EQ(oldV.diag == newV.diag, table[i].expectAgree);
        if (oldV.diag != newV.diag) {
            moved++;
        }

        /* Sampled exactly once per evaluation, on every path -- including the
         * twelve where the value is then discarded because a recorded verdict
         * wins. */
        ASSERT_EQ(newEnv.suspendReasonCalls, 1);
        /* And the old shape never consulted it at all, which is the defect. */
        ASSERT_EQ(oldEnv.suspendReasonCalls, 0);
    }

    /* The headline property: the port is surgical. */
    ASSERT_EQ(moved, 3);
}


int main(void)
{
    printf("#1121 -- SYST:STR:START SD write-arm not-ready diagnosis "
           "(extracted cascade + poll loop)\n");
    printf("---------------------------------------------\n");
    RUN(suspend_during_wait_is_not_blamed_on_a_generic_timeout);
    RUN(recorded_dir_full_refusal_outranks_a_later_suspend);
    RUN(recorded_disk_full_refusal_outranks_a_later_suspend);
    RUN(dir_full_outranks_disk_full_when_both_are_recorded);
    RUN(nothing_recorded_nothing_owned_arm_alive_still_reports_the_timeout);
    RUN(torn_down_with_no_observable_cause_is_not_reported_as_a_timeout);
    RUN(torn_down_by_a_cause_that_has_lifted_is_still_a_teardown);
    RUN(torn_down_with_a_live_cause_still_names_the_cause);
    RUN(dir_full_that_also_tore_the_arm_down_keeps_its_verdict);
    RUN(disk_full_that_also_tore_the_arm_down_keeps_its_verdict);
    RUN(an_open_that_never_completes_does_not_latch);
    RUN(torn_down_after_the_last_poll_sample_is_still_a_teardown);
    RUN(the_reconciliation_ors_it_does_not_overwrite_the_latch);
    RUN(the_loop_early_exits_on_either_recorded_flag);
    RUN(the_cascade_moves_exactly_three_cells_of_the_sixteen);
    return TEST_SUMMARY();
}
