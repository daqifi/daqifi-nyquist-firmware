

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

/*!
 * Reset JSON encoder state (call when streaming stops)
 */
void json_ResetEncoder(void);

/*!
 * Check if JSON header has already been sent in this streaming session
 * @return true if header was sent, false if not yet
 */
bool json_IsHeaderSent(void);

/*!
 * Generate JSON metadata header into a caller-provided buffer without
 * changing encoder state.  Used by streaming.c to write SD-only headers
 * on file rotation without disturbing USB/WiFi header tracking.
 * @param buffer  Output buffer
 * @param size    Size of output buffer
 * @return Bytes written (0 on failure)
 */
size_t json_GenerateHeaderToBuffer(char* buffer, size_t size);

#ifdef	__cplusplus
}
#endif


