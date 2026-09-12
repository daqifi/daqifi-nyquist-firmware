/* ==========================================================================
 * test_980_dac7718_error_paths.c -- issue #980
 *
 * WHAT IS UNDER TEST
 *
 * The DAC7718 driver's error paths, in two independent shapes fixed by #980:
 *
 * PART A -- DAC7718_ReadWriteReg / DAC7718_Unlock (DAC7718.c). Three
 * validation failures (`RW > 1U`, `Reg > DAC7718_MAX_REGISTER`, an invalid
 * config id) `goto cleanup` BEFORE DAC7718_Lock() is ever called. Pre-#980,
 * `DAC7718_Unlock()` gave the mutex unconditionally:
 *
 *     static void DAC7718_Unlock(tDAC7718Config* config, bool csAsserted) {
 *         if (csAsserted && config != NULL) GPIO_PinWrite(config->CS_Pin, true);
 *         if (gDAC7718_Mutex != NULL) xSemaphoreGive(gDAC7718_Mutex);
 *     }
 *
 * so a validation failure gave a mutex this call never took. #980 added a
 * local `lockHeld`, set true only after DAC7718_Lock() itself returns true,
 * and Unlock only gives when lockHeld is true.
 *
 * PART B -- DAC_EnsureHardwareInitialized (SCPIDAC.c). Pre-#980,
 * DAC7718_Init() returned void and was called unconditionally-successful:
 *
 *     DAC7718_Init(newInstanceId, 1);
 *     dacInstanceId = newInstanceId;
 *     dacHardwareInitialized = true;
 *
 * so a failed init (mutex-create failure, SPI timeout writing the config
 * register) was reported as a success, AND the flag was never re-checked
 * against power state after the first success. #980 makes DAC7718_Init()
 * return bool, only publishes `dacHardwareInitialized = true` on an actual
 * success, checks the power precondition on every call (clearing the flag,
 * but NOT the allocated slot id, when the rail drops), and adds a
 * test-and-set claim (`dacInitInProgress`) around the allocate/init/publish
 * region so a retry after a failure reuses the already-allocated slot instead
 * of calling DAC7718_NewConfig() again -- which, since MAX_DAC7718_CONFIG is
 * 1 and the counter never decrements, would return the table-full sentinel
 * forever and permanently brick the DAC.
 *
 * HOW IT IS TESTED
 *
 * Neither DAC7718.c nor SCPIDAC.c is a host-test candidate -- DAC7718.c pulls
 * in Harmony GPIO/SPI/coretimer PLIB headers and FreeRTOS semaphores;
 * SCPIDAC.c additionally pulls in libscpi and the whole board-config/BoardData
 * graph. So, like test_943_bench_stall_bound.c, this file re-implements the
 * two control-flow SHAPES against mocked primitives (a mock lock/unlock pair
 * for Part A; a mock one-slot allocator + mock Init + mock power/board state
 * for Part B), mirroring the real functions' variable names, branch order and
 * early-return points line-for-line. Each mock function increments a call
 * counter so a test can assert not just the return value but which real
 * primitive would have fired and how many times -- which is what actually
 * distinguishes "fixed" from "coincidentally still works".
 *
 * Both parts also carry an OLD_BUGGY reimplementation of the pre-#980 shape,
 * proven against the SAME mocks, so every headline assertion has a
 * "this fails on old firmware" companion the way test_943 contrasts its old
 * and new loop shapes.
 *
 * PART C -- SCPI_DACVoltageSet's all-channel branch (SCPIDAC.c). The DAC7718's
 * load latch is GLOBAL: one DAC7718_UpdateLatch() call commits EVERY
 * channel's shadow register to its physical output at once, not just the
 * channels a given command touched. The pass-2 shape (abort the write loop
 * on the first per-channel failure, before ever calling UpdateLatch) assumed
 * this made an aborted call a no-op -- its own comment claimed the abort
 * "leaves every channel's physical output exactly where it was". That claim
 * was WRONG: channels already written by the aborted loop are left holding
 * an UNCOMMITTED new value in their shadow registers, and the NEXT
 * UpdateLatch call from any UNRELATED command (a single-channel
 * SOUR:VOLT:LEV, CONF:DAC:UPDATE) drives that leftover value onto its
 * physical output -- while BoardData, never published by the aborted call,
 * still reports the OLD voltage. #980's pass-4 fix instead writes every
 * channel it can (skipping only the ones that fail), latches
 * UNCONDITIONALLY (which is what drains any uncommitted shadow state), and
 * publishes BoardData only for the channels that actually took the write.
 *
 * FIDELITY -- what this does NOT cover
 *
 * 1. No real FreeRTOS semaphore, no real critical section, no real
 *    concurrency -- dacInitInProgress's claim is exercised by manually
 *    setting/inspecting the flag around a single-threaded call, which proves
 *    the STATE MACHINE is race-safe in the sense that "already claimed" is
 *    checked before "already initialized" is re-checked and that every exit
 *    path clears a claim this call took, but does not prove the actual
 *    taskENTER_CRITICAL/EXIT pairing compiles to an atomic test-and-set on
 *    real hardware (that is a source-reading, not a host-testable, claim).
 * 2. The GPIO reset-pulse lock/unlock added inside DAC7718_Init() (serializing
 *    the RST pulse against a live DAC7718_ReadWriteReg SPI frame) is not
 *    modelled here -- it is a single, unconditionally-successful Lock/Unlock
 *    pair with no branch, so there is no shape to differentially test.
 * 3. Part C's mock models the SPI bus as binary (a write either loads the
 *    shadow register exactly or does nothing) -- a real timeout can also
 *    truncate a frame MID-transfer, leaving that channel's shadow
 *    indeterminate. Neither the real driver nor this mock can meaningfully
 *    model "indeterminate"; both treat a failed write as "shadow unchanged",
 *    which is exact for validation/lock failures (before CS is asserted) and
 *    optimistic for a mid-frame timeout. Not modelled: no lock is held
 *    across the all-channel loop, so a concurrent command's latch call could
 *    interleave mid-loop -- see the source comment's residual R2 for why
 *    that is deferred rather than fixed here.
 * 4. This is source-level validation only. #980's own Bench line is
 *    "NQ3 (absent from this bench)" -- see the PR body for the queued
 *    daqifi-python-test-suite companion test and the READY FOR BENCH note.
 *
 * PART D -- SCPI_DACVoltageGet's dacWriterPossible gate (PR #990's own
 * pre-merge adversarial audit, BLOCK verdict). PR #990 added a command-
 * serialization mutex (gDacCommandMutex) to both SCPI_DACVoltageSet and
 * SCPI_DACVoltageGet, but that mutex is created ONLY under
 * `BoardVariant == 3` (app_freertos.c). The getter as originally merged
 * called SCPIDAC_LockCommand() UNCONDITIONALLY, so on NQ1/NQ2 -- where the
 * mutex is never created -- the lock attempt always failed
 * ("mutex not created"), and EVERY bare SOUR:VOLT:LEV? answered "DAC command
 * busy, try again" where the pre-PR getter (no lock at all) always answered
 * SCPI_RES_OK. Every board on this bench is an NQ1. The fix gates the lock
 * attempt on `dacWriterPossible` (BoardVariant == 3 -- the same condition
 * the mutex's own creation, and the only two writers of
 * BOARDDATA_AOUT_LATEST, are gated on), and threads the resulting lockHeld
 * through to SCPIDAC_UnlockCommand() so a skipped lock is never given back.
 * ========================================================================== */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include "test_framework.h"

/* ==========================================================================
 * PART A -- DAC7718_ReadWriteReg / DAC7718_Unlock lock-ownership shape
 * ========================================================================== */

typedef struct {
    int lockCalls;
    int giveCalls;             /* stands in for xSemaphoreGive() */
    bool lastLockHeldAtUnlock; /* the value Unlock() was actually called with */
} RegMockEnv;

static void reg_mock_init(RegMockEnv *env)
{
    env->lockCalls = 0;
    env->giveCalls = 0;
    env->lastLockHeldAtUnlock = false;
}

/* Stands in for DAC7718_Lock(). */
static bool mock_lock(RegMockEnv *env, bool lockSucceeds)
{
    env->lockCalls++;
    return lockSucceeds;
}

/* #980 shape -- DAC7718_Unlock(config, csAsserted, lockHeld): gives ONLY when
 * lockHeld is true. csAsserted/GPIO are irrelevant to the ownership question
 * this test is about, so they are omitted from the mock entirely. */
static void new_unlock(RegMockEnv *env, bool lockHeld)
{
    env->lastLockHeldAtUnlock = lockHeld;
    if (lockHeld) {
        env->giveCalls++;
    }
}

/* Pre-#980 shape -- DAC7718_Unlock(config, csAsserted): gives
 * UNCONDITIONALLY. This is the bug: called from a cleanup reached before
 * Lock() ever ran, it still gives. */
static void old_buggy_unlock(RegMockEnv *env)
{
    env->giveCalls++;
}

/* Mirrors DAC7718_ReadWriteReg's shape line-for-line: three validations that
 * `goto cleanup` before Lock() is ever attempted, then Lock(), then an SPI
 * step (collapsed to one bool here -- see FIDELITY note 2 in the file
 * header), then cleanup. `useOldShape` selects which Unlock the cleanup path
 * calls, so both variants share every branch above the label. */
static uint32_t read_write_reg_shape(RegMockEnv *env, bool rwValid, bool regValid,
                                     bool configValid, bool lockSucceeds,
                                     bool spiSucceeds, bool useOldShape)
{
    uint32_t rdData = 0;
    bool lockHeld = false;   /* #980 item 2 */

    if (!rwValid)     { rdData = UINT32_MAX; goto cleanup; }
    if (!regValid)    { rdData = UINT32_MAX; goto cleanup; }
    if (!configValid) { rdData = UINT32_MAX; goto cleanup; }

    if (!mock_lock(env, lockSucceeds)) { rdData = UINT32_MAX; goto cleanup; }
    lockHeld = true;   /* set ONLY after a successful take (#980 item 2) */

    if (!spiSucceeds) { rdData = UINT32_MAX; goto cleanup; }

    rdData = 42U;   /* arbitrary "the SPI transfer succeeded" sentinel */

cleanup:
    if (useOldShape) {
        old_buggy_unlock(env);
    } else {
        new_unlock(env, lockHeld);
    }
    return rdData;
}

/* The headline: each of the three PRE-lock validation failures must reach
 * Lock() zero times and give ZERO times under the new shape -- and the old
 * shape gives every time despite never having taken anything. This is
 * literally the bug report: "a validation failure releases a mutex it never
 * acquired." */
TEST(prelock_validation_failure_never_gives_new_shape_gives_old_shape_does)
{
    struct { bool rw, reg, cfg; const char *name; } cases[] = {
        { false, true,  true,  "RW>1"          },
        { true,  false, true,  "Reg>MAX"       },
        { true,  true,  false, "invalid config"},
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        RegMockEnv newEnv, oldEnv;
        uint32_t newResult, oldResult;

        reg_mock_init(&newEnv);
        newResult = read_write_reg_shape(&newEnv, cases[i].rw, cases[i].reg,
                                         cases[i].cfg, true, true, false);

        reg_mock_init(&oldEnv);
        oldResult = read_write_reg_shape(&oldEnv, cases[i].rw, cases[i].reg,
                                         cases[i].cfg, true, true, true);

        /* Both shapes correctly refuse the call. */
        ASSERT_EQ(newResult, UINT32_MAX);
        ASSERT_EQ(oldResult, UINT32_MAX);

        /* Neither shape ever calls Lock() for a pre-lock validation failure
         * -- that is what "pre-lock" means. */
        ASSERT_EQ(newEnv.lockCalls, 0);
        ASSERT_EQ(oldEnv.lockCalls, 0);

        /* The defect, stated as a count: the new shape gives NOTHING for a
         * mutex it never took; the old shape gives EVERY time regardless. */
        ASSERT_EQ(newEnv.giveCalls, 0);
        ASSERT_FALSE(newEnv.lastLockHeldAtUnlock);
        ASSERT_EQ(oldEnv.giveCalls, 1);
    }
}

/* A failed Lock() itself (mutex busy / create failed) must not give either --
 * there is nothing to give back. Both shapes happen to agree here because
 * DAC7718_Lock() returning false was already `goto cleanup` in the pre-#980
 * code too; included for completeness of the state space. */
TEST(lock_failure_gives_nothing_in_either_shape)
{
    RegMockEnv newEnv, oldEnv;

    reg_mock_init(&newEnv);
    ASSERT_EQ(read_write_reg_shape(&newEnv, true, true, true, false, true, false),
              UINT32_MAX);
    ASSERT_EQ(newEnv.lockCalls, 1);
    ASSERT_EQ(newEnv.giveCalls, 0);
    ASSERT_FALSE(newEnv.lastLockHeldAtUnlock);

    reg_mock_init(&oldEnv);
    ASSERT_EQ(read_write_reg_shape(&oldEnv, true, true, true, false, true, true),
              UINT32_MAX);
    /* The old shape STILL gives -- it never tracked ownership at all. This is
     * a second manifestation of the same defect: a failed Lock() means the
     * mutex is held by someone ELSE (xSemaphoreTake timed out), and the old
     * code gives it away regardless. */
    ASSERT_EQ(oldEnv.giveCalls, 1);
}

/* A POST-lock failure (SPI timeout) must still give -- the call legitimately
 * owns the mutex at that point and must release it. Guards the mutation
 * "make lockHeld stay false always", which would pass the two tests above by
 * accident while leaking the mutex on every real SPI failure. */
TEST(postlock_failure_still_gives_the_mutex_it_took)
{
    RegMockEnv env;
    reg_mock_init(&env);

    ASSERT_EQ(read_write_reg_shape(&env, true, true, true, true, false, false),
              UINT32_MAX);
    ASSERT_EQ(env.lockCalls, 1);
    ASSERT_EQ(env.giveCalls, 1);
    ASSERT_TRUE(env.lastLockHeldAtUnlock);
}

/* Full success: exactly one Lock, one Give, correct data. */
TEST(success_path_locks_and_gives_exactly_once)
{
    RegMockEnv env;
    reg_mock_init(&env);

    ASSERT_EQ(read_write_reg_shape(&env, true, true, true, true, true, false), 42);
    ASSERT_EQ(env.lockCalls, 1);
    ASSERT_EQ(env.giveCalls, 1);
    ASSERT_TRUE(env.lastLockHeldAtUnlock);
}

/* ==========================================================================
 * PART B -- DAC_EnsureHardwareInitialized state machine
 * ========================================================================== */

typedef struct {
    bool hardwareInitialized;  /* dacHardwareInitialized */
    uint8_t instanceId;        /* dacInstanceId, 0xFF == none */
    bool initInProgress;       /* dacInitInProgress -- #980 items 1/4 */
    bool slotConsumed;         /* mirrors MAX_DAC7718_CONFIG==1's allocator:
                                 * the ONE slot, once handed out, never frees */
    int newConfigCalls;
    int initCalls;
} DacState;

static void dac_state_init(DacState *s)
{
    s->hardwareInitialized = false;
    s->instanceId = 0xFF;
    s->initInProgress = false;
    s->slotConsumed = false;
    s->newConfigCalls = 0;
    s->initCalls = 0;
}

/* Stands in for DAC7718_NewConfig(). Faithful to the real one-slot allocator:
 * the FIRST call ever (per DacState) succeeds and returns id 0; every call
 * after that -- including a naive retry that re-allocates instead of reusing
 * -- returns the 0xFF table-full sentinel, because m_DAC7718ConfigCount only
 * increments and MAX_DAC7718_CONFIG is 1. There is no "make allocation fail"
 * input; capacity is the only thing that governs it, exactly like the real
 * driver. */
static uint8_t mock_new_config(DacState *s)
{
    s->newConfigCalls++;
    if (s->slotConsumed) {
        return 0xFF;
    }
    s->slotConsumed = true;
    return 0;
}

/* Stands in for DAC7718_Init(id, 1). `succeeds` is this call's outcome --
 * e.g. an SPI timeout writing the config register on this attempt. */
static bool mock_init(DacState *s, bool succeeds)
{
    s->initCalls++;
    return succeeds;
}

/* #980 shape -- mirrors DAC_EnsureHardwareInitialized() from SCPIDAC.c:
 * power check (clearing the flag, not the slot, on a drop) ahead of the
 * "already initialized" short-circuit, then a claim around allocate+init+
 * publish, with the id retained across an init failure so a retry reuses the
 * same slot instead of calling NewConfig again. BoardData_Get/BoardConfig_Get
 * are collapsed into the boolean inputs; DAC7718_NewConfig/DAC7718_Init are
 * the mocks above.
 *
 * `powerStillUpAtPublish` models the #980-pass-3 fix for Qodo /agentic_review
 * bug "Power cycles leave the DAC marked ready": DAC7718_Init() can take
 * tens of ms, so the `powered` reading taken above is stale by the time
 * Init returns. A second, FRESH power read immediately before publishing
 * `hardwareInitialized = true` closes that window -- without it, a rail
 * drop occurring entirely inside the Init() call would still get published
 * as READY. `ensure_hardware_initialized()` below is the pre-existing 4-arg
 * call shape every other test in this file uses, forwarding
 * powerStillUpAtPublish=true so none of that coverage changes; only the
 * dedicated race test below exercises the false case. */
static bool ensure_hardware_initialized_ex(DacState *s, bool powered, bool boardOk,
                                           bool variantOk, bool initSucceeds,
                                           bool powerStillUpAtPublish)
{
    if (!powered) {
        s->hardwareInitialized = false;   /* #980 item 3 */
        return false;
    }

    if (s->hardwareInitialized) {
        return true;
    }

    if (!boardOk) {
        return false;
    }
    if (!variantOk) {
        return false;
    }

    /* Claim (test-and-set in the real code; single-threaded here, so a
     * concurrent holder is modelled by the caller pre-setting
     * s->initInProgress before calling -- see the concurrency tests below). */
    bool claimed;
    if (s->initInProgress) {
        claimed = false;
    } else {
        s->initInProgress = true;
        claimed = true;
    }

    if (!claimed) {
        return false;
    }

    /* Re-check under the claim (a racer may have finished first). */
    if (s->hardwareInitialized) {
        s->initInProgress = false;
        return true;
    }

    if (s->instanceId == 0xFF) {
        uint8_t newId = mock_new_config(s);
        if (newId == 0xFF) {
            s->initInProgress = false;
            return false;
        }
        s->instanceId = newId;
    }

    if (!mock_init(s, initSucceeds)) {
        /* #980 item 1: slot RETAINED (instanceId untouched), only the claim
         * is released, so the next call retries Init on this same id. */
        s->initInProgress = false;
        return false;
    }

    /* #980 pass 3: re-validate with a FRESH read immediately before
     * publishing -- closes the window Init()'s own duration opened. */
    if (!powerStillUpAtPublish) {
        s->hardwareInitialized = false;
        s->initInProgress = false;
        return false;
    }

    s->hardwareInitialized = true;
    s->initInProgress = false;
    return true;
}

static bool ensure_hardware_initialized(DacState *s, bool powered, bool boardOk,
                                        bool variantOk, bool initSucceeds)
{
    return ensure_hardware_initialized_ex(s, powered, boardOk, variantOk,
                                          initSucceeds, true);
}

/* Pre-#980 shape: DAC7718_Init()'s outcome is never consulted (it returned
 * void), so the flag is published unconditionally the instant a slot is
 * allocated. `initSucceeds` is accepted only so both shapes share a call
 * signature -- the old shape ignores it completely, which IS the bug. */
static bool old_buggy_ensure_hardware_initialized(DacState *s, bool powered,
                                                  bool boardOk, bool variantOk,
                                                  bool initSucceeds)
{
    if (s->hardwareInitialized) {
        return true;
    }
    if (!powered || !boardOk || !variantOk) {
        return false;
    }

    uint8_t newId = mock_new_config(s);
    if (newId == 0xFF) {
        if (s->hardwareInitialized) {
            return true;
        }
        return false;
    }

    (void)mock_init(s, initSucceeds);   /* return value ignored -- the bug */
    s->instanceId = newId;
    s->hardwareInitialized = true;      /* ALWAYS true */
    return true;
}

/* Item 1, headline: a failed Init must be reported as a failure, and a
 * SUBSEQUENT call must retry on the SAME slot rather than re-allocating. The
 * old shape fails both halves at once -- it reports success on the very call
 * that failed. */
TEST(failed_init_is_reported_and_retry_reuses_the_retained_slot)
{
    DacState s;
    dac_state_init(&s);

    /* Call 1: allocation succeeds, DAC7718_Init fails. */
    ASSERT_FALSE(ensure_hardware_initialized(&s, true, true, true, false));
    ASSERT_FALSE(s.hardwareInitialized);
    ASSERT_EQ(s.instanceId, 0);          /* slot retained, not reset to 0xFF */
    ASSERT_EQ(s.newConfigCalls, 1);
    ASSERT_EQ(s.initCalls, 1);
    ASSERT_FALSE(s.initInProgress);      /* claim released on the failure exit */

    /* Call 2 (retry): Init now succeeds. Must NOT call NewConfig again --
     * doing so would hit the table-full sentinel forever, per the ticket. */
    ASSERT_TRUE(ensure_hardware_initialized(&s, true, true, true, true));
    ASSERT_TRUE(s.hardwareInitialized);
    ASSERT_EQ(s.instanceId, 0);
    ASSERT_EQ(s.newConfigCalls, 1);       /* <-- the headline assertion */
    ASSERT_EQ(s.initCalls, 2);

    /* The OLD shape, presented with the identical first call, reports SUCCESS
     * despite Init failing -- the exact defect item 1 names. */
    DacState old;
    dac_state_init(&old);
    ASSERT_TRUE(old_buggy_ensure_hardware_initialized(&old, true, true, true, false));
    ASSERT_TRUE(old.hardwareInitialized);   /* wrong: Init actually failed */
}

/* A naive "fix" that resets instanceId to 0xFF on an Init failure (rather
 * than retaining it) reproduces the ticket's stated permanent-brick failure
 * mode against this SAME mock allocator: the retry calls NewConfig again,
 * finds the slot already consumed, and gets 0xFF forever. This is the guard
 * against that specific regression. */
TEST(resetting_the_slot_on_failure_bricks_the_allocator_permanently)
{
    DacState s;
    dac_state_init(&s);

    ASSERT_FALSE(ensure_hardware_initialized(&s, true, true, true, false));
    ASSERT_EQ(s.instanceId, 0);
    ASSERT_EQ(s.newConfigCalls, 1);

    /* Simulate the naive-fix regression by hand: reset the id as that shape
     * would, then ask the mock allocator for a new slot the way a retry that
     * did NOT retain the id would have to. */
    s.instanceId = 0xFF;
    uint8_t reallocated = mock_new_config(&s);
    ASSERT_EQ(reallocated, 0xFF);   /* table full -- permanently, per the ticket */
    ASSERT_EQ(s.newConfigCalls, 2);
}

/* Item 3: the power precondition is enforced on EVERY call, not just before
 * the first success -- and dropping power clears the flag WITHOUT touching
 * the retained slot, so the rail's return re-initializes on the same id
 * rather than re-allocating. */
TEST(power_loss_clears_ready_but_retains_the_slot)
{
    DacState s;
    dac_state_init(&s);

    ASSERT_TRUE(ensure_hardware_initialized(&s, true, true, true, true));
    ASSERT_TRUE(s.hardwareInitialized);
    ASSERT_EQ(s.instanceId, 0);
    ASSERT_EQ(s.newConfigCalls, 1);
    int initCallsAfterFirstSuccess = s.initCalls;

    /* Rail drops. A command issued now must be refused, not silently
     * succeed on stale hardware state (the OLD shape's short-circuit on
     * `dacHardwareInitialized` ran BEFORE any power check at all, so it
     * would have returned true here). */
    ASSERT_FALSE(ensure_hardware_initialized(&s, false, true, true, true));
    ASSERT_FALSE(s.hardwareInitialized);
    ASSERT_EQ(s.instanceId, 0);              /* slot NOT reset */
    ASSERT_EQ(s.newConfigCalls, 1);          /* no allocation attempted */

    /* Rail returns; re-init runs on the SAME slot. */
    ASSERT_TRUE(ensure_hardware_initialized(&s, true, true, true, true));
    ASSERT_TRUE(s.hardwareInitialized);
    ASSERT_EQ(s.instanceId, 0);
    ASSERT_EQ(s.newConfigCalls, 1);          /* still just the one allocation */
    ASSERT_EQ(s.initCalls, initCallsAfterFirstSuccess + 1);   /* re-ran Init */

    /* Contrast: the old shape's short-circuit is unconditional, so a
     * power-down call issued after a prior success answers "ready" while the
     * rail is down. */
    DacState old;
    dac_state_init(&old);
    ASSERT_TRUE(old_buggy_ensure_hardware_initialized(&old, true, true, true, true));
    ASSERT_TRUE(old_buggy_ensure_hardware_initialized(&old, false, true, true, true));
    /* old.hardwareInitialized was never re-examined against power -- it is
     * still true, which is precisely the item-3 defect. */
    ASSERT_TRUE(old.hardwareInitialized);
}

/* #980 pass 3 (Qodo /agentic_review bug: "Power cycles leave the DAC marked
 * ready"): a rail drop occurring ENTIRELY INSIDE DAC7718_Init() -- after the
 * top-of-function power check passed, before publish -- must not be
 * published as READY. Without the pre-publish re-check this models, a
 * concurrent caller on the other transport could have already observed the
 * drop and correctly cleared hardwareInitialized, only for THIS call to
 * clobber that correct clear with a stale "true" once its own (now-stale)
 * Init() finishes. */
TEST(power_loss_during_init_is_not_published_as_ready)
{
    DacState s;
    dac_state_init(&s);

    /* powered=true at entry, Init succeeds, but the rail is down by the time
     * we would publish. */
    ASSERT_FALSE(ensure_hardware_initialized_ex(&s, true, true, true, true, false));
    ASSERT_FALSE(s.hardwareInitialized);
    ASSERT_EQ(s.instanceId, 0);        /* slot retained -- same item-1 property */
    ASSERT_FALSE(s.initInProgress);    /* claim released, not leaked */
    ASSERT_EQ(s.newConfigCalls, 1);
    ASSERT_EQ(s.initCalls, 1);         /* DAC7718_Init DID run -- only the
                                        * PUBLISH is refused, not the attempt */

    /* A later call, with the rail genuinely back up for the whole attempt,
     * re-initializes correctly on the retained slot -- the rejected publish
     * above did not brick anything. */
    ASSERT_TRUE(ensure_hardware_initialized_ex(&s, true, true, true, true, true));
    ASSERT_TRUE(s.hardwareInitialized);
    ASSERT_EQ(s.newConfigCalls, 1);    /* still the one allocation */
    ASSERT_EQ(s.initCalls, 2);         /* re-ran Init, not skipped */
}

/* Item 1/4 residual: a concurrent holder of the claim must cause THIS call to
 * fail cleanly without disturbing the holder's claim, without attempting an
 * allocation, and without calling Init -- and once the holder releases, a
 * fresh call proceeds normally. This is the state-machine half of the
 * "does not corrupt the allocator under a race" property; it is NOT a proof
 * that dacInitInProgress's real taskENTER_CRITICAL/EXIT pairing is atomic on
 * hardware (see FIDELITY note 1). */
TEST(a_concurrent_claim_holder_is_refused_without_disturbing_the_holder)
{
    DacState s;
    dac_state_init(&s);

    /* Simulate another transport already inside the claimed region. */
    s.initInProgress = true;

    ASSERT_FALSE(ensure_hardware_initialized(&s, true, true, true, true));
    ASSERT_EQ(s.newConfigCalls, 0);   /* never reached NewConfig */
    ASSERT_EQ(s.initCalls, 0);        /* never reached Init */
    ASSERT_TRUE(s.initInProgress);    /* holder's claim untouched -- not cleared */
    ASSERT_FALSE(s.hardwareInitialized);

    /* Holder finishes and releases. */
    s.initInProgress = false;
    s.hardwareInitialized = true;
    s.instanceId = 0;

    ASSERT_TRUE(ensure_hardware_initialized(&s, true, true, true, true));
    ASSERT_EQ(s.newConfigCalls, 0);   /* the re-check-under-claim path was
                                       * never even entered: the top-level
                                       * `if (s->hardwareInitialized)`
                                       * short-circuit answers it first */
}

/* A claim taken by THIS call must be released on every exit, including a
 * genuinely-full-allocator failure (instanceId still 0xFF but the slot is
 * already consumed by construction here) -- otherwise the claim leaks and
 * the DAC is unusable for the rest of the boot. */
TEST(a_genuinely_full_allocator_failure_still_releases_the_claim)
{
    DacState s;
    dac_state_init(&s);
    s.slotConsumed = true;   /* the one slot is already gone; instanceId stays 0xFF */

    ASSERT_FALSE(ensure_hardware_initialized(&s, true, true, true, true));
    ASSERT_EQ(s.newConfigCalls, 1);
    ASSERT_EQ(s.initCalls, 0);         /* never reached Init -- no id to use */
    ASSERT_FALSE(s.initInProgress);    /* claim released, not leaked */
    ASSERT_FALSE(s.hardwareInitialized);
}

/* Board-config / variant refusals must not touch the claim, the allocator, or
 * Init at all -- they are checked BEFORE the claim is taken. */
TEST(board_and_variant_refusals_never_touch_the_claim_or_allocator)
{
    DacState s;
    dac_state_init(&s);
    ASSERT_FALSE(ensure_hardware_initialized(&s, true, false, true, true));
    ASSERT_FALSE(ensure_hardware_initialized(&s, true, true, false, true));
    ASSERT_EQ(s.newConfigCalls, 0);
    ASSERT_EQ(s.initCalls, 0);
    ASSERT_FALSE(s.initInProgress);
}

/* ==========================================================================
 * PART C -- SCPI_DACVoltageSet all-channel branch: write-all / latch-
 * unconditionally / publish-only-staged shape
 * ========================================================================== */

#define MOCK_MAX_AOUT_CHANNEL 8

typedef struct {
    bool hwChannelValid[MOCK_MAX_AOUT_CHANNEL]; /* per-index: is ChannelNumber < DAC7718_NUM_CHANNELS */
    bool writeFails[MOCK_MAX_AOUT_CHANNEL];     /* per-index: DAC7718_ReadWriteReg returns UINT32_MAX */
    bool latchFails;                            /* DAC7718_UpdateLatch returns false */

    int writeCalls;
    int latchCalls;
    bool published[MOCK_MAX_AOUT_CHANNEL];      /* BoardData_Set was called for this index */
} AllChanMockEnv;

static void allchan_mock_init(AllChanMockEnv *env, size_t nChannels)
{
    size_t i;
    for (i = 0; i < MOCK_MAX_AOUT_CHANNEL; i++) {
        env->hwChannelValid[i] = (i < nChannels);
        env->writeFails[i] = false;
        env->published[i] = false;
    }
    env->latchFails = false;
    env->writeCalls = 0;
    env->latchCalls = 0;
}

static bool mock_write_reg(AllChanMockEnv *env, size_t i)
{
    env->writeCalls++;
    return !env->writeFails[i];
}

static bool mock_update_latch(AllChanMockEnv *env)
{
    env->latchCalls++;
    return !env->latchFails;
}

/* #980 pass-4 shape -- mirrors SCPI_DACVoltageSet's all-channel branch:
 * write every channel it can (skip only channels that themselves fail,
 * either an invalid hwChannel or a failed register write), fire the ONE
 * latch UNCONDITIONALLY, then publish BoardData only for channels that were
 * actually staged. Returns the failedCount (0 == overall SCPI_RES_OK, per
 * the real function's `if (failedCount > 0)` tail) or SIZE_MAX if the latch
 * itself failed (nothing published, nothing staged is trustworthy). */
static size_t all_channel_set_new_shape(AllChanMockEnv *env, size_t nChannels)
{
    bool staged[MOCK_MAX_AOUT_CHANNEL];
    size_t failedCount = 0;
    size_t i;

    for (i = 0; i < nChannels; i++) {
        staged[i] = false;

        if (!env->hwChannelValid[i]) {
            failedCount++;
            continue;
        }

        if (!mock_write_reg(env, i)) {
            failedCount++;
            continue;
        }

        staged[i] = true;
    }

    /* UNCONDITIONAL -- fires even when every write failed. */
    if (!mock_update_latch(env)) {
        return SIZE_MAX;   /* nothing published */
    }

    for (i = 0; i < nChannels; i++) {
        if (staged[i]) {
            env->published[i] = true;
        }
    }

    return failedCount;
}

/* Pre-#980 pass-2 (buggy) shape: abort the loop on the FIRST failure, before
 * ever calling UpdateLatch. Channels already written before the abort are
 * left with an uncommitted new value in their shadow registers -- this mock
 * cannot show that indeterminate shadow state directly (it only tracks
 * `published`, which correctly stays all-false), but it DOES show the
 * observable half of the bug: UpdateLatch is never even attempted, so any
 * channel written before the failing one is left in limbo for the NEXT
 * unrelated latch call to surface -- silently, with this command reporting
 * only the one failure and nothing published. */
static size_t all_channel_set_old_shape(AllChanMockEnv *env, size_t nChannels)
{
    size_t i;

    for (i = 0; i < nChannels; i++) {
        if (!env->hwChannelValid[i]) {
            continue;   /* old shape: silent skip, not a failure */
        }
        if (!mock_write_reg(env, i)) {
            return 1;   /* abort -- latch never called, nothing published */
        }
    }

    if (!mock_update_latch(env)) {
        return SIZE_MAX;
    }

    for (i = 0; i < nChannels; i++) {
        if (env->hwChannelValid[i]) {
            env->published[i] = true;
        }
    }

    return 0;
}

/* Headline: full success writes and publishes every channel, exactly one
 * latch call. */
TEST(all_channel_full_success_writes_latches_once_publishes_all)
{
    AllChanMockEnv env;
    size_t i;
    allchan_mock_init(&env, 4);

    ASSERT_EQ(all_channel_set_new_shape(&env, 4), 0);
    ASSERT_EQ(env.writeCalls, 4);
    ASSERT_EQ(env.latchCalls, 1);
    for (i = 0; i < 4; i++) {
        ASSERT_TRUE(env.published[i]);
    }
}

/* The defining property of the fix: a mid-list failure does NOT abort the
 * loop -- every other channel is still written, the latch STILL fires
 * exactly once, and only the failed channel is excluded from publish. This
 * is the assertion that would fail against the old (pass-2) shape. */
TEST(all_channel_one_failure_does_not_abort_the_rest)
{
    AllChanMockEnv env;
    allchan_mock_init(&env, 4);
    env.writeFails[1] = true;   /* channel index 1 fails its register write */

    ASSERT_EQ(all_channel_set_new_shape(&env, 4), 1);   /* one failure reported */
    ASSERT_EQ(env.writeCalls, 4);   /* all four attempted -- no abort */
    ASSERT_EQ(env.latchCalls, 1);   /* latch fires exactly once, unconditionally */
    ASSERT_TRUE(env.published[0]);
    ASSERT_FALSE(env.published[1]); /* the failed channel is NOT published */
    ASSERT_TRUE(env.published[2]);
    ASSERT_TRUE(env.published[3]);

    /* Contrast: the old shape aborts on the first failure and never reaches
     * channels 2/3, never calls the latch, and publishes nothing at all --
     * even the channels that succeeded before the abort. */
    AllChanMockEnv old;
    allchan_mock_init(&old, 4);
    old.writeFails[1] = true;
    ASSERT_EQ(all_channel_set_old_shape(&old, 4), 1);
    ASSERT_EQ(old.writeCalls, 2);     /* stopped at the failing channel */
    ASSERT_EQ(old.latchCalls, 0);     /* latch never even attempted */
    ASSERT_FALSE(old.published[0]);   /* channel 0 succeeded but is unpublished
                                       * -- and, per the real driver, its
                                       * shadow register is left holding the
                                       * new value with nothing to commit it */
}

/* Every channel fails: the latch must STILL fire exactly once (draining any
 * shadow state left over from a completely unrelated prior command), and
 * nothing is published. */
TEST(all_channel_every_write_fails_latch_still_fires_nothing_published)
{
    AllChanMockEnv env;
    size_t i;
    allchan_mock_init(&env, 3);
    env.writeFails[0] = true;
    env.writeFails[1] = true;
    env.writeFails[2] = true;

    ASSERT_EQ(all_channel_set_new_shape(&env, 3), 3);
    ASSERT_EQ(env.writeCalls, 3);
    ASSERT_EQ(env.latchCalls, 1);   /* still fires -- this is the whole point */
    for (i = 0; i < 3; i++) {
        ASSERT_FALSE(env.published[i]);
    }
}

/* An invalid hwChannel (out-of-range ChannelNumber) is a per-channel FAILURE,
 * not a silent skip -- it must count toward failedCount, must not attempt a
 * register write for that index, and must not stop the latch from covering
 * every OTHER channel. */
TEST(all_channel_invalid_hw_channel_counts_as_failure_not_silent_skip)
{
    AllChanMockEnv env;
    allchan_mock_init(&env, 3);
    env.hwChannelValid[1] = false;   /* out-of-range ChannelNumber at index 1 */

    ASSERT_EQ(all_channel_set_new_shape(&env, 3), 1);
    ASSERT_EQ(env.writeCalls, 2);    /* index 1 never reaches the write call */
    ASSERT_EQ(env.latchCalls, 1);
    ASSERT_TRUE(env.published[0]);
    ASSERT_FALSE(env.published[1]);  /* excluded */
    ASSERT_TRUE(env.published[2]);
}

/* If UpdateLatch itself fails, NOTHING is published -- not even channels
 * whose register write succeeded -- because the latch is what makes a
 * staged write physically live, and it never fired successfully. */
TEST(all_channel_latch_failure_publishes_nothing_even_if_all_writes_succeeded)
{
    AllChanMockEnv env;
    size_t i;
    allchan_mock_init(&env, 3);
    env.latchFails = true;

    ASSERT_EQ(all_channel_set_new_shape(&env, 3), SIZE_MAX);
    ASSERT_EQ(env.writeCalls, 3);   /* every write still attempted */
    ASSERT_EQ(env.latchCalls, 1);
    for (i = 0; i < 3; i++) {
        ASSERT_FALSE(env.published[i]);
    }
}

/* ==========================================================================
 * PART D -- SCPI_DACVoltageGet's dacWriterPossible gate
 * ========================================================================== */

typedef struct {
    int  boardVariant;     /* mirrors pCfg->BoardVariant; 3 == NQ3 */
    bool lockTakeSucceeds; /* xSemaphoreTake() outcome -- consulted only when
                             * a lock attempt actually reaches the mock */
    int  lockCalls;
    int  unlockCalls;
    bool lastUnlockHeld;
} GetterMockEnv;

static void getter_mock_init(GetterMockEnv *env, int boardVariant, bool lockTakeSucceeds)
{
    env->boardVariant = boardVariant;
    env->lockTakeSucceeds = lockTakeSucceeds;
    env->lockCalls = 0;
    env->unlockCalls = 0;
    env->lastUnlockHeld = false;
}

/* Mirrors SCPIDAC_LockCommand(): the real function fails when
 * gDacCommandMutex == NULL, which is true exactly when BoardVariant != 3
 * (SCPIDAC_InitGlobal() -- the mutex's only creator -- is called solely
 * under that guard, app_freertos.c:1007). */
static bool mock_lock_command(GetterMockEnv *env)
{
    env->lockCalls++;
    if (env->boardVariant != 3) {
        return false;
    }
    return env->lockTakeSucceeds;
}

static void mock_unlock_command(GetterMockEnv *env, bool lockHeld)
{
    env->unlockCalls++;
    env->lastUnlockHeld = lockHeld;
}

/* Post-fix shape -- mirrors SCPI_DACVoltageGet as it stands after this
 * fix: lock only when dacWriterPossible, thread lockHeld through to Unlock.
 * Returns true for SCPI_RES_OK, false for SCPI_RES_ERR ("DAC command
 * busy"). */
static bool getter_gated_shape(GetterMockEnv *env)
{
    bool dacWriterPossible = (env->boardVariant == 3);
    bool lockHeld = false;

    if (dacWriterPossible) {
        if (!mock_lock_command(env)) {
            return false;
        }
        lockHeld = true;
    }

    mock_unlock_command(env, lockHeld);
    return true;
}

/* Pre-fix shape -- PR #990 as originally merged: locks UNCONDITIONALLY,
 * regardless of board variant. This is the confirmed regression: on
 * NQ1/NQ2 the mutex is never created, so this ALWAYS fails, where the
 * pre-PR getter (no lock at all) always succeeded. */
static bool getter_unconditional_shape(GetterMockEnv *env)
{
    if (!mock_lock_command(env)) {
        return false;
    }
    mock_unlock_command(env, true);
    return true;
}

/* THE test that would have caught PR #990's BLOCK-audit regression. Every
 * board on this bench is an NQ1 -- this is that exact scenario.
 *
 * Contrast within one test, same convention as Part C's
 * all_channel_one_failure_does_not_abort_the_rest: the gated (fixed) shape
 * must succeed and must never even attempt the lock, while the
 * unconditional (as-merged, buggy) shape -- run against the identical mock
 * inputs -- fails. Asserting both against the same inputs, rather than only
 * the fixed shape, is what proves the gate is load-bearing instead of
 * vacuously true. */
TEST(getter_on_non_nq3_succeeds_gated_but_fails_unconditional)
{
    GetterMockEnv env;
    getter_mock_init(&env, 1 /* NQ1 */, true /* lock WOULD succeed if attempted */);
    ASSERT_TRUE(getter_gated_shape(&env));   /* SCPI_RES_OK -- the fix */
    ASSERT_EQ(env.lockCalls, 0);             /* never even attempted */
    ASSERT_EQ(env.unlockCalls, 1);
    ASSERT_FALSE(env.lastUnlockHeld);        /* nothing to give back */

    GetterMockEnv old;
    getter_mock_init(&old, 1 /* NQ1, identical mock inputs */, true);
    ASSERT_FALSE(getter_unconditional_shape(&old)); /* SCPI_RES_ERR -- #990's regression */
    ASSERT_EQ(old.lockCalls, 1);
    ASSERT_EQ(old.unlockCalls, 0);           /* early return -- never reaches Unlock */
}

/* NQ3 behavior must be unchanged by the fix: dacWriterPossible is true, so
 * the getter still locks, still fails CLOSED on contention (never silently
 * skips the lock just because it timed out), and still gives the lock back
 * on success. */
TEST(getter_on_nq3_still_locks_and_unlocks_unchanged)
{
    GetterMockEnv ok;
    getter_mock_init(&ok, 3, true);
    ASSERT_TRUE(getter_gated_shape(&ok));
    ASSERT_EQ(ok.lockCalls, 1);
    ASSERT_EQ(ok.unlockCalls, 1);
    ASSERT_TRUE(ok.lastUnlockHeld);

    GetterMockEnv busy;
    getter_mock_init(&busy, 3, false /* xSemaphoreTake times out */);
    ASSERT_FALSE(getter_gated_shape(&busy)); /* fails CLOSED, not open */
    ASSERT_EQ(busy.lockCalls, 1);
    ASSERT_EQ(busy.unlockCalls, 0);          /* never took it, never gives it */
}

int main(void)
{
    printf("#980 -- DAC7718 error-path honesty (extracted control-flow shapes)\n");
    printf("---------------------------------------------\n");

    /* Part A */
    RUN(prelock_validation_failure_never_gives_new_shape_gives_old_shape_does);
    RUN(lock_failure_gives_nothing_in_either_shape);
    RUN(postlock_failure_still_gives_the_mutex_it_took);
    RUN(success_path_locks_and_gives_exactly_once);

    /* Part B */
    RUN(failed_init_is_reported_and_retry_reuses_the_retained_slot);
    RUN(resetting_the_slot_on_failure_bricks_the_allocator_permanently);
    RUN(power_loss_clears_ready_but_retains_the_slot);
    RUN(power_loss_during_init_is_not_published_as_ready);
    RUN(a_concurrent_claim_holder_is_refused_without_disturbing_the_holder);
    RUN(a_genuinely_full_allocator_failure_still_releases_the_claim);
    RUN(board_and_variant_refusals_never_touch_the_claim_or_allocator);

    /* Part C */
    RUN(all_channel_full_success_writes_latches_once_publishes_all);
    RUN(all_channel_one_failure_does_not_abort_the_rest);
    RUN(all_channel_every_write_fails_latch_still_fires_nothing_published);
    RUN(all_channel_invalid_hw_channel_counts_as_failure_not_silent_skip);
    RUN(all_channel_latch_failure_publishes_nothing_even_if_all_writes_succeeded);

    /* Part D */
    RUN(getter_on_non_nq3_succeeds_gated_but_fails_unconditional);
    RUN(getter_on_nq3_still_locks_and_unlocks_unchanged);

    return TEST_SUMMARY();
}
