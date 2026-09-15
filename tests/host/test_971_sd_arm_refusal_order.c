/* ==========================================================================
 * test_971_sd_arm_refusal_order.c — the under-claim settle order of a
 * REFUSED SD arm, as a deterministic host model (issue #971).
 *
 * ---------------------------------------------------------------------------
 * THE FIRMWARE CODE THIS MODELS
 * ---------------------------------------------------------------------------
 * firmware/src/services/SCPI/SCPIStorageSD.c, SD_ArmOrRefuseWithCleanup().
 * Comments stripped, whitespace normalised, this is its whole body:
 *
 *   if (sd_card_manager_UpdateSettings(cfg)) {
 *       sd_card_manager_ReleaseClaim();
 *       return true;
 *   }
 *   cfg->mode = SD_CARD_MANAGER_MODE_NONE;
 *   if (onRefused != NULL) { onRefused(); }
 *   sd_card_manager_ReleaseClaim();
 *   <log + SCPI_ErrorPush — after the release, carries no ordering content>
 *   return false;
 *
 * Six SCPI commands arm through it. Five (CRC, GET, LISt, DELete, SPACe) go
 * via the SD_ArmOrRefuse() wrapper, which passes onRefused = NULL. FORmat
 * passes sd_card_manager_ClearFormatStatus, to retract the "format pending"
 * flag it published *before* arming.
 *
 * The ordering is the whole point, and it has a bug lineage:
 *   #955 — the `mode` clear used to be the CALLER's, run AFTER the helper had
 *          already released. Between release and that store the other SCPI
 *          transport (USB pri 7 preempts WiFi pri 2, no shared dispatch
 *          mutex) could claim and arm its own operation, and the first
 *          caller's unowned store then zeroed the winner's operand.
 *   #964 — same defect, one field over: FORmat's retraction ran at the call
 *          site, past the release, and could erase the NEXT owner's published
 *          format status.
 *   #942/#974 — a caller that ignored the verdict entirely.
 *
 * ---------------------------------------------------------------------------
 * THE DETAIL THAT MAKES THE WINDOW EXIST (read before judging the model)
 * ---------------------------------------------------------------------------
 * A refused arm CLEARS `mode` ITSELF. `sd_UpdateSettingsImpl()`'s #589
 * refusal branch stores `pSettings->mode = SD_CARD_MANAGER_MODE_NONE` and
 * only then returns false (sd_card_manager.c:3566-3567), and it is the sole
 * `return false` in that function — every other exit returns true. `pSettings`
 * is the same live object the caller wrote: gpSDCardSettings is assigned from
 * the caller's pointer, and every SCPI caller passes
 * BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS) (its own comment at
 * sd_card_manager.c:3543-3549 says so).
 *
 * So the instant an arm is refused, `sd_card_manager_IsBusy()` is already
 * false — mode is NONE and the claim flag is the only thing still holding the
 * competing transport off (sd_card_manager.c:3997-4021). THAT is what makes
 * an early release dangerous, and why "clear then release" is not the same
 * as "release then clear" even though both end with mode == NONE.
 *
 * It also means the helper's own `cfg->mode = MODE_NONE` is, today,
 * redundant in effect. It is not redundant in contract: it is what keeps the
 * helper correct if a second refusal path is ever added to the arm, and the
 * sweep below is therefore run BOTH ways — with an arm that clears and one
 * that does not — so no conclusion here rests on that one modelling choice.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS TEST ESTABLISHES
 * ---------------------------------------------------------------------------
 *   1. Refusal: the trace is exactly CLEAR_MODE -> RETRACT -> RELEASE. Order
 *      is asserted against a recorded event trace, not three booleans.
 *   2. Refusal: at the *instant* the claim is released — snapshotted inside
 *      the release stub, before it drops ownership — `mode` is already
 *      MODE_NONE and the retraction has already run. This is the property
 *      that matters: ownership is handed over with the state fully settled,
 *      not merely settled "eventually".
 *   3. Refusal with onRefused == NULL (the five-caller shape): the clear and
 *      the release still happen, in that order, and nothing is invoked.
 *   4. Success: the claim IS released, the retraction does NOT run, and
 *      `mode` is left exactly as the caller set it — the success path touches
 *      neither.
 *   5. Discrimination (this is the part that is not a tautology): a
 *      cooperative two-owner simulation offers the competing SCPI transport
 *      the CPU at every step boundary of the settle sequence, and shows that
 *      the production order survives EVERY one while both historical defect
 *      orders lose at one. A model that no wrong order can fail would prove
 *      nothing, so the wrong orders are run too and required to fail.
 *
 * ---------------------------------------------------------------------------
 * WHAT THIS TEST DOES **NOT** ESTABLISH — read this before trusting it
 * ---------------------------------------------------------------------------
 * * **It does not compile the firmware.** SCPIStorageSD.c drags in libscpi,
 *   FreeRTOS, Harmony and the whole SD/WiFi stack; it is not host-includable,
 *   which `test_943_bench_stall_bound.c` already hit for this same file. So
 *   the sequence exercised below is a MODEL of the body quoted above, not
 *   that body. The model<->firmware correspondence was established by human
 *   review at the moment SD_ARM_HELPER_SHA was recorded in the Makefile.
 *   That pin asserts NOTHING about the ordering property — it only fails the
 *   build when the modelled function's code changes, forcing the review to
 *   happen again instead of letting the model rot silently. It is a tripwire,
 *   not a checker. Nothing pins the sd_card_manager.c behaviour quoted above
 *   at all; it was read once, cited by line, and could move.
 *
 * * **It does not reproduce concurrency.** There is no scheduler, no
 *   preemption, no interrupts, no critical sections and no cache. The
 *   "interleavings" below are hand-enumerated step boundaries in a
 *   single-threaded program, and the competing transport runs to completion
 *   in one go when it runs at all. A model of a race is not a race: it can
 *   show that an ordering is unsound, it cannot show that an ordering is safe
 *   on PIC32MZ under FreeRTOS. Only hardware can do that, and #971 exists
 *   precisely because a losing arbitration cannot be aimed at from a client.
 *
 * * **It does not establish the caller-side property** — that each of the six
 *   arm sites *ends* on refusal rather than falling through to its readiness
 *   poll (LISt/DELete/SPACe) or returning SCPI_RES_OK having armed nothing
 *   (CRC/GET/FORmat). That is property 3 of the task this file came from, and
 *   it is deliberately absent. See "PROPERTY 3" below.
 *
 * * It says nothing about whether the real clear clears the real field,
 *   whether the real retraction retracts, or whether the real release
 *   releases. Those are one-line calls into the SD manager, reviewed by eye.
 *   What is modelled here is the ORDER they run in, which is what three
 *   separate defects got wrong.
 *
 * ---------------------------------------------------------------------------
 * WHY NOT TEST THE REAL FUNCTION (option A, and why it was rejected)
 * ---------------------------------------------------------------------------
 * The strongest available shape would have been to extract the ~8 lines above
 * into a dependency-free header-only helper (no Harmony/libscpi/FreeRTOS types
 * in its signature) that SD_ArmOrRefuseWithCleanup then calls, so this test
 * could #include and run the REAL ordering logic — the pattern
 * `test_fixedpointfmt.c` and `test_ad7609_scale.c` already use.
 *
 * It was built, and then rejected on evidence, for two reasons:
 *
 *   (a) `tools/lint/scpi_sd_arm_path.py` asserts that ordering TEXTUALLY,
 *       inside this function's body. Run against the extracted variant it
 *       fails outright:
 *         "SD_ArmOrRefuseWithCleanup() never calls
 *          sd_card_manager_ReleaseClaim(), so a claim once taken is never
 *          released ..."
 *       Every token that checker needs — the arm, the `mode` clear, the
 *       `onRefused` NULL-guard, both releases — is exactly what an extraction
 *       moves out. There is no extraction that leaves the checker's premises
 *       standing, short of writing the calls in a macro purely to keep the
 *       grep happy, which is gaming a gate rather than passing it.
 *
 *   (b) The extraction replaces three plain calls with three indirect calls
 *       through a struct of function pointers, in a function whose one virtue
 *       is being obvious on sight. Two of the three defects in its lineage
 *       were "the statement lives in the wrong function"; moving statements
 *       into another function to make them testable trades away the last
 *       line of defence that actually caught them — review — for the order of
 *       three stubbed callbacks.
 *
 * ---------------------------------------------------------------------------
 * WHY THERE IS NO PROPERTY-ASSERTING DRIFT GREP
 * ---------------------------------------------------------------------------
 * `test_943_bench_stall_bound.c` pins two `#define` constants with a grep,
 * and the obvious move here was a grep asserting "the clear appears, then the
 * retraction, then the release". That was deliberately not done. PR #976
 * spent three review rounds cataloguing FIFTEEN ways an honest refactor
 * defeats exactly that kind of textual assertion on exactly this function,
 * and a second, naive copy of it in a Makefile would give false comfort while
 * the careful one is being narrowed for over-claiming. The pin used instead
 * is a content hash, which claims only what it can support: "this text is
 * unchanged". A refactor cannot slip past a hash, and a hash cannot pretend
 * to have understood the code.
 *
 * ---------------------------------------------------------------------------
 * PROPERTY 3: the caller-side "ends on refusal", and the seam that would
 * make it testable
 * ---------------------------------------------------------------------------
 * Not tested here, and not testable on a host in any honest form today:
 *
 *   - Including the real callers is out: they are 60-150 line libscpi
 *     callbacks in the same non-host-includable file.
 *   - Re-implementing six `if (!SD_ArmOrRefuse(...)) { return ...; }` shapes
 *     against mocks would assert only that the six copies in THIS file behave
 *     as written. It binds to nothing, and its only link to the firmware
 *     would be another textual guard — see the section above.
 *
 * The smallest seam that would change that: fold the arm and the readiness
 * poll into ONE helper, so refusal-ends-before-poll is true by construction
 * at a single site instead of being asserted six times, e.g.
 *
 *     static bool SD_ArmAndWait(scpi_t *ctx, const char *cmd,
 *                               sd_card_manager_settings_t *cfg,
 *                               uint32_t timeoutMs, bool pumped);
 *
 * That is a real refactor, not an extraction: only three of the six sites
 * poll at all (LISt uses the PUMPED wait, DELete and SPACe the plain one,
 * with different timeouts, different log text and different post-checks),
 * and CRC/GET/FORmat return without waiting. It also would not settle the
 * census question — "are these the only arm sites?" is a whole-program
 * property no host test can see. Recorded here rather than attempted,
 * because the honest answer to "can this be tested?" is worth as much as
 * the test.
 * ========================================================================== */

#include "test_framework.h"
#include <stdbool.h>
#include <stddef.h>

/* ==========================================================================
 * The modelled world
 *
 * Only the state the ordering reasons about. Names mirror the firmware.
 *
 * `mode` lives in the SHARED settings object — BoardRunTimeConfig_Get(
 * BOARDRUNTIME_SD_CARD_SETTINGS) hands every SCPI transport the same struct,
 * which is why one owner's late store lands on another owner's operation.
 *
 * The claim is sd_card_manager.c's gSdScpiClaim. TryClaim (sd_card_manager.c
 * :3980-3989) grants iff `!gSdScpiClaim && !IsBusyLocked()`, and IsBusyLocked
 * is `mode != MODE_NONE` OR the state machine being off IDLE/INIT
 * (:4004-4031). The model carries the first term only; the state-machine term
 * is not modelled, so the model is if anything MORE permissive about letting
 * the competing transport in than the firmware is.
 * ========================================================================== */

enum {
    MODEL_MODE_NONE   = 0,   /* SD_CARD_MANAGER_MODE_NONE   */
    MODEL_MODE_READ   = 1,   /* SD_CARD_MANAGER_MODE_READ   */
    MODEL_MODE_FORMAT = 5    /* SD_CARD_MANAGER_MODE_FORMAT */
};

typedef enum {
    EV_CLEAR_MODE = 1,
    EV_RETRACT,
    EV_RELEASE
} ModelEvent;

#define MODEL_MAX_EVENTS 8

typedef struct {
    /* shared device state */
    bool claimHeld;          /* gSdScpiClaim                                  */
    int  mode;               /* cfg->mode, the shared settings field          */
    bool formatPending;      /* what SetFormatPending/ClearFormatStatus move  */

    /* event trace */
    ModelEvent events[MODEL_MAX_EVENTS];
    int        eventCount;

    /* snapshot taken INSIDE the release stub, before ownership is dropped */
    bool releaseSeen;
    int  modeAtRelease;
    bool formatPendingAtRelease;
    int  eventsAtRelease;

    /* cooperative scheduler: offer the competing transport the CPU at hook N */
    int  hookCount;
    int  preemptAt;          /* -1 = never */

    /* does a refused arm clear `mode` itself? True mirrors the firmware
     * today (sd_card_manager.c:3566); false is the robustness variant. */
    bool armClearsMode;

    /* the competing transport ("owner B") */
    bool bHasRun;
    bool bArmed;
    int  bArmedMode;
} Model;

static Model g_m;
static bool  g_retractCalled;

static void model_reset(int preemptAt, bool armClearsMode)
{
    int i;
    g_m.claimHeld = false;
    g_m.mode = MODEL_MODE_NONE;
    g_m.formatPending = false;
    for (i = 0; i < MODEL_MAX_EVENTS; i++) {
        g_m.events[i] = (ModelEvent)0;
    }
    g_m.eventCount = 0;
    g_m.releaseSeen = false;
    g_m.modeAtRelease = -1;
    g_m.formatPendingAtRelease = false;
    g_m.eventsAtRelease = -1;
    g_m.hookCount = 0;
    g_m.preemptAt = preemptAt;
    g_m.armClearsMode = armClearsMode;
    g_m.bHasRun = false;
    g_m.bArmed = false;
    g_m.bArmedMode = MODEL_MODE_NONE;
    g_retractCalled = false;
}

static void model_record(ModelEvent ev)
{
    if (g_m.eventCount < MODEL_MAX_EVENTS) {
        g_m.events[g_m.eventCount] = ev;
    }
    g_m.eventCount++;   /* counts past the cap so an overflow stays visible */
}

/* ---------------------------------------------------------------------------
 * Owner B — the other SCPI transport, running its own FORmat.
 *
 * One shot per scenario, and it runs to completion: claim, publish, write
 * `mode` last, arm (accepted for B), release. That is the worst case for a
 * late store by owner A, which is the case worth modelling.
 * --------------------------------------------------------------------------- */
static void owner_b_try(void)
{
    if (g_m.bHasRun) {
        return;
    }
    g_m.bHasRun = true;

    if (g_m.claimHeld || g_m.mode != MODEL_MODE_NONE) {
        return;                         /* TryClaim refused — B does nothing */
    }
    g_m.claimHeld = true;               /* TryClaim granted                  */
    g_m.formatPending = true;           /* SetFormatPending, under the claim */
    g_m.mode = MODEL_MODE_FORMAT;       /* #829: `mode` is written LAST      */
    g_m.bArmed = true;                  /* its arm is accepted               */
    g_m.bArmedMode = g_m.mode;
    g_m.claimHeld = false;              /* ReleaseClaim, success path        */
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
 * The arm, and the three primitives the settle sequence orders.
 * --------------------------------------------------------------------------- */

/* sd_card_manager_UpdateSettings(cfg). On refusal the real one clears
 * `mode` in the shared object before returning false — see the header. */
static bool model_arm(bool verdict)
{
    if (!verdict && g_m.armClearsMode) {
        g_m.mode = MODEL_MODE_NONE;
    }
    model_hook();
    return verdict;
}

static void model_clear_mode(void)
{
    g_m.mode = MODEL_MODE_NONE;         /* cfg->mode = SD_CARD_MANAGER_MODE_NONE */
    model_record(EV_CLEAR_MODE);
    model_hook();
}

static void model_retraction_cb(void)
{
    g_retractCalled = true;
    g_m.formatPending = false;          /* sd_card_manager_ClearFormatStatus() */
    model_record(EV_RETRACT);
    model_hook();
}

static void model_release_claim(void)
{
    /* Snapshot BEFORE dropping ownership: this is what the next owner
     * inherits at the instant the claim becomes takeable. */
    if (!g_m.releaseSeen) {
        g_m.releaseSeen = true;
        g_m.modeAtRelease = g_m.mode;
        g_m.formatPendingAtRelease = g_m.formatPending;
        g_m.eventsAtRelease = g_m.eventCount;
    }
    g_m.claimHeld = false;              /* sd_card_manager_ReleaseClaim()  */
    model_record(EV_RELEASE);
    model_hook();
}

/* ---------------------------------------------------------------------------
 * The helper, parameterised by ordering policy.
 *
 * SETTLE_PRODUCTION is the body quoted at the top of this file. The other two
 * are the historical defects, present so the model can be shown to have teeth.
 * --------------------------------------------------------------------------- */
typedef enum {
    SETTLE_PRODUCTION,            /* clear -> retract -> release  (#955+#964) */
    SETTLE_RELEASE_FIRST,         /* release -> clear -> retract  (pre-#955)  */
    SETTLE_RETRACT_AFTER_RELEASE  /* clear -> release -> retract  (pre-#964)  */
} SettleOrder;

static bool model_arm_or_refuse(SettleOrder order, bool armVerdict,
                                void (*onRefused)(void))
{
    model_hook();                       /* boundary 0: before anything runs */

    if (model_arm(armVerdict)) {
        model_release_claim();
        return true;
    }

    switch (order) {
    case SETTLE_PRODUCTION:
        model_clear_mode();
        if (onRefused != NULL) { onRefused(); }
        model_release_claim();
        break;

    case SETTLE_RELEASE_FIRST:
        model_release_claim();
        model_clear_mode();
        if (onRefused != NULL) { onRefused(); }
        break;

    case SETTLE_RETRACT_AFTER_RELEASE:
        model_clear_mode();
        model_release_claim();
        if (onRefused != NULL) { onRefused(); }
        break;
    }
    return false;
}

/* Step boundaries on the refusal path: one before anything, one after the
 * arm, and one after each of the three primitives. */
#define REFUSAL_HOOKS 5

/* ---------------------------------------------------------------------------
 * Assertion helper: the event trace, in order.
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

/* Owner A up to the point of arming: it holds the claim, has published
 * whatever it publishes, and has written `mode` LAST (#829). */
static void owner_a_claim_and_publish(int mode, bool publishFormat)
{
    g_m.claimHeld = true;
    g_m.formatPending = publishFormat;
    g_m.mode = mode;
}

/* ==========================================================================
 * 1-2. Refusal with a retraction (the FORmat shape)
 * ========================================================================== */
TEST(refusal_clears_mode_then_retracts_then_releases)
{
    static const ModelEvent want[] = { EV_CLEAR_MODE, EV_RETRACT, EV_RELEASE };
    bool armed;

    model_reset(-1, true);
    owner_a_claim_and_publish(MODEL_MODE_FORMAT, true);

    armed = model_arm_or_refuse(SETTLE_PRODUCTION, false, model_retraction_cb);

    ASSERT_FALSE(armed);
    ASSERT_TRUE(trace_is(want, 3));
    ASSERT_TRUE(g_retractCalled);
    ASSERT_EQ(g_m.mode, MODEL_MODE_NONE);
    ASSERT_FALSE(g_m.formatPending);
    ASSERT_FALSE(g_m.claimHeld);
}

TEST(refusal_state_is_settled_at_the_instant_of_release)
{
    model_reset(-1, true);
    owner_a_claim_and_publish(MODEL_MODE_FORMAT, true);

    (void)model_arm_or_refuse(SETTLE_PRODUCTION, false, model_retraction_cb);

    /* Snapshot taken inside the release stub, before ownership was dropped.
     * Both cleanups had already run, so the next owner cannot inherit either. */
    ASSERT_TRUE(g_m.releaseSeen);
    ASSERT_EQ(g_m.modeAtRelease, MODEL_MODE_NONE);
    ASSERT_FALSE(g_m.formatPendingAtRelease);
    ASSERT_EQ(g_m.eventsAtRelease, 2);     /* clear + retract already logged */
}

TEST(refusal_settles_before_release_even_if_the_arm_clears_nothing)
{
    /* Same assertion with the arm's own `mode` clear taken away: the helper's
     * store is what carries the property, not the arm's side effect. */
    model_reset(-1, false);
    owner_a_claim_and_publish(MODEL_MODE_FORMAT, true);

    (void)model_arm_or_refuse(SETTLE_PRODUCTION, false, model_retraction_cb);

    ASSERT_TRUE(g_m.releaseSeen);
    ASSERT_EQ(g_m.modeAtRelease, MODEL_MODE_NONE);
    ASSERT_FALSE(g_m.formatPendingAtRelease);
}

TEST(refusal_releases_the_claim_exactly_once)
{
    int i, releases = 0;

    model_reset(-1, true);
    owner_a_claim_and_publish(MODEL_MODE_FORMAT, true);

    (void)model_arm_or_refuse(SETTLE_PRODUCTION, false, model_retraction_cb);

    for (i = 0; i < g_m.eventCount && i < MODEL_MAX_EVENTS; i++) {
        if (g_m.events[i] == EV_RELEASE) { releases++; }
    }
    ASSERT_EQ(releases, 1);
}

/* ==========================================================================
 * 3. Refusal with no retraction — the CRC/GET/LISt/DELete/SPACe shape
 * ========================================================================== */
TEST(refusal_without_retraction_still_clears_then_releases)
{
    static const ModelEvent want[] = { EV_CLEAR_MODE, EV_RELEASE };
    bool armed;

    model_reset(-1, true);
    owner_a_claim_and_publish(MODEL_MODE_READ, false);   /* e.g. SD:GET */

    armed = model_arm_or_refuse(SETTLE_PRODUCTION, false, NULL);

    ASSERT_FALSE(armed);
    ASSERT_TRUE(trace_is(want, 2));
    ASSERT_FALSE(g_retractCalled);         /* nothing to retract, nothing ran */
    ASSERT_EQ(g_m.mode, MODEL_MODE_NONE);
    ASSERT_FALSE(g_m.claimHeld);
    ASSERT_EQ(g_m.modeAtRelease, MODEL_MODE_NONE);
}

/* ==========================================================================
 * 4. Success — release only; mode untouched, retraction never invoked
 * ========================================================================== */
TEST(success_releases_and_touches_nothing_else)
{
    static const ModelEvent want[] = { EV_RELEASE };
    bool armed;

    model_reset(-1, true);
    owner_a_claim_and_publish(MODEL_MODE_FORMAT, true);  /* as the caller set it */

    armed = model_arm_or_refuse(SETTLE_PRODUCTION, true, model_retraction_cb);

    ASSERT_TRUE(armed);
    ASSERT_TRUE(trace_is(want, 1));
    ASSERT_FALSE(g_retractCalled);          /* retraction is refusal-only   */
    ASSERT_EQ(g_m.mode, MODEL_MODE_FORMAT); /* success path does not clear  */
    ASSERT_TRUE(g_m.formatPending);         /* nor retract the publication  */
    ASSERT_FALSE(g_m.claimHeld);            /* but it DOES release          */

    /* And the armed operand was already in place when ownership was dropped:
     * `mode != MODE_NONE` is what keeps IsBusy() true across the handover. */
    ASSERT_EQ(g_m.modeAtRelease, MODEL_MODE_FORMAT);
}

TEST(success_without_retraction_is_the_same_release_only_path)
{
    static const ModelEvent want[] = { EV_RELEASE };

    model_reset(-1, true);
    owner_a_claim_and_publish(MODEL_MODE_READ, false);

    ASSERT_TRUE(model_arm_or_refuse(SETTLE_PRODUCTION, true, NULL));
    ASSERT_TRUE(trace_is(want, 1));
    ASSERT_EQ(g_m.mode, MODEL_MODE_READ);
    ASSERT_FALSE(g_m.claimHeld);
}

/* ==========================================================================
 * 5. Discrimination — the competing transport, at every step boundary
 *
 * Scenario: owner A holds the claim, has published format-pending and written
 * mode=FORMAT, and its arm is REFUSED. Owner B is offered the CPU at one step
 * boundary of A's settle sequence. If B gets the claim it runs its own FORmat
 * to completion.
 *
 * The invariant: anything B armed must survive. If B armed, then at the end
 * `mode` must still be B's operand and B's format publication must still
 * stand — A has no business writing either after it stopped being the owner.
 * ========================================================================== */
typedef struct {
    bool bArmed;
    bool operandIntact;       /* B's `mode` survived A's remaining stores   */
    bool publicationIntact;   /* B's format-pending survived them           */
} RaceOutcome;

static RaceOutcome run_race(SettleOrder order, int preemptAt, bool armClearsMode)
{
    RaceOutcome out;

    model_reset(preemptAt, armClearsMode);
    owner_a_claim_and_publish(MODEL_MODE_FORMAT, true);

    (void)model_arm_or_refuse(order, false, model_retraction_cb);

    out.bArmed = g_m.bArmed;
    out.operandIntact = (!g_m.bArmed) || (g_m.mode == g_m.bArmedMode);
    out.publicationIntact = (!g_m.bArmed) || g_m.formatPending;
    return out;
}

TEST(production_order_survives_every_injection_point)
{
    int variant, i;

    for (variant = 0; variant < 2; variant++) {
        int armedAt = 0;
        for (i = 0; i < REFUSAL_HOOKS; i++) {
            RaceOutcome r = run_race(SETTLE_PRODUCTION, i, variant == 0);
            ASSERT_TRUE(r.operandIntact);
            ASSERT_TRUE(r.publicationIntact);
            if (r.bArmed) { armedAt++; }
        }
        /* The sweep must not be vacuous: at least one injection point has to
         * actually let B claim and arm, or "intact" holds for want of anyone
         * to damage. With the production order that point is after the
         * release — which is the whole design. */
        ASSERT_TRUE(armedAt > 0);
    }
}

TEST(release_before_clear_loses_the_955_race)
{
    int i;
    int brokenOperand = 0;

    for (i = 0; i < REFUSAL_HOOKS; i++) {
        RaceOutcome r = run_race(SETTLE_RELEASE_FIRST, i, true);
        if (r.bArmed && !r.operandIntact) { brokenOperand++; }
    }

    /* Exactly the #955 defect. The refused arm has already put `mode` back to
     * NONE (sd_card_manager.c:3566), so the early release leaves nothing
     * holding B off: B claims, arms, and A's late `mode = MODE_NONE` zeroes
     * B's operand. The SD task is left with nothing to run and nobody is
     * told. */
    ASSERT_TRUE(brokenOperand > 0);
}

TEST(release_before_clear_loses_even_if_the_arm_clears_nothing)
{
    int i;
    int broken = 0;

    /* The same order is still unsound when the arm leaves `mode` alone — B
     * then gets in one step later, after A's own clear, and it is the
     * retraction that lands on B. So the verdict on this ordering does not
     * depend on the arm's side effect; only the field it damages does. */
    for (i = 0; i < REFUSAL_HOOKS; i++) {
        RaceOutcome r = run_race(SETTLE_RELEASE_FIRST, i, false);
        if (r.bArmed && (!r.operandIntact || !r.publicationIntact)) { broken++; }
    }
    ASSERT_TRUE(broken > 0);
}

TEST(retraction_after_release_loses_the_964_race)
{
    int i;
    int brokenPublication = 0;

    for (i = 0; i < REFUSAL_HOOKS; i++) {
        RaceOutcome r = run_race(SETTLE_RETRACT_AFTER_RELEASE, i, true);
        if (r.bArmed && !r.publicationIntact) { brokenPublication++; }
    }

    /* Exactly the #964 defect: the `mode` clear is correctly under the claim,
     * so the #955 symptom is gone — and the retraction still lands past the
     * release, on the next owner's published format status. Fixing one field
     * and leaving its twin is how this lineage kept recurring. */
    ASSERT_TRUE(brokenPublication > 0);
}

TEST(the_964_shape_still_protects_the_mode_field)
{
    int i;

    /* Stated so the two defect shapes are not read as interchangeable: the
     * pre-#964 order gets `mode` right at every injection point. Its failure
     * is confined to the field the helper did not yet own — which is why
     * #955's fix did not prevent #964. */
    for (i = 0; i < REFUSAL_HOOKS; i++) {
        RaceOutcome r = run_race(SETTLE_RETRACT_AFTER_RELEASE, i, true);
        ASSERT_TRUE(r.operandIntact);
    }
}

int main(void)
{
    printf("=== #971: SD arm-refusal settle order (host model) ===\n");

    RUN(refusal_clears_mode_then_retracts_then_releases);
    RUN(refusal_state_is_settled_at_the_instant_of_release);
    RUN(refusal_settles_before_release_even_if_the_arm_clears_nothing);
    RUN(refusal_releases_the_claim_exactly_once);
    RUN(refusal_without_retraction_still_clears_then_releases);
    RUN(success_releases_and_touches_nothing_else);
    RUN(success_without_retraction_is_the_same_release_only_path);
    RUN(production_order_survives_every_injection_point);
    RUN(release_before_clear_loses_the_955_race);
    RUN(release_before_clear_loses_even_if_the_arm_clears_nothing);
    RUN(retraction_after_release_loses_the_964_race);
    RUN(the_964_shape_still_protects_the_mode_field);

    return TEST_SUMMARY();
}
