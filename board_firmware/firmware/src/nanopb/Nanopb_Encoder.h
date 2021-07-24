/* 
 * File:   Encoder.h
 * Author: Daniel
 *
 * Created on September 12, 2016, 12:29 PM
 */
#pragma once

#include "Util/ArrayWrapper.h"
#include "state/data/BoardData.h"
#include "state/runtime/BoardRuntimeConfig.h"

#ifdef	__cplusplus
extern "C" {
#endif
    /**
     * Encodes the system settings to the nanopb format
     * @param state The state to encode
     * @param array The specific fields to encode (see DaqifiOutMessage_fields)
     * @param buffer The buffer to hold the result
     * @param bufferLen The size of the buffer
     * @return The number of bytes written to the buffer
     */
    size_t Nanopb_Encode(BoardData* state, const NanopbFlagsArray* fields, uint8_t* buffer, size_t bufferLen);

    /**
     * Decodes the the nanopb format into the system settings object
     * @param buffer The buffer to hold the result
     * @param bufferLen The size of the buffer
     * @param fields The specific fields to encode (see DaqifiOutMessage_fields)
     * @param state The state object to hold the result
     * @return True on success, false otherwise
     */
    uint8_array Nanopb_Decode(const uint8_t* buffer, const size_t bufferLen, const NanopbFlagsArray* fields, BoardRuntimeConfig* state);

    void int2PBByteArray(const size_t integer, pb_bytes_array_t* byteArray, size_t maxArrayLen);
    
#ifdef	__cplusplus
}
#endif


