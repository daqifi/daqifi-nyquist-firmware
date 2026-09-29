/* ==========================================================================
 * test_1125_log_once_claim.c -- issue #1125
 *
 * WHAT IS UNDER TEST
 *
 * firmware/src/Util/Logger.c's Logger_OneShotClaim() -- THE REAL ONE. This
 * file does not #include Logger.c (it pulls in <xc.h>, SYSKEY, PIC32
 * __builtin_* intrinsics and Harmony's clock_config.h, none of which are
 * host-buildable -- the same reason test_1000_sd_log_arm_budget.c extracts
 * rather than includes). The Makefile target below extracts
 * Logger_OneShotClaim's body verbatim out of Logger.c into
 * gen_1125_log_once_claim.h, which this file #includes, so the function
 * compiled here is byte-for-byte what ships, not a re-implementation of its
 * shape.
 *
 * THE DEFECT THIS REPLACES
 *
 * Before #1125, LOG_E_ONCE/LOG_I_ONCE/LOG_D_ONCE claimed their bit with a
 * bare `gLogOneShot |= (1u << bit)`. That is a load, an OR and a store, and
 * gLogOneShot is shared across tasks AND interrupt contexts (LOG_x_ONCE is
 * documented ISR-callable, unlike its task-only sibling LOG_x_SESSION). A
 * preemption between the load and the store lets a higher-priority context
 * claim its OWN bit; the resumed store then writes back the pre-preemption
 * snapshot and ERASES that bit. The message for the bit that won the race
 * already printed (LogMessage runs after the set in every version, so
 * nothing is lost there) -- what tears is the SUPPRESSION STATE, so that
 * call site's "at most once" contract breaks and it can fire again. For the
 * documented use (an ISR flood guard), a defeated suppression means exactly
 * the flood the macro exists to prevent, not a quietly missing line.
 *
 * Logger_OneShotClaim() replaces the bare `|=` with a test-and-set entirely
 * inside one context-appropriate critical section (taskENTER_CRITICAL for a
 * task caller, taskENTER_CRITICAL_FROM_ISR for an ISR caller -- picking the
 * wrong one is a real hazard: the PIC32MZ port's vTaskEnterCritical asserts
 * uxInterruptNesting == 0, so calling the task-only form from an ISR is not
 * merely wrong, it is a hard fault). Mirrors the fix #1028 gave the sibling
 * gSessionOneShot race (Logger_SessionOneShotClaim), extended with the ISR
 * variant that race did not need.
 *
 * WHAT THIS SUITE PROVES, AND WHAT IT CANNOT
 *
 * A single-threaded host process cannot literally suspend mid-function and
 * resume inside another context's write, so nothing here "reproduces the
 * race" against the real Logger_OneShotClaim -- there is no such window to
 * reproduce once the whole test-and-set sits inside one critical section.
 * What IS provable on a host:
 *
 *   1. FUNCTIONAL correctness of the claim contract: exactly one caller per
 *      bit sees `true`, every bit is independent, out-of-range bits are
 *      rejected without touching the mask, and a reset re-arms every bit.
 *   2. DISPATCH correctness: a task-context call takes exactly the task
 *      critical section and never the ISR one, and vice versa -- proven by
 *      instrumented mocks that count entries/exits.
 *   3. STRUCTURALLY (in the Makefile recipe below, mirroring
 *      tools/lint/scpi_claim_path.py's own "exactly one opener, one closer,
 *      test+set positioned between them" reasoning for this identical class
 *      of property) that the extracted body holds exactly one of each
 *      critical-section call, and that both of its test-and-set pairs sit
 *      strictly BETWEEN their matching enter/exit -- refused, not silently
 *      passed, on any other count or a torn positional order. That is what
 *      actually rules the race out: a real critical section makes the
 *      interleaving unrepresentable, which is a claim about POSITION, not
 *      about behaviour a host thread can exercise.
 *   4. ILLUSTRATIVELY (bare_or_equals_shape_loses_a_bit... below, NOT
 *      extracted from any shipped source), that the shape being replaced --
 *      an unguarded load/or/store with another write landing in the gap --
 *      really does erase a bit. This motivates the fix; it is not what
 *      gates this suite.
 * ========================================================================== */

#include "test_framework.h"
#include <stdint.h>
#include <stdbool.h>

/* ── Mocked FreeRTOS critical-section primitives ─────────────────────────
 * Logger_OneShotClaim (extracted below) calls these four by name and
 * declares a local of type UBaseType_t to hold the ISR variant's saved
 * status. Each mock counts its own calls so task_context_uses_task_critical_
 * section / isr_context_uses_isr_critical_section can assert dispatch went
 * to the right pair and ONLY that pair. */
typedef unsigned int UBaseType_t;

static int g_enter_task_calls = 0;
static int g_exit_task_calls  = 0;
static int g_enter_isr_calls  = 0;
static int g_exit_isr_calls   = 0;
static UBaseType_t g_last_isr_saved_seen_on_exit = 0;

static void taskENTER_CRITICAL(void) {
    g_enter_task_calls++;
}
static void taskEXIT_CRITICAL(void) {
    g_exit_task_calls++;
}
static UBaseType_t taskENTER_CRITICAL_FROM_ISR(void) {
    g_enter_isr_calls++;
    /* Distinct non-zero sentinel so a test can confirm the value THIS entry
     * returned is the one the matching exit is called with -- the real port
     * plumbs a genuine saved interrupt mask through this pair. */
    return 0xA5A5u + (UBaseType_t)g_enter_isr_calls;
}
static void taskEXIT_CRITICAL_FROM_ISR(UBaseType_t saved) {
    g_exit_isr_calls++;
    g_last_isr_saved_seen_on_exit = saved;
}

static void reset_critical_section_counters(void) {
    g_enter_task_calls = 0;
    g_exit_task_calls  = 0;
    g_enter_isr_calls  = 0;
    g_exit_isr_calls   = 0;
    g_last_isr_saved_seen_on_exit = 0;
}

/* Mocked context switch. The real LogIsInISR() reads FreeRTOS's port-global
 * uxInterruptNesting; this is the host stand-in the test sets directly. */
static bool g_in_isr = false;
static bool LogIsInISR(void) {
    return g_in_isr;
}

/* The real global Logger_OneShotClaim operates on -- same name, same type,
 * so the extracted body below (which references `gLogOneShot` and `mask`
 * exactly as written in Logger.c) resolves against this one. */
volatile uint32_t gLogOneShot = 0;

/* THE REAL FUNCTION, extracted verbatim by the Makefile recipe below. */
#include "gen_1125_log_once_claim.h"

/* ==========================================================================
 * Functional correctness
 * ========================================================================== */

TEST(claim_once_per_bit) {
    gLogOneShot = 0;
    ASSERT_TRUE(Logger_OneShotClaim(3));   /* first caller wins */
    ASSERT_FALSE(Logger_OneShotClaim(3));  /* second caller loses */
    ASSERT_FALSE(Logger_OneShotClaim(3));  /* still loses -- not a one-time fluke */
    ASSERT_EQ(gLogOneShot, (1u << 3));
}

TEST(bits_are_independent) {
    gLogOneShot = 0;
    ASSERT_TRUE(Logger_OneShotClaim(0));
    ASSERT_TRUE(Logger_OneShotClaim(1));   /* a different bit is unaffected by bit 0's claim */
    ASSERT_FALSE(Logger_OneShotClaim(0));  /* but bit 0 itself stays claimed */
    ASSERT_TRUE(Logger_OneShotClaim(2));
    ASSERT_EQ(gLogOneShot, (1u << 0) | (1u << 1) | (1u << 2));
}

TEST(all_32_bits_independently_claimable_exactly_once) {
    gLogOneShot = 0;
    for (uint32_t b = 0; b < 32u; b++) {
        ASSERT_TRUE(Logger_OneShotClaim(b));
    }
    ASSERT_EQ(gLogOneShot, 0xFFFFFFFFu);
    for (uint32_t b = 0; b < 32u; b++) {
        ASSERT_FALSE(Logger_OneShotClaim(b));  /* every bit now refuses a second claim */
    }
}

TEST(out_of_range_bit_rejected_without_touching_the_mask) {
    gLogOneShot = 0;
    ASSERT_FALSE(Logger_OneShotClaim(32));   /* one past the top valid bit */
    ASSERT_FALSE(Logger_OneShotClaim(33));
    ASSERT_FALSE(Logger_OneShotClaim(0xFFFFFFFFu));
    ASSERT_EQ(gLogOneShot, 0u);  /* an out-of-range claim must not corrupt real bits */
}

TEST(reset_re_arms_a_claimed_bit) {
    gLogOneShot = 0;
    ASSERT_TRUE(Logger_OneShotClaim(5));
    ASSERT_FALSE(Logger_OneShotClaim(5));
    gLogOneShot = 0;  /* Logger_ResetOneShots()'s own body: a single 32-bit write */
    ASSERT_TRUE(Logger_OneShotClaim(5));  /* reclaimable again after reset */
}

TEST(reset_does_not_disturb_an_unrelated_bit_left_by_the_test_before_it) {
    /* Guards against a shared-global test-ordering mistake: every test above
     * resets gLogOneShot itself, but this one deliberately does NOT, to
     * confirm Logger_OneShotClaim never reaches past its own bit. */
    gLogOneShot = (1u << 9);  /* simulate some other bit already claimed */
    ASSERT_TRUE(Logger_OneShotClaim(10));
    ASSERT_TRUE((gLogOneShot & (1u << 9)) != 0);   /* untouched */
    ASSERT_TRUE((gLogOneShot & (1u << 10)) != 0);  /* newly claimed */
    gLogOneShot = 0;
}

/* ==========================================================================
 * Dispatch correctness -- proves LogIsInISR() actually steers to the
 * context-legal critical-section pair, and ONLY that pair. Getting this
 * backwards is not cosmetic: calling the task-only taskENTER_CRITICAL from
 * an ISR is a hard fault on this port (vTaskEnterCritical asserts
 * uxInterruptNesting == 0).
 * ========================================================================== */

TEST(task_context_uses_task_critical_section_only) {
    gLogOneShot = 0;
    g_in_isr = false;
    reset_critical_section_counters();

    ASSERT_TRUE(Logger_OneShotClaim(7));

    ASSERT_EQ(g_enter_task_calls, 1);
    ASSERT_EQ(g_exit_task_calls, 1);
    ASSERT_EQ(g_enter_isr_calls, 0);
    ASSERT_EQ(g_exit_isr_calls, 0);
}

TEST(isr_context_uses_isr_critical_section_only) {
    gLogOneShot = 0;
    g_in_isr = true;
    reset_critical_section_counters();

    ASSERT_TRUE(Logger_OneShotClaim(11));

    ASSERT_EQ(g_enter_isr_calls, 1);
    ASSERT_EQ(g_exit_isr_calls, 1);
    ASSERT_EQ(g_enter_task_calls, 0);
    ASSERT_EQ(g_exit_task_calls, 0);

    g_in_isr = false;  /* restore for tests that follow */
}

TEST(isr_exit_receives_the_exact_value_its_own_entry_returned) {
    /* The real port's taskEXIT_CRITICAL_FROM_ISR must be handed the value
     * ITS OWN taskENTER_CRITICAL_FROM_ISR returned, not a stale or shared
     * one -- mismatching the saved interrupt mask across an unrelated pair
     * is its own hazard class. The mock's entry return value is distinct per
     * call (see its definition), so this actually distinguishes "handed the
     * right value" from "handed a compile-time constant". */
    gLogOneShot = 0;
    g_in_isr = true;
    reset_critical_section_counters();

    ASSERT_TRUE(Logger_OneShotClaim(13));
    ASSERT_EQ(g_last_isr_saved_seen_on_exit, 0xA5A5u + 1u);

    g_in_isr = false;
}

TEST(already_claimed_bit_still_enters_and_exits_its_critical_section_every_call) {
    /* The fast pre-check that skips the critical section entirely lives in
     * the LOG_x_ONCE macro (outside what this test compiles), not inside
     * Logger_OneShotClaim itself -- every direct call to the claim function
     * pays for the critical section, even on a bit that is already set, and
     * simply returns false from inside it. Confirms the claim function does
     * not itself short-circuit before entering. */
    gLogOneShot = 0;
    g_in_isr = false;
    ASSERT_TRUE(Logger_OneShotClaim(4));

    reset_critical_section_counters();
    ASSERT_FALSE(Logger_OneShotClaim(4));  /* already claimed */
    ASSERT_EQ(g_enter_task_calls, 1);
    ASSERT_EQ(g_exit_task_calls, 1);
}

/* ==========================================================================
 * Illustrative only -- NOT extracted from Logger.c and not what any shipped
 * version ran. Reconstructs the bare `|=` shape LOG_x_ONCE used before
 * #1125, and manually interleaves two claims the way a preemption between
 * an unguarded RMW's load and store would, to demonstrate what that shape
 * was vulnerable to. Explains WHY the fix matters; the suite's pass/fail
 * does not depend on this test existing -- the functional and dispatch
 * tests above are what's asserted against the real, extracted function.
 * ========================================================================== */

static uint32_t bare_or_equals_two_context_race_demo(void) {
    uint32_t shared = 0;

    /* Context A (say, streaming_Task) begins `shared |= (1u << 3)`: the load
     * half completes, then A is preempted before the store. */
    uint32_t a_stale_load = shared;  /* A's load: 0 */

    /* Context B (a higher-priority ISR) runs to completion inside the gap --
     * an unguarded |= has no critical section to prevent this -- and claims
     * a DIFFERENT bit. B's message already printed at this point in the real
     * macro (the log call happens after the set), so B's claim is not what
     * this demo is about. */
    shared = shared | (1u << 7);  /* B's complete |=: bit 7 wins, and B logs */

    /* A resumes, computing from its now-stale load and storing back --
     * overwriting B's bit with a snapshot that predates it. */
    shared = a_stale_load | (1u << 3);  /* A's stale store: bit 7 is gone */

    return shared;
}

TEST(bare_or_equals_shape_loses_a_bit_under_the_race_logger_oneshotclaim_closes) {
    uint32_t result = bare_or_equals_two_context_race_demo();

    /* Only A's bit survives. B's bit -- already claimed, already logged --
     * is erased from the suppression mask, so B's call site is re-armed and
     * fires again on its next hit: the exact defect #1125 removes. */
    ASSERT_EQ(result, (1u << 3));
    ASSERT_TRUE((result & (1u << 7)) == 0);

    /* Logger_OneShotClaim (above) cannot reproduce this: its whole
     * test-and-set is inside one critical section, so there is no load/store
     * gap for a second context's write to land in -- there is nothing
     * between "another context's call is possible" and "this call has
     * already committed both halves." */
}

int main(void) {
    RUN(claim_once_per_bit);
    RUN(bits_are_independent);
    RUN(all_32_bits_independently_claimable_exactly_once);
    RUN(out_of_range_bit_rejected_without_touching_the_mask);
    RUN(reset_re_arms_a_claimed_bit);
    RUN(reset_does_not_disturb_an_unrelated_bit_left_by_the_test_before_it);
    RUN(task_context_uses_task_critical_section_only);
    RUN(isr_context_uses_isr_critical_section_only);
    RUN(isr_exit_receives_the_exact_value_its_own_entry_returned);
    RUN(already_claimed_bit_still_enters_and_exits_its_critical_section_every_call);
    RUN(bare_or_equals_shape_loses_a_bit_under_the_race_logger_oneshotclaim_closes);
    return TEST_SUMMARY();
}
