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
    bool vbusActual;                  /* the PHYSICAL fact: is VBUS there? */
    bool autoPromotedThisVbusSession; /* the suppression latch */
    bool shutdownNotified;
    bool hasSufficientPower;
    /* observations */
    unsigned powerDownCalls;
    unsigned powerUpCalls;
} Model;

/* Which revision of the two sites is being modelled. */
/*
 * ⛔ EVERY DIMENSION OF THE REVISION LIVES HERE, AND THAT IS THE POINT.
 * refreshBeforeDeciding was briefly a field on Model instead, and the cost was
 * immediate: four arms inherited r4's entry refresh from the fixture
 * constructor while modelling pre-r4 firmware, which silently overwrote the
 * cached reading each one had set up, and TWO arms kept passing for a reason
 * their scenario no longer contained. A per-fixture default cannot encode
 * "which revision is this?", because the answer belongs to the shape the arm
 * is exercising, not to the state it starts from.
 */
typedef struct {
    bool clearRequestInCommonHandler; /* DEFECT A: true = pre-fix */
    bool latchOnlyWhenPgStat;         /* DEFECT B: true = pre-fix */
    bool rearmInsideRequestGate;      /* DEFECT C: true = pre-fix (r3) */
    bool refreshBeforeDeciding;       /* r4: fresh PHY read before the latch */
    bool latchFromFreshReadAtPress;   /* r6: decide the latch AT the press */
} Shape;

static const Shape SHAPE_OLD = { true,  true,  true,  false, false };
static const Shape SHAPE_NEW = { false, false, false, true,  true  };
/* r3 as shipped: A and B fixed, the re-arm still gated, no entry refresh. */
static const Shape SHAPE_R3  = { false, false, true,  false, false };
/* r4b as shipped: the re-arm hoisted and the entry refresh added, but the latch
 * still set UNCONDITIONALLY at the press -- the shape r5 refuted. */
static const Shape SHAPE_R4B = { false, false, false, true,  false };

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
static void model_standby(Model *m, Shape s) {
    if (s.refreshBeforeDeciding) {
        m->pgStat = m->vbusActual;     /* Power_UpdateStatusFromGPIO() */
    }
    if (!s.rearmInsideRequestGate && !m->pgStat) {
        m->autoPromotedThisVbusSession = false;   /* re-arm, first observation */
    }
    if (m->autoPowerOnUsb && m->req == NO_CHANGE) {
        if (s.rearmInsideRequestGate && !m->pgStat) {
            m->autoPromotedThisVbusSession = false;   /* re-arm, gated (pre-fix) */
        } else if (m->pgStat && !m->autoPromotedThisVbusSession) {
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
        model_standby(m, s);
    }
}

/* The button's long-press power-off arm (UI.c). */
static void model_button_poweroff(Model *m, Shape s) {
    m->req = DO_POWER_DOWN;
    m->shutdownNotified = true;
    if (s.latchFromFreshReadAtPress) {
        /* r6: Power_IsVbusPresentFresh() -- the PHY, at this instant. */
        m->autoPromotedThisVbusSession = m->vbusActual;
    } else if (s.latchOnlyWhenPgStat) {
        if (m->pgStat) {                  /* r2: the CACHE */
            m->autoPromotedThisVbusSession = true;
        }
    } else {
        m->autoPromotedThisVbusSession = true;   /* r3/r4b */
    }
}

/*
 * The cable going in changes the PHYSICAL fact first; the cache follows when
 * something reads the PHY. Poking only the cache modelled a plug-in that r4's
 * entry refresh then correctly un-did -- so both move here, together.
 */
static void plug_in_usb(Model *m) {
    m->vbusActual = true;
    m->pgStat = true;              /* Power_Tasks' periodic refresh */
}

static Model fresh(bool autoOn, bool pgStat) {
    Model m;
    memset(&m, 0, sizeof(m));
    m.state = POWERED_UP;
    m.autoPowerOnUsb = autoOn;
    m.pgStat = pgStat;
    m.vbusActual = pgStat;        /* cache agrees with reality by default */
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
    model_standby(&m, SHAPE_NEW);          /* the executor runs */
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

/* A fixed, B pre-fix, and NO entry refresh -- the shape this defect lived in.
 * r4 closes this scenario a second way, by never reading a stale value at all;
 * the latch is still what makes the shutdown stick, so both hold. */
static const Shape SHAPE_B_PREFIX = { false, true, false, false, false };

TEST(old_latch_rule_lets_a_stale_reading_undo_the_shutdown) {
    /* Press while the CACHE says no VBUS, though VBUS is really present. */
    Model m = fresh(true, false);
    m.vbusActual = true;                                /* ...it IS present */
    model_button_poweroff(&m, SHAPE_B_PREFIX);
    ASSERT_FALSE(m.autoPromotedThisVbusSession);        /* latch not set */
    model_update(&m, SHAPE_B_PREFIX);
    ASSERT_EQ(m.powerDownCalls, 1u);                    /* it does power down */
    /* Power_Tasks refreshes the cache: VBUS was there all along. */
    m.pgStat = true;
    model_update(&m, SHAPE_B_PREFIX);
    ASSERT_EQ(m.powerUpCalls, 1u);                      /* ...and back up */
}

TEST(new_latch_rule_keeps_the_shutdown_through_a_stale_reading) {
    Model m = fresh(true, false);
    m.vbusActual = true;        /* VBUS present; the cache has not caught up */
    model_button_poweroff(&m, SHAPE_NEW);
    ASSERT_TRUE(m.autoPromotedThisVbusSession);         /* set regardless */
    model_update(&m, SHAPE_NEW);
    ASSERT_EQ(m.powerDownCalls, 1u);
    model_update(&m, SHAPE_NEW);
    ASSERT_EQ(m.powerUpCalls, 0u);                      /* stays down */
    ASSERT_EQ(m.state, STANDBY);
    /* ⭐ NON-VACUITY. "Did not power up" is ALSO what a board with no VBUS
     * does, so prove the LATCH is what held it down: clear that one bit,
     * change nothing else, and the very next pass must promote. Two arms in
     * this file were passing an hour ago because their VBUS had quietly gone
     * away instead -- a negative assertion needs its positive control.
     */
    m.autoPromotedThisVbusSession = false;
    model_update(&m, SHAPE_NEW);
    ASSERT_EQ(m.powerUpCalls, 1u);
}

/* ---- the unconditional set must be HARMLESS, not merely convenient ------ */

TEST(battery_only_press_sets_no_latch_at_all) {
    /* ⛔ r6 REPLACED THIS ARM'S PREMISE. It used to assert that a battery-only
     * press sets the latch "unnecessarily but harmlessly", because the standby
     * re-arm would clear it. r5 showed that re-arm cannot be relied on, so the
     * press no longer sets it: a fresh PHY read says VBUS is absent, and the
     * answer is recorded instead of guessed. The suppression never exists, so
     * there is nothing for a later pass to have to undo. */
    Model m = fresh(true, false);          /* genuinely on battery */
    model_button_poweroff(&m, SHAPE_NEW);
    ASSERT_FALSE(m.autoPromotedThisVbusSession);
    model_update(&m, SHAPE_NEW);
    ASSERT_EQ(m.powerDownCalls, 1u);
    /* An extra STANDBY pass, still no VBUS, BEFORE the plug-in. */
    model_update(&m, SHAPE_NEW);
    ASSERT_FALSE(m.autoPromotedThisVbusSession);
    plug_in_usb(&m);
    model_update(&m, SHAPE_NEW);
    ASSERT_EQ(m.powerUpCalls, 1u);
}

/* ---- DEFECT C: the r3 regression, which my own test construction hid ----- */
/*
 * ⛔ THE ARM ABOVE PASSES ON THE BROKEN CODE TOO, and that is the lesson.
 * It calls model_update twice before the plug-in, and that second call IS the
 * VBUS-absent pass the re-arm needed. With it, r3's shape re-arms and the
 * board auto-powers-on; without it, r3 leaves the latch set for ever. My test
 * inserted the one event that made the fix look harmless -- a construction
 * that predetermined its own outcome.
 *
 * The real sequence has no such pass: the user holds the button (which holds
 * the MCU up), so the shutdown pass runs with DO_POWER_DOWN STILL PENDING --
 * r3 stopped clearing it in the common handler -- which makes the request gate
 * false and skips the re-arm entirely. USB then goes in before the next ~1 s
 * pass. So r3's two fixes INTERACT: the deferred clear removed the one pass
 * the unconditional set depended on. Neither change is wrong alone.
 *
 * THE PROPERTY: a latch set by a press must be cleared by the FIRST
 * observation of VBUS absent, whichever pass makes it.
 */

/*
 * ⭐ AND THE CLAIM ABOVE IS A TEST, NOT A COMMENT. "My battery arm would have
 * passed on the broken code" is the entire lesson of this section, so it is
 * asserted here rather than asserted about: the SAME sequence, with the SAME
 * intervening pass, run against r3's shape -- and it promotes. An arm that
 * can pass on both shapes distinguishes nothing, and the only way to know
 * that is to run it against the shape it was supposed to catch.
 */
TEST(the_battery_arms_sequence_cannot_detect_defect_c_at_all) {
    Model m = fresh(true, false);
    model_button_poweroff(&m, SHAPE_R3);
    model_update(&m, SHAPE_R3);
    ASSERT_EQ(m.powerDownCalls, 1u);
    model_update(&m, SHAPE_R3);            /* the intervening VBUS-absent pass */
    ASSERT_FALSE(m.autoPromotedThisVbusSession);   /* r3 DOES re-arm here */
    plug_in_usb(&m);
    model_update(&m, SHAPE_R3);
    ASSERT_EQ(m.powerUpCalls, 1u);         /* ⛔ the BROKEN shape, passing */
}

TEST(r3_shape_strands_the_board_when_no_vbus_absent_pass_intervenes) {
    Model m = fresh(true, false);
    model_button_poweroff(&m, SHAPE_R3);
    model_update(&m, SHAPE_R3);            /* shutdown pass, request pending */
    ASSERT_EQ(m.powerDownCalls, 1u);
    ASSERT_TRUE(m.autoPromotedThisVbusSession);   /* never re-armed */
    plug_in_usb(&m);                       /* USB in before the next pass */
    model_update(&m, SHAPE_R3);
    model_update(&m, SHAPE_R3);
    ASSERT_EQ(m.powerUpCalls, 0u);         /* stranded in STANDBY */
    ASSERT_EQ(m.state, STANDBY);
    /* ⭐ NON-VACUITY. "Did not power up" is ALSO what a board with no VBUS
     * does, so prove the LATCH is what held it down: clear that one bit,
     * change nothing else, and the very next pass must promote. Two arms in
     * this file were passing an hour ago because their VBUS had quietly gone
     * away instead. Here it also separates the two r3 failure modes: the
     * board is stranded by the STUCK LATCH, not by a missing promote path.
     */
    m.autoPromotedThisVbusSession = false;
    model_update(&m, SHAPE_R3);
    ASSERT_EQ(m.powerUpCalls, 1u);
}

TEST(new_shape_auto_powers_on_with_no_vbus_absent_pass_intervening) {
    Model m = fresh(true, false);
    model_button_poweroff(&m, SHAPE_NEW);
    model_update(&m, SHAPE_NEW);           /* shutdown pass re-arms anyway */
    ASSERT_EQ(m.powerDownCalls, 1u);
    ASSERT_FALSE(m.autoPromotedThisVbusSession);  /* first observation wins */
    plug_in_usb(&m);
    model_update(&m, SHAPE_NEW);
    ASSERT_EQ(m.powerUpCalls, 1u);         /* auto-powers-on */
}

/* The re-arm must not depend on AUTOOn either: turning it on after the
 * VBUS-absent period must find the latch already armed. */
TEST(rearm_does_not_depend_on_autoon_being_enabled) {
    Model m = fresh(false, false);         /* AUTOOn OFF */
    model_button_poweroff(&m, SHAPE_NEW);
    model_update(&m, SHAPE_NEW);
    ASSERT_FALSE(m.autoPromotedThisVbusSession);
    m.autoPowerOnUsb = true;               /* user enables AUTOOn later */
    plug_in_usb(&m);
    model_update(&m, SHAPE_NEW);
    ASSERT_EQ(m.powerUpCalls, 1u);
}

/* The r1 defect, kept so the set-only rule cannot silently become an
 * assignment again: writing pgStat directly would CLEAR a latch that a boot
 * auto-promote had already set, cancelling the suppression. */
/* ⛔ THE r1 DEFECT WAS NEVER "A VARIABLE", IT WAS "THE CACHE", and r6 makes
 * that precise. r6 writes a non-constant too -- so a test that forbade
 * non-constants would now forbid the fix. The discriminator is the SOURCE: the
 * cached pgStat can be stale in either direction, the PHY cannot.
 *
 * The fixture is the case where the two disagree: VBUS present, cache not yet
 * caught up. Reading the cache cancels a valid suppression (r1). Reading the
 * PHY keeps it. And when VBUS is genuinely absent, r6 writing false is
 * CORRECT, not a regression -- a board on battery is exactly where #454's
 * auto-promote must not be suppressed. */
TEST(the_latch_source_must_be_the_phy_not_the_cache) {
    Model m = fresh(true, false);           /* cache says no VBUS... */
    m.vbusActual = true;                    /* ...but VBUS IS present */
    m.autoPromotedThisVbusSession = true;   /* set by an earlier auto-promote */
    m.autoPromotedThisVbusSession = m.pgStat;      /* the r1 shape */
    ASSERT_FALSE(m.autoPromotedThisVbusSession);   /* suppression cancelled */

    Model n = fresh(true, false);
    n.vbusActual = true;
    n.autoPromotedThisVbusSession = true;
    model_button_poweroff(&n, SHAPE_NEW);          /* reads the PHY */
    ASSERT_TRUE(n.autoPromotedThisVbusSession);    /* suppression survives */

    /* ...and the converse, so this is not just "always true": VBUS genuinely
     * absent must write FALSE, which is the whole point of r6. */
    Model b = fresh(true, false);
    b.autoPromotedThisVbusSession = true;
    model_button_poweroff(&b, SHAPE_NEW);
    ASSERT_FALSE(b.autoPromotedThisVbusSession);
}

/* ---- DEFECT D: the r5 window -- no VBUS-absent pass EVER happens -------- */
/*
 * ⛔ r4b's re-arm fires on the first STANDBY pass that OBSERVES VBUS absent,
 * and nothing guarantees such a pass exists. Hold the button on battery (the
 * button holds the MCU up), plug USB in BEFORE the handler's next ~1 s pass,
 * then release. Every pass from then on sees VBUS, so the latch the press set
 * unconditionally is never cleared and the board sits in STANDBY.
 *
 * This is NOT the r3 case. r3 failed because the re-arm was unreachable while a
 * request was pending; r4b made it reachable, and it still fails, because
 * reachability was never the problem -- the OBSERVATION it waits for can simply
 * not occur. That is why r6 moves the decision to the press, where the answer
 * is available, instead of ordering the re-arm better.
 */
TEST(r4b_shape_strands_the_board_when_no_vbus_absent_pass_ever_happens) {
    Model m = fresh(true, false);          /* battery only at the press */
    model_button_poweroff(&m, SHAPE_R4B);
    ASSERT_TRUE(m.autoPromotedThisVbusSession);   /* set unconditionally */
    plug_in_usb(&m);                       /* USB arrives BEFORE any pass */
    model_update(&m, SHAPE_R4B);
    model_update(&m, SHAPE_R4B);
    ASSERT_EQ(m.powerUpCalls, 0u);         /* stranded in STANDBY */
    ASSERT_EQ(m.state, STANDBY);
    /* ⭐ NON-VACUITY: the stranding is the stuck latch, not a missing promote
     * path. Clear only that bit and the next pass must promote. */
    m.autoPromotedThisVbusSession = false;
    model_update(&m, SHAPE_R4B);
    ASSERT_EQ(m.powerUpCalls, 1u);
}

TEST(new_shape_auto_powers_on_when_usb_arrives_before_the_first_pass) {
    Model m = fresh(true, false);          /* battery only at the press */
    model_button_poweroff(&m, SHAPE_NEW);
    ASSERT_FALSE(m.autoPromotedThisVbusSession);  /* fresh read: no VBUS */
    plug_in_usb(&m);                       /* USB arrives BEFORE any pass */

    /* PASS 1 powers the board DOWN, and must: the user asked for it, and the
     * auto-promote gate requires requestedPowerState == NO_CHANGE, which is
     * false while the power-down it is servicing is still outstanding. An
     * earlier version of this arm expected the promote here and failed -- the
     * firmware was right and the test's sequence was wrong. */
    model_update(&m, SHAPE_NEW);
    ASSERT_EQ(m.powerDownCalls, 1u);
    ASSERT_EQ(m.powerUpCalls, 0u);
    ASSERT_FALSE(m.autoPromotedThisVbusSession);  /* nothing suppresses it */

    /* PASS 2: request serviced, latch clear, VBUS present -> #454 promotes.
     * On hardware that is the board coming back up about a second after it
     * went down, which is what bench case 5 asks the operator to observe. */
    model_update(&m, SHAPE_NEW);
    ASSERT_EQ(m.powerUpCalls, 1u);
}

/* And the case the press-time decision CANNOT see, which is why the standby
 * re-arm is kept: VBUS cycled while the board is already in STANDBY. The press
 * latched correctly (USB was present), and only a later observation can tell
 * us the cable went away. */
TEST(the_standby_rearm_is_still_needed_for_a_vbus_cycle) {
    Model m = fresh(true, true);           /* USB present at the press */
    m.hasSufficientPower = true;
    model_button_poweroff(&m, SHAPE_NEW);
    ASSERT_TRUE(m.autoPromotedThisVbusSession);   /* correctly suppressed */
    model_update(&m, SHAPE_NEW);
    ASSERT_EQ(m.powerDownCalls, 1u);
    ASSERT_EQ(m.powerUpCalls, 0u);                /* stays down */
    /* cable out */
    m.vbusActual = false; m.pgStat = false;
    model_update(&m, SHAPE_NEW);
    ASSERT_FALSE(m.autoPromotedThisVbusSession);  /* the re-arm earns its keep */
    /* cable back in */
    plug_in_usb(&m);
    model_update(&m, SHAPE_NEW);
    ASSERT_EQ(m.powerUpCalls, 1u);
}

int main(void) {
    RUN(old_clear_lets_a_poster_that_forgot_the_latch_be_undone);
    RUN(new_shape_is_correct_even_when_the_poster_forgot_the_latch);
    RUN(old_clear_hides_a_pending_shutdown_from_its_consumers);
    RUN(new_shape_keeps_a_pending_shutdown_visible_until_executed);
    RUN(autoon_off_is_unaffected_by_either_shape);
    RUN(old_latch_rule_lets_a_stale_reading_undo_the_shutdown);
    RUN(new_latch_rule_keeps_the_shutdown_through_a_stale_reading);
    RUN(battery_only_press_sets_no_latch_at_all);
    RUN(the_battery_arms_sequence_cannot_detect_defect_c_at_all);
    RUN(r3_shape_strands_the_board_when_no_vbus_absent_pass_intervenes);
    RUN(new_shape_auto_powers_on_with_no_vbus_absent_pass_intervening);
    RUN(rearm_does_not_depend_on_autoon_being_enabled);
    RUN(the_latch_source_must_be_the_phy_not_the_cache);
    RUN(r4b_shape_strands_the_board_when_no_vbus_absent_pass_ever_happens);
    RUN(new_shape_auto_powers_on_when_usb_arrives_before_the_first_pass);
    RUN(the_standby_rearm_is_still_needed_for_a_vbus_cycle);
    return TEST_SUMMARY();
}
