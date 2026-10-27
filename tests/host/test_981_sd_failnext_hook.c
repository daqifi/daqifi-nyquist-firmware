/* ==========================================================================
 * test_981_sd_failnext_hook.c -- #981 SYSTem:STORage:SD:FAILNext one-shot
 * arm/consume semantics.
 *
 * WHY re-implemented rather than compiled from the real source. Like
 * test_943/test_953 in this directory, `sd_card_manager.c` is not includable
 * on a host: it pulls in FreeRTOS, Harmony's SYS_FS, and the whole SD card
 * state machine. So this file re-implements ONLY the one-shot arm/consume
 * shape -- a mock flag plus a mock "consume if armed" step matching the real
 * critical-section test-and-clear in `SDCardWrite()` -- and the Makefile
 * recipe GREPS the real source for the four properties this re-implementation
 * assumes still hold. A change to any of them invalidates this test's premise
 * and fails the build loudly rather than passing silently on a stale model:
 *
 *   1. `gFailNextWrite` exists as the flag.
 *   2. The consume site is `taskENTER_CRITICAL(); if (gFailNextWrite) {
 *      gFailNextWrite = false; ...} ... taskEXIT_CRITICAL();` in that order --
 *      a test-and-clear inside one critical section, not a plain clear
 *      outside one (see CLAUDE.md's atomicity rules: this is the load-bearing
 *      RMW-across-two-contexts case).
 *   3. `sd_card_manager_Init()` scrubs the flag to false OUTSIDE the
 *      `isInitDone` guard (the #409 retained-RAM rail -- a reset must always
 *      clear an outstanding arm, and the plain `= false` static initializer
 *      alone does not survive MCLR/IPE-flash on this MCU).
 *   4. `sd_card_manager_SetFailNextWrite()`'s body is a PLAIN store with no
 *      critical section (the atomicity argument opus reviewed for #981: it is
 *      a store that reads nothing first, so it is not an RMW and needs none).
 *
 * WHAT THIS PROVES, and what it does NOT. The re-implemented model proves the
 * ONE-SHOT CONTRACT is internally consistent (arm, consume-once, re-arm,
 * disarm-without-consuming) and that the four guarded properties hold in the
 * real source RIGHT NOW. It does NOT exercise the real hardware write path,
 * the real critical section's effect on interrupt masking, or which of
 * `SDCardWrite()`'s five call sites consumes a given arm in practice -- that
 * needs a real device and is `test_981_sd_failnext_hook.py`
 * (daqifi-python-test-suite)'s job.
 * ========================================================================== */
#include "test_framework.h"
#include <stdbool.h>

/* ---- Mock model of gFailNextWrite + its consume site ------------------- */

static bool mock_armed = false;

/* Mirrors SDCardWrite()'s test-and-clear: single-threaded here (host test),
 * so no real critical section is needed to reproduce the OBSERVABLE
 * semantics -- what matters for this model is that arm and clear are the
 * same atomic step, which a single-threaded test-and-clear already is. */
static bool mock_consume_if_armed(void) {
    bool fired = false;
    if (mock_armed) {
        mock_armed = false;
        fired = true;
    }
    return fired;
}

static void mock_set_arm(bool arm) {
    mock_armed = arm;   /* plain store, mirrors the real setter */
}

/* ---- Tests -------------------------------------------------------------- */

TEST(starts_disarmed) {
    mock_armed = false;
    ASSERT_FALSE(mock_armed);
    ASSERT_FALSE(mock_consume_if_armed());
}

TEST(arm_then_consume_is_one_shot) {
    mock_set_arm(true);
    ASSERT_TRUE(mock_armed);
    ASSERT_TRUE(mock_consume_if_armed());   /* first write: fires */
    ASSERT_FALSE(mock_armed);
    ASSERT_FALSE(mock_consume_if_armed());  /* second write: does not */
}

TEST(disarm_without_consuming_prevents_the_next_write_failing) {
    mock_set_arm(true);
    mock_set_arm(false);                    /* explicit disarm, no write yet */
    ASSERT_FALSE(mock_armed);
    ASSERT_FALSE(mock_consume_if_armed());  /* next write: clean */
}

TEST(rearm_after_consume_works_again) {
    mock_set_arm(true);
    ASSERT_TRUE(mock_consume_if_armed());
    mock_set_arm(true);
    ASSERT_TRUE(mock_consume_if_armed());
}

TEST(idle_disarm_is_a_no_op) {
    mock_armed = false;
    mock_set_arm(false);
    ASSERT_FALSE(mock_armed);
}

int main(void) {
    RUN(starts_disarmed);
    RUN(arm_then_consume_is_one_shot);
    RUN(disarm_without_consuming_prevents_the_next_write_failing);
    RUN(rearm_after_consume_works_again);
    RUN(idle_disarm_is_a_no_op);
    return TEST_SUMMARY();
}
