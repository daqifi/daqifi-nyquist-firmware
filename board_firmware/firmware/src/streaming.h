#pragma once

#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "state/data/BoardData.h"

#ifdef	__cplusplus
extern "C" {
#endif

//! Buffer size used for streaming purposes
#define STREAMING_BUFFER_SIZE               ENCODER_BUFFER_SIZE
    
/*! Initializes the streaming component
 * @param[in] pStreamingConfigInit Streaming configuration
 * @param[out] pStreamingRuntimeConfigInit Streaming configuration in runtime
 */
void Streaming_Init(const tStreamingConfig* pStreamingConfigInit,           \
                    const StreamingRuntimeConfig* pStreamingRuntimeConfigInit);

/*! Updates the streaming timer 
 */
void Streaming_UpdateState( void );

/*!
 * Called to write streaming data to the underlying tasks
 * @param runtimeConfig The runtime configuration
 * @param boardData     The board data
 */
void Streaming_Tasks(   tBoardRuntimeConfig* runtimeConfig,                 \
                        tBoardData* boardData);

/**
 * Initializes and starts the timestamp timer
 */
void TimestampTimer_Init( void );

#ifdef	__cplusplus
}
#endif

