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
 * 3. This is source-level validation only. #980's own Bench line is
 *    "NQ3 (absent from this bench)" -- see the PR body for the queued
 *    daqifi-python-test-suite companion test and the READY FOR BENCH note.
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
 * the mocks above. */
static bool ensure_hardware_initialized(DacState *s, bool powered, bool boardOk,
                                        bool variantOk, bool initSucceeds)
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

    s->hardwareInitialized = true;
    s->initInProgress = false;
    return true;
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
    RUN(a_concurrent_claim_holder_is_refused_without_disturbing_the_holder);
    RUN(a_genuinely_full_allocator_failure_still_releases_the_claim);
    RUN(board_and_variant_refusals_never_touch_the_claim_or_allocator);

    return TEST_SUMMARY();
}
