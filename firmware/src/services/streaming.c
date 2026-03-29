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

#include <math.h>
#include "HAL/ADC.h"
#include "HAL/DIO.h"
#include "JSON_Encoder.h"
#include "csv_encoder.h"
#include "DaqifiPB/DaqifiOutMessage.pb.h"
#include "DaqifiPB/NanoPB_Encoder.h"
#include "Util/Logger.h"
#include "Util/CircularBuffer.h"
#include "UsbCdc/UsbCdc.h"
#include "../HAL/TimerApi/TimerApi.h"
#include "HAL/ADC/MC12bADC.h"
#include "sd_card_services/sd_card_manager.h"
#include "wifi_services/wifi_tcp_server.h"
#include "state/runtime/BoardRuntimeConfig.h"

// --- Test pattern streaming mode ---
// When non-zero, overrides ADC sample values with synthetic data patterns.
// Runtime-only (not persisted to NVM). Set via SCPI SYST:STR:TESTpattern.
// 32-bit atomic read on PIC32MZ — no critical section needed for reads/writes.
static volatile uint32_t gTestPattern = 0;       // 0=off, 1-4=pattern type
static uint64_t gTestPatternSampleCount = 0;      // Monotonic counter, reset on start

// Benchmark mode: bypasses frequency cap for throughput testing.
// Uses uint32_t for guaranteed 32-bit atomic access on PIC32MZ.
static volatile uint32_t gBenchmarkMode = 0;

// Task priority constants
#define STREAMING_ISR_TASK_PRIORITY     8
#define STREAMING_BENCHMARK_PRIORITY    2

// Saved ISR task priority for restore after benchmark
static UBaseType_t gSavedIsrPriority = STREAMING_ISR_TASK_PRIORITY;

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
// Written by: deferred ISR task (queueDroppedSamples, totalSamplesStreamed)
//             streaming task (all other fields)
// Read by:    SCPI handler via Streaming_GetStats() atomic snapshot
static StreamingStats gStreamStats = {0};

// Log-once flags: each error condition logs once per session to avoid flooding
// the 64-message circular log buffer. All reset in Streaming_ClearStats().
static bool gLoggedPoolExhaust = false;
static bool gLoggedQueueOverflow = false;
static bool gLoggedUsbDrop = false;
static bool gLoggedWifiDrop = false;
static bool gLoggedSdDrop = false;
static bool gLoggedEncoderFail = false;
static bool gStreamingLoggedOnce = false;

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
static uint32_t gQuesBits = 0;

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

// Encode buffer must fit the largest single encode output for any interface.
// Previously used min() which limited all interfaces to the WiFi buffer size (1400).
// Using max() allows each interface to use its full capacity when active.
#define BUFFER_SIZE max(max(USBCDC_WBUFFER_SIZE, WIFI_WBUFFER_SIZE), SD_CARD_MANAGER_CONF_WBUFFER_SIZE)
uint8_t buffer[BUFFER_SIZE];

//! Pointer to the board configuration data structure to be set in 
//! initialization
//static tStreamingConfig *pConfigStream;
//! Pointer to the board runtime configuration data structure, to be 
//! set in initialization
static StreamingRuntimeConfig *gpRuntimeConfigStream;
static tStreamingConfig* gpStreamingConfig;
//! Indicate if handler is used 
static bool gInTimerHandler = false;
static TaskHandle_t gStreamingInterruptHandle;
static TaskHandle_t gStreamingTaskHandle;

// Sine wave period for test pattern 6: 256 samples per cycle.
// Computed at runtime using hardware FPU (PIC32MZ EF double-precision).
// Requires portTASK_USES_FLOATING_POINT() in the deferred interrupt task.
#define SINE_PERIOD 256
#define SINE_TWO_PI_OVER_PERIOD (6.283185307179586 / 256.0)

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
        case 6: {  // Sine: 256-sample period, computed via hardware FPU
            // Phase offset per channel: channel * 32 (45 degrees apart)
            uint32_t phase = (uint32_t)((sampleCount + (uint32_t)channel * 32) % SINE_PERIOD);
            // sin() returns [-1, +1]; map to [0, adcMax]
            double s = sin(phase * SINE_TWO_PI_OVER_PERIOD);
            return (uint32_t)((s + 1.0) * 0.5 * adcMax);
        }
        default:
            return 0;
    }
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
    // Enable FPU context saving — required for sin() in test pattern mode
    portTASK_USES_FLOATING_POINT();

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
    AInRuntimeArray* pAiRunTimeChannelConfig = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_AIN_CHANNELS);

    AInPublicSampleList_t *pPublicSampleList=NULL;
    AInSample *pAiSample;

    uint64_t ChannelScanFreqDivCount = 0;
    while (1) {
        ulTaskNotifyTake(pdFALSE, xBlockTime);

        if (pRunTimeStreamConf->IsEnabled) {
            // Use object pool instead of heap allocation (eliminates vPortFree overhead)
            // No heap check needed - pool uses pre-allocated static memory
            pPublicSampleList = AInSampleList_AllocateFromPool();
            if(pPublicSampleList==NULL) {
                gStreamStats.queueDroppedSamples++;
                if (!gLoggedPoolExhaust) {
                    gLoggedPoolExhaust = true;
                    LOG_E("Streaming: Sample pool exhausted");
                }
                Streaming_UpdateFlowWindow(true);
                // Still increment test pattern counter to stay in sync
                if (gTestPattern != 0) {
                    taskENTER_CRITICAL();
                    gTestPatternSampleCount++;
                    taskEXIT_CRITICAL();
                }
                continue;
            }
            for (i = 0; i < pAiRunTimeChannelConfig->Size; i++) {
                if (pAiRunTimeChannelConfig->Data[i].IsEnabled == 1
                        && AInChannel_IsPublic(&pBoardConfig->AInChannels.Data[i])) {
                    pAiSample = BoardData_Get(BOARDDATA_AIN_LATEST, i);
                    // Null check to prevent crash
                    if (pAiSample != NULL) {
                        // Use channel ID from BoardConfig (authoritative source) instead of sample data
                        pPublicSampleList->sampleElement[i].Channel=pBoardConfig->AInChannels.Data[i].DaqifiAdcChannelId;
                        // Copy entire sample atomically to prevent torn reads
                        // Task-only context - use efficient critical section
                        taskENTER_CRITICAL();
                        pPublicSampleList->sampleElement[i].Timestamp=pAiSample->Timestamp;
                        pPublicSampleList->sampleElement[i].Value=pAiSample->Value;
                        taskEXIT_CRITICAL();

                        pPublicSampleList->isSampleValid[i]=1;

                        // Test pattern override: replace ADC value with synthetic data
                        // adcMax = Resolution - 1 to match real ADC range (0..4095 for 12-bit)
                        if (gTestPattern != 0) {
                            uint32_t adcMax;
                            if (pBoardConfig->AInChannels.Data[i].Type == AIn_AD7609) {
                                adcMax = (uint32_t)pBoardConfig->AInModules.Data[1].Config.AD7609.Resolution - 1;
                            } else {
                                adcMax = (uint32_t)pBoardConfig->AInModules.Data[0].Config.MC12b.Resolution - 1;
                            }
                            pPublicSampleList->sampleElement[i].Value =
                                Streaming_GenerateTestValue(gTestPattern,
                                    pPublicSampleList->sampleElement[i].Channel,
                                    gTestPatternSampleCount, adcMax);
                        }
                    } else {
                        // Mark as invalid if sample data unavailable
                        pPublicSampleList->isSampleValid[i]=0;
                    }
                }
            }
            if(!AInSampleList_PushBack(pPublicSampleList)){//failed pushing to Q
                gStreamStats.queueDroppedSamples++;
                if (!gLoggedQueueOverflow) {
                    gLoggedQueueOverflow = true;
                    LOG_E("Streaming: Sample queue overflow detected");
                }
                AInSampleList_FreeToPool(pPublicSampleList);  // Use pool!
                Streaming_UpdateFlowWindow(true);
            } else {
                taskENTER_CRITICAL();
                gStreamStats.totalSamplesStreamed++;
                taskEXIT_CRITICAL();
                Streaming_UpdateFlowWindow(false);
            }

            if (pRunTimeStreamConf->ChannelScanFreqDiv == 1) {
                for (i = 0; i < pRunTimeAInModules->Size; ++i) {
                    ADC_TriggerConversion(&pBoardConfig->AInModules.Data[i], MC12B_ADC_TYPE_ALL);
                }
            } else if (pRunTimeStreamConf->ChannelScanFreqDiv != 0) {
                for (i = 0; i < pRunTimeAInModules->Size; ++i) {
                    ADC_TriggerConversion(&pBoardConfig->AInModules.Data[i], MC12B_ADC_TYPE_DEDICATED);
                }

                if (ChannelScanFreqDivCount >= pRunTimeStreamConf->ChannelScanFreqDiv) {
                    for (i = 0; i < pRunTimeAInModules->Size; ++i) {
                        ADC_TriggerConversion(&pBoardConfig->AInModules.Data[i], MC12B_ADC_TYPE_SHARED);
                    }
                    ChannelScanFreqDivCount = 0;
                }
                ChannelScanFreqDivCount++;
            }
            DIO_StreamingTrigger(&pBoardData->DIOLatest, &pBoardData->DIOSamples);

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
  
    uint32_t valueTMR = TimerApi_CounterGet(gpStreamingConfig->TSTimerIndex);
    BoardData_Set(BOARDDATA_STREAMING_TIMESTAMP, 0, (const void*) &valueTMR);
    if (gInTimerHandler) return;
    gInTimerHandler = true;
    Streaming_Defer_Interrupt();
    gInTimerHandler = false;

}

/*!
 * Starts the streaming timer
 */
static void Streaming_Start(void) {
    if (!gpRuntimeConfigStream->Running) {
        // Apply dynamic memory configuration if changed.
        // SCPI validates bounds, but clamp here too for defense-in-depth.
        if (gpRuntimeConfigStream->IsEnabled) {
            MemoryConfig* mc = BoardRunTimeConfig_Get(BOARDRUNTIME_MEMORY_CONFIG);

            // NOTE: USB and WiFi circular buffer resize is NOT done here.
            // The USB/WiFi tasks run concurrently at high priority and may be
            // actively reading from the circular buffer. Resizing here caused
            // use-after-free crashes (USB Code 43 descriptor failure).
            // Circular buffer resize must be done via SCPI commands while
            // streaming is stopped (the streaming guard enforces this).
            // Sample pool resize IS safe here because the deferred ISR task
            // and streaming task are both stopped (Running == false).

            // NOTE: Runtime pool resize is disabled in this PR.
            // AInSampleList_Destroy + re-Initialize crashes the device due to
            // an undiagnosed interaction between pool deallocation and the
            // USB CDC data path. The pool is allocated once at boot (700 samples)
            // and cannot be changed at runtime. SYST:MEM:SAMP:POOL sets the
            // config value but it only takes effect on reboot (via BoardData_Init).
            // TODO: Debug and fix in follow-up PR (issue #229).
            if (mc->samplePoolCount > 0 && mc->samplePoolCount != AInSampleList_PoolCapacity()) {
                LOG_I("Pool resize to %u deferred (runtime resize not yet supported)",
                      (unsigned)mc->samplePoolCount);
            }
        }

        // Clear any stale samples from previous streaming session
        AInPublicSampleList_t* pStale;
        while (AInSampleList_PopFront(&pStale)) {
            if (pStale != NULL) {
                AInSampleList_FreeToPool(pStale);
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
        }

        // Clear encoding buffer once to prevent stale data artifacts in SD files
        memset(buffer, 0, BUFFER_SIZE);

        gSdPbMetadataSent = false;
        gSdFileWasReady = false;

        TimerApi_Initialize(gpStreamingConfig->TimerIndex);
        TimerApi_PeriodSet(gpStreamingConfig->TimerIndex, gpRuntimeConfigStream->ClockPeriod);
        TimerApi_CallbackRegister(gpStreamingConfig->TimerIndex, Streaming_TimerHandler, 0);
        TimerApi_InterruptEnable(gpStreamingConfig->TimerIndex);
        TimerApi_Start(gpStreamingConfig->TimerIndex);
        gpRuntimeConfigStream->Running = 1;
    }
}

/*! 
 * Stops the streaming timer
 */
static void Streaming_Stop(void) {
    if (gpRuntimeConfigStream->Running) {
        TimerApi_Stop(gpStreamingConfig->TimerIndex);
        TimerApi_InterruptDisable(gpStreamingConfig->TimerIndex);
        gpRuntimeConfigStream->Running = false;

        // Log session summary if any data was lost
        bool hadDrops = gStreamStats.queueDroppedSamples > 0 ||
                        gStreamStats.usbDroppedBytes > 0 ||
                        gStreamStats.wifiDroppedBytes > 0 ||
                        gStreamStats.sdDroppedBytes > 0 ||
                        gStreamStats.encoderFailures > 0;
        // Clear QUES condition bits so they don't persist after streaming ends.
        // Stats remain available via SYST:STR:STATS? until next session.
        gQuesBits = 0;  // 32-bit write is atomic on PIC32MZ

        if (hadDrops) {
            uint64_t totalAttempted = gStreamStats.totalSamplesStreamed +
                                     gStreamStats.queueDroppedSamples;
            uint32_t lossPercent = totalAttempted > 0
                ? (uint32_t)((gStreamStats.queueDroppedSamples * 100ULL) / totalAttempted)
                : 0;
            LOG_E("Stream end: lost %u/%llu samples (%u%%), USB=%u WiFi=%u SD=%u bytes dropped, enc=%u",
                  (unsigned)gStreamStats.queueDroppedSamples,
                  (unsigned long long)totalAttempted,
                  (unsigned)lossPercent,
                  (unsigned)gStreamStats.usbDroppedBytes,
                  (unsigned)gStreamStats.wifiDroppedBytes,
                  (unsigned)gStreamStats.sdDroppedBytes,
                  (unsigned)gStreamStats.encoderFailures);
        }
    }
}

void Streaming_Init(tStreamingConfig* pStreamingConfigInit,
        StreamingRuntimeConfig* pStreamingRuntimeConfigInit) {
    gpStreamingConfig = pStreamingConfigInit;
    gpRuntimeConfigStream = pStreamingRuntimeConfigInit;
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
    taskEXIT_CRITICAL();
}

void Streaming_ClearStats(void) {
    memset((void*)&gStreamStats, 0, sizeof(gStreamStats));
    gLoggedPoolExhaust = false;
    gLoggedQueueOverflow = false;
    gLoggedUsbDrop = false;
    gLoggedWifiDrop = false;
    gLoggedSdDrop = false;
    gLoggedEncoderFail = false;
    // Intentionally reset: we want "Streaming started" logged once per session
    gStreamingLoggedOnce = false;
    // Reset windowed flow tracking
    memset(gFlowWindow, 0, sizeof(gFlowWindow));
    gFlowWindowCount = 0;
    gQuesBits = 0;  // 32-bit write is atomic on PIC32MZ
    // NOTE: Pool max-used is NOT reset here — it persists across sessions
    // so users can check peak usage after stopping. Reset via SYST:STR:ClearStats.
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
    gQuesBits = 0;  // 32-bit write is atomic on PIC32MZ
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
    bool hasUsb, hasWifi, hasSD;
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

        // Don't process data or update QUES bits after streaming stops.
        // A notification may already be pending when Stop clears gQuesBits.
        if (!pRunTimeStreamConf->IsEnabled) {
            continue;
        }

        AINDataAvailable = !AInSampleList_IsEmpty();
        DIODataAvailable = !DIOSampleList_IsEmpty(&pBoardData->DIOSamples);

        if (!AINDataAvailable && !DIODataAvailable) {
            continue;
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
                            (char*)buffer, BUFFER_SIZE);
                }
            } else if (pRunTimeStreamConf->Encoding == Streaming_Json) {
                if (json_IsHeaderSent()) {
                    sdHdrLen = json_GenerateHeaderToBuffer(
                            (char*)buffer, BUFFER_SIZE);
                }
            } else {
                // Protobuf: encode a standalone metadata message for SD
                tBoardData* pBoardData =
                    BoardData_Get(BOARDDATA_ALL_DATA, true);
                sdHdrLen = Nanopb_Encode(pBoardData,
                    &fields_sd_metadata, (uint8_t*)buffer, BUFFER_SIZE);
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

        // Single-interface streaming: only stream to the interface that initiated streaming
        // This prevents bandwidth overload at high sample rates
        switch (pRunTimeStreamConf->ActiveInterface) {
            case StreamingInterface_USB:
                hasUsb = (usbSize >= 128);
                hasWifi = false;
                hasSD = false;
                break;
            case StreamingInterface_WiFi:
                hasUsb = false;
                hasWifi = (wifiSize >= 128);
                hasSD = false;
                break;
            case StreamingInterface_SD:
                hasUsb = false;
                hasWifi = false;
                hasSD = (sdSize >= 128);
                break;
            case StreamingInterface_All:
            default:
                // Legacy mode: stream to all interfaces (may cause issues at high rates)
                hasUsb = (usbSize >= 128);
                hasWifi = (wifiSize >= 128);
                hasSD = (sdSize >= 128);
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
        if (!gStreamingLoggedOnce) {
            LOG_I("Streaming started: interface=%d, usbSize=%u, wifiSize=%u, sdSize=%u",
                  pRunTimeStreamConf->ActiveInterface, usbSize, wifiSize, sdSize);
            gStreamingLoggedOnce = true;
        }

        // CRITICAL: Always encode to drain the sample queue, even if no outputs have space
        // This prevents queue backup which causes the deferred interrupt task to drop samples
        // If no outputs available, sample will be encoded then discarded (better than queue backup)

        // Always encode at full buffer capacity. The encoder pops samples
        // from the queue regardless of output space — this prevents queue
        // backup and dropped samples. Encoded data is then written to
        // whichever outputs have space, and discarded if none do.
        // Previously used min(128, output_free) which caused CSV 16ch
        // encoder failures when USB buffer was full (128 < row size ~172).
        maxSize = BUFFER_SIZE;

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
                packetSize = Nanopb_EncodeStreamingFast(pBoardData, &nanopbFlag, (uint8_t *) buffer, maxSize);
                DIO_TIMING_TEST_WRITE_STATE(0);
            }
        }
        if (packetSize == 0 && nanopbFlag.Size > 0 && (AINDataAvailable || DIODataAvailable)) {
            gStreamStats.encoderFailures++;
            // Critical section: gQuesBits is also RMW'd by deferred ISR task
            taskENTER_CRITICAL();
            gQuesBits |= QUES_BIT_ENCODER_FAIL;
            taskEXIT_CRITICAL();
            if (!gLoggedEncoderFail) {
                gLoggedEncoderFail = true;
                LOG_E("Streaming: Encoder failure detected");
            }
        }
        if (packetSize > 0) {
            taskENTER_CRITICAL();
            gStreamStats.totalBytesStreamed += packetSize;
            taskEXIT_CRITICAL();
        }
        DIO_TIMING_TEST_WRITE_STATE(1);
        if (packetSize > 0) {
            if (hasUsb) {
                size_t written = UsbCdc_WriteToBuffer(NULL, (const char *) buffer, packetSize);
                if (written < packetSize) {
                    gStreamStats.usbDroppedBytes += (packetSize - written);
                    // Critical section: gQuesBits is also RMW'd by deferred ISR task
                    taskENTER_CRITICAL();
                    gQuesBits |= QUES_BIT_USB_OVERFLOW;
                    taskEXIT_CRITICAL();
                    if (!gLoggedUsbDrop) {
                        gLoggedUsbDrop = true;
                        LOG_E("Streaming: USB buffer overflow detected");
                    }
                }
            }
            if (hasWifi) {
                size_t written = wifi_manager_WriteToBuffer((const char *) buffer, packetSize);
                if (written < packetSize) {
                    gStreamStats.wifiDroppedBytes += (packetSize - written);
                    // Critical section: gQuesBits is also RMW'd by deferred ISR task
                    taskENTER_CRITICAL();
                    gQuesBits |= QUES_BIT_WIFI_OVERFLOW;
                    taskEXIT_CRITICAL();
                    if (!gLoggedWifiDrop) {
                        gLoggedWifiDrop = true;
                        LOG_E("Streaming: WiFi buffer overflow detected");
                    }
                }
            }
            if (hasSD && gSdFileWasReady) {
                // Guard: gSdFileWasReady is reset by the SD task during file
                // rotation (via Streaming_ResetSdPbMetadata).  If it went
                // false while we were encoding, a rotation happened and our
                // buffer belongs to the old file — writing it now would place
                // non-metadata data at position 0 of the NEW file.  Skipping
                // the write lets the next iteration detect the new file,
                // include metadata, and write at position 0.
                const uint8_t* p = buffer;
                size_t remaining = packetSize;
                size_t total_written = 0;
                unsigned int attempts = 0;
                const unsigned int max_attempts = 3;

                while (remaining > 0 && attempts < max_attempts) {
                    size_t w = sd_card_manager_WriteToBuffer((const char *)p, remaining);
                    if (w == 0) {
                        // No progress, break to avoid tight loop
                        break;
                    }
                    total_written += w;
                    p += w;
                    remaining -= w;
                    attempts++;
                    if (remaining > 0) {
                        // Partial write, yield to give SD task a chance
                        taskYIELD();
                    }
                }

                if (total_written != packetSize) {
                    gStreamStats.sdDroppedBytes += (packetSize - total_written);
                    // Critical section: gQuesBits is also RMW'd by deferred ISR task
                    taskENTER_CRITICAL();
                    gQuesBits |= QUES_BIT_SD_OVERFLOW;
                    taskEXIT_CRITICAL();
                    if (!gLoggedSdDrop) {
                        gLoggedSdDrop = true;
                        LOG_E("Streaming: SD write overflow detected");
                    }
                }

            }

            // Track packets discarded due to output buffer backpressure.
            // The streaming task always encodes to drain the sample queue
            // (preventing queueDroppedSamples), but when an output buffer
            // is too full (< 128 bytes free), the encoded packet is silently
            // discarded. Count these drops so SYST:STR:STATS? is accurate.
            {
                bool sdExpected = hasSD || (
                    pRunTimeStreamConf->ActiveInterface == StreamingInterface_SD ||
                    pRunTimeStreamConf->ActiveInterface == StreamingInterface_All ||
                    (pSDCardSettings && pSDCardSettings->enable &&
                     pSDCardSettings->mode == SD_CARD_MANAGER_MODE_WRITE &&
                     pRunTimeStreamConf->ActiveInterface != StreamingInterface_WiFi));
                bool sdWritten = hasSD && gSdFileWasReady;

                if (sdExpected && !sdWritten) {
                    gStreamStats.sdDroppedBytes += packetSize;
                    taskENTER_CRITICAL();
                    gQuesBits |= QUES_BIT_SD_OVERFLOW;
                    taskEXIT_CRITICAL();
                    if (!gLoggedSdDrop) {
                        gLoggedSdDrop = true;
                        LOG_E("Streaming: SD output skipped (buffer full or file not ready)");
                    }
                }
            }
        }
        DIO_TIMING_TEST_WRITE_STATE(0);
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
                1392, NULL, 2, &gStreamingTaskHandle);  // Profiled: 692 words peak. 2x margin. (was 4096)
        if (result != pdPASS) {
            LOG_E("FATAL: Failed to create streaming_Task\r\n");
            while(1);
        }
    }
    if (gStreamingInterruptHandle == NULL) {
        BaseType_t result = xTaskCreate((TaskFunction_t) _Streaming_Deferred_Interrupt_Task,
                "Stream Interrupt",
                512, NULL, 8, &gStreamingInterruptHandle);  // Profiled: 214 words peak + FPU. 2x margin. (was 4096)
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

void Streaming_SetBenchmarkMode(bool enabled) {
    // Serialize transitions — prevents race if called from USB and WiFi SCPI
    taskENTER_CRITICAL();
    if (enabled && gBenchmarkMode == 0) {
        if (gStreamingInterruptHandle != NULL) {
            gSavedIsrPriority = uxTaskPriorityGet(gStreamingInterruptHandle);
            vTaskPrioritySet(gStreamingInterruptHandle, STREAMING_BENCHMARK_PRIORITY);
        }
        gBenchmarkMode = 1;
    } else if (!enabled && gBenchmarkMode != 0) {
        gBenchmarkMode = 0;
        if (gStreamingInterruptHandle != NULL) {
            vTaskPrioritySet(gStreamingInterruptHandle, gSavedIsrPriority);
        }
    }
    taskEXIT_CRITICAL();
}

bool Streaming_GetBenchmarkMode(void) {
    return gBenchmarkMode != 0;
}

// Removed: Streaming_RunBenchmark — replaced by benchmark mode flag.
// The deferred ISR task checks gBenchmarkMode and skips ulTaskNotifyTake
// when enabled, generating samples as fast as the pipeline can consume them.
// This uses the existing streaming infrastructure (stats, pool, encoder,
// output buffers) without any special code paths.
#if 0  // Reference: old standalone benchmark function
    if (result == NULL || durationSec == 0 || durationSec > 60) return false;

    StreamingRuntimeConfig* pStreamCfg = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);
    if (pStreamCfg->Running || pStreamCfg->IsEnabled) {
        LOG_E("Benchmark rejected: streaming is active");
        return false;
    }

    tBoardData* pBoardData = BoardData_Get(BOARDDATA_ALL_DATA, 0);
    tBoardConfig* pBoardConfig = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    AInRuntimeArray* pAiChannels = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_AIN_CHANNELS);

    memset(result, 0, sizeof(StreamingBenchmarkResult));

    // Generate a fake sample with all enabled channels
    AInPublicSampleList_t fakeSample;
    memset(&fakeSample, 0, sizeof(fakeSample));
    uint32_t sampleCounter = 0;

    for (uint8_t i = 0; i < pAiChannels->Size; i++) {
        if (pAiChannels->Data[i].IsEnabled &&
            AInChannel_IsPublic(&pBoardConfig->AInChannels.Data[i])) {
            fakeSample.sampleElement[i].Channel =
                pBoardConfig->AInChannels.Data[i].DaqifiAdcChannelId;
            fakeSample.sampleElement[i].Timestamp = 0;
            fakeSample.sampleElement[i].Value = 2048;  // Midscale
            fakeSample.isSampleValid[i] = true;
        }
    }

    // Temporarily set encoding and interface for the benchmark
    StreamingEncoding savedEncoding = pStreamCfg->Encoding;
    StreamingInterface savedInterface = pStreamCfg->ActiveInterface;
    pStreamCfg->Encoding = (StreamingEncoding)encoding;
    pStreamCfg->ActiveInterface = (StreamingInterface)interface;

    // Setup flags for encoder
    NanopbFlagsArray nanopbFlag;
    nanopbFlag.Size = 0;
    nanopbFlag.Data[nanopbFlag.Size++] = DaqifiOutMessage_msg_time_stamp_tag;
    nanopbFlag.Data[nanopbFlag.Size++] = DaqifiOutMessage_analog_in_data_tag;

    // Init encoders (CSV/JSON need header state)
    if (encoding == Streaming_Csv) {
        csv_Init();
    } else if (encoding == Streaming_Json) {
        json_Init();
    }

    uint64_t totalBytes = 0;
    uint32_t totalSamples = 0;
    uint32_t outputDrops = 0;
    uint32_t encFails = 0;

    LOG_I("Benchmark: %us, interface=%u, encoding=%u",
          (unsigned)durationSec, (unsigned)interface, (unsigned)encoding);

    TickType_t startTick = xTaskGetTickCount();
    TickType_t endTick = startTick + pdMS_TO_TICKS(durationSec * 1000);

    // Tight loop: encode and output as fast as possible
    while (xTaskGetTickCount() < endTick) {
        // Update fake timestamp
        fakeSample.sampleElement[0].Timestamp = xTaskGetTickCount();
        sampleCounter++;

        // Push fake sample to the queue (so encoder can pop it)
        AInSampleList_PushBack(&fakeSample);

        // Check output buffer space
        size_t outSize = 0;
        switch (interface) {
            case 0: outSize = UsbCdc_WriteBuffFreeSize(NULL); break;
            case 1: outSize = wifi_manager_GetWriteBuffFreeSize(); break;
            case 2: outSize = sd_card_manager_IsWriteReady()
                             ? sd_card_manager_GetWriteBuffFreeSize() : 0; break;
        }

        if (outSize < 128) {
            // Output buffer full — yield to let it drain
            AInPublicSampleList_t* pDiscard;
            AInSampleList_PopFront(&pDiscard);  // Remove our sample
            vTaskDelay(1);
            continue;
        }

        // Encode
        size_t maxEncode = (outSize > BUFFER_SIZE) ? BUFFER_SIZE : outSize;
        size_t packetSize = 0;

        if (encoding == Streaming_Csv) {
            packetSize = csv_Encode(pBoardData, &nanopbFlag, (uint8_t*)buffer, maxEncode);
        } else if (encoding == Streaming_Json) {
            packetSize = Json_Encode(pBoardData, &nanopbFlag, (uint8_t*)buffer, maxEncode);
        } else {
            packetSize = Nanopb_EncodeStreamingFast(pBoardData, &nanopbFlag, (uint8_t*)buffer, maxEncode);
        }

        if (packetSize == 0) {
            encFails++;
            // Still need to pop the sample we pushed
            AInPublicSampleList_t* pDiscard;
            AInSampleList_PopFront(&pDiscard);
            continue;
        }

        totalBytes += packetSize;
        totalSamples++;

        // Write to output
        size_t written = 0;
        switch (interface) {
            case 0: written = UsbCdc_WriteToBuffer(NULL, (const char*)buffer, packetSize); break;
            case 1: written = wifi_tcp_server_WriteBuffer((const char*)buffer, packetSize); break;
            case 2: written = sd_card_manager_WriteToBuffer((const char*)buffer, packetSize); break;
        }

        if (written < packetSize) {
            outputDrops += (packetSize - written);
        }

        // Yield periodically to let output tasks drain
        if ((sampleCounter % 100) == 0) {
            vTaskDelay(1);
        }
    }

    uint32_t elapsed = pdTICKS_TO_MS(xTaskGetTickCount() - startTick);

    // Restore streaming config
    pStreamCfg->Encoding = savedEncoding;
    pStreamCfg->ActiveInterface = savedInterface;

    // Fill result
    result->durationMs = elapsed;
    result->totalBytes = totalBytes;
    result->totalSamples = totalSamples;
    result->bytesPerSec = (elapsed > 0) ? (uint32_t)((totalBytes * 1000ULL) / elapsed) : 0;
    result->samplesPerSec = (elapsed > 0) ? (totalSamples * 1000) / elapsed : 0;
    result->outputDrops = outputDrops;
    result->encoderFailures = encFails;

    LOG_I("Benchmark done: %u bytes in %ums = %u bytes/sec, %u samples/sec",
          (unsigned)totalBytes, (unsigned)elapsed,
          (unsigned)result->bytesPerSec, (unsigned)result->samplesPerSec);

    return true;
}
#endif  // Reference: old standalone benchmark function