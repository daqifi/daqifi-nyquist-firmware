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
 * was real while the flag was wrong. Re-setting the flag to get it back would
 * reinstate the wrong link-state report the PR exists to fix; the recovery is
 * therefore restored by making the call succeed instead.
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
    /* observations */
    unsigned attempts;
    unsigned settleMs;
    bool     loggedFailure;
} Chip;

typedef struct {
    bool retryAcrossSettle;  /* false = the pre-fix single attempt */
} Shape;

static const Shape SHAPE_OLD = { false };
static const Shape SHAPE_NEW = { true  };

static Chip chip(bool connected, unsigned failures) {
    Chip c;
    memset(&c, 0, sizeof(c));
    c.chipConnected = connected;
    c.failuresLeft = failures;
    return c;
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
    Status st = model_disconnect(c);
    if (!s.retryAcrossSettle) {
        /* Pre-fix: one attempt, logged, then the settle wait. */
        c->settleMs = SETTLE_SLICES * SETTLE_SLICE_MS;
        if (!status_is_done(st)) {
            c->loggedFailure = true;
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
    }
}

/* ERROR -> REINIT takes the fresh-init arm once the mode flags are clear, and
 * that arm does NOT clear isConnected -- so a chip still marked connected
 * refuses DHCP for ever, however healthy the transport has become. */
static bool model_dhcp_would_be_refused(const Chip *c) {
    return c->chipConnected;
}

/* ---- the audit's exact scenario ---------------------------------------- */
/*
 * Two transport failures, then recovery -- "then recovers" is the finding's own
 * wording, and it is what makes a retry the fix rather than a mitigation.
 */
TEST(old_shape_loses_recovery_when_the_disconnect_itself_fails) {
    Chip c = chip(true, 2);
    model_apsta_switch(&c, SHAPE_OLD);
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
        model_apsta_switch(&o, SHAPE_OLD);
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
TEST(a_transport_that_never_recovers_still_wedges_the_ticketed_residual) {
    Chip c = chip(true, 1000);
    model_apsta_switch(&c, SHAPE_NEW);
    ASSERT_TRUE(c.chipConnected);
    ASSERT_TRUE(model_dhcp_would_be_refused(&c));
    ASSERT_TRUE(c.loggedFailure);     /* at least it is REPORTED, with the tag */
}

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

int main(void) {
    RUN(old_shape_loses_recovery_when_the_disconnect_itself_fails);
    RUN(new_shape_recovers_once_the_transport_does);
    RUN(an_already_disconnected_chip_takes_exactly_one_attempt);
    RUN(a_connected_chip_with_a_healthy_transport_takes_one_attempt);
    RUN(the_settle_time_is_500ms_whatever_the_transport_does);
    RUN(the_retry_is_bounded_not_a_spin);
    RUN(a_transport_that_never_recovers_still_wedges_the_ticketed_residual);
    RUN(recovery_on_the_very_last_slice_is_still_caught);
    RUN(recovery_one_slice_too_late_is_not_caught_and_is_reported);
    RUN(every_recovery_within_the_window_is_caught_and_none_outside_it);
    return TEST_SUMMARY();
}
