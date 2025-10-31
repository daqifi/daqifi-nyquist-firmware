

#pragma once

#include "Util/ArrayWrapper.h"
#include "state/data/BoardData.h"
#include "state/runtime/BoardRuntimeConfig.h"

#ifdef	__cplusplus
extern "C" {
#endif

/*!
 * Encodes board data as CSV with header
 * @return Number of bytes written
 */
size_t csv_Encode(tBoardData* state,
                  NanopbFlagsArray* fields,
                  uint8_t* pBuffer, size_t buffSize);

/*!
 * Reset CSV encoder state (call when streaming stops)
 * Clears header-sent flag so next stream session gets a fresh header
 */
void csv_ResetEncoder(void);

#ifdef	__cplusplus
}
#endif


