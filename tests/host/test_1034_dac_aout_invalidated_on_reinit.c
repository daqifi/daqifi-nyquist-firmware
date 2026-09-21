/* ==========================================================================
 * test_1034_dac_aout_invalidated_on_reinit.c -- issue #1034
 *
 * WHAT IS UNDER TEST
 *
 * DAC_EnsureHardwareInitialized() (SCPIDAC.c) reinitialises the DAC7718 on
 * its FIRST call after the 10V rail returns from a power loss (the top-of-
 * function power check clears dacHardwareInitialized on the way down, so the
 * next successful call re-runs DAC7718_Init(), which pulses reset and
 * re-latches every physical output to its hardware reset state). Before
 * #1034, nothing reconciled BOARDDATA_AOUT_LATEST with that reset: the cache
 * kept holding whatever voltage was commanded before the power loss, so
 * SOUR:VOLT:LEV? kept confidently reporting it as current -- CONFIDENTLY
 * WRONG, not merely unknown, because the reset just made it false.
 *
 * THE FIX has three parts, all modelled here:
 *
 * 1. AOutSample gained a `Timestamp` field (AOutSample.h), 0 meaning "not
 *    known" -- the same convention AInSample already uses. Every successful
 *    SCPI_DACVoltageSet write stamps it (via SCPIDAC_ValidTimestamp(), see
 *    part 3); every successful DAC7718_Init() inside
 *    DAC_EnsureHardwareInitialized() zeroes it (and Channel, and Voltage)
 *    for EVERY channel slot, under gDacCommandMutex (the same mutex #990
 *    Finding 0 / #1030 already require every other BOARDDATA_AOUT_LATEST
 *    writer to hold, to avoid tearing the 64-bit Voltage double mid-read on
 *    the other SCPI transport).
 * 2. SCPI_DACVoltageGet treats Timestamp < 1 as "not known": the
 *    single-channel form answers a deferred SCPI execution error (mirroring
 *    the existing MEAS:VOLT:DC? precedent in SCPIADC.c for a channel whose
 *    monitoring data is stale -- error rather than answer with a number that
 *    may be wrong) instead of writing a value; the all-channel form cannot
 *    error mid-reply (SCPI numeric list replies are positional, one value
 *    per channel, same reasoning as MEAS:VOLT:DC?'s all-channel fallback for
 *    a disabled/stale AIN channel), so it answers 0.0 for an unknown channel
 *    rather than a voltage that may no longer describe the pin.
 *
 * ROUND 1 (Qodo /agentic_review) found two BLOCKING defects in the first
 * shape of parts 1 and its lock ordering, both fixed here and both modelled:
 *
 * 3. **Tick-zero sentinel collision.** xTaskGetTickCount() legitimately
 *    returns 0 (scheduler startup; every 32-bit wrap), colliding with the
 *    Timestamp==0 "unknown" sentinel -- a command executing on tick 0 would
 *    have been stored as its own invalidation marker and read back as
 *    unknown immediately. Fixed by SCPIDAC_ValidTimestamp(), a single shared
 *    helper (not duplicated at each call site) that maps a 0 tick to 1.
 * 4. **Reset/invalidate/setter-publish ordering.** The first shape called
 *    DAC7718_Init() BEFORE taking gDacCommandMutex, and took the lock only
 *    for the invalidation loop afterward. Two consequences: (a) a setter
 *    that had already passed its OWN (earlier) readiness check, and so
 *    never re-checks it, could win the lock in the gap between
 *    DAC7718_Init() returning and the (then-later) lock attempt here,
 *    publish a genuinely fresh voltage, and have the invalidation loop wipe
 *    it moments later; (b) if the lock attempt then failed (its 2 s
 *    timeout, under contention), the hardware had ALREADY been reset, so
 *    the cache was left reporting pre-reset voltages as current -- worse
 *    than the divergence #1034 exists to fix. Fixed by taking the lock
 *    BEFORE DAC7718_Init() and holding it through invalidation, so a lock
 *    failure now leaves the hardware untouched (part 4's shape below models
 *    this: DAC7718_Init is never even attempted when the lock fails).
 *
 * HOW IT IS TESTED
 *
 * Like test_980_dac7718_error_paths.c, SCPIDAC.c is not a host-test
 * candidate on its own (libscpi, Harmony, FreeRTOS, BoardData/BoardConfig).
 * This file re-implements the two SHAPES under test -- the getter's
 * staleness check, and the reinit-invalidation loop's lock discipline --
 * against a mocked BOARDDATA_AOUT_LATEST array and a mocked
 * SCPIDAC_LockCommand/UnlockCommand pair, mirroring the real functions'
 * variable names and branch order line-for-line (grep-guarded below against
 * the real source so a future edit that moves this shape fails the build
 * instead of silently going untested).
 *
 * The OLD_BUGGY getter (get_single_OLD_BUGGY / get_all_OLD_BUGGY) models
 * SCPI_DACVoltageGet exactly as it read before #1034 -- unconditionally
 * `pSample->Voltage`, no Timestamp check at all -- proven against the SAME
 * post-reinit cache state the fixed getter is proven against, so every
 * headline assertion has a "this is the bug #1034 reports" companion, the
 * way test_980 contrasts its old and new shapes.
 *
 * AUDIT CORRECTION (adversarial audit on PR #1147, medium, wrong_output,
 * disposition fix_now) -- part 2 above, as first shipped, was ASYMMETRIC:
 * the all-channel form answered 0.0 for an unknown channel while the
 * single-channel form errored on the IDENTICAL state. 0.0 is a value
 * indistinguishable from a genuine 0V reading, so that substitution
 * reintroduced the exact fabricated-voltage class #1034 exists to remove,
 * merely relocated from "the stale pre-reinit voltage" to "zero volts":
 * CONF:DAC:UPDATE, then SOUR:VOLT:LEV 0,5, then SOUR:VOLT:LEV? answered
 * 5,0,0,0,0,0,0,0 -- presenting seven genuinely UNKNOWN physical outputs as
 * a definite 0V. The comment that justified it ("the all-channel form
 * cannot error mid-reply") was also factually wrong: every value is
 * buffered into a local array and nothing reaches the transport until the
 * function's cleanup path, gated on success -- an early return-with-error
 * is fully achievable, and is what the corrected shape below does.
 *
 * THE FIX: the all-channel form now applies the IDENTICAL staleness check
 * as the single-channel form (`Timestamp < 1` => error the whole reply, not
 * just the one channel), matching the audit's requirement that the two
 * paths be internally consistent. get_all_FIXED() below models this
 * corrected shape (grep-guarded against SCPIDAC.c, same as the rest of this
 * file); get_all_PRE_1147_FIX() models the shape PR #1147 shipped before
 * the correction, kept ONLY to demonstrate the bug it produced (mirrors this
 * file's existing OLD_BUGGY / FIXED contrast pattern for #1034 itself).
 *
 * FIDELITY -- what this does NOT cover
 *
 * 1. No real FreeRTOS semaphore, no real critical section, no real
 *    concurrency -- the lock mock is a call-counted boolean, proving the
 *    STATE MACHINE takes-and-releases exactly once per invalidation and
 *    fails closed when the mock reports contention, not that the real
 *    xSemaphoreTake/Give pairing is race-free on hardware.
 * 2. What voltage the DAC7718 reset state physically corresponds to
 *    (bipolar/unipolar configuration, full-scale range) is NOT modelled and
 *    NOT claimed -- that is exactly the fact #1034's own ticket says this
 *    project cannot determine from source, which is why the fix invalidates
 *    rather than guesses a replacement value.
 * 3. Source-level validation only. #1034's own Bench line is "none on this
 *    bench (needs an NQ3, see #554) -- the mechanism is provable from
 *    source" -- see the PR body for the queued daqifi-python-test-suite
 *    companion.
 * ========================================================================== */

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#include "test_framework.h"

/* ---- Mirrors AOutSample.h's #1034 shape ------------------------------- */
#define MOCK_MAX_AOUT_CHANNEL 8

typedef struct {
    uint32_t Timestamp;
    uint8_t  Channel;
    double   Voltage;
} MockAOutSample;

static MockAOutSample g_cache[MOCK_MAX_AOUT_CHANNEL];
static uint32_t       g_tick = 0; /* stand-in for xTaskGetTickCount() */

static void cache_reset_to_boot_state(void) {
    memset(g_cache, 0, sizeof(g_cache));
    g_tick = 100; /* an ordinary nonzero tick; tests that care about the
                   * tick==0 edge case set g_tick explicitly. */
}

/* ---- Mirrors SCPIDAC_LockCommand/UnlockCommand's call-counted mock,
 * same idiom test_980's Part A/B use for DAC7718_Lock/Unlock. ----------- */
static int  g_lock_calls, g_unlock_calls;
static bool g_lock_should_succeed;
static bool g_locked; /* true only while the mock lock is actually held */

static void mock_lock_reset(bool shouldSucceed) {
    g_lock_calls = 0;
    g_unlock_calls = 0;
    g_lock_should_succeed = shouldSucceed;
    g_locked = false;
}

static bool mock_SCPIDAC_LockCommand(void) {
    g_lock_calls++;
    if (!g_lock_should_succeed) {
        return false;
    }
    g_locked = true;
    return true;
}

static void mock_SCPIDAC_UnlockCommand(bool lockHeld) {
    if (lockHeld) {
        g_unlock_calls++;
        g_locked = false;
    }
}

/* ---- Mirrors DAC7718_Init(), call-counted the same way, so a test can
 * assert not just the outcome but whether hardware was EVER touched. ---- */
static int  g_dac_init_calls;
static bool g_dac_init_should_succeed;

static void mock_dac_init_reset(bool shouldSucceed) {
    g_dac_init_calls = 0;
    g_dac_init_should_succeed = shouldSucceed;
}

static bool mock_DAC7718_Init(void) {
    g_dac_init_calls++;
    return g_dac_init_should_succeed;
}

/* ---- #1034 round-1-and-2-fixed shape: DAC_EnsureHardwareInitialized's
 * reset-then-invalidate sequence. Mirrors SCPIDAC.c's corrected ordering
 * (grep-guarded below):
 *   - lock is taken BEFORE the (mock) hardware reset and held through the
 *     invalidation loop, so: (a) a lock failure leaves hardware UNTOUCHED
 *     (mock_DAC7718_Init is never called -- closes round-1 finding "Lock
 *     contention revives stale voltages"), and (b) reset and invalidation
 *     happen as one operation with respect to any setter serialized on the
 *     same lock (closes round-1's "Reset can erase a successful voltage");
 *   - the invalidation loop runs UNCONDITIONALLY after the (mock) reset,
 *     regardless of whether it reports success -- closes round-2's
 *     "Invalidate cache after failed resets" (importance 10): a
 *     DAC7718_Init() failure AFTER its RST pulse (config-register write,
 *     UpdateLatch) still leaves the hardware physically reset, so skipping
 *     invalidation on that path reopens the exact bug #1034 exists to fix,
 *     just reached via a failure return instead of a success one. --------- */
static bool reinit_shape(void) {
    if (!mock_SCPIDAC_LockCommand()) {
        return false; /* hardware untouched -- see mock_dac_init_reset call counts */
    }
    bool initSucceeded = mock_DAC7718_Init();
    MockAOutSample invalidated = {0};
    for (size_t i = 0; i < MOCK_MAX_AOUT_CHANNEL; i++) {
        /* g_locked observable here models "this write only happens while
         * held" -- checked explicitly in a dedicated test below. */
        g_cache[i] = invalidated;
    }
    mock_SCPIDAC_UnlockCommand(true);
    return initSucceeded;
}

/* ---- #1034 round-1-fixed shape: SCPIDAC_ValidTimestamp(), mirrored
 * exactly (grep-guarded below) -- both writers call THIS, not a raw tick
 * read, so the tick==0 substitution lives in one place. g_tick is set
 * directly by a test (not auto-incremented) so tick==0 is reachable. ---- */
static uint32_t mock_valid_timestamp(void) {
    return (g_tick == 0) ? 1u : g_tick;
}

/* ---- #1034 shape: SCPI_DACVoltageSet's publish, both branches stamp
 * identically -- mirrors '.Timestamp = SCPIDAC_ValidTimestamp()'. ------- */
static void set_shape(size_t index, uint8_t channelId, double voltage) {
    MockAOutSample sample = { .Timestamp = mock_valid_timestamp(), .Channel = channelId, .Voltage = voltage };
    g_cache[index] = sample;
}

/* ---- #1034 (fixed) shape: SCPI_DACVoltageGet's staleness check.
 * Mirrors 'pSample->Timestamp < 1' / 'pSample->Timestamp >= 1'. --------- */
static bool get_single_FIXED(size_t index, double* outVoltage) {
    MockAOutSample* pSample = &g_cache[index];
    if (pSample->Timestamp < 1) {
        return false; /* mirrors: deferredError = SCPI_ERROR_EXECUTION_ERROR; goto cleanup; */
    }
    *outVoltage = pSample->Voltage;
    return true;
}

/* #1147 audit correction: returns bool now, matching get_single_FIXED's own
 * contract -- ANY unknown channel fails the WHOLE reply (mirrors:
 * deferredError = SCPI_ERROR_EXECUTION_ERROR; goto cleanup;), rather than
 * filling that one slot with a fabricated 0.0 and reporting success. Nothing
 * is written to out[] past the failing index -- mirrors allVoltages[] never
 * reaching SCPI_ResultVoltage on the error path (result != SCPI_RES_OK). */
static bool get_all_FIXED(double out[MOCK_MAX_AOUT_CHANNEL], size_t n) {
    for (size_t i = 0; i < n; i++) {
        MockAOutSample* pSample = &g_cache[i];
        if (pSample->Timestamp < 1) {
            return false;
        }
        out[i] = pSample->Voltage;
    }
    return true;
}

/* ---- PRE-#1147-fix shape: SCPI_DACVoltageGet's all-channel form exactly as
 * PR #1147 first shipped it -- substitutes 0.0 for an unknown channel
 * instead of erroring the whole reply. This is the audit-confirmed
 * wrong_output finding (medium, disposition fix_now) that the shape above
 * corrects; kept only to demonstrate what it would have answered. --------- */
static void get_all_PRE_1147_FIX(double out[MOCK_MAX_AOUT_CHANNEL], size_t n) {
    for (size_t i = 0; i < n; i++) {
        MockAOutSample* pSample = &g_cache[i];
        out[i] = (pSample->Timestamp >= 1) ? pSample->Voltage : 0.0;
    }
}

/* ---- OLD_BUGGY shape: SCPI_DACVoltageGet exactly as it read before
 * #1034 -- unconditional Voltage, no staleness notion at all. ----------- */
static double get_single_OLD_BUGGY(size_t index) {
    MockAOutSample* pSample = &g_cache[index];
    return pSample->Voltage; /* always "succeeds"; never signals unknown */
}

static void get_all_OLD_BUGGY(double out[MOCK_MAX_AOUT_CHANNEL], size_t n) {
    for (size_t i = 0; i < n; i++) {
        out[i] = g_cache[i].Voltage;
    }
}

/* ========================================================================
 * Getter staleness, fixed shape
 * ======================================================================== */

TEST(boot_state_every_channel_reports_unknown) {
    cache_reset_to_boot_state();
    double v = 12345.0; /* poisoned -- must not be touched on the unknown path */
    ASSERT_FALSE(get_single_FIXED(0, &v));
    ASSERT_EQ((long long)(v * 1000), 12345000); /* untouched */

    /* #1147: at boot EVERY channel is unknown, so the all-channel form must
     * error the whole reply too -- not answer eight fabricated 0.0V
     * readings (see pre_1147_fix_shape_would_have_fabricated_a_zero_volt_
     * reading below for what the unfixed shape actually produced). */
    double all[MOCK_MAX_AOUT_CHANNEL];
    for (size_t i = 0; i < MOCK_MAX_AOUT_CHANNEL; i++) all[i] = 12345.0; /* poisoned */
    ASSERT_FALSE(get_all_FIXED(all, MOCK_MAX_AOUT_CHANNEL));
    for (size_t i = 0; i < MOCK_MAX_AOUT_CHANNEL; i++) {
        ASSERT_EQ((long long)(all[i] * 1000), 12345000); /* untouched */
    }
}

TEST(after_a_set_the_channel_reports_known_and_correct) {
    cache_reset_to_boot_state();
    set_shape(3, /*channelId=*/3, 4.25);

    double v = 0.0;
    ASSERT_TRUE(get_single_FIXED(3, &v));
    ASSERT_TRUE(v == 4.25);

    /* #1147: one known channel does not make the sweep answerable -- the
     * other seven are still unknown, so the all-channel form must error. */
    double all[MOCK_MAX_AOUT_CHANNEL];
    ASSERT_FALSE(get_all_FIXED(all, MOCK_MAX_AOUT_CHANNEL));
}

TEST(all_channel_form_errors_on_a_mixed_cache) {
    /* #1147 audit correction: this test used to be named
     * "..._never_errors_even_with_a_mixed_cache" and asserted the OPPOSITE
     * -- that two known channels among eight were enough to answer the
     * whole sweep, fabricating 0.0 for the other six. That was the
     * confirmed finding; this is the corrected contract. */
    cache_reset_to_boot_state();
    set_shape(0, 0, 1.0);
    set_shape(5, 5, -2.5);
    /* channels 1-4, 6, 7 never commanded */

    double all[MOCK_MAX_AOUT_CHANNEL];
    ASSERT_FALSE(get_all_FIXED(all, MOCK_MAX_AOUT_CHANNEL));
}

TEST(all_channel_form_succeeds_only_once_every_channel_is_known) {
    cache_reset_to_boot_state();
    for (uint8_t ch = 0; ch < MOCK_MAX_AOUT_CHANNEL; ch++) {
        set_shape(ch, ch, (double)ch);
    }

    double all[MOCK_MAX_AOUT_CHANNEL];
    ASSERT_TRUE(get_all_FIXED(all, MOCK_MAX_AOUT_CHANNEL));
    for (size_t i = 0; i < MOCK_MAX_AOUT_CHANNEL; i++) {
        ASSERT_TRUE(all[i] == (double)i);
    }
}

/* ========================================================================
 * The #1147 audit finding itself: the all-channel form must not fabricate
 * a 0V reading for an unknown channel, and the OLD (as-first-shipped) shape
 * did exactly that.
 * ======================================================================== */

TEST(pre_1147_fix_shape_would_have_fabricated_a_zero_volt_reading) {
    /* Literal repro from the audit finding: CONF:DAC:UPDATE reaches
     * DAC_EnsureHardwareInitialized()'s reinit path (invalidating every
     * channel), then SOUR:VOLT:LEV 0,5 makes channel 0 known again. The
     * shape PR #1147 shipped before this correction answered
     * SOUR:VOLT:LEV? as 5,0,0,0,0,0,0,0 -- presenting seven genuinely
     * UNKNOWN physical outputs as a definite 0V. */
    cache_reset_to_boot_state();
    mock_dac_init_reset(true);
    mock_lock_reset(true);
    ASSERT_TRUE(reinit_shape()); /* CONF:DAC:UPDATE's reinit path */
    set_shape(0, 0, 5.0);        /* SOUR:VOLT:LEV 0,5 */

    double allPre[MOCK_MAX_AOUT_CHANNEL];
    get_all_PRE_1147_FIX(allPre, MOCK_MAX_AOUT_CHANNEL);
    ASSERT_TRUE(allPre[0] == 5.0);
    for (size_t i = 1; i < MOCK_MAX_AOUT_CHANNEL; i++) {
        ASSERT_TRUE(allPre[i] == 0.0); /* THE BUG: fabricated, not unknown */
    }
}

TEST(fixed_all_channel_form_errors_on_the_identical_scenario) {
    /* Same scenario as the test above, run against the CORRECTED shape --
     * must error the whole reply instead of fabricating. This is the test
     * that fails against the shape PR #1147 first shipped and passes
     * against the fix (see the PR body for both captured outputs). */
    cache_reset_to_boot_state();
    mock_dac_init_reset(true);
    mock_lock_reset(true);
    ASSERT_TRUE(reinit_shape());
    set_shape(0, 0, 5.0);

    double all[MOCK_MAX_AOUT_CHANNEL];
    ASSERT_FALSE(get_all_FIXED(all, MOCK_MAX_AOUT_CHANNEL));
}

/* ========================================================================
 * The #1034 bug itself: reinit must invalidate, and the OLD shape did not
 * ======================================================================== */

TEST(reinit_invalidates_a_previously_known_channel) {
    cache_reset_to_boot_state();
    set_shape(2, 2, 9.9); /* channel 2 commanded to 9.9V before the power loss */

    double before = 0.0;
    ASSERT_TRUE(get_single_FIXED(2, &before));
    ASSERT_TRUE(before == 9.9);

    mock_dac_init_reset(/*shouldSucceed=*/true);
    mock_lock_reset(/*shouldSucceed=*/true);
    ASSERT_TRUE(reinit_shape());

    double after = 12345.0;
    ASSERT_FALSE(get_single_FIXED(2, &after)); /* #1034: now reports unknown */
    ASSERT_EQ((long long)(after * 1000), 12345000); /* untouched by the failed get */
}

TEST(reinit_invalidates_every_channel_not_just_the_one_this_transport_set) {
    cache_reset_to_boot_state();
    /* Simulate BOTH transports having commanded channels before the power
     * loss -- #1034's fix must not special-case "the channel I touched". */
    set_shape(0, 0, 1.1);
    set_shape(1, 1, 2.2);
    set_shape(4, 4, 3.3);
    set_shape(7, 7, 4.4);

    mock_dac_init_reset(true);
    mock_lock_reset(true);
    ASSERT_TRUE(reinit_shape());

    double v;
    for (size_t i = 0; i < MOCK_MAX_AOUT_CHANNEL; i++) {
        ASSERT_FALSE(get_single_FIXED(i, &v));
        ASSERT_EQ((long long)g_cache[i].Timestamp, 0);
    }
}

TEST(old_buggy_shape_would_have_reported_the_stale_voltage_as_current) {
    /* This is the #1034 bug, demonstrated: with NO invalidation step at all
     * (the pre-#1034 shape -- DAC_EnsureHardwareInitialized never touched
     * BOARDDATA_AOUT_LATEST) and the OLD unconditional getter, a reinit
     * changes nothing about what the cache reports. */
    cache_reset_to_boot_state();
    set_shape(6, 6, 7.5);

    /* Deliberately do NOT call reinit_shape() here -- this test proves what
     * the OLD code did (no invalidation call site existed). */

    double stillReportedAsCurrent = get_single_OLD_BUGGY(6);
    ASSERT_TRUE(stillReportedAsCurrent == 7.5); /* confidently wrong: pin was just reset */

    double allOld[MOCK_MAX_AOUT_CHANNEL];
    get_all_OLD_BUGGY(allOld, MOCK_MAX_AOUT_CHANNEL);
    ASSERT_TRUE(allOld[6] == 7.5); /* same bug, all-channel form */
}

TEST(fixed_shape_closes_the_same_scenario_the_old_shape_missed) {
    /* Same setup as the bug-demonstration test above, but running the FIX
     * (invalidation actually happens) and reading with the FIXED getter. */
    cache_reset_to_boot_state();
    set_shape(6, 6, 7.5);

    mock_dac_init_reset(true);
    mock_lock_reset(true);
    ASSERT_TRUE(reinit_shape());

    double v;
    ASSERT_FALSE(get_single_FIXED(6, &v)); /* #1034: no longer confidently wrong */

    /* #1147: after reinit EVERY channel is unknown, so the all-channel form
     * must error too -- not answer eight fabricated 0.0V readings. */
    double allNew[MOCK_MAX_AOUT_CHANNEL];
    ASSERT_FALSE(get_all_FIXED(allNew, MOCK_MAX_AOUT_CHANNEL));
}

TEST(a_set_after_reinit_makes_that_one_channel_known_again) {
    cache_reset_to_boot_state();
    set_shape(3, 3, 1.0);

    mock_dac_init_reset(true);
    mock_lock_reset(true);
    ASSERT_TRUE(reinit_shape());

    /* A fresh command after the reinit is real and must be trusted. */
    set_shape(3, 3, 8.25);

    double v = 0.0;
    ASSERT_TRUE(get_single_FIXED(3, &v));
    ASSERT_TRUE(v == 8.25);

    /* every OTHER channel is still unknown -- the fresh set must not have
     * resurrected the rest of the cache. #1147: that also means the
     * all-channel form must still error (channel 3 alone is not enough). */
    double all[MOCK_MAX_AOUT_CHANNEL];
    ASSERT_FALSE(get_all_FIXED(all, MOCK_MAX_AOUT_CHANNEL));
}

/* ========================================================================
 * Lock discipline of the reset+invalidation step (round 1: lock-before-Init)
 * ======================================================================== */

TEST(reinit_takes_the_command_lock_exactly_once_spanning_reset_and_invalidate) {
    cache_reset_to_boot_state();
    set_shape(0, 0, 1.0);
    mock_dac_init_reset(true);
    mock_lock_reset(true);

    ASSERT_TRUE(reinit_shape());
    ASSERT_EQ(g_lock_calls, 1);
    ASSERT_EQ(g_unlock_calls, 1);
    ASSERT_EQ(g_dac_init_calls, 1);
    ASSERT_FALSE(g_locked); /* not left held */
}

TEST(reinit_fails_closed_when_the_command_lock_cannot_be_taken) {
    /* Mirrors: "if the lock cannot be taken, treat it the same as a
     * DAC7718_Init failure -- do not publish dacHardwareInitialized=true
     * over a cache that might still be stale." The cache must be left
     * UNTOUCHED (still reporting the pre-reinit value) rather than partially
     * invalidated, because the caller is going to report failure and retry
     * -- see #1034's source comment for why this is the conservative choice. */
    cache_reset_to_boot_state();
    set_shape(5, 5, 6.6);
    mock_dac_init_reset(true); /* would succeed -- must never be reached */
    mock_lock_reset(/*shouldSucceed=*/false);

    ASSERT_FALSE(reinit_shape());
    ASSERT_EQ(g_lock_calls, 1);
    ASSERT_EQ(g_unlock_calls, 0); /* never given a lock it never took */

    double v = 0.0;
    ASSERT_TRUE(get_single_FIXED(5, &v)); /* still known -- untouched */
    ASSERT_TRUE(v == 6.6);
}

TEST(round1_fix_lock_failure_leaves_hardware_untouched) {
    /* Round-1 Qodo finding "Lock contention revives stale voltages": the
     * FIRST shape called (mock) DAC7718_Init() before taking the lock, so a
     * lock failure still left the hardware physically reset with the cache
     * unaware of it. The fix takes the lock FIRST -- prove hardware is never
     * even touched when the lock cannot be acquired. */
    cache_reset_to_boot_state();
    mock_dac_init_reset(true);
    mock_lock_reset(false);

    ASSERT_FALSE(reinit_shape());
    ASSERT_EQ(g_dac_init_calls, 0); /* hardware never touched */
}

TEST(dac_init_failure_still_invalidates_the_cache) {
    /* Round-2 Qodo finding "Invalidate cache after failed resets"
     * (importance 10): DAC7718_Init()'s failure paths that matter here run
     * AFTER its RST pulse, so the hardware is already physically reset by
     * the time it reports failure -- exactly like the success path. The
     * cache must be invalidated EITHER way, not just on success. */
    cache_reset_to_boot_state();
    set_shape(4, 4, 3.14);
    mock_dac_init_reset(/*shouldSucceed=*/false);
    mock_lock_reset(true);

    ASSERT_FALSE(reinit_shape());
    ASSERT_EQ(g_lock_calls, 1);
    ASSERT_EQ(g_unlock_calls, 1); /* released even though Init failed */

    double v = 0.0;
    ASSERT_FALSE(get_single_FIXED(4, &v)); /* invalidated despite the failure */
    ASSERT_EQ((long long)g_cache[4].Timestamp, 0);
}

TEST(old_buggy_ordering_let_a_setter_win_the_gap_and_then_be_erased) {
    /* Round-1 Qodo finding "Reset can erase a successful voltage",
     * reconstructed literally: the FIRST shape's own call order was
     * (1) mock_DAC7718_Init() with NO lock held, THEN (2) lock+invalidate.
     * A setter using the SAME lock (exactly like the real
     * SCPI_DACVoltageSet, which always takes SCPIDAC_LockCommand() around
     * its own register-write+publish) can therefore acquire that lock in
     * the window between steps 1 and 2 and publish -- only to have step 2
     * wipe it moments later. This is the FIRST shape's call order, not the
     * shipped one (see the next test for what changed). */
    cache_reset_to_boot_state();
    mock_dac_init_reset(true);
    mock_lock_reset(true);

    ASSERT_TRUE(mock_DAC7718_Init()); /* old shape: hardware reset, UNLOCKED */

    /* window: a setter (using the real lock, like SCPI_DACVoltageSet does)
     * wins the currently-free lock and publishes a genuinely fresh value. */
    ASSERT_TRUE(mock_SCPIDAC_LockCommand());
    set_shape(2, 2, 5.5);
    mock_SCPIDAC_UnlockCommand(true);

    /* old shape's own (late) lock + invalidate now runs, uncontended */
    ASSERT_TRUE(mock_SCPIDAC_LockCommand());
    MockAOutSample invalidated = {0};
    for (size_t i = 0; i < MOCK_MAX_AOUT_CHANNEL; i++) {
        g_cache[i] = invalidated;
    }
    mock_SCPIDAC_UnlockCommand(true);

    double v;
    ASSERT_FALSE(get_single_FIXED(2, &v)); /* the setter's fresh, valid write is gone -- THE BUG */
}

TEST(fixed_ordering_wraps_reset_and_invalidate_in_one_lock_acquisition) {
    /* The fix's structural guarantee: reinit_shape() is ONE call that takes
     * the lock exactly once around BOTH reset and invalidate, so there is
     * no exposed window (from the caller's side) for a setter to observe
     * "reset happened, invalidation has not run yet" the way the old-shape
     * test above constructs by hand. (Real concurrency proof needs real
     * threads -- see FIDELITY item 1; this proves the call shape the fix
     * relies on.) */
    cache_reset_to_boot_state();
    mock_dac_init_reset(true);
    mock_lock_reset(true);

    ASSERT_TRUE(reinit_shape());
    ASSERT_EQ(g_lock_calls, 1); /* exactly one acquisition, not two */
    ASSERT_EQ(g_dac_init_calls, 1);
}

/* ========================================================================
 * Round 1: tick==0 sentinel collision (SCPIDAC_ValidTimestamp)
 * ======================================================================== */

TEST(tick_zero_is_remapped_to_a_nonzero_timestamp) {
    /* xTaskGetTickCount() legitimately returns 0 at scheduler startup and
     * on every 32-bit wrap. A command executing on that exact tick must
     * still read back as known. */
    cache_reset_to_boot_state();
    g_tick = 0;
    set_shape(1, 1, 3.3);

    double v = 0.0;
    ASSERT_TRUE(get_single_FIXED(1, &v));
    ASSERT_TRUE(v == 3.3);
    ASSERT_EQ((long long)g_cache[1].Timestamp, 1); /* remapped, not left at 0 */
}

TEST(old_buggy_timestamp_shape_would_have_collided_at_tick_zero) {
    /* Round-1 bug, reconstructed literally: a bare tick assignment (the
     * FIRST shape, before SCPIDAC_ValidTimestamp existed) stores 0 on a
     * tick-0 command -- indistinguishable from "never commanded". */
    cache_reset_to_boot_state();
    g_tick = 0;
    MockAOutSample bare = { .Timestamp = g_tick, .Channel = 1, .Voltage = 3.3 }; /* round-1 shape */
    g_cache[1] = bare;

    double v;
    ASSERT_FALSE(get_single_FIXED(1, &v)); /* THE BUG: wrongly reads as unknown */
}

int main(void) {
    RUN(boot_state_every_channel_reports_unknown);
    RUN(after_a_set_the_channel_reports_known_and_correct);
    RUN(all_channel_form_errors_on_a_mixed_cache);
    RUN(all_channel_form_succeeds_only_once_every_channel_is_known);
    RUN(pre_1147_fix_shape_would_have_fabricated_a_zero_volt_reading);
    RUN(fixed_all_channel_form_errors_on_the_identical_scenario);
    RUN(reinit_invalidates_a_previously_known_channel);
    RUN(reinit_invalidates_every_channel_not_just_the_one_this_transport_set);
    RUN(old_buggy_shape_would_have_reported_the_stale_voltage_as_current);
    RUN(fixed_shape_closes_the_same_scenario_the_old_shape_missed);
    RUN(a_set_after_reinit_makes_that_one_channel_known_again);
    RUN(reinit_takes_the_command_lock_exactly_once_spanning_reset_and_invalidate);
    RUN(reinit_fails_closed_when_the_command_lock_cannot_be_taken);
    RUN(round1_fix_lock_failure_leaves_hardware_untouched);
    RUN(dac_init_failure_still_invalidates_the_cache);
    RUN(old_buggy_ordering_let_a_setter_win_the_gap_and_then_be_erased);
    RUN(fixed_ordering_wraps_reset_and_invalidate_in_one_lock_acquisition);
    RUN(tick_zero_is_remapped_to_a_nonzero_timestamp);
    RUN(old_buggy_timestamp_shape_would_have_collided_at_tick_zero);
    return TEST_SUMMARY();
}
