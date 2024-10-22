

#pragma once

#include "Util/ArrayWrapper.h"
#include "state/data/BoardData.h"
#include "state/runtime/BoardRuntimeConfig.h"

#ifdef	__cplusplus
extern "C" {
#endif

/*!
 * Encodes board data as JSON
 * @return Number of bytes written 
 */
size_t Json_Encode(     tBoardData* state,                                  
                        NanopbFlagsArray* fields,                           
                        uint8_t* pBffer, size_t buffSize);

#ifdef	__cplusplus
}
#endif


