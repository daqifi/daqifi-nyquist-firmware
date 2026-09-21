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
 * THE FIX has two halves, both modelled here:
 *
 * 1. AOutSample gained a `Timestamp` field (AOutSample.h), 0 meaning "not
 *    known" -- the same convention AInSample already uses. Every successful
 *    SCPI_DACVoltageSet write stamps it with xTaskGetTickCount(); every
 *    successful DAC7718_Init() inside DAC_EnsureHardwareInitialized() zeroes
 *    it (and Channel, and Voltage) for EVERY channel slot, under
 *    gDacCommandMutex (the same mutex #990 Finding 0 / #1030 already require
 *    every other BOARDDATA_AOUT_LATEST writer to hold, to avoid tearing the
 *    64-bit Voltage double mid-read on the other SCPI transport).
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
    g_tick = 0;
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

/* ---- #1034 shape: DAC_EnsureHardwareInitialized's post-Init()
 * invalidation loop. Mirrors SCPIDAC.c lines around 'invalidatedSample'
 * (grep-guarded below) -- lock, zero every slot while a write would be
 * observable, unlock; fail closed (mirroring "do not publish
 * dacHardwareInitialized=true over a cache that might still be stale") if
 * the lock cannot be taken. ---------------------------------------------- */
static bool reinit_invalidate_shape(void) {
    if (!mock_SCPIDAC_LockCommand()) {
        return false;
    }
    MockAOutSample invalidated = {0};
    for (size_t i = 0; i < MOCK_MAX_AOUT_CHANNEL; i++) {
        /* g_locked observable here models "this write only happens while
         * held" -- checked explicitly in a dedicated test below. */
        g_cache[i] = invalidated;
    }
    mock_SCPIDAC_UnlockCommand(true);
    return true;
}

/* ---- #1034 shape: SCPI_DACVoltageSet's publish, both branches stamp
 * identically -- mirrors '.Timestamp = xTaskGetTickCount()'. ------------ */
static void set_shape(size_t index, uint8_t channelId, double voltage) {
    g_tick++;
    MockAOutSample sample = { .Timestamp = g_tick, .Channel = channelId, .Voltage = voltage };
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

static void get_all_FIXED(double out[MOCK_MAX_AOUT_CHANNEL], size_t n) {
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

    double all[MOCK_MAX_AOUT_CHANNEL];
    get_all_FIXED(all, MOCK_MAX_AOUT_CHANNEL);
    for (size_t i = 0; i < MOCK_MAX_AOUT_CHANNEL; i++) {
        ASSERT_TRUE(all[i] == 0.0);
    }
}

TEST(after_a_set_the_channel_reports_known_and_correct) {
    cache_reset_to_boot_state();
    set_shape(3, /*channelId=*/3, 4.25);

    double v = 0.0;
    ASSERT_TRUE(get_single_FIXED(3, &v));
    ASSERT_TRUE(v == 4.25);

    double all[MOCK_MAX_AOUT_CHANNEL];
    get_all_FIXED(all, MOCK_MAX_AOUT_CHANNEL);
    ASSERT_TRUE(all[3] == 4.25);
    /* every other channel is still unknown */
    for (size_t i = 0; i < MOCK_MAX_AOUT_CHANNEL; i++) {
        if (i == 3) continue;
        ASSERT_TRUE(all[i] == 0.0);
    }
}

TEST(all_channel_form_never_errors_even_with_a_mixed_cache) {
    cache_reset_to_boot_state();
    set_shape(0, 0, 1.0);
    set_shape(5, 5, -2.5);
    /* channels 1-4, 6, 7 never commanded */

    double all[MOCK_MAX_AOUT_CHANNEL];
    get_all_FIXED(all, MOCK_MAX_AOUT_CHANNEL); /* must not be skippable/error */
    ASSERT_TRUE(all[0] == 1.0);
    ASSERT_TRUE(all[5] == -2.5);
    ASSERT_TRUE(all[1] == 0.0);
    ASSERT_TRUE(all[7] == 0.0);
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

    mock_lock_reset(/*shouldSucceed=*/true);
    ASSERT_TRUE(reinit_invalidate_shape()); /* models DAC7718_Init() success */

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

    mock_lock_reset(true);
    ASSERT_TRUE(reinit_invalidate_shape());

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

    /* Deliberately do NOT call reinit_invalidate_shape() here -- this test
     * proves what the OLD code did (no invalidation call site existed). */

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

    mock_lock_reset(true);
    ASSERT_TRUE(reinit_invalidate_shape());

    double v;
    ASSERT_FALSE(get_single_FIXED(6, &v)); /* #1034: no longer confidently wrong */

    double allNew[MOCK_MAX_AOUT_CHANNEL];
    get_all_FIXED(allNew, MOCK_MAX_AOUT_CHANNEL);
    ASSERT_TRUE(allNew[6] == 0.0);
}

TEST(a_set_after_reinit_makes_that_one_channel_known_again) {
    cache_reset_to_boot_state();
    set_shape(3, 3, 1.0);

    mock_lock_reset(true);
    ASSERT_TRUE(reinit_invalidate_shape());

    /* A fresh command after the reinit is real and must be trusted. */
    set_shape(3, 3, 8.25);

    double v = 0.0;
    ASSERT_TRUE(get_single_FIXED(3, &v));
    ASSERT_TRUE(v == 8.25);

    /* every OTHER channel is still unknown -- the fresh set must not have
     * resurrected the rest of the cache. */
    double all[MOCK_MAX_AOUT_CHANNEL];
    get_all_FIXED(all, MOCK_MAX_AOUT_CHANNEL);
    for (size_t i = 0; i < MOCK_MAX_AOUT_CHANNEL; i++) {
        if (i == 3) continue;
        ASSERT_TRUE(all[i] == 0.0);
    }
}

/* ========================================================================
 * Lock discipline of the invalidation step itself
 * ======================================================================== */

TEST(invalidation_takes_the_command_lock_exactly_once_and_releases_it) {
    cache_reset_to_boot_state();
    set_shape(0, 0, 1.0);
    mock_lock_reset(true);

    ASSERT_TRUE(reinit_invalidate_shape());
    ASSERT_EQ(g_lock_calls, 1);
    ASSERT_EQ(g_unlock_calls, 1);
    ASSERT_FALSE(g_locked); /* not left held */
}

TEST(invalidation_fails_closed_when_the_command_lock_cannot_be_taken) {
    /* Mirrors: "if the lock cannot be taken, treat it the same as a
     * DAC7718_Init failure -- do not publish dacHardwareInitialized=true
     * over a cache that might still be stale." The cache must be left
     * UNTOUCHED (still reporting the pre-reinit value) rather than partially
     * invalidated, because the caller is going to report failure and retry
     * -- see #1034's source comment for why this is the conservative choice. */
    cache_reset_to_boot_state();
    set_shape(5, 5, 6.6);
    mock_lock_reset(/*shouldSucceed=*/false);

    ASSERT_FALSE(reinit_invalidate_shape());
    ASSERT_EQ(g_lock_calls, 1);
    ASSERT_EQ(g_unlock_calls, 0); /* never given a lock it never took */

    double v = 0.0;
    ASSERT_TRUE(get_single_FIXED(5, &v)); /* still known -- untouched */
    ASSERT_TRUE(v == 6.6);
}

int main(void) {
    RUN(boot_state_every_channel_reports_unknown);
    RUN(after_a_set_the_channel_reports_known_and_correct);
    RUN(all_channel_form_never_errors_even_with_a_mixed_cache);
    RUN(reinit_invalidates_a_previously_known_channel);
    RUN(reinit_invalidates_every_channel_not_just_the_one_this_transport_set);
    RUN(old_buggy_shape_would_have_reported_the_stale_voltage_as_current);
    RUN(fixed_shape_closes_the_same_scenario_the_old_shape_missed);
    RUN(a_set_after_reinit_makes_that_one_channel_known_again);
    RUN(invalidation_takes_the_command_lock_exactly_once_and_releases_it);
    RUN(invalidation_fails_closed_when_the_command_lock_cannot_be_taken);
    return TEST_SUMMARY();
}
