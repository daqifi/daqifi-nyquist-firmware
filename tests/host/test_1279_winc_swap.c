/* ==========================================================================
 * test_1279_winc_swap.c -- issue #1279, codex r3 finding (HIGH)
 *
 * WHAT WAS WRONG
 *
 * The idle SD-buffer restore checked that the WINC SPI was idle
 * (WDRV_WINC_SPI_WaitIdle), reset the coherent pool, then YIELDED inside
 * wifi_tcp_server_SetWriteBuffer (tcpInFlight wait) before it finally called
 * WDRV_WINC_SPI_SetBuffer. In the yield the WINC task could start a receive on
 * the OLD staging buffer; SetBuffer then moved alignedBuffer, and
 * WDRV_WINC_SPIReceive copied out of the NEW pointer what the DMA had written
 * to the OLD one. The old "idle" predicate (the transfer handles) was also
 * unsound: SPISend copies into alignedBuffer BEFORE it sets the handle.
 *
 * WHAT THIS RUNS
 *
 * The REAL wdrv_winc_spi.c (included), the REAL CoherentPool.c, and the REAL
 * SCPI_SwapCoherentDmaBuffers spliced verbatim from SCPIInterface.c by the
 * Makefile, against fakes of the scheduler (suspend depth, tick), the SPI
 * driver (records the buffer the DMA would use; the test lands the data when
 * the driver pends) and the other consumers. The fakes FAIL if anything that
 * can block (vTaskDelay, a semaphore pend, a LOG_E, the SD buffer setter,
 * which takes a mutex) runs while the scheduler is suspended, so "nothing
 * yields in the swap" is executable rather than argued.
 *
 * OWNERSHIP CHECK: when the fake DMA lands (or a TX source is read), the
 * buffer must lie inside the CURRENT "WiFi_SPI" partition. After a swap the old
 * region belongs to SD_write / USB_write, so a transfer using the old buffer
 * corrupts them -- that is what this catches, along with a copy from the wrong
 * buffer (payload check).
 *
 * The driver source and the splice are parameters, so the Makefile's mutation
 * targets can point them at deliberately broken copies.
 * ========================================================================== */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "test_framework.h"

/* ---- FreeRTOS stand-ins ---- */
typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
static TickType_t gTick;
static int gSuspendDepth;
static int gViolations;          /* something that may block ran while suspended */
static void (*gResumeHook)(void);  /* one-shot, when depth returns to 0 */
static void (*gDelayHook)(void);   /* one-shot, from vTaskDelay (a yield) */
static void (*gPendHook)(void);    /* one-shot, from the driver's sem pend */

static void vTaskSuspendAll(void) { gSuspendDepth++; }
static int xTaskResumeAll(void) {
    gSuspendDepth--;
    if (gSuspendDepth == 0 && gResumeHook) {
        void (*h)(void) = gResumeHook; gResumeHook = NULL; h();
    }
    return 0;
}
static TickType_t xTaskGetTickCount(void) { return gTick; }
static void vTaskDelay(TickType_t t) {
    if (gSuspendDepth > 0) gViolations++;
    gTick += t;
    if (gDelayHook) { void (*h)(void) = gDelayHook; gDelayHook = NULL; h(); }
}
void test1279_log_e(void) { if (gSuspendDepth > 0) gViolations++; }

#include "CoherentPool.h"

/* ---- the driver under test, verbatim ---- */
#ifndef WINC_SPI_SRC
#define WINC_SPI_SRC "../../firmware/src/config/default/driver/winc/dev/spi/wdrv_winc_spi.c"
#endif
#include WINC_SPI_SRC

/* ---- consumers the swap installs ---- */
static int gUsbDmaSets, gSdSets;
static uint8_t *gUsbDmaBuf, *gSdBuf;
void UsbCdc_SetDmaWriteBuffer(uint8_t* b, uint32_t n) {
    (void)n; if (gSuspendDepth == 0) gViolations++;  /* must be inside the block */
    gUsbDmaSets++; gUsbDmaBuf = b;
}
void sd_card_manager_SetWriteBuffer(uint8_t* b, uint32_t n) {
    (void)n; if (gSuspendDepth > 0) gViolations++;   /* takes a mutex */
    gSdSets++; gSdBuf = b;
}

/* ---- the REAL swap, spliced ---- */
#include "gen_1279_swap.h"

/* ---- fake SPI: record the buffer, land the data when the driver pends ---- */
static struct {
    bool rxPending, txPending;
    void *rxTarget; size_t rxN;
    const void *txSrc; size_t txN;
    int addInvalidOnce;   /* TransferAdd returns INVALID this many times */
    uint8_t rxPayload[64];
    uint8_t wire[64];     /* what a TX put on the wire */
    bool wireOwned, rxOwned;
} gSpi;

static bool in_wifi_partition(const void *p, size_t n) {
    CoherentPoolInfo info; CoherentPool_GetInfo(&info);
    for (uint32_t i = 0; i < info.partitionCount; i++) {
        if (strcmp(info.partitions[i].name, "WiFi_SPI") == 0) {
            const uint8_t *a = info.partitions[i].ptr;
            return (const uint8_t *)p >= a &&
                   (const uint8_t *)p + n <= a + info.partitions[i].size;
        }
    }
    return false;
}
void test1279_spi_rx_add(void *rx, size_t n, DRV_SPI_TRANSFER_HANDLE *h) {
    if (gSpi.addInvalidOnce > 0) { gSpi.addInvalidOnce--; *h = DRV_SPI_TRANSFER_HANDLE_INVALID; return; }
    gSpi.rxPending = true; gSpi.rxTarget = rx; gSpi.rxN = n; *h = 1;
}
void test1279_spi_tx_add(const void *tx, size_t n, DRV_SPI_TRANSFER_HANDLE *h) {
    if (gSpi.addInvalidOnce > 0) { gSpi.addInvalidOnce--; *h = DRV_SPI_TRANSFER_HANDLE_INVALID; return; }
    gSpi.txPending = true; gSpi.txSrc = tx; gSpi.txN = n; *h = 2;
}
OSAL_RESULT test1279_sem_pend(OSAL_SEM_HANDLE_TYPE *s) {
    (void)s;
    if (gSuspendDepth > 0) gViolations++;
    if (gPendHook) { void (*hk)(void) = gPendHook; gPendHook = NULL; hk(); }
    if (gSpi.rxPending) {            /* the DMA lands in the buffer it was given */
        gSpi.rxOwned = in_wifi_partition(gSpi.rxTarget, gSpi.rxN);
        memcpy(gSpi.rxTarget, gSpi.rxPayload, gSpi.rxN);
        gSpi.rxPending = false;
    }
    if (gSpi.txPending) {            /* the DMA reads the buffer it was given */
        gSpi.wireOwned = in_wifi_partition(gSpi.txSrc, gSpi.txN);
        memcpy(gSpi.wire, gSpi.txSrc, gSpi.txN);
        gSpi.txPending = false;
    }
    return OSAL_RESULT_TRUE;
}

/* ---- scenario helpers ---- */
#define N 32u
#define OLD_SD 4096u
#define OLD_USB 4096u
#define OLD_WIFI 2048u
#define NEW_SD 65536u
#define NEW_USB 16384u
#define NEW_WIFI 32768u

static uint8_t gOutBuf[64];
static bool gOutOk;
static bool gSwapResult;

static void reset_world(void) {
    CoherentPool_Init();
    CoherentPool_Reset();
    gTick = 0; gSuspendDepth = 0; gViolations = 0;
    gResumeHook = gDelayHook = gPendHook = NULL;
    gUsbDmaSets = gSdSets = 0; gUsbDmaBuf = gSdBuf = NULL;
    memset(&gSpi, 0, sizeof(gSpi));
    for (unsigned i = 0; i < N; i++) gSpi.rxPayload[i] = (uint8_t)(0xA0 + i);
    memset(gOutBuf, 0, sizeof(gOutBuf));
    /* the boot layout: SD, USB, WiFi in that order, smaller WiFi buffer */
    (void)CoherentPool_Alloc("SD_write", OLD_SD);
    (void)CoherentPool_Alloc("USB_write", OLD_USB);
    uint8_t *w = CoherentPool_Alloc("WiFi_SPI", OLD_WIFI);
    WDRV_WINC_SPI_SetBuffer(w, OLD_WIFI);
}
static void do_receive(void) {
    gOutOk = WDRV_WINC_SPIReceive(gOutBuf, N);
}
static void do_swap(void) {
    gSwapResult = SCPI_SwapCoherentDmaBuffers(NEW_SD, NEW_USB, NEW_WIFI);
}
static bool payload_intact(void) {
    return memcmp(gOutBuf, gSpi.rxPayload, N) == 0;
}
static void snapshot(CoherentPoolInfo *i) { CoherentPool_GetInfo(i); }
static bool same_pool(const CoherentPoolInfo *a, const CoherentPoolInfo *b) {
    if (a->usedBytes != b->usedBytes || a->partitionCount != b->partitionCount) return false;
    for (uint32_t i = 0; i < a->partitionCount; i++) {
        if (a->partitions[i].ptr != b->partitions[i].ptr ||
            a->partitions[i].size != b->partitions[i].size ||
            strcmp(a->partitions[i].name, b->partitions[i].name) != 0) return false;
    }
    return true;
}

/* T1: a receive is in flight (DMA submitted, not landed) when the swap runs.
 * The swap must refuse, change nothing, and the receive must see its data. */
TEST(test_swap_refused_while_receive_in_flight) {
    reset_world();
    CoherentPoolInfo before, after; snapshot(&before);
    uint8_t *bufBefore = (uint8_t *)before.partitions[2].ptr;
    gPendHook = do_swap;
    do_receive();
    snapshot(&after);
    ASSERT_TRUE(gOutOk);
    ASSERT_FALSE(gSwapResult);
    ASSERT_TRUE(same_pool(&before, &after));
    ASSERT_EQ(0, gUsbDmaSets);
    ASSERT_EQ(0, gSdSets);
    ASSERT_TRUE(gSpi.rxOwned);
    ASSERT_TRUE(payload_intact());
    ASSERT_EQ(0, gViolations);
    /* once the receive is over the same swap goes through and the next
     * receive uses the new buffer */
    do_swap();
    ASSERT_TRUE(gSwapResult);
    CoherentPoolInfo done; snapshot(&done);
    ASSERT_TRUE(done.partitions[2].ptr != bufBefore || done.partitions[2].size != OLD_WIFI);
    gSpi.rxOwned = false; do_receive();
    ASSERT_TRUE(gOutOk && gSpi.rxOwned && payload_intact());
    ASSERT_EQ(0, gViolations);
}

/* T2: a SEND is between its copy into the staging buffer and the TransferAdd
 * (the handle is still INVALID; the add retry yields). The old handles-only
 * "idle" read true here. */
TEST(test_swap_refused_in_send_pre_handle_window) {
    reset_world();
    for (unsigned i = 0; i < N; i++) ((uint8_t *)gOutBuf)[i] = (uint8_t)(0x10 + i);
    uint8_t src[N]; memcpy(src, gOutBuf, N);
    CoherentPoolInfo before, after; snapshot(&before);
    gSpi.addInvalidOnce = 1;
    gDelayHook = do_swap;            /* runs from the add-retry vTaskDelay */
    bool ok = WDRV_WINC_SPISend(src, N);
    snapshot(&after);
    ASSERT_TRUE(ok);
    ASSERT_FALSE(gSwapResult);
    ASSERT_TRUE(same_pool(&before, &after));
    ASSERT_TRUE(gSpi.wireOwned);
    ASSERT_TRUE(memcmp(gSpi.wire, src, N) == 0);
    ASSERT_EQ(0, gViolations);
}

/* T3: the codex r3 interleaving. The WINC task starts a receive the INSTANT
 * the scheduler is resumed. The new buffer must already be installed. */
TEST(test_receive_started_at_resume_uses_new_buffer) {
    reset_world();
    gResumeHook = do_receive;
    do_swap();
    ASSERT_TRUE(gSwapResult);
    ASSERT_TRUE(gOutOk);
    ASSERT_TRUE(gSpi.rxOwned);
    ASSERT_TRUE(payload_intact());
    ASSERT_EQ(0, gViolations);
}

/* T4: a transfer that lands after a clean swap, sized to the NEW buffer, and
 * the swap installs every consumer inside the block / after it as required. */
TEST(test_swap_installs_each_consumer_once) {
    reset_world();
    do_swap();
    ASSERT_TRUE(gSwapResult);
    ASSERT_EQ(1, gUsbDmaSets);
    ASSERT_EQ(1, gSdSets);
    ASSERT_TRUE(gUsbDmaBuf != NULL && gSdBuf != NULL);
    ASSERT_EQ(0, gSuspendDepth);
    ASSERT_EQ(0, gViolations);
    CoherentPoolInfo i; snapshot(&i);
    ASSERT_EQ(3, (int)i.partitionCount);
}

/* T5: a layout that does not fit is refused with nothing changed. */
TEST(test_oversize_layout_refused_untouched) {
    reset_world();
    CoherentPoolInfo before, after; snapshot(&before);
    bool r = SCPI_SwapCoherentDmaBuffers(CoherentPool_TotalSize(), NEW_USB, NEW_WIFI);
    snapshot(&after);
    ASSERT_FALSE(r);
    ASSERT_TRUE(same_pool(&before, &after));
    ASSERT_EQ(0, gUsbDmaSets + gSdSets);
    ASSERT_EQ(0, gSuspendDepth);
}

int main(void) {
    RUN(test_swap_refused_while_receive_in_flight);
    RUN(test_swap_refused_in_send_pre_handle_window);
    RUN(test_receive_started_at_resume_uses_new_buffer);
    RUN(test_swap_installs_each_consumer_once);
    RUN(test_oversize_layout_refused_untouched);
    return TEST_SUMMARY();
}
