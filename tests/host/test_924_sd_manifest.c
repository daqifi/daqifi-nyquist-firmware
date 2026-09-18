/* ==========================================================================
 * test_924_sd_manifest.c — host test for the per-session SD integrity
 * manifest (#924).
 *
 * #924 gives every SD streaming session ONE manifest file holding one line per
 * stream file: `<name>,<bytes>,0x<CRC32>`. The CRC is accumulated over the
 * WRITE path as the bytes go out, not recomputed afterwards, so the record
 * describes the file as it was written rather than as it reads back later.
 *
 * Two of the three claims that makes are testable here with the REAL firmware
 * text — no stubs, no re-implementation:
 *
 *   1. THE ACCUMULATION MODEL IS EXACT. The firmware folds each successful
 *      SYS_FS_FileWrite's landed bytes into a running CRC32, in arbitrary,
 *      write-determined chunks (a full 512-byte-aligned extract, a partial
 *      write's remainder, a 444-byte header). `chunked_*` below pins that
 *      CRC32_Init + N*CRC32_Update + CRC32_Finalize over ANY chunking equals
 *      CRC32_Compute over the concatenation — across 1..N-way splits of a
 *      4 KiB buffer, including zero-length chunks (a write can report 0 only
 *      on the error path, but the accumulator must be indifferent to it).
 *      This is what makes "the CRC in the manifest == zlib.crc32 of the
 *      downloaded file" true, which is #924's headline acceptance criterion.
 *
 *   2. IT REALLY IS zlib's CRC-32. The same criterion compares the recorded
 *      value against Python's zlib.crc32 AND against SYST:STOR:SD:CRC?. Both
 *      sides of that are CRC32.c, so the suite pins it against EXTERNAL
 *      vectors (`zlib_vectors`) rather than against itself — including the
 *      canonical "123456789" -> 0xCBF43926 check value from the CRC catalogue.
 *
 *   3. THE LINE AND NAME RENDERING. SdManifest.h is the part of the manifest
 *      that is pure text, split out of sd_card_manager.c precisely so it can
 *      be compiled here (the same trade #889 made for AD7609Scale.h and #164
 *      for JSON_StringEscape.h — sd_card_manager.c itself drags in Harmony,
 *      FreeRTOS, FatFs and the whole board graph). The format is a wire
 *      contract with the companion test's parser, so it is pinned literally.
 *
 * WHAT THIS SUITE CANNOT SEE, stated plainly so nobody reads it as more than
 * it is: it does not prove the firmware CALLS these in the right places. That
 * is the bench test's job (test_306b_sd_manifest.py in
 * daqifi-python-test-suite) plus the Makefile's grep guards, which fail the
 * BUILD if sd_card_manager.c stops folding the CRC in its single write funnel
 * or stops rendering lines through SdManifest_FormatLine.
 *
 * Mutation proof — RUN, not asserted. Each mutation was applied to the real
 * source, the suite rebuilt and re-run, and the failures below are what it
 * actually printed (2026-09-17):
 *   - drop `\n` from SdManifest_FormatLine's format  -> 6 fail
 *     (line_format_is_exact, small_crc_is_zero_padded, byte_count_is_64_bit,
 *      line_lengths_are_reported, truncation_is_all_or_nothing,
 *      session_shape_round_trips)
 *   - `0x%08X` -> `0x%X`, losing the zero padding    -> 3 fail
 *     (small_crc_is_zero_padded, byte_count_is_64_bit,
 *      truncation_is_all_or_nothing)
 *   - return the truncated line instead of -1        -> 1 fails
 *     (truncation_is_all_or_nothing — the only test that can see it)
 *   - drop the `fullPath[dirLen] != '/'` separator test
 *                                                    -> 2 fail
 *     (sibling_directory_is_not_a_prefix,
 *      non_matching_prefix_falls_back_to_full_path)
 *   - make CRC32_Update restart from CRC32_Init on every call
 *                                                    -> 5 fail
 *     (chunked_equals_whole, chunked_equals_whole_every_length,
 *      zero_length_update_is_identity, header_then_data_is_concatenation,
 *      session_shape_round_trips)
 *     Note `zlib_vectors` SURVIVES that one — CRC32_Compute folds in a single
 *     Update, so a vectors-only suite would have called this firmware correct.
 *     The chunked tests are the whole reason this file exists.
 *   - remove the fold from sd_card_manager.c's SDCardWrite()
 *                                                    -> the BUILD fails, by
 *     the Makefile's first grep guard, before a single assertion runs.
 *
 * Run: make -C tests/host run
 * ========================================================================== */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "test_framework.h"
#include "CRC32.h"        /* real header  (via -I firmware/src/Util) */
#include "SdManifest.h"   /* real header  (via -I firmware/src/Util) */

/* CRC32.c is compiled into this binary — it includes nothing but its own
 * header, so it needs no stub and no UUT copy. */

/* ---------------------------------------------------------------- helpers */

/* The firmware's write path, modelled exactly: init once per FILE, fold each
 * successful write's landed bytes, finalize once at close. */
static uint32_t accumulate_in_chunks(const uint8_t *buf, size_t len,
                                     size_t chunk)
{
    uint32_t crc = CRC32_Init();
    size_t off = 0;

    if (chunk == 0u) {
        chunk = 1u;
    }
    while (off < len) {
        size_t n = (len - off < chunk) ? (len - off) : chunk;
        crc = CRC32_Update(crc, buf + off, n);
        off += n;
    }
    return CRC32_Finalize(crc);
}

/* Deterministic, non-repeating filler: a repeating byte would let a CRC bug
 * that ignores position pass. */
static void fill_pattern(uint8_t *buf, size_t len)
{
    size_t i;
    uint32_t x = 0x12345678u;
    for (i = 0; i < len; i++) {
        x = (x * 1103515245u) + 12345u;   /* plain LCG, not security code */
        buf[i] = (uint8_t)(x >> 16);
    }
}

/* ------------------------------------------------------------- CRC claims */

/* EXTERNAL vectors. These are the published CRC-32/ISO-HDLC values, which is
 * what zlib.crc32, Python's binascii.crc32 and coreutils' `crc32` all produce.
 * Pinning against them is the only way this file can say anything about the
 * "== zlib.crc32(downloaded file)" acceptance criterion — comparing CRC32.c
 * to itself would pass however wrong it is. */
TEST(zlib_vectors)
{
    ASSERT_EQ(CRC32_Compute("", 0), 0x00000000u);
    ASSERT_EQ(CRC32_Compute("a", 1), 0xE8B7BE43u);
    ASSERT_EQ(CRC32_Compute("abc", 3), 0x352441C2u);
    /* The CRC catalogue's check value for CRC-32/ISO-HDLC. */
    ASSERT_EQ(CRC32_Compute("123456789", 9), 0xCBF43926u);
    ASSERT_EQ(CRC32_Compute("The quick brown fox jumps over the lazy dog", 43),
              0x414FA339u);
}

/* THE MODEL THE MANIFEST DEPENDS ON: however the write path happens to split a
 * file into SYS_FS_FileWrite calls, the accumulated CRC is the whole file's.
 * The chunk sizes below are the real ones — 512 (one sector), 4096/16384
 * (extract sizes), 444 (the largest measured #824 header), and a prime that
 * lands mid-sector the way a partial write does. */
TEST(chunked_equals_whole)
{
    uint8_t buf[4096];
    static const size_t chunks[] = { 1, 7, 64, 443, 444, 512, 1024, 4095,
                                     4096, 8192 };
    size_t i;
    uint32_t whole;

    fill_pattern(buf, sizeof(buf));
    whole = CRC32_Compute(buf, sizeof(buf));

    for (i = 0; i < sizeof(chunks) / sizeof(chunks[0]); i++) {
        ASSERT_EQ(accumulate_in_chunks(buf, sizeof(buf), chunks[i]), whole);
    }
}

/* The same property at every length from 0..300, so an off-by-one in the fold
 * cannot hide behind a convenient buffer size. */
TEST(chunked_equals_whole_every_length)
{
    uint8_t buf[300];
    size_t len;

    fill_pattern(buf, sizeof(buf));
    for (len = 0; len <= sizeof(buf); len++) {
        uint32_t whole = CRC32_Compute(buf, len);
        ASSERT_EQ(accumulate_in_chunks(buf, len, 1), whole);
        ASSERT_EQ(accumulate_in_chunks(buf, len, 17), whole);
        ASSERT_EQ(accumulate_in_chunks(buf, len, 512), whole);
    }
}

/* A zero-length fold must be a no-op. The firmware guards `writeLen > 0`
 * before folding, so this is defence for the accumulator rather than a live
 * path — but it is also what makes "header of length 0" (the session's FIRST
 * file, which carries none) cost nothing. */
TEST(zero_length_update_is_identity)
{
    uint8_t buf[64];
    uint32_t crc;

    fill_pattern(buf, sizeof(buf));

    crc = CRC32_Init();
    crc = CRC32_Update(crc, buf, 0);
    crc = CRC32_Update(crc, buf, sizeof(buf));
    crc = CRC32_Update(crc, buf, 0);
    ASSERT_EQ(CRC32_Finalize(crc), CRC32_Compute(buf, sizeof(buf)));
}

/* A file that is header + data must CRC as the concatenation — this is the
 * exact shape of every rotated split file since #824 (header written straight
 * into the handle at open, data appended after). */
TEST(header_then_data_is_concatenation)
{
    uint8_t whole[1024];
    const size_t hdrLen = 444;   /* largest measured real header, #824 */
    uint32_t crc;

    fill_pattern(whole, sizeof(whole));

    crc = CRC32_Init();
    crc = CRC32_Update(crc, whole, hdrLen);
    crc = CRC32_Update(crc, whole + hdrLen, sizeof(whole) - hdrLen);
    ASSERT_EQ(CRC32_Finalize(crc), CRC32_Compute(whole, sizeof(whole)));
}

/* ------------------------------------------------------ line rendering */

TEST(line_format_is_exact)
{
    char line[128];
    int n;

    n = SdManifest_FormatLine(line, sizeof(line), "experiment.csv",
                              20480u, 0x1A2B3C4Du);
    ASSERT_TRUE(n > 0);
    ASSERT_EQ(strcmp(line, "experiment.csv,20480,0x1A2B3C4D\n"), 0);

    /* A rotated part inside a #689 bucket. */
    n = SdManifest_FormatLine(line, sizeof(line), "P001/experiment-65.csv",
                              1u, 0xFFFFFFFFu);
    ASSERT_TRUE(n > 0);
    ASSERT_EQ(strcmp(line, "P001/experiment-65.csv,1,0xFFFFFFFF\n"), 0);
}

TEST(small_crc_is_zero_padded)
{
    char line[128];

    ASSERT_TRUE(SdManifest_FormatLine(line, sizeof(line), "a.csv", 0u, 0u) > 0);
    ASSERT_EQ(strcmp(line, "a.csv,0,0x00000000\n"), 0);

    ASSERT_TRUE(SdManifest_FormatLine(line, sizeof(line), "a.csv", 0u, 0xABu) > 0);
    ASSERT_EQ(strcmp(line, "a.csv,0,0x000000AB\n"), 0);
}

/* currentFileBytes is uint64_t. A %u/%lu here would silently wrap a file
 * bigger than 4 GB — which is precisely the size file splitting exists for. */
TEST(byte_count_is_64_bit)
{
    char line[128];

    ASSERT_TRUE(SdManifest_FormatLine(line, sizeof(line), "big.csv",
                                      4294967296ull, 0x00000001u) > 0);
    ASSERT_EQ(strcmp(line, "big.csv,4294967296,0x00000001\n"), 0);

    ASSERT_TRUE(SdManifest_FormatLine(line, sizeof(line), "big.csv",
                                      18446744073709551615ull, 0u) > 0);
    ASSERT_EQ(strcmp(line, "big.csv,18446744073709551615,0x00000000\n"), 0);
}

TEST(line_lengths_are_reported)
{
    char line[128];
    int n = SdManifest_FormatLine(line, sizeof(line), "x.csv", 5u, 0u);

    ASSERT_TRUE(n > 0);
    ASSERT_EQ((size_t)n, strlen(line));
    /* The trailing newline is part of what is written to the card, so it must
     * be inside the reported count — the caller passes n straight to
     * SYS_FS_FileWrite. */
    ASSERT_EQ(line[n - 1], '\n');
}

TEST(truncation_is_all_or_nothing)
{
    char line[16];
    int n;

    /* "verylongname.csv,20480,0x1A2B3C4D\n" needs 34 + NUL. */
    n = SdManifest_FormatLine(line, sizeof(line), "verylongname.csv",
                              20480u, 0x1A2B3C4Du);
    ASSERT_EQ(n, -1);
    ASSERT_EQ(line[0], '\0');

    /* Exactly one byte short is still a refusal, not a silent cut. */
    {
        char tight[sizeof("a.csv,1,0x00000002\n")];   /* == needed size */
        char cramped[sizeof("a.csv,1,0x00000002\n") - 1u];

        ASSERT_TRUE(SdManifest_FormatLine(tight, sizeof(tight), "a.csv", 1u, 2u) > 0);
        ASSERT_EQ(strcmp(tight, "a.csv,1,0x00000002\n"), 0);

        ASSERT_EQ(SdManifest_FormatLine(cramped, sizeof(cramped), "a.csv", 1u, 2u), -1);
        ASSERT_EQ(cramped[0], '\0');
    }
}

TEST(format_line_null_safety)
{
    char line[64];

    ASSERT_EQ(SdManifest_FormatLine(NULL, sizeof(line), "a", 0u, 0u), -1);
    ASSERT_EQ(SdManifest_FormatLine(line, 0u, "a", 0u, 0u), -1);
    ASSERT_EQ(SdManifest_FormatLine(line, sizeof(line), NULL, 0u, 0u), -1);
}

/* ------------------------------------------------------- relative names */

TEST(relative_name_strips_the_directory)
{
    ASSERT_EQ(strcmp(SdManifest_RelativeName("DAQiFi/experiment.csv", "DAQiFi"),
                     "experiment.csv"), 0);
    ASSERT_EQ(strcmp(SdManifest_RelativeName("DAQiFi/experiment-7.csv", "DAQiFi"),
                     "experiment-7.csv"), 0);
}

/* #689 bucketing puts later parts in a subdirectory. The manifest must keep
 * that subdirectory in the name, because SD:CRC? / SD:GET need it to find the
 * file (they prepend the configured directory and nothing else). */
TEST(relative_name_keeps_the_bucket)
{
    ASSERT_EQ(strcmp(SdManifest_RelativeName("DAQiFi/P001/experiment-65.csv",
                                             "DAQiFi"),
                     "P001/experiment-65.csv"), 0);
}

/* "DAQiFi" must not be treated as a prefix of "DAQiFi2". Without the
 * separator check the name would come back as "/x.csv", which names nothing. */
TEST(sibling_directory_is_not_a_prefix)
{
    const char *r = SdManifest_RelativeName("DAQiFi2/x.csv", "DAQiFi");
    ASSERT_EQ(strcmp(r, "DAQiFi2/x.csv"), 0);
}

TEST(non_matching_prefix_falls_back_to_full_path)
{
    ASSERT_EQ(strcmp(SdManifest_RelativeName("other/x.csv", "DAQiFi"),
                     "other/x.csv"), 0);
    /* An empty directory setting cannot strip anything. */
    ASSERT_EQ(strcmp(SdManifest_RelativeName("DAQiFi/x.csv", ""),
                     "DAQiFi/x.csv"), 0);
    /* Directory with no file after it is not a name. */
    ASSERT_EQ(strcmp(SdManifest_RelativeName("DAQiFi/", "DAQiFi"),
                     "DAQiFi/"), 0);
    /* Exact equality, no separator at all. */
    ASSERT_EQ(strcmp(SdManifest_RelativeName("DAQiFi", "DAQiFi"), "DAQiFi"), 0);
}

TEST(relative_name_null_safety)
{
    ASSERT_TRUE(SdManifest_RelativeName(NULL, "DAQiFi") == NULL);
    ASSERT_EQ(strcmp(SdManifest_RelativeName("DAQiFi/x.csv", NULL),
                     "DAQiFi/x.csv"), 0);
}

/* ------------------------------------------------------- end to end shape */

/* One rotating session, rendered the way the firmware does it: a running CRC
 * per file, finalized at that file's close, one line each. Proves the two
 * halves compose — the value the accumulator finalizes is the value the line
 * carries, and it equals the whole-file CRC. */
TEST(session_shape_round_trips)
{
    uint8_t file0[1500], file1[900];
    char l0[128], l1[128];
    uint32_t c0, c1;
    char expect0[128], expect1[128];

    fill_pattern(file0, sizeof(file0));
    fill_pattern(file1, sizeof(file1));

    /* file 0: header (0 bytes — the session's first file carries none) then
     * three write-sized chunks. */
    c0 = accumulate_in_chunks(file0, sizeof(file0), 512);
    /* file 1: a rotation, so a 444-byte header then the rest. */
    c1 = accumulate_in_chunks(file1, sizeof(file1), 444);

    ASSERT_EQ(c0, CRC32_Compute(file0, sizeof(file0)));
    ASSERT_EQ(c1, CRC32_Compute(file1, sizeof(file1)));

    ASSERT_TRUE(SdManifest_FormatLine(l0, sizeof(l0),
                    SdManifest_RelativeName("DAQiFi/exp.csv", "DAQiFi"),
                    sizeof(file0), c0) > 0);
    ASSERT_TRUE(SdManifest_FormatLine(l1, sizeof(l1),
                    SdManifest_RelativeName("DAQiFi/exp-1.csv", "DAQiFi"),
                    sizeof(file1), c1) > 0);

    snprintf(expect0, sizeof(expect0), "exp.csv,%u,0x%08X\n",
             (unsigned)sizeof(file0), (unsigned)c0);
    snprintf(expect1, sizeof(expect1), "exp-1.csv,%u,0x%08X\n",
             (unsigned)sizeof(file1), (unsigned)c1);
    ASSERT_EQ(strcmp(l0, expect0), 0);
    ASSERT_EQ(strcmp(l1, expect1), 0);
}

int main(void)
{
    printf("=== #924 SD session integrity manifest ===\n");
    RUN(zlib_vectors);
    RUN(chunked_equals_whole);
    RUN(chunked_equals_whole_every_length);
    RUN(zero_length_update_is_identity);
    RUN(header_then_data_is_concatenation);
    RUN(line_format_is_exact);
    RUN(small_crc_is_zero_padded);
    RUN(byte_count_is_64_bit);
    RUN(line_lengths_are_reported);
    RUN(truncation_is_all_or_nothing);
    RUN(format_line_null_safety);
    RUN(relative_name_strips_the_directory);
    RUN(relative_name_keeps_the_bucket);
    RUN(sibling_directory_is_not_a_prefix);
    RUN(non_matching_prefix_falls_back_to_full_path);
    RUN(relative_name_null_safety);
    RUN(session_shape_round_trips);
    return TEST_SUMMARY();
}
