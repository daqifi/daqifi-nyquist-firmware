#pragma once

#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "state/data/BoardData.h"
#include "encoder.h"
#include "json/JSON_Encoder.h"

#ifdef	__cplusplus
extern "C" {
#endif

    //! Buffer size used for streaming purposes
    #define STREAMING_BUFFER_SIZE               ENCODER_BUFFER_SIZE

    /**
     * Initializes the streaming component
     */
    void Streaming_Init(const StreamingConfig* config, StreamingRuntimeConfig* runtimeConfig);
    
    /**
     * Starts the streaming timer
     * @param config The hardware configuration
     * @param runtimeConfig The runtime configurations
     */
    void Streaming_Start(const StreamingConfig* config, StreamingRuntimeConfig* runtimeConfig);
    
    /**
     * Stops the streaming timer
     * @param config The haardware configuration
     * @param runtimeConfig The runtime configuration
     */
    void Streaming_Stop(const StreamingConfig* config, StreamingRuntimeConfig* runtimeConfig);
    
    /**
     * Updates the streaming timer 
     * @param boardConfig The haardware configuration
     * @param runtimeConfig The runtime configuration
     */
    void Streaming_UpdateState(const BoardConfig* boardConfig, BoardRuntimeConfig* runtimeConfig);
    
    /**
     * Called to write streaming data to the underlying tasks
     * @param boardConfig The haardware configuration
     * @param runtimeConfig The runtime configuration
     * @param boardData The board data
     */
    void Streaming_Tasks(const BoardConfig* boardConfig, BoardRuntimeConfig* runtimeConfig, BoardData* boardData);
    
    /**
     * Initializes and starts the timestamp timer
     * @param config The hardware configuration
     * @param runtimeConfig The runtime configurations
     */
    void TimestampTimer_Init(const StreamingConfig* config, StreamingRuntimeConfig* runtimeConfig);
    
#ifdef	__cplusplus
}
#endif

