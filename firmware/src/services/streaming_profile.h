/*! @file streaming_profile.h
 *  @brief Streaming profiling gates: #388 PB hot-path counters (+ their
 *  accumulator hooks) and the #251 deferred-task read-loop probe.
 *
 *  Isolated from streaming.h so that UsbCdc.c can include it without
 *  pulling in the full StreamingStats / BoardData / HAL transitive
 *  dependency tree.  Owned by streaming.c; consumed by UsbCdc.c (the
 *  hot-path producer) and streaming.h (which #includes this for the
 *  PB_PROFILE_COUNTERS and READ_LOOP_PROFILE gates that control
 *  StreamingStats fields).
 */
#ifndef STREAMING_PROFILE_H
#define STREAMING_PROFILE_H

#include <stdint.h>

#ifndef PB_PROFILE_COUNTERS
#define PB_PROFILE_COUNTERS 0
#endif

/* #251 — timing probe on the per-channel loop of
 * _Streaming_Deferred_Interrupt_Task. Two core-timer reads bracket the loop;
 * the per-tick loop time is accumulated (sum, count, max) into StreamingStats
 * and SYST:STR:STATS? reports it as ReadLoopMaxNs / ReadLoopMeanNs. It exists
 * to measure the ADC-side, per-channel term of the NQ1 cap, which nothing
 * isolated before (the cap model in streaming.h is a whole-pipeline fit).
 *
 * ON by default, unlike PB_PROFILE_COUNTERS. The two STATS fields are only
 * worth having if an ordinary build reports them: #251's regression test reads
 * them from the shipped firmware, and a gate that defaulted off would leave it
 * nothing to read. The cost is one critical section and a handful of integer
 * operations per tick, with no work per channel.
 *
 * Build with -DREAD_LOOP_PROFILE=0 to compile the probe, its three
 * StreamingStats fields and both STATS lines out entirely. That is the A/B
 * #251's acceptance calls for (probe under 2% of the tick at 16 channels), and
 * it is the fallback if that A/B fails: flip this default to 0 and ship it as a
 * characterization-build option, as PB_PROFILE_COUNTERS is. */
#ifndef READ_LOOP_PROFILE
#define READ_LOOP_PROFILE 1
#endif

#if PB_PROFILE_COUNTERS

// Task-context accumulators (callers on USB task, pri 7):
void Streaming_AddProfileSample_WriteBuf(uint32_t cycles);
void Streaming_AddProfileSample_DmaCopy(uint32_t cycles);
void Streaming_AddProfileSample_DmaIdle(void);

// ISR-context accumulator (caller is USB_DEVICE_CDC_EVENT_WRITE_COMPLETE):
void Streaming_AddProfileSample_DmaPending_FromISR(uint32_t cycles);

// Reset hook called from disconnect/reset/clear paths so a delayed
// WRITE_COMPLETE event after a transfer-in-flight is invalidated
// doesn't pollute the next session's accumulator.
void UsbCdc_Profile_ResetPendingStamp(void);

#endif

#endif /* STREAMING_PROFILE_H */
