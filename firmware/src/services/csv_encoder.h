

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

/*!
 * Check if CSV header has already been sent in this streaming session
 * @return true if header was sent, false if not yet
 */
bool csv_IsHeaderSent(void);

/*!
 * Generate CSV header into a caller-provided buffer without changing
 * encoder state.  Used by streaming.c to write SD-only headers on
 * file rotation without disturbing USB/WiFi header tracking.
 * @param buffer  Output buffer
 * @param size    Size of output buffer
 * @return Bytes written (0 on failure)
 */
size_t csv_GenerateHeaderToBuffer(char* buffer, size_t size);

#ifdef	__cplusplus
}
#endif


