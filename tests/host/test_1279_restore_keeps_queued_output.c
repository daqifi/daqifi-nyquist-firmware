/* ==========================================================================
 * test_1279_restore_keeps_queued_output.c -- issue #1279, audit finding 1
 *
 * WHAT WAS WRONG
 *
 * SCPI_RestoreIdleSdBuffers() re-partitions the streaming pool before
 * SD:BENCHmark and SD:GET arm. PrepareStreamingBuffers ends by calling
 * UsbCdc_SetWriteBuffer and wifi_tcp_server_SetWriteBuffer, and both RESET
 * their output ring (insert/remove pointers and produced/consumed counts),
 * discarding whatever was queued but not yet sent.
 *
 * At that point the ring can hold real output. libscpi writes every reply
 * into the transport as it is produced (parser.c writeData ->
 * interface->write -> UsbCdc_WriteToBuffer / wifi_tcp_server_WriteBuffer) and
 * flushes only at the end of the message (writeNewLine -> flushData), and
 * neither transport task drains its ring while it is inside a SCPI callback:
 * the USB ring's drain, UsbCdc_ProcessState, is the task running the callback,
 * and SCPI_TCP_Flush is a no-op, so the TCP ring waits for WifiTask's loop.
 * So `*IDN?;:SYST:STOR:SD:BENCH 1024,0` lost the *IDN? reply, and with the
 * console echo on (the default for both consoles) the command's own echo was
 * lost too.
 *
 * THE FIX, as this file sees it
 *
 * The restore pumps both rings out (bounded) before re-partitioning, and
 * refuses instead of discarding if they will not drain. This binary runs the
 * REAL SCPI_RestoreIdleSdBuffers and SdIdleRestoreDue, spliced verbatim from
 * SCPIInterface.c by the Makefile, against a model of the two rings:
 *
 *   - PrepareStreamingBuffers (stub) does what the real one does to the rings:
 *     whatever is queued when it runs is LOST. It also installs the idle
 *     layout, so the predicate stops firing.
 *   - vTaskDelay lets the OTHER transport's task run its own drain, as the
 *     scheduler does: the USB task drains the USB ring if the host is reading,
 *     WifiTask drains the TCP ring if the WINC is sending.
 *
 * The refusal PrepareStreamingBuffers itself adds just before its re-carve is
 * mid-function and not host-compilable; check_1279_sd_idle_restore.py pins it.
 *
 * ON 319c24f3 (before this fix) the same splice compiles and the restore
 * re-partitions straight over the queued bytes, so the loss assertions below
 * fail.
 * ========================================================================== */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "test_framework.h"

#include "gen_1279_out_defs.h"   /* SD_CARD_MANAGER_* out of sd_card_manager.h */

/* ---- FreeRTOS stand-ins: a 1 kHz tick, advanced only by vTaskDelay ---- */
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define DEFAULT_AIN_SAMPLE_COUNT 1100u

/* ---- The world ---- */
static struct {
    /* layout */
    uint32_t sdCirc, sdDma;
    /* rings: bytes queued, and what became of them */
    uint32_t usbQueued, wifiQueued;
    uint32_t delivered, lost;
    bool hostReading;        /* USB host drains the IN endpoint */
    bool wincSending;        /* WINC accepts send() */
    bool callerIsUsbTask;    /* which SCPI task runs the restore */
    /* gates */
    bool claimOk, sdBusy, prepareOk;
    /* observations */
    TickType_t tick;
    int delays, prepareCalls, usbPumps, wifiPumps, claims, releases;
} w;

typedef struct {
    uint32_t sdCircularBufSize;
    uint32_t samplePoolCount;
} MemoryConfig;
static MemoryConfig g_mc;
enum { BOARDRUNTIME_MEMORY_CONFIG };
static void* BoardRunTimeConfig_Get(int which) { (void)which; return &g_mc; }

typedef enum { STREAM_CFG_CLAIM_OK = 0, STREAM_CFG_CLAIM_BUSY = 1 } StreamCfgClaim;
StreamCfgClaim Streaming_BeginConfigChange(void) {
    w.claims++;
    return w.claimOk ? STREAM_CFG_CLAIM_OK : STREAM_CFG_CLAIM_BUSY;
}
void Streaming_EndConfigChange(void) { w.releases++; }

uint32_t StreamingBufferPool_SdCircularSize(void) { return w.sdCirc; }
uint32_t sd_card_manager_GetWriteBufferSize(void) { return w.sdDma; }
bool sd_card_manager_IsBusy(void) { return w.sdBusy; }

TickType_t xTaskGetTickCount(void) { return w.tick; }

/* Non-static: on the pre-fix source nothing calls these, and the build uses
 * -Werror=unused-function. */
uint32_t UsbCdc_PendingWriteBytes(void) { return w.usbQueued; }
uint32_t wifi_tcp_server_PendingWriteBytes(void) { return w.wifiQueued; }
bool UsbCdc_CallerIsUsbTask(void) { return w.callerIsUsbTask; }
void UsbCdc_PumpWrite(void) {
    w.usbPumps++;
    if (w.hostReading) { w.delivered += w.usbQueued; w.usbQueued = 0; }
}
bool wifi_tcp_server_TransmitBufferedData(void) {
    w.wifiPumps++;
    if (w.wincSending) { w.delivered += w.wifiQueued; w.wifiQueued = 0; }
    return true;
}

/* The other SCPI task gets the CPU while this one sleeps, and drains its own
 * ring the way its loop does. */
void vTaskDelay(TickType_t t) {
    w.delays++;
    w.tick += t;
    if (w.callerIsUsbTask) {
        if (w.wincSending) { w.delivered += w.wifiQueued; w.wifiQueued = 0; }
    } else {
        if (w.hostReading) { w.delivered += w.usbQueued; w.usbQueued = 0; }
    }
}

/* What the real re-partition does to the rings: resets them. */
static bool PrepareStreamingBuffers(uint32_t poolCount, size_t elemSize,
                                    bool idleLayout) {
    (void)poolCount; (void)elemSize;
    w.prepareCalls++;
    if (!idleLayout || !w.prepareOk) return false;
    w.lost += w.usbQueued + w.wifiQueued;
    w.usbQueued = w.wifiQueued = 0;
    w.sdCirc = SD_CARD_MANAGER_DEFAULT_CIRCULAR_SIZE;
    w.sdDma = SD_CARD_MANAGER_CONF_WBUFFER_SIZE;
    return true;
}

/* ---- The REAL functions under test, spliced verbatim ---- */
#include "gen_1279_out_due.h"
#include "gen_1279_out_restore.h"

/* After a WiFi or USB-only stream: SD circular 4 KB, SD DMA 512 B. */
static void after_non_sd_stream(void) {
    memset(&w, 0, sizeof w);
    g_mc = (MemoryConfig){0};
    w.sdCirc = 4096u;
    w.sdDma = 512u;
    w.hostReading = true;
    w.wincSending = true;
    w.claimOk = true;
    w.prepareOk = true;
}

/* ------------------------------------------------------------------------ */

TEST(usb_reply_earlier_in_the_message_is_sent_not_discarded) {
    /* `*IDN?;:SYST:STOR:SD:BENCH 1024,0` over USB: the *IDN? reply is in the
     * USB ring when BENCH's restore runs on the USB task. */
    after_non_sd_stream();
    w.callerIsUsbTask = true;
    w.usbQueued = 40u;
    ASSERT_TRUE(SCPI_RestoreIdleSdBuffers());
    ASSERT_EQ(w.lost, 0u);
    ASSERT_EQ(w.delivered, 40u);
    ASSERT_EQ(w.prepareCalls, 1);
    ASSERT_EQ(w.sdDma, SD_CARD_MANAGER_CONF_WBUFFER_SIZE);
}

TEST(tcp_echo_is_sent_by_the_tcp_task_itself) {
    /* Over TCP, on WifiTask: nobody else will send this ring, so the restore
     * must pump it itself. */
    after_non_sd_stream();
    w.callerIsUsbTask = false;
    w.wifiQueued = 28u;          /* "SYST:STOR:SD:BENCH 1024,0\r\n" echoed */
    ASSERT_TRUE(SCPI_RestoreIdleSdBuffers());
    ASSERT_EQ(w.lost, 0u);
    ASSERT_EQ(w.delivered, 28u);
    ASSERT_TRUE(w.wifiPumps >= 1);
}

TEST(usb_task_leaves_the_tcp_ring_to_wifitask) {
    /* A TCP reply is queued while the USB task runs the restore: WifiTask
     * sends it while the restore waits, and the USB task never calls into the
     * WINC driver's send(). */
    after_non_sd_stream();
    w.callerIsUsbTask = true;
    w.wifiQueued = 64u;
    ASSERT_TRUE(SCPI_RestoreIdleSdBuffers());
    ASSERT_EQ(w.lost, 0u);
    ASSERT_EQ(w.delivered, 64u);
    ASSERT_EQ(w.wifiPumps, 0);
}

TEST(undeliverable_output_refuses_instead_of_discarding) {
    /* The USB host is not reading. The restore must give up -- bounded --
     * and leave the bytes where they are, not reset them away. */
    after_non_sd_stream();
    w.callerIsUsbTask = false;
    w.hostReading = false;
    w.usbQueued = 40u;
    ASSERT_FALSE(SCPI_RestoreIdleSdBuffers());
    ASSERT_EQ(w.lost, 0u);
    ASSERT_EQ(w.usbQueued, 40u);
    ASSERT_EQ(w.prepareCalls, 0);
    ASSERT_TRUE(w.tick >= 500u && w.tick <= 510u);
    ASSERT_EQ(w.releases, 1);       /* the claim is still released */
}

TEST(nothing_queued_restores_without_waiting) {
    after_non_sd_stream();
    w.callerIsUsbTask = true;
    ASSERT_TRUE(SCPI_RestoreIdleSdBuffers());
    ASSERT_EQ(w.delays, 0);
    ASSERT_EQ(w.prepareCalls, 1);
    ASSERT_EQ(w.releases, 1);
}

TEST(idle_layout_touches_nothing) {
    after_non_sd_stream();
    w.sdCirc = SD_CARD_MANAGER_DEFAULT_CIRCULAR_SIZE;
    w.sdDma = SD_CARD_MANAGER_CONF_WBUFFER_SIZE;
    w.usbQueued = 40u;
    ASSERT_TRUE(SCPI_RestoreIdleSdBuffers());
    ASSERT_EQ(w.claims, 0);
    ASSERT_EQ(w.usbPumps, 0);
    ASSERT_EQ(w.prepareCalls, 0);
    ASSERT_EQ(w.usbQueued, 40u);    /* left for its own task's normal drain */
}

TEST(busy_sd_manager_refuses_before_draining) {
    after_non_sd_stream();
    w.sdBusy = true;
    w.usbQueued = 40u;
    ASSERT_FALSE(SCPI_RestoreIdleSdBuffers());
    ASSERT_EQ(w.usbPumps, 0);
    ASSERT_EQ(w.prepareCalls, 0);
    ASSERT_EQ(w.lost, 0u);
    ASSERT_EQ(w.releases, 1);
}

TEST(refused_claim_refuses_before_draining) {
    after_non_sd_stream();
    w.claimOk = false;
    w.usbQueued = 40u;
    ASSERT_FALSE(SCPI_RestoreIdleSdBuffers());
    ASSERT_EQ(w.usbPumps, 0);
    ASSERT_EQ(w.prepareCalls, 0);
    ASSERT_EQ(w.releases, 0);
}

int main(void) {
    printf("test_1279_restore_keeps_queued_output\n");
    RUN(usb_reply_earlier_in_the_message_is_sent_not_discarded);
    RUN(tcp_echo_is_sent_by_the_tcp_task_itself);
    RUN(usb_task_leaves_the_tcp_ring_to_wifitask);
    RUN(undeliverable_output_refuses_instead_of_discarding);
    RUN(nothing_queued_restores_without_waiting);
    RUN(idle_layout_touches_nothing);
    RUN(busy_sd_manager_refuses_before_draining);
    RUN(refused_claim_refuses_before_draining);
    return TEST_SUMMARY();
}
