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
 * @param[in]  config        Streaming configuration
 * @param[out] runtimeConfig Streaming configuration in runtime
 */
void Streaming_Init(    const tStreamingConfig* config,                      \
                        StreamingRuntimeConfig* runtimeConfig);

/*! Updates the streaming timer 
 * @param[in] boardConfig       Board configuration, includes board type
 * @param[in/out] runtimeConfig The runtime configuration
 */
void Streaming_UpdateState(                                                 \
                        const tBoardConfig* boardConfig,                    \
                        tBoardRuntimeConfig* runtimeConfig );

/*!
 * Called to write streaming data to the underlying tasks
 * @param boardConfig   The hardware configuration
 * @param runtimeConfig The runtime configuration
 * @param boardData     The board data
 */
void Streaming_Tasks(   const tBoardConfig* boardConfig,                    \
                        tBoardRuntimeConfig* runtimeConfig,                 \
                        tBoardData* boardData);

/**
 * Initializes and starts the timestamp timer
 * @param config The hardware configuration
 * @param runtimeConfig The runtime configurations
 */
void TimestampTimer_Init(const tStreamingConfig* config, StreamingRuntimeConfig* runtimeConfig);

#ifdef	__cplusplus
}
#endif

