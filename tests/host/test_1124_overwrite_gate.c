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
    /* r21 item 1: the grant named a different resolved path than the one
     * about to be written. */
    REFUSE_AUTH_PATH_CHANGED,
    /* r21 item 3: another capture's manifest records this exact path. */
    REFUSE_FOREIGN_RECORD,
    /* r21 item 3, fail closed: a manifest could not be read or parsed. */
    REFUSE_MANIFEST_UNPARSABLE,
} Refusal;

/* r21 item 3: what the OTHER manifests in this directory look like.
 *
 * FOREIGN_PRESENT_ONLY is the control that stops "refuse whenever anyone else
 * logged here" from passing: sharing a directory is ordinary and must not
 * refuse. FOREIGN_UNPARSABLE is the third state -- a manifest that cannot be
 * read is NOT a manifest that records nothing, and reading it as the latter is
 * what turns this scan into a clean-bill generator. */
typedef enum {
    FOREIGN_NONE = 0,
    FOREIGN_PRESENT_ONLY,
    FOREIGN_RECORDS_TARGET,
    FOREIGN_UNPARSABLE
} ForeignKind;

/* r20: whether the target's name is a rotation part of an EXISTING manifest's
 * base -- `exp-1.csv` while `exp.csv.mfst` exists. The opt-in cannot authorise
 * overwriting that, because the client named `exp-1.csv` and never named the
 * base whose record would be left claiming changed bytes. */
typedef enum { PART_NO = 0, PART_OF_RECORDED_SET } PartKind;

typedef struct {
    FsState dataFile;        /* the target the caller is about to open */
    FsState manifestAtName;  /* whatever sits at the target's manifest name */
    bool    manifestIsOurLiveOne;
    bool    overwriteAsked;  /* the SESSION's consumed authorisation */
    bool    stagedAuth;      /* the SD:FILE staged flag, pre-consume */
    PartKind part;           /* r20: is this target a recorded set's part? */
    bool    inspectorFaults; /* r20: the manifest inspector cannot tell */
    /* r21 item 1: does the path the grant was given FOR still resolve to the
     * path about to be written? False models an SD:DIR/SD:FILE having moved
     * the session's identity between the grant and this open. */
    bool    grantSubjectRecorded; /* the grant recorded a subject at all */
    bool    grantSubjectMatches;
    ForeignKind foreign;     /* r21 item 3 */
    /* r21 audit: how many foreign manifests sit in the directory, and whether
     * one of them is actually a DATA file that merely ends in .mfst. */
    unsigned foreignManifests;
    bool     mfstNameIsADataFile;
    bool     realManifestIsDamaged;
    /* r22 audit: a FOREIGN record whose part index is past this session's own
     * limit (exp-5000.csv), and the bucket-parent layout. */
    bool     foreignOverLimitRecord;
    bool     dirIsBucket;
    bool     parentRecordsTarget;
    /* r23 audit (:2980): a manifest INSIDE one of our bucket children, written
     * by a session configured at `D/P001`. The mirror of the parent case: we
     * are at the top of the bucket set and must look DOWN. */
    bool     bucketChildRecordsTarget;
    /* r23 audit (:424): a foreign record whose index OVERFLOWS the parse. */
    bool     foreignOverflowRecord;
    /* r25 audit (:2890): THIS session's own part counter is at or above the
     * bitset's range. Reachable because the existing-target REOPEN path
     * suppresses the bucket roll AND the count that feeds it, so the counter
     * is not bounded by bucket capacity. */
    bool     counterAboveLimit;
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
    /* r20: one resolved path for the gate AND creation. false reproduces r18/
     * r19 finding 1, where the gate stripped a subdirectory and the creation
     * path did not. */
    bool oneResolvedPath;
    /* r20: refuse an opt-in whose target is a part of a recorded set. */
    bool refusePartOfRecordedSet;
    /* r20: an inspector that cannot tell must refuse, not be read as
     * "not a manifest" and therefore safe to proceed past. */
    bool inspectorFaultRefuses;
    /* r20 / ruling V=A: the authorisation is consumed when the session arms. */
    bool consumeAtArm;
    /* r21 item 1: the grant is bound to the resolved path it was granted for,
     * and the open refuses on any difference. False = r20, where the grant was
     * a bare yes about whatever the config said by the time it was consumed. */
    bool grantBoundToPath;
    /* r21 item 3: refuse a target that another capture's manifest records. */
    bool refuseForeignRecord;
    /* r21 item 3: a manifest that cannot be parsed REFUSES. False models the
     * attractive wrong version -- skip the bad line and carry on -- which makes
     * a corrupt manifest read as recording nothing. */
    bool scanFaultRefuses;
    /* r21 audit HIGH (:2915): a COUNT of pre-existing manifests must never
     * refuse a session. False reproduces what the audit blocked -- a FAULT
     * past 16 manifests, which disarmed every capture after 17 short sessions
     * in one directory. */
    bool countNeverRefuses;
    /* r21 audit MEDIUM (:2909): a .mfst is identified by CONTENT, so a data
     * file wearing that name is skipped rather than parsed-and-FAULTed. */
    bool identifyByContent;
    /* r22 audit MEDIUM (:2871): an index past our part limit is SKIPPED. False
     * reproduces r21, which FAULTed -- and FAULT is a refusal, so one foreign
     * exp-5000.csv disarmed every capture in the directory. */
    bool overLimitIsSkipped;
    /* r22 audit MEDIUM (:2930): when the configured directory is itself a
     * bucket, the PARENT is scanned too -- for every session, not just an
     * opt-in, because creation alone invalidates a parent's record. */
    bool scansParentOfBucket;
    /* r23 audit HIGH (:2980): the immediate bucket CHILDREN are scanned too. */
    bool scansBucketChildren;
    /* r23 audit MEDIUM (:424): an overflowing index is SKIPPED. False
     * reproduces r22, which faulted -- so `exp-4294967296.csv` refused. */
    bool overflowIsSkipped;
    /* r25 audit HIGH (:2890): an index the bitset cannot represent routes to
     * the EXACT comparison. False reproduces r24, which SKIPPED it -- a FALSE
     * NEGATIVE in the core check, which is a silent miss rather than a loud
     * refusal and so strictly worse than the faults r22-r24 removed. */
    bool aboveLimitRoutesToExact;
} Shape;

static const Shape SHAPE_R18 = { true,  false, false, false, false, false,
                                 false, false, false, false, false,
                                 false, false, 
                                 false, false, false };
/* r19 as shipped: refuse-by-default and the disarm, but the gate still derived
 * the manifest name independently, honoured an opt-in on a recorded set's
 * part, let an inspector fault read as permissive, and never consumed. */
static const Shape SHAPE_R19 = { false, true,  false, false, false, false,
                                 false, false, false, false, false,
                                 false, false, 
                                 false, false, false };
/* r20 as shipped: everything r19 lacked, and none of r21's three. This is the
 * baseline every r21 test is differential against -- without it, "the fix
 * works" would be a claim about a shape nobody ever ran. */
static const Shape SHAPE_R20 = { false, true,  true,  true,  true,  true,
                                 false, false, false, false, false,
                                 false, false, 
                                 false, false, false };
/* What the r21 audit BLOCKED: every r21 property, and neither audit fix. This
 * is the baseline the two arms below are differential against. */
static const Shape SHAPE_R21_AUDITED = { false, true, true, true, true, true,
                                         true, true, true, false, false,
                                         false, false, 
                                         false, false, false };
/* What the r22 audit BLOCKED: every r21 fix plus both r21-audit fixes, and
 * neither r22-audit fix. The baseline the arms below are differential against. */
/* What the r23 BLIND leg found at: every fix through r22's audit, and neither
 * r23 fix. (r23's steered leg failed on capacity, so that round is INCOMPLETE
 * coverage -- this baseline is named for what it contains, not for a clean
 * round.) */
/* What r25's blind leg found at: every fix through r24, and not r25's. */
static const Shape SHAPE_R24_AUDITED = { false, true, true, true, true, true,
                                         true, true, true, true, true,
                                         true, true, true, true, false };
static const Shape SHAPE_R23_AUDITED = { false, true, true, true, true, true,
                                         true, true, true, true, true,
                                         true, true, false, false, false };
static const Shape SHAPE_R22_AUDITED = { false, true, true, true, true, true,
                                         true, true, true, true, true,
                                         false, false, 
                                         false, false, false };
static const Shape SHAPE_C   = { false, true,  true,  true,  true,  true,
                                 true,  true,  true,  true,  true,
                                 true,  true,  
                                 true,  true, true };
/* r21 with the scan's fail-closed arm removed: the one-line mistake that makes
 * an unparseable manifest read as empty. Everything else intact. */
static const Shape SHAPE_C_SCAN_SKIPS = { false, true, true, true, true, true,
                                          true, true, false, true, true,
                                          true, true, 
                                          true, true, true };
/* C's gate without the disarm: what this PR shipped before conv-ts asked
 * whether a refused session leaves the card usable. */
static const Shape SHAPE_C_NO_DISARM = { false, false, true, true, true, true,
                                         true, true, true, true, true,
                                         true, true, 
                                         true, true, true };

static World world(FsState data, FsState mfst, bool ours, bool asked) {
    World w;
    memset(&w, 0, sizeof(w));
    w.dataFile = data;
    w.manifestAtName = mfst;
    w.manifestIsOurLiveOne = ours;
    w.overwriteAsked = asked;
    w.stagedAuth = asked;
    w.sessionArmed = true;          /* mode == WRITE: the session is running */
    /* r21: the ordinary world is "the grant recorded its subject and the
     * config has not moved", so these default to the non-hazard value and each
     * test that cares sets it explicitly. They are WORLD facts (what happened
     * on the device), not revision dimensions -- the revision dimension is
     * whether the code CHECKS them, which lives in Shape. */
    w.grantSubjectRecorded = asked;
    w.grantSubjectMatches = true;
    w.foreign = FOREIGN_NONE;
    return w;
}

/* sd_UpdateSettingsImpl's WRITE arm: ONE atomic transition that takes the
 * staged authorisation into the session and clears the staged copy. Ruling
 * V=A: one opt-in authorises one session, and SD:FILE? reports 0 after. */
static void model_arm_session(World *w, Shape s) {
    if (!s.consumeAtArm) {
        w->overwriteAsked = w->stagedAuth;   /* r19: re-read, never consumed */
        return;
    }
    w->overwriteAsked = w->stagedAuth;
    w->stagedAuth = false;
}

/* SD:DIR -- r19 finding 4: the authorisation is bound to the RESOLVED target,
 * so changing the directory withdraws it. */
static void model_set_directory(World *w) { w->stagedAuth = false; }

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

    /* r21 item 1: the grant must be for THIS resolved path. Inside the
     * opt-in, because a session with no grant destroys nothing. */
    if (w->overwriteAsked && s.grantBoundToPath) {
        if (!w->grantSubjectRecorded || !w->grantSubjectMatches) {
            w->refusal = REFUSE_AUTH_PATH_CHANGED;
            return;
        }
    }
    /* r21 item 3: no foreign manifest may RECORD this target. OUTSIDE the
     * opt-in: a granted session can TRUNCATE a recorded file, but ANY session
     * can CREATE one at a recorded-but-deleted name, and both leave that
     * manifest describing bytes it never recorded. */
    /* r21 audit HIGH (:2915): the manifest COUNT. A cap here is a refusal, and
     * a refusal driven by how much the user has already logged is a brick. */
    if (s.refuseForeignRecord && !s.countNeverRefuses
            && (w->foreignManifests > 16u)) {
        w->refusal = REFUSE_MANIFEST_UNPARSABLE;
        return;
    }
    /* r21 audit MEDIUM (:2909): a DATA file that merely ends in .mfst. Parsed
     * as a manifest it fails and FAULTs, blocking every later capture; the fix
     * identifies by content and skips it. A genuinely DAMAGED manifest still
     * faults -- that distinction is the whole point. */
    if (s.refuseForeignRecord && w->mfstNameIsADataFile && !s.identifyByContent) {
        w->refusal = REFUSE_MANIFEST_UNPARSABLE;
        return;
    }
    if (s.refuseForeignRecord && w->realManifestIsDamaged && s.scanFaultRefuses) {
        w->refusal = REFUSE_MANIFEST_UNPARSABLE;
        return;
    }
    /* r22 audit MEDIUM (:2871): a foreign record whose index is past our own
     * part limit. It cannot be one of our names, so there is nothing to check
     * -- skip it. r21 FAULTed, i.e. refused. */
    if (s.refuseForeignRecord && w->foreignOverLimitRecord
            && !s.overLimitIsSkipped) {
        w->refusal = REFUSE_MANIFEST_UNPARSABLE;
        return;
    }
    /* r23 audit MEDIUM (:424): a foreign record whose index overflows the
     * parse. Same answer as an over-limit one -- it cannot be a name we open,
     * so skip it. r22 faulted, i.e. refused. */
    if (s.refuseForeignRecord && w->foreignOverflowRecord
            && !s.overflowIsSkipped) {
        w->refusal = REFUSE_MANIFEST_UNPARSABLE;
        return;
    }
    /* ⛔⛔ r25 audit HIGH (:2890): a foreign record ABOVE the bitset's range,
     * and OUR counter is up there too. The bitset cannot represent it, so
     * SKIPPING it is a false negative -- it must route to the exact check. */
    if (s.refuseForeignRecord && w->foreignOverLimitRecord
            && w->counterAboveLimit) {
        if (s.aboveLimitRoutesToExact) {
            w->refusal = REFUSE_FOREIGN_RECORD;
            return;
        }
        /* r24's behaviour: skipped, so the collision went unseen. */
    }
    /* r23 audit HIGH (:2980): the record lives in a manifest INSIDE one of our
     * bucket children, written by a session configured there. The mirror of
     * the parent case, and it needs no opt-in for the same reason. */
    if (s.refuseForeignRecord && w->bucketChildRecordsTarget) {
        if (s.scansBucketChildren) {
            w->refusal = REFUSE_FOREIGN_RECORD;
            return;
        }
        /* r23's behaviour: the bucket children were never read. */
    }
    /* r22 audit MEDIUM (:2930): the configured directory IS a bucket and the
     * record lives in the parent's manifest. Needs no opt-in: the part was
     * deleted, so creating it refuses nothing and the parent's record starts
     * describing our bytes. */
    if (s.refuseForeignRecord && w->dirIsBucket && w->parentRecordsTarget) {
        if (s.scansParentOfBucket) {
            w->refusal = REFUSE_FOREIGN_RECORD;
            return;
        }
        /* r22's behaviour: the parent was never read. */
    }
    if (s.refuseForeignRecord) {
        if (w->foreign == FOREIGN_RECORDS_TARGET) {
            w->refusal = REFUSE_FOREIGN_RECORD;
            return;
        }
        if (w->foreign == FOREIGN_UNPARSABLE) {
            if (s.scanFaultRefuses) {
                w->refusal = REFUSE_MANIFEST_UNPARSABLE;
                return;
            }
            /* The attractive wrong version: the bad line is skipped, so the
             * manifest reads as recording nothing and the write proceeds. */
        }
    }

    /* r20: a part of a recorded set -- checked even WITH the opt-in. */
    if (s.refusePartOfRecordedSet && (w->part == PART_OF_RECORDED_SET)) {
        w->refusal = REFUSE_MANIFEST_NOT_OURS;
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
        /* r20: the inspector cannot tell. r19 read that as "not a manifest"
         * and proceeded; it must refuse. */
        if (w->inspectorFaults) {
            if (s.inspectorFaultRefuses) {
                w->refusal = REFUSE_TARGET_UNREADABLE;
                return;
            }
            /* r19's behaviour: a refusal converted into permission. */
            w->opened = true;
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

/* ---- r19's six findings, as pairs -------------------------------------- */

/* (1) A subdirectory target. r19's gate stripped "sub/" and asked about a
 * DIFFERENT capture's sidecar; one shared derivation makes them agree. Modelled
 * as the shape flag, because the divergence is structural, not per-fixture. */
TEST(r19_gate_and_creation_disagreed_on_a_subdirectory_target) {
    ASSERT_FALSE(SHAPE_R19.oneResolvedPath);
    ASSERT_TRUE(SHAPE_C.oneResolvedPath);
}

/* (2) A part of a recorded set, WITH the opt-in. */
TEST(r19_honoured_an_opt_in_on_a_recorded_sets_part) {
    World w = world(FS_FILE, FS_ABSENT, false, true);
    w.part = PART_OF_RECORDED_SET;
    model_open_target(&w, SHAPE_R19);
    ASSERT_TRUE(w.opened);               /* truncated a recorded part */
}

TEST(c_refuses_an_opt_in_on_a_recorded_sets_part) {
    World w = world(FS_FILE, FS_ABSENT, false, true);
    w.part = PART_OF_RECORDED_SET;
    model_open_target(&w, SHAPE_C);
    ASSERT_FALSE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_MANIFEST_NOT_OURS);
}

/* (3) An inspector that cannot tell. */
TEST(r19_read_an_inspector_fault_as_permission) {
    World w = world(FS_ABSENT, FS_FILE, false, true);
    w.inspectorFaults = true;
    model_open_target(&w, SHAPE_R19);
    ASSERT_TRUE(w.opened);               /* proceeded past "could not tell" */
}

TEST(c_refuses_when_the_inspector_cannot_tell) {
    World w = world(FS_ABSENT, FS_FILE, false, true);
    w.inspectorFaults = true;
    model_open_target(&w, SHAPE_C);
    ASSERT_FALSE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_TARGET_UNREADABLE);
}

/* (4) A directory change must withdraw the authorisation. */
TEST(a_directory_change_withdraws_the_authorisation) {
    World w = world(FS_FILE, FS_ABSENT, false, true);
    ASSERT_TRUE(w.stagedAuth);
    model_set_directory(&w);             /* SD:DIR "archive" */
    model_arm_session(&w, SHAPE_C);
    model_open_target(&w, SHAPE_C);
    ASSERT_FALSE(w.opened);              /* the new target is NOT overwritable */
    ASSERT_EQ(w.refusal, REFUSE_TARGET_EXISTS);
}

/* (5) Ruling V=A: one opt-in authorises ONE session. */
TEST(the_authorisation_is_consumed_so_a_second_session_is_refused) {
    World w = world(FS_FILE, FS_ABSENT, false, true);
    model_arm_session(&w, SHAPE_C);
    ASSERT_FALSE(w.stagedAuth);          /* SD:FILE? reports 0 now */
    model_open_target(&w, SHAPE_C);
    ASSERT_TRUE(w.opened);               /* session 1 was authorised */

    /* session 2, no fresh ",1" */
    World n = world(FS_FILE, FS_ABSENT, false, false);
    n.stagedAuth = w.stagedAuth;         /* i.e. false */
    model_arm_session(&n, SHAPE_C);
    model_open_target(&n, SHAPE_C);
    ASSERT_FALSE(n.opened);
    ASSERT_EQ(n.refusal, REFUSE_TARGET_EXISTS);
}

TEST(r19_never_consumed_so_a_second_session_silently_overwrote) {
    World w = world(FS_FILE, FS_ABSENT, false, true);
    model_arm_session(&w, SHAPE_R19);
    ASSERT_TRUE(w.stagedAuth);           /* still armed -- the contradiction */
    World n = world(FS_FILE, FS_ABSENT, false, false);
    n.stagedAuth = w.stagedAuth;         /* true, carried over */
    model_arm_session(&n, SHAPE_R19);
    model_open_target(&n, SHAPE_R19);
    ASSERT_TRUE(n.opened);               /* a second session, no new opt-in */
}

/* A refused session still clears the flag -- the ruling's accepted trade. */
TEST(a_refused_session_still_consumes_the_authorisation) {
    World w = world(FS_UNREADABLE, FS_ABSENT, false, true);
    model_arm_session(&w, SHAPE_C);
    model_open_target(&w, SHAPE_C);
    ASSERT_FALSE(w.opened);              /* refused, unrelated reason */
    ASSERT_EQ(w.refusal, REFUSE_TARGET_UNREADABLE);
    ASSERT_FALSE(w.stagedAuth);          /* and the flag is gone anyway */
}

/* ---- r21 item 1: a grant is FOR a path -------------------------------- */

/* ⛔ What r20 shipped. The grant was a bare yes, so it applied to whatever the
 * configuration said by the time the SD task consumed it. */
TEST(r20_honoured_a_grant_after_the_resolved_path_moved) {
    World w = world(FS_FILE, FS_ABSENT, false, true);
    w.grantSubjectMatches = false;          /* SD:DIR/SD:FILE moved it */
    model_arm_session(&w, SHAPE_R20);
    model_open_target(&w, SHAPE_R20);
    ASSERT_TRUE(w.opened);                  /* truncated a file nobody named */
    ASSERT_EQ(w.refusal, REFUSE_NONE);
}

TEST(r21_refuses_a_grant_whose_resolved_path_moved) {
    World w = world(FS_FILE, FS_ABSENT, false, true);
    w.grantSubjectMatches = false;
    model_arm_session(&w, SHAPE_C);
    model_open_target(&w, SHAPE_C);
    model_finish_refusal(&w, SHAPE_C);
    ASSERT_FALSE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_AUTH_PATH_CHANGED);
    ASSERT_FALSE(model_is_busy(&w));        /* and the card stays usable */
}

/* A grant that never recorded a subject is not permission either -- the
 * fail-closed half of the same check. */
TEST(r21_refuses_a_grant_that_recorded_no_subject) {
    World w = world(FS_FILE, FS_ABSENT, false, true);
    w.grantSubjectRecorded = false;
    model_arm_session(&w, SHAPE_C);
    model_open_target(&w, SHAPE_C);
    ASSERT_FALSE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_AUTH_PATH_CHANGED);
}

/* ⭐ THE CONTROL. Without this, "refuse every grant" passes both arms above
 * and the opt-in silently stops working at all. */
TEST(r21_honours_a_grant_for_the_path_it_was_given_for) {
    World w = world(FS_FILE, FS_ABSENT, false, true);
    model_arm_session(&w, SHAPE_C);
    model_open_target(&w, SHAPE_C);
    ASSERT_TRUE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_NONE);
}

/* ---- r21 item 3: a foreign manifest's records are off limits ----------- */

TEST(r20_truncated_a_file_another_manifest_records) {
    World w = world(FS_FILE, FS_ABSENT, false, true);
    w.foreign = FOREIGN_RECORDS_TARGET;
    model_arm_session(&w, SHAPE_R20);
    model_open_target(&w, SHAPE_R20);
    ASSERT_TRUE(w.opened);  /* that manifest now describes bytes it never saw */
    ASSERT_EQ(w.refusal, REFUSE_NONE);
}

TEST(r21_refuses_a_target_another_manifest_records) {
    World w = world(FS_FILE, FS_ABSENT, false, true);
    w.foreign = FOREIGN_RECORDS_TARGET;
    model_arm_session(&w, SHAPE_C);
    model_open_target(&w, SHAPE_C);
    model_finish_refusal(&w, SHAPE_C);
    ASSERT_FALSE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_FOREIGN_RECORD);
    ASSERT_FALSE(model_is_busy(&w));
}

/* ⛔⛔ THE SCOPE CASE, and the one my first design would have missed.
 *
 * No opt-in, and the target does NOT exist -- so nothing is truncated and the
 * existence check never fires. The file was DELETED while its manifest was
 * kept, so creating it fresh makes that manifest assert a size and a CRC32
 * over our bytes. The harm needs no truncation, which is why this check cannot
 * live inside the opt-in. */
TEST(r21_refuses_a_recorded_name_even_with_no_opt_in) {
    World w = world(FS_ABSENT, FS_ABSENT, false, false);
    w.foreign = FOREIGN_RECORDS_TARGET;
    model_arm_session(&w, SHAPE_C);
    model_open_target(&w, SHAPE_C);
    ASSERT_FALSE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_FOREIGN_RECORD);
}

/* The same world under r20: created, and the foreign record silently became a
 * lie. This is the pair that makes the test above evidence. */
TEST(r20_created_a_recorded_name_and_invalidated_the_record) {
    World w = world(FS_ABSENT, FS_ABSENT, false, false);
    w.foreign = FOREIGN_RECORDS_TARGET;
    model_arm_session(&w, SHAPE_R20);
    model_open_target(&w, SHAPE_R20);
    ASSERT_TRUE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_NONE);
}

/* ⭐ THE OVER-REFUSAL CONTROL. Sharing a directory with another capture is
 * ordinary. If this fails, the guard has become "refuse whenever anyone else
 * logged here", which would break the common two-captures-one-directory case
 * and would still satisfy every refusal arm above. */
TEST(a_foreign_manifest_recording_nothing_of_ours_does_not_refuse) {
    World w = world(FS_ABSENT, FS_ABSENT, false, false);
    w.foreign = FOREIGN_PRESENT_ONLY;
    model_arm_session(&w, SHAPE_C);
    model_open_target(&w, SHAPE_C);
    ASSERT_TRUE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_NONE);
}

/* ---- r21 item 3, constraint (b): the parser never trusts the file ------- */

TEST(r21_refuses_when_a_manifest_cannot_be_parsed) {
    World w = world(FS_ABSENT, FS_ABSENT, false, false);
    w.foreign = FOREIGN_UNPARSABLE;
    model_arm_session(&w, SHAPE_C);
    model_open_target(&w, SHAPE_C);
    model_finish_refusal(&w, SHAPE_C);
    ASSERT_FALSE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_MANIFEST_UNPARSABLE);
    ASSERT_FALSE(model_is_busy(&w));
}

/* ⛔ The attractive wrong version, run so the arm above is not a claim about
 * code nobody wrote: skip the unparseable line and carry on. The manifest then
 * reads as recording NOTHING, which is the permissive answer over exactly the
 * input that should refuse. */
TEST(skipping_an_unparseable_line_would_have_let_it_through) {
    World w = world(FS_ABSENT, FS_ABSENT, false, false);
    w.foreign = FOREIGN_UNPARSABLE;
    model_arm_session(&w, SHAPE_C_SCAN_SKIPS);
    model_open_target(&w, SHAPE_C_SCAN_SKIPS);
    ASSERT_TRUE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_NONE);
}

/* The sweep from the disarm test, re-run across the r21 worlds: every one of
 * the three new reasons must also end the operation, not merely report it.
 * A per-reason arm covering only the headline case is how the two fail-closed
 * wedges survived r19. */
TEST(every_r21_refusal_reason_also_disarms_the_session) {
    const ForeignKind kinds[] = { FOREIGN_NONE, FOREIGN_PRESENT_ONLY,
                                  FOREIGN_RECORDS_TARGET, FOREIGN_UNPARSABLE };
    unsigned seenAuth = 0, seenForeign = 0, seenUnparsable = 0;
    for (unsigned k = 0; k < 4; k++) {
        for (unsigned asked = 0; asked < 2; asked++) {
            for (unsigned moved = 0; moved < 2; moved++) {
                World w = world(FS_ABSENT, FS_ABSENT, false, asked != 0);
                w.foreign = kinds[k];
                w.grantSubjectMatches = (moved == 0);
                model_arm_session(&w, SHAPE_C);
                model_open_target(&w, SHAPE_C);
                model_finish_refusal(&w, SHAPE_C);
                if (w.refusal == REFUSE_NONE) {
                    ASSERT_TRUE(w.opened);
                    ASSERT_TRUE(model_is_busy(&w));
                    continue;
                }
                ASSERT_FALSE(w.opened);
                ASSERT_FALSE(model_is_busy(&w));
                if (w.refusal == REFUSE_AUTH_PATH_CHANGED)   { seenAuth++; }
                if (w.refusal == REFUSE_FOREIGN_RECORD)      { seenForeign++; }
                if (w.refusal == REFUSE_MANIFEST_UNPARSABLE) { seenUnparsable++; }
            }
        }
    }
    /* ⭐ NON-VACUITY, per reason. Without these three the sweep passes if the
     * table stopped producing that refusal at all -- which is the failure mode
     * a sweep is most prone to, because it still looks busy. */
    ASSERT_TRUE(seenAuth > 0u);
    ASSERT_TRUE(seenForeign > 0u);
    ASSERT_TRUE(seenUnparsable > 0u);
}

/* ---- r21 audit HIGH (:2915): a count must never refuse ------------------ */

/* ⛔ What the audit blocked. Seventeen short sessions in one directory -- 34
 * files, well under MAX_DIR_FILES -- and every later capture is disarmed, with
 * or without the opt-in. The guard meant to protect ordinary logging broke it. */
TEST(the_audited_r21_refused_a_session_after_seventeen_short_sessions) {
    World w = world(FS_ABSENT, FS_ABSENT, false, false);
    w.foreignManifests = 17u;
    model_arm_session(&w, SHAPE_R21_AUDITED);
    model_open_target(&w, SHAPE_R21_AUDITED);
    ASSERT_FALSE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_MANIFEST_UNPARSABLE);
}

/* ⭐ The fix, and the arm the audit asked for by name: 63 manifests in the
 * directory and a fresh session still arms. The number is deliberately just
 * under MAX_DIR_FILES, because that -- not a cap of our own -- is what bounds
 * how many manifests a conforming directory can hold. */
TEST(a_manifest_count_never_refuses_a_session) {
    unsigned n;
    for (n = 0u; n <= 63u; n++) {
        World w = world(FS_ABSENT, FS_ABSENT, false, false);
        w.foreignManifests = n;
        model_arm_session(&w, SHAPE_C);
        model_open_target(&w, SHAPE_C);
        model_finish_refusal(&w, SHAPE_C);
        ASSERT_TRUE(w.opened);
        ASSERT_EQ(w.refusal, REFUSE_NONE);
        ASSERT_TRUE(model_is_busy(&w));      /* still armed, still logging */
    }
}

/* ---- r21 audit MEDIUM (:2909): .mfst is not proof of being a manifest --- */

TEST(the_audited_r21_faulted_on_a_data_file_named_mfst) {
    World w = world(FS_ABSENT, FS_ABSENT, false, false);
    w.mfstNameIsADataFile = true;
    model_arm_session(&w, SHAPE_R21_AUDITED);
    model_open_target(&w, SHAPE_R21_AUDITED);
    ASSERT_FALSE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_MANIFEST_UNPARSABLE);
}

TEST(a_non_manifest_file_named_mfst_is_skipped_not_faulted) {
    World w = world(FS_ABSENT, FS_ABSENT, false, false);
    w.mfstNameIsADataFile = true;
    model_arm_session(&w, SHAPE_C);
    model_open_target(&w, SHAPE_C);
    ASSERT_TRUE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_NONE);
}

/* ⭐ THE CONTROL that stops the fix becoming "skip everything that does not
 * parse". A file that IS a manifest and is DAMAGED is a damaged record, not an
 * unrelated file, and must still refuse. Without this arm, identifying by
 * content could degrade into ignoring every manifest it found hard to read --
 * which is the clean-bill generator the whole fail-closed design exists to
 * avoid. */
TEST(a_damaged_real_manifest_still_refuses) {
    World w = world(FS_ABSENT, FS_ABSENT, false, false);
    w.realManifestIsDamaged = true;
    model_arm_session(&w, SHAPE_C);
    model_open_target(&w, SHAPE_C);
    model_finish_refusal(&w, SHAPE_C);
    ASSERT_FALSE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_MANIFEST_UNPARSABLE);
    ASSERT_FALSE(model_is_busy(&w));
}

/* ---- r22 audit MEDIUM (:2871): a big foreign index is not a fault ------- */

TEST(the_audited_r22_faulted_on_a_foreign_record_with_a_large_index) {
    World w = world(FS_ABSENT, FS_ABSENT, false, false);
    w.foreignOverLimitRecord = true;
    model_arm_session(&w, SHAPE_R22_AUDITED);
    model_open_target(&w, SHAPE_R22_AUDITED);
    ASSERT_FALSE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_MANIFEST_UNPARSABLE);
}

/* ⭐ The arm the audit asked for by name: an exp-5000.csv record in the
 * directory, and a fresh exp.csv session still ARMS. */
TEST(a_foreign_record_with_an_index_past_our_limit_still_arms) {
    World w = world(FS_ABSENT, FS_ABSENT, false, false);
    w.foreignOverLimitRecord = true;
    model_arm_session(&w, SHAPE_C);
    model_open_target(&w, SHAPE_C);
    model_finish_refusal(&w, SHAPE_C);
    ASSERT_TRUE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_NONE);
    ASSERT_TRUE(model_is_busy(&w));
}

/* ---- r22 audit MEDIUM (:2930): the parent of a bucket directory --------- */

/* ⛔ The default path, with NO opt-in. r21 narrowed the opt-in away from this
 * layout, which closed truncation and left creation open. */
TEST(the_audited_r22_recreated_a_part_recorded_by_the_parents_manifest) {
    World w = world(FS_ABSENT, FS_ABSENT, false, false);
    w.dirIsBucket = true;
    w.parentRecordsTarget = true;
    model_arm_session(&w, SHAPE_R22_AUDITED);
    model_open_target(&w, SHAPE_R22_AUDITED);
    ASSERT_TRUE(w.opened);            /* the parent was never read */
    ASSERT_EQ(w.refusal, REFUSE_NONE);
}

TEST(a_bucket_directory_scans_its_parent_even_without_the_opt_in) {
    World w = world(FS_ABSENT, FS_ABSENT, false, false);
    w.dirIsBucket = true;
    w.parentRecordsTarget = true;
    model_arm_session(&w, SHAPE_C);
    model_open_target(&w, SHAPE_C);
    model_finish_refusal(&w, SHAPE_C);
    ASSERT_FALSE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_FOREIGN_RECORD);
    ASSERT_FALSE(model_is_busy(&w));
}

/* ⭐ THE CONTROL: an ordinary directory must not pay for this, and a bucket
 * directory whose parent records nothing of ours must still log. Without these
 * the two arms above are satisfied by "refuse whenever the directory looks
 * like a bucket", which would break ordinary logging into a bucket. */
TEST(a_bucket_directory_whose_parent_records_nothing_still_logs) {
    World a = world(FS_ABSENT, FS_ABSENT, false, false);
    a.dirIsBucket = true;             /* parentRecordsTarget stays false */
    model_arm_session(&a, SHAPE_C);
    model_open_target(&a, SHAPE_C);
    ASSERT_TRUE(a.opened);
    ASSERT_EQ(a.refusal, REFUSE_NONE);

    World b = world(FS_ABSENT, FS_ABSENT, false, false);
    b.parentRecordsTarget = true;     /* but the directory is NOT a bucket, so
                                       * there is no parent to consult */
    model_arm_session(&b, SHAPE_C);
    model_open_target(&b, SHAPE_C);
    ASSERT_TRUE(b.opened);
    ASSERT_EQ(b.refusal, REFUSE_NONE);
}

/* ---- r23 audit MEDIUM (:424): an overflowing index skips ---------------- */

TEST(the_audited_r23_faulted_on_a_foreign_record_whose_index_overflows) {
    World w = world(FS_ABSENT, FS_ABSENT, false, false);
    w.foreignOverflowRecord = true;
    model_arm_session(&w, SHAPE_R23_AUDITED);
    model_open_target(&w, SHAPE_R23_AUDITED);
    ASSERT_FALSE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_MANIFEST_UNPARSABLE);
}

TEST(a_foreign_record_whose_index_overflows_still_arms) {
    World w = world(FS_ABSENT, FS_ABSENT, false, false);
    w.foreignOverflowRecord = true;
    model_arm_session(&w, SHAPE_C);
    model_open_target(&w, SHAPE_C);
    model_finish_refusal(&w, SHAPE_C);
    ASSERT_TRUE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_NONE);
    ASSERT_TRUE(model_is_busy(&w));
}

/* ---- r23 audit HIGH (:2980): bucket children, the mirror of the parent --- */

TEST(the_audited_r23_missed_a_manifest_inside_a_bucket_child) {
    World w = world(FS_ABSENT, FS_ABSENT, false, false);
    w.bucketChildRecordsTarget = true;
    model_arm_session(&w, SHAPE_R23_AUDITED);
    model_open_target(&w, SHAPE_R23_AUDITED);
    ASSERT_TRUE(w.opened);            /* the children were never read */
    ASSERT_EQ(w.refusal, REFUSE_NONE);
}

TEST(a_manifest_inside_a_bucket_child_is_found_without_the_opt_in) {
    World w = world(FS_ABSENT, FS_ABSENT, false, false);
    w.bucketChildRecordsTarget = true;
    model_arm_session(&w, SHAPE_C);
    model_open_target(&w, SHAPE_C);
    model_finish_refusal(&w, SHAPE_C);
    ASSERT_FALSE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_FOREIGN_RECORD);
    ASSERT_FALSE(model_is_busy(&w));
}

/* ⭐ BOTH DIRECTIONS, AND THEY ARE EXCLUSIVE. A session inside a bucket looks
 * UP; one at the top of a bucket set looks DOWN. The pair matters because an
 * implementation doing only one passes half the arms above, and because doing
 * both unconditionally would double the scan for every session. The control is
 * the ordinary directory, which must pay for neither. */
TEST(the_scan_looks_up_from_a_bucket_and_down_from_its_parent) {
    World up = world(FS_ABSENT, FS_ABSENT, false, false);
    up.dirIsBucket = true;
    up.parentRecordsTarget = true;
    model_open_target(&up, SHAPE_C);
    ASSERT_EQ(up.refusal, REFUSE_FOREIGN_RECORD);

    World down = world(FS_ABSENT, FS_ABSENT, false, false);
    down.bucketChildRecordsTarget = true;
    model_open_target(&down, SHAPE_C);
    ASSERT_EQ(down.refusal, REFUSE_FOREIGN_RECORD);

    World plain = world(FS_ABSENT, FS_ABSENT, false, false);
    model_open_target(&plain, SHAPE_C);
    ASSERT_TRUE(plain.opened);
    ASSERT_EQ(plain.refusal, REFUSE_NONE);
}

/* ---- r25 audit HIGH (:2890): a skip that was a FALSE NEGATIVE ----------- */

/* ⛔⛔ THE DIRECTION MATTERS. r22-r24 removed six fail-CLOSED branches, where a
 * foreign name refused a session loudly. This is the opposite and is worse: the
 * collision is missed SILENTLY. Fixing a habit six times is what made me apply
 * it once where its premise did not hold -- fileCounter is NOT bounded by
 * bucket capacity, because the existing-target reopen path suppresses both the
 * bucket roll and the count that drives it. */
TEST(the_audited_r24_missed_a_collision_above_the_bitset_range) {
    World w = world(FS_ABSENT, FS_ABSENT, false, false);
    w.foreignOverLimitRecord = true;
    w.counterAboveLimit = true;
    model_arm_session(&w, SHAPE_R24_AUDITED);
    model_open_target(&w, SHAPE_R24_AUDITED);
    ASSERT_TRUE(w.opened);            /* skipped -- the collision went unseen */
    ASSERT_EQ(w.refusal, REFUSE_NONE);
}

TEST(an_index_above_the_bitset_range_routes_to_the_exact_check) {
    World w = world(FS_ABSENT, FS_ABSENT, false, false);
    w.foreignOverLimitRecord = true;
    w.counterAboveLimit = true;
    model_arm_session(&w, SHAPE_C);
    model_open_target(&w, SHAPE_C);
    model_finish_refusal(&w, SHAPE_C);
    ASSERT_FALSE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_FOREIGN_RECORD);
    ASSERT_FALSE(model_is_busy(&w));
}

/* ⭐ THE CONTROL, and it is the whole reason this is not just "always do the
 * exact check". An over-limit foreign record while OUR counter is in range
 * cannot collide -- different indices -- so it must still ARM, with no scan.
 * Without this row the fix above is satisfied by refusing every session that
 * shares a directory with a long-named foreign record. */
TEST(an_over_limit_record_still_arms_when_our_counter_is_in_range) {
    World w = world(FS_ABSENT, FS_ABSENT, false, false);
    w.foreignOverLimitRecord = true;   /* counterAboveLimit stays false */
    model_arm_session(&w, SHAPE_C);
    model_open_target(&w, SHAPE_C);
    model_finish_refusal(&w, SHAPE_C);
    ASSERT_TRUE(w.opened);
    ASSERT_EQ(w.refusal, REFUSE_NONE);
    ASSERT_TRUE(model_is_busy(&w));

    /* and the converse: our counter out of range with no such foreign record */
    World b = world(FS_ABSENT, FS_ABSENT, false, false);
    b.counterAboveLimit = true;
    model_open_target(&b, SHAPE_C);
    ASSERT_TRUE(b.opened);
    ASSERT_EQ(b.refusal, REFUSE_NONE);
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
    RUN(r19_gate_and_creation_disagreed_on_a_subdirectory_target);
    RUN(r19_honoured_an_opt_in_on_a_recorded_sets_part);
    RUN(c_refuses_an_opt_in_on_a_recorded_sets_part);
    RUN(r19_read_an_inspector_fault_as_permission);
    RUN(c_refuses_when_the_inspector_cannot_tell);
    RUN(a_directory_change_withdraws_the_authorisation);
    RUN(the_authorisation_is_consumed_so_a_second_session_is_refused);
    RUN(r19_never_consumed_so_a_second_session_silently_overwrote);
    RUN(a_refused_session_still_consumes_the_authorisation);
    RUN(r20_honoured_a_grant_after_the_resolved_path_moved);
    RUN(r21_refuses_a_grant_whose_resolved_path_moved);
    RUN(r21_refuses_a_grant_that_recorded_no_subject);
    RUN(r21_honours_a_grant_for_the_path_it_was_given_for);
    RUN(r20_truncated_a_file_another_manifest_records);
    RUN(r21_refuses_a_target_another_manifest_records);
    RUN(r21_refuses_a_recorded_name_even_with_no_opt_in);
    RUN(r20_created_a_recorded_name_and_invalidated_the_record);
    RUN(a_foreign_manifest_recording_nothing_of_ours_does_not_refuse);
    RUN(r21_refuses_when_a_manifest_cannot_be_parsed);
    RUN(skipping_an_unparseable_line_would_have_let_it_through);
    RUN(every_r21_refusal_reason_also_disarms_the_session);
    RUN(the_audited_r21_refused_a_session_after_seventeen_short_sessions);
    RUN(a_manifest_count_never_refuses_a_session);
    RUN(the_audited_r21_faulted_on_a_data_file_named_mfst);
    RUN(a_non_manifest_file_named_mfst_is_skipped_not_faulted);
    RUN(a_damaged_real_manifest_still_refuses);
    RUN(the_audited_r22_faulted_on_a_foreign_record_with_a_large_index);
    RUN(a_foreign_record_with_an_index_past_our_limit_still_arms);
    RUN(the_audited_r22_recreated_a_part_recorded_by_the_parents_manifest);
    RUN(a_bucket_directory_scans_its_parent_even_without_the_opt_in);
    RUN(a_bucket_directory_whose_parent_records_nothing_still_logs);
    RUN(the_audited_r23_faulted_on_a_foreign_record_whose_index_overflows);
    RUN(a_foreign_record_whose_index_overflows_still_arms);
    RUN(the_audited_r23_missed_a_manifest_inside_a_bucket_child);
    RUN(a_manifest_inside_a_bucket_child_is_found_without_the_opt_in);
    RUN(the_scan_looks_up_from_a_bucket_and_down_from_its_parent);
    RUN(the_audited_r24_missed_a_collision_above_the_bitset_range);
    RUN(an_index_above_the_bitset_range_routes_to_the_exact_check);
    RUN(an_over_limit_record_still_arms_when_our_counter_is_in_range);
    return TEST_SUMMARY();
}
