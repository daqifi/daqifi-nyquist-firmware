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
 * r4 (codex): the span must prove EVERY coherent-DMA owner idle, not just the
 * WINC. The REAL UsbCdc_DmaSwap* and sd_card_manager_DmaSwap* are spliced
 * verbatim too (USB_SWAP_SRC / SD_SWAP_SRC), against fakes of the USB and SD
 * state they read and of the mutex (zero-wait take; a nonzero wait while
 * suspended is a violation). The scenarios DRIVE the interleaving: the swap's
 * own retry yield flips the CDC to PROCESS and starts a transfer from the old
 * region, and a transfer that is still running when the swap returns, or whose
 * region was handed to another owner, fails the test.
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

/* mutex: zero-wait take only while suspended */
typedef struct { bool held; } FakeSem;
typedef FakeSem *SemaphoreHandle_t;
#define pdTRUE 1
#define pdFALSE 0
static int xSemaphoreTake(SemaphoreHandle_t m, TickType_t t) {
    if (t != 0 && gSuspendDepth > 0) gViolations++;   /* would block suspended */
    if (m->held) return pdFALSE;
    m->held = true; return pdTRUE;
}
static int xSemaphoreGive(SemaphoreHandle_t m) { m->held = false; return pdTRUE; }

#include "CoherentPool.h"

/* ---- the driver under test, verbatim ---- */
#ifndef WINC_SPI_SRC
#define WINC_SPI_SRC "../../firmware/src/config/default/driver/winc/dev/spi/wdrv_winc_spi.c"
#endif
#include WINC_SPI_SRC

/* ---- USB state the real UsbCdc_DmaSwap* read ---- */
#define USB_CDC_STATE_WAIT 1
#define USB_CDC_STATE_PROCESS 2
#define USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID 0
typedef struct { uint32_t n; } CircularBuf_t;
static uint32_t CircularBuf_NumBytesAvailable(CircularBuf_t *c) { return c->n; }
static void CircularBuf_Reset(CircularBuf_t *c) { c->n = 0; }
static FakeSem gUsbMutex, gSdMutex;
static struct {
    SemaphoreHandle_t wMutex;
    int state;
    volatile bool writeInProgress;
    uint32_t writeTransferHandle;
    CircularBuf_t wCirbuf;
    uint8_t *dmaWriteBuffer;
    uint32_t dmaWriteBufferSize;
} gRunTimeUsbSttings;
static bool gUsbSwapDiscardRing;
#include "gen_1279_usb.h"

/* ---- SD state the real sd_card_manager_DmaSwap* read ---- */
#define SD_CARD_MANAGER_PROCESS_STATE_IDLE 0
#define SD_CARD_MANAGER_PROCESS_STATE_INIT 1
#define SD_CARD_MANAGER_PROCESS_STATE_WRITE_TO_FILE 2
#define SD_CARD_MANAGER_MODE_NONE 0
#define SD_CARD_MANAGER_MODE_READ 1
#define SD_CARD_MANAGER_MODE_WRITE 2
static struct {
    SemaphoreHandle_t wMutex;
    int currentProcessState;
    uint8_t *writeBuffer;
    uint32_t writeBufferSize, writeBufferLength, sdCardWriteBufferOffset;
} gSDCardData;
struct SdSet { int mode; };
static struct SdSet gSdSettings;
static struct SdSet *gpSDCardSettings = &gSdSettings;
#include "gen_1279_sd.h"

/* a fake USB task: with the CDC in PROCESS and a ring, it starts a transfer
 * from the CURRENT staging buffer, exactly as UsbCdc_BeginWrite would */
static struct { bool inFlight; uint8_t *src; int completions; bool ownedAtCompletion; } gXfer;
static bool gPumpDrains;       /* UsbCdc_PumpWrite really drains the ring */
static bool in_partition(const char *name, const void *p, size_t n);
static void fake_usb_begin_write(void) {
    if (gRunTimeUsbSttings.state != USB_CDC_STATE_PROCESS) return;
    if (gRunTimeUsbSttings.writeInProgress ||
        gRunTimeUsbSttings.writeTransferHandle != 0) return;
    if (gRunTimeUsbSttings.wCirbuf.n == 0) return;
    gRunTimeUsbSttings.writeInProgress = true;
    gRunTimeUsbSttings.writeTransferHandle = 1;
    gRunTimeUsbSttings.wCirbuf.n = 0;
    gXfer.inFlight = true; gXfer.src = gRunTimeUsbSttings.dmaWriteBuffer;
}
static void fake_usb_complete(void) {
    if (!gXfer.inFlight) return;
    gXfer.ownedAtCompletion = in_partition("USB_write", gXfer.src, 64);
    gXfer.completions++; gXfer.inFlight = false;
    gRunTimeUsbSttings.writeTransferHandle = 0;
    gRunTimeUsbSttings.writeInProgress = false;
}
void UsbCdc_PumpWrite(void) { if (gPumpDrains) fake_usb_begin_write(); }

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

static bool in_partition(const char *name, const void *p, size_t n) {
    CoherentPoolInfo info; CoherentPool_GetInfo(&info);
    for (uint32_t i = 0; i < info.partitionCount; i++) {
        if (strcmp(info.partitions[i].name, name) == 0) {
            const uint8_t *a = info.partitions[i].ptr;
            return (const uint8_t *)p >= a &&
                   (const uint8_t *)p + n <= a + info.partitions[i].size;
        }
    }
    return false;
}
static bool in_wifi_partition(const void *p, size_t n) {
    return in_partition("WiFi_SPI", p, n);
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
static bool gKeep = true;       /* keepQueuedOutput: the idle restore */

static void reset_world(void) {
    CoherentPool_Init();
    CoherentPool_Reset();
    gTick = 0; gSuspendDepth = 0; gViolations = 0;
    gResumeHook = gDelayHook = gPendHook = NULL;
    memset(&gUsbMutex, 0, sizeof gUsbMutex); memset(&gSdMutex, 0, sizeof gSdMutex);
    memset(&gRunTimeUsbSttings, 0, sizeof gRunTimeUsbSttings);
    gRunTimeUsbSttings.wMutex = &gUsbMutex; gRunTimeUsbSttings.state = USB_CDC_STATE_WAIT;
    memset(&gSDCardData, 0, sizeof gSDCardData);
    gSDCardData.wMutex = &gSdMutex;
    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
    gSdSettings.mode = SD_CARD_MANAGER_MODE_NONE;
    memset(&gXfer, 0, sizeof gXfer); gPumpDrains = false; gUsbSwapDiscardRing = false;
    memset(&gSpi, 0, sizeof(gSpi));
    for (unsigned i = 0; i < N; i++) gSpi.rxPayload[i] = (uint8_t)(0xA0 + i);
    memset(gOutBuf, 0, sizeof(gOutBuf));
    /* the boot layout: SD, USB, WiFi in that order, smaller WiFi buffer */
    (void)CoherentPool_Alloc("SD_write", OLD_SD);
    (void)CoherentPool_Alloc("USB_write", OLD_USB);
    uint8_t *w = CoherentPool_Alloc("WiFi_SPI", OLD_WIFI);
    WDRV_WINC_SPI_SetBuffer(w, OLD_WIFI);
    {
        CoherentPoolInfo i0; CoherentPool_GetInfo(&i0);
        gSDCardData.writeBuffer = (uint8_t *)i0.partitions[0].ptr;
        gSDCardData.writeBufferSize = OLD_SD;
        gRunTimeUsbSttings.dmaWriteBuffer = (uint8_t *)i0.partitions[1].ptr;
        gRunTimeUsbSttings.dmaWriteBufferSize = OLD_USB;
    }
}
static void do_receive(void) {
    gOutOk = WDRV_WINC_SPIReceive(gOutBuf, N);
}
static void do_swap(void) {
    gSwapResult = SCPI_SwapCoherentDmaBuffers(NEW_SD, NEW_USB, NEW_WIFI, gKeep);
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

/* T4: a clean swap installs each consumer once, inside the span, and both
 * mutexes are released. */
TEST(test_swap_installs_each_consumer_once) {
    reset_world();
    do_swap();
    ASSERT_TRUE(gSwapResult);
    ASSERT_TRUE(in_partition("USB_write", gRunTimeUsbSttings.dmaWriteBuffer, NEW_USB));
    ASSERT_EQ(NEW_USB, (int)gRunTimeUsbSttings.dmaWriteBufferSize);
    ASSERT_TRUE(in_partition("SD_write", gSDCardData.writeBuffer, NEW_SD));
    ASSERT_EQ(NEW_SD, (int)gSDCardData.writeBufferSize);
    ASSERT_FALSE(gUsbMutex.held);
    ASSERT_FALSE(gSdMutex.held);
    ASSERT_EQ(0, gSuspendDepth);
    ASSERT_EQ(0, gViolations);
    CoherentPoolInfo i; snapshot(&i);
    ASSERT_EQ(3, (int)i.partitionCount);
}

/* T5: a layout that does not fit is refused with nothing changed. */
TEST(test_oversize_layout_refused_untouched) {
    reset_world();
    CoherentPoolInfo before, after; snapshot(&before);
    bool r = SCPI_SwapCoherentDmaBuffers(CoherentPool_TotalSize(), NEW_USB, NEW_WIFI, true);
    snapshot(&after);
    ASSERT_FALSE(r);
    ASSERT_TRUE(same_pool(&before, &after));
    ASSERT_EQ(0, gSuspendDepth);
}


/* ===================== r4: every coherent-DMA owner ===================== */
static void assert_untouched(const CoherentPoolInfo *before) {
    CoherentPoolInfo after; snapshot(&after);
    ASSERT_TRUE(same_pool(before, &after));
    ASSERT_FALSE(gUsbMutex.held);
    ASSERT_FALSE(gSdMutex.held);
    ASSERT_EQ(0, gSuspendDepth);
}

/* the codex r4 interleaving: USB is in WAIT with 4096 B queued and the WINC is
 * busy, so the swap yields. During the yield DTR rises, the CDC goes PROCESS
 * and the USB task submits the ring from the OLD staging region. The swap must
 * NOT reset the pool while that transfer is in flight. */
static void usb_wakes_and_submits(void) {
    gRunTimeUsbSttings.state = USB_CDC_STATE_PROCESS;
    fake_usb_begin_write();
    gWincSpiRxBusy = false;          /* WINC goes idle in the same yield */
    gDelayHook = fake_usb_complete;  /* the transfer finishes on the NEXT yield */
}
TEST(test_usb_transfer_started_in_retry_yield_blocks_the_reset) {
    reset_world();
    gRunTimeUsbSttings.wCirbuf.n = 4096;
    gWincSpiRxBusy = true;
    gDelayHook = usb_wakes_and_submits;
    do_swap();
    ASSERT_TRUE(gSwapResult);
    ASSERT_EQ(1, gXfer.completions);       /* it was waited for ... */
    ASSERT_TRUE(gXfer.ownedAtCompletion);  /* ... and finished on a region USB still owned */
    ASSERT_FALSE(gXfer.inFlight);
    ASSERT_EQ(0, gViolations);
}

/* the transfer never completes: refuse after 1 s with nothing changed */
static void usb_wakes_and_hangs(void) {
    gRunTimeUsbSttings.state = USB_CDC_STATE_PROCESS;
    fake_usb_begin_write();
    gWincSpiRxBusy = false;
}
TEST(test_usb_transfer_that_never_ends_refuses_untouched) {
    reset_world();
    gRunTimeUsbSttings.wCirbuf.n = 4096;
    gWincSpiRxBusy = true;
    CoherentPoolInfo before; snapshot(&before);
    uint8_t *oldUsb = gRunTimeUsbSttings.dmaWriteBuffer;
    gDelayHook = usb_wakes_and_hangs;
    do_swap();
    ASSERT_FALSE(gSwapResult);
    ASSERT_TRUE(gXfer.inFlight);
    assert_untouched(&before);
    ASSERT_TRUE(gRunTimeUsbSttings.dmaWriteBuffer == oldUsb);
    ASSERT_EQ(OLD_USB, (int)gRunTimeUsbSttings.dmaWriteBufferSize);
    ASSERT_EQ(0, gViolations);
}

/* each half of the USB "transfer in progress" predicate, alone, must refuse
 * (submit claims writeInProgress BEFORE the handle exists; the handle is what
 * remains after a claim was released by a teardown) */
static void clear_usb_claim(void) {
    gRunTimeUsbSttings.writeInProgress = false;
    gRunTimeUsbSttings.writeTransferHandle = 0;
}
TEST(test_usb_claim_alone_refuses_until_released) {
    reset_world();
    gRunTimeUsbSttings.writeInProgress = true;      /* handle still INVALID */
    gDelayHook = clear_usb_claim;
    do_swap();
    ASSERT_TRUE(gSwapResult);
    ASSERT_TRUE(gTick >= 1);
    ASSERT_EQ(0, gViolations);
}
TEST(test_usb_handle_alone_refuses_until_released) {
    reset_world();
    gRunTimeUsbSttings.writeTransferHandle = 7;     /* claim already released */
    gDelayHook = clear_usb_claim;
    do_swap();
    ASSERT_TRUE(gSwapResult);
    ASSERT_TRUE(gTick >= 1);
    ASSERT_EQ(0, gViolations);
}

/* a task is mid-UsbCdc_BeginWrite (it holds wMutex and has read the OLD size as
 * its length cap): committing a smaller buffer under it would overflow. */
static void usb_writer_leaves(void) { gUsbMutex.held = false; }
TEST(test_usb_task_mid_begin_write_blocks_the_swap) {
    reset_world();
    gUsbMutex.held = true;
    gDelayHook = usb_writer_leaves;
    do_swap();
    ASSERT_TRUE(gSwapResult);
    ASSERT_TRUE(gTick >= 1);
    ASSERT_EQ(0, gViolations);
}

/* WAIT + queued: not deliverable now, so it is discarded atomically with the
 * swap -- DTR rising right after resume finds an empty ring and the NEW buffer. */
static void dtr_rises_at_resume(void) {
    gRunTimeUsbSttings.state = USB_CDC_STATE_PROCESS;
    fake_usb_begin_write();
}
TEST(test_undeliverable_ring_is_discarded_with_the_swap) {
    reset_world();
    gRunTimeUsbSttings.wCirbuf.n = 4096;
    gResumeHook = dtr_rises_at_resume;
    do_swap();
    ASSERT_TRUE(gSwapResult);
    ASSERT_EQ(0, (int)gRunTimeUsbSttings.wCirbuf.n);
    ASSERT_FALSE(gXfer.inFlight);               /* nothing torn went out */
    ASSERT_TRUE(in_partition("USB_write", gRunTimeUsbSttings.dmaWriteBuffer, NEW_USB));
    ASSERT_EQ(0, gViolations);
}

/* PROCESS + queued and nothing drains it: refuse (never discard), ring intact */
TEST(test_deliverable_ring_refuses_not_discards) {
    reset_world();
    gRunTimeUsbSttings.state = USB_CDC_STATE_PROCESS;
    gRunTimeUsbSttings.wCirbuf.n = 4096;
    CoherentPoolInfo before; snapshot(&before);
    do_swap();
    ASSERT_FALSE(gSwapResult);
    ASSERT_EQ(4096, (int)gRunTimeUsbSttings.wCirbuf.n);
    assert_untouched(&before);
    ASSERT_EQ(0, gViolations);
}

/* PROCESS + queued, and the swap's own pump delivers it: then it proceeds,
 * after the transfer ends */
TEST(test_deliverable_ring_is_pumped_then_swap_proceeds) {
    reset_world();
    gRunTimeUsbSttings.state = USB_CDC_STATE_PROCESS;
    gRunTimeUsbSttings.wCirbuf.n = 4096;
    gPumpDrains = true;
    gDelayHook = fake_usb_complete;
    do_swap();
    ASSERT_TRUE(gSwapResult);
    ASSERT_EQ(1, gXfer.completions);
    ASSERT_TRUE(gXfer.ownedAtCompletion);
    ASSERT_EQ(0, gViolations);
}

/* the streaming callers (keepQueuedOutput=false) keep their old behaviour:
 * the swap neither refuses on nor discards the ring (SetWriteBuffer does) */
TEST(test_streaming_caller_does_not_refuse_on_queued_bytes) {
    reset_world();
    gKeep = false;
    gRunTimeUsbSttings.state = USB_CDC_STATE_PROCESS;
    gRunTimeUsbSttings.wCirbuf.n = 4096;
    do_swap();
    gKeep = true;
    ASSERT_TRUE(gSwapResult);
    ASSERT_EQ(4096, (int)gRunTimeUsbSttings.wCirbuf.n);
    ASSERT_EQ(0, gViolations);
}

/* ---- SD write DMA owner ---- */
static void sd_goes_idle(void) {
    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_IDLE;
    gSdSettings.mode = SD_CARD_MANAGER_MODE_NONE;
    gSdMutex.held = false;
}
TEST(test_sd_write_in_flight_blocks_the_swap) {
    reset_world();
    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_WRITE_TO_FILE;
    gDelayHook = sd_goes_idle;
    do_swap();
    ASSERT_TRUE(gSwapResult);
    ASSERT_TRUE(gTick >= 1);
    ASSERT_EQ(0, gViolations);
}
TEST(test_sd_write_armed_blocks_the_swap) {
    reset_world();
    gSdSettings.mode = SD_CARD_MANAGER_MODE_WRITE;   /* armed, task not started */
    gDelayHook = sd_goes_idle;
    do_swap();
    ASSERT_TRUE(gSwapResult);
    ASSERT_TRUE(gTick >= 1);
    ASSERT_EQ(0, gViolations);
}
TEST(test_sd_task_mid_copy_blocks_the_swap) {
    reset_world();
    gSdMutex.held = true;
    gDelayHook = sd_goes_idle;
    do_swap();
    ASSERT_TRUE(gSwapResult);
    ASSERT_TRUE(gTick >= 1);
    ASSERT_EQ(0, gViolations);
}
TEST(test_stale_read_mode_does_not_block_the_swap) {
    reset_world();
    gSdSettings.mode = SD_CARD_MANAGER_MODE_READ;    /* finished op, mode left set */
    do_swap();
    ASSERT_TRUE(gSwapResult);
    ASSERT_EQ(0, (int)gTick);
}
TEST(test_sd_busy_for_good_refuses_untouched_and_releases_usb) {
    reset_world();
    gSDCardData.currentProcessState = SD_CARD_MANAGER_PROCESS_STATE_WRITE_TO_FILE;
    CoherentPoolInfo before; snapshot(&before);
    do_swap();
    ASSERT_FALSE(gSwapResult);
    assert_untouched(&before);
    ASSERT_EQ(0, gViolations);
}
TEST(test_winc_busy_for_good_releases_both_mutexes) {
    reset_world();
    gWincSpiRxBusy = true;
    CoherentPoolInfo before; snapshot(&before);
    do_swap();
    ASSERT_FALSE(gSwapResult);
    assert_untouched(&before);
    ASSERT_EQ(0, gViolations);
}

int main(void) {
    RUN(test_swap_refused_while_receive_in_flight);
    RUN(test_swap_refused_in_send_pre_handle_window);
    RUN(test_receive_started_at_resume_uses_new_buffer);
    RUN(test_swap_installs_each_consumer_once);
    RUN(test_oversize_layout_refused_untouched);
    RUN(test_usb_transfer_started_in_retry_yield_blocks_the_reset);
    RUN(test_usb_transfer_that_never_ends_refuses_untouched);
    RUN(test_usb_claim_alone_refuses_until_released);
    RUN(test_usb_handle_alone_refuses_until_released);
    RUN(test_usb_task_mid_begin_write_blocks_the_swap);
    RUN(test_undeliverable_ring_is_discarded_with_the_swap);
    RUN(test_deliverable_ring_refuses_not_discards);
    RUN(test_deliverable_ring_is_pumped_then_swap_proceeds);
    RUN(test_streaming_caller_does_not_refuse_on_queued_bytes);
    RUN(test_sd_write_in_flight_blocks_the_swap);
    RUN(test_sd_write_armed_blocks_the_swap);
    RUN(test_sd_task_mid_copy_blocks_the_swap);
    RUN(test_stale_read_mode_does_not_block_the_swap);
    RUN(test_sd_busy_for_good_refuses_untouched_and_releases_usb);
    RUN(test_winc_busy_for_good_releases_both_mutexes);
    return TEST_SUMMARY();
}
