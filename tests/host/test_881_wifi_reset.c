/*
 * #881 (codex r4/r5 on #1259): nothing may bring WiFi back after the user
 * turned it off. A hard reset arms a deferred INIT that re-enables WiFi 2 s
 * later; after SYSTem:COMMunicate:LAN:POWer 0 (PowerOff -> Deinit: latch set,
 * deferred INIT cancelled, DEINIT queued) no path may arm it again.
 *
 * Paths that arm it (every caller of wifi_manager_HardReset() and of the
 * deadline itself, enumerated for r6):
 *   - MaybeResetWincForRxDone()       the HIF layer's reset request
 *   - iperf2's MaybeAutoReset()       every iperf session end, including the
 *                                     one inside Iperf2_Stop(), which the
 *                                     two paths above and Deinit all call
 *   - SCPI_LANHardReset()             SYSTem:COMMunicate:LAN:HRESet
 *   - the APPLY divert in ProcessState (not spliceable here; it reaches the
 *                                     same HardResetImpl() these tests drive)
 * All of them now go through ArmHardResetLocked(), which refuses while WiFi
 * is off by request.
 *
 * wifi_manager.c and iperf2.c are not host-includable (FreeRTOS, Harmony,
 * the WINC driver), so the recipe splices the REAL functions involved out of
 * both into gen_881_wifi_reset.h and compiles them against the prelude below.
 * Iperf2_Stop() and MaybeAutoReset() are the real ones, not stubs: the r5
 * finding lived in Iperf2_Stop()'s nested auto-reset. "WiFi stays off" is
 * asserted as: the deferred-INIT deadline is disarmed and no INIT is queued.
 * ProcessState's deferred INIT fires only while gWifiReinitDeadlineTick is
 * nonzero, and nothing else in these sequences queues INIT.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_framework.h"

/* ---- prelude: what the spliced functions reference ------------------- */
typedef uint32_t TickType_t;
static TickType_t gNow = 1000;
static TickType_t xTaskGetTickCount(void) { return gNow; }
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
static int gCritical;
#define taskENTER_CRITICAL() (gCritical++)
#define taskEXIT_CRITICAL()  (gCritical--)
#define LOG_I(...) ((void)0)
#define LOG_E(...) ((void)0)

typedef enum { WIFI_MANAGER_EVENT_INIT, WIFI_MANAGER_EVENT_DEINIT } wifi_manager_event_t;
static unsigned gInits, gDeinits;
static bool SendEvent(wifi_manager_event_t e)
{
    if (e == WIFI_MANAGER_EVENT_INIT) gInits++; else gDeinits++;
    return true;
}
static void InvalidateStaLinkState(void) {}
static bool IsWithinApplyTeardownWindow(void) { return false; }

static uint32_t gHifRequests;
uint32_t hif_rx_reset_requests(void) { return gHifRequests; }

struct wifi_settings { uint8_t isEnabled; };
static struct wifi_settings gSettings;
static struct { struct wifi_settings *pWifiSettings; } gStateMachineContext = { &gSettings };
static volatile TickType_t gWifiReinitDeadlineTick;
static volatile bool gApplyInProgress;
static volatile TickType_t gApplyInProgressDeadlineTick;
static volatile bool gWincDeepPowerOff;
static uint32_t gRxResetHandled;   /* r5+; unused by older sources */
/* r6+. A copy: a rename in wifi_manager.c fails this build, not the test. */
typedef enum {
    HARD_RESET_QUEUED,
    HARD_RESET_REFUSED,
    HARD_RESET_QUEUE_FAILED,
} hard_reset_result_t;
static hard_reset_result_t HardResetImpl(void);

/* iperf2.c's side. */
enum { IPERF2_MODE_IDLE, IPERF2_MODE_ACTIVE };
static struct { int mode; unsigned long long bytes_confirmed; } gCtx;
static volatile bool gAutoResetOnStop = true;
static int gPreemptPowerOff;        /* LAN:POWer 0 lands inside Iperf2_Stop */
bool wifi_manager_PowerOff(void);
static void FinalizeStats(void)
{
    /* Iperf2_Stop() has passed its active-mode check: the point the audit
     * had USB SCPI (priority 7) preempt it with LAN:POWer 0. */
    if (gPreemptPowerOff) { gPreemptPowerOff = 0; (void)wifi_manager_PowerOff(); }
}
static void CloseAll(void) {}
static void ResetContext(void) { gCtx.mode = IPERF2_MODE_IDLE; }

void Iperf2_Stop(void);
static void MaybeAutoReset(void);
bool wifi_manager_HardReset(void);

#include "gen_881_wifi_reset.h"

/* ---- helpers ---------------------------------------------------------- */
static void wifi_up(void)
{
    gSettings.isEnabled = 1;
    gWincDeepPowerOff = false;
    gWifiReinitDeadlineTick = 0;
    gApplyInProgress = false;
    gInits = gDeinits = 0;
    gCritical = 0;
    gNow = 1000;
    gCtx.mode = IPERF2_MODE_IDLE;
    gAutoResetOnStop = true;
    gPreemptPowerOff = 0;
}

static int reset_armed(void) { return gWifiReinitDeadlineTick != 0; }

static void assert_wifi_stays_off(void)
{
    ASSERT_EQ(reset_armed(), 0);
    ASSERT_EQ(gInits, 0);
    ASSERT_EQ(gSettings.isEnabled, 0);
    ASSERT_EQ(gCritical, 0);
}

/* ---- MaybeResetWincForRxDone ----------------------------------------- */

/* Baseline: with WiFi up, a request is served by one hard reset. */
TEST(request_is_served_by_one_reset_while_wifi_is_up)
{
    wifi_up();
    gHifRequests++;
    MaybeResetWincForRxDone();
    ASSERT_EQ(reset_armed(), 1);
    ASSERT_EQ(gDeinits, 1);
    ASSERT_EQ(gSettings.isEnabled, 0);
    MaybeResetWincForRxDone();              /* served once, not again */
    ASSERT_EQ(gDeinits, 1);
    ASSERT_EQ(gCritical, 0);
}

/* The r4 audit's sequence: request, then POWer 0, then the manager loop. */
TEST(power_off_cancels_a_pending_reset_request)
{
    wifi_up();
    gHifRequests++;
    ASSERT_TRUE(wifi_manager_PowerOff());
    MaybeResetWincForRxDone();
    assert_wifi_stays_off();
    ASSERT_EQ(gWincDeepPowerOff, true);
    ASSERT_EQ(gDeinits, 1);                 /* PowerOff's own, only */
}

/* A request counted while WiFi is already off is consumed, not served, and
 * does not resurface once WiFi is enabled again. */
TEST(request_while_wifi_is_off_is_consumed)
{
    wifi_up();
    ASSERT_TRUE(wifi_manager_PowerOff());
    gHifRequests++;
    MaybeResetWincForRxDone();
    ASSERT_EQ(reset_armed(), 0);
    ASSERT_EQ(gDeinits, 1);
    gSettings.isEnabled = 1;                /* POWer 1 brought it back */
    gWincDeepPowerOff = false;
    MaybeResetWincForRxDone();
    ASSERT_EQ(reset_armed(), 0);
    ASSERT_EQ(gDeinits, 1);
}

/* POWer 0 then POWer 1 before the manager loop runs: the bring-up
 * re-initialises the WINC, so the request the power-off superseded must not
 * cost a second, spurious reset afterwards. */
TEST(request_superseded_by_power_off_stays_consumed_after_power_on)
{
    wifi_up();
    gHifRequests++;
    ASSERT_TRUE(wifi_manager_PowerOff());
    gSettings.isEnabled = 1;                /* POWer 1 (wifi_manager_PowerOn) */
    gWincDeepPowerOff = false;
    MaybeResetWincForRxDone();
    ASSERT_EQ(reset_armed(), 0);
    ASSERT_EQ(gDeinits, 1);
}

/* A request during a reset already in flight is served by that reset: its
 * deadline is not pushed back and no second DEINIT is queued. */
TEST(request_during_a_reset_in_flight_is_served_by_it)
{
    TickType_t armed;
    wifi_up();
    ASSERT_TRUE(wifi_manager_HardReset());
    armed = gWifiReinitDeadlineTick;
    gNow += 500;
    gHifRequests++;
    MaybeResetWincForRxDone();
    ASSERT_EQ(gWifiReinitDeadlineTick, armed);
    ASSERT_EQ(gDeinits, 1);
}

/* ---- iperf2's auto-reset (real Iperf2_Stop / MaybeAutoReset) ---------- */

/* The r5 audit's sequence: a HIF request arms a reset; MaybeResetWincFor-
 * RxDone() then runs Iperf2_Stop() on a live session with AUTOReset on, and
 * LAN:POWer 0 preempts it after its active-mode check. When Stop resumes,
 * its auto-reset must not re-arm the deferred INIT. */
TEST(iperf_auto_reset_after_a_preempting_power_off_is_refused)
{
    wifi_up();
    gCtx.mode = IPERF2_MODE_ACTIVE;
    gPreemptPowerOff = 1;
    gHifRequests++;
    MaybeResetWincForRxDone();
    assert_wifi_stays_off();
    ASSERT_EQ(gWincDeepPowerOff, true);
}

/* The same preemption with no HIF request: an iperf stop of its own. */
TEST(iperf_stop_preempted_by_power_off_does_not_reset)
{
    wifi_up();
    gCtx.mode = IPERF2_MODE_ACTIVE;
    gPreemptPowerOff = 1;
    Iperf2_Stop();
    assert_wifi_stays_off();
}

/* Baseline: an iperf session ending while WiFi is up still auto-resets. */
TEST(iperf_session_end_while_wifi_is_up_still_resets)
{
    wifi_up();
    gCtx.mode = IPERF2_MODE_ACTIVE;
    Iperf2_Stop();
    ASSERT_EQ(reset_armed(), 1);
    ASSERT_TRUE(gDeinits >= 1);
}

/* ---- wifi_manager_HardReset (LAN:HRESet; the APPLY divert's core) ----- */

TEST(hard_reset_arms_while_wifi_is_up)
{
    wifi_up();
    ASSERT_TRUE(wifi_manager_HardReset());
    ASSERT_EQ(reset_armed(), 1);
    ASSERT_EQ(gSettings.isEnabled, 0);
    ASSERT_EQ(gDeinits, 1);
}

TEST(hard_reset_is_refused_while_powered_off)
{
    wifi_up();
    ASSERT_TRUE(wifi_manager_PowerOff());
    ASSERT_FALSE(wifi_manager_HardReset());
    assert_wifi_stays_off();
    ASSERT_EQ(gDeinits, 1);
}

/* PowerOff() sets the latch, then runs Deinit(). A reset from another task
 * between the two (LAN:POWer 0 over TCP SCPI on the WifiTask, preempted by
 * LAN:HRESet over USB) still sees WiFi enabled; only the latch stops it. */
TEST(hard_reset_is_refused_between_the_power_off_latch_and_its_deinit)
{
    wifi_up();
    gWincDeepPowerOff = true;               /* PowerOff's first store */
    ASSERT_FALSE(wifi_manager_HardReset());
    ASSERT_EQ(reset_armed(), 0);
    ASSERT_EQ(gDeinits, 0);
}

TEST(hard_reset_is_refused_while_wifi_is_disabled)
{
    wifi_up();
    ASSERT_TRUE(wifi_manager_Deinit());     /* ENAbled 0 + APPLY, or standby */
    ASSERT_FALSE(wifi_manager_HardReset());
    assert_wifi_stays_off();
}

/* ---- tick wrap (codex r5, blind leg) --------------------------------- */

/* now + 2000 ticks wraps to exactly 0, the "no deadline" sentinel. Stored
 * as is, the deferred INIT is never scheduled and WiFi stays off for good. */
TEST(deadline_that_wraps_to_zero_is_still_armed)
{
    wifi_up();
    gNow = 0xFFFFF830u;                     /* + 2000 == 0 (mod 2^32) */
    ASSERT_TRUE(wifi_manager_HardReset());
    ASSERT_EQ(reset_armed(), 1);
    /* ProcessState's expiry test, (int32_t)(now - deadline) >= 0, is wrap
     * safe: not yet due at arming, due 2001 ticks after it. */
    ASSERT_TRUE((int32_t)(gNow - gWifiReinitDeadlineTick) < 0);
    ASSERT_TRUE((int32_t)((TickType_t)(gNow + 2001u) - gWifiReinitDeadlineTick) >= 0);
}

TEST(hif_request_deadline_that_wraps_to_zero_is_still_armed)
{
    wifi_up();
    gNow = 0xFFFFF830u;
    gHifRequests++;
    MaybeResetWincForRxDone();
    ASSERT_EQ(reset_armed(), 1);
}

int main(void)
{
    printf("#881 WINC resets vs. an explicit power-off\n");
    RUN(request_is_served_by_one_reset_while_wifi_is_up);
    RUN(power_off_cancels_a_pending_reset_request);
    RUN(request_while_wifi_is_off_is_consumed);
    RUN(request_superseded_by_power_off_stays_consumed_after_power_on);
    RUN(request_during_a_reset_in_flight_is_served_by_it);
    RUN(iperf_auto_reset_after_a_preempting_power_off_is_refused);
    RUN(iperf_stop_preempted_by_power_off_does_not_reset);
    RUN(iperf_session_end_while_wifi_is_up_still_resets);
    RUN(hard_reset_arms_while_wifi_is_up);
    RUN(hard_reset_is_refused_while_powered_off);
    RUN(hard_reset_is_refused_between_the_power_off_latch_and_its_deinit);
    RUN(hard_reset_is_refused_while_wifi_is_disabled);
    RUN(deadline_that_wraps_to_zero_is_still_armed);
    RUN(hif_request_deadline_that_wraps_to_zero_is_still_armed);
    return TEST_SUMMARY();
}
