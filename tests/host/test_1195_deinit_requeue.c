/*
 * #1195: MainState's own DEINIT re-queue (driver BUSY, and the firmware-update
 * exit in REINIT) used to discard SendEvent's result. SendEvent uses timeout 0
 * there -- the caller holds gProcessStateMutex and is the only drainer -- so a
 * send that failed abandoned the teardown with nothing left to retry it.
 *
 * The fix: both sites call RequeueDeinit(), which latches gDeinitRequeuePending
 * on a failed send; ProcessStateImpl retries it every pass; a dispatched DEINIT
 * clears it.
 *
 * wifi_manager.c is not host-includable, so the recipe splices the REAL
 * RequeueDeinit() into gen_1195_deinit_requeue.h, and pins the three call-site
 * facts a host test cannot see (no statement-position SendEvent(DEINIT) left,
 * ProcessStateImpl retries while pending, the DEINIT case clears the flag).
 * The handler and the ProcessState pass below are a MODEL of those call sites,
 * reduced to the parts the pins hold in place.
 */
#include <stdbool.h>
#include <stdio.h>

#include "test_framework.h"

/* ---- prelude: what the spliced function references -------------------- */
typedef enum { WIFI_MANAGER_EVENT_NONE, WIFI_MANAGER_EVENT_DEINIT } wifi_manager_event_t;

#define QCAP 4u
static wifi_manager_event_t gQ[QCAP];
static unsigned gQLen;
static unsigned gFailSends;   /* next N sends fail, as a full queue does */
static unsigned gSendCalls;

static bool SendEvent(wifi_manager_event_t e)
{
    gSendCalls++;
    if (gFailSends > 0) { gFailSends--; return false; }
    if (gQLen == QCAP) return false;
    gQ[gQLen++] = e;
    return true;
}

static bool gDeinitRequeuePending = false;

#include "gen_1195_deinit_requeue.h"

/* ---- model of the call sites ------------------------------------------ */
static unsigned gBusyPasses;  /* handler sees SYS_STATUS_BUSY this many times */
static unsigned gTeardowns;
static bool gOldShape;        /* negative control: the pre-#1195 discard */

static void DeinitHandler(void)
{
    gDeinitRequeuePending = false;
    if (gBusyPasses > 0) {
        gBusyPasses--;
        if (gOldShape) {
            (void)SendEvent(WIFI_MANAGER_EVENT_DEINIT);
        } else {
            RequeueDeinit();
        }
        return;
    }
    gTeardowns++;
}

static void ProcessPass(void)
{
    if (gDeinitRequeuePending) {
        RequeueDeinit();
    }
    if (gQLen > 0) {
        wifi_manager_event_t e = gQ[0];
        for (unsigned i = 1; i < gQLen; i++) gQ[i - 1] = gQ[i];
        gQLen--;
        if (e == WIFI_MANAGER_EVENT_DEINIT) DeinitHandler();
    }
}

static void Reset(bool oldShape, unsigned busy, unsigned failSends)
{
    gQLen = 0; gSendCalls = 0; gTeardowns = 0;
    gDeinitRequeuePending = false;
    gOldShape = oldShape; gBusyPasses = busy; gFailSends = failSends;
    gQ[gQLen++] = WIFI_MANAGER_EVENT_DEINIT;   /* the public Deinit's event */
}

static void RunPasses(unsigned n) { while (n--) ProcessPass(); }

/* A re-queue that fails while BUSY is retried, and the teardown completes. */
TEST(failed_requeue_is_retried_and_teardown_completes)
{
    Reset(false, 2, 3);
    RunPasses(20);
    ASSERT_EQ(gTeardowns, 1);
    ASSERT_FALSE(gDeinitRequeuePending);
    ASSERT_EQ(gQLen, 0);
}

/* Negative control: the old discard loses the teardown under the same input,
 * so the test above is measuring the fix and not the model. */
TEST(old_shape_abandons_the_teardown)
{
    Reset(true, 2, 3);
    RunPasses(20);
    ASSERT_EQ(gTeardowns, 0);
    ASSERT_EQ(gQLen, 0);
}

/* The success path is unchanged: one send per BUSY pass, nothing pending. */
TEST(successful_requeue_leaves_nothing_pending)
{
    Reset(false, 3, 0);
    RunPasses(20);
    ASSERT_EQ(gTeardowns, 1);
    ASSERT_EQ(gSendCalls, 3);
    ASSERT_FALSE(gDeinitRequeuePending);
}

/* A DEINIT dispatched from elsewhere consumes a pending retry: the retry in
 * the same pass fails, the queued public DEINIT tears down, and no further
 * DEINIT is sent afterwards. */
TEST(dispatched_deinit_clears_a_pending_retry)
{
    Reset(false, 0, 1);       /* queue holds a public DEINIT; one send fails */
    gDeinitRequeuePending = true;
    RunPasses(5);
    ASSERT_EQ(gTeardowns, 1);
    ASSERT_EQ(gSendCalls, 1);
    ASSERT_FALSE(gDeinitRequeuePending);
}

int main(void)
{
    printf("#1195 DEINIT re-queue result\n");
    RUN(failed_requeue_is_retried_and_teardown_completes);
    RUN(old_shape_abandons_the_teardown);
    RUN(successful_requeue_leaves_nothing_pending);
    RUN(dispatched_deinit_clears_a_pending_retry);
    return TEST_SUMMARY();
}
