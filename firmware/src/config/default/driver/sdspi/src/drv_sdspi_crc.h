/* DAQiFi addition (#1217): the two CRCs SPI-mode SD uses once CMD59 has turned
 * checking on. Header-only and free of Harmony includes so tests/host can test
 * the real code against the published vectors (test_1217_sd_crc.c).
 *
 * Both are MSB-first with an initial value of 0, per the SD Physical Layer
 * spec's CRC section: CRC7 is x^7 + x^3 + 1 (0x09), CRC16 is CCITT
 * x^16 + x^12 + x^5 + 1 (0x1021). */
#ifndef DRV_SDSPI_CRC_H
#define DRV_SDSPI_CRC_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

/* The 7-bit CRC of a command frame's first five bytes. On the wire it is sent
 * as (crc << 1) | 1, the 1 being the frame's end bit. Bitwise: only five bytes,
 * once per command. */
static inline uint8_t DRV_SDSPI_Crc7(const uint8_t* data, size_t len)
{
    uint8_t crc = 0U;

    for (size_t i = 0U; i < len; i++)
    {
        for (uint8_t mask = 0x80U; mask != 0U; mask >>= 1)
        {
            const bool topSet = ((crc & 0x40U) != 0U);
            const bool bitSet = ((data[i] & mask) != 0U);

            crc = (uint8_t)((crc << 1) & 0x7FU);
            if (topSet != bitSet)
            {
                crc ^= 0x09U;
            }
        }
    }
    return crc;
}

/* CRC-16-CCITT of a data block. A block is 512 bytes and is often in the
 * uncached coherent write buffer, so this runs per sector on the write path:
 * it uses the standard table-free byte update instead of a bit loop (about one
 * eighth of the work) and needs no table in RAM or flash. test_1217_sd_crc.c
 * checks it against a plain bitwise version. */
static inline uint16_t DRV_SDSPI_Crc16(const uint8_t* data, size_t len)
{
    uint16_t crc = 0U;

    for (size_t i = 0U; i < len; i++)
    {
        crc = (uint16_t)((uint16_t)(crc >> 8) | (uint16_t)(crc << 8));
        crc ^= data[i];
        crc ^= (uint16_t)((crc & 0x00FFU) >> 4);
        crc ^= (uint16_t)(crc << 12);
        crc ^= (uint16_t)((crc & 0x00FFU) << 5);
    }
    return crc;
}

#endif /* DRV_SDSPI_CRC_H */
