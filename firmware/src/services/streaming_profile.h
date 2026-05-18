/*! @file streaming_profile.h
 *  @brief #388 — PB streaming profile counter gate + accumulator hooks.
 *
 *  Isolated from streaming.h so that UsbCdc.c can include it without
 *  pulling in the full StreamingStats / BoardData / HAL transitive
 *  dependency tree.  Owned by streaming.c; consumed by UsbCdc.c (the
 *  hot-path producer) and streaming.h (which #includes this for the
 *  PB_PROFILE_COUNTERS gate that controls StreamingStats fields).
 */
#ifndef STREAMING_PROFILE_H
#define STREAMING_PROFILE_H

#include <stdint.h>

#ifndef PB_PROFILE_COUNTERS
#define PB_PROFILE_COUNTERS 0
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
