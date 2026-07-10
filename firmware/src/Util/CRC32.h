/**
 * @file CRC32.h
 * @brief Standalone CRC-32 (IEEE 802.3, reflected polynomial 0xEDB88320).
 *
 * #306: single small implementation serving settings integrity, SD file
 * integrity, and transfer verification - severs those paths from wolfSSL
 * (pinned at v5.4.0, not upgradable). Nibble-table variant: 64-byte table,
 * ~2 cycles/bit - SD/NVM I/O dominates every intended use.
 *
 * Usage (streaming):
 *   uint32_t crc = CRC32_Init();
 *   crc = CRC32_Update(crc, buf, len);   // repeat per chunk
 *   crc = CRC32_Finalize(crc);
 * One-shot: CRC32_Compute(buf, len).
 *
 * Matches zlib crc32() / Python binascii.crc32 / `crc32` coreutils output.
 */
#ifndef UTIL_CRC32_H
#define UTIL_CRC32_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Start a streaming CRC-32 computation. */
static inline uint32_t CRC32_Init(void)
{
    return 0xFFFFFFFFu;
}

/** Fold `len` bytes into a running CRC started with CRC32_Init(). */
uint32_t CRC32_Update(uint32_t crc, const void *data, size_t len);

/** Final XOR - call once after the last CRC32_Update(). */
static inline uint32_t CRC32_Finalize(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFu;
}

/** One-shot convenience over a contiguous buffer. */
uint32_t CRC32_Compute(const void *data, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* UTIL_CRC32_H */
