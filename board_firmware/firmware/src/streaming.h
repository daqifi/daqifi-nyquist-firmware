#pragma once

#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "state/data/BoardData.h"

#ifdef	__cplusplus
extern "C" {
#endif
    /**
     * Initializes the streaming component
     */
    void Streaming_Init(const tStreamingConfig* config, StreamingRuntimeConfig* runtimeConfig);
    
    /**
     * Starts the streaming timer
     * @param config The hardware configuration
     * @param runtimeConfig The runtime configurations
     */
    void Streaming_Start(const tStreamingConfig* config, StreamingRuntimeConfig* runtimeConfig);
    
    /**
     * Stops the streaming timer
     * @param config The haardware configuration
     * @param runtimeConfig The runtime configuration
     */
    void Streaming_Stop(const tStreamingConfig* config, StreamingRuntimeConfig* runtimeConfig);
    
    /**
     * Updates the streaming timer 
     * @param boardConfig The haardware configuration
     * @param runtimeConfig The runtime configuration
     */
    void Streaming_UpdateState(const tBoardConfig* boardConfig, BoardRuntimeConfig* runtimeConfig);
    
    /**
     * Called to write streaming data to the underlying tasks
     * @param boardConfig The haardware configuration
     * @param runtimeConfig The runtime configuration
     * @param boardData The board data
     */
    void Streaming_Tasks(const tBoardConfig* boardConfig, BoardRuntimeConfig* runtimeConfig, tBoardData* boardData);
    
    /**
     * Initializes and starts the timestamp timer
     * @param config The hardware configuration
     * @param runtimeConfig The runtime configurations
     */
    void TimestampTimer_Init(const tStreamingConfig* config, StreamingRuntimeConfig* runtimeConfig);
    
#ifdef	__cplusplus
}
#endif

