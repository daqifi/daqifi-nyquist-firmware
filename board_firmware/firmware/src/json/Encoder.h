#pragma once

#include "Util/ArrayWrapper.h"
#include "state/data/BoardData.h"
#include "state/runtime/BoardRuntimeConfig.h"

#ifdef	__cplusplus
extern "C" {
#endif

    /**
     * Encodes board data as JSON
     * @return 
     */
    size_t Json_Encode(tBoardData* state, NanopbFlagsArray* fields, uint8_t* buffer, size_t bufferLen);

#ifdef	__cplusplus
}
#endif


