/* 
 * File:   NanoPB_Encoder.h
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
 * @param ppBuffer The buffer to hold the result
 * @return The number of bytes written to the buffer
 */
size_t Nanopb_Encode(   tBoardData* state,
                        const NanopbFlagsArray* fields,
                        uint8_t* ppBuffer,size_t buffSize);

/**
 * Fast-path streaming protobuf encoder.
 * Writes wire-format bytes directly for streaming fields (timestamp,
 * analog_in_data, digital_data, digital_port_dir), bypassing nanopb's
 * 65-field descriptor iteration. ~10-25x faster than Nanopb_Encode
 * for the streaming hot path. Use Nanopb_Encode for metadata/config.
 */
size_t Nanopb_EncodeStreamingFast(tBoardData* state,
                        const NanopbFlagsArray* fields,
                        uint8_t* pBuffer, size_t buffSize);

void int2PBByteArray(   const size_t integer,
                        pb_bytes_array_t* byteArray,                        
                        size_t maxArrayLen);
#ifdef	__cplusplus
}
#endif


