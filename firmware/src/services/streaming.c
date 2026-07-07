#define LOG_LVL LOG_LEVEL_DEBUG
#define LOG_MODULE LOG_MODULE_STREAM
/*! @file streaming.c
 * @brief Real-time data streaming engine for analog and digital samples.
 *
 * This module manages the data acquisition and streaming pipeline:
 * 1. Deferred interrupt task: Collects samples from ADC/DIO into object pool
 * 2. Streaming task: Encodes samples (CSV/JSON/ProtoBuf) and writes to outputs
 *
 * Key features:
 * - Uses object pool (not heap) for O(1) sample allocation
 * - Supports 15kHz+ sample rates with 512-sample queue depth
 * - Single-interface streaming to prevent bandwidth contention
 * - FPU context saving for voltage conversion in streaming task
 *
 * @see AInSample.c for object pool implementation
 * @see csv_encoder.c for CSV output format
 */

#include "streaming.h"

#if PB_PROFILE_COUNTERS
#include <xc.h>  // for _CP0_GET_COUNT() — coprocessor 0 cycle counter
#endif

#include "HAL/ADC.h"
#include "HAL/DIO.h"
#include "HAL/DioProbe.h"
#include "JSON_Encoder.h"
#include "csv_encoder.h"
#include "DaqifiPB/DaqifiOutMessage.pb.h"
#include "DaqifiPB/NanoPB_Encoder.h"
#include "Util/Logger.h"
#include "Util/CircularBuffer.h"
#include "Util/StreamingBufferPool.h"
#include "Util/CoherentPool.h"
#include "UsbCdc/UsbCdc.h"
#include "../HAL/TimerApi/TimerApi.h"
#include "HAL/ADC/MC12bADC.h"
#include "sd_card_services/sd_card_manager.h"
#include "wifi_services/wifi_tcp_server.h"
#include "state/runtime/BoardRuntimeConfig.h"

// --- Test pattern streaming mode ---
// When non-zero, overrides ADC sample values with synthetic data patterns.
// Runtime-only (not persisted to NVM). Set via SCPI SYST:STR:TEST:PATtern.
// 32-bit atomic read on PIC32MZ — no critical section needed for reads/writes.
static volatile uint32_t gTestPattern = 0;       // 0=off, 1-4=pattern type
static uint64_t gTestPatternSampleCount = 0;      // Monotonic counter, reset on start

// Benchmark mode level (BENCHMARK_OFF/NOCAP/PIPELINE).
// Uses uint32_t for guaranteed 32-bit atomic access on PIC32MZ.
static volatile uint32_t gBenchmarkMode = BENCHMARK_OFF;

// #537: the shared (MODULE7) scan must run for ANY MC12b streaming session,
// independent of OnboardDiagEnabled.  Two consumers depend on it:
//   1. Type-2 USER channels — their conversions ARE the scan.
//   2. Type-1 result reads — since #292 these happen in
//      MC12bADC_EosInterruptTask, and EOS only fires at end-of-scan, so a
//      T1-only session with no scan never reads its results either.
// Pre-#535 builds streamed frozen LATEST values silently in both cases; the
// #535 validity gate turned them into 100% encoder failures (the 2026-06-11
// overnight caught T2 on its first cell, then the T1 variant on the next).
// Computed in Streaming_Start before any trigger is armed (true when any
// public channel is enabled); read by the deferred ISR task's trigger logic.
// volatile: written in SCPI-task context, read in the priority-9 deferred
// task.
static volatile bool gNeedSharedScan = false;

// Task priority constant (benchmark no longer changes priority)
#define STREAMING_ISR_TASK_PRIORITY     8

// SD protobuf metadata: emitted as first message in each SD log file.
// volatile: written by SD card task (via Streaming_ResetSdPbMetadata),
// read by streaming task — compiler must not cache in registers.
static volatile bool gSdPbMetadataSent = false;

// Tracks whether the SD file has become ready during this streaming session.
// Used to reset encoder header flags when SD transitions to ready, since
// encoding may have already fired (and burned headers) before the file opened.
// volatile: written by SD card task, read by streaming task.
static volatile bool gSdFileWasReady = false;

// Per-session streaming statistics.
// Written by: deferred ISR task (queueDroppedSamples, poolExhaustedSamples,
//             queueOverflowSamples, totalSamplesStreamed)
//             streaming task (all other fields)
// Read by:    SCPI handler via Streaming_GetStats() atomic snapshot
static StreamingStats gStreamStats = {0};

// Timer ISR call counter (#265). Lives outside gStreamStats so it can be
// declared volatile — gStreamStats is not volatile because the other fields
// rely on taskENTER_CRITICAL barriers for cross-context visibility, but the
// timer ISR writes this field WITHOUT a critical section (single writer,
// no preemption from same source). volatile prevents the compiler from
// caching the value across statements or eliding writes it thinks are dead.
//
// 64-bit so it never wraps in practice (~6 million years at the ~90 kHz
// hardware ceiling). 64-bit increment on PIC32MZ is two 32-bit ops; the
// low-word add can overflow into the high-word add, which is fine because
// the timer ISR is the only writer (no concurrent RMW from another context).
// 64-bit reads are NOT atomic on PIC32MZ; Streaming_GetStats() snapshots
// this inside taskENTER_CRITICAL which blocks the timer ISR (priority 1).
static volatile uint64_t gTimerISRCalls = 0;

// #557 scan-stale safety net. The scan RATE CAP prevents retriggering the
// MODULE7 scan before it completes; this is the belt-and-suspenders runtime
// detector behind it. The ADC EOS ISR bumps gScanEosSeq each time the shared
// scan completes; the streaming timer ISR records the value it last observed
// (gScanEosSeqSeen) at each new scan trigger. If the seq has NOT advanced since
// the last trigger, the prior scan didn't complete (the #539 scan-busy /
// frozen-data condition) — that tick's data is stale. Surfaced as its OWN stat
// (SYST:STR:STATS? ScanStaleDropped); NOT folded into SampleLossPercent — like
// eosOverruns, the sample was still streamed, just with frozen data (#563). A
// monotonic seq (vs the old boolean) is edge-safe — it can't lose a completion
// if two EOS land between ticks (#563). Reads ~0 with the cap in place; fires
// under NOCAP / cap miscalibration / edge cases, turning otherwise-silent
// frozen data into a visible, accounted staleness metric. Separate
// volatile globals (not in non-volatile gStreamStats) so the timer/EOS ISRs can
// write them safely (single writer each); folded into the snapshot under
// taskENTER_CRITICAL, exactly like gTimerISRCalls.
static volatile uint32_t gScanEosSeq      = 1u;  // bumped per completed scan (EOS ISR); primed 1 ahead of "seen"
static volatile uint32_t gScanEosSeqSeen  = 0u;  // last seq the timer ISR observed
static volatile uint32_t gScanStaleDropped = 0;  // ticks scan armed but no new EOS

// Log-once flags: each error condition logs once per session via
// LOG_E_SESSION / LOG_I_SESSION macros (gSessionOneShot bitmask in Logger).
// All reset in Streaming_ClearStats() via Logger_ResetSessionOneShots().

// --- Windowed data flow health tracking ---
// Forward declarations (defined after Streaming_ClearStats)
static void Streaming_InitFlowWindow(uint64_t frequency);
static void Streaming_UpdateFlowWindow(bool dropped);
// Double-buffer window: tracks sample attempts and drops over a sliding window
// of N sample periods (where N scales with frequency). Loss percentage is
// recomputed at each window rotation and drives QUES condition bits.
//
// QUES condition bit definitions (must match SCPIInterface.c QUES_* defines):
#define QUES_BIT_DATA_LOSS    (1 << 4)   // Bit 4: windowed sample loss >= 5%
#define QUES_BIT_USB_OVERFLOW (1 << 8)   // Bit 8: USB buffer overflow
#define QUES_BIT_WIFI_OVERFLOW (1 << 9)  // Bit 9: WiFi buffer overflow
#define QUES_BIT_SD_OVERFLOW  (1 << 10)  // Bit 10: SD write failure
#define QUES_BIT_ENCODER_FAIL (1 << 11)  // Bit 11: Encoder failure
#define QUES_BIT_TRANSPORT_DOWN (1 << 12) // Bit 12: all configured transports down >grace (auto-stop fired)
#define QUES_BIT_SPI_BUS_FAULT STREAMING_QUES_SPI_BUS_FAULT // Bit 13: shared SPI4 bus jammed (#589 — suspect SD card); set/cleared externally, survives session clears

#define FLOW_WINDOW_MIN   20
#define FLOW_WINDOW_MAX   10000
#define FLOW_LOSS_THRESHOLD_DEFAULT  5

typedef struct {
    uint32_t attempted;
    uint32_t dropped;
} FlowWindow;

// Double-buffer: [0]=previous complete window, [1]=current partial window.
// Loss is computed over both halves for a sliding-window effect.
static FlowWindow gFlowWindow[2] = {{0}};
static uint32_t gFlowWindowCount = 0;      // periods elapsed in current window
static uint32_t gFlowWindowSize = FLOW_WINDOW_MIN; // N, set at streaming start

// User-configurable flow window parameters (set via SCPI, survive across sessions).
// gLossThresholdPct: loss % that triggers QUES_BIT_DATA_LOSS (1-100, default 5).
// gFlowWindowOverride: 0 = auto (clamp(freq*2, 20, 10000)), >0 = explicit size.
static uint32_t gLossThresholdPct = FLOW_LOSS_THRESHOLD_DEFAULT;
static uint32_t gFlowWindowOverride = 0;  // 0 = auto
// Cached SCPI questionable condition bits.  Modified by both the deferred ISR
// task (via Streaming_UpdateFlowWindow) and the streaming task (output overflow
// sites).  Read-modify-write (|=, &=~) uses taskENTER_CRITICAL; plain writes
// (= 0) and reads are atomic on PIC32MZ (32-bit bus) and need no protection.
// Read by SCPI handlers via Streaming_GetQuesBits().  Cleared in Streaming_Stop().
// volatile per #421 — RMW'd by deferred ISR task and streaming task; read by
// SCPI tasks. Critical sections protect the RMW; volatile prevents -O3 from
// caching the value across loop iterations / function calls.
static volatile uint32_t gQuesBits = 0;

// #397 Self-heal transport tracking.  Per-transport tick of "first observed
// unhealthy"; 0 = healthy.  Once (now - downSince) exceeds the grace window,
// the transport is considered dead.  If every transport in ActiveInterface
// is dead at the same time, streaming auto-stops with QUES_BIT_TRANSPORT_DOWN.
// Transient blips (router hiccup, brief USB unenum) ride out and recover.
// Per-transport overflow QUES bits (USB/WiFi/SD) still fire as before for
// pre-grace drops.  Only written from streaming_Task; volatile so external
// debug reads via JTAG see the live value.
static volatile TickType_t gTransportDownSinceUsb = 0;
static volatile TickType_t gTransportDownSinceWifi = 0;
static volatile TickType_t gTransportDownSinceSd = 0;
#define TRANSPORT_GRACE_DEFAULT_SEC 60
#define TRANSPORT_GRACE_MIN_SEC     5
#define TRANSPORT_GRACE_MAX_SEC     300
// volatile: written by SCPI callback context (USBDeviceTask pri 7 or
// app_WifiTask pri 2) and read by streaming_Task (pri 6) every iteration.
// 32-bit write/read is atomic on PIC32MZ; volatile prevents -O3 caching
// the value across the streaming task's tight loop.
static volatile uint32_t gTransportGraceSec = TRANSPORT_GRACE_DEFAULT_SEC;

// #450 — startup-drop grace window.  After each Streaming_Start, drops
// counted before the grace window expires increment ONLY the existing
// *DroppedBytes totals.  Drops counted after the grace expires also
// increment a parallel *DroppedBytesSteady counter, letting callers
// distinguish startup-transient loss (WINC/encoder/buffer ramp) from
// real-data-loss events.  Default 3 s — empirically covers the AP-mode
// association + encoder warmup window observed on the bench.
//
// 32-bit volatile reads/writes are atomic on PIC32MZ; gStreamStartTick
// is written only by streaming_Task at Start and read by the same task
// at every drop site.  gLossGraceSec can be written by SCPI callbacks
// from USB (pri 7) or WiFi (pri 2) tasks but only when streaming is
// not active (SCPI handler rejects mid-stream changes per
// SCPI_SetTransportGraceSec convention).
static volatile TickType_t gStreamStartTick = 0;
#define LOSS_GRACE_DEFAULT_SEC 3
#define LOSS_GRACE_MIN_SEC     0
#define LOSS_GRACE_MAX_SEC     60
static volatile uint32_t gLossGraceSec = LOSS_GRACE_DEFAULT_SEC;

// Returns true once the per-session startup grace has expired.  Cheap
// helper inlined at every drop site; pdMS_TO_TICKS is compile-time
// constant only when arg is constant, hence the multiplication form
// using configTICK_RATE_HZ to avoid runtime division.
static inline bool Streaming_PastStartupGrace(void) {
    return (TickType_t)(xTaskGetTickCount() - gStreamStartTick)
        >= (TickType_t)(gLossGraceSec * configTICK_RATE_HZ);
}

// SD protobuf metadata field tags for standalone metadata message
static const NanopbFlagsArray fields_sd_metadata = {
    .Size = 6,
    .Data = {
        DaqifiOutMessage_timestamp_freq_tag,
        DaqifiOutMessage_analog_in_port_num_tag,
        DaqifiOutMessage_digital_port_num_tag,
        DaqifiOutMessage_device_sn_tag,
        DaqifiOutMessage_device_pn_tag,
        DaqifiOutMessage_device_fw_rev_tag,
    }
};

#define UNUSED(x) (void)(x)
#ifndef min
#define min(x,y) ((x) <= (y) ? (x) : (y))
#endif // min

#ifndef max
#define max(x,y) ((x) >= (y) ? (x) : (y))
#endif // max

// --- Channel mapping for compact sample pool (#177) ---
// Built at stream start, maps packed array indices to board config channels.
// Used by deferred ISR task (sample population) and all encoders.
//
// Concurrency: Streaming_BuildChannelMapping() is always called before
// pRunTimeStreamConf->IsEnabled is set to true and before the timer ISR starts
// (see SCPI_StartStreaming: BuildChannelMapping → ... → IsEnabled=true →
// Streaming_UpdateState). The deferred ISR task guards all gChannelMapping
// reads behind the IsEnabled check, so the mapping is fully written before
// any reader sees it. On PIC32MZ single-core, no memory barriers are needed.
static AInChannelMapping gChannelMapping = {0};

// Encoder buffer — allocated from StreamingBufferPool, runtime-adjustable.
// ENCODER_BUFFER_DEFAULT (8192) and ENCODER_BUFFER_MIN (1024) defined in
// StreamingBufferPool.h. Benchmark: 8KB optimal for USB, 16KB helps SD.
//
// Written by SCPI/USB task during StartStreamData while the streaming
// task is suspended on ulTaskNotifyTake (Running == false). Read by the
// streaming task body each iteration of the while(1) loop. Non-volatile:
// see docs/SET_ONCE_POINTER_AUDIT.md — multi-trial bench A/B vs main
// (8 trials × ~400k samples each on this branch and on main, 2026-05-10)
// showed identical encoder-failure rate (2/3.2M on both branches), so the
// qualifier provides no observable behavioral benefit. Stripping saves
// 8 instructions in streaming_Task at -O3.
static uint8_t* buffer = NULL;
static uint32_t bufferSize = 0;

//! Pointer to the board configuration data structure to be set in 
//! initialization
//static tStreamingConfig *pConfigStream;
//! Pointer to the board runtime configuration data structure, to be
//! set in initialization
static StreamingRuntimeConfig *gpRuntimeConfigStream;
//! Pointer to the board configuration data structure, to be
//! set in initialization
static tStreamingConfig *gpStreamingConfig;
//! Indicate if handler is used 
// volatile per #421 — re-entry guard; written and read in timer ISR.
// Without volatile, -O3 may elide the read or hoist it out of the function.
static volatile bool gInTimerHandler = false;
static TaskHandle_t gStreamingInterruptHandle;
static TaskHandle_t gStreamingTaskHandle;

/* #486: cross-task quiescence flags.  Set inside each streaming task's
 * "critical region" — the span where it dereferences resources that
 * SCPI_StartStreaming re-partitions (encoder buffer, sample queue,
 * sample pool, output circular buffers, coherent DMA buffers).
 * SCPI_StartStreaming polls Streaming_TasksAreQuiescent() after
 * Streaming_Stop and before the re-partition, with a bounded wait
 * + vTaskDelay(1) so the lower-priority streaming_Task (pri 6) and
 * higher-priority deferred ISR task (pri 9) can each finish their
 * iteration and clear the flag.
 *
 * uint32_t (not bool) — CLAUDE.md "Atomicity & Concurrency Rules":
 * 32-bit reads/writes are atomic on the PIC32MZ bus.  volatile keeps
 * the compiler from caching the value across loop iterations.
 * No RMW: writes are unconditional 0 or 1, no |= or &=. */
static volatile uint32_t gStreamingTaskInCritical = 0;
static volatile uint32_t gDeferredTaskInCritical = 0;

// Sine wave period for test pattern 6: 256 samples per cycle.
// Integer-only implementation via Q0.16 lookup table —
// eliminates FPU usage in the deferred task so context switches
// don't pay FPU save/restore cost. Table: (sin(i*2π/256)+1)*0.5 scaled
// to [0,65535]. Runtime scaling: (lut[phase] * adcMax) >> 16 is one
// 32-bit multiply + shift, no FPU.
#define SINE_PERIOD 256
static const uint16_t kSineLutQ16[SINE_PERIOD] = {
    32768, 33572, 34375, 35178, 35979, 36779, 37575, 38369,
    39160, 39947, 40729, 41507, 42279, 43046, 43807, 44560,
    45307, 46046, 46777, 47500, 48214, 48919, 49613, 50298,
    50972, 51635, 52287, 52927, 53555, 54170, 54773, 55362,
    55938, 56499, 57047, 57579, 58097, 58600, 59087, 59558,
    60013, 60451, 60873, 61278, 61666, 62036, 62389, 62724,
    63041, 63339, 63620, 63881, 64124, 64348, 64553, 64739,
    64905, 65053, 65180, 65289, 65377, 65446, 65496, 65525,
    65535, 65525, 65496, 65446, 65377, 65289, 65180, 65053,
    64905, 64739, 64553, 64348, 64124, 63881, 63620, 63339,
    63041, 62724, 62389, 62036, 61666, 61278, 60873, 60451,
    60013, 59558, 59087, 58600, 58097, 57579, 57047, 56499,
    55938, 55362, 54773, 54170, 53555, 52927, 52287, 51635,
    50972, 50298, 49613, 48919, 48214, 47500, 46777, 46046,
    45307, 44560, 43807, 43046, 42279, 41507, 40729, 39947,
    39160, 38369, 37575, 36779, 35979, 35178, 34375, 33572,
    32768, 31963, 31160, 30357, 29556, 28756, 27960, 27166,
    26375, 25588, 24806, 24028, 23256, 22489, 21728, 20975,
    20228, 19489, 18758, 18035, 17321, 16616, 15922, 15237,
    14563, 13900, 13248, 12608, 11980, 11365, 10762, 10173,
     9597,  9036,  8488,  7956,  7438,  6935,  6448,  5977,
     5522,  5084,  4662,  4257,  3869,  3499,  3146,  2811,
     2494,  2196,  1915,  1654,  1411,  1187,   982,   796,
      630,   482,   355,   246,   158,    89,    39,    10,
        0,    10,    39,    89,   158,   246,   355,   482,
      630,   796,   982,  1187,  1411,  1654,  1915,  2196,
     2494,  2811,  3146,  3499,  3869,  4257,  4662,  5084,
     5522,  5977,  6448,  6935,  7438,  7956,  8488,  9036,
     9597, 10173, 10762, 11365, 11980, 12608, 13248, 13900,
    14563, 15237, 15922, 16616, 17321, 18035, 18758, 19489,
    20228, 20975, 21728, 22489, 23256, 24028, 24806, 25588,
    26375, 27166, 27960, 28756, 29556, 30357, 31160, 31963,
};

_Static_assert((sizeof(kSineLutQ16) / sizeof(kSineLutQ16[0])) == SINE_PERIOD,
               "kSineLutQ16 must have exactly SINE_PERIOD entries");

/* #549: the pool's active-USB overcommit floor must equal one USB CDC DMA
 * write, so a degraded partition never hands back an active USB ring below
 * the setter floor / CONF:CAP-advertised usb.min. StreamingBufferPool.c can't
 * include the heavyweight UsbCdc.h, so the value is mirrored there and tied
 * here (this TU sees both). */
_Static_assert(STREAMING_USB_ACTIVE_MIN == USBCDC_WBUFFER_SIZE,
               "#549: STREAMING_USB_ACTIVE_MIN must track USBCDC_WBUFFER_SIZE");

/**
 * Generate a synthetic ADC value for test pattern streaming.
 * @param pattern    Pattern type (1-6, see switch cases)
 * @param channel    Channel ID (0-based)
 * @param sampleCount Monotonic sample counter (reset each session)
 * @param adcMax     Maximum ADC raw code (e.g. 4095 for 12-bit, 262143 for 18-bit)
 * @return Synthetic ADC value in [0, adcMax]
 */
static uint32_t Streaming_GenerateTestValue(uint32_t pattern, uint8_t channel,
                                             uint64_t sampleCount, uint32_t adcMax) {
    uint32_t range = adcMax + 1;  // Values from 0 to adcMax inclusive
    switch (pattern) {
        case 1:  // Counter: predictable sequence for integrity verification
            return (uint32_t)((sampleCount + channel) % range);
        case 2:  // Midscale: constant value for consistent encoding size
            return adcMax / 2;
        case 3:  // Fullscale: maximum value for worst-case ProtoBuf size
            return adcMax;
        case 4:  // Walking: channel-dependent ramp for visual verification
            return (uint32_t)(((sampleCount * (channel + 1))) % range);
        case 5: {  // Triangle: ramps up then down, period = 2*adcMax samples
            // Phase offset per channel so multi-channel view is staggered
            uint32_t period = 2 * range;
            uint32_t pos = (uint32_t)((sampleCount + (uint32_t)channel * (range / 4)) % period);
            return (pos < range) ? pos : (period - 1 - pos);
        }
        case 6: {  // Sine: 256-sample period, integer Q0.16 LUT scaling
            uint32_t phase = (uint32_t)((sampleCount + (uint32_t)channel * 32) % SINE_PERIOD);
            /* Scale lut[phase] ∈ [0,65535] to [0,adcMax] with rounding.
             * Multiply by (adcMax+1) and >>16 maps the full Q0.16 range
             * (where 65535 represents ~1.0) to [0,adcMax] exactly; add
             * 0x8000 for round-to-nearest; clamp against the +1 overshoot. */
            uint64_t scaled =
                ((uint64_t)kSineLutQ16[phase] * (uint64_t)(adcMax + 1U) + 0x8000ULL) >> 16;
            if (scaled > adcMax) scaled = adcMax;
            return (uint32_t)scaled;
        }
        default:
            return 0;
    }
}

// --- Channel mapping API ---

uint8_t Streaming_BuildChannelMapping(const tBoardConfig* pBoardConfig,
                                       const AInRuntimeArray* pRuntimeChannels) {
    memset(&gChannelMapping, 0, sizeof(gChannelMapping));

    size_t count = pBoardConfig->AInChannels.Size < pRuntimeChannels->Size
                 ? pBoardConfig->AInChannels.Size : pRuntimeChannels->Size;

    uint8_t packed = 0;
    for (size_t i = 0; i < count && packed < MAX_AIN_PUBLIC_CHANNELS; i++) {
        if (pRuntimeChannels->Data[i].IsEnabled &&
            AInChannel_IsPublic(&pBoardConfig->AInChannels.Data[i])) {
            const AInChannel* ch = &pBoardConfig->AInChannels.Data[i];
            gChannelMapping.channelIds[packed] = ch->DaqifiAdcChannelId;
            gChannelMapping.configIndices[packed] = (uint8_t)i;
            // #541 D-A: T1 (dedicated-module) channels are read directly
            // from their ADC result registers in the deferred task, gated
            // on per-input ARDY.  Record the hardware channel number here
            // so the hot loop doesn't have to chase board-config pointers.
            if (ch->Type == AIn_MC12bADC && ch->Config.MC12b.ChannelType == 1) {
                gChannelMapping.t1DirectMask |= (uint16_t)(1U << packed);
                gChannelMapping.hwChannelIds[packed] =
                        (uint8_t)ch->Config.MC12b.ChannelId;
            }
            packed++;
        }
    }
    gChannelMapping.count = packed;
    return packed;
}

const AInChannelMapping* Streaming_GetChannelMapping(void) {
    return &gChannelMapping;
}

void Streaming_CountActiveChannels(uint16_t* out_type1Count,
                                   uint16_t* out_totalPublic,
                                   bool* out_hasAD7609) {
    uint16_t type1 = 0;
    uint16_t total = 0;
    bool has7609 = false;

    /* Per CLAUDE.md: BoardRunTimeConfig_Get / BoardConfig_Get index into
     * static arrays populated at boot and never return NULL. No guard. */
    volatile AInRuntimeArray* rt =
        BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_AIN_CHANNELS);
    volatile AInArray* cfg =
        BoardConfig_Get(BOARDCONFIG_AIN_CHANNELS, 0);

    size_t count = (cfg->Size < rt->Size) ? cfg->Size : rt->Size;
    for (size_t i = 0; i < count; i++) {
        if (rt->Data[i].IsEnabled != 1) continue;

        if (cfg->Data[i].Type == AIn_AD7609) {
            if (cfg->Data[i].Config.AD7609.IsPublic) {
                has7609 = true;
                total++;
            }
        } else if (cfg->Data[i].Type == AIn_MC12bADC) {
            if (cfg->Data[i].Config.MC12b.IsPublic) {
                total++;
                if (cfg->Data[i].Config.MC12b.ChannelType == 1) {
                    type1++;
                }
            }
        }
    }

    if (out_type1Count  != NULL) *out_type1Count  = type1;
    if (out_totalPublic != NULL) *out_totalPublic = total;
    if (out_hasAD7609   != NULL) *out_hasAD7609   = has7609;
}

uint32_t Streaming_ComputeMaxFreqForConfigIface(StreamingInterface iface) {
    uint16_t type1 = 0, total = 0;
    Streaming_CountActiveChannels(&type1, &total, NULL);

    /* BoardRunTimeConfig_Get / BoardConfig_Get never return NULL (CLAUDE.md). */
    StreamingRuntimeConfig* sc =
        BoardRunTimeConfig_Get(BOARDRUNTIME_STREAMING_CONFIGURATION);
    tBoardConfig* bc = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);

    /* Session scan list: enabled user-T2 (+ monitoring when OBDiag on). */
    uint32_t scanCount = MC12b_ComputeScanList(true, sc->OnboardDiagEnabled, NULL, NULL);
    uint32_t userT2    = MC12b_ComputeScanList(true, false, NULL, NULL);

    uint32_t maxFreq;
    if (bc != NULL && bc->BoardVariant == 1u && total > 0u) {
        /* NQ1 (#557): freeze-aware additive ADC/scan cap replaces the
         * tick-budget / type1Agg / MC12b_ScanMaxFreq terms below. The prior
         * sweeps were drop-blind and counted frozen scanned data as clean, so
         * those terms were both over-conservative (T1) AND wrong (inflated T2).
         * monitoring channels in the scan = scanCount - userT2 (8 when OBDiag). */
        uint32_t nMon = (scanCount > userT2) ? (scanCount - userT2) : 0u;
        maxFreq = Streaming_AdcAdditiveCap_NQ1(
                type1, userT2, nMon, (sc->Encoding == Streaming_ProtoBuffer) ? 1u : 0u);
        /* #563: the additive model was fit at the default SAMC. It replaces the
         * EOS-rate/event-rate caps, but the SAMC/divider-dependent scan-busy
         * limit (#539) must still apply so a non-default SAMC can't push the cap
         * above the real scan-retrigger rate. min() with the hardware term only
         * (not full ScanMaxFreq, whose EOS/event caps the additive model supersedes). */
        if (scanCount > 0) {
            uint32_t hwScanMax = MC12b_HardwareScanMaxFreq(scanCount);
            if (hwScanMax < maxFreq) maxFreq = hwScanMax;
        }
        /* #574: SD-PB writer-vs-scan cap. The SD writer task loses CPU to the
         * scan's data-ready/EOS ISRs, so scan-armed configs sustain a lower
         * zero-loss SD rate than the ADC/transport terms predict. SD interface
         * only — UsbAndSd is uncharacterized (separate follow-up). */
        if (iface == StreamingInterface_SD) {
            uint32_t sdMax = Streaming_SdAdditiveCap_NQ1(
                    type1, userT2, nMon,
                    (sc->Encoding == Streaming_ProtoBuffer) ? 1u : 0u);
            if (sdMax < maxFreq) maxFreq = sdMax;
        }
    } else {
        /* NQ2/NQ3 (AD7609 — no MODULE7 scan) and the 0-channel case: legacy
         * conservative formula (#541). Mirror ComputeMaxFreq's ISR_MAX-for-0
         * behavior so disabling the last channel doesn't cap to 0. */
        maxFreq = Streaming_ComputeMaxFreq(type1, total);
        /* #541 D-C shared-scan rate bound (documented-undefined retrigger,
         * FRM §22.3.2 / #539). N_active==0 arms no scan -> no bound. */
        if (scanCount > 0) {
            uint32_t scanMax = MC12b_ScanMaxFreq(scanCount, userT2);
            if (scanMax < maxFreq) maxFreq = scanMax;
        }
    }

    /* Per-interface, per-format TRANSPORT cap (#524) applies to ALL variants;
     * binds CSV (byte-bound) below the ADC cap. Interface is a PARAMETER so the
     * capabilities query can compute for the detected interface w/o mutating
     * shared state (#524 Qodo). */
    uint32_t transportMax = Streaming_TransportMaxFreq(iface, sc->Encoding, total);
    if (transportMax < maxFreq) maxFreq = transportMax;
    return maxFreq;
}

uint32_t Streaming_ComputeMaxFreqForConfig(void) {
    StreamingRuntimeConfig* sc =
        BoardRunTimeConfig_Get(BOARDRUNTIME_STREAMING_CONFIGURATION);
    return Streaming_ComputeMaxFreqForConfigIface(sc->ActiveInterface);
}

/**
 * @brief Deferred interrupt handler for sample collection.
 *
 * This task runs at high priority and is notified by the timer ISR.
 * It collects the latest ADC and DIO samples into an object pool entry,
 * then pushes the entry to the sample queue for the streaming task.
 *
 * Critical path optimizations:
 * - Uses object pool allocation (O(1)) instead of heap
 * - Uses critical section for atomic sample copy (prevents torn reads)
 * - Triggers next ADC conversion immediately after sample collection
 */
void _Streaming_Deferred_Interrupt_Task(void) {
    /* No portTASK_USES_FLOATING_POINT() — this task is pure integer.
     * sin() in test pattern 6 was replaced with a Q0.16 LUT. Keeping
     * FPU registration off saves 32x 64-bit register save+restore on
     * every context switch with another FPU-registered task (notably
     * streaming_Task when CSV/JSON with VoltagePrecision>0 is active).
     * Measured in Session 12 — P3 Tstd drops from 14 us back to ~6 us. */

    TickType_t xBlockTime = portMAX_DELAY;
    uint8_t i = 0;
    tBoardData * pBoardData = BoardData_Get(
            BOARDDATA_ALL_DATA,
            0);
    tBoardConfig * pBoardConfig = BoardConfig_Get(
            BOARDCONFIG_ALL_CONFIG,
            0);

    StreamingRuntimeConfig * pRunTimeStreamConf = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);

    AInModRuntimeArray * pRunTimeAInModules = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_AIN_MODULES);

    AInPublicSampleList_t *pPublicSampleList=NULL;
    AInSample *pAiSample;

    uint64_t ChannelScanFreqDivCount = 0;

    while (1) {
        ulTaskNotifyTake(pdFALSE, xBlockTime);
        DioProbe_Toggle(2);  /* probe 2: deferred task wake rate */

        if (pRunTimeStreamConf->IsEnabled) {
            /* #486 — quiescence flag for cross-task sync against
             * SCPI_StartStreaming re-partition.  Set BEFORE any deref
             * of the sample pool / queue resources that re-partition
             * tears down; the re-check below closes the TOCTOU between
             * the IsEnabled gate above and the flag set.  Memory
             * barrier prevents -O3 from hoisting subsequent non-volatile
             * reads above the volatile flag store.
             *
             * Single-exit goto-cleanup pattern (Qodo pass-3 /improve):
             * all pool-touching exit paths land at `pool_done` which
             * unconditionally clears the flag.  The ADC trigger code
             * below the label runs OUTSIDE the critical region — it
             * only touches board-config statics and ADC peripheral
             * SFRs, none of which re-partition tears down. */
            gDeferredTaskInCritical = 1;
            __asm__ __volatile__ ("" ::: "memory");
            if (!pRunTimeStreamConf->IsEnabled) {
                goto pool_done;
            }
            DioProbe_PulseStart(3);  /* probe 3: alloc + channel loop + queue push */
            // Use object pool instead of heap allocation (eliminates vPortFree overhead)
            // No heap check needed - pool uses pre-allocated static memory
            pPublicSampleList = AInSampleList_AllocateFromPool();
            if(pPublicSampleList==NULL) {
                // #499: split counter — this path = pool depth too shallow for
                // the configured rate. Distinct from the PushBack-fail path
                // below, which is "queue full / streaming_Task too slow."
                // #483: Steady = post-startup-grace subset (the aggregate
                // queueDroppedSamples stays for back-compat; per-sub-counter
                // Steady variants intentionally not added — the existing
                // aggregate Steady is sufficient for the session-end gating).
                bool pastGrace = Streaming_PastStartupGrace();
                taskENTER_CRITICAL();
                gStreamStats.queueDroppedSamples++;
                gStreamStats.poolExhaustedSamples++;
                if (pastGrace) {
                    gStreamStats.queueDroppedSamplesSteady++;
                }
                taskEXIT_CRITICAL();
                LOG_E_SESSION(LOG_SESSION_POOL_EXHAUST, "Streaming: Sample pool exhausted");
                Streaming_UpdateFlowWindow(true);
                // Still increment test pattern counter to stay in sync
                if (gTestPattern != 0) {
                    taskENTER_CRITICAL();
                    gTestPatternSampleCount++;
                    taskEXIT_CRITICAL();
                }
                DioProbe_PulseEnd(3);
                /* Pool exhausted: skip ADC trigger AND xTaskNotifyGive
                 * by continuing to next loop iteration.  Goto-cleanup
                 * is NOT used here because the pool_done label lands
                 * before the ADC trigger code which the pool-exhausted
                 * path MUST skip (preserves pre-PR behavior; the
                 * original `continue` skipped both ADC trigger and
                 * encoder-task notification).  Exit barrier
                 * (pass-4 Qodo importance-9) pairs with the entry
                 * barrier — see pool_done label below. */
                __asm__ __volatile__ ("" ::: "memory");
                gDeferredTaskInCritical = 0;
                continue;
            }

            // Fill packed sample using channel mapping (#177).
            // Mapping was built at stream start; packed index j maps to
            // board config index via gChannelMapping.configIndices[j].
            const AInChannelMapping* mapping = &gChannelMapping;
            pPublicSampleList->channelCount = mapping->count;
            pPublicSampleList->validMask = 0;
            pPublicSampleList->Timestamp = 0;

            // Streaming trigger timestamp for this tick, captured by
            // Streaming_TimerHandler at the single point all sample stamps
            // derive from (clamps 0 -> 1, so 0 can never alias the #533
            // invalid sentinel).  Hoisted out of the channel loop: the #541
            // T1 direct-read path stamps every T1 sample with it, and the
            // post-loop fallback uses it too.
            uint32_t trigStamp = 0;
            {
                uint32_t* pTrigStamp =
                        BoardData_Get(BOARDDATA_STREAMING_TIMESTAMP, 0);
                if (pTrigStamp != NULL) trigStamp = *pTrigStamp;
            }

            for (uint8_t j = 0; j < mapping->count; j++) {
                uint8_t cfgIdx = mapping->configIndices[j];

                uint32_t adcMax;
                // #368: this task is registered as pure-integer (see comment
                // at top of _Streaming_Deferred_Interrupt_Task — no
                // portTASK_USES_FLOATING_POINT()).  The historical bug:
                // Resolution used to be `double`, so the cast in
                // `adcMax = (uint32_t)Resolution - 1;` emitted FPU
                // instructions that picked up register-contamination
                // from FPU-using tasks during context switches, producing
                // garbage like 0x80000FE6 instead of 4095.
                //
                // PR #369 fixed it with hardcoded integer constants here,
                // and a follow-up (see #465) hardened the type system by
                // changing MC12bModuleConfig.Resolution / AD7609Module-
                // Config.Resolution to `uint32_t` in AInConfig.h.  Either
                // defense alone is enough; keeping both means a future
                // contributor can't accidentally re-introduce the bug
                // by reverting one of the two.
                if (pBoardConfig->AInChannels.Data[cfgIdx].Type == AIn_AD7609) {
                    adcMax = 262143u;  // 18-bit AD7609
                } else {
                    adcMax = 4095u;    // 12-bit MC12bADC
                }

                if (gBenchmarkMode == BENCHMARK_PIPELINE) {
                    // Pipeline: skip ADC entirely, generate synthetic data directly.
                    // Timestamp comes from the streaming timer tick captured by
                    // Streaming_TimerHandler (BOARDDATA_STREAMING_TIMESTAMP) — same
                    // source the ADC ISR uses for AInSample.Timestamp in normal
                    // operation, so PB/CSV/JSON output is consistent across modes.
                    pPublicSampleList->Values[j] =
                        Streaming_GenerateTestValue(gTestPattern,
                            mapping->channelIds[j],
                            gTestPatternSampleCount, adcMax);
                    pPublicSampleList->validMask |= (1U << j);
                } else if (gTestPattern != 0) {
                    // Test pattern: always produce deterministic data regardless
                    // of ADC state. Read ADC for timestamp only.
                    pAiSample = BoardData_Get(BOARDDATA_AIN_LATEST, cfgIdx);
                    if (pAiSample != NULL && pPublicSampleList->Timestamp == 0) {
                        pPublicSampleList->Timestamp = pAiSample->Timestamp;
                    }
                    pPublicSampleList->Values[j] =
                        Streaming_GenerateTestValue(gTestPattern,
                            mapping->channelIds[j],
                            gTestPatternSampleCount, adcMax);
                    pPublicSampleList->validMask |= (1U << j);
                } else if (mapping->t1DirectMask & (1U << j)) {
                    // #541 D-A: T1 (dedicated-module) result read directly
                    // from the ADC result register, gated on the per-input
                    // ARDY flag (sets at conversion end, clears on ADCDATAx
                    // read — FRM DS60001344E Fig 22-7 / p.22-88).  Replaces
                    // the EOS-task -> LATEST-cache hop, whose freshness
                    // collapsed above ~4.6 kHz because EOSRDY only sets on
                    // full-scan completion and the scan was being
                    // retriggered out-of-spec (#539).  Timing: the T1
                    // conversion completes ~1.3 us after the TMR5 trigger;
                    // this task wakes several us later (ISR->task latency +
                    // pool work above), so ARDY is essentially always set.
                    // Miss policy: NO spin — leave the validMask bit 0 for
                    // this tick (#535 semantics) and count it.
                    uint32_t val;
                    if (MC12b_ReadResult(
                            (ADCHS_CHANNEL_NUM)mapping->hwChannelIds[j],
                            &val)) {
                        pPublicSampleList->Values[j] = val;
                        pPublicSampleList->validMask |= (1U << j);
                        if (pPublicSampleList->Timestamp == 0) {
                            pPublicSampleList->Timestamp = trigStamp;
                        }
                        // Refresh the LATEST cache so non-streaming readers
                        // (MEAS:VOLT:DC?) stay live during a session.  Same
                        // per-tick work the EOS task used to do at the same
                        // priority — relocated, not added.
                        AInSample wb;
                        wb.Timestamp = trigStamp;
                        wb.Channel = mapping->channelIds[j];
                        wb.Value = val;
                        BoardData_Set(BOARDDATA_AIN_LATEST, cfgIdx, &wb);
                    } else {
                        // Single-writer 32-bit increment — no critical
                        // section needed (deferred task is the only writer).
                        gStreamStats.t1ArdyMisses++;
                        LOG_E_SESSION(LOG_SESSION_T1_ARDY_MISS,
                                "Streaming: T1 ARDY miss (result not ready at read)");
                    }
                } else {
                    // Normal: read real ADC data from BoardData
                    pAiSample = BoardData_Get(BOARDDATA_AIN_LATEST, cfgIdx);
                    if (pAiSample != NULL) {
                        taskENTER_CRITICAL();
                        uint32_t ts = pAiSample->Timestamp;
                        uint32_t val = pAiSample->Value;
                        taskEXIT_CRITICAL();

                        // #533: Timestamp==0 marks the LATEST slot invalid —
                        // Streaming_Start zeroes it so the previous session's
                        // final conversion (parked in this one-deep cache
                        // across the stop gap) can't be re-emitted as the
                        // new session's first sample.  Skipping leaves the
                        // validMask bit 0; the slot revalidates when this
                        // session's first conversion lands (next tick).
                        // A genuine counter reading of 0 can never alias the
                        // sentinel: Streaming_TimerHandler clamps 0 -> 1 at
                        // the single capture point all stamps derive from.
                        if (ts != 0) {
                            pPublicSampleList->Values[j] = val;
                            pPublicSampleList->validMask |= (1U << j);
                            if (pPublicSampleList->Timestamp == 0) {
                                pPublicSampleList->Timestamp = ts;
                            }
                        }
                    }
                    // else: bit stays 0 in validMask (invalid)
                }
            }

            // Guarantee a non-zero timestamp on every emitted packet. Pipeline
            // mode never reads ADC samples; normal/test-pattern modes may also
            // end up with Timestamp=0 in edge cases (DIO-only streaming, or
            // all AIN channels happen to be invalid this cycle). Fall back to
            // the streaming timer tick captured by Streaming_TimerHandler —
            // same source the ADC ISR writes to AInSample.Timestamp, so the
            // wire output remains consistent across modes. Required because
            // the PB encoder omits the msg_time_stamp field when timestamp==0.
            if (pPublicSampleList->Timestamp == 0) {
                pPublicSampleList->Timestamp = trigStamp;
            }
            if(!AInSampleList_PushBack(pPublicSampleList)){//failed pushing to Q
                // #499: split counter — this path = FreeRTOS queue full,
                // i.e. streaming_Task can't drain fast enough. Distinct from
                // the AllocateFromPool-NULL path above (pool depth shallow).
                // #483: Steady = post-startup-grace subset of the aggregate.
                bool pastGrace = Streaming_PastStartupGrace();
                taskENTER_CRITICAL();
                gStreamStats.queueDroppedSamples++;
                gStreamStats.queueOverflowSamples++;
                if (pastGrace) {
                    gStreamStats.queueDroppedSamplesSteady++;
                }
                taskEXIT_CRITICAL();
                LOG_E_SESSION(LOG_SESSION_QUEUE_OVERFLOW, "Streaming: Sample queue overflow detected");
                AInSampleList_FreeToPool(pPublicSampleList);  // Use pool!
                Streaming_UpdateFlowWindow(true);
            } else {
                taskENTER_CRITICAL();
                gStreamStats.totalSamplesStreamed++;
                taskEXIT_CRITICAL();
                Streaming_UpdateFlowWindow(false);
            }
            DioProbe_PulseEnd(3);
pool_done:
            /* #486 — exit pool-touching region.  ADC trigger code below
             * does not touch pool / queue / encoder buffer (only board-
             * config statics + ADC peripheral SFRs), so it's safe
             * outside the quiescence window.  The IsEnabled-false
             * re-check `goto`s here to clear the flag without skipping
             * the ADC trigger — at this point streaming is being
             * stopped, but the ADC trigger is idempotent.
             *
             * Exit barrier (pass-4 Qodo importance-9): pairs with the
             * entry barrier at line 476.  Without it, the compiler at
             * -O3 could sink the final non-volatile pool/queue access
             * past the volatile flag store, defeating the protection
             * on the exit side. */
            __asm__ __volatile__ ("" ::: "memory");
            gDeferredTaskInCritical = 0;

            // Pipeline mode skips ADC hardware entirely (no triggers, no waits).
            // Normal/NoCap modes: with hardware triggering (#282), the timer
            // match event directly triggers MC12bADC modules — only non-HW
            // devices (AD7609, DIO) and divided-rate shared scans need software
            // triggers here.
            if (gBenchmarkMode != BENCHMARK_PIPELINE) {
                DioProbe_PulseStart(4);  /* probe 4: ADC trigger duration */
                bool hwDed = MC12b_IsHwTriggerDedicated();
                bool hwShd = MC12b_IsHwTriggerShared();
                // #537: the shared (MODULE7) scan carries BOTH the OBDiag
                // monitoring channels AND every Type-2 USER channel, so it
                // must run whenever either consumer needs it.  Gating it on
                // OnboardDiagEnabled alone froze T2 user data in OBDiag=0
                // streams (silently on pre-#535 builds; 100% encoder
                // failures once the #535 validity gate landed).  The EOS
                // task already skips monitoring-channel READS when OBDiag=0,
                // so monitoring stays dormant as intended.
                bool skipShared = !pRunTimeStreamConf->OnboardDiagEnabled &&
                                  !gNeedSharedScan;

                if (pRunTimeStreamConf->ChannelScanFreqDiv == 1) {
                    for (i = 0; i < pRunTimeAInModules->Size; ++i) {
                        if (pBoardConfig->AInModules.Data[i].Type != AIn_MC12bADC) {
                            ADC_TriggerConversion(&pBoardConfig->AInModules.Data[i], MC12B_ADC_TYPE_ALL);
                            continue;
                        }
                        // MC12bADC: determine which trigger types need software
                        bool needSwDed = !hwDed;
                        bool needSwShd = !hwShd && !skipShared;
                        if (needSwDed && needSwShd) {
                            ADC_TriggerConversion(&pBoardConfig->AInModules.Data[i], MC12B_ADC_TYPE_ALL);
                        } else if (needSwDed) {
                            ADC_TriggerConversion(&pBoardConfig->AInModules.Data[i], MC12B_ADC_TYPE_DEDICATED);
                        } else if (needSwShd) {
                            ADC_TriggerConversion(&pBoardConfig->AInModules.Data[i], MC12B_ADC_TYPE_SHARED);
                        }
                        // else: both hw-triggered or shared skipped — no software trigger
                    }
                } else if (pRunTimeStreamConf->ChannelScanFreqDiv != 0) {
                    // Dedicated at full rate, shared at divided rate.
                    for (i = 0; i < pRunTimeAInModules->Size; ++i) {
                        bool isMC12b = (pBoardConfig->AInModules.Data[i].Type == AIn_MC12bADC);
                        if (!(isMC12b && hwDed)) {
                            ADC_TriggerConversion(&pBoardConfig->AInModules.Data[i], MC12B_ADC_TYPE_DEDICATED);
                        }
                    }

                    // Shared at divided rate — skip entirely when hw-triggered
                    // or when onboard diagnostics are disabled.
                    if (!hwShd && !skipShared) {
                        if (ChannelScanFreqDivCount >= pRunTimeStreamConf->ChannelScanFreqDiv) {
                            for (i = 0; i < pRunTimeAInModules->Size; ++i) {
                                ADC_TriggerConversion(&pBoardConfig->AInModules.Data[i], MC12B_ADC_TYPE_SHARED);
                            }
                            ChannelScanFreqDivCount = 0;
                        }
                        ChannelScanFreqDivCount++;
                    }
                }
                DIO_StreamingTrigger(&pBoardData->DIOLatest, &pBoardData->DIOSamples);
                DioProbe_PulseEnd(4);
            }

            // Increment test pattern counter once per ISR tick (after all channels)
            if (gTestPattern != 0) {
                taskENTER_CRITICAL();
                gTestPatternSampleCount++;
                taskEXIT_CRITICAL();
            }
        }

        xTaskNotifyGive(gStreamingTaskHandle);

    }
}

void Streaming_Defer_Interrupt(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(gStreamingInterruptHandle, &xHigherPriorityTaskWoken);
    portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
}

static void TSTimerCB(uintptr_t context, uint32_t alarmCount) {

}

/*!
 * Function to manage timer handler
 * @param[in] context    unused
 * @param[in] alarmCount unused
 */
static void Streaming_TimerHandler(uintptr_t context, uint32_t alarmCount) {
    DioProbe_Toggle(0);  /* probe 0: true timer ISR rate */
    uint32_t valueTMR = TimerApi_CounterGet(gpStreamingConfig->TSTimerIndex);
    // #533: Timestamp==0 is reserved as the "LATEST slot invalid" sentinel
    // (Streaming_Start zeroes the per-channel snapshots so a prior session's
    // final conversion can't be re-emitted).  Clamp a genuine counter
    // reading of 0 to 1 so a phase-aligned 2^32 wrap can never alias the
    // sentinel and silently drop a real tick — a one-timer-tick bias
    // (nanoseconds) once per wrap, vs a lost sample.
    if (valueTMR == 0u) valueTMR = 1u;
    BoardData_Set(BOARDDATA_STREAMING_TIMESTAMP, 0, (const void*) &valueTMR);

    // Defensive re-entry guard. PIC32MZ same-source ISRs cannot preempt
    // themselves so this should never trigger, but the existing flag is
    // kept for paranoia. Counter increment is BELOW the guard so the
    // invariant `gTimerISRCalls == samples + queue_drops` holds even if
    // the guard ever fires (preventing this ISR from dispatching work).
    if (gInTimerHandler) return;
    gInTimerHandler = true;

    // ISR-safety contract for gTimerISRCalls (#265):
    //
    // - Storage: declared `static volatile uint64_t` so the compiler cannot
    //   cache the value across statements or elide writes it thinks are dead.
    // - Single writer: ONLY this timer ISR increments gTimerISRCalls. No
    //   other ISR, no task, no DMA callback touches it. Verified via grep.
    // - Increment: 64-bit RMW expands to multiple 32-bit ops on PIC32MZ. It
    //   is safe here because there are no other writers and this ISR cannot
    //   be preempted by itself (same source).
    // - Read side: 64-bit reads are NOT atomic on PIC32MZ. Streaming_GetStats
    //   snapshots gTimerISRCalls inside its existing taskENTER_CRITICAL,
    //   which blocks the timer ISR (priority 1) for a coherent read.
    // - Overflow: 64-bit so it never wraps in practice (~6M years at 90 kHz).
    //
    // IsEnabled gate: Streaming_UpdateState() always cycles Stop→Start, and
    // Streaming_Start() unconditionally re-arms the timer even when
    // IsEnabled is false (existing reset-on-reconfig behavior). The deferred
    // task ignores those notifications, but without this gate the counter
    // would still increment on phantom ISR firings between sessions.
    //
    // Increment AND defer happen INSIDE the IsEnabled check so the invariant
    // TimerISRCalls == TotalSamples + QueueDropped holds exactly: every
    // counted ISR call corresponds to exactly one Defer_Interrupt dispatch,
    // which becomes either a queued sample or a pool-exhaust drop. Gating
    // Defer_Interrupt on IsEnabled also avoids spurious deferred-task
    // wakeups during the Stop→Start reconfig window when the timer is
    // re-armed but streaming is disabled.
    if (gpRuntimeConfigStream != NULL && gpRuntimeConfigStream->IsEnabled) {
        gTimerISRCalls++;
        // #557 scan-stale detector: this tick is a new shared-scan trigger
        // (STRGSRC=TMR5). If a scan is armed but its EOS hasn't fired since the
        // last trigger, the prior scan didn't complete (scan-busy) — its data
        // is stale, so count the tick as a dropped sample. Then re-arm for the
        // scan this tick triggers. 32-bit atomic loads/stores across the timer
        // (pri 1) / EOS ISRs; single writer per global (#563 edge-safe seq).
        //
        // #563: only valid when the HW trigger fires the scan on EVERY tick
        // (STRGSRC=TMR5). In the software divided-trigger path
        // (ChannelScanFreqDiv > 1) the scan isn't retriggered each tick, so a
        // missing EOS between triggers is expected — gating on
        // MC12b_IsHwTriggerShared() avoids overcounting there.
        if (gNeedSharedScan && MC12b_IsHwTriggerShared()) {
            uint32_t eosSeq = gScanEosSeq;                       // 32-bit atomic load
            if (eosSeq == gScanEosSeqSeen) gScanStaleDropped++;  // no new EOS since last tick -> stale
            gScanEosSeqSeen = eosSeq;
        }
        Streaming_Defer_Interrupt();
    }

    gInTimerHandler = false;

}

void Streaming_SetEncoderBuffer(uint8_t* buf, uint32_t size) {
    if (buf == NULL || size < ENCODER_BUFFER_MIN) return;
    buffer = buf;
    bufferSize = size;
    LOG_I("Encoder buffer: %u bytes", (unsigned)size);
}

/**
 * Write an encoded packet to an output buffer. All-or-nothing semantics:
 * the full packet is written or nothing is. No partial writes, no garbled
 * data at the receiver. Blocks until buffer has space or 10s timeout.
 *
 * Write functions are all-or-nothing: return len (full packet written) or
 * 0 (buffer full). This prevents the "convoy effect" where tiny partial
 * writes burn CPU on mutex lock/unlock without letting the output task drain.
 *
 * @param writeFn   All-or-nothing write function
 * @param buf       Encoded packet to write
 * @param len       Packet size in bytes
 * @return          len on success, 0 on 10s timeout (interface dead)
 */
typedef size_t (*StreamWriteFn)(const char* buf, size_t len);

#define STREAM_WRITE_TIMEOUT_MS 10000  // 10s — assume interface dead

static size_t Streaming_UsbWrite(const char* buf, size_t len) {
    return UsbCdc_WriteToBuffer(NULL, buf, len);
}

/* #486 — distinguish "shutdown-initiated abort" from "true 10 s output
 * timeout" in the caller.  Pass-3 used `==0` for both and disambiguated
 * with an IsEnabled re-read at the call site; pass-5 Qodo /improve
 * tightened this to use distinct sentinel values, so call sites can
 * branch on the return alone without racing IsEnabled.
 *
 *   TIMEOUT = 0      (matches "wrote 0 bytes" — also a real backpressure
 *                     failure that bumps drop counters)
 *   STOPPED = SIZE_MAX  (impossible-to-write count — explicit
 *                     stop-abort, no counter bumps, no log)
 *
 * Any positive return < len is treated as a partial write (currently
 * never produced by writeFn — they're whole-buffer-or-zero — but the
 * sentinel scheme leaves room).
 *
 * SIZE_MAX is from <stdint.h> via streaming.h's transitive includes. */
#define STREAM_WRITE_RETURN_TIMEOUT   ((size_t)0)
#define STREAM_WRITE_RETURN_STOPPED   (SIZE_MAX)

static size_t Streaming_WriteWithRetry(StreamWriteFn writeFn,
                                        const uint8_t* buf, size_t len) {
    /* Cache the runtime config pointer locally so all phases use a
     * consistent NULL-checked view (Qodo pass-3 /improve). */
    StreamingRuntimeConfig *cfg = gpRuntimeConfigStream;

    // Phase 1: quick spin — catch in-progress DMA completions
    for (int i = 0; i < 10; i++) {
        if (writeFn((const char*)buf, len) == len) return len;
    }

    /* #486 — abort retries if streaming was stopped.  The caller holds
     * gStreamingTaskInCritical for the full duration of this function;
     * if SCPI is waiting in the quiescence gate (100 ms bound),
     * continuing to retry for up to 10 s would cause STR:START to
     * spuriously fail with the abort error even though the operation
     * would succeed safely.  Lost output bytes are correct semantics
     * here — the user asked to stop, those bytes are stale by the
     * time the next stream starts. */
    if (cfg && !cfg->IsEnabled) return STREAM_WRITE_RETURN_STOPPED;

    // Phase 2: short sleeps — let lower-priority output tasks drain.
    // taskYIELD() only reschedules within the same priority; encoder at 6
    // yielding to SD at 5 is a no-op. vTaskDelay(1) blocks for 1 tick so
    // lower-priority SD task gets CPU to drain its circular buffer (#312).
    for (int i = 0; i < 50; i++) {
        vTaskDelay(1);
        if (writeFn((const char*)buf, len) == len) return len;
        if (cfg && !cfg->IsEnabled) return STREAM_WRITE_RETURN_STOPPED;
    }

    // Phase 3: tick-based backoff — 1ms sleeps, up to 10s timeout.
    // If we reach here, the output is overwhelmed. Block the encoder
    // so backpressure propagates to sample drops (the only acceptable loss).
    TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(STREAM_WRITE_TIMEOUT_MS);
    while (xTaskGetTickCount() < deadline) {
        vTaskDelay(1);
        if (writeFn((const char*)buf, len) == len) return len;
        if (cfg && !cfg->IsEnabled) return STREAM_WRITE_RETURN_STOPPED;
    }

    LOG_E("Output write timeout (%u ms, %u bytes) — interface dead",
          STREAM_WRITE_TIMEOUT_MS, (unsigned)len);
    return STREAM_WRITE_RETURN_TIMEOUT;
}

/**
 * Compute optimal circular buffer sizes based on active interfaces.
 *
 * Benchmark findings (issue #229, buffer sweep):
 * - USB buffer 4KB-64KB: no throughput difference (USB CDC scheduling is bottleneck)
 * - WiFi buffer 1.4KB-28KB: no throughput difference (WINC1500 SPI frame is bottleneck)
 * - Encoder buffer: 8KB optimal across all formats
 *
 * Strategy: give active interfaces their compile-time defaults (proven sufficient),
 * minimize inactive interfaces. All remaining pool space goes to sample depth.
 */
void Streaming_ComputeAutoBuffers(uint32_t* outUsbSize, uint32_t* outWifiSize,
                                   uint32_t* outSdSize, uint32_t* outSdDmaSize,
                                   uint32_t* outUsbDmaSize, uint32_t* outWifiDmaSize,
                                   uint32_t* outEncoderSize) {
    StreamingRuntimeConfig* sc = BoardRunTimeConfig_Get(
        BOARDRUNTIME_STREAMING_CONFIGURATION);
    sd_card_manager_settings_t* sd = BoardRunTimeConfig_Get(
        BOARDRUNTIME_SD_CARD_SETTINGS);

    bool hasUsb = (sc->ActiveInterface == StreamingInterface_USB ||
                   sc->ActiveInterface == StreamingInterface_UsbAndSd);
    bool hasWifi = (sc->ActiveInterface == StreamingInterface_WiFi);
    /* SD logging is actually requested only when all three conditions
     * hold: interface allows it (SD or All, or USB with enable+file
     * via SCPI_StartStreaming override), SD is enabled, and a filename
     * is set. Matches sdLoggingRequested in SCPIInterface.c — keeps
     * buffer allocation consistent with actual SD activity, avoids
     * reserving SD space during USB-only streaming when SD is dormant. */
    bool hasSd = (sc->ActiveInterface != StreamingInterface_WiFi) &&
                 sd->enable && sd->file[0] != '\0';

    // SD circular now lives in streaming pool (CPU-only, no DMA).
    // Active: full default size. Inactive: minimum (pool needs valid pointer).
    *outSdSize = hasSd ? SD_CARD_MANAGER_DEFAULT_CIRCULAR_SIZE
                       : STREAMING_SD_CIRCULAR_MIN;

    // DMA write buffers: divide entire coherent pool among active consumers.
    // Inactive interfaces get minimum. Remaining pool space is split
    // among active interfaces — no committed coherent RAM left unused.
    {
        uint32_t pool = CoherentPool_TotalSize();
        uint32_t overhead = 3 * COHERENT_POOL_ALIGNMENT;  // alignment per alloc
        uint32_t sdMin = SD_CARD_MANAGER_MIN_WBUFFER_SIZE;
        uint32_t usbMin = USBCDC_DMA_WBUFFER_MIN;
        uint32_t wifiMin = WIFI_DMA_MIN;

        // Start with minimums for all, then distribute remaining to active.
        *outSdDmaSize   = sdMin;
        *outUsbDmaSize  = usbMin;
        *outWifiDmaSize = wifiMin;

        uint32_t totalMin = sdMin + usbMin + wifiMin + overhead;
        if (pool > totalMin) {
            uint32_t avail = pool - totalMin;

            // Weighted distribution across active interfaces — weights only
            // count for interfaces that are actually active, so `avail` is
            // always fully allocated (no inactive-interface "share" wasted).
            // Ratios (when multiple active): SD=5, USB=3, WiFi=2.  Single-
            // active is the degenerate weighted case (all weight on one).
            //
            // Prior bug (#502 follow-up audit): the activeCount==2 branch
            // wrote `wifiShare = hasWifi ? (avail - sdShare - usbShare) : 0`,
            // which dropped 20% (~24.7 KB) of the coherent pool on the
            // floor for USB+SD streaming.
            uint32_t sdW   = hasSd   ? 5u : 0u;
            uint32_t usbW  = hasUsb  ? 3u : 0u;
            uint32_t wifiW = hasWifi ? 2u : 0u;
            uint32_t totW  = sdW + usbW + wifiW;
            if (totW > 0) {
                uint32_t sdShare   = (avail * sdW)   / totW;
                uint32_t usbShare  = (avail * usbW)  / totW;
                uint32_t wifiShare = (avail * wifiW) / totW;

                // Route the integer-division remainder to the highest-
                // priority active interface (SD > USB > WiFi) so the full
                // `avail` lands somewhere.  totW > 0 guarantees at least
                // one of the three is active, so one of the branches will
                // always absorb the remainder.
                uint32_t accounted = sdShare + usbShare + wifiShare;
                uint32_t rem = (avail > accounted) ? (avail - accounted) : 0u;
                if (rem > 0u) {
                    if (hasSd)        sdShare   += rem;
                    else if (hasUsb)  usbShare  += rem;
                    else              wifiShare += rem;  /* hasWifi true */
                }

                if (hasSd)   *outSdDmaSize   += sdShare;
                if (hasUsb)  *outUsbDmaSize  += usbShare;
                if (hasWifi) *outWifiDmaSize += wifiShare;
            }
            // totW == 0 (degenerate: no active interface — possible if
            // ActiveInterface=SD but sd->enable=false) leaves all three
            // buffers at minimum.  No streaming consumer exists, so the
            // unused RAM doesn't actively hurt — partition will be
            // recomputed when the user re-enables a consumer.
        }
    }

    // Encoder buffer: 16KB when SD active (larger writes reduce SPI overhead),
    // 8KB default otherwise (sufficient for USB/WiFi).
    *outEncoderSize = hasSd ? (ENCODER_BUFFER_DEFAULT * 2) : ENCODER_BUFFER_DEFAULT;

    // Circular buffers: larger buffers reduce retry frequency for
    // all-or-nothing writes, but must leave enough pool for samples.
    //
    // #497: the StreamingInterface enum has no USB+WiFi or WiFi+SD
    // combination — WiFi is always single-interface (see
    // StreamingRuntimeConfig.h:17-22 and the runtime guard at
    // SCPIInterface.c:2823 that refuses WiFi while SD is logging due
    // to the SPI bus conflict).  So `hasWifi` always means "WiFi is
    // the SOLE active interface".  Bump to STREAMING_WIFI_WIFI_ONLY
    // (96 KB) — triples the burst-absorption window vs the prior
    // fixed 32 KB before WINC SPI back-pressure cascades to wst.
    //
    // Sample-pool tradeoff (Qodo PR #501 pass 2): the pool is NOT
    // capped here at 1100; that's only the DEFAULT_AIN_SAMPLE_COUNT.
    // MAX_AIN_SAMPLE_COUNT is 10000, and Partition() in auto mode
    // (sampleCount == 0) sets sampleCount = remaining_pool_bytes /
    // per_sample_bytes (capped at MAX).  So bumping WiFi 32→96 KB
    // shrinks the auto-sized sample pool by ~64 KB / per-sample
    // (≈ 2000 fewer samples at 5×T1's 30 B/sample stride).  That in
    // turn shrinks the FreeRTOS heap used by the AInSample queue —
    // observable as the heap delta in #501 validation (13272 → 4344
    // during wifi_5xT1 @ 2 kHz).  Acceptable tradeoff because
    // SamplePoolMaxUsed measurements on actual workloads are tiny
    // (≤16 across all probed rates per #497 evidence), so even the
    // post-shrink sample pool has 100× burst-absorption headroom for
    // streaming use.
    *outWifiSize = hasWifi ? STREAMING_WIFI_WIFI_ONLY : STREAMING_WIFI_MIN;
    *outUsbSize  = hasUsb  ? STREAMING_USB_DEFAULT    : STREAMING_USB_MIN;
}

/*!
 * Starts the streaming timer
 */
static void Streaming_DrainSessionSampleQueues(void);

static void Streaming_Start(void) {
    if (!gpRuntimeConfigStream->Running) {
        // Buffer and sample pool resize is handled in SCPI_StartStreaming
        // (USB task context) via StreamingBufferPool_Partition before
        // IsEnabled is set.

        // Clear any stale samples from the previous streaming session —
        // AIN queue and (#533) the DIO sample list, which isn't covered by
        // the start-path pool re-init.  Symmetric backstop to the drain in
        // Streaming_Stop, catching a sample pushed by an in-flight
        // deferred-task tick after stop.
        Streaming_DrainSessionSampleQueues();

        // #533 ROOT CAUSE: invalidate the per-channel BOARDDATA_AIN_LATEST
        // snapshots.  LATEST is a one-conversion-deep cache: the deferred
        // ISR task reads it at tick N to emit the sample for tick N-1's
        // conversion.  The PREVIOUS session's final conversion therefore
        // sits in LATEST across the stop gap (it is never invalidated),
        // and the first tick of the NEXT session would re-read it —
        // emitting one frame carrying the prior session's timestamp,
        // exactly one sample period past that session's last frame (the
        // desktop-observed leftover; HW-reproduced 6/6 on a factory-state
        // device, where no background ADC polling masks the stale stamp).
        // Timestamp=0 marks a slot invalid: the deferred task skips such
        // channels (real-ADC mode) or falls back to the fresh streaming
        // trigger stamp (test-pattern modes).  Plain 32-bit stores are
        // atomic on PIC32MZ (no critical section needed); a concurrent
        // background-poll ISR write is last-writer-wins per field, both
        // outcomes valid.
        {
            const AInArray* pAinCfg = BoardConfig_Get(BOARDCONFIG_AIN_CHANNELS, 0);
            if (pAinCfg != NULL) {
                for (size_t ch = 0; ch < pAinCfg->Size; ch++) {
                    AInSample* pLatest = BoardData_Get(BOARDDATA_AIN_LATEST, ch);
                    if (pLatest != NULL) {
                        pLatest->Timestamp = 0;
                    }
                }
            }
        }

        // Only clear stats when starting a new enabled session.
        // Streaming_UpdateState() calls Stop→Start even on disable,
        // and we need stats to survive for post-session query.
        if (gpRuntimeConfigStream->IsEnabled) {
            Streaming_ClearStats();
            Streaming_InitFlowWindow(gpRuntimeConfigStream->Frequency);
            // Reset test pattern counter so each session starts at 0
            taskENTER_CRITICAL();
            gTestPatternSampleCount = 0;
            taskEXIT_CRITICAL();
            // #450: anchor the startup-grace window at the start of each
            // enabled session.  Steady drop counters won't increment
            // until xTaskGetTickCount() - gStreamStartTick >= grace.
            gStreamStartTick = xTaskGetTickCount();
        }

        // Clear encoding buffer once to prevent stale data artifacts in SD files
        if (buffer != NULL) memset(buffer, 0, bufferSize);

        gSdPbMetadataSent = false;
        // If SD file is already open and ready (SCPI_StartStreaming waited
        // for it), start with true to avoid dropping packets while the
        // encoder waits for the first sdSize > 0 detection.
        gSdFileWasReady = sd_card_manager_IsWriteReady();

        TimerApi_Initialize(gpStreamingConfig->TimerIndex);
        TimerApi_PeriodSet(gpStreamingConfig->TimerIndex, gpRuntimeConfigStream->ClockPeriod);
        TimerApi_CallbackRegister(gpStreamingConfig->TimerIndex, Streaming_TimerHandler, 0);
        // Harmony's TMRx_Initialize (e.g. plib_tmr4.c:80) sets the IEC bit
        // as a side effect — disable it here so the InterruptEnable gate
        // below is authoritative.  Without this defensive disable, the IRQ
        // is armed at boot even though we gate TimerApi_Start on IsEnabled.
        TimerApi_InterruptDisable(gpStreamingConfig->TimerIndex);

        // Arm the timer ISR + start the timer + flip Running only when
        // streaming is truly enabled.  Streaming_Start runs at boot via
        // Streaming_UpdateState's unconditional Stop→Start cycle with
        // IsEnabled=false; pre-fix this set Running=1 unconditionally,
        // lying about hardware state to every consumer checking `Running`
        // alone (HAL/ADC.c:129, SCPIInterface.c:1916 "streaming already
        // active" guard, SCPIADC.c:64/439).  Now the invariant: Running
        // == true iff the timer is actually ticking — see #379.
        // InterruptEnable was previously also unconditional; moving it
        // inside the gate keeps it symmetric with Streaming_Stop's
        // TimerApi_InterruptDisable (which only fires when Running was
        // true), so the timer ISR is never armed-without-source.
        if (gpRuntimeConfigStream->IsEnabled) {
            // Enable hardware ADC triggering only when actually streaming.
            // Streaming_Start runs at boot before ADC_Init — must not
            // write ADCTRG/ADCCON1 until ADC is ready.
            // #541 D-B: rebuild the shared (MODULE7) scan list for this
            // session — enabled T2 user channels, plus monitoring channels
            // when OBDiag=1 (the dead temp sensor is always excluded,
            // erratum 18).  The Harmony boot CSS scanned all 19 inputs
            // regardless of configuration, fixing T_scan at ~216 us and
            // making the #539 EOS collapse channel-count-independent;
            // scanning only what the session uses lets T_scan (and the
            // scan-rate bound) scale with the actual config.
            //
            // The scan is armed iff the session scan list is non-empty.
            // This supersedes the #537 total-based gate: T1 results no
            // longer ride the EOS task while streaming (#541 D-A reads
            // them directly via ARDY), so a T1-only OBDiag=0 session
            // genuinely needs no scan.  The mid-stream-enable edge that
            // forced #537's conservative gate is closed — CONF:ADC:CHANnel
            // and CONF:ADC:OBDiag are both REJECTED while streaming
            // (#116), so the session's scan list cannot go stale.
            {
                uint32_t css1, css2;
                uint32_t scanCount = MC12b_ComputeScanList(
                        true, gpRuntimeConfigStream->OnboardDiagEnabled,
                        &css1, &css2);
                MC12b_ApplyScanList(css1, css2);
                gNeedSharedScan = (scanCount > 0);
            }
            // #541 D-A: clear any Type 1 result parked since the last idle
            // poll — ARDY would still be set and the direct-read path would
            // emit the stale conversion as this session's first T1 sample
            // (same class as the #533 LATEST invalidation above).
            MC12b_DrainType1Results();
            bool hwShared = (gpRuntimeConfigStream->OnboardDiagEnabled ||
                             gNeedSharedScan) &&
                            (gpRuntimeConfigStream->ChannelScanFreqDiv <= 1);
            MC12b_ConfigureHardwareTrigger(true, hwShared);

            // #292: EOS stays enabled across all modes. The EOS deferred
            // task now reads T1 user channels (moved from ADC_DATA3 ISR),
            // so disabling EOS would break T1 streaming. OBDiag=0 gating
            // is handled in the task body (skips monitoring reads when
            // Running && !OnboardDiagEnabled).

            // Order matters: publish Running=1 BEFORE arming the IRQ
            // and starting the timer.  Streaming_TimerHandler itself
            // gates on IsEnabled (not Running), so this is NOT about
            // protecting the ISR — it's about async TASK consumers
            // (HAL/ADC.c:129 EOS deferred task, SCPI handlers) that
            // could otherwise observe Running=0 for a few cycles while
            // the timer is already ticking, producing inconsistent
            // hardware-state reads.
            gpRuntimeConfigStream->Running = 1;
            TimerApi_InterruptEnable(gpStreamingConfig->TimerIndex);
            TimerApi_Start(gpStreamingConfig->TimerIndex);
        }
    }
}

/*!
 * #397 Self-heal: check the health of every transport selected by
 * ActiveInterface.  Returns true if every configured transport has been
 * unhealthy for longer than the grace window (auto-stop trigger);
 * returns false otherwise.  Recovered transports clear their down-since
 * counter and log a recovery line.
 */
static bool Streaming_AllConfiguredTransportsDead(StreamingRuntimeConfig *cfg) {
    TickType_t now = xTaskGetTickCount();
    TickType_t graceTicks = pdMS_TO_TICKS((TickType_t)gTransportGraceSec * 1000U);
    bool wantUsb = (cfg->ActiveInterface == StreamingInterface_USB ||
                    cfg->ActiveInterface == StreamingInterface_UsbAndSd);
    bool wantWifi = (cfg->ActiveInterface == StreamingInterface_WiFi);
    bool wantSd  = (cfg->ActiveInterface == StreamingInterface_SD ||
                    cfg->ActiveInterface == StreamingInterface_UsbAndSd);

    bool usbDead = false, wifiDead = false, sdDead = false;

    if (wantUsb) {
        if (UsbCdc_IsConfigured()) {
            if (gTransportDownSinceUsb != 0) {
                LOG_I("Streaming: USB transport recovered");
                gTransportDownSinceUsb = 0;
            }
        } else {
            if (gTransportDownSinceUsb == 0) {
                // Use 1 as "armed" sentinel to disambiguate from 0=healthy.
                gTransportDownSinceUsb = (now == 0) ? 1 : now;
            }
            usbDead = ((now - gTransportDownSinceUsb) >= graceTicks);
        }
    } else {
        gTransportDownSinceUsb = 0;
    }

    if (wantWifi) {
        if (wifi_manager_IsWiFiConnected()) {
            if (gTransportDownSinceWifi != 0) {
                LOG_I("Streaming: WiFi transport recovered");
                gTransportDownSinceWifi = 0;
            }
        } else {
            if (gTransportDownSinceWifi == 0) {
                gTransportDownSinceWifi = (now == 0) ? 1 : now;
            }
            wifiDead = ((now - gTransportDownSinceWifi) >= graceTicks);
        }
    } else {
        gTransportDownSinceWifi = 0;
    }

    if (wantSd) {
        if (sd_card_manager_IsWriteReady()) {
            if (gTransportDownSinceSd != 0) {
                LOG_I("Streaming: SD transport recovered");
                gTransportDownSinceSd = 0;
            }
        } else {
            if (gTransportDownSinceSd == 0) {
                gTransportDownSinceSd = (now == 0) ? 1 : now;
            }
            sdDead = ((now - gTransportDownSinceSd) >= graceTicks);
        }
    } else {
        gTransportDownSinceSd = 0;
    }

    bool anyConfigured = wantUsb || wantWifi || wantSd;
    if (!anyConfigured) return false;

    bool allDead = true;
    if (wantUsb)  allDead = allDead && usbDead;
    if (wantWifi) allDead = allDead && wifiDead;
    if (wantSd)   allDead = allDead && sdDead;
    return allDead;
}

uint32_t Streaming_GetTransportGraceSec(void) {
    return gTransportGraceSec;
}

bool Streaming_SetTransportGraceSec(uint32_t sec) {
    if (sec < TRANSPORT_GRACE_MIN_SEC || sec > TRANSPORT_GRACE_MAX_SEC) {
        return false;
    }
    gTransportGraceSec = sec;
    return true;
}

// #450 — startup-drop grace window getters/setters.  Symmetric with
// TransportGrace above; same semantics.
uint32_t Streaming_GetLossGraceSec(void) {
    return gLossGraceSec;
}

bool Streaming_SetLossGraceSec(uint32_t sec) {
    if (sec < LOSS_GRACE_MIN_SEC || sec > LOSS_GRACE_MAX_SEC) {
        return false;
    }
    gLossGraceSec = sec;
    return true;
}

/* #486: queried by SCPI_StartStreaming before the re-partition window.
 * Two-flag check is correct without a critical section: each flag is
 * atomic on PIC32MZ (32-bit r/w), and a transient state where one is 0
 * and the other is 1 just means the caller will spin one more
 * vTaskDelay(1) iteration — never observes a torn quiescent state. */
bool Streaming_TasksAreQuiescent(void) {
    return (gStreamingTaskInCritical == 0 && gDeferredTaskInCritical == 0);
}

/*!
 * Stops the streaming timer
 */
/**
 * Shared SD drop bookkeeping (#534 DRY): counter pair + QUES bit under one
 * critical section so a concurrent SYST:STR:STATS? snapshot sees them
 * coherently (steady never > total).  Callers log their own context-
 * specific LOG_E_SESSION line.
 */
static void Streaming_CountSdDrop(size_t packetSize) {
    bool pastGrace = Streaming_PastStartupGrace();
    taskENTER_CRITICAL();
    gStreamStats.sdDroppedBytes += packetSize;
    if (pastGrace) {
        gStreamStats.sdDroppedBytesSteady += packetSize;
    }
    gQuesBits |= QUES_BIT_SD_OVERFLOW;
    taskEXIT_CRITICAL();
}

/**
 * #533: drain both per-session sample queues (AIN + DIO) so no sample
 * captured by one session can be encoded into the next.  Called from
 * Streaming_Stop (the session is over — discard) and Streaming_Start
 * (symmetric backstop for a sample pushed by an in-flight deferred-task
 * tick between the stop-side drain and the next start).  Both pops are
 * 0-tick non-blocking (AINSAMPLE_/DIOSAMPLE_QUEUE_TICKS_TO_WAIT == 0).
 */
static void Streaming_DrainSessionSampleQueues(void) {
    AInPublicSampleList_t* pStaleAin;
    while (AInSampleList_PopFront(&pStaleAin)) {
        if (pStaleAin != NULL) {
            AInSampleList_FreeToPool(pStaleAin);
        }
    }
    tBoardData* pBd = BoardData_Get(BOARDDATA_ALL_DATA, 0);
    if (pBd != NULL) {
        DIOSample staleDio;
        while (DIOSampleList_PopFront(&pBd->DIOSamples, &staleDio)) {
            // discard — belongs to the previous session
        }
    }
}

static void Streaming_Stop(void) {
    if (gpRuntimeConfigStream->Running) {
        TimerApi_Stop(gpStreamingConfig->TimerIndex);
        TimerApi_InterruptDisable(gpStreamingConfig->TimerIndex);

        // Revert ADC to software triggering so non-streaming reads
        // (ADC_Tasks polling) still work.
        MC12b_ConfigureHardwareTrigger(false, false);
        // #541 D-B: restore the idle scan list (all public T2 + enabled
        // monitoring) so idle-time MEAS:VOLT:DC? and SYST:INFo monitoring
        // cover the full channel set again, not just last session's subset.
        MC12b_RestoreIdleScanList();
        gpRuntimeConfigStream->Running = false;

        // #533: drain BOTH sample queues at stop so nothing captured by
        // this session's final ticks can be encoded into the NEXT session.
        // The AIN queue was previously cleared only at start; the DIO list
        // was never cleared at all — a DIO sample pushed by the final tick
        // sat latched across stop (surviving port close/reopen and repeated
        // stops) and, encoded as the next session's first DIO-carrying
        // message, stamped it with this session's StreamTrigStamp — the
        // desktop-observed leftover frame exactly one sample period past
        // the prior session's last frame.  A late deferred-task push after
        // this drain is caught by the symmetric drain in Streaming_Start.
        Streaming_DrainSessionSampleQueues();

        // #367 diagnostics: snapshot bytes still sitting in the WiFi TCP
        // circular buffer at session end.  If TotalBytesStreamed -
        // WifiTcpBytesSent - WifiDroppedBytes equals this value, the
        // accounting gap is "tail bytes never drained at Stop".
        gStreamStats.circularBufferEndBytes =
            wifi_tcp_server_GetCircularBufferAvailable();
        if (gStreamStats.circularBufferEndBytes > 0) {
            LOG_E_SESSION(LOG_SESSION_BUFFER_TAIL,
                "diag367: circular buffer tail at Stop = %u bytes",
                (unsigned)gStreamStats.circularBufferEndBytes);
        }

        // Log session summary if any data was lost.
        // Gate on STEADY counters so startup-window transients (within
        // gLossGraceSec, default 3 s) don't produce misleading end-of-
        // session error logs.  Total counters are still available via
        // SYST:STR:STATS? for forensic diagnostic.
        bool hadDrops = gStreamStats.queueDroppedSamplesSteady > 0 ||
                        gStreamStats.usbDroppedBytesSteady > 0 ||
                        gStreamStats.wifiDroppedBytesSteady > 0 ||
                        gStreamStats.sdDroppedBytesSteady > 0 ||
                        gStreamStats.encoderFailuresSteady > 0 ||
                        gStreamStats.dioDroppedSamplesSteady > 0 ||
                        gStreamStats.eosOverruns > 0;  // no Steady variant — hw staleness, not a grace-window false flag
        // Clear runtime overflow / data-loss condition bits — they refer to
        // the live session that just ended.  Preserve QUES_BIT_TRANSPORT_DOWN
        // (#397) because it captures the REASON streaming stopped; clearing
        // it here would make the auto-stop bit unobservable via SCPI
        // immediately after the stop.  It's cleared by Streaming_ClearStats
        // at next session start, so STAT:QUES:COND? between auto-stop and
        // next start correctly reports "transport down was the cause".
        // RMW (`&=`) needs taskENTER_CRITICAL per the CLAUDE.md atomicity
        // rules — gQuesBits is also `|=`'d by the deferred ISR task and
        // streaming task at the overflow sites.
        taskENTER_CRITICAL();
        gQuesBits &= (QUES_BIT_TRANSPORT_DOWN | QUES_BIT_SPI_BUS_FAULT);
        taskEXIT_CRITICAL();

        if (hadDrops) {
            uint64_t totalAttempted = gStreamStats.totalSamplesStreamed +
                                     gStreamStats.queueDroppedSamples;
            // EOS coalescing is data staleness (ADC register overwrite),
            // not a dropped sample — exclude from loss total/percentage.
            // Steady counters for the loss math: startup-window transients
            // shouldn't inflate the reported loss percent.
            // #557: scan-stale ticks are genuine dropped samples (the prior
            // scan never completed — its data is stale), so include them in the
            // loss total, unlike eosOverruns (task-behind-but-fresh, excluded).
            uint32_t totalSampleLoss = gStreamStats.queueDroppedSamplesSteady +
                                      gStreamStats.encoderDroppedSamplesSteady +
                                      gStreamStats.dioDroppedSamplesSteady +
                                      gScanStaleDropped;
            uint32_t lossPercent = totalAttempted > 0
                ? (uint32_t)((totalSampleLoss * 100ULL) / totalAttempted)
                : 0;
            LOG_E("Stream end: lost %u/%llu samples (%u%%), USB=%u WiFi=%u SD=%u bytes, encFail=%u encDrop=%u dioDrop=%u eos=%u (all post-grace)",
                  (unsigned)totalSampleLoss,
                  (unsigned long long)totalAttempted,
                  (unsigned)lossPercent,
                  (unsigned)gStreamStats.usbDroppedBytesSteady,
                  (unsigned)gStreamStats.wifiDroppedBytesSteady,
                  (unsigned)gStreamStats.sdDroppedBytesSteady,
                  (unsigned)gStreamStats.encoderFailuresSteady,
                  (unsigned)gStreamStats.encoderDroppedSamplesSteady,
                  (unsigned)gStreamStats.dioDroppedSamplesSteady,
                  (unsigned)gStreamStats.eosOverruns);
        }
    }
}

void Streaming_Init(tStreamingConfig* pStreamingConfigInit,
        StreamingRuntimeConfig* pStreamingRuntimeConfigInit) {
    /* Defensive zero-init for retained-RAM safety (#409). With -fdata-sections
     * each file-static lands in its own .bss.<name> section, often placed by
     * the best-fit allocator OUTSIDE [_bss_begin,_bss_end] — so the
     * compile-time `= 0` initializers are not honored across MCLR / IPE flash.
     * No critical section needed: this runs pre-scheduler with interrupts off. */
    gTestPattern = 0;
    gTestPatternSampleCount = 0;
    gBenchmarkMode = BENCHMARK_OFF;
    gSdPbMetadataSent = false;
    gSdFileWasReady = false;
    memset((void*)&gStreamStats, 0, sizeof(gStreamStats));
    gTimerISRCalls = 0;
    gScanStaleDropped = 0;
    gScanEosSeq = 1u;       // #557/#563: prime seq one ahead of "seen" so the
    gScanEosSeqSeen = 0u;   // first post-start tick (no scan completed yet) reads fresh
    memset(gFlowWindow, 0, sizeof(gFlowWindow));
    gFlowWindowCount = 0;
    gFlowWindowSize = FLOW_WINDOW_MIN;
    gFlowWindowOverride = 0;
    taskENTER_CRITICAL();
    gQuesBits &= QUES_BIT_SPI_BUS_FAULT;  // externally-owned fault bit survives session clears
    taskEXIT_CRITICAL();
    gInTimerHandler = false;
    /* #397 self-heal: defensive reset of transport-down trackers and the
     * grace window. These live in retained-RAM along with the other
     * file-statics; the static initializer values are not guaranteed to
     * survive MCLR / IPE flash. */
    gTransportDownSinceUsb = 0;
    gTransportDownSinceWifi = 0;
    gTransportDownSinceSd = 0;
    gTransportGraceSec = TRANSPORT_GRACE_DEFAULT_SEC;
    /* #450 startup-grace bookkeeping — same retained-RAM concern. */
    gStreamStartTick = 0;
    gLossGraceSec = LOSS_GRACE_DEFAULT_SEC;
    /* buffer/bufferSize are file-statics that may also live in
     * retained-RAM. Reset before the `if (buffer == NULL)` guard
     * below so a stale non-NULL pointer doesn't skip the pool fetch. */
    buffer = NULL;
    bufferSize = 0;

    gpStreamingConfig = pStreamingConfigInit;
    gpRuntimeConfigStream = pStreamingRuntimeConfigInit;

    // Set encoder buffer from pool (partitioned at boot)
    if (buffer == NULL) {
        uint8_t* encBuf; uint32_t encLen;
        StreamingBufferPool_GetEncoder(&encBuf, &encLen);
        if (encBuf != NULL && encLen > 0) {
            buffer = encBuf;
            bufferSize = encLen;
        }
    }

    TimestampTimer_Init();
    TimerApi_Stop(gpStreamingConfig->TimerIndex);
    TimerApi_InterruptDisable(gpStreamingConfig->TimerIndex);
    TimerApi_PeriodSet(gpStreamingConfig->TimerIndex, gpRuntimeConfigStream->ClockPeriod);
    gpRuntimeConfigStream->Running = false;
}

void Streaming_ResetSdPbMetadata(void) {
    gSdPbMetadataSent = false;
    gSdFileWasReady = false;
}

void Streaming_GetStats(StreamingStats* out) {
    if (out == NULL) return;
    taskENTER_CRITICAL();
    *out = gStreamStats;
    // Snapshot the volatile ISR counter into the struct copy. The volatile
    // read forces a fresh load from memory; the critical section blocks the
    // timer ISR (priority 1) so we get a coherent reading.
    out->timerISRCalls = gTimerISRCalls;
    out->scanStaleDropped = gScanStaleDropped;  // #557 (separate volatile, like timerISRCalls)
    taskEXIT_CRITICAL();
}

void Streaming_ClearStats(void) {
    // Defensive: this function uses taskENTER_CRITICAL which is not safe
    // from ISR context. All current callers (SCPI handlers, Streaming_Start)
    // run in task context. Belt-and-suspenders: configASSERT catches misuse
    // in debug builds, and the runtime guard below makes us silently no-op
    // (rather than crash) if called from an ISR in a release build where
    // configASSERT compiles out.
    //
    // Note: xPortIsInsideInterrupt() is not implemented in the PIC32MZ
    // FreeRTOS port, so we check uxInterruptNesting directly — the same
    // pattern used by Logger.c::LogIsInISR(). The variable is maintained
    // by the assembly ISR wrappers in ISR_Support.h.
    configASSERT(uxInterruptNesting == 0);
    if (uxInterruptNesting != 0) {
        return;
    }

    // Mid-session clearing is intentionally allowed: callers may want to
    // measure throughput over a sub-window without restarting the stream
    // (e.g., wait for steady state, clear, measure for N seconds, query).
    // After this call, all counters represent "since last clear" rather
    // than "since session start" — the caller is responsible for tracking
    // the elapsed time of their measurement window.
    //
    // Single critical section covers ALL session observability state:
    //   - gStreamStats:     written by deferred ISR task (sample/drop counters)
    //                       and streaming task (encoder/output drop counters)
    //   - gTimerISRCalls:   written by timer ISR (priority 1)
    //   - gFlowWindow:      written by deferred ISR task (priority 8)
    //   - gFlowWindowCount: written by deferred ISR task
    //   - gQuesBits:        written by streaming task on threshold cross
    //   - Logger session one-shots: reset alongside so observers don't see
    //                       half-cleared session state across the boundary
    //
    // SCPI:STR:CLEARSTATS can be invoked mid-session from USB (priority 7).
    // The deferred task at priority 8 can preempt the SCPI handler at any
    // time, so without a single atomic clear a concurrent reader could see
    // half-reset state. taskENTER_CRITICAL raises syscall priority to 4,
    // blocking the timer ISR (priority 1) — and since the deferred task
    // wakes only via that ISR's notification, it's transitively blocked
    // for the duration of the clear.
    taskENTER_CRITICAL();
    memset((void*)&gStreamStats, 0, sizeof(gStreamStats));
    gTimerISRCalls = 0;
    gScanStaleDropped = 0;
    gScanEosSeq = 1u;       // #557/#563: prime seq one ahead of "seen" so the
    gScanEosSeqSeen = 0u;   // first post-start tick (no scan completed yet) reads fresh
#if PB_PROFILE_COUNTERS
    // #388: also clear UsbCdc's in-flight DMA timestamp so it doesn't
    // leak into the new session's usbDmaPendingCycles on the next
    // WRITE_COMPLETE event.
    UsbCdc_Profile_ResetPendingStamp();
#endif
    memset(gFlowWindow, 0, sizeof(gFlowWindow));
    gFlowWindowCount = 0;
    taskENTER_CRITICAL();
    gQuesBits &= QUES_BIT_SPI_BUS_FAULT;  // externally-owned fault bit survives session clears
    taskEXIT_CRITICAL();
    // #397 reset per-transport down-since trackers so a new session starts
    // with every transport considered healthy, regardless of pre-session state.
    gTransportDownSinceUsb = 0;
    gTransportDownSinceWifi = 0;
    gTransportDownSinceSd = 0;
    Logger_ResetSessionOneShots();
    taskEXIT_CRITICAL();
    // NOTE: Pool max-used is NOT reset here — it persists across sessions
    // so users can check peak usage after stopping.
}

/**
 * Compute the flow window size based on streaming frequency.
 * If gFlowWindowOverride > 0, uses that value directly (clamped to MIN..MAX).
 * Otherwise auto-calculates: clamp(frequency * 2, MIN, MAX), giving ~2 seconds
 * of history at any rate with a floor for statistical significance.
 */
static void Streaming_InitFlowWindow(uint64_t frequency) {
    if (gFlowWindowOverride > 0) {
        uint32_t n = gFlowWindowOverride;
        if (n < FLOW_WINDOW_MIN) n = FLOW_WINDOW_MIN;
        if (n > FLOW_WINDOW_MAX) n = FLOW_WINDOW_MAX;
        gFlowWindowSize = n;
    } else {
        uint64_t n = frequency * 2;
        if (n < FLOW_WINDOW_MIN) n = FLOW_WINDOW_MIN;
        if (n > FLOW_WINDOW_MAX) n = FLOW_WINDOW_MAX;
        gFlowWindowSize = (uint32_t)n;
    }
    memset(gFlowWindow, 0, sizeof(gFlowWindow));
    gFlowWindowCount = 0;
    taskENTER_CRITICAL();
    gQuesBits &= QUES_BIT_SPI_BUS_FAULT;  // RMW: preserve externally-owned fault bit
    taskEXIT_CRITICAL();
}

void Streaming_QuesExternalSet(uint32_t mask)
{
    taskENTER_CRITICAL();
    gQuesBits |= mask;
    taskEXIT_CRITICAL();
}

void Streaming_QuesExternalClear(uint32_t mask)
{
    taskENTER_CRITICAL();
    gQuesBits &= ~mask;
    taskEXIT_CRITICAL();
}

/**
 * Called once per sample period from the deferred ISR task.
 * Updates the windowed flow tracking and recomputes loss percentage.
 * @param dropped true if this sample was dropped (queue full)
 */
static void Streaming_UpdateFlowWindow(bool dropped) {
    gFlowWindow[1].attempted++;
    if (dropped) {
        gFlowWindow[1].dropped++;
    }
    gFlowWindowCount++;

    // Rotate window when current half is full
    if (gFlowWindowCount >= gFlowWindowSize) {
        gFlowWindow[0] = gFlowWindow[1];
        memset(&gFlowWindow[1], 0, sizeof(FlowWindow));
        gFlowWindowCount = 0;
    }

    // Compute loss over both halves (previous + current)
    uint32_t totalAttempted = gFlowWindow[0].attempted + gFlowWindow[1].attempted;
    uint32_t totalDropped = gFlowWindow[0].dropped + gFlowWindow[1].dropped;

    uint32_t lossPct = 0;
    if (totalAttempted > 0) {
        lossPct = (totalDropped * 100) / totalAttempted;
    }
    // Critical section: keep windowLossPercent and gQuesBits coherent for
    // SCPI snapshot reads (also protects RMW on gQuesBits which is modified
    // by the streaming task at lower priority)
    taskENTER_CRITICAL();
    gStreamStats.windowLossPercent = lossPct;
    if (lossPct >= gLossThresholdPct) {
        gQuesBits |= QUES_BIT_DATA_LOSS;
    } else {
        gQuesBits &= ~QUES_BIT_DATA_LOSS;
    }
    taskEXIT_CRITICAL();
}

uint32_t Streaming_GetQuesBits(void) {
    return gQuesBits;  // 32-bit read is atomic on PIC32MZ
}

void Streaming_IncrDioDropped(void) {
    bool pastGrace = Streaming_PastStartupGrace();
    taskENTER_CRITICAL();
    gStreamStats.dioDroppedSamples++;  // Single writer (deferred ISR task, pri 8)
    if (pastGrace) {
        gStreamStats.dioDroppedSamplesSteady++;
    }
    taskEXIT_CRITICAL();
}

void Streaming_IncrEosOverruns(uint32_t missed) {
    gStreamStats.eosOverruns += missed;  // Single writer (EOS task, pri 8)
}

// #557: called from the ADC EOS ISR (ADC_EOSInterruptCB) each time the shared
// MODULE7 scan completes. Single volatile write — ISR-safe, no critical section.
void Streaming_NoteEosFired(void) {
    gScanEosSeq++;   /* bump per completed scan; the EOS ISR is the only writer (RMW safe) */
}

#if PB_PROFILE_COUNTERS
// #388: PB streaming profile sample inputs from UsbCdc.c.  Each writer
// (USB task, pri 7) is a single producer for its respective counter, but
// gStreamStats is read via Streaming_GetStats() under taskENTER_CRITICAL,
// so the 64-bit accumulate must also be critical-section guarded to
// prevent torn reads on the snapshot side.  Idle-count is 32-bit so the
// increment is atomic on PIC32MZ and doesn't need the critical section.
void Streaming_AddProfileSample_WriteBuf(uint32_t cycles) {
    taskENTER_CRITICAL();
    gStreamStats.usbWriteBufCycles += cycles;
    taskEXIT_CRITICAL();
}
void Streaming_AddProfileSample_DmaCopy(uint32_t cycles) {
    taskENTER_CRITICAL();
    gStreamStats.usbDmaCopyCycles += cycles;
    taskEXIT_CRITICAL();
}
void Streaming_AddProfileSample_DmaIdle(void) {
    // CLAUDE.md: 32-bit RMW (`++`) is NOT atomic — must be critical-
    // section guarded.  Streaming_ClearStats() zeroes gStreamStats under
    // taskENTER_CRITICAL, so an unguarded ++ here could lose a count
    // across the clear boundary.
    taskENTER_CRITICAL();
    gStreamStats.usbDmaIdleCount++;
    taskEXIT_CRITICAL();
}
void Streaming_AddProfileSample_DmaPending_FromISR(uint32_t cycles) {
    // Called from USB_DEVICE_CDC_EVENT_WRITE_COMPLETE handler (ISR
    // context per UsbCdc.c file header).  taskENTER_CRITICAL is task-
    // context only — use the FROM_ISR variant to bracket the 64-bit
    // accumulate.
    UBaseType_t saved = taskENTER_CRITICAL_FROM_ISR();
    gStreamStats.usbDmaPendingCycles += cycles;
    taskEXIT_CRITICAL_FROM_ISR(saved);
}
#endif

uint32_t Streaming_GetLossThreshold(void) {
    return gLossThresholdPct;
}

void Streaming_SetLossThreshold(uint32_t pct) {
    if (pct < 1) pct = 1;
    if (pct > 100) pct = 100;
    gLossThresholdPct = pct;  // takes effect immediately (next flow window check)
}

uint32_t Streaming_GetFlowWindowOverride(void) {
    return gFlowWindowOverride;
}

void Streaming_SetFlowWindowOverride(uint32_t size) {
    // 0 = auto, otherwise clamped at next streaming start
    gFlowWindowOverride = size;
}

// Stop then (re)start the streaming timer + task to apply the current
// runtime config (frequency, IsEnabled, interface).
//
// IMPORTANT — this does NOT partition/allocate the streaming buffer pool.
// Buffer partitioning (USB/WiFi/SD/encoder circular buffers + sample pool)
// is the CALLER's responsibility and lives in SCPI_StartStreaming's inline
// setup (and the shared PrepareStreamingBuffers helper).  Code that pokes
// pStreamCfg->IsEnabled and calls Streaming_UpdateState() WITHOUT first
// partitioning (e.g. an ad-hoc benchmark) will run the encoder against
// stale/unsized buffers and produce 0 bytes — see #520.  If you reach for
// this to start a stream, make sure the pool has been partitioned for the
// active interface first.
void Streaming_UpdateState(void) {
    Streaming_Stop();
    Streaming_Start();
}

/**
 * @brief Handles streaming tasks by checking available data and writing it to active communication channels.
 * 
 * This function continuously monitors the availability of Analog and Digital I/O data and streams it 
 * over active communication channels (USB, WiFi, SD). It encodes data in the specified format and writes 
 * the output to all active channels based on available buffer sizes.
 * 
 * @param runtimeConfig Pointer to the runtime configuration of the board, including streaming settings.
 * @param boardData Pointer to the data structure that contains the board's input/output data.
 * 
 * @note This function will return early if streaming is disabled or there is no data to process.
 */

void streaming_Task(void) {
    // Enable FPU context saving for this task (required for ADC voltage conversion)
    portTASK_USES_FLOATING_POINT();

     TickType_t xBlockTime = portMAX_DELAY;
    NanopbFlagsArray nanopbFlag;
    size_t usbSize, wifiSize, sdSize, maxSize;
    /* #371/#372: USB/WiFi gates moved to ActiveInterface checks at the
     * write sites — hasUsb / hasWifi are no longer consulted, removed
     * from the per-iteration switch.  hasSD is still consulted by the SD
     * write block below. */
    bool hasSD = false;
    bool AINDataAvailable;
    bool DIODataAvailable;
    size_t packetSize=0;    
    tBoardData * pBoardData = BoardData_Get(
            BOARDDATA_ALL_DATA,
            0);
     StreamingRuntimeConfig * pRunTimeStreamConf = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);
    while(1) {
        ulTaskNotifyTake(pdFALSE, xBlockTime);
        DioProbe_Toggle(7);  /* probe 7: encoder task wake */

        // Don't process data or update QUES bits after streaming stops.
        // A notification may already be pending when Stop clears gQuesBits.
        if (!pRunTimeStreamConf->IsEnabled) {
            continue;
        }

        /* #486 — quiescence flag for cross-task sync against
         * SCPI_StartStreaming re-partition.  Set BEFORE any deref of the
         * encoder buffer pointer, sample queue (AInSampleList_IsEmpty
         * reads queue head/tail; AInSampleList_InitializeExternal calls
         * vQueueDelete + xQueueCreate so the queue handle itself is a
         * use-after-free target), DIO sample list, or output buffer
         * size accessors.
         *
         * The compiler memory barrier prevents -O3 from hoisting any
         * subsequent non-volatile read above the volatile flag store.
         * The volatile keyword alone orders the store with respect to
         * OTHER volatile accesses, not non-volatile reads like the
         * IsEmpty / WriteBuffFreeSize helpers below.
         *
         * Re-check IsEnabled inside the flag window to close the TOCTOU
         * between the line-1556 check above and this commit point — if
         * SCPI flipped IsEnabled between those two reads, we clear the
         * flag and skip this iteration so quiescence is observable. */
        gStreamingTaskInCritical = 1;
        __asm__ __volatile__ ("" ::: "memory");
        if (!pRunTimeStreamConf->IsEnabled) {
            goto iter_done;
        }

        // #397 self-heal check: if every configured transport has been
        // down for longer than the grace window, auto-stop the stream
        // and surface the reason via QUES bit 12 + LOG_E.  Individual
        // overflow bits (USB/WiFi/SD) still fire pre-grace for transient
        // drops — this only fires when nothing is consuming our data.
        if (Streaming_AllConfiguredTransportsDead(pRunTimeStreamConf)) {
            taskENTER_CRITICAL();
            gQuesBits |= QUES_BIT_TRANSPORT_DOWN;
            taskEXIT_CRITICAL();
            LOG_E("Streaming: all configured transports down >%u s — auto-stop",
                  (unsigned)gTransportGraceSec);
            pRunTimeStreamConf->IsEnabled = false;
            Streaming_Stop();
            goto iter_done;
        }

        // Encoder buffer must be set (from pool) before encoding
        if (buffer == NULL || bufferSize == 0) {
            goto iter_done;
        }

        AINDataAvailable = !AInSampleList_IsEmpty();
        DIODataAvailable = !DIOSampleList_IsEmpty(&pBoardData->DIOSamples);

        if (!AINDataAvailable && !DIODataAvailable) {
            goto iter_done;
        }

        usbSize = UsbCdc_WriteBuffFreeSize(NULL);
        wifiSize = wifi_manager_GetWriteBuffFreeSize();
        // Only check SD buffer size once the file is actually open.
        // The SD card manager clears the circular buffer during file open,
        // so writing before that would lose data (including headers).
        sdSize = sd_card_manager_IsWriteReady()
               ? sd_card_manager_GetWriteBuffFreeSize()
               : 0;

        // When SD file first becomes ready (new file or rotation), write
        // an SD-only header/metadata so each file is self-describing
        // without injecting duplicate headers into the USB/WiFi stream.
        if (sdSize > 0 && !gSdFileWasReady) {
            gSdFileWasReady = true;
            gSdPbMetadataSent = false;

            // Write SD-only header/metadata for each new file so every
            // file is self-describing and independently parseable.
            size_t sdHdrLen = 0;
            if (pRunTimeStreamConf->Encoding == Streaming_Csv) {
                // On rotation (header already sent to USB), generate
                // SD-only header.  First file: encoder flag is false,
                // so the encoder naturally includes the header for ALL
                // interfaces — no special handling needed.
                if (csv_IsHeaderSent()) {
                    sdHdrLen = csv_GenerateHeaderToBuffer(
                            (char*)buffer, bufferSize);
                }
            } else if (pRunTimeStreamConf->Encoding == Streaming_Json) {
                if (json_IsHeaderSent()) {
                    sdHdrLen = json_GenerateHeaderToBuffer(
                            (char*)buffer, bufferSize);
                }
            } else {
                // Protobuf: encode a standalone metadata message for SD
                tBoardData* pBoardData =
                    BoardData_Get(BOARDDATA_ALL_DATA, true);
                sdHdrLen = Nanopb_Encode(pBoardData,
                    &fields_sd_metadata, (uint8_t*)buffer, bufferSize);
                if (sdHdrLen > 0) {
                    gSdPbMetadataSent = true;
                }
            }
            if (sdHdrLen > 0) {
                size_t written = sd_card_manager_WriteToBuffer(
                        (const char*)buffer, sdHdrLen);
                if (written != sdHdrLen) {
                    LOG_E("SD: header write failed, expected=%u written=%u",
                          (unsigned)sdHdrLen, (unsigned)written);
                }
                memset(buffer, 0, sdHdrLen);
            }
        }

        // Compute hasSD for the SD write block below.  ActiveInterface
        // selects which network/USB output(s) are streamed to:
        //   - USB:        USB only       (+ SD if SD-logging is enabled — see override below)
        //   - WiFi:       WiFi only      (WiFi+SD unsupported; share SPI bus)
        //   - SD:         SD only
        //   - UsbAndSd:   USB + SD concurrent
        // USB and WiFi write blocks gate on ActiveInterface directly
        // (per #371 / #372 silent-loss fixes).  Only the SD write block
        // still consults a free-space flag, since its retry path needs
        // to distinguish "buffer too full right now" from "SD not active".
        // The "SD-logging enabled" override below can also enable SD
        // writes for ActiveInterface=USB or SD — only WiFi is excluded.
        switch (pRunTimeStreamConf->ActiveInterface) {
            case StreamingInterface_SD:
            case StreamingInterface_UsbAndSd:
                hasSD = (sdSize >= 128);
                break;
            default:
                hasSD = false;
                break;
        }

        // Override: If SD card logging is explicitly enabled, automatically enable SD output
        // Only allow USB+SD (not WiFi+SD, as they share the SPI bus)
        sd_card_manager_settings_t* pSDCardSettings =
            BoardRunTimeConfig_Get(BOARDRUNTIME_SD_CARD_SETTINGS);
        if (pSDCardSettings && pSDCardSettings->enable &&
            pSDCardSettings->mode == SD_CARD_MANAGER_MODE_WRITE) {
            // Only enable SD if we're not streaming to WiFi (SPI bus conflict)
            if (pRunTimeStreamConf->ActiveInterface != StreamingInterface_WiFi) {
                hasSD = (sdSize >= 128);
            }
        }

        // Log streaming start info once for debugging
        LOG_I_SESSION(LOG_SESSION_STREAM_STARTED,
            "Streaming started: interface=%d, usbSize=%u, wifiSize=%u, sdSize=%u",
            pRunTimeStreamConf->ActiveInterface, usbSize, wifiSize, sdSize);

        // CRITICAL: Always encode to drain the sample queue, even if no outputs have space
        // This prevents queue backup which causes the deferred interrupt task to drop samples
        // If no outputs available, sample will be encoded then discarded (better than queue backup)

        // Always encode at full buffer capacity. The encoder pops samples
        // from the queue regardless of output space — this prevents queue
        // backup and dropped samples. Encoded data is then written to
        // whichever outputs have space, and discarded if none do.
        // Previously used min(128, output_free) which caused CSV 16ch
        // encoder failures when USB buffer was full (128 < row size ~172).
        maxSize = bufferSize;

        nanopbFlag.Size = 0;

        nanopbFlag.Data[nanopbFlag.Size++] = DaqifiOutMessage_msg_time_stamp_tag;

        if (AINDataAvailable) {
            nanopbFlag.Data[nanopbFlag.Size++] = DaqifiOutMessage_analog_in_data_tag;
        }
        if (DIODataAvailable) {
            nanopbFlag.Data[nanopbFlag.Size++] = DaqifiOutMessage_digital_data_tag;
            nanopbFlag.Data[nanopbFlag.Size++] = DaqifiOutMessage_digital_port_dir_tag;
        }

        packetSize = 0;
        if (nanopbFlag.Size > 0) {
            DioProbe_PulseStart(8);  /* probe 8: encode duration */
            if(pRunTimeStreamConf->Encoding == Streaming_Csv){
                DIO_TIMING_TEST_WRITE_STATE(1);
                packetSize = csv_Encode(pBoardData, &nanopbFlag, (uint8_t *) buffer, maxSize);
                DIO_TIMING_TEST_WRITE_STATE(0);
            }
            else if (pRunTimeStreamConf->Encoding == Streaming_Json) {
                DIO_TIMING_TEST_WRITE_STATE(1);
                packetSize = Json_Encode(pBoardData, &nanopbFlag, (uint8_t *) buffer, maxSize);
                DIO_TIMING_TEST_WRITE_STATE(0);
            } else {
                DIO_TIMING_TEST_WRITE_STATE(1);
#if PB_PROFILE_COUNTERS
                uint32_t pbStart = _CP0_GET_COUNT();
                packetSize = Nanopb_EncodeStreamingFast(pBoardData, &nanopbFlag, (uint8_t *) buffer, maxSize);
                uint32_t pbCycles = _CP0_GET_COUNT() - pbStart;
                taskENTER_CRITICAL();
                gStreamStats.pbEncodeCycles += pbCycles;
                if (pbCycles > gStreamStats.pbEncodeMaxCycles) {
                    gStreamStats.pbEncodeMaxCycles = pbCycles;
                }
                if (packetSize > 0) {
                    gStreamStats.pbEncodeBytesOut += packetSize;
                }
                taskEXIT_CRITICAL();
#else
                packetSize = Nanopb_EncodeStreamingFast(pBoardData, &nanopbFlag, (uint8_t *) buffer, maxSize);
#endif
                DIO_TIMING_TEST_WRITE_STATE(0);
            }
            DioProbe_PulseEnd(8);
        }
        if (packetSize == 0 && nanopbFlag.Size > 0 && (AINDataAvailable || DIODataAvailable)) {
            // #484: shutdown race.  If Streaming_Stop ran between the
            // IsEnabled check at the top of this loop iteration and the
            // encoder invocation, the encoder runs against partially
            // torn-down state (pool freed, sample queue drained,
            // encoder buffer pointer about to be cleared) and returns
            // packetSize=0.  That's not a real encoder failure — the
            // operator asked us to stop.  Verified empirically (n=20,
            // 5 s vs 30 s test streams both fire EF at ~50 % regardless
            // of stream duration → fires at Stop, not mid-stream).
            // Skip the failure bookkeeping ONLY (don't `continue`) so any
            // post-encoder cleanup or future code below still runs.
            if (pRunTimeStreamConf->IsEnabled) {
                bool pastGrace = Streaming_PastStartupGrace();
                // Each encoder pops exactly 1 sample per call. On failure that
                // sample is freed back to the pool but its data is lost (#297).
                // Counting 1 per failure avoids the race condition where the
                // higher-priority deferred ISR task pushes new samples between
                // queue depth reads, making a delta approach inaccurate.
                // Count for both AIN and DIO encoder failures — any sample type
                // consumed by a failed encode is lost.
                // #483: also bump Steady subset when past the 3 s startup grace.
                // Single critical section covers all counters and the QUES bit
                // so a concurrent Streaming_GetStats snapshot sees them coherently.
                taskENTER_CRITICAL();
                gStreamStats.encoderFailures++;
                gStreamStats.encoderDroppedSamples++;
                if (pastGrace) {
                    gStreamStats.encoderFailuresSteady++;
                    gStreamStats.encoderDroppedSamplesSteady++;
                }
                gQuesBits |= QUES_BIT_ENCODER_FAIL;
                taskEXIT_CRITICAL();
                LOG_E_SESSION(LOG_SESSION_ENCODER_SAMPLE_LOSS,
                    "Streaming: encoder failure lost 1 sample");
                LOG_E_SESSION(LOG_SESSION_ENCODER_FAIL, "Streaming: Encoder failure detected");
            }
        }
        if (packetSize > 0) {
            taskENTER_CRITICAL();
            gStreamStats.totalBytesStreamed += packetSize;
            taskEXIT_CRITICAL();
        }
        DIO_TIMING_TEST_WRITE_STATE(1);
        if (packetSize > 0) {
            DioProbe_PulseStart(9);  /* probe 9: output write duration */
            // All-or-nothing output writes. On timeout (10s), the interface
            // is assumed dead. Backpressure propagates to sample queue —
            // PoolExhaustedSamples (deferred task can't allocate) and
            // QueueOverflowSamples (deferred task can't enqueue) are the only
            // input-side data loss mechanisms.  Their sum is QueueDroppedSamples.
            // USB/WiFi: all-or-nothing without retry. Full packet written
            // or cleanly dropped (no garbled partial data). No retry because
            // the ISR→encoder feedback loop causes sample queue overflow.
            // #372: ActiveInterface == USB / UsbAndSd means we should ALWAYS push
            // to USB (or count the drop).  Previously `hasUsb = (usbSize >= 128)`
            // made this per-iteration, silently skipping UsbCdc_WriteToBuffer
            // when the buffer was nearly full — bytes lost with usbDroppedBytes
            // never incremented.  Same bug class as #371 on the WiFi path.
            // Now we call UsbCdc_WriteToBuffer unconditionally when USB is in
            // the active set; its own pre-check returns 0 on no-space, and
            // we count that as a drop.
            if (pRunTimeStreamConf->ActiveInterface == StreamingInterface_USB) {
                // #520 backpressure (solo USB): block the encoder on a full USB
                // ring so the sample pool absorbs the burst (single drop point =
                // pool exhaustion), bounded by WriteWithRetry's 10 s
                // dead-interface timeout.  Same fix as the solo-WiFi path above.
                // UsbAndSd is intentionally left on the per-output no-retry drop
                // (below) — multi-output backpressure pacing is a separate
                // ticket (SD would pace USB).
                size_t usbWr = Streaming_WriteWithRetry(
                    Streaming_UsbWrite, buffer, packetSize);
                if (usbWr == STREAM_WRITE_RETURN_TIMEOUT) {
                    bool pastGrace = Streaming_PastStartupGrace();
                    taskENTER_CRITICAL();
                    gStreamStats.usbDroppedBytes += packetSize;
                    if (pastGrace) {
                        gStreamStats.usbDroppedBytesSteady += packetSize;
                    }
                    gQuesBits |= QUES_BIT_USB_OVERFLOW;
                    taskEXIT_CRITICAL();
                    LOG_E_SESSION(LOG_SESSION_USB_DROP, "Streaming: USB interface dead (10s timeout)");
                }
                // else: usbWr == packetSize (success) or STOPPED (stop-abort).
            } else if (pRunTimeStreamConf->ActiveInterface == StreamingInterface_UsbAndSd) {
                // Multi-output: keep per-output all-or-nothing drop (unchanged —
                // SD path below already backpressures via WriteWithRetry).
                if (Streaming_UsbWrite((const char*)buffer, packetSize) != packetSize) {
                    bool pastGrace = Streaming_PastStartupGrace();
                    // CLAUDE.md atomicity: 32-bit RMW (+=) is not atomic.
                    // Single critical section covers both counter bumps so
                    // a concurrent Streaming_GetStats snapshot sees the
                    // pair coherently (steady never > total).
                    taskENTER_CRITICAL();
                    gStreamStats.usbDroppedBytes += packetSize;
                    if (pastGrace) {
                        gStreamStats.usbDroppedBytesSteady += packetSize;
                    }
                    gQuesBits |= QUES_BIT_USB_OVERFLOW;
                    taskEXIT_CRITICAL();
                    LOG_E_SESSION(LOG_SESSION_USB_DROP, "Streaming: USB buffer overflow detected");
                }
            }
            // #371: ActiveInterface == WiFi means we should ALWAYS push to WiFi
            // (or count the drop).  Previously `hasWifi = (wifiSize >= 128)` made
            // this per-iteration, silently skipping WriteBuffer when buffer
            // was nearly full — losing 86% of bytes at saturation with
            // wifiDroppedBytes=0.  Now we call WriteBuffer unconditionally when
            // WiFi is the active interface; WriteBuffer's pre-check returns 0
            // on no-space, and we count that as a drop.
            if (pRunTimeStreamConf->ActiveInterface == StreamingInterface_WiFi) {
                if (packetSize >= 4) {
                    LOG_E_SESSION(LOG_SESSION_PACKETSIZE,
                        "diag367: encoder packetSize=%u first4=0x%02x%02x%02x%02x",
                        (unsigned)packetSize,
                        (unsigned)buffer[0], (unsigned)buffer[1],
                        (unsigned)buffer[2], (unsigned)buffer[3]);
                } else {
                    LOG_E_SESSION(LOG_SESSION_PACKETSIZE,
                        "diag367: encoder packetSize=%u (<4 bytes)",
                        (unsigned)packetSize);
                }
                // #520 backpressure (solo WiFi): BLOCK the encoder while the
                // WiFi ring is full (bounded retry) so the sample pool absorbs
                // the burst and the SINGLE drop point becomes pool exhaustion
                // (PoolExhausted/QueueOverflow in the deferred ISR task) — NOT a
                // ring drop with the pool sitting empty.  The old all-or-nothing
                // no-retry path dropped at the ring while the pool's ~3000-slot
                // absorption capacity sat idle (HW-confirmed 2026-05-31: pool 1%
                // while ring pegged + dropping 1.17 MB over 60 s).  Bounded by
                // WriteWithRetry's 10 s dead-interface timeout so a stuck WINC
                // (#383/#385/#491) drops at the ring rather than wedging the
                // stream forever.  WriteWithRetry also aborts (STOPPED) if
                // streaming was stopped mid-retry, so STR:START quiescence isn't
                // blocked.
                size_t wifiWr = Streaming_WriteWithRetry(
                    wifi_manager_WriteToBuffer, buffer, packetSize);
                if (wifiWr == STREAM_WRITE_RETURN_TIMEOUT) {
                    bool pastGrace = Streaming_PastStartupGrace();
                    taskENTER_CRITICAL();
                    gStreamStats.wifiDroppedBytes += packetSize;
                    if (pastGrace) {
                        gStreamStats.wifiDroppedBytesSteady += packetSize;
                    }
                    gQuesBits |= QUES_BIT_WIFI_OVERFLOW;
                    taskEXIT_CRITICAL();
                    LOG_E_SESSION(LOG_SESSION_WIFI_DROP, "Streaming: WiFi interface dead (10s timeout)");
                }
                // else: wifiWr == packetSize (success) or
                //       STREAM_WRITE_RETURN_STOPPED (stop-abort, no bookkeeping).
            }
            if (hasSD && gSdFileWasReady) {
                if (pRunTimeStreamConf->ActiveInterface != StreamingInterface_SD) {
                    /* #534: multi-output — a stalled SD must never block the
                     * (healthy) USB path through this shared encoder loop.
                     * Gate is "not solo-SD" rather than "== UsbAndSd" because
                     * the SD-logging override above (SD enabled while
                     * ActiveInterface == USB) also produces concurrent
                     * USB+SD writes without the UsbAndSd enum value (Qodo
                     * pass-1 catch on PR #536).
                     * Saturation is already shed by the per-iteration
                     * hasSD = (sdSize >= 128) gate above (HW-verified: USB
                     * holds 100% of target at 2x SD over-rate while SD
                     * drops).  The remaining hazard is a HARD-STALLED card
                     * with the circular buffer stuck at partial fill —
                     * free space >= 128 but nothing draining — where
                     * WriteWithRetry would block 10 s PER PACKET and
                     * throttle USB to ~0.  No-retry write-if-space-else-
                     * drop+count, mirroring the USB side of this branch.
                     * Solo-SD keeps the blocking backpressure below
                     * (#520: pool absorbs the burst; single drop point). */
                    if (sd_card_manager_WriteToBuffer((const char*)buffer,
                                                      packetSize) != packetSize) {
                        /* Skip the bookkeeping when streaming was stopped
                         * mid-iteration — mirrors WriteWithRetry's STOPPED
                         * path: a non-write during the stop transition (file
                         * closing, buffers being torn down) is not an SD
                         * fault and must not pollute the session stats. */
                        if (pRunTimeStreamConf->IsEnabled) {
                            Streaming_CountSdDrop(packetSize);
                            LOG_E_SESSION(LOG_SESSION_SD_DROP,
                                "Streaming: SD buffer overflow (multi-output no-retry)");
                        }
                    }
                } else {
                    size_t wr = Streaming_WriteWithRetry(
                        sd_card_manager_WriteToBuffer, buffer, packetSize);
                    if (wr == STREAM_WRITE_RETURN_TIMEOUT) {
                        /* True 10 s interface-dead timeout (pass-5 Qodo
                         * refinement): bump drop counters + QUES bit + log. */
                        Streaming_CountSdDrop(packetSize);
                        LOG_E_SESSION(LOG_SESSION_SD_DROP, "Streaming: SD interface dead (10s timeout)");
                    }
                    /* else: wr == packetSize (success) or
                     *       wr == STREAM_WRITE_RETURN_STOPPED (stop-abort,
                     *       intentional, no bookkeeping). */
                }
            }

            // Track packets discarded due to output buffer backpressure.
            // The streaming task always encodes to drain the sample queue
            // (preventing queueOverflowSamples from rising further), but
            // when an output buffer is too full (< 128 bytes free), the
            // encoded packet is silently discarded.  Count these drops so
            // SYST:STR:STATS? is accurate.
            {
                bool sdExpected = hasSD || (
                    pRunTimeStreamConf->ActiveInterface == StreamingInterface_SD ||
                    pRunTimeStreamConf->ActiveInterface == StreamingInterface_UsbAndSd ||
                    (pSDCardSettings && pSDCardSettings->enable &&
                     pSDCardSettings->mode == SD_CARD_MANAGER_MODE_WRITE &&
                     pRunTimeStreamConf->ActiveInterface != StreamingInterface_WiFi));
                bool sdWritten = hasSD && gSdFileWasReady;

                if (sdExpected && !sdWritten) {
                    // Gate on IsEnabled, mirroring the #484 encoder-failure
                    // guard: if Streaming_Stop ran between this iteration's
                    // top-of-loop check and here, the SD file is mid-close
                    // and the failed write is teardown, not data loss the
                    // operator should see counted (Qodo #536).
                    if (pRunTimeStreamConf->IsEnabled) {
                        Streaming_CountSdDrop(packetSize);
                        LOG_E_SESSION(LOG_SESSION_SD_DROP, "Streaming: SD output skipped (buffer full or file not ready)");
                    }
                }
            }
            DioProbe_PulseEnd(9);
        }
        DIO_TIMING_TEST_WRITE_STATE(0);

iter_done:
        /* #486: single-exit flag clear. All early `goto iter_done`
         * paths above land here; the normal end-of-iteration also
         * falls through. Guarantees the quiescence flag is cleared
         * on every loop iteration without scattering per-continue
         * clears that are easy to miss on future edits.
         *
         * Exit barrier (pass-4 Qodo importance-9): pairs with the
         * entry barrier at line 1594.  Without it, the compiler at
         * -O3 could sink the final encoder / buffer / output write
         * past the volatile flag store, defeating the protection
         * on the exit side. */
        __asm__ __volatile__ ("" ::: "memory");
        gStreamingTaskInCritical = 0;
    }
}

void TimestampTimer_Init(void) {
    //     Initialize and start timestamp timer
    //     This is a free running timer used for reference -
    //     this doesn't interrupt or callback

    // Stack sizes profiled under stress. See issue #230.
    if (gStreamingTaskHandle == NULL) {
        BaseType_t result = xTaskCreate((TaskFunction_t) streaming_Task,
                "Stream task",
                1392, NULL, 6, &gStreamingTaskHandle);  // Profiled: 692 words peak. 2x margin. (was 4096)
        if (result != pdPASS) {
            LOG_E("FATAL: Failed to create streaming_Task\r\n");
            while(1);
        }
    }
    if (gStreamingInterruptHandle == NULL) {
        BaseType_t result = xTaskCreate((TaskFunction_t) _Streaming_Deferred_Interrupt_Task,
                "Stream Interrupt",
                512, NULL, 9, &gStreamingInterruptHandle);  // Profiled: 214 words peak + FPU. 2x margin. (was 4096)
        if (result != pdPASS) {
            LOG_E("FATAL: Failed to create _Streaming_Deferred_Interrupt_Task\r\n");
            while(1);
        }
    }
    
    TimerApi_Stop(gpStreamingConfig->TSTimerIndex);
    TimerApi_Initialize(gpStreamingConfig->TSTimerIndex);
    TimerApi_InterruptDisable(gpStreamingConfig->TSTimerIndex);
    TimerApi_CallbackRegister(gpStreamingConfig->TSTimerIndex, TSTimerCB, 0);
    TimerApi_PeriodSet(gpStreamingConfig->TSTimerIndex, gpRuntimeConfigStream->TSClockPeriod);
    TimerApi_InterruptEnable(gpStreamingConfig->TSTimerIndex);
    TimerApi_Start(gpStreamingConfig->TSTimerIndex);

}

void Streaming_SetTestPattern(uint32_t pattern) {
    gTestPattern = pattern;  // 32-bit write is atomic on PIC32MZ
}

uint32_t Streaming_GetTestPattern(void) {
    return gTestPattern;  // 32-bit read is atomic on PIC32MZ
}

void Streaming_SetBenchmarkMode(uint32_t mode) {
    gBenchmarkMode = mode;
    // Pipeline mode requires test patterns (no real ADC data)
    if (mode == BENCHMARK_PIPELINE && gTestPattern == 0) {
        gTestPattern = 2;  // Force midscale
    }
}

uint32_t Streaming_GetBenchmarkMode(void) {
    return gBenchmarkMode;
}

// #331: used by the WINC idle-gate in tasks.c to pace the WINC driver's
// hot loop when we know WiFi isn't on the streaming data path. Both
// fields are plain bools / enums updated on the SCPI task — readers
// only see one-cycle-stale values in the worst case, which is fine
// because the worst cost of staleness here is "we poll WINC at the
// wrong cadence for one iteration of its loop."
//
bool Streaming_IsActiveOnNonWifiInterface(void) {
    // volatile-qualified view ensures the compiler re-reads each field
    // even with -O3 / LTO inlining the caller (the WINC task loop).
    // Without this, the read could be hoisted out of the loop entirely.
    const volatile StreamingRuntimeConfig* cfg =
        (const volatile StreamingRuntimeConfig*)gpRuntimeConfigStream;
    if (cfg == NULL) return false;
    if (!cfg->IsEnabled) return false;
    StreamingInterface iface = cfg->ActiveInterface;
    return (iface == StreamingInterface_USB ||
            iface == StreamingInterface_SD  ||
            iface == StreamingInterface_UsbAndSd);
}

// #29: complement of the above — true only when a streaming session is
// enabled and its active interface is WiFi.  Read by the WiFi power-save
// policy (app_WifiTask) to keep the WINC at full power while the WiFi data
// path is carrying a stream.  Same volatile-view / staleness reasoning as
// Streaming_IsActiveOnNonWifiInterface().
bool Streaming_IsActiveOnWifiInterface(void) {
    const volatile StreamingRuntimeConfig* cfg =
        (const volatile StreamingRuntimeConfig*)gpRuntimeConfigStream;
    if (cfg == NULL) return false;
    if (!cfg->IsEnabled) return false;
    return (cfg->ActiveInterface == StreamingInterface_WiFi);
}

