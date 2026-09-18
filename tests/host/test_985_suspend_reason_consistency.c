/* ==========================================================================
 * test_985_suspend_reason_consistency.c -- issue #985
 *
 * WHAT IS UNDER TEST
 *
 * SD_SuspendReasonText() (SCPIStorageSD.c) names WHY the SD stack is
 * suspended. Its string reaches every #589 refusal -- SD:GET, SD:LISt?,
 * SD:CRC?, SD:DELete, SD:FORmat, SD:SPACe? and SD:BENCHmark -- through
 * SYST:LOG?, so a wrong name sends the operator after the wrong remedy.
 *
 * Until #985 it decided WHETHER it was suspended from one pair of reads and
 * WHICH CAUSE to name from a second, later pair (SCPIStorageSD.c, pre-fix):
 *
 *     if (!app_SDCard_SpiOwnedByWifi() && !SpiBusHealth_IsSdSuspended()) {
 *         return NULL;                                   // pair 1
 *     }
 *     if (SpiBusHealth_IsSdQuarantined())            { ... }  // pair 2
 *     if (wifi_manager_IsWifiFirmwareUpdateActive()) { ... }
 *     return "WiFi streaming owns SPI4 - SYST:STR:STOP first";
 *
 * app_SDCard_SpiOwnedByWifi() (app_freertos.c) is itself
 * `wifiStream || fwUpdate || quarantined`. So pair 1 composes THREE flags,
 * pair 2 re-reads TWO of those three, and the tail names the third without
 * reading it at all. The four are independent, written by unsynchronised
 * tasks, and change asynchronously.
 *
 * The defect: a cause that ENDS between the two pairs is reported as a
 * different cause that may never have been true at any single instant. A WiFi
 * firmware update opens the gate, completes before its own re-read, and the
 * function returns "WiFi streaming owns SPI4 - SYST:STR:STOP first" -- an
 * instruction to stop a stream that is not running. And a snapshot in which
 * only the published suspension is set (its owner cleared, the SD task not
 * yet round to republishing) fell into the same tail: a WiFi-streaming
 * diagnosis with nothing whatsoever observed about WiFi streaming.
 *
 * The fix takes ONE flat snapshot and branches on locals only:
 *
 *     const bool quarantined = SpiBusHealth_IsSdQuarantined();
 *     const bool fwUpdate    = wifi_manager_IsWifiFirmwareUpdateActive();
 *     const bool wifiStream  = app_SDCard_WifiStreamActive();   // #985, new
 *     const bool suspended   = SpiBusHealth_IsSdSuspended();
 *     if (quarantined) ... if (fwUpdate) ... if (wifiStream) ...
 *     if (suspended)  -> the new fourth message
 *     return NULL;
 *
 * app_SDCard_WifiStreamActive() is #985's other half: the composite's
 * WiFi-streaming term, exposed on its own so this function can sample it once
 * instead of calling the composite AND re-reading its parts, which is the bug
 * restated rather than fixed.
 *
 * NOT under test: staleness. Every one of the four can change the instant
 * after it is read, and the fixed function is still a snapshot -- the ticket
 * puts that explicitly out of scope, and no critical section is added or
 * asserted here. What is under test is that the verdict is assembled from ONE
 * read of each flag, so it can be out of date but never self-contradictory.
 *
 * HOW IT IS TESTED
 *
 * SCPIStorageSD.c is not host-compilable -- libscpi, FreeRTOS and the whole
 * board graph -- so this follows test_953_bench_suspend_diagnosis.c and
 * test_943_bench_stall_bound.c: both DECISION SHAPES are re-implemented here
 * over an injected environment, and their verdicts compared on identical
 * inputs.
 *
 * The environment is the part that matters. Each of the four flags is a
 * TIMELINE, not a value: a sequence of what successive reads observe, with
 * the last value repeating. That is what lets a scenario express "the FW
 * update was over by the second read of it", which is the whole defect and
 * which a single-bool environment cannot express at all. The old shape reads
 * some flags twice and walks the timeline; the new shape reads each once. A
 * flat-bool test would have both shapes agreeing everywhere except the fourth
 * quadrant and would MISS the mislabel entirely.
 *
 * The new model is deliberately split in two: new_sample() takes the four
 * reads, new_decide() is a pure function of the resulting struct. The bug is
 * unreachable through new_decide() by construction -- which is the point, and
 * is why the interesting assertions are about what the OLD shape does to a
 * moving timeline, not about talking the new one into misbehaving.
 *
 * CASES
 *
 *   #  timeline                                   OLD           NEW
 *   -----------------------------------------------------------------------
 *   1  fwUpdate true then false, nothing else     WIFI_STREAM   FW_UPDATE
 *   2  quarantine true then false, nothing else   WIFI_STREAM   QUARANTINE
 *   3  suspended only, no owner at all            WIFI_STREAM   NOT_RESUMED
 *   4  nothing set                                NULL          NULL
 *   5  each owner set and stable                  same          same
 *   6  all 16 stable combinations                 agree but for #3
 *
 * Rows 1-3 are the three ways the old tail fires without having observed WiFi
 * streaming. Row 6 is the headline: over stable input, exactly ONE of the
 * sixteen combinations moves, so #985 is surgical and the NULL boundary is
 * exactly the old one.
 *
 * FIDELITY -- what these models are and are not
 *
 * 1. The old model reproduces C's `||` SHORT-CIRCUIT, which is load-bearing:
 *    app_SDCard_SpiOwnedByWifi() stops at the first true, so an fwUpdate that
 *    opens the gate means the quarantine flag is NOT read at gate time, and a
 *    true gate means SpiBusHealth_IsSdSuspended() is not read at all. Read
 *    counts are asserted, so a model that read eagerly would fail its own
 *    tests rather than quietly diverge.
 * 2. The strings are COPIES of the firmware's literals, and this file cannot
 *    detect a reword or truncation in the real source -- the same gap
 *    test_953 records at length. Three mechanisms for closing it were tried
 *    and defeated in review; the guard has to measure the REAL string, which
 *    is #1001. What the Makefile guard for THIS target checks instead is the
 *    SHAPE the models copy: one read per flag, no composite call, and the
 *    branch order. That is what would silently invalidate these tests.
 * 3. The fourth message's wording is MINE -- authored with this fix, not
 *    inherited -- so pinning its exact text would pin nothing but my own
 *    copy of it. It is asserted to be non-NULL and distinct from all three
 *    inherited strings (pointer and content), which is the caller-visible
 *    property. The three inherited strings ARE pinned by identity, content
 *    and length, because #985 must not touch them: the
 *    daqifi-python-test-suite contract tests (test_589_sd_suspended_contract
 *    and the #955/#964 companions) match them by substring.
 * 4. The timeline is per-READ, not per-microsecond. "Cause A ends between the
 *    two pairs" is modelled as "the flag reads true once, then false", which
 *    is precisely how that race presents to this function and is all it can
 *    observe. No attempt is made to model WHY a flag moved.
 * 5. Only SD_SuspendReasonText()'s own decision is modelled. Its callers'
 *    outer gates (SCPIInterface.c and SD_RefuseIfSuspended test
 *    `app_SDCard_SpiOwnedByWifi() || SpiBusHealth_IsSdSuspended()` and THEN
 *    call this function) are a second, outer instance of the same
 *    read-twice shape. The ticket puts that out of scope; it is survivable
 *    because every call site already falls back on NULL (`why ? why : "..."`,
 *    all five sites), so a mismatched outer gate degrades to a generic
 *    sentence rather than a wrong one.
 * ========================================================================== */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "test_framework.h"

/* ==========================================================================
 * The reasons the function can return.
 * ========================================================================== */
typedef enum {
    REASON_NONE = 0,        /* NULL -- not suspended */
    REASON_QUARANTINE,
    REASON_FW_UPDATE,
    REASON_WIFI_STREAM,
    REASON_NOT_RESUMED      /* #985's new fourth arm */
} SuspendReason;

typedef struct {
    SuspendReason which;
    const char   *text;     /* the literal the arm returns; NULL for NONE */
} SuspendVerdict;

/* ==========================================================================
 * Fixtures.
 *
 * The first three are SD_SuspendReasonText()'s inherited literals, copied
 * verbatim (FIDELITY 2). The fourth is #985's, authored with the fix
 * (FIDELITY 3).
 * ========================================================================== */
static const char *const kReasonQuarantine =
    "SD quarantined after a bus jam - reseat the card, "
    "then SYST:STOR:SD:ENAble 1";
static const char *const kReasonFwUpdate =
    "a WiFi firmware update owns SPI4 - retry when it completes";
static const char *const kReasonWifiStream =
    "WiFi streaming owns SPI4 - SYST:STR:STOP first";
static const char *const kReasonNotResumed =
    "the SD task has not resumed yet - retry shortly";

/* ==========================================================================
 * Injected environment: four flags, each a TIMELINE of what successive reads
 * observe. seq[i] is the value returned by read i; the last entry repeats
 * forever, so a stable flag is a one-entry sequence.
 * ========================================================================== */
#define FLAG_MAX_READS 4

typedef struct {
    bool     seq[FLAG_MAX_READS];
    unsigned len;         /* entries in use; always >= 1 */
    unsigned reads;       /* how many times this flag has been read */
    unsigned trueReads;   /* how many of those returned true */
} Flag;

typedef struct {
    Flag quarantined;     /* SpiBusHealth_IsSdQuarantined()             */
    Flag fwUpdate;        /* wifi_manager_IsWifiFirmwareUpdateActive()  */
    Flag wifiStream;      /* app_SDCard_WifiStreamActive()  (#985, new) */
    Flag suspended;       /* SpiBusHealth_IsSdSuspended()               */
} Env;

static void flag_stable(Flag *f, bool value)
{
    f->seq[0]    = value;
    f->len       = 1;
    f->reads     = 0;
    f->trueReads = 0;
}

/* "true when first read, false from the second read on" -- a cause that ended
 * between the old shape's two pairs of reads. */
static void flag_true_then_false(Flag *f)
{
    f->seq[0]    = true;
    f->seq[1]    = false;
    f->len       = 2;
    f->reads     = 0;
    f->trueReads = 0;
}

static bool flag_read(Flag *f)
{
    unsigned idx = (f->reads < f->len) ? f->reads : (f->len - 1);
    bool v = f->seq[idx];
    f->reads++;
    if (v) {
        f->trueReads++;
    }
    return v;
}

static void env_stable(Env *e, bool quarantined, bool fwUpdate,
                       bool wifiStream, bool suspended)
{
    flag_stable(&e->quarantined, quarantined);
    flag_stable(&e->fwUpdate,    fwUpdate);
    flag_stable(&e->wifiStream,  wifiStream);
    flag_stable(&e->suspended,   suspended);
}

/* Did this call ever OBSERVE the cause it went on to name? That is the
 * property #985 is about: a verdict may be stale, but it must not name
 * something no read of this call returned true. */
static bool cause_was_observed(const Env *e, SuspendReason r)
{
    switch (r) {
    case REASON_QUARANTINE:  return e->quarantined.trueReads > 0;
    case REASON_FW_UPDATE:   return e->fwUpdate.trueReads > 0;
    case REASON_WIFI_STREAM: return e->wifiStream.trueReads > 0;
    case REASON_NOT_RESUMED: return e->suspended.trueReads > 0;
    case REASON_NONE:        return true;   /* names nothing */
    default:                 break;
    }
    return false;
}

/* ==========================================================================
 * PRE-#985: two pairs of reads.
 * ========================================================================== */

/* app_SDCard_SpiOwnedByWifi(), including the `||` short-circuit and the order
 * of its three terms (FIDELITY 1). */
static bool old_spi_owned_by_wifi(Env *e)
{
    return flag_read(&e->wifiStream)
        || flag_read(&e->fwUpdate)
        || flag_read(&e->quarantined);
}

static SuspendVerdict old_suspend_reason(Env *e)
{
    SuspendVerdict v;

    /* Pair 1: the composite, then -- only if it was false -- the published
     * flag. */
    if (!old_spi_owned_by_wifi(e) && !flag_read(&e->suspended)) {
        v.which = REASON_NONE;
        v.text  = NULL;
        return v;
    }
    /* Pair 2: two of the composite's three terms, read AGAIN. */
    if (flag_read(&e->quarantined)) {
        v.which = REASON_QUARANTINE;
        v.text  = kReasonQuarantine;
        return v;
    }
    if (flag_read(&e->fwUpdate)) {
        v.which = REASON_FW_UPDATE;
        v.text  = kReasonFwUpdate;
        return v;
    }
    /* The third term is never read here at all -- it is the default. */
    v.which = REASON_WIFI_STREAM;
    v.text  = kReasonWifiStream;
    return v;
}

/* ==========================================================================
 * POST-#985: one flat snapshot, then a decision that touches no flag.
 * ========================================================================== */
typedef struct {
    bool quarantined;
    bool fwUpdate;
    bool wifiStream;
    bool suspended;
} SuspendSnapshot;

static SuspendSnapshot new_sample(Env *e)
{
    SuspendSnapshot s;
    /* Unconditional and in source order -- no short-circuit, so every flag is
     * read exactly once on every path. */
    s.quarantined = flag_read(&e->quarantined);
    s.fwUpdate    = flag_read(&e->fwUpdate);
    s.wifiStream  = flag_read(&e->wifiStream);
    s.suspended   = flag_read(&e->suspended);
    return s;
}

static SuspendVerdict new_decide(const SuspendSnapshot *s)
{
    SuspendVerdict v;

    /* Quarantine first: it is the one that does NOT clear on its own. */
    if (s->quarantined) {
        v.which = REASON_QUARANTINE;
        v.text  = kReasonQuarantine;
    } else if (s->fwUpdate) {
        v.which = REASON_FW_UPDATE;
        v.text  = kReasonFwUpdate;
    } else if (s->wifiStream) {
        v.which = REASON_WIFI_STREAM;
        v.text  = kReasonWifiStream;
    } else if (s->suspended) {
        v.which = REASON_NOT_RESUMED;
        v.text  = kReasonNotResumed;
    } else {
        v.which = REASON_NONE;
        v.text  = NULL;
    }
    return v;
}

static SuspendVerdict new_suspend_reason(Env *e)
{
    SuspendSnapshot s = new_sample(e);
    return new_decide(&s);
}

/* Every flag read exactly once -- the flat-snapshot property itself. */
static void assert_new_sampled_each_flag_once(const Env *e)
{
    ASSERT_EQ(e->quarantined.reads, 1);
    ASSERT_EQ(e->fwUpdate.reads,    1);
    ASSERT_EQ(e->wifiStream.reads,  1);
    ASSERT_EQ(e->suspended.reads,   1);
}

/* Pin an inherited string by identity, content AND length. Identity alone
 * would pass a shape returning some other literal; strcmp alone would pass a
 * truncation that matched a prefix. The content checks sit behind a NULL
 * guard so a mutant returning NULL is REPORTED by the identity assertion
 * rather than segfaulting in strcmp and losing the suite's summary. */
static void assert_text_is(const char *actual, const char *expected)
{
    ASSERT_TRUE(actual == expected);
    if (actual != NULL) {
        ASSERT_TRUE(strcmp(actual, expected) == 0);
        ASSERT_EQ(strlen(actual), strlen(expected));
    }
}

/* ==========================================================================
 * Tests
 * ========================================================================== */

/* CASE 1 -- THE TICKET'S RACE. A WiFi firmware update is what opened the
 * gate, and it is over by the time the old shape re-reads it. Nothing else is
 * set: no quarantine, no streaming.
 *
 * OLD walks off the end of its cascade and returns the WiFi-streaming string
 * -- having read the WiFi-streaming flag once, at gate time, and seen it
 * FALSE. That is the mislabel: an instruction to stop a stream the same call
 * already observed was not running.
 *
 * NEW reads fwUpdate once, sees the true, and names it.
 *
 * Run with `suspended` both false and true, because the old shape's
 * short-circuit means it never reads that flag once the gate is open -- so it
 * cannot be what saves it, either way. */
TEST(fw_update_ending_between_the_reads_is_not_relabelled_as_streaming)
{
    int s;

    for (s = 0; s < 2; s++) {
        Env oldEnv, newEnv;
        SuspendVerdict oldV, newV;

        env_stable(&oldEnv, false, false, false, (s != 0));
        env_stable(&newEnv, false, false, false, (s != 0));
        flag_true_then_false(&oldEnv.fwUpdate);
        flag_true_then_false(&newEnv.fwUpdate);

        oldV = old_suspend_reason(&oldEnv);
        newV = new_suspend_reason(&newEnv);

        /* The defect, stated twice: the wrong cause, and the proof it is
         * wrong -- the flag it names was read and came back false. */
        ASSERT_EQ(oldV.which, REASON_WIFI_STREAM);
        assert_text_is(oldV.text, kReasonWifiStream);
        ASSERT_TRUE(oldEnv.wifiStream.reads > 0);
        ASSERT_EQ(oldEnv.wifiStream.trueReads, 0);
        ASSERT_FALSE(cause_was_observed(&oldEnv, oldV.which));

        /* The fix. */
        ASSERT_EQ(newV.which, REASON_FW_UPDATE);
        assert_text_is(newV.text, kReasonFwUpdate);
        ASSERT_TRUE(cause_was_observed(&newEnv, newV.which));
        assert_new_sampled_each_flag_once(&newEnv);

        /* And it is the FW-update remedy, not the streaming one. */
        ASSERT_TRUE(newV.text != kReasonWifiStream);
    }
}

/* CASE 2 -- the same race through the other re-read flag. A jam quarantine
 * opens the gate (third term of the composite, so all three are read) and is
 * cleared -- SYST:STOR:SD:ENAble 1, the documented escape hatch -- before the
 * old shape's re-read. Same mislabel, different cause.
 *
 * This is the ticket's "reverse" case in its useful form. Read literally --
 * "a quarantine that STARTS after its own read but before the gate" -- the
 * quarantine is never observed by this call at all, the gate is open because
 * the published suspension is set, and the verdict is the one CASE 3 covers.
 * Both readings land on the same defect: the tail names WiFi streaming
 * without ever having looked at it. */
TEST(quarantine_ending_between_the_reads_is_not_relabelled_either)
{
    Env oldEnv, newEnv;
    SuspendVerdict oldV, newV;

    env_stable(&oldEnv, false, false, false, false);
    env_stable(&newEnv, false, false, false, false);
    flag_true_then_false(&oldEnv.quarantined);
    flag_true_then_false(&newEnv.quarantined);

    oldV = old_suspend_reason(&oldEnv);
    newV = new_suspend_reason(&newEnv);

    ASSERT_EQ(oldV.which, REASON_WIFI_STREAM);
    ASSERT_FALSE(cause_was_observed(&oldEnv, oldV.which));
    /* It read the quarantine twice -- the second read is what lost it. */
    ASSERT_EQ(oldEnv.quarantined.reads, 2);
    ASSERT_EQ(oldEnv.quarantined.trueReads, 1);

    ASSERT_EQ(newV.which, REASON_QUARANTINE);
    assert_text_is(newV.text, kReasonQuarantine);
    ASSERT_TRUE(cause_was_observed(&newEnv, newV.which));
    assert_new_sampled_each_flag_once(&newEnv);
}

/* CASE 3 -- THE FOURTH QUADRANT. Suspended, and not one of the three owners
 * set, in an environment where nothing moves at all: the cause has cleared
 * and app_SDCardTask has not yet run to republish. Reachable without any race
 * -- that task publishes `state == SUSPENDED || app_SDCard_SpiOwnedByWifi()`
 * once per loop iteration, so the flag outlives its owner by up to an
 * iteration.
 *
 * OLD reports WiFi streaming on no evidence whatsoever: it read the streaming
 * flag once (false), then never read it again. NEW says the one true thing
 * available -- the task has not resumed -- and, because that string is mine
 * (FIDELITY 3), it is pinned only as non-NULL and distinct from the three
 * inherited strings, which are what the python contract tests match on. */
TEST(suspended_with_no_owner_gets_its_own_message)
{
    Env oldEnv, newEnv;
    SuspendVerdict oldV, newV;

    env_stable(&oldEnv, false, false, false, true);
    env_stable(&newEnv, false, false, false, true);

    oldV = old_suspend_reason(&oldEnv);
    newV = new_suspend_reason(&newEnv);

    ASSERT_EQ(oldV.which, REASON_WIFI_STREAM);
    assert_text_is(oldV.text, kReasonWifiStream);
    ASSERT_FALSE(cause_was_observed(&oldEnv, oldV.which));

    ASSERT_EQ(newV.which, REASON_NOT_RESUMED);
    ASSERT_TRUE(cause_was_observed(&newEnv, newV.which));
    assert_new_sampled_each_flag_once(&newEnv);

    /* Still a refusal reason, not a NULL -- the NULL boundary does not move
     * (see the sixteen-combination test below). */
    ASSERT_TRUE(newV.text != NULL);
    ASSERT_TRUE(oldV.text != NULL);

    /* Distinct from all three inherited strings, by pointer and by content,
     * so the fourth arm cannot be satisfied by re-using one of them. */
    ASSERT_TRUE(newV.text != kReasonWifiStream);
    ASSERT_TRUE(newV.text != kReasonFwUpdate);
    ASSERT_TRUE(newV.text != kReasonQuarantine);
    if (newV.text != NULL) {
        ASSERT_TRUE(strcmp(newV.text, kReasonWifiStream) != 0);
        ASSERT_TRUE(strcmp(newV.text, kReasonFwUpdate)   != 0);
        ASSERT_TRUE(strcmp(newV.text, kReasonQuarantine) != 0);
        ASSERT_TRUE(strlen(newV.text) > 0);
    }
}

/* CASE 4 -- regression. Nothing set: NULL, from both shapes, and the new one
 * still paid for exactly four reads. Together with CASE 3 this is what stops
 * the lazy mutation "always return the fourth string". */
TEST(nothing_set_still_returns_null)
{
    Env oldEnv, newEnv;
    SuspendVerdict oldV, newV;

    env_stable(&oldEnv, false, false, false, false);
    env_stable(&newEnv, false, false, false, false);

    oldV = old_suspend_reason(&oldEnv);
    newV = new_suspend_reason(&newEnv);

    ASSERT_EQ(oldV.which, REASON_NONE);
    ASSERT_EQ(newV.which, REASON_NONE);
    ASSERT_TRUE(oldV.text == NULL);
    ASSERT_TRUE(newV.text == NULL);
    assert_new_sampled_each_flag_once(&newEnv);
}

/* CASE 5 -- the three inherited strings, and the precedence between them.
 * Nothing moves in these environments, so both shapes must agree exactly;
 * this is the guard that #985 changed no wording and no ordering. The
 * daqifi-python-test-suite contract tests (test_589_sd_suspended_contract and
 * the #955/#964 companions) match these by substring, so a reword here is a
 * breaking change elsewhere. */
TEST(stable_owners_keep_their_exact_strings_and_precedence)
{
    static const struct {
        bool          quarantined;
        bool          fwUpdate;
        bool          wifiStream;
        SuspendReason expect;
    } rows[] = {
        /* one owner at a time */
        { true,  false, false, REASON_QUARANTINE  },
        { false, true,  false, REASON_FW_UPDATE   },
        { false, false, true,  REASON_WIFI_STREAM },
        /* precedence: quarantine outranks both, FW update outranks streaming */
        { true,  true,  true,  REASON_QUARANTINE  },
        { true,  false, true,  REASON_QUARANTINE  },
        { false, true,  true,  REASON_FW_UPDATE   },
    };
    size_t i;

    for (i = 0; i < sizeof(rows) / sizeof(rows[0]); i++) {
        const char *expect =
            (rows[i].expect == REASON_QUARANTINE) ? kReasonQuarantine :
            (rows[i].expect == REASON_FW_UPDATE)  ? kReasonFwUpdate :
                                                    kReasonWifiStream;
        Env oldEnv, newEnv;
        SuspendVerdict oldV, newV;

        /* suspended true: the realistic companion state, and it must not
         * change which owner is named. */
        env_stable(&oldEnv, rows[i].quarantined, rows[i].fwUpdate,
                   rows[i].wifiStream, true);
        env_stable(&newEnv, rows[i].quarantined, rows[i].fwUpdate,
                   rows[i].wifiStream, true);

        oldV = old_suspend_reason(&oldEnv);
        newV = new_suspend_reason(&newEnv);

        ASSERT_EQ(newV.which, rows[i].expect);
        ASSERT_EQ(oldV.which, newV.which);
        assert_text_is(newV.text, expect);
        assert_text_is(oldV.text, expect);
        ASSERT_TRUE(cause_was_observed(&newEnv, newV.which));
        assert_new_sampled_each_flag_once(&newEnv);
    }
}

/* CASE 6 -- the headline. Over all sixteen STABLE combinations (nothing
 * changes between reads, so the old shape's two pairs cannot disagree and it
 * is at its most defensible), exactly ONE verdict moves: the fourth quadrant.
 *
 * Two properties fall out of that, and both matter to callers:
 *   - the NULL boundary is exactly the old one, so no call site sees a reason
 *     where it used to see NULL or the reverse (all five sites already write
 *     `why ? why : "..."` regardless);
 *   - every other combination returns exactly what it returned before, so the
 *     three inherited strings reach the python contract tests unchanged.
 *
 * And, for the new shape, over every one of the sixteen: four reads, and a
 * verdict naming something this call observed. */
TEST(exactly_one_stable_combination_moves_and_null_boundary_is_unchanged)
{
    unsigned mask;
    unsigned moved = 0;

    for (mask = 0; mask < 16u; mask++) {
        bool quarantined = (mask & 1u) != 0;
        bool fwUpdate    = (mask & 2u) != 0;
        bool wifiStream  = (mask & 4u) != 0;
        bool suspended   = (mask & 8u) != 0;
        Env oldEnv, newEnv;
        SuspendVerdict oldV, newV;

        env_stable(&oldEnv, quarantined, fwUpdate, wifiStream, suspended);
        env_stable(&newEnv, quarantined, fwUpdate, wifiStream, suspended);

        oldV = old_suspend_reason(&oldEnv);
        newV = new_suspend_reason(&newEnv);

        /* NULL exactly when nothing at all is set -- both shapes, all 16. */
        ASSERT_EQ(oldV.text == NULL, mask == 0);
        ASSERT_EQ(newV.text == NULL, mask == 0);

        if (oldV.which != newV.which) {
            moved++;
            /* The only combination allowed to move, named rather than
             * counted, so a different one moving cannot pass by arithmetic. */
            ASSERT_TRUE(!quarantined && !fwUpdate && !wifiStream && suspended);
            ASSERT_EQ(oldV.which, REASON_WIFI_STREAM);
            ASSERT_EQ(newV.which, REASON_NOT_RESUMED);
        } else {
            ASSERT_TRUE(oldV.text == newV.text);
        }

        ASSERT_TRUE(cause_was_observed(&newEnv, newV.which));
        assert_new_sampled_each_flag_once(&newEnv);
    }

    ASSERT_EQ(moved, 1);
}

/* The property #985 is actually about, asserted over the MOVING timelines
 * rather than the stable ones: when a cause ends mid-call, the old shape can
 * name a cause it never observed, and the new one never can.
 *
 * The third row is the one that keeps this honest. With BOTH re-read flags
 * moving, the old shape does NOT violate: the gate opens on the FW update's
 * first read, and the quarantine's first read then happens inside pair 2 and
 * still returns true, so it names a cause it did observe. The old shape is
 * not wrong in every race -- only in the ones where its cascade runs out of
 * arms and falls into the unread default. Expecting a violation on all three
 * would have been a test of a strawman; the per-row expectation is what makes
 * "observed" a real property rather than a synonym for "old".
 *
 * The counts are asserted exactly, so a timeline that stopped reproducing the
 * defect fails here instead of quietly turning this into a test of nothing. */
TEST(only_the_old_shape_can_name_an_unobserved_cause)
{
    static const struct {
        bool movingFwUpdate;
        bool movingQuarantine;
        bool expectOldViolates;
    } timelines[] = {
        /* gate opens on fwUpdate, gone by pair 2 -> unread default */
        { true,  false, true  },
        /* gate opens on quarantine, gone by pair 2 -> unread default */
        { false, true,  true  },
        /* gate opens on fwUpdate; quarantine's FIRST read lands in pair 2 and
         * is still true, so the cause named was observed after all */
        { true,  true,  false },
    };
    size_t i;
    unsigned oldViolations = 0;
    unsigned newViolations = 0;

    for (i = 0; i < sizeof(timelines) / sizeof(timelines[0]); i++) {
        Env oldEnv, newEnv;
        SuspendVerdict oldV, newV;
        bool oldViolated;

        env_stable(&oldEnv, false, false, false, false);
        env_stable(&newEnv, false, false, false, false);
        if (timelines[i].movingFwUpdate) {
            flag_true_then_false(&oldEnv.fwUpdate);
            flag_true_then_false(&newEnv.fwUpdate);
        }
        if (timelines[i].movingQuarantine) {
            flag_true_then_false(&oldEnv.quarantined);
            flag_true_then_false(&newEnv.quarantined);
        }

        oldV = old_suspend_reason(&oldEnv);
        newV = new_suspend_reason(&newEnv);

        oldViolated = !cause_was_observed(&oldEnv, oldV.which);
        ASSERT_EQ(oldViolated, timelines[i].expectOldViolates);
        if (oldViolated) {
            oldViolations++;
            /* Always the same failure: the unread default. */
            ASSERT_EQ(oldV.which, REASON_WIFI_STREAM);
        }
        if (!cause_was_observed(&newEnv, newV.which)) {
            newViolations++;
        }
        assert_new_sampled_each_flag_once(&newEnv);
    }

    ASSERT_EQ(oldViolations, 2);
    ASSERT_EQ(newViolations, 0);
}

int main(void)
{
    printf("#985 -- SD_SuspendReasonText single-snapshot verdict\n");
    printf("---------------------------------------------\n");
    RUN(fw_update_ending_between_the_reads_is_not_relabelled_as_streaming);
    RUN(quarantine_ending_between_the_reads_is_not_relabelled_either);
    RUN(suspended_with_no_owner_gets_its_own_message);
    RUN(nothing_set_still_returns_null);
    RUN(stable_owners_keep_their_exact_strings_and_precedence);
    RUN(exactly_one_stable_combination_moves_and_null_boundary_is_unchanged);
    RUN(only_the_old_shape_can_name_an_unobserved_cause);
    return TEST_SUMMARY();
}
