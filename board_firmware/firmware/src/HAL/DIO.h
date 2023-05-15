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
#include "state/board/BoardConfig.h"

#ifdef	__cplusplus
extern "C" {
#endif
    
/*!
 * Performs board initialization
 * @param pInitBoardConfiguration Board configuration data structure
 * @param[in] pInitBoardRuntmeConfig Board runtime configuration data
 * structure
 */
bool DIO_InitHardware( const tBoardConfig *pInitBoardConfiguration,         \
                        const tBoardRuntimeConfig *pInitBoardRuntimeConfig );

/*!
 * Sets the initial state for all DIO channelss
 */
bool DIO_WriteStateAll( void );
    
/*!
 * Updates the state for a single DIO channel
 * @param[in] Data channel index
 */
bool DIO_WriteStateSingle( uint8_t dataIndex );
    
/*!
 * Generates a sample based all enabled samples included in the mask
 * @param sample The sample to populate
 * @param mask Defines the channels that will be included
 */
bool DIO_ReadSampleByMask(DIOSample* sample, uint32_t mask);
    
/*!
 * Performs periodic tasks for DIO
 * @param latest Storage for the latest values
 * @param streamingSamples Storage for the latest streaming values
 */
void DIO_Tasks( DIOSample* latest, DIOSampleList* streamingSamples );
    
#ifdef	__cplusplus
}
#endif

