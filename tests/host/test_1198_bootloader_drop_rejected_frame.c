/* ==========================================================================
 * test_1198_bootloader_drop_rejected_frame.c -- issue #1198
 *
 * WHAT IS UNDER TEST
 *
 * Bootloader_ProcessBuffer (bootloader/.../framework/bootloader/src/
 * datastream/datastream.c) dispatches one received frame. Bootloader_Tasks
 * (bootloader.c) calls it from its PROCESS_COMMAND case, whose body is that
 * one call -- so the frame is dispatched again on every scheduler pass until
 * Bootloader_ProcessBuffer itself moves currentState on.
 *
 * Before #1198 two arms never did:
 *   - PROGRAM_FLASH whose APP_ProgramHexRecord result was not HEX_REC_NORMAL
 *     (a bare `break`), and
 *   - `default:` (an unknown command byte).
 * Both left currentState == PROCESS_COMMAND, so the same buffer was replayed
 * forever: every record EARLIER in a rejected PROGRAM_FLASH frame was
 * re-programmed on each pass with no erase in between, and the device never
 * read the host's next frame, so a retry could not reach it. Only a USB reset
 * recovered it.
 *
 * The fix sets currentState = BOOTLOADER_GET_COMMAND in both arms: the frame
 * is dropped without an ACK (exactly as before, from the host's side) and the
 * device goes back to reading.
 *
 * HOW
 *
 * The REAL datastream.c is compiled into test_1198_datastream_uut.c, so the
 * dispatch, the framing parser and the read re-arm are the shipping code. What
 * is stubbed or mirrored, and why, is listed at the top of that file. The
 * Makefile also builds this test against a copy of datastream.c with the two
 * fix lines deleted and REQUIRES that build to fail, so the test is shown to
 * be able to fail on every run, not just once by hand.
 * ========================================================================== */
#include <stdint.h>
#include <string.h>

#include "test_framework.h"
#include "test_1198_uut.h"

/* Comfortably more passes than any real dispatch needs (one). Before the fix
 * a rejected frame used every one of them. */
#define MANY_PASSES 1000u

/* A PROGRAM_FLASH frame as the parser leaves it in buff2: command byte,
 * body, two CRC bytes (low first, as Bootloader_BufferEventHandler reads
 * them). The body is opaque here -- APP_ProgramHexRecord is stubbed. */
static int build_frame(uint8_t *out, uint8_t cmd, const uint8_t *body, int bodyLen)
{
    out[0] = cmd;
    memcpy(&out[1], body, (size_t)bodyLen);
    const uint16_t crc = t1198_Crc(out, (uint32_t)(1 + bodyLen));
    out[1 + bodyLen] = (uint8_t)(crc & 0xFFu);
    out[2 + bodyLen] = (uint8_t)(crc >> 8);
    return 3 + bodyLen;
}

/* The same frame on the wire: SOH, DLE-escaped bytes, EOT. */
static int encode_wire(uint8_t *wire, const uint8_t *frame, int len)
{
    int n = 0;
    wire[n++] = t1198_SOH;
    for (int i = 0; i < len; i++)
    {
        if ((frame[i] == t1198_SOH) || (frame[i] == t1198_EOT) || (frame[i] == t1198_DLE))
        {
            wire[n++] = t1198_DLE;
        }
        wire[n++] = frame[i];
    }
    wire[n++] = t1198_EOT;
    return n;
}

static const uint8_t kRecord[] = {
    /* :04 0000 00 DEADBEEF cs -- shape only; the stub does not parse it */
    0x04, 0x00, 0x00, 0x00, 0xDE, 0xAD, 0xBE, 0xEF, 0xC4
};

/* ---- the defect: a rejected PROGRAM_FLASH ------------------------------ */

TEST(rejected_program_flash_returns_to_get_command)
{
    const int rejects[] = { t1198_HEX_CRC_ERROR, t1198_HEX_PGM_ERROR, t1198_HEX_UNKNOW_TYPE };
    for (unsigned i = 0; i < sizeof(rejects) / sizeof(rejects[0]); i++)
    {
        uint8_t frame[64];
        t1198_Reset();
        t1198_SetHexResult(rejects[i]);
        t1198_LoadFrame(frame, build_frame(frame, (uint8_t)t1198_CMD_PROGRAM_FLASH, kRecord, (int)sizeof(kRecord)));

        t1198_ProcessOnce();

        ASSERT_EQ(t1198_HexCalls(), 1);
        ASSERT_EQ(t1198_State(), t1198_ST_GET_COMMAND);
        /* No ACK: nothing was queued for SEND_RESPONSE. */
        ASSERT_EQ(t1198_ResponseSize(), 0);
    }
}

TEST(rejected_program_flash_is_programmed_exactly_once)
{
    /* #1198's acceptance criterion, through the dispatcher: a rejected frame
     * is processed once, not once per scheduler pass. Pre-fix this made
     * MANY_PASSES calls -- each one re-programming the frame's records. */
    uint8_t frame[64];
    t1198_Reset();
    t1198_SetHexResult(t1198_HEX_PGM_ERROR);
    t1198_LoadFrame(frame, build_frame(frame, (uint8_t)t1198_CMD_PROGRAM_FLASH, kRecord, (int)sizeof(kRecord)));

    const unsigned passes = t1198_DispatchPasses(MANY_PASSES);

    ASSERT_EQ(passes, 1);
    ASSERT_EQ(t1198_HexCalls(), 1);
    ASSERT_EQ(t1198_State(), t1198_ST_GET_COMMAND);
}

/* ---- the same defect: an unknown command ------------------------------- */

TEST(unknown_command_returns_to_get_command)
{
    const uint8_t unknown[] = { 0x00, 0x06, 0x7F, 0xFF };
    for (unsigned i = 0; i < sizeof(unknown); i++)
    {
        uint8_t frame[16];
        const uint8_t body[2] = { 0x11, 0x22 };
        t1198_Reset();
        t1198_LoadFrame(frame, build_frame(frame, unknown[i], body, (int)sizeof(body)));

        const unsigned passes = t1198_DispatchPasses(MANY_PASSES);

        ASSERT_EQ(passes, 1);
        ASSERT_EQ(t1198_State(), t1198_ST_GET_COMMAND);
        ASSERT_EQ(t1198_ResponseSize(), 0);
        ASSERT_EQ(t1198_HexCalls(), 0);
        ASSERT_EQ(t1198_EraseCalls(), 0);
        ASSERT_EQ(t1198_ResetCalls(), 0);
    }
}

/* ---- what must NOT change ---------------------------------------------- */

TEST(accepted_program_flash_still_acks)
{
    uint8_t frame[64];
    t1198_Reset();
    t1198_SetHexResult(t1198_HEX_NORMAL);
    const int len = build_frame(frame, (uint8_t)t1198_CMD_PROGRAM_FLASH, kRecord, (int)sizeof(kRecord));
    t1198_LoadFrame(frame, len);

    t1198_ProcessOnce();

    ASSERT_EQ(t1198_State(), t1198_ST_SEND_RESPONSE);
    ASSERT_EQ(t1198_ResponseSize(), 1);
    ASSERT_EQ(t1198_ResponseByte0(), t1198_CMD_PROGRAM_FLASH);
    ASSERT_EQ(t1198_HexCalls(), 1);
    /* The record handed over is the frame body, minus command and CRC. */
    ASSERT_TRUE(t1198_LastHexRecord() == t1198_CmdBuffer() + 1);
    ASSERT_EQ(t1198_LastHexLen(), len - 3);
}

TEST(other_commands_keep_their_transitions)
{
    uint8_t frame[16];
    const uint8_t zeros[8] = { 0 };

    t1198_Reset();
    t1198_LoadFrame(frame, build_frame(frame, (uint8_t)t1198_CMD_READ_BOOT_INFO, zeros, 0));
    t1198_ProcessOnce();
    ASSERT_EQ(t1198_State(), t1198_ST_SEND_RESPONSE);
    ASSERT_EQ(t1198_ResponseSize(), 3);

    t1198_Reset();
    t1198_LoadFrame(frame, build_frame(frame, (uint8_t)t1198_CMD_ERASE_FLASH, zeros, 0));
    t1198_ProcessOnce();
    ASSERT_EQ(t1198_State(), t1198_ST_WAIT_FOR_NVM);
    ASSERT_EQ(t1198_ResponseSize(), 1);
    ASSERT_EQ(t1198_EraseCalls(), 1);

    t1198_Reset();
    t1198_LoadFrame(frame, build_frame(frame, (uint8_t)t1198_CMD_READ_CRC, zeros, 8));
    t1198_ProcessOnce();
    ASSERT_EQ(t1198_State(), t1198_ST_SEND_RESPONSE);
    ASSERT_EQ(t1198_ResponseSize(), 3);

    t1198_Reset();
    t1198_LoadFrame(frame, build_frame(frame, (uint8_t)t1198_CMD_JMP_TO_APP, zeros, 0));
    t1198_ProcessOnce();
    ASSERT_EQ(t1198_State(), t1198_ST_CLOSE_DATASTREAM);
    ASSERT_EQ(t1198_ResetCalls(), 1);
}

TEST(no_command_byte_is_dispatched_twice)
{
    /* Exhaustive over the command byte, for both an accepted and a rejected
     * record: one Bootloader_ProcessBuffer call always moves the state off
     * PROCESS_COMMAND. This is the "no OTHER arm replays" half of #1198 -- it
     * would catch a new case added later with the same bare `break`. */
    const int results[] = { t1198_HEX_NORMAL, t1198_HEX_PGM_ERROR };
    unsigned stuck = 0u;
    for (unsigned r = 0; r < sizeof(results) / sizeof(results[0]); r++)
    {
        for (unsigned cmd = 0; cmd <= 0xFFu; cmd++)
        {
            uint8_t frame[16];
            const uint8_t zeros[8] = { 0 };
            t1198_Reset();
            t1198_SetHexResult(results[r]);
            t1198_LoadFrame(frame, build_frame(frame, (uint8_t)cmd, zeros, 8));
            if (t1198_DispatchPasses(MANY_PASSES) != 1u)
            {
                if (stuck++ < 4u)
                {
                    printf("    command 0x%02X (hex result %d) was dispatched more than once\n",
                           cmd, results[r]);
                }
            }
        }
    }
    ASSERT_EQ(stuck, 0);
}

/* ---- the re-arm: the next frame gets through --------------------------- */

TEST(dropped_frame_rearms_the_reader_for_the_next_frame)
{
    /* End to end through the REAL Bootloader_BufferEventHandler: a rejected
     * frame, then the host's retry. The point of dropping (rather than
     * replaying) is that the retry is READ -- so the parser must come back
     * clean: the retry's SOH resets the accumulator, and nothing of the
     * dropped frame leaks into it. */
    uint8_t frame1[64], frame2[64], wire[160];
    /* The retry's body carries all three framing bytes, so the escape path is
     * exercised on the frame that matters. */
    const uint8_t body2[] = { 0x03, 0x00, 0x10, 0x00, 0x01, 0x04, 0x10, 0x7E };

    t1198_Reset();
    t1198_EnterGetCommand();
    ASSERT_EQ(t1198_State(), t1198_ST_WAIT_FOR_DONE);

    const int len1 = build_frame(frame1, (uint8_t)t1198_CMD_PROGRAM_FLASH, kRecord, (int)sizeof(kRecord));
    ASSERT_TRUE(t1198_DeliverWire(wire, encode_wire(wire, frame1, len1)));
    ASSERT_EQ(t1198_State(), t1198_ST_PROCESS_COMMAND);

    t1198_SetHexResult(t1198_HEX_PGM_ERROR);
    ASSERT_EQ(t1198_DispatchPasses(MANY_PASSES), 1);
    ASSERT_EQ(t1198_State(), t1198_ST_GET_COMMAND);
    ASSERT_EQ(t1198_ResponseSize(), 0);

    /* Bootloader_Tasks' GET_COMMAND case, then the host's retry. */
    t1198_EnterGetCommand();
    ASSERT_EQ(t1198_State(), t1198_ST_WAIT_FOR_DONE);
    ASSERT_EQ(t1198_PrevState(), t1198_ST_GET_COMMAND);

    const int len2 = build_frame(frame2, (uint8_t)t1198_CMD_PROGRAM_FLASH, body2, (int)sizeof(body2));
    ASSERT_TRUE(t1198_DeliverWire(wire, encode_wire(wire, frame2, len2)));
    ASSERT_EQ(t1198_State(), t1198_ST_PROCESS_COMMAND);
    ASSERT_EQ(t1198_CmdBufferLength(), len2);
    ASSERT_BYTES(t1198_CmdBuffer(), frame2, (size_t)len2);

    t1198_SetHexResult(t1198_HEX_NORMAL);
    ASSERT_EQ(t1198_DispatchPasses(MANY_PASSES), 1);
    ASSERT_EQ(t1198_State(), t1198_ST_SEND_RESPONSE);
    ASSERT_EQ(t1198_ResponseSize(), 1);
    ASSERT_EQ(t1198_HexCalls(), 2);
    ASSERT_EQ(t1198_LastHexLen(), len2 - 3);
    ASSERT_BYTES(t1198_LastHexRecord(), body2, sizeof(body2));
}

int main(void)
{
    printf("#1198: a rejected bootloader frame is dropped, not replayed\n");
    RUN(rejected_program_flash_returns_to_get_command);
    RUN(rejected_program_flash_is_programmed_exactly_once);
    RUN(unknown_command_returns_to_get_command);
    RUN(accepted_program_flash_still_acks);
    RUN(other_commands_keep_their_transitions);
    RUN(no_command_byte_is_dispatched_twice);
    RUN(dropped_frame_rearms_the_reader_for_the_next_frame);
    return test_summary();
}
