/* ==========================================================================
 * test_1153_nvm_recursive_lock.c -- PR firmware#1153: one recursive mutex
 * for the NVM API, closing the stale-load race in daqifi_settings.c.
 *
 * #1153 round 2: added Pin 4 below. Round 1's lock lived inside nvm.c's
 * write/erase entry points only, which protects the raw flash access but
 * NOT daqifi_settings_SaveToNvm()'s own erase -> stage-into-shared-buffer ->
 * write span -- so a DIRECT caller of SaveToNvm() (SCPILAN.c's two WiFi
 * savers, daqifi_settings_SaveADCCalSettings()) racing a SECOND direct
 * caller could still lose a record to the #1153 mechanism even with round
 * 1's fix in place. Pin 1/2 below are unchanged and still model
 * daqifi_settings_Update()'s own lock, which Pin 4 does not duplicate.
 *
 * WHAT IS UNDER TEST
 *
 * #1153 reports a LOST FIELD, not a corrupted record. Every TopLevelSettings
 * saver does:
 *
 *     daqifi_settings_LoadFromNvm(type, &settings)   // snapshot the record
 *     settings.<one field> = newValue                // modify ONE field
 *     daqifi_settings_SaveToNvm(&settings)            // write the WHOLE record
 *
 * with no synchronization. If a SECOND saver's load (on a different
 * transport, or a different SCPI command) lands anywhere between the first
 * saver's load and its save, the second saver's own save -- writing back ITS
 * stale snapshot -- silently reverts the field the first saver just
 * persisted. The fix is daqifi_settings_Update(): hold ONE recursive mutex
 * (owned by nvm.c) across the WHOLE load-modify-save span, so two such
 * sequences can never interleave.
 *
 * HOW IT IS TESTED
 *
 * Neither nvm.c nor daqifi_settings.c is host-includable (Harmony's
 * configuration.h/definitions.h, FreeRTOS, the PIC32 SFR headers, the WINC
 * driver headers), so -- same technique as test_908_cal_source_seqlock.c and
 * test_1152_usecal_type_trust.c -- the load-modify-save-under-a-lock shape is
 * re-implemented against an in-memory backing store and a trivial mutex
 * stub, and the REAL sources are pinned separately, by grep/sed guards in
 * the Makefile recipe, to still contain the shape the model assumes.
 *
 * ⚠ SCOPE, stated plainly per the PR brief: a single-threaded host model
 * cannot demonstrate, and this file does NOT claim to demonstrate, mutual
 * exclusion between two REAL preempting FreeRTOS tasks. What it shows:
 *
 *   1. (the reported symptom) A save that does NOT bracket its
 *      load-modify-save span lets a write injected between the load and
 *      the save get silently overwritten by the stale snapshot's save --
 *      the exact mechanism #1153 describes. A save that DOES bracket the
 *      span (by construction, in this model) cannot have anything injected
 *      inside it, because nothing else in a single-threaded model runs
 *      while the span is "open" -- which is exactly the property a REAL
 *      mutex provides against a second TASK, and exactly the property
 *      this test cannot independently verify across two real tasks.
 *   2. (the span itself) The event log recorded by the locked model always
 *      shows LOAD and SAVE strictly between a LOCK and its matching
 *      UNLOCK. The Makefile guards confirm the REAL daqifi_settings_Update,
 *      the REAL nvm_Lock/nvm_Unlock call sites in nvm.c's write/erase
 *      entry points, and the REAL 5 call sites this PR converts, still
 *      have that same shape -- so a future edit that narrows the lock span
 *      (e.g. unlocking before the save) fails the guard even though this
 *      file's own model still passes.
 * ========================================================================== */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "test_framework.h"

/* ---- the in-memory "NVM" backing store -------------------------------- */

typedef struct {
    int32_t calVals;          /* stands in for TopLevelSettings.calVals */
    bool    autoPowerOnUsb;   /* stands in for TopLevelSettings.autoPowerOnUsb */
} Store;

static Store g_store;

/* ---- the event log, shared by every model call in a test ------------- */

typedef enum { EV_LOCK, EV_LOAD, EV_SAVE, EV_UNLOCK, EV_INJECT } EventKind;

#define MAX_EVENTS 32
static EventKind g_events[MAX_EVENTS];
static int        g_eventCount;

static void log_reset(void) { g_eventCount = 0; }

static void log_event(EventKind k) {
    if (g_eventCount < MAX_EVENTS) {
        g_events[g_eventCount++] = k;
    }
}

/* ---- the mutex stub ----------------------------------------------------
 * A plain recursion counter is enough for a single-threaded model: there is
 * no second task to block, so "exclusion" here means only "every lock this
 * file issues is matched by exactly one unlock, and nothing this model runs
 * claims to have happened while depth was nonzero without going through
 * lock/unlock itself" -- see the file banner's SCOPE note. */
static int g_lockDepth;

static void stub_lock(void)   { g_lockDepth++; log_event(EV_LOCK); }
static void stub_unlock(void) { log_event(EV_UNLOCK); g_lockDepth--; }

/* ---- the model under test ----------------------------------------------
 * modify() receives the loaded snapshot and the injected context; it
 * returns the snapshot with exactly one field changed, mirroring a single
 * daqifi_settings_Update() callback. inject(), when non-NULL, runs between
 * the load and the modify -- standing in for "whatever a concurrent writer
 * would have landed in the backing store during this span, if it could." */
typedef void (*ModifyFn)(Store *snapshot, void *context);
typedef void (*InjectFn)(void);

static void model_update(bool useLock, ModifyFn modify, void *context,
                          InjectFn inject) {
    if (useLock) {
        stub_lock();
    }

    Store snapshot = g_store;              /* the "load" */
    log_event(EV_LOAD);

    if (inject != NULL) {
        inject();                          /* the "concurrent write" */
    }

    modify(&snapshot, context);

    g_store = snapshot;                    /* the "save" -- whole record */
    log_event(EV_SAVE);

    if (useLock) {
        stub_unlock();
    }
}

/* ---- modify callbacks used by the tests below -------------------------- */

static void set_calVals(Store *snapshot, void *context) {
    snapshot->calVals = *(int32_t *)context;
}

/* ---- the "concurrent writer": sets autoPowerOnUsb directly on the real
 * backing store, bypassing any snapshot -- this is literally what another
 * transport's own SaveToNvm does: write straight to the record. */
static void inject_autoPowerOnUsb_true(void) {
    log_event(EV_INJECT);
    g_store.autoPowerOnUsb = true;
}

/* ==========================================================================
 * Pin 1 (the reported symptom): an injected field surviving, or not,
 * depending on whether the span that raced it was locked.
 * ========================================================================== */

TEST(unlocked_update_loses_field_injected_mid_span) {
    g_store = (Store){ .calVals = 0, .autoPowerOnUsb = false };
    log_reset();

    int32_t newCalVals = 1;
    /* No lock: the injection lands AFTER this call's load but BEFORE its
     * save, exactly as #1153 describes -- a concurrent transport's write
     * reaching the record while this call's stale snapshot is still live. */
    model_update(/*useLock=*/false, set_calVals, &newCalVals,
                 inject_autoPowerOnUsb_true);

    /* The symptom: the injected field is GONE. This call's save overwrote
     * the whole record with its own (stale, pre-injection) snapshot. */
    ASSERT_EQ(g_store.calVals, 1);
    ASSERT_FALSE(g_store.autoPowerOnUsb);
}

TEST(locked_update_cannot_lose_a_field_injected_outside_its_span) {
    g_store = (Store){ .calVals = 0, .autoPowerOnUsb = false };
    log_reset();

    /* With the lock held across load-modify-save, nothing in a
     * single-threaded model can run "inside" the span at all -- there is no
     * second call stack to pause this one and inject from. That is
     * precisely the property the real recursive mutex buys against a
     * second REAL task (which this host model cannot independently
     * exercise -- see the file banner). Model that honestly: the injection
     * now happens where a blocked writer's own lock/load/save would really
     * land -- strictly AFTER this call's unlock, never between its load and
     * its save. */
    int32_t newCalVals = 1;
    model_update(/*useLock=*/true, set_calVals, &newCalVals, NULL);
    inject_autoPowerOnUsb_true();   /* the other transport, now unblocked */

    /* Both fields survive: this call's calVals, and the other transport's
     * autoPowerOnUsb, because the two were never able to overlap. */
    ASSERT_EQ(g_store.calVals, 1);
    ASSERT_TRUE(g_store.autoPowerOnUsb);
}

/* ==========================================================================
 * Pin 2 (the span itself): LOAD and SAVE both fall strictly inside one
 * LOCK/UNLOCK bracket. Run the UNLOCKED model first to show what "no lock
 * at all" looks like in the same event log shape -- that recorded sequence
 * is exactly what today's (pre-#1153) daqifi_settings_LoadFromNvm /
 * daqifi_settings_SaveToNvm call pairs produce, with no bracket at all.
 * ========================================================================== */

static bool events_equal(const EventKind *actual, int actualCount,
                          const EventKind *expected, int expectedCount) {
    if (actualCount != expectedCount) {
        return false;
    }
    return memcmp(actual, expected, (size_t)actualCount * sizeof(EventKind)) == 0;
}

TEST(unlocked_model_span_has_no_lock_bracket) {
    g_store = (Store){ .calVals = 0, .autoPowerOnUsb = false };
    log_reset();

    int32_t newCalVals = 7;
    model_update(/*useLock=*/false, set_calVals, &newCalVals, NULL);

    /* This IS the red-at-baseline shape: LOAD immediately followed by SAVE,
     * nothing bracketing either -- what every TopLevelSettings saver in
     * daqifi_settings.c / SCPIInterface.c / SCPIADC.c did before #1153. */
    static const EventKind expected[] = { EV_LOAD, EV_SAVE };
    ASSERT_TRUE(events_equal(g_events, g_eventCount, expected,
                              (int)(sizeof(expected) / sizeof(expected[0]))));
}

TEST(locked_model_span_brackets_load_and_save) {
    g_store = (Store){ .calVals = 0, .autoPowerOnUsb = false };
    log_reset();

    int32_t newCalVals = 7;
    model_update(/*useLock=*/true, set_calVals, &newCalVals, NULL);

    static const EventKind expected[] = { EV_LOCK, EV_LOAD, EV_SAVE, EV_UNLOCK };
    ASSERT_TRUE(events_equal(g_events, g_eventCount, expected,
                              (int)(sizeof(expected) / sizeof(expected[0]))));

    /* The mutex is balanced: every lock in this model is given back. */
    ASSERT_EQ(g_lockDepth, 0);
}

/* ==========================================================================
 * Pin 3 (the deadlock this PR must avoid): the mutex MUST be reentrant for
 * the SAME logical caller, because daqifi_settings_SaveToNvm() calls
 * daqifi_settings_ClearNvm() itself -- a non-recursive lock taken at both
 * levels would self-deadlock on every save, boot's included. Model that as
 * nested model_update-style lock/unlock pairs from the SAME call stack and
 * assert the depth counter tolerates the nesting instead of stuck above 0.
 * ========================================================================== */

TEST(stub_mutex_tolerates_recursive_nesting) {
    g_lockDepth = 0;
    log_reset();

    /* Outer: daqifi_settings_Update()'s own lock.
     * Inner: SaveToNvm() -> ClearNvm() -> nvm_ErasePage()'s lock, taken
     * while the outer is still held -- exactly the nesting #1153 requires
     * to be legal. */
    stub_lock();
    ASSERT_EQ(g_lockDepth, 1);
    stub_lock();
    ASSERT_EQ(g_lockDepth, 2);
    stub_unlock();
    ASSERT_EQ(g_lockDepth, 1);
    stub_unlock();
    ASSERT_EQ(g_lockDepth, 0);
}

/* ==========================================================================
 * Pin 4 (#1153 round 2): the DIRECT-CALLER race the round-2 audit
 * found, distinct from Pin 1/2 above.
 *
 * Pin 1/2 model daqifi_settings_Update()'s lock -- the span two
 * Update()-routed callers race across. But daqifi_settings_SaveToNvm() also
 * has DIRECT callers that never go through Update() at all: SCPILAN.c's
 * SCPI_LANSettingsApply() and SCPI_LANSettingsSave() (both WiFi savers), and
 * daqifi_settings_SaveADCCalSettings(). Round 1 put the lock inside nvm.c's
 * write/erase entry points (nvm_ErasePage, nvm_WriteRowtoAddr) -- which is
 * AFTER SaveToNvm() has already erased the page and staged the record into
 * the SHARED gTempFflashBuffer. So two direct callers racing EACH OTHER
 * (neither going through Update()'s lock) can have caller A's staged bytes
 * overwritten by caller B's stage before A's own write reads the buffer
 * back out -- A's write (and its readback verify, which compares against
 * that SAME buffer) then persists B's bytes to A's address, and A's own
 * write reports SUCCESS over it.
 * ========================================================================== */

typedef struct {
    char data[16];
} Record;

/* One shared stage buffer (stands in for gTempFflashBuffer) and two
 * independent "flash addresses" (stands in for two different NVM pages,
 * e.g. WIFI_SETTINGS_ADDR and TOP_LEVEL_SETTINGS_ADDR). */
static Record g_stage;

/* Models daqifi_settings_SaveToNvm()'s shape: erase (modeled as a no-op --
 * the hazard below is the shared BUFFER, not the page) -> stage into the
 * SHARED buffer -> write (copy the shared buffer to the caller's OWN
 * address, "readback-verify" against that SAME shared buffer, exactly as
 * nvm_WriteRowtoAddr() does at nvm.c). `useLock` models whether SaveToNvm()
 * itself brackets this whole span (the round-2 fix) or not (the round-1-only
 * shape, where the shared buffer is unprotected between the stage and the
 * write). */
static bool model_save_to_nvm(const Record *rec, Record *flashAddr,
                               bool useLock, InjectFn inject) {
    if (useLock) {
        stub_lock();
    }

    g_stage = *rec;                 /* the "stage": SaveToNvm's memcpy into
                                      * gTempFflashBuffer */
    log_event(EV_LOAD);             /* reuse EV_LOAD as "staged" for the log */

    if (inject != NULL) {
        inject();                   /* another direct caller's own
                                      * stage+write landing in the gap */
    }

    *flashAddr = g_stage;           /* the "write": nvm_WriteRowtoAddr */
    log_event(EV_SAVE);
    bool verifyOk = (memcmp(flashAddr, &g_stage, sizeof(Record)) == 0);

    if (useLock) {
        stub_unlock();
    }
    return verifyOk;
}

static Record g_recB_for_inject;
static Record *g_flashB_for_inject;

static void inject_callerB_full_save(void) {
    /* Caller B runs its OWN full save (unlocked -- it is the thing racing
     * A, exactly as A is unlocked) while A's buffer is staged but not yet
     * written back out: this is literally what a second direct SaveToNvm()
     * caller's own erase+stage does, from A's point of view. */
    model_save_to_nvm(&g_recB_for_inject, g_flashB_for_inject,
                       /*useLock=*/false, NULL);
}

TEST(unlocked_direct_caller_save_can_be_overwritten_by_another_direct_caller) {
    Record recA = { "AAAA" };
    Record recB = { "BBBB" };
    Record flashA, flashB;
    memset(&flashA, 0, sizeof(flashA));
    memset(&flashB, 0, sizeof(flashB));
    g_recB_for_inject = recB;
    g_flashB_for_inject = &flashB;
    log_reset();

    /* Round-1-only shape: SaveToNvm() itself takes no lock of its own, so
     * the window between its stage and its write is open to a second
     * direct caller's full save. */
    bool resultA = model_save_to_nvm(&recA, &flashA, /*useLock=*/false,
                                      inject_callerB_full_save);

    /* The round-2 defect, reproduced: A's write reports SUCCESS -- its own
     * readback verify passed, because it compared the buffer against
     * itself, which B had already overwritten -- but A's OWN address now
     * holds B's bytes, not A's. The row write did NOT write caller A's own
     * bytes to caller A's own address. */
    ASSERT_TRUE(resultA);
    ASSERT_FALSE(memcmp(&flashA, &recA, sizeof(Record)) == 0);
    ASSERT_TRUE(memcmp(&flashA, &recB, sizeof(Record)) == 0);
}

TEST(locked_direct_caller_save_cannot_be_overwritten) {
    Record recA = { "AAAA" };
    Record recB = { "BBBB" };
    Record flashA, flashB;
    memset(&flashA, 0, sizeof(flashA));
    memset(&flashB, 0, sizeof(flashB));
    log_reset();

    /* round-2 fix: SaveToNvm() brackets the whole span itself, so -- same
     * reasoning as Pin 2 above -- nothing in a single-threaded model can
     * run "inside" it. Model that honestly: caller B's own save happens
     * strictly AFTER caller A's unlock, never between A's stage and A's
     * write. */
    bool resultA = model_save_to_nvm(&recA, &flashA, /*useLock=*/true, NULL);
    bool resultB = model_save_to_nvm(&recB, &flashB, /*useLock=*/true, NULL);

    /* Both callers' own bytes land at their own addresses -- neither write
     * ever sees the other's staged record. */
    ASSERT_TRUE(resultA);
    ASSERT_TRUE(resultB);
    ASSERT_TRUE(memcmp(&flashA, &recA, sizeof(Record)) == 0);
    ASSERT_TRUE(memcmp(&flashB, &recB, sizeof(Record)) == 0);
}

int main(void) {
    RUN(unlocked_update_loses_field_injected_mid_span);
    RUN(locked_update_cannot_lose_a_field_injected_outside_its_span);
    RUN(unlocked_model_span_has_no_lock_bracket);
    RUN(locked_model_span_brackets_load_and_save);
    RUN(stub_mutex_tolerates_recursive_nesting);
    RUN(unlocked_direct_caller_save_can_be_overwritten_by_another_direct_caller);
    RUN(locked_direct_caller_save_cannot_be_overwritten);
    return TEST_SUMMARY();
}
