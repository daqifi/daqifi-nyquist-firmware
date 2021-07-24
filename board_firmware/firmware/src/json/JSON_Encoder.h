#pragma once

#include "Util/ArrayWrapper.h"
#include "state/data/BoardData.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "encoder.h"

#ifdef	__cplusplus
extern "C" {
#endif

//! Buffer size used for streaming purposes
#define JSON_ENCODER_BUFFER_SIZE                    ENCODER_BUFFER_SIZE
/**
 * Encodes board data as JSON
 * @return Number of bytes written
 */
size_t Json_Encode(BoardData* state, NanopbFlagsArray* fields, uint8_t** ppBuffer);

#ifdef	__cplusplus
}
#endif


