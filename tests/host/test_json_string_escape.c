/* ==========================================================================
 * test_json_string_escape.c -- host test for services/JSON_StringEscape.h
 * (#164)
 *
 * JSON_Encoder.c wrote free-form strings (the WiFi SSID, and defensively the
 * friendly device name) straight onto the wire with a raw "%s". SCPI's SSID
 * setter (SCPI_SafeParamString, SCPILAN.c) does a bare memcpy with no
 * character validation, so a '"' or '\' landed unescaped and produced a JSON
 * document `json.loads()` (or any conformant parser) rejects.
 *
 * Neither field is currently reachable via a real streaming session: the
 * ONLY caller of Json_Encode (streaming.c ~line 3303) builds its field list
 * from exactly four tags -- msg_time_stamp, analog_in_data, digital_data,
 * digital_port_dir -- so ssid_tag and friendly_device_name_tag are dead code
 * on the wire today. That is exactly why this fix needs a HOST test: there
 * is no bench recipe that can stream an SSID through JSON_Encode to prove
 * the escaping. This suite is what makes "the escaper is correct" mean
 * something more than reading the diff.
 *
 * It can exist at all because the escaping logic was split into
 * JSON_StringEscape.h, which includes nothing but <stddef.h>. The function
 * below is compiled from the SAME text XC32 compiles into the firmware --
 * no stubs, no mock of BoardConfig.h / daqifi_settings.h / the rest of the
 * graph JSON_Encoder.c pulls in.
 *
 * Mutation proof (required by #164): reverting the escaper to the pre-fix
 * behaviour -- i.e. copying bytes through unescaped except for a NUL check --
 * must fail this suite. `quote_and_backslash_are_escaped`,
 * `control_chars_use_named_escapes`, and `high_bytes_use_uXXXX` each pin an
 * exact escaped byte sequence a raw passthrough cannot produce.
 *
 * Run: make -C tests/host run
 * ========================================================================== */
#include <stdio.h>
#include <string.h>

#include "test_framework.h"
#include "JSON_StringEscape.h"   /* real header (via -I firmware/src/services) */

/* ---------------------------------------------------------------------------
 * Plain printable ASCII (0x20..0x7E) other than '"' and '\' passes through
 * completely unchanged -- this is the common case (a normal SSID like
 * "MyHomeNetwork") and must not pick up spurious escaping.
 * ------------------------------------------------------------------------- */
TEST(printable_ascii_passes_through_unchanged)
{
    char out[JSON_ESC_MAX_LEN];
    const char* in = "MyHomeNetwork-5G_2.4";
    size_t n = escape_json_string(in, strlen(in), out, sizeof(out));
    ASSERT_EQ(n, strlen(in));
    ASSERT_TRUE(strcmp(out, in) == 0);
}

TEST(empty_input_produces_empty_output)
{
    char out[JSON_ESC_MAX_LEN];
    size_t n = escape_json_string("", 0, out, sizeof(out));
    ASSERT_EQ(n, 0);
    ASSERT_EQ(out[0], '\0');
}

/* ---------------------------------------------------------------------------
 * The two JSON-structural characters. This is the defect #164 fixes: a raw
 * SSID containing '"' or '\' broke the JSON document's structure outright.
 * A passthrough implementation produces `n == strlen(in)` here; a correct
 * one produces `n == strlen(in) + (number of quote/backslash bytes)`.
 * ------------------------------------------------------------------------- */
TEST(quote_and_backslash_are_escaped)
{
    char out[JSON_ESC_MAX_LEN];
    const char in[] = "a\"b\\c";              /* a " b \ c -- 5 source bytes */
    size_t n = escape_json_string(in, sizeof(in) - 1, out, sizeof(out));
    ASSERT_TRUE(strcmp(out, "a\\\"b\\\\c") == 0);  /* a\"b\\c -- 7 bytes */
    ASSERT_EQ(n, 7);
}

/* ---------------------------------------------------------------------------
 * The named single-character escapes RFC 8259 prefers over \u00XX for
 * backspace/formfeed/newline/CR/tab.
 * ------------------------------------------------------------------------- */
TEST(control_chars_use_named_escapes)
{
    char out[JSON_ESC_MAX_LEN];
    const char in[] = { '\b', '\f', '\n', '\r', '\t' };
    size_t n = escape_json_string(in, sizeof(in), out, sizeof(out));
    ASSERT_TRUE(strcmp(out, "\\b\\f\\n\\r\\t") == 0);
    ASSERT_EQ(n, 10);
}

/* ---------------------------------------------------------------------------
 * Every other byte outside printable ASCII -- both the C0 control range not
 * covered by a named escape, and everything above 0x7E -- becomes \u00XX.
 * This is deliberately more aggressive than the RFC 8259 minimum (which only
 * requires escaping 0x00..0x1F): a JSON document must also be valid UTF-8,
 * and an 802.11 SSID is an opaque octet string that may carry raw bytes in
 * 0x80..0xFF, which would otherwise make the whole STREAM unparseable, not
 * just this one field.
 * ------------------------------------------------------------------------- */
TEST(high_bytes_use_uXXXX)
{
    char out[JSON_ESC_MAX_LEN];
    const unsigned char in[] = { 0x01, 0x1F, 0x7F, 0x80, 0xC3, 0xA9, 0xFF };
    size_t n = escape_json_string((const char*)in, sizeof(in), out, sizeof(out));
    ASSERT_TRUE(strcmp(out,
        "\\u0001\\u001F\\u007F\\u0080\\u00C3\\u00A9\\u00FF") == 0);
    ASSERT_EQ(n, 7 * 6);
}

/* ---------------------------------------------------------------------------
 * Only inLen bytes are read. This is the over-read fix noted in #164: the
 * pre-fix SSID caller clamped its length CHECK to WDRV_WINC_MAX_SSID_LEN but
 * then handed the un-clamped source array to "%s", which reads until the
 * first NUL regardless of the check. A non-NUL-terminated 32-byte SSID field
 * would over-read into whatever memory follows it. Model that here with a
 * buffer that is NOT NUL-terminated within inLen and has a sentinel
 * printable byte immediately after the declared length: if the function read
 * past inLen, that sentinel would appear in the output.
 * ------------------------------------------------------------------------- */
TEST(only_inLen_bytes_are_read)
{
    char out[JSON_ESC_MAX_LEN];
    char in[6] = { 'a', 'b', 'c', 'd', 'e', '!' };  /* '!' is one past inLen=5 */
    size_t n = escape_json_string(in, 5, out, sizeof(out));
    ASSERT_TRUE(strcmp(out, "abcde") == 0);
    ASSERT_EQ(n, 5);
    ASSERT_TRUE(strchr(out, '!') == NULL);
}

/* ---------------------------------------------------------------------------
 * Buffer-too-small: the function must leave `out` empty and return 0 rather
 * than writing a truncated (and therefore unterminated-looking, or worse,
 * mid-escape-sequence) partial escape. JSON_Encoder.c relies on this to
 * decide whether to omit the optional field.
 * ------------------------------------------------------------------------- */
TEST(overflow_returns_zero_and_empties_output)
{
    char out[4];
    /* "abcd" needs 5 bytes (4 + NUL); the 4-byte destination cannot hold it. */
    size_t n = escape_json_string("abcd", 4, out, sizeof(out));
    ASSERT_EQ(n, 0);
    ASSERT_EQ(out[0], '\0');
}

/* ---------------------------------------------------------------------------
 * An escape sequence must never be split across the overflow boundary.
 * Five plain bytes fit with four bytes of room left over (nine total), but
 * the sixth source byte needs a full six-character escape plus the NUL
 * terminator (seven bytes), which does not fit in the four remaining. A
 * broken implementation could write part of the escape and stop; the
 * correct one checks before writing any part of an escape and refuses the
 * whole call, emptying what had already been written for the first five
 * bytes too.
 * ------------------------------------------------------------------------- */
TEST(overflow_mid_escape_sequence_is_refused_wholesale)
{
    char out[9];
    unsigned char in[6];
    in[0] = 'a';
    in[1] = 'b';
    in[2] = 'c';
    in[3] = 'd';
    in[4] = 'e';
    in[5] = (unsigned char)1;
    size_t n = escape_json_string((const char*)in, sizeof(in), out, sizeof(out));
    ASSERT_EQ(n, 0);
    ASSERT_EQ(out[0], '\0');
}

/* ---------------------------------------------------------------------------
 * Worst-case sizing: exactly JSON_ESC_SRC_MAX_LEN bytes, every one of them
 * needing the maximal 6-byte \u00XX escape, must fit EXACTLY in
 * JSON_ESC_MAX_LEN (the buffer size JSON_Encoder.c actually allocates on its
 * stack). This is the claim the #164 comment makes; pin it so a future
 * change to either constant is caught here rather than on hardware.
 * ------------------------------------------------------------------------- */
TEST(worst_case_source_exactly_fits_the_sized_buffer)
{
    char in[JSON_ESC_SRC_MAX_LEN];
    char out[JSON_ESC_MAX_LEN];
    size_t i;
    for (i = 0; i < sizeof(in); ++i) {
        in[i] = (char)1;   /* every byte needs a full \u00XX escape */
    }
    size_t n = escape_json_string(in, sizeof(in), out, sizeof(out));
    ASSERT_EQ(n, JSON_ESC_SRC_MAX_LEN * 6u);
    ASSERT_EQ(n + 1u, JSON_ESC_MAX_LEN);   /* +1 for the NUL this buffer also holds */
    /* One byte less of room must refuse rather than truncate. */
    {
        char tightOut[JSON_ESC_MAX_LEN - 1];
        size_t n2 = escape_json_string(in, sizeof(in), tightOut, sizeof(tightOut));
        ASSERT_EQ(n2, 0);
    }
}

/* ---------------------------------------------------------------------------
 * NULL-safety: neither a NULL source nor a NULL/zero-sized destination may
 * be dereferenced.
 * ------------------------------------------------------------------------- */
TEST(null_and_zero_size_inputs_are_refused_safely)
{
    char out[JSON_ESC_MAX_LEN];
    ASSERT_EQ(escape_json_string(NULL, 5, out, sizeof(out)), 0);
    ASSERT_EQ(escape_json_string("abc", 3, out, 0), 0);
    ASSERT_EQ(escape_json_string("abc", 3, NULL, sizeof(out)), 0);
}

int main(void)
{
    RUN(printable_ascii_passes_through_unchanged);
    RUN(empty_input_produces_empty_output);
    RUN(quote_and_backslash_are_escaped);
    RUN(control_chars_use_named_escapes);
    RUN(high_bytes_use_uXXXX);
    RUN(only_inLen_bytes_are_read);
    RUN(overflow_returns_zero_and_empties_output);
    RUN(overflow_mid_escape_sequence_is_refused_wholesale);
    RUN(worst_case_source_exactly_fits_the_sized_buffer);
    RUN(null_and_zero_size_inputs_are_refused_safely);
    return TEST_SUMMARY();
}
