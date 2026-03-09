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

typedef struct {
    uint32_t queueDroppedSamples;   // Sample queue full (deferred ISR task)
    uint32_t usbDroppedBytes;       // USB circular buffer full
    uint32_t wifiDroppedBytes;      // WiFi circular buffer full
    uint32_t sdDroppedBytes;        // SD write timeout/partial
    uint32_t encoderFailures;       // Encoder returned 0 with data available
    uint32_t totalSamplesStreamed;   // Samples successfully queued
    uint32_t totalBytesStreamed;     // Total bytes encoded (offered to outputs)
} StreamingStats;

const StreamingStats* Streaming_GetStats(void);
void Streaming_ClearStats(void);

#ifdef	__cplusplus
}
#endif

