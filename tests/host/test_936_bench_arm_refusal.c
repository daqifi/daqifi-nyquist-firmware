/* ==========================================================================
 * test_936_bench_arm_refusal.c -- issue #936
 *
 * WHAT IS UNDER TEST
 *
 * SYST:STOR:SD:BENCHmark (SCPI_StorageSDBenchmark, SCPIStorageSD.c) arms a
 * plain write through sd_card_manager_UpdateSettingsForPlainWrite(), which can
 * be REFUSED by the same #589 suspend gate every other SD-arming command is
 * guarded by: WiFi streaming owns SPI4, a WiFi firmware update is running, or
 * the bus is quarantined after a jam (app_SDCard_SpiOwnedByWifi(),
 * app_freertos.c).
 *
 * Until #936 that return was DISCARDED. The callback carried on into the
 * "wait for the file to be open" loop, which -- nothing having been armed --
 * could only burn its full timeout and then blame the wrong thing: a
 * "card is likely SPI-mode incompatible" diagnosis for what is actually the SD
 * task not running at all.
 *
 * ⛔ THE PRE-FIX SOURCE DOCUMENTED THE BEHAVIOUR IT DID NOT PERFORM. At
 * 4e5e5c592^ this function already opened with "#589: the benchmark arms a
 * WRITE like any other SD operation, so it is refused while the SD task is
 * suspended" -- a comment asserting the refusal, above code that threw the
 * refusal away. That is why a shape test is worth more here than a reading of
 * the file: the file says the right thing either way.
 *
 * WHY THIS IS A HOST TEST AND NOT A DEVICE TEST
 *
 * It cannot be a device test, and that is a property of the firmware rather
 * than of the bench. The suspension has three causes. A WiFi firmware update
 * has no injection hook. The quarantine has no setter -- every registered
 * SYSTem:DIAGnostic:SPIBus* pattern is a GETTER. And the remaining cause, a
 * live WiFi stream, cannot work: SCPI_StorageSDBenchmark refuses earlier, at
 * its #854 SD_StreamingIsLive() check (`IsEnabled || Running`, any interface),
 * which sits above even parameter parsing. The stream that creates the
 * suspension is the same thing that stops BENCHmark ever reaching the arm, so
 * a device test would show identical fast #854 refusals on both images and
 * prove nothing about #936.
 *
 * Two sources in this repo already say so, independently of this file:
 *   * tools/lint/scpi_sd_arm_path.py, under "Non-goal", excludes
 *     SCPI_StorageSDBenchmark from its arm-call-site census precisely because
 *     it open-codes the shape and arms through ...ForPlainWrite();
 *   * test_953_bench_suspend_diagnosis.c says of "the identical race shape in
 *     the sibling ticket #936" that it needs "a higher-priority event to land
 *     inside a window a test cannot aim at".
 *
 * WHAT THIS FILE ASSERTS, AND WHAT THE MAKEFILE ASSERTS
 *
 * SCPIStorageSD.c is not host-compilable (FreeRTOS, libscpi, Harmony, the
 * whole board graph), so this file re-implements the refusal branch's SHAPE
 * against mocks -- the same technique as $(BENCH_BIN)/test_943 and
 * test_953_bench_suspend_diagnosis.c. A model is a COPY, and a copy drifts, so
 * the two-sidedness does NOT come from this file: it comes from the greps on
 * this target in tests/host/Makefile, which read the REAL source and fail the
 * BUILD if the arm's return is discarded again or the refusal branch stops
 * reporting. This file proves the shape is coherent and ordered; the pins
 * prove the shipped source still has it.
 *
 * The five obligations of the refused path, which the real code performs in
 * this order:
 *   1. clear `mode` back to MODE_NONE -- UNDER the claim (#955);
 *   2. release the claim -- AFTER that clear, never before;
 *   3. log WHY, from SD_SuspendReasonText(), not a card diagnosis;
 *   4. push an SCPI execution error;
 *   5. leave via __exit_point WITHOUT entering the file-ready wait loop.
 *
 * Order is the point of 1 and 2, so the mocks record a single ordered trace
 * rather than per-flag booleans: a set of booleans cannot tell a correct
 * clear-then-release from a release-then-clear, and release-then-clear is the
 * bug #955 fixed in the neighbouring arms.
 * ========================================================================== */
#include "test_framework.h"
#include <stdbool.h>
#include <string.h>

/* ---- Mocks -------------------------------------------------------------- */

/* One ordered trace, not a set of flags. The #955 ordering property is only
 * expressible as a sequence, and a boolean-per-effect model is exactly the
 * model that cannot fail when the order regresses. */
#define TRACE_MAX 16
static const char *trace[TRACE_MAX];
static int trace_n;

static void record(const char *what) {
    if (trace_n < TRACE_MAX) {
        trace[trace_n++] = what;
    }
}

static int trace_index(const char *what) {
    for (int i = 0; i < trace_n; i++) {
        if (strcmp(trace[i], what) == 0) {
            return i;
        }
    }
    return -1;
}

/* Stands in for the runtime config's `mode` field. */
#define MODE_NONE  0
#define MODE_WRITE 2
static int mock_mode;

static bool mock_claim_held;
static bool mock_arm_succeeds;
static bool mock_wait_loop_entered;
static const char *mock_suspend_reason;
static int mock_scpi_error;

#define SCPI_ERROR_EXECUTION_ERROR (-200)

static void mock_take_claim(void) {
    mock_claim_held = true;
    record("claim");
}

static void mock_release_claim(void) {
    mock_claim_held = false;
    record("release");
}

static void mock_set_mode(int mode) {
    mock_mode = mode;
    record(mode == MODE_NONE ? "mode_none" : "mode_write");
}

/* Stands in for sd_card_manager_UpdateSettingsForPlainWrite(). The real one
 * resets `mode` itself before returning false (its own #589 gate); modelled,
 * because the production code's explicit clear is deliberately NOT allowed to
 * depend on that -- see the pin on the clear in the Makefile. */
static bool mock_arm_plain_write(void) {
    record("arm");
    if (!mock_arm_succeeds) {
        mock_mode = MODE_NONE;
        return false;
    }
    return true;
}

static const char *mock_suspend_reason_text(void) {
    return mock_suspend_reason;
}

static void mock_log_refusal(const char *why) {
    record(why ? "log_reason" : "log_fallback");
}

static void mock_push_error(int code) {
    mock_scpi_error = code;
    record("scpi_error");
}

static void mock_enter_wait_loop(void) {
    mock_wait_loop_entered = true;
    record("wait_loop");
}

static void reset_mocks(void) {
    trace_n = 0;
    mock_mode = MODE_NONE;
    mock_claim_held = false;
    mock_arm_succeeds = true;
    mock_wait_loop_entered = false;
    mock_suspend_reason = "WiFi streaming owns SPI4";
    mock_scpi_error = 0;
}

/* ---- The shape under test ----------------------------------------------- */

/* SCPI_StorageSDBenchmark's arm-and-check sequence, from the mode=WRITE write
 * down to either the refusal exit or the wait loop. Returns false for
 * SCPI_RES_ERR. */
static bool bench_arm_sequence(void) {
    mock_take_claim();
    mock_set_mode(MODE_WRITE);

    const bool bench_armed = mock_arm_plain_write();

    if (!bench_armed) {
        /* #955: clear under the claim, THEN release. */
        mock_set_mode(MODE_NONE);
        mock_release_claim();

        const char *why = mock_suspend_reason_text();
        mock_log_refusal(why);
        mock_push_error(SCPI_ERROR_EXECUTION_ERROR);
        return false;                   /* goto __exit_point */
    }

    /* The success path releases too -- ownership moved from the claim flag to
     * `mode`, which keeps IsBusy() true, so there is no gap. */
    mock_release_claim();
    mock_enter_wait_loop();
    return true;
}

/* The PRE-#936 shape, kept so the assertions below are demonstrably capable of
 * failing. Identical but for discarding the arm's return -- which is the whole
 * defect. Nothing in production resembles this; it exists so that every
 * assertion about the fixed shape has a counterexample in the same build. */
static bool bench_arm_sequence_pre_936(void) {
    mock_take_claim();
    mock_set_mode(MODE_WRITE);
    (void)mock_arm_plain_write();       /* #936: the discarded return */
    mock_release_claim();
    mock_enter_wait_loop();
    return true;
}

/* ---- Tests -------------------------------------------------------------- */

TEST(a_successful_arm_proceeds_to_the_wait_loop) {
    reset_mocks();
    ASSERT_TRUE(bench_arm_sequence());
    ASSERT_TRUE(mock_wait_loop_entered);
    ASSERT_EQ(mock_mode, MODE_WRITE);
    ASSERT_FALSE(mock_claim_held);
    ASSERT_EQ(mock_scpi_error, 0);
}

TEST(a_refused_arm_reports_an_error_and_never_waits) {
    reset_mocks();
    mock_arm_succeeds = false;
    ASSERT_FALSE(bench_arm_sequence());
    /* The defect was not an absent error -- it was burning a full timeout and
     * then blaming the card. Not entering the loop IS the fix. */
    ASSERT_FALSE(mock_wait_loop_entered);
    ASSERT_EQ(mock_scpi_error, SCPI_ERROR_EXECUTION_ERROR);
}

TEST(a_refused_arm_clears_mode_and_releases_the_claim) {
    reset_mocks();
    mock_arm_succeeds = false;
    ASSERT_FALSE(bench_arm_sequence());
    ASSERT_EQ(mock_mode, MODE_NONE);
    ASSERT_FALSE(mock_claim_held);
}

TEST(the_clear_happens_UNDER_the_claim_not_after_the_release) {
    /* #955. A release-then-clear leaves MODE_WRITE visible for a window with
     * the claim already gone, so another arm can see a busy manager that owns
     * nothing. Expressible only as an order, which is why the mocks trace. */
    reset_mocks();
    mock_arm_succeeds = false;
    ASSERT_FALSE(bench_arm_sequence());
    const int clear = trace_index("mode_none");
    const int release = trace_index("release");
    ASSERT_TRUE(clear >= 0);
    ASSERT_TRUE(release >= 0);
    ASSERT_TRUE(clear < release);
}

TEST(the_refusal_names_the_suspend_reason_not_the_card) {
    reset_mocks();
    mock_arm_succeeds = false;
    ASSERT_FALSE(bench_arm_sequence());
    ASSERT_TRUE(trace_index("log_reason") >= 0);
}

TEST(a_missing_reason_string_still_logs_rather_than_dereferencing_it) {
    /* The real code is `why ? why : "the SD task is not accepting work"`.
     * SD_SuspendReasonText() returning NULL must not become a crash or a
     * silent skip. */
    reset_mocks();
    mock_arm_succeeds = false;
    mock_suspend_reason = NULL;
    ASSERT_FALSE(bench_arm_sequence());
    ASSERT_TRUE(trace_index("log_fallback") >= 0);
    ASSERT_EQ(mock_scpi_error, SCPI_ERROR_EXECUTION_ERROR);
}

TEST(the_claim_is_released_on_BOTH_paths) {
    /* If a later edit moves the release into the success arm only, the refused
     * path leaks the claim and wedges the manager permanently: IsBusy() stays
     * true for every later SD command, and the #925 watchdog's own TryClaim
     * then fails forever too. Both paths, one test. */
    reset_mocks();
    ASSERT_TRUE(bench_arm_sequence());
    ASSERT_FALSE(mock_claim_held);

    reset_mocks();
    mock_arm_succeeds = false;
    ASSERT_FALSE(bench_arm_sequence());
    ASSERT_FALSE(mock_claim_held);
}

TEST(the_pre_936_shape_FAILS_these_assertions) {
    /* ⛔ THE NEGATIVE CONTROL. Without it every assertion above is compatible
     * with a test that cannot fail, and this suite's own history says a
     * passing run is not evidence until the failing run exists. The pre-#936
     * shape discards the arm's return, so a refused arm reaches the wait loop
     * with no error pushed -- exactly what shipped before the fix. */
    reset_mocks();
    mock_arm_succeeds = false;
    ASSERT_TRUE(bench_arm_sequence_pre_936());
    ASSERT_TRUE(mock_wait_loop_entered);        /* the burned timeout */
    ASSERT_EQ(mock_scpi_error, 0);              /* nothing reported */
    ASSERT_TRUE(trace_index("scpi_error") < 0);
    ASSERT_TRUE(trace_index("log_reason") < 0);
}

int main(void) {
    RUN(a_successful_arm_proceeds_to_the_wait_loop);
    RUN(a_refused_arm_reports_an_error_and_never_waits);
    RUN(a_refused_arm_clears_mode_and_releases_the_claim);
    RUN(the_clear_happens_UNDER_the_claim_not_after_the_release);
    RUN(the_refusal_names_the_suspend_reason_not_the_card);
    RUN(a_missing_reason_string_still_logs_rather_than_dereferencing_it);
    RUN(the_claim_is_released_on_BOTH_paths);
    RUN(the_pre_936_shape_FAILS_these_assertions);
    return TEST_SUMMARY();
}
