/* ==========================================================================
 * test_1182_event_flag_atomic.c -- issue #1182
 *
 * WHAT WAS WRONG
 *
 * wifi_manager.c keeps its state flags in ONE 16-bit word (eventFlags.value)
 * written from more than one task. SetEventFlag / ResetEventFlag were plain
 * load/modify/store, so a flag written by another task between the load and the
 * store was silently erased. MainState ENTRY made it worse: it read the two
 * flags that must outlive a reset (the user's firmware-update request, the
 * #1060 teardown marker) into locals, memset the whole word, and re-set them --
 * a flag set by another task between the read and the memset was erased, and
 * the restore was a second, separate write.
 *
 * WHAT THIS RUNS
 *
 * The REAL SetEventFlag, ResetEventFlag and ResetEventFlagsExcept, spliced
 * verbatim out of wifi_manager.c by the Makefile (nothing here is a copy that
 * can drift), plus the real flag layout. taskENTER_CRITICAL / taskEXIT_CRITICAL
 * are stubs that record nesting depth and the flag word at the moment of each
 * call. The properties:
 *
 *   - the word changes ONLY while inside a critical section, and ENTER / EXIT
 *     are balanced;
 *   - a "preempting writer" hook fired at ENTER (it stands in for another task
 *     running just before the section is taken) sets a flag; the keep-mask
 *     helper must still preserve exactly its mask, including that flag, and must
 *     clear everything else;
 *   - the helper returns the value that is actually stored afterwards.
 *
 * ON UNMODIFIED origin/main the helpers take no critical section and
 * ResetEventFlagsExcept does not exist, so the splice finds no function and the
 * build fails (see the PR body for that output).
 * ========================================================================== */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "test_framework.h"

/* The real flag layout: the enum body of wifi_manager_stateFlag_t. */
typedef struct {
#include "gen_1182_flags.h"
} wifi_manager_stateFlag_t;

/* ---- critical-section recorder ---- */
static int      g_depth;          /* current nesting */
static int      g_enter, g_exit;  /* calls */
static int      g_underflow;      /* EXIT with depth 0 */
static uint16_t g_pre_set;        /* a "preempting writer" ORs this at ENTER */

/* Where is the watched word, so the stubs can observe it. */
static wifi_manager_stateFlag_t *g_watch;
static uint16_t g_value_at_enter, g_value_at_exit;

static void stub_enter(void) {
    g_enter++;
    if (g_depth == 0 && g_pre_set != 0 && g_watch != NULL) {
        /* Another task running just before the section is taken. */
        g_watch->value = (uint16_t)(g_watch->value | g_pre_set);
    }
    g_depth++;
    if (g_watch != NULL) {
        g_value_at_enter = g_watch->value;
    }
}

static void stub_exit(void) {
    g_exit++;
    if (g_depth == 0) {
        g_underflow++;
        return;
    }
    if (g_watch != NULL) {
        g_value_at_exit = g_watch->value;
    }
    g_depth--;
}

#define taskENTER_CRITICAL() stub_enter()
#define taskEXIT_CRITICAL()  stub_exit()

/* ---- the REAL helpers, spliced verbatim by the Makefile ---- */
#include "gen_1182_helpers.h"

/* Flag names used below come from the spliced enum. */
#define F_FW   WIFI_MANAGER_STATE_FLAG_WIFI_FW_UPDATE_REQUESTED
#define F_TEAR WIFI_MANAGER_STATE_FLAG_TEARDOWN_PENDING

static void reset_world(wifi_manager_stateFlag_t *f, uint16_t initial) {
    g_depth = g_enter = g_exit = g_underflow = 0;
    g_pre_set = 0;
    g_value_at_enter = g_value_at_exit = 0;
    f->value = initial;
    g_watch = f;
}

TEST(set_changes_word_only_inside_a_balanced_section) {
    wifi_manager_stateFlag_t f;
    reset_world(&f, WIFI_MANAGER_STATE_FLAG_INITIALIZED);
    uint16_t before = f.value;
    SetEventFlag(&f, WIFI_MANAGER_STATE_FLAG_AP_STARTED);
    ASSERT_EQ(g_enter, 1);
    ASSERT_EQ(g_exit, 1);
    ASSERT_EQ(g_depth, 0);
    ASSERT_EQ(g_underflow, 0);
    /* untouched at ENTER, changed by EXIT: the store is inside the section */
    ASSERT_EQ(g_value_at_enter, before);
    ASSERT_EQ(g_value_at_exit,
              before | WIFI_MANAGER_STATE_FLAG_AP_STARTED);
    ASSERT_EQ(f.value, before | WIFI_MANAGER_STATE_FLAG_AP_STARTED);
}

TEST(reset_changes_word_only_inside_a_balanced_section) {
    wifi_manager_stateFlag_t f;
    reset_world(&f, WIFI_MANAGER_STATE_FLAG_AP_STARTED |
                    WIFI_MANAGER_STATE_FLAG_STA_STARTED);
    uint16_t before = f.value;
    ResetEventFlag(&f, WIFI_MANAGER_STATE_FLAG_AP_STARTED);
    ASSERT_EQ(g_enter, 1);
    ASSERT_EQ(g_exit, 1);
    ASSERT_EQ(g_depth, 0);
    ASSERT_EQ(g_underflow, 0);
    ASSERT_EQ(g_value_at_enter, before);
    ASSERT_EQ(g_value_at_exit, WIFI_MANAGER_STATE_FLAG_STA_STARTED);
    ASSERT_EQ(f.value, WIFI_MANAGER_STATE_FLAG_STA_STARTED);
}

TEST(a_writer_that_lands_before_the_section_is_not_lost) {
    /* The window the issue describes is load..store. With the load inside the
     * section a flag another task set just before it is read, not erased. */
    wifi_manager_stateFlag_t f;
    reset_world(&f, 0);
    g_pre_set = WIFI_MANAGER_STATE_FLAG_STA_CONNECTED;
    SetEventFlag(&f, WIFI_MANAGER_STATE_FLAG_AP_STARTED);
    ASSERT_EQ(f.value, WIFI_MANAGER_STATE_FLAG_AP_STARTED |
                       WIFI_MANAGER_STATE_FLAG_STA_CONNECTED);
    reset_world(&f, WIFI_MANAGER_STATE_FLAG_AP_STARTED);
    g_pre_set = WIFI_MANAGER_STATE_FLAG_STA_CONNECTED;
    ResetEventFlag(&f, WIFI_MANAGER_STATE_FLAG_AP_STARTED);
    ASSERT_EQ(f.value, WIFI_MANAGER_STATE_FLAG_STA_CONNECTED);
}

TEST(keep_mask_helper_preserves_exactly_its_mask) {
    wifi_manager_stateFlag_t f;
    reset_world(&f, 0xFFFFu);
    uint16_t kept = ResetEventFlagsExcept(&f, F_FW | F_TEAR);
    ASSERT_EQ(f.value, F_FW | F_TEAR);
    ASSERT_EQ(kept, F_FW | F_TEAR);
    ASSERT_EQ(g_enter, 1);
    ASSERT_EQ(g_exit, 1);
    ASSERT_EQ(g_depth, 0);
    ASSERT_EQ(g_underflow, 0);
    /* the whole word changed inside the section, in one step */
    ASSERT_EQ(g_value_at_enter, 0xFFFFu);
    ASSERT_EQ(g_value_at_exit, F_FW | F_TEAR);
}

TEST(keep_mask_helper_does_not_invent_flags) {
    wifi_manager_stateFlag_t f;
    reset_world(&f, WIFI_MANAGER_STATE_FLAG_INITIALIZED |
                    WIFI_MANAGER_STATE_FLAG_AP_STARTED | F_TEAR);
    uint16_t kept = ResetEventFlagsExcept(&f, F_FW | F_TEAR);
    ASSERT_EQ(f.value, F_TEAR);            /* FW was not set, stays not set */
    ASSERT_EQ(kept, F_TEAR);
    ASSERT_EQ(ResetEventFlagsExcept(&f, 0), 0);
    ASSERT_EQ(f.value, 0);
}

TEST(keep_mask_helper_keeps_a_flag_set_by_a_task_just_before_the_section) {
    /* The ENTER stub sets FW_UPDATE_REQUESTED, simulating the SCPI task
     * running SYST:COMM:LAN:FWUpdate between the old code's read of the flag
     * and its memset. The new helper reads inside the section, so the request
     * survives and is what the helper reports. */
    wifi_manager_stateFlag_t f;
    reset_world(&f, WIFI_MANAGER_STATE_FLAG_INITIALIZED |
                    WIFI_MANAGER_STATE_FLAG_STA_CONNECTED);
    g_pre_set = F_FW;
    uint16_t kept = ResetEventFlagsExcept(&f, F_FW | F_TEAR);
    ASSERT_EQ(f.value, F_FW);
    ASSERT_EQ(kept, F_FW);
    ASSERT_TRUE((kept & F_FW) != 0);
}

TEST(a_flag_outside_the_mask_set_just_before_the_section_is_cleared) {
    /* Negative control for the test above: the helper keeps its MASK, not
     * "whatever was set at the moment". */
    wifi_manager_stateFlag_t f;
    reset_world(&f, 0);
    g_pre_set = WIFI_MANAGER_STATE_FLAG_UDP_SOCKET_OPEN;
    uint16_t kept = ResetEventFlagsExcept(&f, F_FW | F_TEAR);
    ASSERT_EQ(f.value, 0);
    ASSERT_EQ(kept, 0);
}

TEST(nesting_inside_a_callers_section_stays_balanced) {
    wifi_manager_stateFlag_t f;
    reset_world(&f, 0);
    taskENTER_CRITICAL();
    SetEventFlag(&f, WIFI_MANAGER_STATE_FLAG_AP_STARTED);
    ResetEventFlag(&f, WIFI_MANAGER_STATE_FLAG_AP_STARTED);
    (void)ResetEventFlagsExcept(&f, F_FW);
    taskEXIT_CRITICAL();
    ASSERT_EQ(g_enter, 4);
    ASSERT_EQ(g_exit, 4);
    ASSERT_EQ(g_depth, 0);
    ASSERT_EQ(g_underflow, 0);
}

int main(void) {
    RUN(set_changes_word_only_inside_a_balanced_section);
    RUN(reset_changes_word_only_inside_a_balanced_section);
    RUN(a_writer_that_lands_before_the_section_is_not_lost);
    RUN(keep_mask_helper_preserves_exactly_its_mask);
    RUN(keep_mask_helper_does_not_invent_flags);
    RUN(keep_mask_helper_keeps_a_flag_set_by_a_task_just_before_the_section);
    RUN(a_flag_outside_the_mask_set_just_before_the_section_is_cleared);
    RUN(nesting_inside_a_callers_section_stays_balanced);
    return TEST_SUMMARY();
}
