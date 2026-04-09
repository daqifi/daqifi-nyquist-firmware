#pragma once

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

#define STREAMING_ISR_MAX_HZ        11000
#define STREAMING_TYPE1_AGG_MAX_HZ  30000
#define STREAMING_TICK_BUDGET       77000
#define STREAMING_TICK_OVERHEAD     6

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

// Streaming loss/throughput statistics, accumulated per session.
// 32-bit fields are atomic on PIC32MZ; 64-bit fields require critical sections.
// Use Streaming_GetStats() for an atomic snapshot of all fields.
typedef struct {
    uint32_t queueDroppedSamples;   // Pool exhaustion or queue full (deferred ISR task)
    uint32_t usbDroppedBytes;       // USB circular buffer full
    uint32_t wifiDroppedBytes;      // WiFi circular buffer full
    uint32_t sdDroppedBytes;        // SD write timeout/partial
    uint32_t encoderFailures;       // Encoder returned 0 with data available
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
} StreamingStats;

// Copies stats into *out inside a critical section (atomic snapshot)
void Streaming_GetStats(StreamingStats* out);
void Streaming_ClearStats(void);

/**
 * Returns current SCPI STATus:QUEStionable condition bits for streaming health.
 * Bit 4 = windowed sample loss >= threshold, Bit 8 = USB overflow,
 * Bit 9 = WiFi overflow, Bit 10 = SD overflow, Bit 11 = encoder failure.
 * Definitions match QUES_* constants in SCPIInterface.c.
 * Called by SCPI_SyncQuesBits() in SCPIInterface.c before register queries.
 * Bits are cleared automatically when streaming stops.
 */
uint32_t Streaming_GetQuesBits(void);

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

