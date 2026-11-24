/* ==========================================================================
 * test_1279_sd_idle_buffer_restore.c -- issue #1279
 *
 * WHAT WAS WRONG
 *
 * Auto-balance (Streaming_ComputeAutoBuffers, streaming.c) sizes the SD
 * circular and the SD DMA write buffer for the stream being set up. For any
 * stream SD does not consume -- WiFi, or USB without SD logging -- it gives SD
 * the floor: STREAMING_SD_CIRCULAR_MIN (4 KB) and SD_CARD_MANAGER_MIN_WBUFFER_SIZE
 * (512 B). Nothing grew them back when that stream stopped, so
 * SYSTem:STORage:SD:BENCHmark, which drains through both, measured ~4.5x below
 * the card's speed until the next SD stream or a reboot (measured on an NQ1,
 * 2026-11-19, in the issue).
 *
 * THE FIX, and what this file can and cannot see
 *
 * Before SD:BENCHmark and SD:GET arm, SCPI_RestoreIdleSdBuffers()
 * (SCPIInterface.c) re-partitions to the idle layout if SdIdleRestoreDue() says
 * the SD buffers are smaller than idle. SCPIInterface.c and SCPIStorageSD.c are
 * not host-compilable (FreeRTOS, libscpi, Harmony), so this binary runs the two
 * PURE pieces the decision is made of, spliced verbatim by the Makefile:
 *
 *   gen_1279_autobuf.h  Streaming_ComputeAutoBuffers   (streaming.c)
 *   gen_1279_due.h      SdIdleRestoreDue               (SCPIInterface.c)
 *
 * and asserts that the real predicate fires on exactly the partitions the real
 * sizing produces after a non-SD stream, and on nothing else. That two-function
 * chain is the part a model would get wrong. The CALL SITES -- that BENCHmark
 * and GET actually ask, before they arm, and that BENCHmark refuses on failure
 * -- are pinned against the real source by check_1279_sd_idle_restore.py,
 * which the same Makefile target runs.
 *
 * On origin/main before the fix there is no SdIdleRestoreDue to splice, so the
 * Makefile recipe fails the build with a message saying so, and the call-site
 * check fails on the missing call. Both are run in the PR description.
 *
 * The one thing below that IS a copy: PrepareStreamingBuffers' four-line
 * sector/alignment/floor treatment of the SD DMA size between the sizing and
 * the install (prepare_sd_dma below). It is mid-function, so it cannot be
 * spliced; it only rounds and floors, and the floor is the constant this test
 * reads from the real header.
 * ========================================================================== */
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "test_framework.h"

/* Real, host-includable headers (std includes only). */
#include "StreamingBufferPool.h"   /* STREAMING_*_MIN / defaults, ENCODER_* */
#include "CoherentPool.h"          /* COHERENT_POOL_DEFAULT_SIZE / _ALIGNMENT */

/* Real #defines out of sd_card_manager.h, UsbCdc.h and streaming.h, which are
 * not host-includable; extracted by the Makefile. */
#include "gen_1279_defs.h"

/* ---- Minimal stand-ins for the types Streaming_ComputeAutoBuffers reads ---- */
typedef enum {
    StreamingInterface_USB = 0,
    StreamingInterface_WiFi = 1,
    StreamingInterface_SD = 2,
    StreamingInterface_UsbAndSd = 3,
} StreamingInterface;

typedef struct { StreamingInterface ActiveInterface; } StreamingRuntimeConfig;
typedef struct { bool enable; char file[41]; } sd_card_manager_settings_t;

enum { BOARDRUNTIME_STREAMING_CONFIGURATION, BOARDRUNTIME_SD_CARD_SETTINGS };

static StreamingRuntimeConfig g_sc;
static sd_card_manager_settings_t g_sd;

static void* BoardRunTimeConfig_Get(int which) {
    return (which == BOARDRUNTIME_STREAMING_CONFIGURATION) ? (void*)&g_sc
                                                            : (void*)&g_sd;
}

uint32_t CoherentPool_TotalSize(void) { return COHERENT_POOL_DEFAULT_SIZE; }

/* ---- The REAL functions under test, spliced verbatim ---- */
#include "gen_1279_autobuf.h"
#include "gen_1279_due.h"

/* ---- The partition a stream on `iface` leaves behind (auto mode) ---- */
typedef struct { uint32_t sdCirc; uint32_t sdDma; } SdLayout;

/* COPY of PrepareStreamingBuffers' SD-DMA treatment (see header comment). */
static uint32_t prepare_sd_dma(uint32_t sdDmaSize) {
    sdDmaSize &= ~(511U);
    if (sdDmaSize < 512) sdDmaSize = 512;
    sdDmaSize &= ~(COHERENT_POOL_ALIGNMENT - 1);
    if (sdDmaSize < SD_CARD_MANAGER_MIN_WBUFFER_SIZE)
        sdDmaSize = SD_CARD_MANAGER_MIN_WBUFFER_SIZE;
    return sdDmaSize;
}

static SdLayout after_stream(StreamingInterface iface, bool sdEnable,
                             const char *file) {
    uint32_t usb, wifi, sdCirc, sdDma, usbDma, wifiDma, enc;
    g_sc.ActiveInterface = iface;
    g_sd.enable = sdEnable;
    strncpy(g_sd.file, file, sizeof(g_sd.file) - 1);
    g_sd.file[sizeof(g_sd.file) - 1] = '\0';
    Streaming_ComputeAutoBuffers(&usb, &wifi, &sdCirc, &sdDma, &usbDma,
                                 &wifiDma, &enc);
    /* StreamingBufferPool_Partition clamps the SD circular to its floor. */
    if (sdCirc < STREAMING_SD_CIRCULAR_MIN) sdCirc = STREAMING_SD_CIRCULAR_MIN;
    SdLayout l = { sdCirc, prepare_sd_dma(sdDma) };
    return l;
}

/* The boot layout: app_freertos.c's StreamingBufferPool_Init SD argument and
 * sd_card_manager_Init's 64 KB write buffer. Also what the fixed (idle) branch
 * of PrepareStreamingBuffers installs with an all-zero MemoryConfig. */
static const SdLayout kBoot = { SD_CARD_MANAGER_DEFAULT_CIRCULAR_SIZE,
                                SD_CARD_MANAGER_CONF_WBUFFER_SIZE };

/* ------------------------------------------------------------------------ */

TEST(boot_layout_needs_no_restore) {
    /* Without this an SD op on a fresh boot would re-partition every time. */
    ASSERT_FALSE(SdIdleRestoreDue(kBoot.sdCirc, kBoot.sdDma, 0u));
}

TEST(wifi_stream_leaves_small_sd_buffers_and_restore_is_due) {
    SdLayout l = after_stream(StreamingInterface_WiFi, true, "log.bin");
    /* The issue's measured state (SD:BUFfer? = 4096), from the real sizing. */
    ASSERT_EQ(l.sdCirc, STREAMING_SD_CIRCULAR_MIN);
    ASSERT_EQ(l.sdDma, SD_CARD_MANAGER_MIN_WBUFFER_SIZE);
    ASSERT_TRUE(SdIdleRestoreDue(l.sdCirc, l.sdDma, 0u));
}

TEST(usb_only_stream_leaves_small_sd_buffers_and_restore_is_due) {
    /* USB with SD disabled, and USB with SD enabled but no file: both are
     * "SD is not a consumer" to the real sizing. */
    SdLayout a = after_stream(StreamingInterface_USB, false, "");
    SdLayout b = after_stream(StreamingInterface_USB, true, "");
    ASSERT_EQ(a.sdCirc, STREAMING_SD_CIRCULAR_MIN);
    ASSERT_EQ(b.sdCirc, STREAMING_SD_CIRCULAR_MIN);
    ASSERT_TRUE(SdIdleRestoreDue(a.sdCirc, a.sdDma, 0u));
    ASSERT_TRUE(SdIdleRestoreDue(b.sdCirc, b.sdDma, 0u));
}

TEST(sd_interface_with_sd_disabled_is_due) {
    /* The degenerate no-consumer case the sizing comment names. */
    SdLayout l = after_stream(StreamingInterface_SD, false, "log.bin");
    ASSERT_TRUE(SdIdleRestoreDue(l.sdCirc, l.sdDma, 0u));
}

TEST(sd_logging_streams_need_no_restore) {
    /* SD-only and USB+SD logging leave SD buffers at or above idle, so the
     * download-after-logging path does not pay a re-partition. */
    SdLayout sd  = after_stream(StreamingInterface_SD, true, "log.bin");
    SdLayout usd = after_stream(StreamingInterface_UsbAndSd, true, "log.bin");
    ASSERT_TRUE(sd.sdCirc >= SD_CARD_MANAGER_DEFAULT_CIRCULAR_SIZE);
    ASSERT_TRUE(sd.sdDma >= SD_CARD_MANAGER_CONF_WBUFFER_SIZE);
    ASSERT_TRUE(usd.sdDma >= SD_CARD_MANAGER_CONF_WBUFFER_SIZE);
    ASSERT_FALSE(SdIdleRestoreDue(sd.sdCirc, sd.sdDma, 0u));
    ASSERT_FALSE(SdIdleRestoreDue(usd.sdCirc, usd.sdDma, 0u));
}

TEST(restore_converges) {
    /* After the idle re-partition the predicate must be false, or every SD
     * operation would re-partition again. Auto mode and a static config. */
    ASSERT_FALSE(SdIdleRestoreDue(kBoot.sdCirc, kBoot.sdDma, 0u));
    ASSERT_FALSE(SdIdleRestoreDue(8192u, SD_CARD_MANAGER_CONF_WBUFFER_SIZE,
                                  8192u));
}

TEST(static_sd_buffer_config_is_honoured) {
    /* SYSTem:MEMory:SD:BUFfer 4096 is the user's idle size: not "small". */
    ASSERT_FALSE(SdIdleRestoreDue(4096u, SD_CARD_MANAGER_CONF_WBUFFER_SIZE,
                                  4096u));
    /* A static size larger than the live partition IS due (it is applied). */
    ASSERT_TRUE(SdIdleRestoreDue(4096u, SD_CARD_MANAGER_CONF_WBUFFER_SIZE,
                                 8192u));
    /* Static SD circular met, but the DMA buffer an earlier auto WiFi stream
     * left at 512 B still throttles every card write: due. */
    SdLayout l = after_stream(StreamingInterface_WiFi, true, "log.bin");
    ASSERT_TRUE(SdIdleRestoreDue(4096u, l.sdDma, 4096u));
}

TEST(each_buffer_alone_triggers) {
    /* Either collapsed buffer alone throttles the benchmark. */
    ASSERT_TRUE(SdIdleRestoreDue(STREAMING_SD_CIRCULAR_MIN,
                                 SD_CARD_MANAGER_CONF_WBUFFER_SIZE, 0u));
    ASSERT_TRUE(SdIdleRestoreDue(SD_CARD_MANAGER_DEFAULT_CIRCULAR_SIZE,
                                 SD_CARD_MANAGER_MIN_WBUFFER_SIZE, 0u));
}

int main(void) {
    printf("test_1279_sd_idle_buffer_restore\n");
    RUN(boot_layout_needs_no_restore);
    RUN(wifi_stream_leaves_small_sd_buffers_and_restore_is_due);
    RUN(usb_only_stream_leaves_small_sd_buffers_and_restore_is_due);
    RUN(sd_interface_with_sd_disabled_is_due);
    RUN(sd_logging_streams_need_no_restore);
    RUN(restore_converges);
    RUN(static_sd_buffer_config_is_honoured);
    RUN(each_buffer_alone_triggers);
    return TEST_SUMMARY();
}
