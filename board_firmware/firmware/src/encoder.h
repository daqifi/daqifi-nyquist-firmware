#pragma once

#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "state/data/BoardData.h"
#include "json/JSON_Encoder.h"

#ifdef	__cplusplus
extern "C" {
#endif

//! Buffer size
#define ENCODER_BUFFER_SIZE 2048
    
/*! This function is used for getting the enconding
 * @return Pointer to the encoding buffer
 */
uint8_t *Encoder_Get_Buffer( void );
    
#ifdef	__cplusplus
}
#endif

