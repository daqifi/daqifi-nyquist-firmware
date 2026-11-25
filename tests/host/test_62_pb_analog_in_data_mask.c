/* ==========================================================================
 * test_62_pb_analog_in_data_mask.c -- issue #62
 *
 * WHAT IS UNDER TEST
 *
 * A PB streaming message carries a bare packed analog_in_data array whose
 * length is the number of channels that had a VALID sample THIS tick, not the
 * number the client enabled. When a Type-1 channel misses its ARDY (or a
 * shared-scan channel's slot is not yet valid) the array is short and nothing
 * on the wire says which channel is missing, so a client that maps values to
 * channels by position misassigns every value after the gap.
 *
 * The fix adds `analog_in_data_mask` (tag 76): bit j = the j-th ENABLED
 * channel is present in analog_in_data. It is emitted ONLY when
 * analog_in_data is short; a message with a full analog_in_data omits it, so
 * that wire is unchanged and a client that does not know tag 76 skips it as an
 * unknown field. "Absent means complete" holds only for a message that carries
 * a non-empty analog_in_data: a message with no analog_in_data at all (the
 * standalone DIO message) carries no tag 76 either.
 *
 * HOW IT IS TESTED
 *
 * The REAL encode_streaming_fields() is spliced verbatim out of
 * NanoPB_Encoder.c by the Makefile (gen_62_fields.h) and linked against the
 * REAL nanopb encoder and the REAL generated DaqifiOutMessage.pb.c. The bytes
 * it writes are then read back two ways:
 *   - a raw wire walker (no schema), which is what proves "tag 76 is absent
 *     on a full frame" and "the bytes before the new field are unchanged";
 *   - the generated nanopb DECODER, which proves a schema-aware client sees
 *     the mask and the values it was given.
 * The worst-case short frame is also measured against the real
 * STREAMING_MSG_MAX_SIZE (spliced from the same file), because the encoder
 * checks buffer room against that constant BEFORE it pops a sample.
 *
 * WHAT THIS DOES NOT COVER: the mask is computed in Nanopb_EncodeStreamingFast
 * (it needs the sample pool and FreeRTOS, so it is not host-compilable). The
 * Makefile pins that call site against the real source; the end-to-end check
 * (a real short frame from a real ARDY miss) is the test-suite's
 * test_62_pb_channel_mask.py, which needs the bench.
 * ========================================================================== */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include "test_framework.h"

#include "libraries/nanopb/pb_encode.h"
#include "libraries/nanopb/pb_decode.h"
#include "DaqifiOutMessage.pb.h"

/* Spliced verbatim from NanoPB_Encoder.c: PB_VARINT32_MAX .. the closing
 * paren of STREAMING_MSG_MAX_SIZE, then encode_streaming_fields(). */
#include "gen_62_defs.h"
#include "gen_62_fields.h"

#define MAX_CH 16

/* ---- raw wire walker --------------------------------------------------- */

typedef struct {
    bool     present;
    uint64_t value;      /* varint value, or payload length for wire type 2 */
} WireField;

static bool read_varint(const uint8_t *p, size_t n, size_t *pos, uint64_t *out)
{
    uint64_t v = 0;
    unsigned shift = 0;
    while (*pos < n) {
        uint8_t b = p[(*pos)++];
        v |= (uint64_t)(b & 0x7F) << shift;
        if (!(b & 0x80)) { *out = v; return true; }
        shift += 7;
        if (shift > 63) return false;
    }
    return false;
}

/* Find field `tag` at top level. Returns false on a malformed buffer. */
static bool wire_find(const uint8_t *p, size_t n, uint32_t tag, WireField *f)
{
    size_t pos = 0;
    f->present = false;
    f->value = 0;
    while (pos < n) {
        uint64_t key;
        if (!read_varint(p, n, &pos, &key)) return false;
        uint32_t t = (uint32_t)(key >> 3);
        uint32_t wt = (uint32_t)(key & 7);
        uint64_t v = 0;
        if (wt == 0) {
            if (!read_varint(p, n, &pos, &v)) return false;
        } else if (wt == 2) {
            if (!read_varint(p, n, &pos, &v)) return false;
            if (pos + v > n) return false;
            pos += (size_t)v;
        } else {
            return false;   /* the streaming encoder emits only 0 and 2 */
        }
        if (t == tag) { f->present = true; f->value = v; }
    }
    return true;
}

/* ---- helpers ----------------------------------------------------------- */

static size_t encode(uint8_t *buf, size_t cap, uint32_t ts,
                     const int32_t *vals, size_t count,
                     uint32_t validMask, size_t enabledCount)
{
    pb_ostream_t s = pb_ostream_from_buffer(buf, cap);
    bool ok = encode_streaming_fields(&s, ts, vals, count, validMask,
                                      enabledCount, NULL, 0, NULL, 0);
    return ok ? s.bytes_written : 0;
}

static bool decode(const uint8_t *buf, size_t n, DaqifiOutMessage *msg)
{
    memset(msg, 0, sizeof(*msg));
    pb_istream_t s = pb_istream_from_buffer(buf, n);
    return pb_decode(&s, DaqifiOutMessage_fields, msg);
}

static unsigned popcount32(uint32_t v)
{
    unsigned c = 0;
    for (; v; v >>= 1) c += v & 1u;
    return c;
}

/* Reconstruct channel -> value the way a client must: walk the enabled
 * channels in order and consume one value per set mask bit. */
static void client_remap(const DaqifiOutMessage *m, size_t enabled,
                         int32_t *outVal, bool *outHave)
{
    uint32_t mask = m->analog_in_data_mask;
    bool full = (mask == 0);           /* absent = every enabled channel */
    size_t k = 0;
    for (size_t j = 0; j < enabled; j++) {
        bool here = full || (mask & (1u << j));
        outHave[j] = here;
        if (here) outVal[j] = m->analog_in_data[k++];
    }
}

/* ---- tests ------------------------------------------------------------- */

/* A full frame must not change: no tag 76, and the bytes are exactly the
 * pre-change encoding of {timestamp, analog_in_data}. Hand-built expected. */
TEST(full_frame_omits_tag_76_and_wire_is_unchanged)
{
    const int32_t v[3] = { 10, -3, 100000 };
    uint8_t buf[64];
    /* The mask argument is deliberately NONZERO garbage-free-looking (0x7):
     * a full frame must omit the field because count == enabled, not because
     * the mask happened to be zero. */
    size_t n = encode(buf, sizeof buf, 5, v, 3, 0x7u, 3);
    ASSERT_TRUE(n > 0);

    WireField f;
    ASSERT_TRUE(wire_find(buf, n, DaqifiOutMessage_analog_in_data_mask_tag, &f));
    ASSERT_FALSE(f.present);

    /* ts: tag1 varint 5; analog_in_data: tag2 len-delim, zigzag 10->20,
     * -3->5, 100000->200000 (0xC0 0x9A 0x0C). */
    const uint8_t expect[] = { 0x08, 0x05,
                               0x12, 0x05, 0x14, 0x05, 0xC0, 0x9A, 0x0C };
    ASSERT_EQ((long long)n, (long long)sizeof expect);
    ASSERT_TRUE(memcmp(buf, expect, sizeof expect) == 0);
}

/* Four enabled, channel 1 missed: the frame carries mask 0b1101 and exactly
 * the three values that were present. */
TEST(short_frame_carries_mask_whose_popcount_equals_value_count)
{
    const int32_t v[3] = { 111, 333, 444 };   /* channels 0, 2, 3 */
    uint8_t buf[64];
    size_t n = encode(buf, sizeof buf, 7, v, 3, 0xDu, 4);
    ASSERT_TRUE(n > 0);

    WireField f;
    ASSERT_TRUE(wire_find(buf, n, DaqifiOutMessage_analog_in_data_mask_tag, &f));
    ASSERT_TRUE(f.present);
    ASSERT_EQ((long long)f.value, 0xD);
    ASSERT_EQ((long long)popcount32((uint32_t)f.value), 3);

    DaqifiOutMessage m;
    ASSERT_TRUE(decode(buf, n, &m));
    ASSERT_EQ((long long)m.analog_in_data_count, 3);
    ASSERT_EQ((long long)m.analog_in_data_mask, 0xD);
}

/* The defect itself: without the mask a client that assigns by position puts
 * channel 2's value on channel 1. With the mask the remap is exact. */
TEST(a_client_can_rebuild_the_channel_mapping_from_the_mask)
{
    const int32_t v[3] = { 111, 333, 444 };
    uint8_t buf[64];
    size_t n = encode(buf, sizeof buf, 7, v, 3, 0xDu, 4);
    DaqifiOutMessage m;
    ASSERT_TRUE(decode(buf, n, &m));

    int32_t val[4] = {0};
    bool have[4] = {false};
    client_remap(&m, 4, val, have);
    ASSERT_TRUE(have[0]);  ASSERT_EQ(val[0], 111);
    ASSERT_FALSE(have[1]);
    ASSERT_TRUE(have[2]);  ASSERT_EQ(val[2], 333);
    ASSERT_TRUE(have[3]);  ASSERT_EQ(val[3], 444);

    /* What the pre-fix client did: positional, no mask. Show it is wrong, so
     * the assertions above are not satisfiable by an encoder that sends
     * nothing. */
    ASSERT_TRUE(m.analog_in_data[1] != 0 && m.analog_in_data[1] == 333);
    /* position 1 holds channel 2's value, not channel 1's */
}

/* A short frame whose missing channel is the LAST one is also tagged; the
 * old Math.Min client happened to survive that case, a mask must still be
 * correct for it. */
TEST(missing_last_channel_is_tagged_too)
{
    const int32_t v[3] = { 1, 2, 3 };
    uint8_t buf[64];
    size_t n = encode(buf, sizeof buf, 1, v, 3, 0x7u, 4);
    DaqifiOutMessage m;
    ASSERT_TRUE(decode(buf, n, &m));
    ASSERT_EQ((long long)m.analog_in_data_mask, 0x7);
    ASSERT_EQ((long long)m.analog_in_data_count, 3);
}

/* Every enabled-channel count, every single-channel miss: popcount equals the
 * value count, and the remap puts each value on its own channel. */
TEST(every_single_channel_miss_at_every_width_remaps_exactly)
{
    for (size_t enabled = 2; enabled <= MAX_CH; enabled++) {
        for (size_t miss = 0; miss < enabled; miss++) {
            int32_t v[MAX_CH];
            uint32_t mask = 0;
            size_t c = 0;
            for (size_t j = 0; j < enabled; j++) {
                if (j == miss) continue;
                v[c++] = (int32_t)(1000 + 7 * j);
                mask |= 1u << j;
            }
            uint8_t buf[128];
            size_t n = encode(buf, sizeof buf, 9, v, c, mask, enabled);
            ASSERT_TRUE(n > 0);
            DaqifiOutMessage m;
            ASSERT_TRUE(decode(buf, n, &m));
            ASSERT_EQ((long long)popcount32(m.analog_in_data_mask),
                      (long long)m.analog_in_data_count);
            int32_t val[MAX_CH] = {0};
            bool have[MAX_CH] = {false};
            client_remap(&m, enabled, val, have);
            for (size_t j = 0; j < enabled; j++) {
                if (j == miss) {
                    ASSERT_FALSE(have[j]);
                } else {
                    ASSERT_TRUE(have[j]);
                    ASSERT_EQ(val[j], (int32_t)(1000 + 7 * j));
                }
            }
        }
    }
}

/* Several channels missing at once, including the first: 8 enabled, only
 * channels 1, 3 and 7 present (mask 0x8A). */
TEST(several_missing_channels_including_the_first_are_tagged)
{
    const int32_t v[3] = { 21, 23, 27 };           /* channels 1, 3, 7 */
    uint8_t buf[64];
    size_t n = encode(buf, sizeof buf, 3, v, 3, 0x8Au, 8);
    ASSERT_TRUE(n > 0);
    DaqifiOutMessage m;
    ASSERT_TRUE(decode(buf, n, &m));
    ASSERT_EQ((long long)m.analog_in_data_mask, 0x8A);
    /* Hand-built wire bytes, independent of the generated tag constant:
     * field 76, varint = (76 << 3) | 0 = 608 -> E0 04, then 0x8A -> 8A 01. */
    {
        static const uint8_t mask_field[4] = { 0xE0, 0x04, 0x8A, 0x01 };
        bool found = false;
        for (size_t i = 0; i + sizeof mask_field <= n; i++) {
            if (memcmp(buf + i, mask_field, sizeof mask_field) == 0) { found = true; break; }
        }
        ASSERT_TRUE(found);
    }
    ASSERT_EQ((long long)popcount32(m.analog_in_data_mask),
              (long long)m.analog_in_data_count);
    int32_t val[8] = {0};
    bool have[8] = {false};
    client_remap(&m, 8, val, have);
    ASSERT_FALSE(have[0]);
    ASSERT_TRUE(have[1]);  ASSERT_EQ(val[1], 21);
    ASSERT_TRUE(have[3]);  ASSERT_EQ(val[3], 23);
    ASSERT_TRUE(have[7]);  ASSERT_EQ(val[7], 27);
    ASSERT_FALSE(have[2]); ASSERT_FALSE(have[4]);
}

/* The standalone DIO message: no analog values, nothing enabled counted. It
 * must carry no analog_in_data and no tag 76 (so "absent = complete" is NOT a
 * statement about this message). */
TEST(dio_only_message_carries_no_analog_data_and_no_tag_76)
{
    const uint8_t dio[2] = { 0x03, 0x00 };
    uint8_t buf[64];
    pb_ostream_t s = pb_ostream_from_buffer(buf, sizeof buf);
    ASSERT_TRUE(encode_streaming_fields(&s, 9, NULL, 0, 0, 0,
                                        dio, sizeof dio, NULL, 0));
    WireField f;
    ASSERT_TRUE(wire_find(buf, s.bytes_written,
                          DaqifiOutMessage_analog_in_data_mask_tag, &f));
    ASSERT_FALSE(f.present);
    ASSERT_TRUE(wire_find(buf, s.bytes_written,
                          DaqifiOutMessage_analog_in_data_tag, &f));
    ASSERT_FALSE(f.present);
    ASSERT_TRUE(wire_find(buf, s.bytes_written,
                          DaqifiOutMessage_digital_data_tag, &f));
    ASSERT_TRUE(f.present);
}

/* The encoder checks room against STREAMING_MSG_MAX_SIZE before it pops a
 * sample, so the worst short frame (16 enabled, 15 present, maximal varints,
 * DIO data and direction attached) plus the length prefix must fit. */
TEST(worst_case_short_frame_fits_streaming_msg_max_size)
{
    int32_t v[MAX_CH];
    for (int i = 0; i < MAX_CH; i++) v[i] = (i & 1) ? INT32_MIN : INT32_MAX;
    uint8_t dio[2] = { 0xFF, 0xFF };
    uint8_t dir[2] = { 0xFF, 0xFF };
    uint8_t buf[512];

    pb_ostream_t s = pb_ostream_from_buffer(buf, sizeof buf);
    bool ok = encode_streaming_fields(&s, 0xFFFFFFFFu, v, 15, 0x7FFFu,
                                      16, dio, sizeof dio, dir, sizeof dir);
    ASSERT_TRUE(ok);

    pb_ostream_t sz = PB_OSTREAM_SIZING;
    ASSERT_TRUE(pb_encode_varint(&sz, (uint32_t)s.bytes_written));
    size_t total = sz.bytes_written + s.bytes_written;
    ASSERT_TRUE(total <= (size_t)STREAMING_MSG_MAX_SIZE);
}

int main(void)
{
    printf("#62 -- PB analog_in_data_mask (tag 76)\n");
    printf("--------------------------------------\n");
    RUN(full_frame_omits_tag_76_and_wire_is_unchanged);
    RUN(short_frame_carries_mask_whose_popcount_equals_value_count);
    RUN(a_client_can_rebuild_the_channel_mapping_from_the_mask);
    RUN(missing_last_channel_is_tagged_too);
    RUN(every_single_channel_miss_at_every_width_remaps_exactly);
    RUN(several_missing_channels_including_the_first_are_tagged);
    RUN(dio_only_message_carries_no_analog_data_and_no_tag_76);
    RUN(worst_case_short_frame_fits_streaming_msg_max_size);
    return TEST_SUMMARY();
}
