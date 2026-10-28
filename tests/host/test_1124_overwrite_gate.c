/* ==========================================================================
 * test_1124_overwrite_gate.c -- issue #924 / PR #1124, operator ruling L=C
 *
 * THE DECISION TABLE OF sd_TargetSafeToOpenForWrite(), run through the OLD
 * (r18) and NEW (C) shapes so every property is asserted as a PAIR. A test
 * that cannot fail on the old code is not a regression test.
 *
 * WHY A MODEL. sd_card_manager.c includes Harmony's SYS_FS, the SPI driver and
 * the board config, so it cannot be compiled on the host -- the same trade
 * #889 made for AD7609Scale.h and #1092 for PowerApi.c. What is modelled here
 * is the decision structure and nothing else, and each structural claim it
 * rests on is pinned against the real source in $(GATE1124_BIN)'s recipe. If a
 * pin fails, this model describes firmware that no longer exists.
 *
 * ---------------------------------------------------------------------------
 * WHAT r16-r18 ESTABLISHED, which is why the shape changed rather than the
 * naming rule. Three consecutive rounds each closed one case of "which
 * manifest describes the file I am about to destroy?" and each time the next
 * round found another:
 *
 *   r18 #1  a subdirectory target whose manifest name drops the directory,
 *           so invalidation deleted an UNTOUCHED capture's manifest;
 *   r18 #2  a split part belonging to a previous session's manifest;
 *   r18 #3  a stat fault read as an absence, so truncation proceeded;
 *   r18 #4  an 8.3 alias resolving to a different file.
 *
 * Those are one defect: THE CODE INFERS FILE IDENTITY FROM A NAME AND THE
 * FILESYSTEM DOES NOT. The map from data path to manifest name is not
 * injective -- `DAQiFi/exp.csv`, `DAQiFi/sub/exp.csv` and `DAQiFi/0/exp.csv`
 * all name `DAQiFi/exp.csv.mfst` -- so no naming rule can decide it. Note that
 * #1 and #4 are wrong-DELETION bugs: they destroy another capture's record
 * whether or not anything was truncated, so refusing to truncate alone would
 * not have closed them. What closes them is refusing to DELETE what cannot be
 * proved to be ours.
 * ========================================================================== */

#include "test_framework.h"
#include <stdbool.h>
#include <string.h>

/* ---- the modelled world -------------------------------------------------- */

/* What a stat of a path reports. UNREADABLE is the third state r18 finding 3
 * collapsed into ABSENT, and keeping it distinct here is the whole point: a
 * two-valued "does it exist?" cannot express it, and a predicate written
 * against a two-valued answer fails open. */
typedef enum { FS_ABSENT = 0, FS_FILE, FS_DIR, FS_UNREADABLE } FsState;

typedef enum {
    REFUSE_NONE = 0,
    REFUSE_TARGET_EXISTS,
    REFUSE_TARGET_UNREADABLE,
    REFUSE_MANIFEST_NOT_OURS,
} Refusal;

typedef struct {
    FsState dataFile;        /* the target the caller is about to open */
    FsState manifestAtName;  /* whatever sits at the target's manifest name */
    bool    manifestIsOurLiveOne;
    bool    overwriteAsked;  /* the SD:FILE opt-in */
    /* observations */
    bool    opened;          /* the WRITE_PLUS open happened (truncating) */
    bool    manifestRemoved; /* something was unlinked */
    Refusal refusal;
    bool    sessionArmed;    /* mode == WRITE: IsBusy() is true while it is */
} World;

typedef struct {
    /* true  = r18: delete whatever manifest the name maps to, then truncate.
     * false = C:  prove it is safe, or refuse. */
    bool deleteThenTruncate;
    /* C only: a refusal DISARMS the session (mode -> NONE). Without this a
     * refusal leaves mode == WRITE, and that is a wedge, not a tidiness
     * detail -- see the_refusal_must_not_wedge_the_card below. */
    bool refusalDisarmsSession;
} Shape;

static const Shape SHAPE_R18 = { true,  false };
static const Shape SHAPE_C   = { false, true  };
/* C's gate without the disarm: what this PR shipped before conv-ts asked
 * whether a refused session leaves the card usable. */
static const Shape SHAPE_C_NO_DISARM = { false, false };

static World world(FsState data, FsState mfst, bool ours, bool asked) {
    World w;
    memset(&w, 0, sizeof(w));
    w.dataFile = data;
    w.manifestAtName = mfst;
    w.manifestIsOurLiveOne = ours;
    w.overwriteAsked = asked;
    w.sessionArmed = true;          /* mode == WRITE: the session is running */
    return w;
}

/* sd_card_manager_IsBusyLocked() tests `mode != MODE_NONE` FIRST, before any
 * state consideration, so an armed session means busy whatever the state
 * machine is doing. */
static bool model_is_busy(const World *w) { return w->sessionArmed; }

/* sd_RemoveStaleManifestAt(): refuses a non-manifest file, and since r18 a
 * DIRECTORY -- the predicate it shares with the create path answers "is this
 * mine to destroy?" and a directory stats with fsize == 0. */
static bool model_remove_manifest(World *w) {
    if (w->manifestAtName == FS_DIR) {
        return true;                 /* left alone, and that is not a failure */
    }
    if (w->manifestAtName == FS_UNREADABLE) {
        return false;
    }
    if (w->manifestAtName == FS_FILE) {
        w->manifestRemoved = true;
    }
    return true;
}

/* The one truncating open in the module. Both the session's first file and
 * every rotation part reach it, which is why a gate here covers rotation with
 * no extra code -- and why r17's "first open only" gate was a proxy for the
 * wrong thing. */
static void model_open_target(World *w, Shape s) {
    if (s.deleteThenTruncate) {
        /* r18: the manifest for this NAME is invalidated unless it is ours,
         * and then the file is truncated regardless of what was there. */
        if (!w->manifestIsOurLiveOne) {
            if (!model_remove_manifest(w)) {
                return;              /* refused: handle stays invalid */
            }
        }
        w->opened = true;
        return;
    }

    /* C: the data file first. It is never truncated unless asked for. */
    if (w->dataFile == FS_UNREADABLE) {
        w->refusal = REFUSE_TARGET_UNREADABLE;   /* fail closed */
        return;
    }
    if (w->dataFile == FS_FILE || w->dataFile == FS_DIR) {
        if (!w->overwriteAsked) {
            w->refusal = REFUSE_TARGET_EXISTS;
            return;
        }
    }
    /* Then the manifest that this target's name maps to. */
    if (w->manifestIsOurLiveOne) {
        w->opened = true;            /* ours and open: untouched */
        return;
    }
    if (w->manifestAtName == FS_UNREADABLE) {
        w->refusal = REFUSE_TARGET_UNREADABLE;   /* fail closed */
        return;
    }
    if (w->manifestAtName == FS_FILE || w->manifestAtName == FS_DIR) {
        if (!w->overwriteAsked) {
            w->refusal = REFUSE_MANIFEST_NOT_OURS;
            return;
        }
        if (!model_remove_manifest(w)) {
            w->refusal = REFUSE_TARGET_UNREADABLE;
            return;
        }
    }
    w->opened = true;
}

/* Every refusal arm above routes through here in the firmware: the gate
 * returns false and the caller decides what that means for the session. */
static void model_finish_refusal(World *w, Shape s) {
    if (w->refusal != REFUSE_NONE && s.refusalDisarmsSession) {
        w->sessionArmed = false;
    }
}

/* ---- the headline property: an existing target is not destroyed ---------- */

TEST(r18_truncates_an_existing_target_without_being_asked) {
    World w = world(FS_FILE, FS_ABSENT, false, false);
    model_open_target(&w, SHAPE_R18);
    ASSERT_TRUE(w.opened);                  /* FA_CREATE_ALWAYS: bytes gone */
    ASSERT_EQ(w.refusal, REFUSE_NONE);
}

TEST(c_refuses_an_existing_target) {
    World w = world(FS_FILE, FS_ABSENT, false, false);
    model_open_target(&w, SHAPE_C);
    ASSERT_FALSE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_TARGET_EXISTS);
}

TEST(c_overwrites_an_existing_target_when_asked) {
    World w = world(FS_FILE, FS_ABSENT, false, true);
    model_open_target(&w, SHAPE_C);
    ASSERT_TRUE(w.opened);                  /* the opt-in is the ONLY way in */
    ASSERT_EQ(w.refusal, REFUSE_NONE);
}

/* The ordinary case must still work, or the fix is a denial of service: a
 * fresh name on a default-configured board opens with no opt-in at all. */
TEST(c_opens_a_fresh_target_with_no_opt_in) {
    World w = world(FS_ABSENT, FS_ABSENT, false, false);
    model_open_target(&w, SHAPE_C);
    ASSERT_TRUE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_NONE);
}

/* ---- r18 findings 1 and 4: the WRONG DELETION -------------------------- */
/*
 * These are the cases that refusing-to-truncate would NOT have closed, and the
 * reason the fix had to be "refuse to delete" and not merely "refuse to
 * overwrite". The data file is ABSENT -- so nothing is being overwritten and
 * no amount of overwrite policy is engaged -- yet r18 still unlinks the
 * manifest that the name maps to, which may describe a completely different
 * capture (a subdirectory target, or an 8.3 alias).
 */
TEST(r18_deletes_another_captures_manifest_with_nothing_overwritten) {
    World w = world(FS_ABSENT, FS_FILE, false, false);
    model_open_target(&w, SHAPE_R18);
    ASSERT_TRUE(w.manifestRemoved);         /* someone else's record, gone */
    ASSERT_TRUE(w.opened);
}

TEST(c_refuses_rather_than_deleting_a_manifest_it_cannot_claim) {
    World w = world(FS_ABSENT, FS_FILE, false, false);
    model_open_target(&w, SHAPE_C);
    ASSERT_FALSE(w.manifestRemoved);
    ASSERT_FALSE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_MANIFEST_NOT_OURS);
}

TEST(c_removes_a_prior_manifest_only_when_authorised) {
    World w = world(FS_ABSENT, FS_FILE, false, true);
    model_open_target(&w, SHAPE_C);
    ASSERT_TRUE(w.manifestRemoved);
    ASSERT_TRUE(w.opened);
}

/* Our own live manifest is not a collision and must never be touched --
 * removing it mid-session destroys the records of every file already logged,
 * which is the opposite of the protection this function provides. */
TEST(our_own_live_manifest_is_never_removed_in_either_shape) {
    World a = world(FS_ABSENT, FS_FILE, true, false);
    model_open_target(&a, SHAPE_R18);
    ASSERT_FALSE(a.manifestRemoved);
    ASSERT_TRUE(a.opened);

    World b = world(FS_ABSENT, FS_FILE, true, false);
    model_open_target(&b, SHAPE_C);
    ASSERT_FALSE(b.manifestRemoved);
    ASSERT_TRUE(b.opened);
}

/* ---- r18 finding 3: a stat fault is not an absence ---------------------- */
/*
 * ⛔ THE THIRD STATE. r18's inspection treated any stat failure as "nothing
 * there", so a transient fault let the truncation proceed beside a manifest
 * that still described the old bytes. A two-valued "does it exist?" cannot
 * represent UNREADABLE, and collapsing it lands on the PERMISSIVE value --
 * which is the general shape, not a detail of this function.
 */
TEST(r18_proceeds_when_the_target_cannot_be_inspected) {
    World w = world(FS_UNREADABLE, FS_ABSENT, false, false);
    model_open_target(&w, SHAPE_R18);
    ASSERT_TRUE(w.opened);                  /* truncated a file it never read */
}

TEST(c_fails_closed_when_the_target_cannot_be_inspected) {
    World w = world(FS_UNREADABLE, FS_ABSENT, false, false);
    model_open_target(&w, SHAPE_C);
    ASSERT_FALSE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_TARGET_UNREADABLE);
}

/* ...and the opt-in does NOT buy past a fault. The user authorised overwriting
 * a FILE, not proceeding blind on a failing card. */
TEST(the_opt_in_does_not_override_a_fail_closed_inspection) {
    World w = world(FS_UNREADABLE, FS_ABSENT, false, true);
    model_open_target(&w, SHAPE_C);
    ASSERT_FALSE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_TARGET_UNREADABLE);
}

TEST(c_fails_closed_when_the_manifest_cannot_be_inspected) {
    World w = world(FS_ABSENT, FS_UNREADABLE, false, false);
    model_open_target(&w, SHAPE_C);
    ASSERT_FALSE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_TARGET_UNREADABLE);
}

/* ---- a DIRECTORY at either name ---------------------------------------- */
/*
 * A directory stats with fsize == 0, which the create path's predicate read as
 * "nothing there to lose" -- harmless for an open-for-write, destructive once
 * the same predicate guarded an unlink. Under C the directory never reaches
 * that question without an opt-in, and the predicate's own refusal remains.
 */
TEST(a_directory_at_the_target_name_is_refused_not_opened) {
    World w = world(FS_DIR, FS_ABSENT, false, false);
    model_open_target(&w, SHAPE_C);
    ASSERT_FALSE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_TARGET_EXISTS);
}

TEST(a_directory_at_the_manifest_name_is_left_alone) {
    World w = world(FS_ABSENT, FS_DIR, false, true);  /* even when authorised */
    model_open_target(&w, SHAPE_C);
    ASSERT_FALSE(w.manifestRemoved);
}

/* ---- the whole table, as one sweep ------------------------------------- */
/*
 * ⭐ THE PROPERTY THAT MATTERS ACROSS EVERY CASE, rather than case by case:
 * under C nothing is destroyed without the opt-in. Enumerating the table is
 * what makes that a claim about the FUNCTION instead of about the nine
 * fixtures above -- and it is cheap, so there is no reason to assert less.
 */
TEST(without_the_opt_in_c_destroys_nothing_anywhere_in_the_table) {
    const FsState states[] = { FS_ABSENT, FS_FILE, FS_DIR, FS_UNREADABLE };
    unsigned checked = 0;
    for (unsigned d = 0; d < 4; d++) {
        for (unsigned m = 0; m < 4; m++) {
            for (unsigned ours = 0; ours < 2; ours++) {
                World w = world(states[d], states[m], ours != 0, false);
                model_open_target(&w, SHAPE_C);
                /* never an unlink */
                ASSERT_FALSE(w.manifestRemoved);
                /* and never a truncating open over existing bytes */
                if (states[d] == FS_FILE || states[d] == FS_DIR) {
                    ASSERT_FALSE(w.opened);
                }
                if (states[d] == FS_UNREADABLE) {
                    ASSERT_FALSE(w.opened);
                }
                checked++;
            }
        }
    }
    /* ⭐ NON-VACUITY: a loop that ran zero times asserts nothing, and a
     * mis-sized bound is invisible in a passing run. */
    ASSERT_EQ(checked, 32u);
}

/* The converse sweep, so the one above is not satisfied by a gate that simply
 * refuses everything: with the opt-in, every INSPECTABLE case proceeds. */
TEST(with_the_opt_in_every_inspectable_case_proceeds) {
    const FsState states[] = { FS_ABSENT, FS_FILE, FS_DIR };
    unsigned opened = 0;
    for (unsigned d = 0; d < 3; d++) {
        for (unsigned m = 0; m < 3; m++) {
            World w = world(states[d], states[m], false, true);
            model_open_target(&w, SHAPE_C);
            ASSERT_TRUE(w.opened);
            ASSERT_EQ(w.refusal, REFUSE_NONE);
            opened++;
        }
    }
    ASSERT_EQ(opened, 9u);
}

/* ---- the refusal must not wedge the card ------------------------------- */
/*
 * ⛔ FOUND BY A REVIEW QUESTION, NOT BY THIS SUITE: "when a session is refused
 * at the open, is the card left usable for the next command?"
 *
 * It was not. Leaving the handle INVALID routes a refusal into the generic
 * failed-open arm, which is built for a TRANSIENT I/O error: it unmounts and
 * re-INITs with mode still WRITE, so the session re-arms and re-enters
 * OPEN_FILE. For an I/O glitch that retry is correct. For "the file exists and
 * you did not ask to overwrite it" it can never succeed -- and because
 * IsBusyLocked() tests mode BEFORE any state, IsBusy() stays true for ever and
 * every SD command is refused, INCLUDING the SD:DELete that would remove the
 * file and the SD:FILE that would grant the opt-in.
 *
 * ⭐ The refusal would have wedged the card against its own remedy. The
 * property is therefore not "the refusal is reported" but "the refusal ENDS
 * the session", and the two look identical from any test that only checks the
 * open did not happen -- which is every other arm in this file.
 */
TEST(a_refusal_without_the_disarm_wedges_the_card) {
    World w = world(FS_FILE, FS_ABSENT, false, false);
    model_open_target(&w, SHAPE_C_NO_DISARM);
    model_finish_refusal(&w, SHAPE_C_NO_DISARM);
    ASSERT_FALSE(w.opened);                  /* correctly refused... */
    ASSERT_EQ(w.refusal, REFUSE_TARGET_EXISTS);
    ASSERT_TRUE(model_is_busy(&w));          /* ...and wedged: busy for ever */
}

TEST(a_refusal_disarms_the_session_so_the_card_stays_usable) {
    World w = world(FS_FILE, FS_ABSENT, false, false);
    model_open_target(&w, SHAPE_C);
    model_finish_refusal(&w, SHAPE_C);
    ASSERT_FALSE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_TARGET_EXISTS);
    ASSERT_FALSE(model_is_busy(&w));         /* the remedy is reachable */
}

/* EVERY refusal reason must disarm, not just the headline one -- a fail-closed
 * inspection fault is just as terminal, and the manifest refusal is reached by
 * a different branch. An arm that only covered TARGET_EXISTS would leave two
 * wedges behind. */
TEST(every_refusal_reason_disarms_the_session) {
    const FsState states[] = { FS_ABSENT, FS_FILE, FS_DIR, FS_UNREADABLE };
    unsigned refusals = 0;
    for (unsigned d = 0; d < 4; d++) {
        for (unsigned m = 0; m < 4; m++) {
            World w = world(states[d], states[m], false, false);
            model_open_target(&w, SHAPE_C);
            model_finish_refusal(&w, SHAPE_C);
            if (w.refusal != REFUSE_NONE) {
                ASSERT_FALSE(model_is_busy(&w));
                refusals++;
            } else {
                ASSERT_TRUE(w.opened);
            }
        }
    }
    /* ⭐ NON-VACUITY: if the table stopped producing refusals this arm would
     * pass having asserted nothing about a refusal at all. */
    ASSERT_TRUE(refusals >= 8u);
}

/* ...and a SUCCESSFUL open must NOT disarm: the session has to stay armed or
 * nothing writes. The control that stops "disarm everything" passing. */
TEST(a_successful_open_leaves_the_session_armed) {
    World w = world(FS_ABSENT, FS_ABSENT, false, false);
    model_open_target(&w, SHAPE_C);
    model_finish_refusal(&w, SHAPE_C);
    ASSERT_TRUE(w.opened);
    ASSERT_TRUE(model_is_busy(&w));
}

int main(void) {
    RUN(r18_truncates_an_existing_target_without_being_asked);
    RUN(c_refuses_an_existing_target);
    RUN(c_overwrites_an_existing_target_when_asked);
    RUN(c_opens_a_fresh_target_with_no_opt_in);
    RUN(r18_deletes_another_captures_manifest_with_nothing_overwritten);
    RUN(c_refuses_rather_than_deleting_a_manifest_it_cannot_claim);
    RUN(c_removes_a_prior_manifest_only_when_authorised);
    RUN(our_own_live_manifest_is_never_removed_in_either_shape);
    RUN(r18_proceeds_when_the_target_cannot_be_inspected);
    RUN(c_fails_closed_when_the_target_cannot_be_inspected);
    RUN(the_opt_in_does_not_override_a_fail_closed_inspection);
    RUN(c_fails_closed_when_the_manifest_cannot_be_inspected);
    RUN(a_directory_at_the_target_name_is_refused_not_opened);
    RUN(a_directory_at_the_manifest_name_is_left_alone);
    RUN(without_the_opt_in_c_destroys_nothing_anywhere_in_the_table);
    RUN(with_the_opt_in_every_inspectable_case_proceeds);
    RUN(a_refusal_without_the_disarm_wedges_the_card);
    RUN(a_refusal_disarms_the_session_so_the_card_stays_usable);
    RUN(every_refusal_reason_disarms_the_session);
    RUN(a_successful_open_leaves_the_session_armed);
    return TEST_SUMMARY();
}
