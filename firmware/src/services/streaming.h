#pragma once

#include "../state/board/BoardConfig.h"
#include "../state/runtime/BoardRuntimeConfig.h"
#include "../state/data/BoardData.h"


#ifdef	__cplusplus
extern "C" {
#endif

//! Buffer size used for streaming purposes
#define STREAMING_BUFFER_SIZE               ENCODER_BUFFER_SIZE
    
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
    uint32_t queueDroppedSamples;   // Sample queue full (deferred ISR task)
    uint32_t usbDroppedBytes;       // USB circular buffer full
    uint32_t wifiDroppedBytes;      // WiFi circular buffer full
    uint32_t sdDroppedBytes;        // SD write timeout/partial
    uint32_t encoderFailures;       // Encoder returned 0 with data available
    uint64_t totalSamplesStreamed;   // Samples successfully queued (64-bit for week-long sessions)
    uint64_t totalBytesStreamed;     // Total bytes encoded (64-bit for week-long sessions)
    uint32_t windowLossPercent;     // Windowed sample loss percentage (0-100)
} StreamingStats;

// Copies stats into *out inside a critical section (atomic snapshot)
void Streaming_GetStats(StreamingStats* out);
void Streaming_ClearStats(void);

/**
 * Returns current SCPI STATus:QUEStionable condition bits for streaming health.
 * Bit 4 = windowed sample loss >= 5%, Bit 8 = USB overflow,
 * Bit 9 = WiFi overflow, Bit 10 = SD overflow, Bit 11 = encoder failure.
 * Definitions match QUES_* constants in SCPIInterface.c.
 * Called by SCPI_SyncQuesBits() in SCPIInterface.c before register queries.
 * Bits are cleared automatically when streaming stops.
 */
uint32_t Streaming_GetQuesBits(void);

#ifdef	__cplusplus
}
#endif

