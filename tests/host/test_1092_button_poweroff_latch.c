/* ==========================================================================
 * test_1092_button_poweroff_latch.c -- issues #1084 / #1092
 *
 * A button power-off must STICK. Two independent defects could undo it, and
 * this suite pins both by running the SAME fixtures through the OLD and the
 * NEW shape and asserting they disagree. A test that cannot fail on the old
 * code is not a regression test, so every pair below is written as a pair.
 *
 * WHY A MODEL AND NOT THE REAL FUNCTIONS. PowerApi.c includes definitions.h
 * (Harmony), BoardConfig.h, the BQ24297 driver and the WINC headers, so it
 * cannot be compiled on the host -- the same trade #889 made for AD7609Scale.h
 * and #1152 for daqifi_settings.c. The model below is the state machine's
 * decision structure, nothing else, and the Makefile target pins each
 * structural claim it rests on against the real source. If a pin fails, this
 * model is describing firmware that no longer exists and the build says so.
 *
 * ---------------------------------------------------------------------------
 * DEFECT A -- NO_CHANGE conflated two states. (NOT the root cause of the
 * observable defect; see the correction in the DEFECT A section below, which
 * this model produced before it was claimed anywhere.)
 *
 * The common power-down handler set powerState = STANDBY and then CLEARED
 * requestedPowerState to NO_CHANGE, "delegating" the power-down to the STANDBY
 * handler. That handler's auto-promote gate is
 *
 *     autoPowerOnUsb && requestedPowerState == NO_CHANGE
 *
 * and NO_CHANGE means both "nothing was requested" and "a request was just
 * consumed". So the clear hands that gate the first meaning while the second
 * is true. What it costs on the SHIPPED firmware is (1) every consumer of the
 * field is told no shutdown is pending while one is -- including the LED
 * takeover and SCPI_PowerStatusGet's shutdown diagnostic, which emits nothing
 * in that window -- and (2) the conflation survives, so a future poster of
 * DO_POWER_DOWN that forgets the suppression latch resurrects the auto-promote
 * defect. It does NOT currently skip the power-down, because both existing
 * posters set that latch.
 *
 * ---------------------------------------------------------------------------
 * DEFECT B (#1092 r2 residual) -- a latch set only when it was known to be
 * needed.
 *
 * The suppression latch (autoPromotedThisVbusSession) was written from the
 * button path only `if (pgStat)`, i.e. only when the CACHED VBUS reading was
 * already true. On a stale-FALSE reading the latch stayed clear, Power_Tasks
 * then refreshed pgStat to true, and the next STANDBY pass auto-promoted the
 * board back up -- #1084's own symptom reached through the cache.
 *
 * r2 called this unfixable from that site ("you cannot set a latch you do not
 * know you need"). The asymmetry is the answer: setting it when it was NOT
 * needed costs nothing, because Power_HandleStandbyState re-arms the latch on
 * any STANDBY pass without VBUS, so a battery-only press self-corrects on the
 * next pass. FAILING to set it costs the shutdown. So it is now set
 * unconditionally, and `battery_only_press_*` below is the arm that proves the
 * unconditional set is harmless rather than merely convenient.
 * ========================================================================== */

#include "test_framework.h"
#include <stdbool.h>
#include <string.h>

/* ---- the modelled state ------------------------------------------------- */

typedef enum { NO_CHANGE = 0, DO_POWER_UP, DO_POWER_UP_EXT_DOWN,
               DO_POWER_DOWN } PowerReq;
typedef enum { STANDBY = 0, POWERED_UP, POWERED_UP_EXT_DOWN } PowerState;

typedef struct {
    PowerReq   req;
    PowerState state;
    bool autoPowerOnUsb;
    bool pgStat;                      /* the CACHED VBUS reading */
    bool autoPromotedThisVbusSession; /* the suppression latch */
    bool shutdownNotified;
    bool hasSufficientPower;
    /* observations */
    unsigned powerDownCalls;
    unsigned powerUpCalls;
} Model;

/* Which revision of the two sites is being modelled. */
typedef struct {
    bool clearRequestInCommonHandler; /* DEFECT A: true = pre-fix */
    bool latchOnlyWhenPgStat;         /* DEFECT B: true = pre-fix */
} Shape;

static const Shape SHAPE_OLD = { true,  true  };
static const Shape SHAPE_NEW = { false, false };

/* Power_Down(): rails off, then the EXECUTOR clears the request. */
static void model_power_down(Model *m) {
    m->powerDownCalls++;
    m->state = STANDBY;
    m->req = NO_CHANGE;
}

static void model_power_up(Model *m, bool ext) {
    m->powerUpCalls++;
    m->state = ext ? POWERED_UP : POWERED_UP_EXT_DOWN;
}

/* Power_HandleStandbyState() */
static void model_standby(Model *m) {
    if (m->autoPowerOnUsb && m->req == NO_CHANGE) {
        if (!m->pgStat) {
            m->autoPromotedThisVbusSession = false;   /* re-arm */
        } else if (!m->autoPromotedThisVbusSession) {
            m->req = DO_POWER_UP;
            m->autoPromotedThisVbusSession = true;
        }
    }
    if (m->req == DO_POWER_UP || m->req == DO_POWER_UP_EXT_DOWN) {
        if (m->hasSufficientPower) {
            model_power_up(m, m->req == DO_POWER_UP);
            m->req = NO_CHANGE;
            return;                   /* ⛔ skips the power-down block */
        }
        m->shutdownNotified = false;
        m->req = NO_CHANGE;
    }
    if (m->shutdownNotified) {
        model_power_down(m);
        m->shutdownNotified = false;
        if (!m->pgStat) {
            return;
        }
    }
}

/* Power_UpdateState() */
static void model_update(Model *m, Shape s) {
    if (m->req == DO_POWER_DOWN) {
        m->state = STANDBY;
        if (s.clearRequestInCommonHandler) {
            m->req = NO_CHANGE;
        }
    }
    if (m->state == STANDBY) {
        model_standby(m);
    }
}

/* The button's long-press power-off arm (UI.c). */
static void model_button_poweroff(Model *m, Shape s) {
    m->req = DO_POWER_DOWN;
    m->shutdownNotified = true;
    if (s.latchOnlyWhenPgStat) {
        if (m->pgStat) {
            m->autoPromotedThisVbusSession = true;
        }
    } else {
        m->autoPromotedThisVbusSession = true;
    }
}

static Model fresh(bool autoOn, bool pgStat) {
    Model m;
    memset(&m, 0, sizeof(m));
    m.state = POWERED_UP;
    m.autoPowerOnUsb = autoOn;
    m.pgStat = pgStat;
    m.hasSufficientPower = true;
    return m;
}

/* ---- DEFECT A: what the clear actually costs ---------------------------- */
/*
 * ⛔ CORRECTED, AND MY FIRST VERSION OF THIS SECTION WAS WRONG. It asserted
 * that the old shape skips the power-down with AUTOOn on and VBUS present, and
 * the model REFUTED it: with pgStat true the button path already set the latch,
 * so `!autoPromotedThisVbusSession` is false and the auto-promote cannot fire
 * whatever the request says. Measured here before it was claimed anywhere else.
 *
 * And both DO_POWER_DOWN posters set that latch unconditionally --
 * SCPIInterface.c:1646 for `SYSTem:POWer:STATe 0`, and UI.c for the button
 * after this PR. So on the shipped firmware the clear is NOT reachable as the
 * auto-promote defect, and it is DEFECT B below that closes the observable
 * "shutdown undone" case.
 *
 * What the clear does cost is REAL and is what these arms pin:
 *   1. it tells every consumer "nothing was requested" while a power-down is
 *      genuinely pending -- UI.c's LED block and SCPI_PowerStatusGet's
 *      "Shutdown: Pending" diagnostic both read that field, and the latter
 *      emits NOTHING in that window today;
 *   2. it leaves the conflation in place, so a FUTURE poster of
 *      DO_POWER_DOWN that forgets the latch resurrects the defect. Removing
 *      the conflation makes the gate correct by construction instead of by
 *      every caller remembering -- the request cannot say "idle" while a
 *      power-down is outstanding.
 */

/* The hardening property, stated as a test: a poster that omits the latch. */
static void model_poster_without_latch(Model *m) {
    m->req = DO_POWER_DOWN;
    m->shutdownNotified = true;
}

TEST(old_clear_lets_a_poster_that_forgot_the_latch_be_undone) {
    Model m = fresh(true, true);
    m.autoPromotedThisVbusSession = false;
    model_poster_without_latch(&m);
    model_update(&m, SHAPE_OLD);
    ASSERT_EQ(m.powerDownCalls, 0u);       /* delegated, never executed */
    ASSERT_EQ(m.powerUpCalls, 1u);         /* powered UP on a power-DOWN */
}

TEST(new_shape_is_correct_even_when_the_poster_forgot_the_latch) {
    Model m = fresh(true, true);
    m.autoPromotedThisVbusSession = false;
    model_poster_without_latch(&m);
    model_update(&m, SHAPE_NEW);
    ASSERT_EQ(m.powerDownCalls, 1u);
    ASSERT_EQ(m.powerUpCalls, 0u);
    ASSERT_EQ(m.state, STANDBY);
}

/* The consumer-visibility half: what the field SAYS while the power-down is
 * outstanding. This is the part users can actually observe today, through the
 * LED window and the SCPI shutdown diagnostic. */
TEST(old_clear_hides_a_pending_shutdown_from_its_consumers) {
    Model m = fresh(true, true);
    model_button_poweroff(&m, SHAPE_OLD);
    /* the common handler runs, then consumers read the field */
    if (m.req == DO_POWER_DOWN) { m.state = STANDBY; m.req = NO_CHANGE; }
    ASSERT_EQ(m.req, NO_CHANGE);           /* "nothing requested" -- a lie */
}

TEST(new_shape_keeps_a_pending_shutdown_visible_until_executed) {
    Model m = fresh(true, true);
    model_button_poweroff(&m, SHAPE_NEW);
    if (m.req == DO_POWER_DOWN) { m.state = STANDBY; }   /* no clear */
    ASSERT_EQ(m.req, DO_POWER_DOWN);       /* consumers see the truth */
    model_standby(&m);                     /* the executor runs */
    ASSERT_EQ(m.powerDownCalls, 1u);
    ASSERT_EQ(m.req, NO_CHANGE);           /* cleared where it is serviced */
}

/* With AUTOOn OFF the gate cannot fire at all, so neither shape misbehaves --
 * which is why this was only ever reported with AUTOOn on. An arm that failed
 * here would be describing a defect that does not exist. */
TEST(autoon_off_is_unaffected_by_either_shape) {
    Model a = fresh(false, true);
    model_poster_without_latch(&a);
    model_update(&a, SHAPE_OLD);
    ASSERT_EQ(a.powerDownCalls, 1u);
    Model b = fresh(false, true);
    model_poster_without_latch(&b);
    model_update(&b, SHAPE_NEW);
    ASSERT_EQ(b.powerDownCalls, 1u);
}

/* ---- DEFECT B: the stale-pgStat latch window ---------------------------- */

TEST(old_latch_rule_lets_a_stale_reading_undo_the_shutdown) {
    /* Press while the CACHE says no VBUS, though VBUS is really present. */
    Model m = fresh(true, false);
    model_button_poweroff(&m, (Shape){ false, true });  /* A fixed, B pre-fix */
    ASSERT_FALSE(m.autoPromotedThisVbusSession);        /* latch not set */
    model_update(&m, (Shape){ false, true });
    ASSERT_EQ(m.powerDownCalls, 1u);                    /* it does power down */
    /* Power_Tasks refreshes the cache: VBUS was there all along. */
    m.pgStat = true;
    model_update(&m, (Shape){ false, true });
    ASSERT_EQ(m.powerUpCalls, 1u);                      /* ...and back up */
}

TEST(new_latch_rule_keeps_the_shutdown_through_a_stale_reading) {
    Model m = fresh(true, false);
    model_button_poweroff(&m, SHAPE_NEW);
    ASSERT_TRUE(m.autoPromotedThisVbusSession);         /* set regardless */
    model_update(&m, SHAPE_NEW);
    ASSERT_EQ(m.powerDownCalls, 1u);
    m.pgStat = true;
    model_update(&m, SHAPE_NEW);
    ASSERT_EQ(m.powerUpCalls, 0u);                      /* stays down */
    ASSERT_EQ(m.state, STANDBY);
}

/* ---- the unconditional set must be HARMLESS, not merely convenient ------ */

TEST(battery_only_press_rearms_so_a_later_plug_in_still_auto_promotes) {
    Model m = fresh(true, false);          /* genuinely on battery */
    model_button_poweroff(&m, SHAPE_NEW);
    ASSERT_TRUE(m.autoPromotedThisVbusSession);   /* set unnecessarily */
    model_update(&m, SHAPE_NEW);
    ASSERT_EQ(m.powerDownCalls, 1u);
    /* Next STANDBY pass, still no VBUS: the handler re-arms the latch. */
    model_update(&m, SHAPE_NEW);
    ASSERT_FALSE(m.autoPromotedThisVbusSession);
    /* Now the user plugs in: #454 must still auto-promote. */
    m.pgStat = true;
    model_update(&m, SHAPE_NEW);
    ASSERT_EQ(m.powerUpCalls, 1u);
}

/* The r1 defect, kept so the set-only rule cannot silently become an
 * assignment again: writing pgStat directly would CLEAR a latch that a boot
 * auto-promote had already set, cancelling the suppression. */
TEST(assigning_pgstat_would_clear_a_valid_suppression) {
    Model m = fresh(true, false);
    m.autoPromotedThisVbusSession = true;   /* set by an earlier auto-promote */
    /* the r1 shape: latch = pgStat */
    m.autoPromotedThisVbusSession = m.pgStat;
    ASSERT_FALSE(m.autoPromotedThisVbusSession);   /* the r1 defect */
    /* the shipped shape never writes false here */
    Model n = fresh(true, false);
    n.autoPromotedThisVbusSession = true;
    model_button_poweroff(&n, SHAPE_NEW);
    ASSERT_TRUE(n.autoPromotedThisVbusSession);
}

int main(void) {
    RUN(old_clear_lets_a_poster_that_forgot_the_latch_be_undone);
    RUN(new_shape_is_correct_even_when_the_poster_forgot_the_latch);
    RUN(old_clear_hides_a_pending_shutdown_from_its_consumers);
    RUN(new_shape_keeps_a_pending_shutdown_visible_until_executed);
    RUN(autoon_off_is_unaffected_by_either_shape);
    RUN(old_latch_rule_lets_a_stale_reading_undo_the_shutdown);
    RUN(new_latch_rule_keeps_the_shutdown_through_a_stale_reading);
    RUN(battery_only_press_rearms_so_a_later_plug_in_still_auto_promotes);
    RUN(assigning_pgstat_would_clear_a_valid_suppression);
    return TEST_SUMMARY();
}
