/* ==========================================================================
 * test_1060_apsta_disconnect_retry.c -- issue #1060 / PR #1078, r1 audit
 *
 * THE AP->STA DISCONNECT RETRY, run through the OLD (one attempt, log-only)
 * and NEW (re-attempt across the settle) shapes so every property is a PAIR.
 *
 * WHY A MODEL. wifi_manager.c pulls in the WINC driver, FreeRTOS and the whole
 * board graph, so it cannot be compiled on the host -- the same trade #889 made
 * for AD7609Scale.h, #1092 for PowerApi.c and #1124 for sd_card_manager.c. The
 * decision structure is modelled here; each structural claim it rests on is
 * pinned against the real source in $(APSTA1060_BIN)'s recipe.
 *
 * ---------------------------------------------------------------------------
 * THE DEFECT, from the r1 audit (both legs, independently):
 *
 *   A soft-AP client is associated. APPLY switches to STA. The transport fails
 *   for both M2M_WIFI_REQ_DISABLE_AP and M2M_WIFI_REQ_DISCONNECT (hif_send ->
 *   M2M_ERR_BUS_FAIL), then RECOVERS. Neither request reached the chip, so the
 *   driver's isConnected stays true. The branch cleared every mode flag and
 *   only LOGGED the disconnect failure, so ERROR -> REINIT takes the fresh-init
 *   arm, where WDRV_WINC_Initialize and WDRV_WINC_Open return the existing
 *   instance without running wincResetCtrlDcpt -- isConnected is never cleared
 *   and every later WDRV_WINC_IPUseDHCPSet is refused. For ever, after the
 *   transport is healthy again.
 *
 * ⭐ NEW BY SUBTRACTION, which is what decides the fix. Nothing this PR added
 * misbehaves. The stale STA_CONNECTED it REMOVES was accidentally routing
 * ERROR -> REINIT into the arm that retried the disconnect, so that protection
 * was real while the flag was wrong.
 *
 * ⛔ AND THE SUBTRACTED PROTECTION WAS UNBOUNDED, which r2 missed and the r2
 * audit caught. ERROR -> REINIT repeats, so the accidental retry ran on EVERY
 * pass until it succeeded: any transient fault self-healed, however long it
 * lasted. r2 replaced that with a retry bounded to the 500 ms settle, which
 * still turns a fault that outlasts the window into a PERMANENT one -- so for
 * this path the wedge was new, not the pre-existing class I had labelled it.
 * Bounding an unbounded retry is a regression dressed as a fix.
 *
 * r3 restores the unbounded behaviour with a DEDICATED teardown-pending flag:
 * set on exhaustion, cleared when a disconnect reaches the chip, re-attempted
 * on every REINIT pass while set. Re-setting STA_CONNECTED would also have
 * restored it -- and would have reinstated the wrong link-state report this PR
 * exists to fix, because one flag would again answer two questions. A separate
 * fact gets a separate flag.
 * ========================================================================== */

#include "test_framework.h"
#include <stdbool.h>
#include <string.h>

/* Mirrors the firmware's constants; the recipe pins these against the source,
 * including that their PRODUCT is still the 500 ms settle. */
#define SETTLE_SLICES   5u
#define SETTLE_SLICE_MS 100u

/* The WDRV_WINC statuses this decision actually distinguishes. REQUEST_ERROR is
 * the harmless "isConnected was already false" answer, which is why it counts
 * as done rather than as a failure -- conflating it with a real failure would
 * retry on every ordinary AP->STA switch. */
typedef enum { ST_OK = 0, ST_REQUEST_ERROR, ST_DISCONNECT_FAIL } Status;

typedef struct {
    bool chipConnected;      /* the driver's internal isConnected */
    unsigned failuresLeft;   /* transport failures before it recovers */
    bool staConnectedFlag;   /* the LINK-STATE flag GetLinkState() reports */
    bool teardownPending;    /* r3's dedicated "a teardown is outstanding" */
    /* observations */
    unsigned attempts;
    unsigned settleMs;
    bool     loggedFailure;
} Chip;

typedef struct {
    bool clearsModeFlags;    /* this PR's fix: the link state becomes honest */
    bool retryAcrossSettle;  /* r2: bounded re-attempt during the settle */
    bool teardownPendingFlag;/* r3: a dedicated flag drives an UNBOUNDED retry */
    bool flagSurvivesEntry;  /* r4: ENTRY's ResetAllEventFlags preserves it */
} Shape;

/* BASE -- before this PR. The stale STA_CONNECTED is left set, which reports a
 * link that does not exist (the #1060 defect) and ALSO, by accident, routes
 * every ERROR -> REINIT through STA reconfiguration, which retries the
 * disconnect indefinitely. So the base self-heals, for the wrong reason. */
static const Shape SHAPE_BASE = { false, false, false, false };
/* r1 -- flags cleared, one logged attempt. Honest link state, and the
 * accidental unbounded retry is gone with it: a transient fault is permanent. */
static const Shape SHAPE_R1   = { true,  false, false, false };
/* r2 -- the retry is BOUNDED to the settle window. Better, and still permanent
 * for any fault that outlasts the window. This is what the r2 audit rejected. */
static const Shape SHAPE_R2   = { true,  true,  false, false };
/* r3 -- bounded retry PLUS a dedicated teardown-pending flag. It claimed to
 * re-attempt on every REINIT pass, and it did not: a FAILED REINIT-side
 * attempt falls through to fresh-init, whose MainState ENTRY memsets the whole
 * flag word and erases the marker. One failed retry and the unbounded retry is
 * gone again -- the r3 audit's HIGH. */
static const Shape SHAPE_R3   = { true,  true,  true,  false };
/* r4 -- the marker survives ENTRY, preserved there exactly as the
 * firmware-update request already was. Only now is the retry truly unbounded. */
static const Shape SHAPE_NEW  = { true,  true,  true,  true  };

static Chip chip(bool connected, unsigned failures) {
    Chip c;
    memset(&c, 0, sizeof(c));
    c.chipConnected = connected;
    c.failuresLeft = failures;
    /* An AP client association set the shared flag -- the situation #1060 is
     * about. Whether the fix clears it is the shape's business. */
    c.staConnectedFlag = true;
    return c;
}

/* What GetLinkState() would report. The #1060 defect is this being true with
 * no STA link, and no fix may reintroduce it. */
static bool model_link_reported_connected(const Chip *c) {
    return c->staConnectedFlag;
}

/* WDRV_WINC_BSSDisconnect(): reaches the chip unless the transport is still
 * failing. When it reaches a connected chip it clears isConnected. */
static Status model_disconnect(Chip *c) {
    c->attempts++;
    if (c->failuresLeft > 0u) {
        c->failuresLeft--;
        return ST_DISCONNECT_FAIL;       /* hif_send -> M2M_ERR_BUS_FAIL */
    }
    if (!c->chipConnected) {
        return ST_REQUEST_ERROR;         /* already disconnected: harmless */
    }
    c->chipConnected = false;
    return ST_OK;
}

static bool status_is_done(Status s) {
    return (s == ST_OK) || (s == ST_REQUEST_ERROR);
}

/* The AP->STA APPLY branch, from the disconnect to SendEvent(INIT). */
static void model_apsta_switch(Chip *c, Shape s) {
    if (s.clearsModeFlags) {
        c->staConnectedFlag = false;     /* the link state becomes honest */
    }
    Status st = model_disconnect(c);
    if (!s.retryAcrossSettle) {
        c->settleMs = SETTLE_SLICES * SETTLE_SLICE_MS;
        if (!status_is_done(st)) {
            c->loggedFailure = true;
            if (s.teardownPendingFlag) {
                c->teardownPending = true;
            }
        }
        return;
    }
    for (unsigned i = 0; i < SETTLE_SLICES; i++) {
        c->settleMs += SETTLE_SLICE_MS;
        if (status_is_done(st)) {
            continue;                    /* done; just finish settling */
        }
        st = model_disconnect(c);
    }
    if (!status_is_done(st)) {
        c->loggedFailure = true;
        /* r3: exhausted, not finished. */
        if (s.teardownPendingFlag) {
            c->teardownPending = true;
        }
    }
}

/* ERROR -> REINIT takes the fresh-init arm once the mode flags are clear, and
 * that arm does NOT clear isConnected -- so a chip still marked connected
 * refuses DHCP for ever, however healthy the transport has become. */
static bool model_dhcp_would_be_refused(const Chip *c) {
    return c->chipConnected;
}

/* ⛔ MainState ENTRY. ResetAllEventFlags() is a memset of the whole flag word,
 * so EVERY flag in it dies here unless something preserves it -- the
 * firmware-update request is read into a local before the reset and re-set
 * after, and r4 does the same for the teardown marker.
 *
 * ⭐ THE MODEL COULD NOT REPRESENT THIS TRANSITION, which is exactly why it
 * passed r3 while r3's central claim was false. A model that cannot express a
 * state change cannot test any property that depends on one. */
static void model_mainstate_entry(Chip *c, Shape s) {
    if (!s.flagSurvivesEntry) {
        c->teardownPending = false;      /* erased by the memset */
    }
}

/* One ERROR -> REINIT pass. THREE different things can happen here, and which
 * one is the whole subject of this round:
 *   BASE  the stale STA_CONNECTED routes into STA reconfiguration, which
 *         retries the disconnect -- unbounded, because REINIT repeats;
 *   r1/r2 the flags are clear, so the fresh-init arm runs and clears nothing;
 *   r3    the dedicated flag re-attempts the disconnect, unbounded, without
 *         consulting the link-state flag at all.
 */
static void model_reinit_pass(Chip *c, Shape s) {
    if (s.teardownPendingFlag && c->teardownPending) {
        Status st = model_disconnect(c);
        if (status_is_done(st)) {
            c->teardownPending = false;
            return;
        }
        /* The retry FAILED, so REINIT falls through to the fresh-init arm,
         * which re-enters MainState and runs its ENTRY. This fall-through is
         * the step the model was missing. */
        model_mainstate_entry(c, s);
        return;
    }
    if (!s.clearsModeFlags && c->staConnectedFlag) {
        (void)model_disconnect(c);   /* the accidental unbounded retry */
        return;
    }
    /* fresh-init arm: returns the existing instance, clears nothing */
}

/* ---- the audit's exact scenario ---------------------------------------- */
/*
 * Two transport failures, then recovery -- "then recovers" is the finding's own
 * wording, and it is what makes a retry the fix rather than a mitigation.
 */
TEST(r1_loses_recovery_when_the_disconnect_itself_fails) {
    Chip c = chip(true, 2);
    model_apsta_switch(&c, SHAPE_R1);
    ASSERT_EQ(c.attempts, 1u);                       /* one attempt, logged */
    ASSERT_TRUE(c.loggedFailure);
    ASSERT_TRUE(c.chipConnected);                    /* never cleared */
    ASSERT_TRUE(model_dhcp_would_be_refused(&c));    /* ...for ever */
}

TEST(new_shape_recovers_once_the_transport_does) {
    Chip c = chip(true, 2);
    model_apsta_switch(&c, SHAPE_NEW);
    ASSERT_FALSE(c.chipConnected);                   /* cleared on a retry */
    ASSERT_FALSE(model_dhcp_would_be_refused(&c));
    ASSERT_FALSE(c.loggedFailure);
}

/* ---- the common case must not change, in behaviour OR timing ----------- */

TEST(an_already_disconnected_chip_takes_exactly_one_attempt) {
    Chip c = chip(false, 0);
    model_apsta_switch(&c, SHAPE_NEW);
    ASSERT_EQ(c.attempts, 1u);        /* REQUEST_ERROR counts as done */
    ASSERT_FALSE(c.loggedFailure);
    ASSERT_EQ(c.settleMs, 500u);
}

TEST(a_connected_chip_with_a_healthy_transport_takes_one_attempt) {
    Chip c = chip(true, 0);
    model_apsta_switch(&c, SHAPE_NEW);
    ASSERT_EQ(c.attempts, 1u);
    ASSERT_FALSE(c.chipConnected);
    ASSERT_EQ(c.settleMs, 500u);
}

/* ⭐ THE SETTLE TIME IS UNCHANGED IN EVERY CASE, which is the claim that the
 * retry "costs no wall clock". Asserted for the worst case too, not just the
 * happy one -- the retries are spread across the wait, not added to it. */
TEST(the_settle_time_is_500ms_whatever_the_transport_does) {
    for (unsigned failures = 0; failures <= SETTLE_SLICES + 2u; failures++) {
        Chip c = chip(true, failures);
        model_apsta_switch(&c, SHAPE_NEW);
        ASSERT_EQ(c.settleMs, 500u);
        Chip o = chip(true, failures);
        model_apsta_switch(&o, SHAPE_R1);
        ASSERT_EQ(o.settleMs, 500u);   /* the pre-fix wait, for comparison */
    }
}

/* ---- the bound, and the residual it leaves ----------------------------- */

TEST(the_retry_is_bounded_not_a_spin) {
    Chip c = chip(true, 1000);                 /* transport never recovers */
    model_apsta_switch(&c, SHAPE_NEW);
    ASSERT_EQ(c.attempts, SETTLE_SLICES + 1u); /* 1 + one per slice */
}

/*
 * ⚠ THE RESIDUAL, ASSERTED RATHER THAN IMPLIED. If the transport never
 * recovers the wedge remains, and that is the PRE-EXISTING class shared with
 * the three older log-only sites -- it needs a recovery path (a DEINIT, whose
 * WDRV_WINC_Close clears isConnected), not a per-site patch, and it is ticketed
 * as the #1060 design item. Writing it as a test rather than a sentence means
 * nobody can later believe this PR closed it: the day a recovery path lands,
 * this arm fails and has to be rewritten, which is exactly the right signal.
 */
/* ⛔ RETIRED AT r3, and deliberately replaced rather than deleted quietly.
 * This arm asserted the exhaustion wedge and labelled it the pre-existing
 * #1181 class. The r2 audit showed that for THIS path it was NEW -- the
 * subtraction removed an UNBOUNDED retry and r2 only bounded it -- so the label
 * was wrong and the arm was certifying a regression as inherited.
 * a_transport_that_NEVER_recovers_stays_wedged_the_1181_residual below carries
 * the correctly scoped claim. */

/* ⭐ THE BOUNDARY, because "recovers in time" and "recovers too late" are the
 * two sides of what the bound buys, and only one of them was obvious. */
TEST(recovery_on_the_very_last_slice_is_still_caught) {
    Chip c = chip(true, SETTLE_SLICES);   /* fails 1 + 4, succeeds on the 5th */
    model_apsta_switch(&c, SHAPE_NEW);
    ASSERT_FALSE(c.chipConnected);
    ASSERT_FALSE(c.loggedFailure);
    ASSERT_EQ(c.attempts, SETTLE_SLICES + 1u);
}

TEST(recovery_one_slice_too_late_is_not_caught_and_is_reported) {
    Chip c = chip(true, SETTLE_SLICES + 1u);
    model_apsta_switch(&c, SHAPE_NEW);
    ASSERT_TRUE(c.chipConnected);
    ASSERT_TRUE(c.loggedFailure);
}

/* A sweep, so the pair above is a claim about the loop and not about two
 * hand-picked numbers. */
TEST(every_recovery_within_the_window_is_caught_and_none_outside_it) {
    unsigned caught = 0, missed = 0;
    for (unsigned failures = 0; failures <= SETTLE_SLICES + 3u; failures++) {
        Chip c = chip(true, failures);
        model_apsta_switch(&c, SHAPE_NEW);
        if (failures <= SETTLE_SLICES) {
            ASSERT_FALSE(c.chipConnected);
            caught++;
        } else {
            ASSERT_TRUE(c.chipConnected);
            missed++;
        }
    }
    /* ⭐ NON-VACUITY: both halves must be exercised, or the sweep is asserting
     * only one of the two behaviours it exists to separate. */
    ASSERT_EQ(caught, SETTLE_SLICES + 1u);
    ASSERT_EQ(missed, 3u);
}

/* ---- r2's rejected framing, as the four-shape matrix ------------------- */
/*
 * ⭐ THE SEQUENCE THE AUDIT NAMED: the transport fails through the AP stop and
 * every attempt the settle window allows, and THEN recovers. Six failures is
 * one more than the window, which is the smallest input that separates a
 * bounded retry from an unbounded one -- and separating exactly those two is
 * the point of this round.
 */
#define FAILURES_PAST_THE_WINDOW (SETTLE_SLICES + 1u)

/* How many REINIT passes to run. The property under test is "it heals
 * EVENTUALLY", so this must comfortably exceed the fault duration -- otherwise
 * a shape that retries once per pass is starved rather than tested, and its arm
 * fails for the model's reason instead of the firmware's. (Measured: at 4
 * passes the BASE shape got 5 attempts against 6 failures and read as "never
 * healed", which is the opposite of what BASE actually did.) The shapes that
 * never heal, r1 and r2, never heal at ANY pass count -- which is the real
 * discriminator and is why raising this does not weaken the matrix. */
#define REINIT_PASSES 10u

static Chip run_sequence(Shape s, unsigned failures) {
    Chip c = chip(true, failures);
    model_apsta_switch(&c, s);
    for (unsigned i = 0; i < REINIT_PASSES; i++) {
        model_reinit_pass(&c, s);
    }
    return c;
}

TEST(base_self_healed_but_lied_about_the_link) {
    Chip c = run_sequence(SHAPE_BASE, FAILURES_PAST_THE_WINDOW);
    ASSERT_FALSE(model_dhcp_would_be_refused(&c));   /* it DID self-heal */
    /* ...and this is why it is not the answer: it reported a link that was
     * never there, which is the defect #1060 was filed for. */
    ASSERT_TRUE(model_link_reported_connected(&c));
}

TEST(r1_turns_a_transient_fault_into_a_permanent_one) {
    Chip c = run_sequence(SHAPE_R1, FAILURES_PAST_THE_WINDOW);
    ASSERT_TRUE(model_dhcp_would_be_refused(&c));
    ASSERT_FALSE(model_link_reported_connected(&c));  /* honest, but wedged */
}

TEST(r2_only_bounded_it_so_the_fault_is_still_permanent) {
    Chip c = run_sequence(SHAPE_R2, FAILURES_PAST_THE_WINDOW);
    ASSERT_TRUE(model_dhcp_would_be_refused(&c));
    ASSERT_FALSE(model_link_reported_connected(&c));
}

TEST(r3_self_heals_AND_keeps_the_link_state_honest) {
    Chip c = run_sequence(SHAPE_NEW, FAILURES_PAST_THE_WINDOW);
    ASSERT_FALSE(model_dhcp_would_be_refused(&c));    /* base's recovery back */
    ASSERT_FALSE(model_link_reported_connected(&c));  /* without base's lie */
    ASSERT_FALSE(c.teardownPending);                  /* and the flag cleared */
}

/* ⭐ The two properties are independent, so assert they hold TOGETHER rather
 * than one per arm. Only r3 has both; each other shape has exactly one. */
TEST(only_r3_has_both_properties) {
    const Shape shapes[] = { SHAPE_BASE, SHAPE_R1, SHAPE_R2, SHAPE_NEW };
    unsigned both = 0;
    for (unsigned i = 0; i < 4; i++) {
        Chip c = run_sequence(shapes[i], FAILURES_PAST_THE_WINDOW);
        if (!model_dhcp_would_be_refused(&c) &&
            !model_link_reported_connected(&c)) {
            both++;
        }
    }
    ASSERT_EQ(both, 1u);
}

/* However long the fault lasts, r3 heals once it clears -- that is what
 * "unbounded" means, and a single duration cannot show it. */
TEST(r3_heals_at_any_fault_duration_within_the_reinit_passes) {
    for (unsigned extra = 0; extra <= REINIT_PASSES; extra++) {
        Chip c = run_sequence(SHAPE_NEW, SETTLE_SLICES + extra);
        ASSERT_FALSE(model_dhcp_would_be_refused(&c));
        ASSERT_FALSE(model_link_reported_connected(&c));
    }
}

/* ⚠ THE RESIDUAL, NOW CORRECTLY SCOPED. r2's version of this arm asserted the
 * wedge for the exhaustion case and called it pre-existing; the audit showed
 * that for this path it was NEW, because the subtraction removed an unbounded
 * retry. Only the NEVER-RECOVERS case is genuinely the #1181 class: no retry
 * policy can help a transport that never comes back, which is why that ticket
 * asks for a recovery path rather than more retries. */
TEST(a_transport_that_NEVER_recovers_stays_wedged_the_1181_residual) {
    Chip c = chip(true, 100000u);          /* never recovers, ever */
    model_apsta_switch(&c, SHAPE_NEW);
    for (unsigned i = 0; i < 50u; i++) {
        model_reinit_pass(&c, SHAPE_NEW);
    }
    ASSERT_TRUE(model_dhcp_would_be_refused(&c));
    ASSERT_TRUE(c.teardownPending);        /* still trying, correctly */
    ASSERT_FALSE(model_link_reported_connected(&c));
}

/* The control the audit asked for: the common path is untouched. No teardown is
 * ever outstanding, so no REINIT pass issues an extra call, and the settle is
 * the same 500 ms. */
TEST(the_common_path_is_unchanged_in_calls_and_timing) {
    Chip c = chip(true, 0);                /* healthy transport */
    model_apsta_switch(&c, SHAPE_NEW);
    ASSERT_EQ(c.attempts, 1u);
    ASSERT_EQ(c.settleMs, 500u);
    ASSERT_FALSE(c.teardownPending);
    const unsigned before = c.attempts;
    for (unsigned i = 0; i < REINIT_PASSES; i++) {
        model_reinit_pass(&c, SHAPE_NEW);
    }
    ASSERT_EQ(c.attempts, before);         /* not one extra disconnect */
}

/* ---- r3's HIGH: the marker did not survive the ENTRY reset ------------- */
/*
 * ⭐ THE SEQUENCE THE r3 AUDIT NAMED: the window's attempts all fail, then ONE
 * REINIT-side attempt also fails -- which is what drops through to fresh-init
 * and erases the flag -- and THEN the transport recovers. r3 ends wedged
 * despite having a flag whose entire purpose was to prevent that; r4 recovers.
 *
 * One failed REINIT attempt is the smallest input that separates them, and r3's
 * own test suite contained nothing that could produce it, because the model had
 * no ENTRY.
 */
TEST(r3_loses_the_marker_after_a_single_failed_reinit_attempt) {
    Chip c = chip(true, SETTLE_SLICES + 2u);   /* window + 1 REINIT, then ok */
    model_apsta_switch(&c, SHAPE_R3);
    ASSERT_TRUE(c.teardownPending);            /* set on exhaustion... */
    model_reinit_pass(&c, SHAPE_R3);           /* ...this attempt fails... */
    ASSERT_FALSE(c.teardownPending);           /* ...and ENTRY erased it */
    for (unsigned i = 0; i < REINIT_PASSES; i++) {
        model_reinit_pass(&c, SHAPE_R3);
    }
    ASSERT_TRUE(model_dhcp_would_be_refused(&c));   /* wedged, transport fine */
}

TEST(r4_keeps_the_marker_and_recovers_after_a_failed_reinit_attempt) {
    Chip c = chip(true, SETTLE_SLICES + 2u);
    model_apsta_switch(&c, SHAPE_NEW);
    ASSERT_TRUE(c.teardownPending);
    model_reinit_pass(&c, SHAPE_NEW);          /* fails, ENTRY runs... */
    ASSERT_TRUE(c.teardownPending);            /* ...and the marker survives */
    for (unsigned i = 0; i < REINIT_PASSES; i++) {
        model_reinit_pass(&c, SHAPE_NEW);
    }
    ASSERT_FALSE(model_dhcp_would_be_refused(&c));
    ASSERT_FALSE(model_link_reported_connected(&c));
    ASSERT_FALSE(c.teardownPending);
}

/* ⭐ The marker may be cleared ONLY by a disconnect that reached the chip. Any
 * number of ENTRYs, on their own, must not clear it -- that is the invariant
 * r3 violated, stated directly rather than only through a scenario. */
TEST(only_a_successful_disconnect_clears_the_marker) {
    Chip c = chip(true, 100000u);              /* never recovers */
    model_apsta_switch(&c, SHAPE_NEW);
    ASSERT_TRUE(c.teardownPending);
    for (unsigned i = 0; i < 20u; i++) {
        model_mainstate_entry(&c, SHAPE_NEW);  /* ENTRY alone, 20 times */
        ASSERT_TRUE(c.teardownPending);
    }
    c.failuresLeft = 0;                        /* transport heals */
    model_reinit_pass(&c, SHAPE_NEW);
    ASSERT_FALSE(c.teardownPending);           /* cleared, and only now */
}

/* r4 must heal whatever MIX of window and REINIT failures occurs, which is the
 * general form of the audit's scenario rather than its one instance. */
TEST(r4_heals_across_any_split_of_window_and_reinit_failures) {
    for (unsigned extra = 0; extra <= REINIT_PASSES; extra++) {
        Chip c = chip(true, SETTLE_SLICES + extra);
        model_apsta_switch(&c, SHAPE_NEW);
        for (unsigned i = 0; i < REINIT_PASSES + 1u; i++) {
            model_reinit_pass(&c, SHAPE_NEW);
        }
        ASSERT_FALSE(model_dhcp_would_be_refused(&c));
    }
}

int main(void) {
    RUN(r1_loses_recovery_when_the_disconnect_itself_fails);
    RUN(new_shape_recovers_once_the_transport_does);
    RUN(an_already_disconnected_chip_takes_exactly_one_attempt);
    RUN(a_connected_chip_with_a_healthy_transport_takes_one_attempt);
    RUN(the_settle_time_is_500ms_whatever_the_transport_does);
    RUN(the_retry_is_bounded_not_a_spin);
    RUN(recovery_on_the_very_last_slice_is_still_caught);
    RUN(recovery_one_slice_too_late_is_not_caught_and_is_reported);
    RUN(every_recovery_within_the_window_is_caught_and_none_outside_it);
    RUN(base_self_healed_but_lied_about_the_link);
    RUN(r1_turns_a_transient_fault_into_a_permanent_one);
    RUN(r2_only_bounded_it_so_the_fault_is_still_permanent);
    RUN(r3_self_heals_AND_keeps_the_link_state_honest);
    RUN(only_r3_has_both_properties);
    RUN(r3_heals_at_any_fault_duration_within_the_reinit_passes);
    RUN(a_transport_that_NEVER_recovers_stays_wedged_the_1181_residual);
    RUN(the_common_path_is_unchanged_in_calls_and_timing);
    RUN(r3_loses_the_marker_after_a_single_failed_reinit_attempt);
    RUN(r4_keeps_the_marker_and_recovers_after_a_failed_reinit_attempt);
    RUN(only_a_successful_disconnect_clears_the_marker);
    RUN(r4_heals_across_any_split_of_window_and_reinit_failures);
    return TEST_SUMMARY();
}
