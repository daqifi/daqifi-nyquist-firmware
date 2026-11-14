/*
 * #881 (codex r4 on #1259, finding 3): a WINC reset the HIF layer requested
 * must never reverse an explicit power-off. Sequence: the HIF layer counts a
 * reset request; SYSTem:COMMunicate:LAN:POWer 0 runs (PowerOff -> Deinit:
 * deep-power-down latch set, deferred INIT cancelled, DEINIT queued); then
 * wifi_manager's loop serves the request with a hard reset, which re-arms the
 * deferred INIT -- 2 s later WiFi is enabled and INIT powers the WINC back up.
 *
 * wifi_manager.c is not host-includable (FreeRTOS, Harmony, the WINC driver),
 * so the recipe splices the four REAL functions involved out of it into
 * gen_881_wifi_reset.h -- wifi_manager_Deinit, wifi_manager_PowerOff,
 * wifi_manager_HardReset and MaybeResetWincForRxDone -- and compiles them
 * against the prelude below. "The WINC stays off" is asserted as: the
 * deferred-INIT deadline is disarmed and no INIT is queued. ProcessState's
 * deferred INIT fires only while gWifiReinitDeadlineTick is nonzero, and
 * nothing else in this sequence queues INIT.
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
static void Iperf2_Stop(void) {}
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
static uint32_t gRxResetHandled;   /* r5; unused by pre-r5 sources */

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
}

/* The two observable outcomes. */
static int reset_armed(void) { return gWifiReinitDeadlineTick != 0; }

/* ---- tests ------------------------------------------------------------ */

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

/* The audit's sequence: request, then POWer 0, then the manager loop. */
TEST(power_off_cancels_a_pending_reset_request)
{
    wifi_up();
    gHifRequests++;
    ASSERT_TRUE(wifi_manager_PowerOff());
    MaybeResetWincForRxDone();
    ASSERT_EQ(reset_armed(), 0);
    ASSERT_EQ(gInits, 0);
    ASSERT_EQ(gSettings.isEnabled, 0);
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

int main(void)
{
    printf("#881 WINC reset requests vs. an explicit power-off\n");
    RUN(request_is_served_by_one_reset_while_wifi_is_up);
    RUN(power_off_cancels_a_pending_reset_request);
    RUN(request_while_wifi_is_off_is_consumed);
    RUN(request_superseded_by_power_off_stays_consumed_after_power_on);
    RUN(request_during_a_reset_in_flight_is_served_by_it);
    return TEST_SUMMARY();
}
