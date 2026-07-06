/**
 * @file CRC32.c
 * @brief CRC-32 (IEEE 802.3) - nibble-table implementation. See CRC32.h.
 */
#include "CRC32.h"

/* 16-entry table for the reflected polynomial 0xEDB88320: table[i] is the
 * CRC of the 4-bit value i. 64 bytes of flash vs 1 KB for the byte-wide
 * table; two lookups per byte. */
static const uint32_t crc32_nibble[16] = {
    0x00000000u, 0x1DB71064u, 0x3B6E20C8u, 0x26D930ACu,
    0x76DC4190u, 0x6B6B51F4u, 0x4DB26158u, 0x5005713Cu,
    0xEDB88320u, 0xF00F9344u, 0xD6D6A3E8u, 0xCB61B38Cu,
    0x9B64C2B0u, 0x86D3D2D4u, 0xA00AE278u, 0xBDBDF21Cu
};

uint32_t CRC32_Update(uint32_t crc, const void *data, size_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    while (len-- != 0u)
    {
        crc ^= *p++;
        crc = (crc >> 4) ^ crc32_nibble[crc & 0x0Fu];
        crc = (crc >> 4) ^ crc32_nibble[crc & 0x0Fu];
    }
    return crc;
}

uint32_t CRC32_Compute(const void *data, size_t len)
{
    return CRC32_Finalize(CRC32_Update(CRC32_Init(), data, len));
}
