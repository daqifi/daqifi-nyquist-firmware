#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "../state/board/BoardConfig.h"
#include "../state/runtime/BoardRuntimeConfig.h"
#include "../state/data/BoardData.h"
#include "../state/data/AInSample.h"


#ifdef	__cplusplus
extern "C" {
#endif

//! Buffer size used for streaming purposes
#define STREAMING_BUFFER_SIZE               ENCODER_BUFFER_SIZE

// Streaming frequency limits (validated via benchmark testing, see issue #215)
//
// Three independent constraints limit the maximum streaming frequency:
//
// 1. ISR ceiling: hard limit on timer interrupt rate regardless of channel count.
//    Fixed per-invocation cost (context switch, task notify, pool alloc, queue push).
//    Benchmark: 1ch@11kHz PASS, 1ch@12kHz FAIL.
//
// 2. Type 1 aggregate: dedicated ADC modules convert simultaneously, but the
//    deferred task channel loop + encoder cost scales with Type 1 count.
//    Benchmark: 5ch@6kHz PASS (all Type 1), 5ch@6.2kHz FAIL.
//
// 3. Per-tick budget: every ISR tick iterates ALL enabled channels (sample copy,
//    test pattern, encode). More channels = more work per tick = lower max freq.
//    Benchmark: 16ch@3.5kHz PASS, 16ch@3.75kHz FAIL.
//
// Effective limit: min(ISR_MAX, TYPE1_AGG / type1Count, BUDGET / (OVERHEAD + total))
// WiFi SPI DMA staging buffer limits
#define WIFI_DMA_MAX  (32U * 1024U)  // 32KB max (benchmarked: fixes CSV 8ch/16ch drops)
#define WIFI_DMA_MIN  (2U * 1024U)   // 2KB min (enough for WINC1500 control plane)

// Benchmark mode levels (extensible — add new values for future modes)
#define BENCHMARK_OFF      0   // Normal: freq cap active, real ADC
#define BENCHMARK_NOCAP    1   // Bypass freq cap, real ADC timing
#define BENCHMARK_PIPELINE 2   // Bypass freq cap + skip ADC entirely. Sample
                               // values are generated directly without touching
                               // ADC hardware (uses current test pattern; if
                               // pattern=0 then values will be 0). Timestamp
                               // comes from the streaming timer tick (same
                               // source as normal-mode AInSample timestamps),
                               // so output remains comparable across modes.

// Frequency cap constants — fitted to characterization data (2026-04-13).
// ISR batching (#277) reduced Type 1 overhead; refit with updated ceilings.
// ISR_MAX raised 13000->16000 (#524): the per-interface TRANSPORT caps
// (Streaming_TransportMaxFreq) now govern the real ceilings; ISR_MAX is the
// ADC/ISR safety ceiling and must sit above the highest single-channel transport
// cap (USB PB 1ch = 15000) so it does not override it.
#define STREAMING_ISR_MAX_HZ        16000
#define STREAMING_TYPE1_AGG_MAX_HZ  55000
#define STREAMING_TICK_BUDGET       110000
#define STREAMING_TICK_OVERHEAD     6

// Per-interface, per-format wire/storage transport caps live in
// Streaming_TransportMaxFreq below (#524), which superseded the earlier
// WiFi-only budget term (#520/#522) and generalized it to all interfaces.

// Type 2 (shared MODULE7 mux) channels are scanned sequentially via the analog
// multiplexer. The former fixed 1 kHz throttle (ChannelScanFreqDiv = freq/1000)
// and its up-front reject (#232) were REMOVED in #107: real-ADC characterization
// (18-pass overnight matrix, daqifi-python-test-suite benchmarks/107_*) showed the
// mux scan never overruns up to >=40 kHz at any channel count. ChannelScanFreqDiv
// is now always 1 (T2 scans every tick = real full-rate data), and T2 is bounded
// by the same per-interface/format transport cap as T1 (Streaming_TransportMaxFreq).

/**
 * Compute maximum safe streaming frequency for a given channel configuration.
 * Uses three-constraint model validated against empirical benchmark data.
 *
 * @param type1Count            Number of enabled Type 1 (dedicated ADC) channels
 * @param totalEnabledChannels  Total number of enabled public ADC channels
 * @return Maximum safe frequency in Hz
 */
static inline uint32_t Streaming_ComputeMaxFreq(uint32_t type1Count, uint32_t totalEnabledChannels) {
    uint32_t maxFreq = STREAMING_ISR_MAX_HZ;

    // Type 1 aggregate constraint
    if (type1Count > 0) {
        uint32_t type1Max = STREAMING_TYPE1_AGG_MAX_HZ / type1Count;
        if (type1Max < maxFreq) maxFreq = type1Max;
    }

    // Per-tick budget constraint (all enabled channels add per-tick cost)
    if (totalEnabledChannels > 0) {
        uint32_t tickMax = STREAMING_TICK_BUDGET / (STREAMING_TICK_OVERHEAD + totalEnabledChannels);
        if (tickMax < maxFreq) maxFreq = tickMax;
    }

    if (maxFreq == 0) maxFreq = 1;
    return maxFreq;
}

/**
 * Per-interface, per-format TRANSPORT wire-rate cap (Hz) — generalizes the
 * WiFi-only term (#520) to USB / SD / USB+SD (#524).  Fitted to the 3-run
 * real-ADC zero-loss characterization (matrix_524 run-1/2/3 + WiFi-v2,
 * conservative min across runs).  Form: single-channel special-cased + A/(B+n)
 * for n>=2 (the "F3" fit) — 1-channel (esp. CSV) sits far above the
 * multi-channel curve, which a single A/(B+n) cannot hug.  Every predicted cap
 * is <= the measured zero-loss ceiling at the tested channel counts (safe by
 * construction; tightness 86-100%).  This closes the prior format-blind hole
 * where high-channel CSV was capped well ABOVE its true ceiling (silent loss).
 * JSON is treated as CSV (text; slightly optimistic — refine if it matters).
 * Only meaningful for the ACTIVE interface; ComputeMaxFreqForConfig gates on it.
 *
 * @param interface      StreamingInterface (USB / WiFi / SD / UsbAndSd)
 * @param encoding       StreamingEncoding (PB vs CSV/JSON)
 * @param totalChannels  Total enabled public ADC channels
 * @return transport-limited max frequency in Hz
 */
static inline uint32_t Streaming_TransportMaxFreq(StreamingInterface interface,
                                                  StreamingEncoding encoding,
                                                  uint32_t totalChannels) {
    if (totalChannels == 0) return STREAMING_ISR_MAX_HZ;
    /* Explicit encoding handling (Qodo): unknown encodings cap at 1 Hz so a
     * future/garbage value can never over-cap. */
    uint32_t pb = 0u, json = 0u;  /* init pb defensively (Qodo pass-7); every
                                   * non-default case still assigns it explicitly */
    switch (encoding) {
        case Streaming_ProtoBuffer: pb = 1u; break;
        case Streaming_Csv:         pb = 0u; break;
        case Streaming_Json:        pb = 0u; json = 1u; break;  /* CSV coefficients, derated below */
        default:                    return 1u;
    }
    uint32_t single, A, B;
    switch (interface) {
        case StreamingInterface_USB:
            if (pb) { single = 15000u; A = 180000u; B = 10u; }
            else    { single = 15000u; A =  34000u; B =  1u; }
            break;
        case StreamingInterface_WiFi:
            /* Refit 2026-06-11 (take-5 walk-down soaks, T2-only,
             * atcap_20260611_045901.csv) — the first endurance basis with
             * the #537 fix in.  All earlier WiFi OBDiag=off soak bases
             * (2026-06-02 sweep, 2026-06-10 atcap) were measured with the
             * MODULE7 scan silently skipped (#537): frozen T2 data and no
             * EOS-task load, so the device sustained ~1.5x the honest
             * rate.  With the scan actually running, walk-down ceilings:
             *
             *   PB : 1ch 5175 · 3ch 4225 · 5ch 4000 · 8ch 3675 · 11ch 3400
             *        -> single 5175; A/(B+n) refit 210000->139000 (B=30):
             *        139000/(30+n) = 4212/3971/3658/3390 — under every
             *        measured cell within 1%.
             *   CSV: 1ch 4675 · 3ch 3050 · 5ch 2857 / 8ch 2000 / 11ch 1538
             *        AT CAP (zero-drop at the existing 20000/(2+n) curve).
             *        Keep the curve (validated at cap for n>=5 same-night);
             *        single 8000->4675; clamp low-n multi to the measured
             *        3-ch ceiling 3050 (the curve over-caps n=2..4, where
             *        higher Hz costs more per-tick overhead than the
             *        hyperbola predicts).
             *
             * Night-to-night link variation remains (the 06-10 basis held
             * its caps clean; tonight needed 0.66x) — static caps are
             * worst-observed-safe, runtime AIMD (#523) is the real fix. */
            if (pb) { single =  5175u; A = 139000u; B = 30u; }
            else    { single =  4675u; A =  20000u; B =  2u; }
            break;
        case StreamingInterface_SD:
            if (pb) { single =  9000u; A = 150000u; B = 15u; }
            else    { single =  7500u; A =  42000u; B = 12u; }
            break;
        case StreamingInterface_UsbAndSd:
            if (pb) { single =  8000u; A =  66000u; B =  6u; }
            else    { single =  8000u; A =  15000u; B =  0u; }
            break;
        default:
            return 1u;  /* unknown/corrupted interface -> fail-safe floor, never over-cap (Qodo) */
    }
    /* Widen the divide to 64-bit so a corrupted/huge channel count can never wrap
     * the denominator (Qodo pass-8 hardening). denom is always >= 2 in this branch
     * (totalChannels >= 2, B >= 0), so no div-by-zero guard is needed. */
    uint32_t hz = single;
    if (totalChannels != 1u) {
        uint64_t denom = (uint64_t)B + (uint64_t)totalChannels;
        hz = (uint32_t)((uint64_t)A / denom);
        /* WiFi CSV low-n clamp (2026-06-11 take-5): 20000/(2+n) is
         * validated AT CAP for n>=5 but over-caps n=2..4 — the measured
         * 3-ch soak ceiling is 3050 while the curve gives 4000.  Clamp
         * multi-channel WiFi CSV to that measured ceiling; transparent
         * for n>=5 where the curve is already below it. */
        if (interface == StreamingInterface_WiFi && !pb && hz > 3050u) {
            hz = 3050u;
        }
    }
    /* JSON emits ~2-3x CSV bytes/sample (object braces + per-sample field names),
     * so its true ceiling is below CSV's and it was not separately characterized.
     * Derate the CSV-based cap by half to stay conservative (never over-cap JSON)
     * until JSON is measured (#524 follow-up). */
    if (json) hz /= 2u;
    return (hz == 0u) ? 1u : hz;
}

/**
 * Max safe streaming frequency for the CURRENTLY configured interface + format
 * + enabled channels.  Reads ActiveInterface / Encoding from the streaming
 * runtime config and the enabled-channel counts, then returns
 * min(Streaming_ComputeMaxFreq(...), WiFi term when ActiveInterface==WiFi).
 * This is the single "what rate can this config stream?" entry point for the
 * START cap, the channel-enable recompute, and the WiFi finder.
 *
 * @return Max safe frequency in Hz (STREAMING_ISR_MAX_HZ when no channels are
 *         enabled, matching Streaming_ComputeMaxFreq(0,0))
 */
uint32_t Streaming_ComputeMaxFreqForConfig(void);

/**
 * Same as Streaming_ComputeMaxFreqForConfig() but for an explicitly-supplied
 * interface instead of the live ActiveInterface — lets the capabilities query
 * advertise the cap for a client's detected interface without mutating the
 * shared runtime config (#524). Encoding + channel counts still come from config.
 */
uint32_t Streaming_ComputeMaxFreqForConfigIface(StreamingInterface iface);

/**
 * Count enabled public ADC channels from current board + runtime config.
 * Used by SCPI_StartStreaming, the ADC channel-enable path, and the
 * capability rollup so they all agree on what counts toward the cap.
 *
 * @param[out] out_type1Count     Enabled MC12bADC ChannelType=1 (Type 1) channels
 * @param[out] out_totalPublic    Total enabled IsPublic channels (both ADC types)
 * @param[out] out_hasAD7609      true if any enabled IsPublic channel is AD7609
 *
 * Any out_* pointer may be NULL.
 */
void Streaming_CountActiveChannels(uint16_t* out_type1Count,
                                   uint16_t* out_totalPublic,
                                   bool* out_hasAD7609);

/*! Initializes the streaming component
 * @param[in] pStreamingConfigInit Streaming configuration
 * @param[out] pStreamingRuntimeConfigInit Streaming configuration in runtime
 */
void Streaming_Init(tStreamingConfig* pStreamingConfigInit,           
                    StreamingRuntimeConfig* pStreamingRuntimeConfigInit);

/*! Updates the streaming timer 
 */
void Streaming_UpdateState( void );

/*!
 * Called to write streaming data to the underlying tasks
 * @param runtimeConfig The runtime configuration
 * @param boardData     The board data
 */
void Streaming_Tasks(   StreamingRuntimeConfig* pStreamConfig, tBoardData* boardData);

/**
 * Initializes and starts the timestamp timer
 */
void TimestampTimer_Init( void );

/**
 * Resets the SD protobuf metadata flag so the next SD log file
 * gets a self-describing metadata header as its first message.
 */
void Streaming_ResetSdPbMetadata(void);

// #388 — Compile-time profiling counters for the PB streaming hot path.
// When enabled, instruments encoder + USB write paths with _CP0_GET_COUNT()
// cycle measurements.  Off by default in production: enable here for a
// characterization build, then `SYST:STR:STATS?` exposes the counters
// for the bench operator to read post-run.
//
// Cost when enabled: ~5 cycles per measurement point (TASK critical-section
// pair + 64-bit accumulate).  Six measurement points → ~30 cycles per sample
// at 16 kHz × 1 ch = 480 kHz of overhead = 0.5 % of a 200 MHz CPU.  Below
// our worst-case rate-cap headroom; safe to leave on for typical bench
// runs.  Strip by setting to 0 before shipping a production build.
//
// The gate macro + accumulator function declarations live in
// streaming_profile.h so consumers (UsbCdc.c) can pull in only that
// header instead of all of streaming.h's transitive includes.
#include "streaming_profile.h"

// Streaming loss/throughput statistics, accumulated per session.
// 32-bit fields are atomic on PIC32MZ; 64-bit fields require critical sections.
// Use Streaming_GetStats() for an atomic snapshot of all fields.
typedef struct {
    uint32_t queueDroppedSamples;   // Aggregate: poolExhaustedSamples + queueOverflowSamples
    // #499 split — two distinct mechanisms previously combined in queueDroppedSamples:
    //   poolExhaustedSamples: AInSampleList_AllocateFromPool() returned NULL
    //                         (no free slot — pool depth too shallow for rate)
    //   queueOverflowSamples: AInSampleList_PushBack() failed
    //                         (FreeRTOS queue full — streaming_Task not draining fast enough)
    // Sum equals queueDroppedSamples (kept for backward compat with existing parsers).
    uint32_t poolExhaustedSamples;
    uint32_t queueOverflowSamples;
    uint32_t usbDroppedBytes;       // USB circular buffer full (total — incl. startup transients)
    uint32_t wifiDroppedBytes;      // WiFi circular buffer full (total — incl. startup transients)
    uint32_t sdDroppedBytes;        // SD write timeout/partial (total — incl. startup transients)
    // #450 + follow-up: post-grace subsets of the above. Drops that
    // occur within the first `gLossGraceSec` of a session increment
    // only the Total above; drops after the grace expires also
    // increment these Steady counters. Steady == real-data-loss after
    // the pipeline stabilizes. Total − Steady == startup-window drops.
    uint32_t usbDroppedBytesSteady;
    uint32_t wifiDroppedBytesSteady;
    uint32_t sdDroppedBytesSteady;
    uint32_t encoderFailures;       // Encoder returned 0 with data available
    uint32_t encoderFailuresSteady;
    uint32_t encoderDroppedSamples; // AIn samples consumed by failed encode calls (#297)
    uint32_t encoderDroppedSamplesSteady;
    uint32_t dioDroppedSamples;     // DIO queue full — PushBack returned false (#296)
    uint32_t dioDroppedSamplesSteady;
    uint32_t queueDroppedSamplesSteady;  // post-grace subset of queueDroppedSamples
    uint32_t eosOverruns;      // EOS notifications coalesced (>1 per wake) (#295)
    uint64_t totalSamplesStreamed;   // Samples successfully queued (64-bit for week-long sessions)
    uint64_t totalBytesStreamed;     // Total bytes encoded (64-bit for week-long sessions)
    uint32_t windowLossPercent;     // Windowed sample loss percentage (0-100)
    // Timer ISR tracking (#265). Distinguishes "timer firing at requested rate"
    // from downstream bottlenecks (sample pool, encoder, output transport).
    // The invariant `timerISRCalls == totalSamplesStreamed + queueDroppedSamples`
    // should always hold during a session — every timer event becomes either
    // a successfully queued sample or a pool-exhaustion drop.
    //
    // 64-bit so it never wraps in practice — at the ~90 kHz hardware ceiling
    // it would take ~6 million years to overflow. Matches the other 64-bit
    // session counters (totalSamplesStreamed, totalBytesStreamed).
    //
    // Storage note: this field is populated by Streaming_GetStats() from a
    // separate `static volatile uint64_t gTimerISRCalls` global. The volatile
    // global is the actual ISR-modified storage; the StreamingStats field is
    // a snapshot copy taken inside taskENTER_CRITICAL (which makes the
    // non-atomic 64-bit read coherent by blocking the timer ISR).
    uint64_t timerISRCalls;          // Actual timer ISR entry count this session
    // #367 diagnostics — populated at Streaming_Stop() to reconcile the
    // accounting gap (TotalBytesStreamed vs WifiTcpBytesSent at saturation).
    uint32_t circularBufferEndBytes; // Bytes still in WiFi circular buffer at Stop
#if PB_PROFILE_COUNTERS
    // #388 PB streaming bottleneck instrumentation.  All cycle fields are
    // raw `_CP0_GET_COUNT()` differences (SYSCLK/2 = 100 MHz on PIC32MZ).
    // To convert: cycles / 100_000_000 → seconds.
    uint64_t pbEncodeCycles;        // Accumulated time inside the encoder call
    uint32_t pbEncodeMaxCycles;     // Worst-case per-call time
    uint64_t pbEncodeBytesOut;      // Bytes the encoder produced (post-Nanopb)
    uint64_t usbWriteBufCycles;     // Accumulated time inside UsbCdc_WriteToBuffer
                                    // (the encoder → circular-buffer copy)
    uint64_t usbDmaCopyCycles;      // Accumulated time inside CircularBuf_ProcessBytes
                                    // (the circular → DMA buffer copy)
    uint64_t usbDmaPendingCycles;   // Accumulated time per DMA transfer between
                                    // USB_DEVICE_CDC_Write() success and the
                                    // WRITE_COMPLETE event — i.e. wire-time per
                                    // packet from the host stack's perspective
    uint32_t usbDmaIdleCount;       // State-machine iterations skipped because
                                    // the prior DMA transfer was still pending —
                                    // the "bus idle window" between completion
                                    // and next transfer start
#endif
} StreamingStats;

// Copies stats into *out inside a critical section (atomic snapshot)
void Streaming_GetStats(StreamingStats* out);
void Streaming_ClearStats(void);

// #388 profile counter accumulator hooks are declared in
// streaming_profile.h, included near the top of this file.

// Increment DIO dropped sample counter (called from DIO_StreamingTrigger).
// 32-bit increment on PIC32MZ — single writer (deferred ISR task, pri 8).
void Streaming_IncrDioDropped(void);

// Increment EOS coalesce counter (called from MC12bADC_EosInterruptTask).
// @param missed Number of coalesced notifications (notifCount - 1).
void Streaming_IncrEosOverruns(uint32_t missed);

/**
 * Returns current SCPI STATus:QUEStionable condition bits for streaming health.
 * Bit 4 = windowed sample loss >= threshold, Bit 8 = USB overflow,
 * Bit 9 = WiFi overflow, Bit 10 = SD overflow, Bit 11 = encoder failure,
 * Bit 12 = all-transports-down auto-stop (#397).
 * Definitions match QUES_* constants in SCPIInterface.c.
 * Called by SCPI_SyncQuesBits() in SCPIInterface.c before register queries.
 * Bits are cleared automatically when streaming stops.
 */
uint32_t Streaming_GetQuesBits(void);

/**
 * #397 self-heal grace window — seconds an active transport may be
 * unhealthy before the streaming task counts it as "dead".  Default 60.
 * Range [5, 300].  Runtime-only (not NVM-persisted, reset on reboot).
 * Used by Streaming_AllConfiguredTransportsDead() inside streaming_Task.
 */
uint32_t Streaming_GetTransportGraceSec(void);
bool     Streaming_SetTransportGraceSec(uint32_t sec);

// #450 — startup-drop grace window in seconds (0..60, default 3).
// Drops before this window are counted only in *DroppedBytes totals;
// drops after also bump the *DroppedBytesSteady fields.  Setter takes
// effect immediately (matches LOSS:THREshold pattern) — the grace
// check reads gLossGraceSec live at every drop site.
uint32_t Streaming_GetLossGraceSec(void);
bool     Streaming_SetLossGraceSec(uint32_t sec);

/**
 * #486 — true when neither streaming task is currently inside the
 * region that dereferences pool / buffer / queue memory.  Polled by
 * SCPI_StartStreaming after Streaming_Stop and before the re-partition
 * of the unified pool, so the destructive operations (pool repartition,
 * encoder buffer swap, sample queue re-init, coherent pool reset) don't
 * race with an in-flight encode or sample-pool allocation.
 */
bool Streaming_TasksAreQuiescent(void);

/**
 * Compute optimal circular buffer sizes based on currently active interfaces.
 * Used by auto-balance at stream start and by SYST:MEM:AUTO SCPI command.
 *
 * @param[out] outUsbSize     Optimal USB circular buffer size (bytes)
 * @param[out] outWifiSize    Optimal WiFi circular buffer size (bytes)
 * @param[out] outSdSize      Optimal SD circular buffer size (bytes)
 * @param[out] outSdDmaSize    Optimal SD DMA write buffer size (bytes, coherent pool)
 * @param[out] outUsbDmaSize   Optimal USB DMA write buffer size (bytes, coherent pool)
 * @param[out] outWifiDmaSize  Optimal WiFi SPI staging buffer size (bytes, coherent pool)
 * @param[out] outEncoderSize  Optimal encoder buffer size (bytes)
 */
void Streaming_ComputeAutoBuffers(uint32_t* outUsbSize, uint32_t* outWifiSize,
                                   uint32_t* outSdSize, uint32_t* outSdDmaSize,
                                   uint32_t* outUsbDmaSize, uint32_t* outWifiDmaSize,
                                   uint32_t* outEncoderSize);

/**
 * Set the encoder buffer to pool-managed memory.
 * Must be called before streaming starts.
 */
void Streaming_SetEncoderBuffer(uint8_t* buf, uint32_t size);

// Flow window configuration (configurable via SCPI SYST:STR:LOSS commands).
// Loss threshold: percentage (1-100) that triggers QUES data loss bit (default 5).
uint32_t Streaming_GetLossThreshold(void);
void Streaming_SetLossThreshold(uint32_t pct);

// Flow window size override: 0 = auto (clamp(freq*2, 20, 10000)), >0 = explicit.
// Takes effect at next streaming start.
uint32_t Streaming_GetFlowWindowOverride(void);
void Streaming_SetFlowWindowOverride(uint32_t size);

// Test pattern streaming mode.
// 0=off (real ADC data), 1=counter, 2=midscale, 3=fullscale, 4=walking,
// 5=triangle, 6=sine. Runtime-only (not persisted to NVM).
// Counter resets each streaming session.
void Streaming_SetTestPattern(uint32_t pattern);
uint32_t Streaming_GetTestPattern(void);

// Benchmark mode: when enabled, the deferred ISR task generates test pattern
// samples as fast as possible (no timer wait), bypassing ADC timing.
// Benchmark modes isolate pipeline stages for bottleneck analysis:
//   0 = Normal (freq cap + real ADC)
//   1 = NoCap (bypass cap, real ADC timing)
//   2 = Pipeline (bypass cap + skip ADC, test pattern required)
void Streaming_SetBenchmarkMode(uint32_t mode);
uint32_t Streaming_GetBenchmarkMode(void);

/**
 * True when streaming is active AND the active interface does not use
 * WiFi. Used by the WINC idle-gate (#331) to decide when it is safe to
 * pace the WINC driver's task loop down. Safe to call from any context;
 * reads plain bools with no cross-task coordination needed.
 */
bool Streaming_IsActiveOnNonWifiInterface(void);

/**
 * Build channel mapping from current board config and runtime config.
 * Must be called before streaming starts (from SCPI_StartStreaming).
 * Stores mapping globally for ISR and encoder access.
 *
 * @param pBoardConfig     Board hardware configuration
 * @param pRuntimeChannels Runtime channel enable/disable state
 * @return Number of enabled public channels (mapping.count)
 */
uint8_t Streaming_BuildChannelMapping(const tBoardConfig* pBoardConfig,
                                       const AInRuntimeArray* pRuntimeChannels);

/**
 * Get the current channel mapping (built at stream start).
 * Valid only while streaming is active or after BuildChannelMapping.
 */
const AInChannelMapping* Streaming_GetChannelMapping(void);

#ifdef	__cplusplus
}
#endif

