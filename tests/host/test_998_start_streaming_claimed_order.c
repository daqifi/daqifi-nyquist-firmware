/* ==========================================================================
 * test_998_start_streaming_claimed_order.c — the claim/arm/refusal/poll order
 * of SCPI_StartStreamingClaimed()'s SD-logging arm, as a deterministic host
 * model (issue #998).
 *
 * ---------------------------------------------------------------------------
 * THE FIRMWARE CODE THIS MODELS
 * ---------------------------------------------------------------------------
 * firmware/src/services/SCPI/SCPIInterface.c, SCPI_StartStreamingClaimed() --
 * specifically the SD-logging arm inside it. That function is ~1250 lines
 * (one `static scpi_result_t` body with more than twenty return sites), so
 * what is modelled, and what the Makefile pins by content hash, is the slice
 * from its `if (!sd_card_manager_TryClaim()) {` line to the head of its
 * readiness poll. With comments elided for readability here (the pin itself
 * hashes them), that slice is:
 *
 *   if (!sd_card_manager_TryClaim()) {
 *       <clear OPER bits, unpublish the interface, LOG_E, SCPI_ErrorPush>
 *       return SCPI_RES_ERR;
 *   }
 *   sd_card_manager_ClearStartupDirFull();
 *   sd_card_manager_ClearStartupDiskFull();
 *   pSDCardSettings->mode = SD_CARD_MANAGER_MODE_WRITE;
 *   bool sdArmed = sd_card_manager_UpdateSettingsForStreamingLog(pSDCardSettings);
 *   if (!sdArmed) {
 *       pSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE;
 *       sd_card_manager_ReleaseClaim();
 *       <SD_SuspendReasonText, clear OPER bits, unpublish, LOG_E, ErrorPush>
 *       return SCPI_RES_ERR;
 *   }
 *   sd_card_manager_ReleaseClaim();
 *   int readyWait = 0;
 *   while (!sd_card_manager_IsWriteReady() && readyWait < 500) {
 *
 * The bracketed calls are elided above but ARE inside the pinned slice. They
 * carry no SD-ordering content: SCPI_ClearStreamingOperBits and
 * SCPI_UnpublishStartInterface write the STREAMING runtime config
 * (`ActiveInterface`, the OPER bits), which has its own #850 generation-
 * counter protocol and is not the object the SD claim protects; LOG_E and
 * SCPI_ErrorPush touch neither. That is why they are allowed to sit after the
 * release, and it is a claim made by reading them, not one this model tests.
 *
 * Three orderings in that slice are the subject, and all three were real bugs:
 *
 *   A. REFUSAL BEATS THE POLL. A false `sdArmed` returns SCPI_RES_ERR right
 *      there; the readiness poll is never entered. Before #942/#974 the
 *      verdict was not consulted at all, so a refused arm fell into the poll.
 *      sd_UpdateSettingsImpl's #589 refusal arms nothing and puts `mode` back
 *      to MODE_NONE, and sd_card_manager_IsWriteReady() requires
 *      mode == MODE_WRITE -- so the poll could only run its full bound and
 *      then report "SD file not ready", blaming the media for the SD task
 *      simply not running.
 *
 *   B. CLEAR, THEN RELEASE. Inside the refusal branch the `mode` store comes
 *      BEFORE sd_card_manager_ReleaseClaim(). A clear executed after the
 *      release is an unowned write: USB SCPI (pri 7) preempts WiFi SCPI
 *      (pri 2) with no shared dispatch mutex, so the other transport can
 *      TryClaim and arm in that gap and this store lands on its operand. Same
 *      defect shape as #955/#963 in SCPIStorageSD.c's
 *      SD_ArmOrRefuseWithCleanup, at a different site.
 *
 *   C. THE CLAIM IS TAKEN ONCE, BEFORE THE OPERAND IS PUBLISHED. #836:
 *      `sd_card_manager_TryClaim()` runs before `mode = MODE_WRITE` and before
 *      the arm. Without it, a concurrent SCPI SD:GET can arm MODE_READ and
 *      return OK in the gap between a bare IsBusy() test and this `mode`
 *      write, and this arm then silently overwrites it -- leaving the GET
 *      caller waiting forever for data that is never coming.
 *
 * THIS PROPERTY HAS NO ANALOGUE IN test_971_sd_arm_refusal_order.c. That
 * model's world starts with the claim already held (its `owner_a_claim_and_
 * publish` just sets the flag); the helper it models never calls TryClaim.
 * Here the modelled code takes the claim itself, so C is new ground and is
 * modelled from scratch.
 *
 * ---------------------------------------------------------------------------
 * THE TWO FACTS OUTSIDE THE PINNED SLICE THAT THE MODEL RELIES ON
 * ---------------------------------------------------------------------------
 * Both were read once and are cited by line. Neither is pinned by anything --
 * they can move, and if they do this header is what goes stale first.
 *
 * 1. A REFUSED ARM CLEARS `mode` ITSELF. sd_UpdateSettingsImpl()'s #589
 *    refusal branch stores `pSettings->mode = SD_CARD_MANAGER_MODE_NONE` and
 *    only then returns false (sd_card_manager.c:3678-3679), and that is its
 *    SOLE `return false` -- every other exit returns true.
 *    sd_card_manager_UpdateSettingsForStreamingLog() is a one-line wrapper
 *    onto it (sd_card_manager.c:3787-3790), and `pSettings` is the same live
 *    object the caller wrote (gpSDCardSettings aliases it; the function's own
 *    comment at :3658-3665 says so).
 *
 *    So the instant an arm is refused, sd_card_manager_IsBusy() is ALREADY
 *    false -- `mode` is NONE and the claim flag is the only thing still
 *    holding the competing transport off (sd_card_manager.c:4104-4126). THAT
 *    is what makes an early release dangerous, and why "clear then release" is
 *    not the same as "release then clear" even though both end at mode NONE.
 *
 *    It also makes the site's own `mode` clear redundant in effect today. It
 *    is not redundant in contract, and the source says so in as many words
 *    ("kept so the invariant is provable at THIS site rather than by reading
 *    the callee"). So every sweep below is run BOTH ways -- with an arm that
 *    clears and one that does not -- and no conclusion rests on that one
 *    modelling choice. The two ways fail the WRONG order through two
 *    DIFFERENT assertions, which is the point of running both.
 *
 * 2. THE POST-POLL CLEANUP IS UNOWNED. Below the pinned slice, the poll's
 *    failure branch calls SCPI_ReleaseSdLoggingArm(pSDCardSettings)
 *    (SCPIInterface.c:5089), which is `sd->mode = SD_CARD_MANAGER_MODE_NONE;`
 *    followed by an UpdateSettings teardown (SCPIInterface.c:4155-4161) --
 *    with no claim held. On a SUCCESSFUL arm the model treats that store as
 *    safe, on the reasoning that ownership passed from the claim flag to
 *    `mode == MODE_WRITE`, which keeps IsBusy() true.
 *
 *    THAT REASONING IS NOT COMPLETE, and the model does not establish it.
 *    sd_card_manager.c's OPEN_FILE clean-stop path clears `mode`/state before
 *    SCPI_ReleaseSdLoggingArm runs, and that teardown carries no discriminator
 *    identifying whose operand it is clearing -- so after a clean stop the
 *    "nobody else can be holding the manager" step does not hold, and this
 *    model has no representation of that transition. Read fact 2 as a
 *    statement about the interleavings the model DOES sweep (a competing
 *    transport preempting A's own sequence), not as a proof that the post-poll
 *    store is safe in all of the firmware's states. Covering the clean-stop
 *    transition would need the model extended, which is deliberately left to
 *    its own change rather than widened into this one. On a REFUSED arm that
 *    reached the poll (the pre-#942 shape) it is the #955 unowned store again,
 *    with the window widened from a few instructions to the poll's full bound
 *    -- 500 iterations of vTaskDelay(pdMS_TO_TICKS(10)) (SCPIInterface.c:5046),
 *    i.e. ~5 s. The model carries this, which is why defect A loses a race
 *    below and not merely five seconds.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS TEST ESTABLISHES
 * ---------------------------------------------------------------------------
 *   1. (A) On a refused arm the production order records no POLL_ENTERED and
 *      spends zero poll iterations; it returns the error immediately.
 *   2. (A, non-vacuity) POLL_ENTERED is REACHABLE -- the production order on a
 *      SUCCESSFUL arm does record it. Without this, 1 would hold for want of
 *      an event rather than for want of a path.
 *   3. (B) The refusal trace is exactly TRYCLAIM_OK -> SET_MODE_WRITE -> ARM
 *      -> CLEAR_MODE -> RELEASE -> RETURN_ERR, asserted against a recorded
 *      event trace rather than a handful of booleans; and at the INSTANT of
 *      release -- snapshotted inside the release stub, before ownership is
 *      dropped -- `mode` is already MODE_NONE.
 *   4. (C) TRYCLAIM_OK precedes SET_MODE_WRITE which precedes ARM; exactly one
 *      claim is taken and exactly one release is issued on each path.
 *   5. (C) A REFUSED claim publishes nothing and releases nothing -- no
 *      `mode` write, no arm, and in particular no ReleaseClaim, which would
 *      free the claim the winner is holding.
 *   6. Success: ownership hands over from the claim flag to `mode` with no
 *      gap -- `mode` is already MODE_WRITE at the instant of release.
 *   7. Discrimination (the part that is not a tautology): a cooperative
 *      two-owner simulation offers a competing SCPI transport the CPU at every
 *      step boundary, and shows the production order protects that transport's
 *      operand at EVERY one -- on the refusal path and on the success path --
 *      while each of the four wrong orders loses at some point. A model no
 *      wrong order can fail would prove nothing, so the wrong orders are run
 *      too and REQUIRED to fail.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS TEST DOES **NOT** ESTABLISH — read this before trusting it
 * ---------------------------------------------------------------------------
 * * **It does not compile the firmware.** SCPIInterface.c is 8,705 lines and
 *   includes FreeRTOS (semphr.h), Harmony PLIBs (plib_gpio.h,
 *   plib_usbhs_header.h), the WINC driver, nanopb and libscpi; it is not
 *   host-includable, the same wall test_943_bench_stall_bound.c and
 *   test_971_sd_arm_refusal_order.c hit on its sibling SCPIStorageSD.c. So
 *   what runs below is a MODEL of the slice quoted above, not that slice. The
 *   model<->firmware correspondence was established by human review at the
 *   moment START_CLAIMED_SHA was recorded in the Makefile. That pin asserts
 *   NOTHING about the ordering; it only fails the build when the slice's text
 *   changes, forcing the review to happen again instead of letting the model
 *   rot silently. It is a tripwire, not a checker.
 *
 * * **The pin hashes the slice's RAW BYTES.** It is deliberately NOT
 *   whitespace- or comment-insensitive, and an earlier revision of this note
 *   said it was. Three successive attempts to exempt "harmless" edits each
 *   also exempted a harmful one -- see the Makefile for the three and what
 *   each erased. So reindenting the slice fires, and rewording a comment in it
 *   fires. If you get a drift failure for an edit that turns out not to matter,
 *   that is the intended cost: re-read the slice against this model and
 *   re-derive START_CLAIMED_SHA.
 *
 * * **It does not reproduce concurrency.** There is no scheduler, no
 *   preemption, no interrupts, no critical sections and no cache. The
 *   "interleavings" below are hand-enumerated step boundaries in a
 *   single-threaded program, and the competing transport runs to completion in
 *   one go when it runs at all -- so it is never still HOLDING the claim when
 *   the modelled code calls ReleaseClaim(), and the model therefore cannot
 *   show that an unowned ReleaseClaim() steals someone else's claim (the real
 *   sd_card_manager_ReleaseClaim() clears the flag unconditionally,
 *   sd_card_manager.c:4115-4118). A model of a race is not a race: it can show
 *   an ordering is unsound; it cannot show an ordering is safe on PIC32MZ
 *   under FreeRTOS.
 *
 * * **The claim predicate is modelled by its first term only.**
 *   sd_card_manager_TryClaim() grants iff `!gSdScpiClaim && !IsBusyLocked()`,
 *   and IsBusyLocked is `gpSDCardSettings == NULL` OR `mode != MODE_NONE` OR
 *   the state machine being off IDLE/INIT (sd_card_manager.c:4128-4155). The
 *   model carries `!claimHeld && mode == MODE_NONE`. The dropped terms only
 *   ever make the real TryClaim refuse MORE often, so the model is if anything
 *   more permissive about letting the competing transport in than the firmware
 *   is -- the safe direction for a test whose job is to catch losses.
 *
 * * **It does not establish the census.** "Is this the only place START arms
 *   an SD write?" is a whole-program property no host test can see.
 *   SCPIInterface.c's own #942 comment asserts exactly two WRITE-arming sites
 *   in the tree; that is a claim from review, not from here.
 *
 * * It says nothing about whether the real clear clears the real field,
 *   whether the real release releases, or whether the real arm arms. Those are
 *   one-line calls into the SD manager. What is modelled is the ORDER they run
 *   in, which is what the defects got wrong.
 *
 * ---------------------------------------------------------------------------
 * WHY NOT TEST THE REAL FUNCTION, AND WHY NOT A TEXTUAL GATE
 * ---------------------------------------------------------------------------
 * Extraction into a host-includable helper was considered and rejected for the
 * same two reasons #971 rejected it, plus one specific to this site. The slice
 * is not a function: it is the middle of a 1250-line body, interleaved with
 * that body's OPER-bit and interface-publish bookkeeping and with five of its
 * twenty-odd return sites. Lifting it out would mean passing `context`,
 * `pRunTimeStreamConfig`, `pSDCardSettings`, `ifaceForStart`, `ifaceAtDetect`
 * and two pinned generation counters through a new signature -- more moving
 * parts than the ordering it would make testable, in a function whose defects
 * have all been "the statement is in the wrong place".
 *
 * A textual gate was also rejected. `tools/lint/scpi_sd_arm_path.py` (#976)
 * carried a branch-gated positional check of exactly this shape for exactly
 * this site, and it was REMOVED after three review rounds catalogued the ways
 * an honest refactor defeats reasoning-about-which-branch-a-write-sits-in with
 * regexes. #998 exists because that removal left this site with no coverage at
 * all; re-adding the same check is explicitly out of its scope. A content hash
 * claims only "this text is unchanged", which is all a build recipe can
 * honestly know, and no refactor can slip past it.
 * ========================================================================== */

#include "test_framework.h"
#include <stdbool.h>
#include <stddef.h>

/* ==========================================================================
 * The modelled world
 *
 * Only the state the ordering reasons about. Names mirror the firmware.
 *
 * `mode` lives in the SHARED settings object -- BoardRunTimeConfig_Get(
 * BOARDRUNTIME_SD_CARD_SETTINGS) hands every SCPI transport the same struct,
 * which is why one owner's late store lands on another owner's operation.
 * The claim is sd_card_manager.c's gSdScpiClaim.
 * ========================================================================== */

enum {
    MODEL_MODE_NONE  = 0,   /* SD_CARD_MANAGER_MODE_NONE                    */
    MODEL_MODE_READ  = 1,   /* SD_CARD_MANAGER_MODE_READ  -- owner B's GET  */
    MODEL_MODE_WRITE = 2    /* SD_CARD_MANAGER_MODE_WRITE -- the stream log */
};

/* The poll's iteration bound, from the slice's last line:
 *     while (!sd_card_manager_IsWriteReady() && readyWait < 500)
 * That line is the Makefile's END ANCHOR, so a change to the 500 fails the
 * anchor grep by name rather than merely shifting the hash. At
 * vTaskDelay(pdMS_TO_TICKS(10)) per iteration (SCPIInterface.c:5047, which is
 * BELOW the pinned slice and so is cited, not pinned) that is ~5 s. */
#define MODEL_POLL_MAX 500

typedef enum {
    EV_TRYCLAIM_OK = 1,
    EV_TRYCLAIM_REFUSED,
    EV_SET_MODE_WRITE,
    EV_ARM,
    EV_CLEAR_MODE,
    EV_RELEASE,
    EV_POLL_ENTERED,
    EV_RETURN_ERR,
    EV_RETURN_OK
} ModelEvent;

#define MODEL_MAX_EVENTS 16

typedef enum { RES_OK = 0, RES_ERR = 1 } ModelResult;

typedef struct {
    /* shared device state */
    bool claimHeld;           /* gSdScpiClaim                                */
    int  mode;                /* pSDCardSettings->mode, the shared field     */

    /* event trace */
    ModelEvent events[MODEL_MAX_EVENTS];
    int        eventCount;

    /* snapshot taken INSIDE the release stub, before ownership is dropped */
    bool releaseSeen;
    int  modeAtRelease;
    int  eventsAtRelease;

    /* what the readiness poll cost */
    bool pollEntered;
    int  pollIterations;

    /* cooperative scheduler: offer the competing transport the CPU at hook N */
    int  hookCount;
    int  preemptAt;           /* -1 = never                                  */

    /* does a refused arm clear `mode` itself? True mirrors the firmware
     * today (sd_card_manager.c:3678); false is the robustness variant. */
    bool armClearsMode;

    /* how many poll iterations before the file is open, on an arm that was
     * accepted. 0 = ready immediately; > MODEL_POLL_MAX = never opens. */
    int  readyAfter;

    /* the competing transport ("owner B"), running its own SD:GET */
    bool bHasRun;
    bool bArmed;
    int  bArmedMode;
} Model;

static Model g_m;

static void model_reset(int preemptAt, bool armClearsMode)
{
    int i;
    g_m.claimHeld = false;
    g_m.mode = MODEL_MODE_NONE;
    for (i = 0; i < MODEL_MAX_EVENTS; i++) {
        g_m.events[i] = (ModelEvent)0;
    }
    g_m.eventCount = 0;
    g_m.releaseSeen = false;
    g_m.modeAtRelease = -1;
    g_m.eventsAtRelease = -1;
    g_m.pollEntered = false;
    g_m.pollIterations = -1;
    g_m.hookCount = 0;
    g_m.preemptAt = preemptAt;
    g_m.armClearsMode = armClearsMode;
    g_m.readyAfter = 0;
    g_m.bHasRun = false;
    g_m.bArmed = false;
    g_m.bArmedMode = MODEL_MODE_NONE;
}

static void model_record(ModelEvent ev)
{
    if (g_m.eventCount < MODEL_MAX_EVENTS) {
        g_m.events[g_m.eventCount] = ev;
    }
    g_m.eventCount++;   /* counts past the cap so an overflow stays visible */
}

/* ---------------------------------------------------------------------------
 * Owner B — the other SCPI transport, running its own SD:GET.
 *
 * One shot per scenario, and it runs to completion: claim, fill operands,
 * write `mode` LAST (#829), arm (accepted for B), release. That is the worst
 * case for a late store by owner A, which is the case worth modelling.
 * --------------------------------------------------------------------------- */
static void owner_b_try(void)
{
    if (g_m.bHasRun) {
        return;
    }
    g_m.bHasRun = true;

    if (g_m.claimHeld || g_m.mode != MODEL_MODE_NONE) {
        return;                         /* TryClaim refused -- B does nothing */
    }
    g_m.claimHeld = true;               /* TryClaim granted                   */
    g_m.mode = MODEL_MODE_READ;         /* #829: `mode` is written LAST       */
    g_m.bArmed = true;                  /* its arm is accepted                */
    g_m.bArmedMode = g_m.mode;
    g_m.claimHeld = false;              /* ReleaseClaim, success path         */
}

/* Step boundary: the point at which the other transport could get the CPU. */
static void model_hook(void)
{
    if (g_m.hookCount == g_m.preemptAt) {
        owner_b_try();
    }
    g_m.hookCount++;
}

/* ---------------------------------------------------------------------------
 * The primitives the slice orders.
 * --------------------------------------------------------------------------- */

/* sd_card_manager_TryClaim(). Grants iff the flag is free AND the manager is
 * not already busy; the model carries `mode != MODE_NONE` as the whole of the
 * busy term (see the header for the dropped terms). */
static bool model_try_claim(void)
{
    bool got = (!g_m.claimHeld) && (g_m.mode == MODEL_MODE_NONE);
    if (got) {
        g_m.claimHeld = true;
    }
    model_record(got ? EV_TRYCLAIM_OK : EV_TRYCLAIM_REFUSED);
    model_hook();
    return got;
}

/* pSDCardSettings->mode = SD_CARD_MANAGER_MODE_WRITE; */
static void model_set_mode_write(void)
{
    g_m.mode = MODEL_MODE_WRITE;
    model_record(EV_SET_MODE_WRITE);
    model_hook();
}

/* sd_card_manager_UpdateSettingsForStreamingLog(cfg). On refusal the real one
 * clears `mode` in the shared object before returning false -- see header. */
static bool model_arm(bool verdict)
{
    if (!verdict && g_m.armClearsMode) {
        g_m.mode = MODEL_MODE_NONE;
    }
    model_record(EV_ARM);
    model_hook();
    return verdict;
}

/* pSDCardSettings->mode = SD_CARD_MANAGER_MODE_NONE; -- also stands in for
 * SCPI_ReleaseSdLoggingArm()'s identical store on the post-poll branch. */
static void model_clear_mode(void)
{
    g_m.mode = MODEL_MODE_NONE;
    model_record(EV_CLEAR_MODE);
    model_hook();
}

static void model_release_claim(void)
{
    /* Snapshot BEFORE dropping ownership: this is what the next owner
     * inherits at the instant the claim becomes takeable. */
    if (!g_m.releaseSeen) {
        g_m.releaseSeen = true;
        g_m.modeAtRelease = g_m.mode;
        g_m.eventsAtRelease = g_m.eventCount;
    }
    g_m.claimHeld = false;              /* sd_card_manager_ReleaseClaim()     */
    model_record(EV_RELEASE);
    model_hook();
}

/* sd_card_manager_IsWriteReady() -- requires mode == MODE_WRITE, plus the SD
 * task having reached WRITE_TO_FILE with an open handle, modelled as "at least
 * readyAfter poll iterations have passed" (sd_card_manager.c:4158-4164). */
static bool model_is_write_ready(int iterations)
{
    return (g_m.mode == MODEL_MODE_WRITE) && (iterations >= g_m.readyAfter);
}

/* ---------------------------------------------------------------------------
 * The slice, parameterised by ordering policy.
 *
 * START_PRODUCTION is the code quoted at the top of this file. The other four
 * are the defect shapes, present so the model can be shown to have teeth.
 * --------------------------------------------------------------------------- */
typedef enum {
    START_PRODUCTION,                 /* claim -> publish -> arm -> clear -> release */
    START_WRONG_POLL_FIRST,           /* (A) the verdict is never consulted          */
    START_WRONG_RELEASE_BEFORE_CLEAR, /* (B) release -> clear, the #955 shape        */
    START_WRONG_CLAIM_AFTER_ARM,      /* (C) claim taken once the operand is public  */
    START_WRONG_NO_CLAIM              /* (C) never claims, the pre-#836 shape        */
} StartOrder;

/* The readiness poll, plus the post-poll failure branch. The poll head is the
 * last line of the pinned slice; everything after it is cited, not pinned
 * (see fact 2 in the header). */
static ModelResult model_poll_and_finish(void)
{
    int readyWait = 0;

    g_m.pollEntered = true;
    model_record(EV_POLL_ENTERED);
    model_hook();

    while (!model_is_write_ready(readyWait) && readyWait < MODEL_POLL_MAX) {
        readyWait++;
    }
    g_m.pollIterations = readyWait;

    if (!model_is_write_ready(readyWait)) {
        /* SCPI_ReleaseSdLoggingArm(pSDCardSettings) -- NO claim is held here. */
        model_clear_mode();
        model_record(EV_RETURN_ERR);
        return RES_ERR;
    }
    model_record(EV_RETURN_OK);
    return RES_OK;
}

static ModelResult model_start_streaming_claimed(StartOrder order, bool armVerdict)
{
    bool sdArmed;

    model_hook();                       /* boundary 0: before anything runs */

    if (order != START_WRONG_NO_CLAIM && order != START_WRONG_CLAIM_AFTER_ARM) {
        if (!model_try_claim()) {
            /* "Cannot start SD logging - SD card busy with another operation".
             * Nothing was published, so there is nothing to undo -- and in
             * particular NO ReleaseClaim: the claim belongs to whoever won. */
            model_record(EV_RETURN_ERR);
            return RES_ERR;
        }
    }

    model_set_mode_write();
    sdArmed = model_arm(armVerdict);

    if (order == START_WRONG_CLAIM_AFTER_ARM) {
        (void)model_try_claim();        /* too late: the operand is public */
    }

    if (order == START_WRONG_POLL_FIRST) {
        /* pre-#942/#974: `sdArmed` is not consulted at all. The release and
         * then the poll run unconditionally, and a refused arm surfaces only
         * as "SD file not ready" once the poll has run its full bound. */
        model_release_claim();
    } else if (!sdArmed) {
        if (order == START_WRONG_RELEASE_BEFORE_CLEAR) {
            model_release_claim();
            model_clear_mode();
        } else {
            model_clear_mode();         /* #955: clear under the claim, ... */
            model_release_claim();      /* ... THEN release.                */
        }
        model_record(EV_RETURN_ERR);
        return RES_ERR;
    } else {
        /* #836: armed -- ownership has handed over from the claim flag to
         * `mode`, which is MODE_WRITE and keeps IsBusy() true, so there is no
         * gap between releasing here and the manager being busy. */
        model_release_claim();
    }

    return model_poll_and_finish();
}

/* Step boundaries, counted generously: boundary 0, one after each of the five
 * primitives, one at poll entry, and slack for the wrong orders' extra calls.
 * Injection points past an order's own boundary count simply never fire. */
#define START_HOOKS 9

/* ---------------------------------------------------------------------------
 * Assertion helpers over the event trace.
 * --------------------------------------------------------------------------- */
static bool trace_is(const ModelEvent *want, int n)
{
    int i;
    if (g_m.eventCount != n) {
        return false;
    }
    for (i = 0; i < n; i++) {
        if (g_m.events[i] != want[i]) {
            return false;
        }
    }
    return true;
}

static int trace_index(ModelEvent ev)     /* first occurrence, or -1 */
{
    int i;
    for (i = 0; i < g_m.eventCount && i < MODEL_MAX_EVENTS; i++) {
        if (g_m.events[i] == ev) {
            return i;
        }
    }
    return -1;
}

static int trace_count(ModelEvent ev)
{
    int i, n = 0;
    for (i = 0; i < g_m.eventCount && i < MODEL_MAX_EVENTS; i++) {
        if (g_m.events[i] == ev) {
            n++;
        }
    }
    return n;
}

static bool trace_has(ModelEvent ev)
{
    return trace_index(ev) >= 0;
}

/* ==========================================================================
 * PROPERTY A — a refused arm returns before the readiness poll
 * ========================================================================== */
TEST(production_refusal_never_enters_the_readiness_poll)
{
    ModelResult r;

    model_reset(-1, true);
    r = model_start_streaming_claimed(START_PRODUCTION, false);

    ASSERT_EQ(r, RES_ERR);
    ASSERT_FALSE(trace_has(EV_POLL_ENTERED));
    ASSERT_FALSE(g_m.pollEntered);
    ASSERT_EQ(g_m.pollIterations, -1);     /* the loop never ran at all */
}

TEST(production_success_does_enter_the_readiness_poll)
{
    ModelResult r;

    /* Non-vacuity for the test above: POLL_ENTERED is a REACHABLE event, so
     * "production never records it on a refusal" is a statement about the
     * path, not about a dead event. */
    model_reset(-1, true);
    r = model_start_streaming_claimed(START_PRODUCTION, true);

    ASSERT_EQ(r, RES_OK);
    ASSERT_TRUE(trace_has(EV_POLL_ENTERED));
    ASSERT_TRUE(g_m.pollEntered);
    ASSERT_EQ(g_m.pollIterations, 0);      /* file already open */
}

TEST(poll_first_order_enters_the_poll_and_burns_the_whole_bound)
{
    ModelResult r;

    /* NEGATIVE CONTROL for property A. This is the pre-#942/#974 shape, and
     * the two assertions production passes above are inverted here: it DOES
     * reach POLL_ENTERED on a refused arm, and it spends the full 500
     * iterations (~5 s at 10 ms each) before reporting "SD file not ready" --
     * an arm that was refused for a reason the device already knew. */
    model_reset(-1, true);
    r = model_start_streaming_claimed(START_WRONG_POLL_FIRST, false);

    ASSERT_EQ(r, RES_ERR);
    ASSERT_TRUE(trace_has(EV_POLL_ENTERED));
    ASSERT_TRUE(g_m.pollEntered);
    ASSERT_EQ(g_m.pollIterations, MODEL_POLL_MAX);
}

/* ==========================================================================
 * PROPERTY B — inside the refusal branch, clear then release
 * ========================================================================== */
TEST(production_refusal_trace_is_claim_publish_arm_clear_release)
{
    static const ModelEvent want[] = {
        EV_TRYCLAIM_OK, EV_SET_MODE_WRITE, EV_ARM,
        EV_CLEAR_MODE, EV_RELEASE, EV_RETURN_ERR
    };
    ModelResult r;

    model_reset(-1, true);
    r = model_start_streaming_claimed(START_PRODUCTION, false);

    ASSERT_EQ(r, RES_ERR);
    ASSERT_TRUE(trace_is(want, 6));
    ASSERT_EQ(g_m.mode, MODEL_MODE_NONE);
    ASSERT_FALSE(g_m.claimHeld);
}

TEST(production_refusal_state_is_settled_at_the_instant_of_release)
{
    /* Snapshot taken inside the release stub, before ownership was dropped:
     * the clear had already run, so the next owner cannot inherit an operand
     * this call is still about to zero. */
    model_reset(-1, true);
    (void)model_start_streaming_claimed(START_PRODUCTION, false);

    ASSERT_TRUE(g_m.releaseSeen);
    ASSERT_EQ(g_m.modeAtRelease, MODEL_MODE_NONE);
    ASSERT_EQ(g_m.eventsAtRelease, 4);  /* claim, publish, arm, clear logged */
}

TEST(production_refusal_settles_even_if_the_arm_clears_nothing)
{
    /* Same assertion with the arm's own `mode` clear taken away: the site's
     * store is what carries the property, not the callee's side effect. */
    model_reset(-1, false);
    (void)model_start_streaming_claimed(START_PRODUCTION, false);

    ASSERT_TRUE(g_m.releaseSeen);
    ASSERT_EQ(g_m.modeAtRelease, MODEL_MODE_NONE);
}

TEST(release_before_clear_hands_over_an_unowned_armed_mode)
{
    /* NEGATIVE CONTROL for property B, variant 1 (the arm clears nothing).
     * The very assertion production passes two tests up --
     * modeAtRelease == MODEL_MODE_NONE -- is false here: the claim is dropped
     * with `mode` still reading MODE_WRITE, so for the width of the gap
     * IsBusy() is true with NOBODY owning the manager, and every other SD
     * command is refused by a phantom operation. */
    model_reset(-1, false);
    (void)model_start_streaming_claimed(START_WRONG_RELEASE_BEFORE_CLEAR, false);

    ASSERT_TRUE(g_m.releaseSeen);
    ASSERT_EQ(g_m.modeAtRelease, MODEL_MODE_WRITE);   /* NOT MODE_NONE */
}

TEST(release_before_clear_loses_the_955_race)
{
    int i;
    int brokenOperand = 0;

    /* NEGATIVE CONTROL for property B, variant 2 (the arm clears `mode`, as
     * the firmware's does today). The refused arm has already put `mode` back
     * to NONE, so the early release leaves nothing holding B off: B claims,
     * arms, and A's late `mode = MODE_NONE` zeroes B's operand. The SD task is
     * left with nothing to run and nobody is told. Exactly #955. */
    for (i = 0; i < START_HOOKS; i++) {
        model_reset(i, true);
        (void)model_start_streaming_claimed(START_WRONG_RELEASE_BEFORE_CLEAR, false);
        if (g_m.bArmed && g_m.mode != g_m.bArmedMode) {
            brokenOperand++;
        }
    }
    ASSERT_TRUE(brokenOperand > 0);
}

/* ==========================================================================
 * PROPERTY C — the claim is taken exactly once, before the operand is public
 * ========================================================================== */
TEST(production_claims_before_it_publishes_the_operand)
{
    int claimAt, publishAt, armAt;

    model_reset(-1, true);
    (void)model_start_streaming_claimed(START_PRODUCTION, true);

    claimAt   = trace_index(EV_TRYCLAIM_OK);
    publishAt = trace_index(EV_SET_MODE_WRITE);
    armAt     = trace_index(EV_ARM);

    ASSERT_TRUE(claimAt >= 0);
    ASSERT_TRUE(claimAt < publishAt);
    ASSERT_TRUE(publishAt < armAt);
}

TEST(production_takes_one_claim_and_issues_one_release_on_each_path)
{
    model_reset(-1, true);
    (void)model_start_streaming_claimed(START_PRODUCTION, false);
    ASSERT_EQ(trace_count(EV_TRYCLAIM_OK) + trace_count(EV_TRYCLAIM_REFUSED), 1);
    ASSERT_EQ(trace_count(EV_RELEASE), 1);

    model_reset(-1, true);
    (void)model_start_streaming_claimed(START_PRODUCTION, true);
    ASSERT_EQ(trace_count(EV_TRYCLAIM_OK) + trace_count(EV_TRYCLAIM_REFUSED), 1);
    ASSERT_EQ(trace_count(EV_RELEASE), 1);
}

TEST(a_refused_claim_publishes_nothing_and_releases_nothing)
{
    static const ModelEvent want[] = { EV_TRYCLAIM_REFUSED, EV_RETURN_ERR };
    ModelResult r;

    /* Another owner already holds the manager with a READ armed. */
    model_reset(-1, true);
    g_m.claimHeld = true;
    g_m.mode = MODEL_MODE_READ;

    r = model_start_streaming_claimed(START_PRODUCTION, true);

    ASSERT_EQ(r, RES_ERR);
    ASSERT_TRUE(trace_is(want, 2));
    ASSERT_FALSE(trace_has(EV_SET_MODE_WRITE));   /* published nothing        */
    ASSERT_FALSE(trace_has(EV_ARM));              /* armed nothing            */
    ASSERT_FALSE(trace_has(EV_RELEASE));          /* and freed nobody's claim */
    ASSERT_EQ(g_m.mode, MODEL_MODE_READ);         /* the winner's operand     */
    ASSERT_TRUE(g_m.claimHeld);                   /* still the winner's       */
}

TEST(no_claim_at_all_loses_the_836_race)
{
    int i;
    int brokenOperand = 0;

    /* NEGATIVE CONTROL for property C, variant 1: the pre-#836 shape, where
     * a bare IsBusy() test was all that stood between the check and the
     * `mode = MODE_WRITE` write. B arms its SD:GET in that gap and A's publish
     * overwrites MODE_READ -- the GET caller then waits forever for data that
     * is never coming. */
    for (i = 0; i < START_HOOKS; i++) {
        model_reset(i, true);
        (void)model_start_streaming_claimed(START_WRONG_NO_CLAIM, false);
        if (g_m.bArmed && g_m.mode != g_m.bArmedMode) {
            brokenOperand++;
        }
    }
    ASSERT_TRUE(brokenOperand > 0);
}

TEST(claim_after_the_arm_loses_the_836_race)
{
    int i;
    int brokenOperand = 0;

    /* NEGATIVE CONTROL for property C, variant 2: the claim IS taken, just
     * not before the operand is published. A claim that starts after the
     * damage protects nothing, which is what this asserts.
     *
     * An earlier version of this comment also explained WHY the late claim is
     * refused, blaming A's own `mode = MODE_WRITE`. That was wrong -- tracing
     * the call site shows `mode` is already NONE at that point for almost
     * every iteration, and the refusals that do occur come from owner B's
     * MODE_READ instead. The explanation is dropped rather than replaced,
     * because the assertion below does not depend on it and a second guess
     * would be no better checked than the first. */
    for (i = 0; i < START_HOOKS; i++) {
        model_reset(i, true);
        (void)model_start_streaming_claimed(START_WRONG_CLAIM_AFTER_ARM, false);
        if (g_m.bArmed && g_m.mode != g_m.bArmedMode) {
            brokenOperand++;
        }
    }
    ASSERT_TRUE(brokenOperand > 0);
}

/* ==========================================================================
 * DISCRIMINATION — the competing transport, at every step boundary
 *
 * Scenario: owner B is a competing SCPI transport running its own SD:GET. It
 * is offered the CPU at one step boundary of A's sequence, and if the claim is
 * takeable it runs its whole operation. The invariant: anything B armed must
 * survive -- A has no business writing `mode` after it stopped being the
 * owner.
 * ========================================================================== */
typedef struct {
    bool bArmed;
    bool operandIntact;
} RaceOutcome;

static RaceOutcome run_race(StartOrder order, int preemptAt, bool armClearsMode,
                            bool armVerdict, int readyAfter)
{
    RaceOutcome out;

    model_reset(preemptAt, armClearsMode);
    g_m.readyAfter = readyAfter;

    (void)model_start_streaming_claimed(order, armVerdict);

    out.bArmed = g_m.bArmed;
    out.operandIntact = (!g_m.bArmed) || (g_m.mode == g_m.bArmedMode);
    return out;
}

TEST(production_refusal_survives_every_injection_point)
{
    int variant, i;

    for (variant = 0; variant < 2; variant++) {
        int armedAt = 0;
        for (i = 0; i < START_HOOKS; i++) {
            RaceOutcome r = run_race(START_PRODUCTION, i, variant == 0, false, 0);
            ASSERT_TRUE(r.operandIntact);
            if (r.bArmed) { armedAt++; }
        }
        /* The sweep must not be vacuous: some injection point has to actually
         * let B claim and arm, or "intact" holds for want of anyone to damage.
         * With the production order there are two such points -- before A's
         * TryClaim (where A then LOSES the claim and publishes nothing) and
         * after A's release (where A has nothing left to write). */
        ASSERT_TRUE(armedAt > 0);
    }
}

TEST(production_success_survives_every_injection_point)
{
    int i;
    int armedAt = 0;

    /* The success path, with a file that never opens -- so A runs its poll to
     * the bound and then performs SCPI_ReleaseSdLoggingArm's unowned `mode`
     * store, ~5 s after it released the claim. That store is still safe, and
     * the reason is the #836 handover: `mode == MODE_WRITE` keeps IsBusy()
     * true for that whole time, so B is refused at every boundary and there is
     * no operand of B's for the late store to land on. */
    for (i = 0; i < START_HOOKS; i++) {
        RaceOutcome r = run_race(START_PRODUCTION, i, true, true,
                                 MODEL_POLL_MAX + 1);
        ASSERT_TRUE(r.operandIntact);
        if (r.bArmed) { armedAt++; }
    }
    /* Non-vacuity: B still gets in at the boundary BEFORE A's TryClaim, where
     * A then loses the claim outright. */
    ASSERT_TRUE(armedAt > 0);
}

TEST(poll_first_order_also_loses_the_race_five_seconds_late)
{
    int i;
    int brokenOperand = 0;

    /* Property A's defect is not only a wrong diagnosis and a 5 s stall. On a
     * refused arm the pre-#942 shape releases the claim, polls to the bound,
     * and THEN runs SCPI_ReleaseSdLoggingArm's `mode = MODE_NONE` -- the #955
     * unowned store with its window widened from a few instructions to ~5 s.
     * So refusing before the poll is what keeps that store from existing. */
    for (i = 0; i < START_HOOKS; i++) {
        RaceOutcome r = run_race(START_WRONG_POLL_FIRST, i, true, false, 0);
        if (r.bArmed && !r.operandIntact) { brokenOperand++; }
    }
    ASSERT_TRUE(brokenOperand > 0);
}

TEST(production_success_hands_ownership_from_the_claim_to_mode)
{
    static const ModelEvent want[] = {
        EV_TRYCLAIM_OK, EV_SET_MODE_WRITE, EV_ARM,
        EV_RELEASE, EV_POLL_ENTERED, EV_RETURN_OK
    };
    ModelResult r;

    model_reset(-1, true);
    r = model_start_streaming_claimed(START_PRODUCTION, true);

    ASSERT_EQ(r, RES_OK);
    ASSERT_TRUE(trace_is(want, 6));
    ASSERT_FALSE(trace_has(EV_CLEAR_MODE));   /* success clears nothing */
    /* The operand was already in place when ownership was dropped:
     * `mode != MODE_NONE` is what keeps IsBusy() true across the handover. */
    ASSERT_EQ(g_m.modeAtRelease, MODEL_MODE_WRITE);
    ASSERT_FALSE(g_m.claimHeld);
}

int main(void)
{
    printf("=== #998: SCPI_StartStreamingClaimed SD-arm order (host model) ===\n");

    /* Property A -- refusal beats the poll */
    RUN(production_refusal_never_enters_the_readiness_poll);
    RUN(production_success_does_enter_the_readiness_poll);
    RUN(poll_first_order_enters_the_poll_and_burns_the_whole_bound);

    /* Property B -- clear, then release */
    RUN(production_refusal_trace_is_claim_publish_arm_clear_release);
    RUN(production_refusal_state_is_settled_at_the_instant_of_release);
    RUN(production_refusal_settles_even_if_the_arm_clears_nothing);
    RUN(release_before_clear_hands_over_an_unowned_armed_mode);
    RUN(release_before_clear_loses_the_955_race);

    /* Property C -- claim once, before the operand is published */
    RUN(production_claims_before_it_publishes_the_operand);
    RUN(production_takes_one_claim_and_issues_one_release_on_each_path);
    RUN(a_refused_claim_publishes_nothing_and_releases_nothing);
    RUN(no_claim_at_all_loses_the_836_race);
    RUN(claim_after_the_arm_loses_the_836_race);

    /* Discrimination */
    RUN(production_refusal_survives_every_injection_point);
    RUN(production_success_survives_every_injection_point);
    RUN(poll_first_order_also_loses_the_race_five_seconds_late);
    RUN(production_success_hands_ownership_from_the_claim_to_mode);

    return TEST_SUMMARY();
}
