/* 
 * File:   DIO.h
 * Author: Daniel
 * The DIO Driver using the new state functions
 */

#pragma once

#include "state/board/DIOConfig.h"
#include "state/runtime/DIORuntimeConfig.h"
#include "state/data/DIOSample.h"
#include "state/runtime/BoardRuntimeConfig.h"

#ifdef	__cplusplus
extern "C" {
#endif
    /**
     * Performs board initialization
     * @param boardConfig The hardware configuration
     */
    bool DIO_InitHardware(const DIOArray* boardConfig);

    /**
     * Sets the initial state for all DIO channelss
     */
    bool DIO_WriteStateAll(const DIOArray* boardConfig, DIORuntimeArray* runtimeConfig);
    
    /**
     * Updates the state for a single DIO channel
     */
    bool DIO_WriteStateSingle(const DIOConfig* boardConfig, DIORuntimeConfig* runtimeConfig);
    
    /**
     * Generates a sample based all enabled samples included in the mask
     * @param sample The sample to populate
     * @param boardConfig The configuration for the board
     * @param runtimeConfig The runtime configuration for the board
     * @param mask Defines the channels that will be included
     */
    bool DIO_ReadSampleByMask(DIOSample* sample, const DIOArray* boardConfig, DIORuntimeArray* runtimeConfig, uint32_t mask);
    
    /**
     * Performs periodic tasks for DIO
     * @param boardConfig The board config to use
     * @param runtimeConfig The runtime config to use
     * @param latest Storage for the latest values
     * @param streamingSamples Storage for the latest streaming values
     */
    void DIO_Tasks(const DIOArray* boardConfig, tBoardRuntimeConfig* runtimeConfig, DIOSample* latest, DIOSampleList* streamingSamples);
    
#ifdef	__cplusplus
}
#endif

