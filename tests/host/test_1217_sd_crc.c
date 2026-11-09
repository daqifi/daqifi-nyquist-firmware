/* #1217: the SD CRCs, against published values. drv_sdspi_crc.h is header-only
 * and Harmony-free, so this is the real code, and no stubs.
 *
 * Vectors are from the SD Physical Layer spec, except the CMD17 frame, which is
 * the usual one quoted for its CRC7 example, and the CMD59 frame, which is the
 * one this firmware now sends. */
#include <stdbool.h>
#include <stdint.h>

#include "drv_sdspi_crc.h"
#include "test_framework.h"

static uint8_t frameCrcByte(uint8_t cmd, uint32_t arg)
{
    const uint8_t frame[5] = {
        (uint8_t)(0x40U | cmd), (uint8_t)(arg >> 24), (uint8_t)(arg >> 16),
        (uint8_t)(arg >> 8), (uint8_t)arg
    };
    return (uint8_t)((DRV_SDSPI_Crc7(frame, sizeof(frame)) << 1) | 1U);
}

/* Spec shape, kept deliberately naive so it cannot share a bug with the
 * table-free update under test. */
static uint16_t refCrc16(const uint8_t* data, size_t len)
{
    uint16_t crc = 0U;
    for (size_t i = 0U; i < len; i++)
    {
        for (uint8_t mask = 0x80U; mask != 0U; mask >>= 1)
        {
            const bool top = (crc & 0x8000U) != 0U;
            const bool bit = (data[i] & mask) != 0U;
            crc = (uint16_t)(crc << 1);
            if (top != bit)
            {
                crc ^= 0x1021U;
            }
        }
    }
    return crc;
}

TEST(crc7_cmd0)
{
    ASSERT_EQ(frameCrcByte(0U, 0U), 0x95);
}

TEST(crc7_cmd8_0x1aa)
{
    ASSERT_EQ(frameCrcByte(8U, 0x1AAU), 0x87);
}

TEST(crc7_cmd17_0)
{
    ASSERT_EQ(frameCrcByte(17U, 0U), 0x55);
}

TEST(crc7_spec_examples)
{
    /* Spec section 4.5: CMD0 arg 0 -> 0x4A (unshifted); CMD17 arg 0 -> 0x2A;
     * the CMD17 response 0x11 00000900 -> 0x33. */
    const uint8_t cmd0[5] = { 0x40, 0, 0, 0, 0 };
    const uint8_t cmd17[5] = { 0x51, 0, 0, 0, 0 };
    const uint8_t resp17[5] = { 0x11, 0, 0, 0x09, 0 };
    ASSERT_EQ(DRV_SDSPI_Crc7(cmd0, 5U), 0x4A);
    ASSERT_EQ(DRV_SDSPI_Crc7(cmd17, 5U), 0x2A);
    ASSERT_EQ(DRV_SDSPI_Crc7(resp17, 5U), 0x33);
}

TEST(crc7_cmd59_on)
{
    ASSERT_EQ(frameCrcByte(59U, 1U), 0x83);
}

/* The resync (lDRV_SDSPI_WriteResyncTasks) is only harmless on a card that is
 * NOT waiting if a bit-level command parser fails what 0xFE/0xFD plus 0xFF
 * looks like: start and transmit bits, then all ones, i.e. CMD63 with a
 * 0xFFFFFFFF argument and CRC field 0x7F. If that frame's real CRC7 were 0x7F
 * the card would execute a reserved command. It is not. */
TEST(resync_ghost_cmd63_has_bad_crc)
{
    const uint8_t ghost[5] = { 0x7F, 0xFF, 0xFF, 0xFF, 0xFF };
    ASSERT_TRUE(DRV_SDSPI_Crc7(ghost, 5U) != 0x7FU);
}

TEST(crc16_spec_vector_512_ff)
{
    uint8_t block[512];
    memset(block, 0xFF, sizeof(block));
    ASSERT_EQ(DRV_SDSPI_Crc16(block, sizeof(block)), 0x7FA1);
}

/* The property the CMD24 resync rests on: its data block's CRC16 is not the
 * 0xFFFF it sends, so a waiting card must reject it. Asserted from the code,
 * not from the published vector above. */
TEST(resync_block_is_guaranteed_bad)
{
    uint8_t block[512];
    memset(block, 0xFF, sizeof(block));
    ASSERT_TRUE(DRV_SDSPI_Crc16(block, sizeof(block)) != 0xFFFFU);
}

TEST(crc16_zero_block)
{
    uint8_t block[512];
    memset(block, 0x00, sizeof(block));
    ASSERT_EQ(DRV_SDSPI_Crc16(block, sizeof(block)), 0x0000);
}

TEST(crc16_check_value)
{
    /* CRC-16/XMODEM's catalogue check value: same polynomial, init and bit
     * order as SD's data CRC. */
    const uint8_t text[9] = { '1', '2', '3', '4', '5', '6', '7', '8', '9' };
    ASSERT_EQ(DRV_SDSPI_Crc16(text, sizeof(text)), 0x31C3);
}

TEST(crc16_matches_bitwise_reference)
{
    uint8_t block[512];
    uint32_t lfsr = 0x12345678U;

    for (int round = 0; round < 64; round++)
    {
        for (size_t i = 0U; i < sizeof(block); i++)
        {
            lfsr = (lfsr >> 1) ^ ((0U - (lfsr & 1U)) & 0xEDB88320U);
            block[i] = (uint8_t)lfsr;
        }
        ASSERT_EQ(DRV_SDSPI_Crc16(block, sizeof(block)), refCrc16(block, sizeof(block)));
    }
}

int main(void)
{
    RUN(crc7_cmd0);
    RUN(crc7_cmd8_0x1aa);
    RUN(crc7_cmd17_0);
    RUN(crc7_spec_examples);
    RUN(crc7_cmd59_on);
    RUN(resync_ghost_cmd63_has_bad_crc);
    RUN(crc16_spec_vector_512_ff);
    RUN(resync_block_is_guaranteed_bad);
    RUN(crc16_zero_block);
    RUN(crc16_check_value);
    RUN(crc16_matches_bitwise_reference);
    return TEST_SUMMARY();
}
