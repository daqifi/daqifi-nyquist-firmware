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
 *      relative_name_canonicalises_the_directory_it_slices)
 *     ⚠ RE-MEASURED 2026-10-25: the second name CHANGED. It was
 *     non_matching_prefix_falls_back_to_full_path; the #1124 r8 test now covers
 *     the sibling case too and fails first. The COUNT being unchanged is why
 *     this had to be re-run rather than assumed -- a stale mutation record
 *     reads as a live measurement.
 *
 * Added 2026-10-25 (#1124 r8 finding 2):
 *   - SdManifest_RelativeName's SdManifest_CanonicalDirLen -> strlen, i.e. the
 *     finding-2 defect itself, with the Makefile's RelativeName pin NEUTERED so
 *     the build reaches the test and the binary forced to rebuild
 *                                                    -> 1 fails, 3 assertions
 *     (relative_name_canonicalises_the_directory_it_slices)
 *     ⛔ Both of those isolations were necessary and each one initially bit:
 *     with the pin live the build aborts and nothing runs, and without the
 *     forced rebuild `./run_924_tests` is the STALE binary and reports 0 failed.
 *     A mutation that "passes" is worth nothing until you have shown the
 *     binary under test was actually rebuilt from the mutant.
 *   - make CRC32_Update restart from CRC32_Init on every call
 *                                                    -> 5 fail
 *     (chunked_equals_whole, chunked_equals_whole_every_length,
 *      zero_length_update_is_identity, header_then_data_is_concatenation,
 *      session_shape_round_trips)
 *   - corrupt ONE entry of the CRC-32 nibble table    -> 1 fails
 *     (zlib_vectors, and ONLY it)
 *   - remove the fold from sd_card_manager.c's SDCardWrite()
 *                                                    -> the BUILD fails, by
 *     the Makefile's first grep guard, before a single assertion runs.
 *
 * THE TWO CRC MUTATIONS ARE THE INTERESTING PAIR, because they are orthogonal
 * and each is the other's blind spot -- which is the argument for carrying
 * both kinds of test, made by measurement rather than by assertion:
 *
 *   - `zlib_vectors` SURVIVES the CRC32_Update mutation. CRC32_Compute folds
 *     in a single Update, so a vectors-only suite would have called that
 *     firmware correct.
 *   - the chunked tests SURVIVE a corrupted table. They compare
 *     accumulate_in_chunks against CRC32_Compute, and BOTH use the same
 *     corrupted table -- so they establish CONSISTENCY, not CORRECTNESS.
 *
 * The chunked tests prove the accumulation model; zlib_vectors proves that
 * what is being accumulated is zlib's CRC-32. Neither is redundant, and the
 * mutation run is what shows it rather than merely claiming it.
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
                              20480u, 0x1A2B3C4Du, false);
    ASSERT_TRUE(n > 0);
    ASSERT_EQ(strcmp(line, "experiment.csv,20480,0x1A2B3C4D\n"), 0);

    /* A rotated part inside a #689 bucket. */
    n = SdManifest_FormatLine(line, sizeof(line), "P001/experiment-65.csv",
                              1u, 0xFFFFFFFFu, false);
    ASSERT_TRUE(n > 0);
    ASSERT_EQ(strcmp(line, "P001/experiment-65.csv,1,0xFFFFFFFF\n"), 0);

    /* #924 round-3 audit HIGH: THE PREFIX MARKER.
     *
     * A write that returns an error may still have put bytes on the card, and
     * the committed count is UNKNOWABLE -- Harmony's SYS_FS_FileWrite
     * overwrites FatFs's partial `*bw` with 0xFFFFFFFF on any error
     * (sys_fs.c:2442-2448) even though f_write keeps it across ABORT
     * (ff.c:4041). So the record describes a PREFIX of the file, and says so.
     *
     * The marker is a FOURTH field, which is the property that matters: a
     * reader indexing [0..2] is unaffected, and the CRC stays VERIFIABLE --
     * truncate the file to `bytes` and the value still matches. A bare
     * "invalid" marker would have thrown that away. */
    n = SdManifest_FormatLine(line, sizeof(line), "experiment.csv",
                              20480u, 0x1A2B3C4Du, true);
    ASSERT_TRUE(n > 0);
    ASSERT_EQ(strcmp(line, "experiment.csv,20480,0x1A2B3C4D,PARTIAL\n"), 0);

    /* NEGATIVE CONTROL, and it is the backward-compatibility claim: the SAME
     * arguments with isPrefix=false must render the UNCHANGED three-field line.
     * Without this, a marker that was emitted unconditionally would pass the
     * assertion above and silently change every record on the card. */
    n = SdManifest_FormatLine(line, sizeof(line), "experiment.csv",
                              20480u, 0x1A2B3C4Du, false);
    ASSERT_TRUE(n > 0);
    ASSERT_EQ(strcmp(line, "experiment.csv,20480,0x1A2B3C4D\n"), 0);

    /* The marker must also be accounted for in the line budget: a
     * maximum-width name plus the marker must still fit SD_MANIFEST_LINE_MAX,
     * which is why that macro carries sizeof(SD_MANIFEST_PREFIX_MARK)-1 as its
     * own named term rather than a rounded-up constant. */
    ASSERT_TRUE(strlen(SD_MANIFEST_PREFIX_MARK) == 8u);
}

TEST(small_crc_is_zero_padded)
{
    char line[128];

    ASSERT_TRUE(SdManifest_FormatLine(line, sizeof(line), "a.csv", 0u, 0u, false) > 0);
    ASSERT_EQ(strcmp(line, "a.csv,0,0x00000000\n"), 0);

    ASSERT_TRUE(SdManifest_FormatLine(line, sizeof(line), "a.csv", 0u, 0xABu, false) > 0);
    ASSERT_EQ(strcmp(line, "a.csv,0,0x000000AB\n"), 0);
}

/* currentFileBytes is uint64_t. A %u/%lu here would silently wrap a file
 * bigger than 4 GB — which is precisely the size file splitting exists for. */
TEST(byte_count_is_64_bit)
{
    char line[128];

    ASSERT_TRUE(SdManifest_FormatLine(line, sizeof(line), "big.csv",
                                      4294967296ull, 0x00000001u, false) > 0);
    ASSERT_EQ(strcmp(line, "big.csv,4294967296,0x00000001\n"), 0);

    ASSERT_TRUE(SdManifest_FormatLine(line, sizeof(line), "big.csv",
                                      18446744073709551615ull, 0u, false) > 0);
    ASSERT_EQ(strcmp(line, "big.csv,18446744073709551615,0x00000000\n"), 0);
}

TEST(line_lengths_are_reported)
{
    char line[128];
    int n = SdManifest_FormatLine(line, sizeof(line), "x.csv", 5u, 0u, false);

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
                              20480u, 0x1A2B3C4Du, false);
    ASSERT_EQ(n, -1);
    ASSERT_EQ(line[0], '\0');

    /* Exactly one byte short is still a refusal, not a silent cut. */
    {
        char tight[sizeof("a.csv,1,0x00000002\n")];   /* == needed size */
        char cramped[sizeof("a.csv,1,0x00000002\n") - 1u];

        ASSERT_TRUE(SdManifest_FormatLine(tight, sizeof(tight), "a.csv", 1u, 2u, false) > 0);
        ASSERT_EQ(strcmp(tight, "a.csv,1,0x00000002\n"), 0);

        ASSERT_EQ(SdManifest_FormatLine(cramped, sizeof(cramped), "a.csv", 1u, 2u, false), -1);
        ASSERT_EQ(cramped[0], '\0');
    }
}

TEST(format_line_null_safety)
{
    char line[64];

    ASSERT_EQ(SdManifest_FormatLine(NULL, sizeof(line), "a", 0u, 0u, false), -1);
    ASSERT_EQ(SdManifest_FormatLine(line, 0u, "a", 0u, 0u, false), -1);
    ASSERT_EQ(SdManifest_FormatLine(line, sizeof(line), NULL, 0u, 0u, false), -1);
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
                    "DAQiFi/exp.csv",
                    sizeof(file0), c0, false) > 0);
    ASSERT_TRUE(SdManifest_FormatLine(l1, sizeof(l1),
                    "DAQiFi/exp-1.csv",
                    sizeof(file1), c1, false) > 0);

    /* #1124 r13 (item 8 = B): the recorded name is the FULL path now. */
    snprintf(expect0, sizeof(expect0), "DAQiFi/exp.csv,%u,0x%08X\n",
             (unsigned)sizeof(file0), (unsigned)c0);
    snprintf(expect1, sizeof(expect1), "DAQiFi/exp-1.csv,%u,0x%08X\n",
             (unsigned)sizeof(file1), (unsigned)c1);
    ASSERT_EQ(strcmp(l0, expect0), 0);
    ASSERT_EQ(strcmp(l1, expect1), 0);
}

/* ================================================================
 * Post-merge adversarial audit on #1124 (verdict issuecomment-5723694027):
 * three confirmed defects, fixed by adding three pure helpers to
 * SdManifest.h that sd_card_manager.c now calls at the exact points the
 * audit named. Each block below tests the helper directly; the Makefile's
 * grep guards (added alongside these tests) are what proves sd_card_manager.c
 * actually calls them, the same limitation the file-level comment above
 * already states for every other guarded call site.
 * ================================================================ */

/* -------------------------------------- defect 2: case-insensitive guard */

/* FatFs's own duplicate-open lock (FF_FS_LOCK) is case-insensitive, so the
 * self-collision guard in sd_OpenSessionManifest() must be too, or a
 * configured stream name differing from the manifest path only in case
 * passes the guard, then loses the FatFs race silently (manifestOpenAttempted
 * latches, no retry, zero integrity records for the whole session). */
TEST(paths_equal_case_insensitive_matches_across_case)
{
    ASSERT_TRUE(SdManifest_PathsEqualCaseInsensitive("DAQiFi/foo.mfst",
                                                      "DAQiFi/foo.mfst"));
    ASSERT_TRUE(SdManifest_PathsEqualCaseInsensitive("DAQiFi/foo.mfst",
                                                      "DAQiFi/foo.MFST"));
    ASSERT_TRUE(SdManifest_PathsEqualCaseInsensitive("daqifi/FOO.MfSt",
                                                      "DAQIFI/foo.mfst"));
}

TEST(paths_equal_case_insensitive_rejects_real_differences)
{
    ASSERT_FALSE(SdManifest_PathsEqualCaseInsensitive("DAQiFi/foo.mfst",
                                                       "DAQiFi/foo.csv"));
    /* A prefix of the other string must not compare equal. */
    ASSERT_FALSE(SdManifest_PathsEqualCaseInsensitive("DAQiFi/foo.mfst",
                                                       "DAQiFi/foo.mfst2"));
    ASSERT_FALSE(SdManifest_PathsEqualCaseInsensitive("DAQiFi/foo.mfst",
                                                       "DAQiFi2/foo.mfst"));
}

TEST(paths_equal_case_insensitive_null_safety)
{
    ASSERT_TRUE(SdManifest_PathsEqualCaseInsensitive(NULL, NULL));
    ASSERT_FALSE(SdManifest_PathsEqualCaseInsensitive(NULL, "a"));
    ASSERT_FALSE(SdManifest_PathsEqualCaseInsensitive("a", NULL));
}

/* ------------------------------ defect 1: refuse to overwrite a non-manifest */

/* A real manifest line, produced by the SAME rendering function this file
 * already pins, must be recognised -- this is the "same base filename across
 * sessions overwrites the earlier MANIFEST" case the module's header accepts
 * and defect 1's fix must not break. */
TEST(first_line_check_accepts_a_real_rendered_line)
{
    char line[128];
    int n = SdManifest_FormatLine(line, sizeof(line), "experiment-3.csv",
                                  20480u, 0x1A2B3C4Du, false);
    ASSERT_TRUE(n > 0);
    ASSERT_TRUE(SdManifest_FirstLineLooksLikeManifest(line, (size_t)n));

    /* A later line in the same file must not matter -- only the FIRST is
     * inspected before deciding whether to overwrite. */
    char twoLines[256];
    int n1 = snprintf(twoLines, sizeof(twoLines), "%s%s", line,
                      "garbage-that-would-fail-on-its-own,,,\n");
    ASSERT_TRUE(n1 > 0);
    ASSERT_TRUE(SdManifest_FirstLineLooksLikeManifest(twoLines, (size_t)n1));
}

/* #924 round-4 audit: THE WRITER GAINED A FOURTH FIELD AND THE READER DID NOT.
 * A format grew `,PARTIAL`, sd_card_manager.c was updated, and this -- the ONE
 * in-repo reader of a manifest line -- was not. A manifest whose FIRST line was
 * a PARTIAL record then stopped looking like a manifest, so the overwrite probe
 * declined and the session silently ran with no manifest.
 *
 * THIS TEST IS A ROUND TRIP ON PURPOSE. Asserting a hand-written string would
 * re-create the same failure the moment the marker's spelling changes: the
 * writer and the test would drift together and the reader would be left behind
 * again. Feeding FormatLine's OWN output to the reader makes writer/reader
 * divergence fail the build, which is the only form that cannot rot. */
TEST(first_line_check_accepts_a_rendered_PARTIAL_line)
{
    char line[128];
    int n = SdManifest_FormatLine(line, sizeof(line), "experiment-3.csv",
                                  20480u, 0x1A2B3C4Du, true);
    ASSERT_TRUE(n > 0);
    /* the marker really is present -- otherwise this passes vacuously by
     * testing the three-field line a second time */
    ASSERT_TRUE(strstr(line, SD_MANIFEST_PREFIX_MARK) != NULL);
    ASSERT_TRUE(SdManifest_FirstLineLooksLikeManifest(line, (size_t)n));

    /* NEGATIVE CONTROLS. The arm accepts THE marker, not any fourth field --
     * otherwise it would wave through a corrupt or foreign line that merely has
     * a comma after the CRC, which is precisely the data-file case the probe
     * exists to catch. */
    static const char wrongMark[] = "exp.csv,20480,0x1A2B3C4D,WRONGMARK\n";
    ASSERT_FALSE(SdManifest_FirstLineLooksLikeManifest(
        wrongMark, sizeof(wrongMark) - 1u));

    static const char truncated[] = "exp.csv,20480,0x1A2B3C4D,PART\n";
    ASSERT_FALSE(SdManifest_FirstLineLooksLikeManifest(
        truncated, sizeof(truncated) - 1u));

    /* the mark present but the newline missing: the line is unterminated, so
     * the record is not complete and must not be trusted */
    static const char noNewline[] = "exp.csv,20480,0x1A2B3C4D,PARTIAL";
    ASSERT_FALSE(SdManifest_FirstLineLooksLikeManifest(
        noNewline, sizeof(noNewline) - 1u));
}

/* #924 round-5 audit: THE OPERAND-LIMIT BOUNDARY, asserted AT the boundary.
 *
 * The defect was an off-by-one in reasoning: a 40-char FILE ending ".csv" gives
 * a 40-char FILE, so `<file>.mfst` is 45 and SD:GET/SD:CRC?/SD:DELete validate
 * their operand at <= 40. The manifest the feature writes could not be fetched
 * by the command that exists to fetch it.
 *
 * An off-by-one is only fixed once its boundary is pinned, so this asserts BOTH
 * SIDES of the exact threshold rather than one comfortable case either side. */
/* #924 round-5 audit (:1969): FatFs NORMALISES, SO THE COMPARATOR MUST TOO.
 *
 * ff.c:2895-2901 inside create_name(), which every open goes through, snips a
 * segment's trailing spaces and dots. So `FILE "foo.mfst "` -- a trailing
 * space, which SD_ValidatePathParam accepts -- opens as `foo.mfst`, and this
 * session's manifest candidate is also `foo.mfst`. A raw byte compare called
 * them unequal, the self-collision guard passed, the overwrite probe saw a
 * zero-size file and permitted it, and the write-open then failed: the silent
 * no-manifest outcome the guard exists to prevent, reachable by one space. */
TEST(paths_equal_normalises_the_way_fatfs_does)
{
    /* THE AUDIT'S CASE */
    ASSERT_TRUE(SdManifest_PathsEqualCaseInsensitive("DAQiFi/foo.mfst ",
                                                     "DAQiFi/foo.mfst"));
    /* dots are snipped with spaces -- same vendor loop, same condition */
    ASSERT_TRUE(SdManifest_PathsEqualCaseInsensitive("DAQiFi/foo.", "DAQiFi/foo"));
    ASSERT_TRUE(SdManifest_PathsEqualCaseInsensitive("foo. . ", "foo"));
    /* PER SEGMENT, not over the whole string: create_name() runs once per
     * component, so a trailing space on a DIRECTORY name counts too. */
    ASSERT_TRUE(SdManifest_PathsEqualCaseInsensitive("DAQiFi /foo.csv",
                                                     "DAQiFi/foo.csv"));
    /* normalisation composes with the case-insensitivity already tested above */
    ASSERT_TRUE(SdManifest_PathsEqualCaseInsensitive("DAQIFI /FOO.MFST.",
                                                     "daqifi/foo.mfst"));

    /* ===== E2: DUPLICATED separators collapse (ff.c:2878) =====
     * Round 6's finding 1. ⛔ WAS reachable from config alone and is NO LONGER,
     * as of #1124: the path was built dir + '/' + file, so a `directory` ending
     * in '/' produced this with no user error (SD_ValidatePathParam's segment
     * checks only test lengths 1 and 2, so a zero-length segment passes).
     * Construction is now canonical, so our own caller cannot produce it.
     *
     * These assertions stay because the function promises equivalence the way
     * FatFs computes it, not over whatever its current single caller passes --
     * see the E2 note in SdManifest.h for why narrowing it would be unsafe and
     * silent. They are no longer a regression test for the constructor; THAT is
     * every_construction_shape_is_slash_safe_with_a_trailing_slash_dir below,
     * which asserts what the constructor emits rather than inferring it from
     * these still passing. */
    ASSERT_TRUE(SdManifest_PathsEqualCaseInsensitive("DAQiFi//foo.csv",
                                                     "DAQiFi/foo.csv"));
    ASSERT_TRUE(SdManifest_PathsEqualCaseInsensitive("DAQiFi///foo.csv",
                                                     "DAQiFi/foo.csv"));

    /* ===== E3: a TERMINATING separator is ignored (ff.c:2880) ===== */
    ASSERT_TRUE(SdManifest_PathsEqualCaseInsensitive("DAQiFi/foo.csv/",
                                                     "DAQiFi/foo.csv"));
    ASSERT_TRUE(SdManifest_PathsEqualCaseInsensitive("DAQiFi/foo.csv//",
                                                     "DAQiFi/foo.csv"));

    /* ===== E4: LEADING separators are stripped (ff.c:3069) =====
     * NOT reachable through SD_ValidatePathParam, which rejects absolute
     * paths -- asserted so the function stays correct independent of callers. */
    ASSERT_TRUE(SdManifest_PathsEqualCaseInsensitive("/DAQiFi/foo.csv",
                                                     "DAQiFi/foo.csv"));

    /* combined: every equivalence at once must still collapse to equal */
    ASSERT_TRUE(SdManifest_PathsEqualCaseInsensitive("//DAQIFI //FOO.CSV. /",
                                                     "daqifi/foo.csv"));

    /* ===== N1: '\\' is a separator to FatFs but NOT here =====
     * ff.c:51 IsSeparator accepts both, and SD_ValidatePathParam rejects '\\'
     * outright, so it cannot reach this function. Pinned as NOT normalised so
     * the decision is visible: if that validator is ever relaxed, this
     * assertion is the one that must change, deliberately. */
    ASSERT_FALSE(SdManifest_PathsEqualCaseInsensitive("DAQiFi\\foo.csv",
                                                      "DAQiFi/foo.csv"));

    /* ===== NEGATIVE CONTROLS ===== */

    /* THE ONE THAT MATTERS MOST: the guard's whole job is to tell a stream file
     * from its own manifest. If normalisation made these equal, every session
     * would refuse its manifest. */
    ASSERT_FALSE(SdManifest_PathsEqualCaseInsensitive("DAQiFi/foo.csv",
                                                      "DAQiFi/foo.mfst"));
    /* only TRAILING runs are snipped -- an embedded space is part of the name */
    ASSERT_FALSE(SdManifest_PathsEqualCaseInsensitive("foo bar", "foobar"));
    /* ...and so is a LEADING one. The vendor loop only decrements the END
     * index (ff.c:2898 `di--`), never advances the start, so the LFN keeps its
     * leading characters. create_name()'s leading-space removal at :2904 is in
     * the 8.3 SFN-generation path and sets NS_LOSS, which is what makes the LFN
     * authoritative -- it does not change the long name's identity. */
    ASSERT_FALSE(SdManifest_PathsEqualCaseInsensitive(" foo", "foo"));
    /* segment COUNT still matters */
    ASSERT_FALSE(SdManifest_PathsEqualCaseInsensitive("a/b", "a/b/c"));
    ASSERT_FALSE(SdManifest_PathsEqualCaseInsensitive("a/b", "a"));
    /* an all-snipped segment must not equal a real name */
    ASSERT_FALSE(SdManifest_PathsEqualCaseInsensitive("foo/   ", "foo/bar"));
    ASSERT_TRUE(SdManifest_PathsEqualCaseInsensitive("foo/   ", "foo/..."));
    /* ⚠ AND THE DISTINCTION ROUND 5's COMMENT GOT WRONG: a segment that is
     * EMPTY AFTER SNIPPING is not the same thing as a separator RUN.
     * "foo/   /bar" has a real middle segment that snips to nothing --
     * FR_INVALID_NAME at ff.c:2901 -- whereas "foo//bar" has no segment there
     * at all and opens foo/bar. They must not compare equal. */
    ASSERT_FALSE(SdManifest_PathsEqualCaseInsensitive("foo/   /bar", "foo/bar"));
    ASSERT_TRUE(SdManifest_PathsEqualCaseInsensitive("foo//bar", "foo/bar"));
}

TEST(manifest_name_must_fit_the_retrieval_operand)
{
    const size_t cap = 40u;                       /* SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX */
    const size_t ext = sizeof(SD_MANIFEST_EXT) - 1u;   /* ".mfst" == 5 */
    ASSERT_EQ(ext, 5u);                           /* else the numbers below mean nothing */

    /* AT the budget: fits, because the operand check is itself `<=`. */
    ASSERT_TRUE(SdManifest_NameFitsOperand(cap - ext, ext, cap));   /* 35 -> "…….mfst" == 40 */
    /* ONE over: refused. This is the case the audit found. */
    ASSERT_FALSE(SdManifest_NameFitsOperand(cap - ext + 1u, ext, cap)); /* 36 -> 41 */

    /* THE AUDIT'S OWN EXAMPLE, end to end: a 40-char FILE "aaa….csv" strips to
     * a 36-char base, which must be refused. */
    ASSERT_FALSE(SdManifest_NameFitsOperand(36u, ext, 40u));

    /* Degenerate inputs, because the subtraction must not wrap: an extension
     * alone longer than the cap has no base short enough to rescue it, and
     * must not underflow into a huge budget that accepts everything. */
    ASSERT_FALSE(SdManifest_NameFitsOperand(0u, 41u, 40u));
    ASSERT_FALSE(SdManifest_NameFitsOperand(1000u, 41u, 40u));
    ASSERT_TRUE(SdManifest_NameFitsOperand(0u, 40u, 40u));   /* exactly consumes it */
    ASSERT_TRUE(SdManifest_NameFitsOperand(0u, 0u, 0u));     /* nothing fits nothing */
}

TEST(first_line_check_accepts_empty_content)
{
    /* Nothing there to be destroyed either way -- an empty existing file
     * (0 bytes) is not distinguishable from "no file", so it is fine to
     * proceed. sd_OpenSessionManifest() itself never even reaches this
     * check for a 0-byte SYS_FS_FSTAT.fsize, but the helper must agree. */
    ASSERT_TRUE(SdManifest_FirstLineLooksLikeManifest("", 0u));
    ASSERT_TRUE(SdManifest_FirstLineLooksLikeManifest(NULL, 0u));
}

/* THE DEFECT 1 SCENARIO ITSELF: an earlier session's real DATA file (not a
 * manifest) sitting at this exact path -- e.g. because ITS configured stream
 * name collided with ITS OWN manifest and the fallback in
 * sd_OpenSessionManifest() renamed the manifest out of the way, leaving the
 * data file at the `<file>.mfst` path a LATER session's manifest now
 * computes. Ordinary logged data does not look like `name,digits,0xHEX\n`. */
TEST(first_line_check_rejects_unrelated_data)
{
    static const char csvHeader[] = "timestamp,ch0,ch1,ch2\n1000,1.23,4.56,7.89\n";
    ASSERT_FALSE(SdManifest_FirstLineLooksLikeManifest(
            csvHeader, sizeof(csvHeader) - 1u));

    static const char binaryish[] = "\x02\x01\x00\x00\x00\xAB\xCD\xEF\x01";
    ASSERT_FALSE(SdManifest_FirstLineLooksLikeManifest(
            binaryish, sizeof(binaryish) - 1u));
}

/* THE FRAGMENT SCENARIO (post-merge audit round 2, defect 1 follow-up): a
 * disk-full mid-write can leave sd_AppendManifestLine()'s OWN bounded
 * completion loop terminating a partial line with a bare '\n' rather than
 * losing it outright (see that function's own comment on
 * SD_MANIFEST_WRITE_TRIES) -- so a file this same feature wrote can itself be
 * left holding a line that is NEITHER a complete manifest line NOR unrelated
 * data. This check must reject it exactly like any other malformed content
 * (accepting a fragment as "looks like a manifest" would let a corrupt file
 * satisfy the guard by accident, which defeats the point) -- callers must not
 * conclude "reject == permanently destroy the path": sd_OpenSessionManifest()
 * responds to a rejection by trying the collision-fallback name instead of
 * giving up, so a fragment here redirects the NEXT session's manifest rather
 * than blocking it forever. */
TEST(first_line_check_rejects_a_disk_full_fragment)
{
    /* Cut mid-number: the write landed "exp-3.csv,204" then nothing more
     * could be written, so the completion loop appended just '\n'. */
    static const char midNumber[] = "exp-3.csv,204\n";
    ASSERT_FALSE(SdManifest_FirstLineLooksLikeManifest(
            midNumber, sizeof(midNumber) - 1u));

    /* Cut right after the first comma. */
    static const char rightAfterComma[] = "exp-3.csv,\n";
    ASSERT_FALSE(SdManifest_FirstLineLooksLikeManifest(
            rightAfterComma, sizeof(rightAfterComma) - 1u));

    /* Cut mid-CRC. */
    static const char midCrc[] = "exp-3.csv,204,0x1A2\n";
    ASSERT_FALSE(SdManifest_FirstLineLooksLikeManifest(
            midCrc, sizeof(midCrc) - 1u));

    /* Cut before anything landed at all -- off == 0 in
     * sd_AppendManifestLine's own accounting, so per its comment NO fragment
     * (not even a bare newline) is written; this is the empty-file case
     * SdManifest_FirstLineLooksLikeManifest already treats as safe (nothing
     * to lose), covered by first_line_check_accepts_empty_content above, not
     * repeated here. */
}

TEST(first_line_check_rejects_near_misses)
{
    /* Missing the second comma. */
    static const char a[] = "name,1230x1A2B3C4D\n";
    ASSERT_FALSE(SdManifest_FirstLineLooksLikeManifest(a, sizeof(a) - 1u));

    /* No digits between the commas. */
    static const char b[] = "name,,0x1A2B3C4D\n";
    ASSERT_FALSE(SdManifest_FirstLineLooksLikeManifest(b, sizeof(b) - 1u));

    /* Missing the "0x" prefix. */
    static const char c[] = "name,123,1A2B3C4D\n";
    ASSERT_FALSE(SdManifest_FirstLineLooksLikeManifest(c, sizeof(c) - 1u));

    /* 7 hex digits, one short. */
    static const char d[] = "name,123,0x1A2B3C4\n";
    ASSERT_FALSE(SdManifest_FirstLineLooksLikeManifest(d, sizeof(d) - 1u));

    /* 9 hex digits, one over -- the byte right after the 8th must be '\n'. */
    static const char e[] = "name,123,0x1A2B3C4D5\n";
    ASSERT_FALSE(SdManifest_FirstLineLooksLikeManifest(e, sizeof(e) - 1u));

    /* No trailing newline at all within the given length. */
    static const char f[] = "name,123,0x1A2B3C4D";
    ASSERT_FALSE(SdManifest_FirstLineLooksLikeManifest(f, sizeof(f) - 1u));

    /* Empty name. */
    static const char g[] = ",123,0x1A2B3C4D\n";
    ASSERT_FALSE(SdManifest_FirstLineLooksLikeManifest(g, sizeof(g) - 1u));
}

/* ======================================================================
 * #1124 r38 -- OUTCOME PINS for the truncated-first-line give-up site.
 *
 * sd_ScanForeignManifests() reads the first chunk of a candidate manifest and
 * asks SdManifest_FirstLineLooksLikeManifest(). That returns false for TWO
 * reasons, and r37 finding (a) was that the call site collapsed them: a real
 * manifest whose first record is longer than the read buffer was skipped as
 * CLEAR, so its recorded names set no foreign bit and rotation could truncate
 * a file the manifest had recorded.
 *
 * These are OUTCOME pins, not structural ones: the discriminator the site now
 * calls is exercised here directly, with the real inputs, so a regression that
 * widens the FAULT arm (refusing ordinary non-manifests) or narrows it
 * (re-swallowing a truncated manifest) fails a test rather than a grep.
 * ====================================================================== */

TEST(r38_truncated_first_line_is_not_fully_visible)
{
    /* THE DEFECT ITSELF. A full read (len == cap) with no terminator in it:
     * more bytes follow that nobody has looked at, so "not a manifest" is an
     * answer about a line we never saw the end of. Must be FAULT-worthy. */
    char chunk[64];
    memset(chunk, 'A', sizeof(chunk));          /* no '\n' anywhere */

    /* The classifier rejects it -- that is the ambiguous verdict ... */
    ASSERT_FALSE(SdManifest_FirstLineLooksLikeManifest(chunk, sizeof(chunk)));
    /* ... and the discriminator says why: we could not see the whole line. */
    ASSERT_FALSE(SdManifest_FirstLineFullyVisible(chunk, sizeof(chunk),
                                                  sizeof(chunk)));

    /* A LONG manifest-shaped first record is the realistic form of it: the
     * name alone overruns the buffer, so the bytes in hand look like nothing. */
    char longName[64];
    memset(longName, 'n', sizeof(longName));
    ASSERT_FALSE(SdManifest_FirstLineLooksLikeManifest(longName,
                                                       sizeof(longName)));
    ASSERT_FALSE(SdManifest_FirstLineFullyVisible(longName, sizeof(longName),
                                                  sizeof(longName)));
}

TEST(r38_short_read_without_terminator_is_fully_visible)
{
    /* No terminator, but the read came up SHORT -- the file ended. The whole
     * first line is in hand, it simply has no '\n'. That is a genuine
     * not-a-manifest and must stay CLEAR (skipped), never FAULT: treating EOF
     * as unreadable would refuse to scan every unterminated foreign file. */
    static const char f[] = "name,123,0x1A2B3C4D";   /* no '\n' */
    ASSERT_FALSE(SdManifest_FirstLineLooksLikeManifest(f, sizeof(f) - 1u));
    ASSERT_TRUE(SdManifest_FirstLineFullyVisible(f, sizeof(f) - 1u, 64u));
}

TEST(r38_terminator_present_is_fully_visible_even_on_a_full_read)
{
    /* A full read is not itself suspicious -- only a full read with nothing
     * to end the first line is. Fill the buffer completely but terminate the
     * first line inside it. */
    char chunk[64];
    memset(chunk, 'x', sizeof(chunk));
    chunk[3] = '\n';
    ASSERT_TRUE(SdManifest_FirstLineFullyVisible(chunk, sizeof(chunk),
                                                 sizeof(chunk)));

    /* Terminator on the very last byte still counts: the line ended. */
    memset(chunk, 'x', sizeof(chunk));
    chunk[sizeof(chunk) - 1u] = '\n';
    ASSERT_TRUE(SdManifest_FirstLineFullyVisible(chunk, sizeof(chunk),
                                                 sizeof(chunk)));
}

TEST(r38_empty_and_null_are_fully_visible)
{
    /* An empty read has no first line to mis-read, and must not be refused --
     * it is the same "nothing to lose" case
     * first_line_check_accepts_empty_content covers for the classifier. */
    ASSERT_TRUE(SdManifest_FirstLineFullyVisible("", 0u, 64u));
    ASSERT_TRUE(SdManifest_FirstLineFullyVisible(NULL, 0u, 64u));

    /* A non-zero length with no buffer is a caller bug, not an EOF: refuse. */
    ASSERT_FALSE(SdManifest_FirstLineFullyVisible(NULL, 8u, 64u));
}

/* ⛔ THE ARM MUST BE EXACTLY AS WIDE AS THE DEFECT. Every input the classifier
 * already rejects for a CONTENT reason has to come back fully-visible, or the
 * new FAULT arm would refuse ordinary foreign files and the scan would stop
 * working on a healthy card. This is the half a structural pin cannot state:
 * it asserts which side of the split each real input lands on. */
TEST(r38_content_rejections_stay_clear_not_fault)
{
    static const char *const seenInFull[] = {
        "timestamp,ch0,ch1,ch2\n1000,1.23,4.56,7.89\n",  /* unrelated data */
        "exp-3.csv,204\n",                               /* disk-full fragment */
        "exp-3.csv,\n",
        "exp-3.csv,204,0x1A2\n",
        "name,1230x1A2B3C4D\n",                          /* near misses */
        "name,,0x1A2B3C4D\n",
        "name,123,1A2B3C4D\n",
        "name,123,0x1A2B3C4\n",
        "name,123,0x1A2B3C4D5\n",
        ",123,0x1A2B3C4D\n",
    };
    size_t i;
    for (i = 0u; i < (sizeof(seenInFull) / sizeof(seenInFull[0])); i++) {
        size_t len = strlen(seenInFull[i]);
        /* Each is rejected ... */
        ASSERT_FALSE(SdManifest_FirstLineLooksLikeManifest(seenInFull[i], len));
        /* ... and each was rejected on CONTENT, having been seen whole, so
         * the site skips it as CLEAR. None of these may become a FAULT. */
        ASSERT_TRUE(SdManifest_FirstLineFullyVisible(seenInFull[i], len, 64u));
    }
}

/* ---------------------------------- defect 3: fresh-file accounting reset */

/* The exact record for a file that was just truncate-opened and never
 * written to: 0 bytes, and a CRC that finalizes to the CRC of nothing --
 * which is what an aborted rotation-open's manifest line must carry for the
 * new, empty file, instead of the file it was rotating away from. */
TEST(fresh_file_accounting_is_zero_bytes_and_empty_crc)
{
    uint64_t bytes = 123456789ull;   /* stale, as if left by a retired file */
    uint32_t crc = 0xDEADBEEFu;      /* stale running CRC of that same file */

    bool isPrefix924 = true;   /* #924 round-3: must be CLEARED by the reset */
    SdManifest_FreshFileAccounting(&bytes, &crc, &isPrefix924);

    ASSERT_EQ(bytes, 0ull);
    /* Finalizing a freshly-armed running CRC must equal the CRC of an empty
     * byte range -- the same value CRC32_Compute("", 0) produces, pinned by
     * zlib_vectors above. */
    ASSERT_EQ(CRC32_Finalize(crc), CRC32_Compute("", 0));
    ASSERT_EQ(CRC32_Finalize(crc), 0x00000000u);
}

/* The reset must compose with the real line renderer exactly the way
 * UNMOUNT_DISK uses it: format a line straight from freshly-armed state. */
TEST(fresh_file_accounting_renders_as_the_empty_file_it_describes)
{
    uint64_t bytes = 999u;
    uint32_t crc = 0x11111111u;
    char line[64];

    bool isPrefix924 = true;   /* #924 round-3: must be CLEARED by the reset */
    SdManifest_FreshFileAccounting(&bytes, &crc, &isPrefix924);
    ASSERT_TRUE(SdManifest_FormatLine(line, sizeof(line), "exp-2.csv", bytes,
                                      CRC32_Finalize(crc), false) > 0);
    ASSERT_EQ(strcmp(line, "exp-2.csv,0,0x00000000\n"), 0);
}

TEST(fresh_file_accounting_null_safety)
{
    uint64_t bytes = 5u;
    uint32_t crc = 5u;

    /* Neither pointer may be dereferenced when NULL -- both call sites in
     * sd_card_manager.c pass real struct-member addresses, but a defensive
     * helper must not assume that forever. */
    SdManifest_FreshFileAccounting(NULL, &crc, NULL);
    ASSERT_EQ(CRC32_Finalize(crc), CRC32_Compute("", 0));

    SdManifest_FreshFileAccounting(&bytes, NULL, NULL);
    ASSERT_EQ(bytes, 0ull);

    SdManifest_FreshFileAccounting(NULL, NULL, NULL);   /* must not crash */
}

/* ==========================================================================
 * #1124: the ONE canonicalising accessor.
 *
 * This is the POSITIVE proof that construction is canonical. The alternative
 * on the table was to delete the comparator's separator-collapsing arms and
 * observe that nothing broke -- but that proves a gap by ABSENCE, and an
 * absence cannot distinguish "unreachable" from "reachable and untested".
 * Asserting what the constructor EMITS is the claim itself.
 * ========================================================================== */

TEST(canonical_dir_len_strips_a_trailing_separator_run)
{
    /* the ordinary case contributes its whole length */
    ASSERT_EQ(SdManifest_CanonicalDirLen("DAQiFi"), 6u);

    /* one separator, and a RUN of them, both collapse to the same answer --
     * the defect is reachable from config alone, so "dir/" is the realistic
     * input and "dir///" is what a careless edit leaves behind */
    ASSERT_EQ(SdManifest_CanonicalDirLen("DAQiFi/"), 6u);
    ASSERT_EQ(SdManifest_CanonicalDirLen("DAQiFi///"), 6u);

    /* BOUNDARIES: a directory that is nothing but separators, and the root.
     * 0 yields an absolute "/name", which is the same path "/" + name denotes
     * -- the only sensible reading of a directory naming the root. */
    ASSERT_EQ(SdManifest_CanonicalDirLen("/"), 0u);
    ASSERT_EQ(SdManifest_CanonicalDirLen("///"), 0u);
    ASSERT_EQ(SdManifest_CanonicalDirLen(""), 0u);
    ASSERT_EQ(SdManifest_CanonicalDirLen(NULL), 0u);

    /* an interior separator is NOT touched -- only the trailing run is */
    ASSERT_EQ(SdManifest_CanonicalDirLen("a/b"), 3u);
    ASSERT_EQ(SdManifest_CanonicalDirLen("a/b/"), 3u);

    /* idempotent: feeding back the canonical prefix changes nothing. This is
     * what makes applying it at a site whose input is already canonical
     * (generateFilename's bucketPath) safe rather than merely harmless. */
    ASSERT_EQ(SdManifest_CanonicalDirLen("DAQiFi"),
              SdManifest_CanonicalDirLen("DAQiFi/"));
}

TEST(every_construction_shape_is_slash_safe_with_a_trailing_slash_dir)
{
    /* The four format shapes sd_card_manager.c actually builds paths with.
     * Each is exercised with a directory ending in '/', which before #1124
     * produced "DAQiFi//..." at every one of them. A doubled separator
     * anywhere in the result fails the test. */
    char out[128];
    const char *dir = "DAQiFi/";
    const int dl = (int)SdManifest_CanonicalDirLen(dir);

    /* plain join -- generateFilename counter == 0, the READ/CRC path, :1213 */
    (void)snprintf(out, sizeof(out), "%.*s/%s", dl, dir, "data.csv");
    ASSERT_TRUE(strstr(out, "//") == NULL);
    ASSERT_BYTES(out, "DAQiFi/data.csv", sizeof("DAQiFi/data.csv"));

    /* rotated part -- generateFilename counter != 0 */
    (void)snprintf(out, sizeof(out), "%.*s/%s-%u%s", dl, dir, "data", 10u, ".csv");
    ASSERT_TRUE(strstr(out, "//") == NULL);
    ASSERT_BYTES(out, "DAQiFi/data-10.csv", sizeof("DAQiFi/data-10.csv"));

    /* the manifest's own name */
    (void)snprintf(out, sizeof(out), "%.*s/%s" SD_MANIFEST_EXT, dl, dir, "data");
    ASSERT_TRUE(strstr(out, "//") == NULL);
    ASSERT_BYTES(out, "DAQiFi/data" SD_MANIFEST_EXT, sizeof("DAQiFi/data" SD_MANIFEST_EXT));

    /* the bucket path -- sd_BuildBucketPath, bucket != 0 */
    (void)snprintf(out, sizeof(out), "%.*s/P%03u", dl, dir, 7u);
    ASSERT_TRUE(strstr(out, "//") == NULL);
    ASSERT_BYTES(out, "DAQiFi/P007", sizeof("DAQiFi/P007"));

    /* and the bucket == 0 arm, which is a COPY and so has no slash for a
     * format-shaped check to key on -- the reason the Makefile pins that one
     * by function name instead */
    (void)snprintf(out, sizeof(out), "%.*s", dl, dir);
    ASSERT_BYTES(out, "DAQiFi", sizeof("DAQiFi"));

    /* a root directory stays absolute rather than losing its separator */
    (void)snprintf(out, sizeof(out), "%.*s/%s",
                   (int)SdManifest_CanonicalDirLen("/"), "/", "data.csv");
    ASSERT_BYTES(out, "/data.csv", sizeof("/data.csv"));
}

TEST(canonical_construction_agrees_with_the_fatfs_comparator)
{
    /* The comparator collapses separator runs because FatFs does (E2). With
     * construction canonical, the two paths it is handed at
     * sd_card_manager.c:2032 are BOTH canonical, so that arm is no longer
     * reachable from our own construction -- but the comparator still has to
     * agree with FatFs, which is why the arm stays. These assert both halves:
     * the canonical pair matches, and so does the doubled one it would have
     * been handed before. */
    ASSERT_TRUE(SdManifest_PathsEqualCaseInsensitive("DAQiFi/data" SD_MANIFEST_EXT,
                                                     "DAQiFi/data" SD_MANIFEST_EXT));
    ASSERT_TRUE(SdManifest_PathsEqualCaseInsensitive("DAQiFi//data" SD_MANIFEST_EXT,
                                                     "DAQiFi/data" SD_MANIFEST_EXT));
}


TEST(a_file_name_must_not_end_in_a_separator)
{
    /* r9 finding 1. The ordinary case. */
    ASSERT_TRUE(SdManifest_NameIsUsable("log.csv"));
    ASSERT_TRUE(SdManifest_NameIsUsable("data"));

    /* THE DEFECT: SD:FILE "log.csv/" was accepted, and the join then produced
     * "<dir>/log.csv/.mfst", which FatFs reads as a directory -- so the
     * session lost its manifest and the diagnostic blamed the open. */
    ASSERT_FALSE(SdManifest_NameIsUsable("log.csv/"));

    /* a run, and a bare separator, are the same mistake */
    ASSERT_FALSE(SdManifest_NameIsUsable("log.csv//"));
    ASSERT_FALSE(SdManifest_NameIsUsable("/"));

    /* nothing is not a file name either */
    ASSERT_FALSE(SdManifest_NameIsUsable(""));
    ASSERT_FALSE(SdManifest_NameIsUsable(NULL));

    /* ⚠ AN INTERIOR SEPARATOR IS LEFT ALONE. SD_ValidatePathParam permits
     * subdirectory components, and whether the LAST component names a file is
     * a different question from whether the path has components. Rejecting
     * these would break a legal operand. */
    ASSERT_TRUE(SdManifest_NameIsUsable("sub/log.csv"));
    ASSERT_TRUE(SdManifest_NameIsUsable("a/b/c.csv"));
}

TEST(the_directory_half_strips_where_the_name_half_refuses)
{
    /* ⛔ THE DESIGN TEST, and the reason the operand matrix had to be settled
     * before any code was written: the SAME trailing '/' means different
     * things on the two halves of one join, so the two halves need OPPOSITE
     * treatments. A future "let's canonicalise every path operand uniformly"
     * is actively wrong, and this test is what should stop it.
     *
     *   as a DIRECTORY, "DAQiFi/" is a redundant spelling of "DAQiFi"  -> STRIP
     *   as a FILE NAME, "DAQiFi/" names no file at all                 -> REFUSE
     */
    const char *const both = "DAQiFi/";

    /* directory half: accepted, and normalises to the same length as the
     * spelling without the separator */
    ASSERT_EQ(SdManifest_CanonicalDirLen(both), 6u);
    ASSERT_EQ(SdManifest_CanonicalDirLen(both), SdManifest_CanonicalDirLen("DAQiFi"));

    /* name half: refused outright -- NOT normalised to "DAQiFi" */
    ASSERT_FALSE(SdManifest_NameIsUsable(both));

    /* and the converse pairing, so neither predicate can be swapped for the
     * other: a plain name is usable, and a plain name's "canonical dir length"
     * is just its length -- the functions are not interchangeable */
    ASSERT_TRUE(SdManifest_NameIsUsable("DAQiFi"));
    ASSERT_EQ(SdManifest_CanonicalDirLen("DAQiFi"), 6u);
}

/* extractBaseFilename's logic, copied verbatim from sd_card_manager.c:1469 so
 * this file can exercise it (that .c drags in Harmony/FreeRTOS/FatFs). If the
 * real one changes, the Makefile's grep guard on it fails the BUILD. */
static void test_extract_base(char *out, size_t outSize, const char *filename)
{
    char *ext;

    strncpy(out, filename, outSize - 1u);
    out[outSize - 1u] = '\0';
    ext = strrchr(out, '.');
    if (ext != NULL) {
        *ext = '\0';
    }
}

TEST(the_manifest_name_is_usable_however_the_file_is_spelled)
{
    /* ⛔ r10 finding 2, and the reason sd_OpenSessionManifest carries NO
     * trailing-separator check. The manifest name is always <base> + ".mfst",
     * so the COMPLETED name ends in the extension and cannot end in '/'
     * whatever the base looks like.
     *
     * r9 validated the stripped INTERMEDIATE instead, and got it wrong BOTH
     * ways. This asserts the measured table, so re-adding that check fails
     * here rather than silently refusing legal configurations. */
    static const struct {
        const char *file;        /* what SD:FILE was set to        */
        const char *base;        /* what extractBaseFilename yields */
        bool baseLooksUsable;    /* what r9's check said about it   */
    } cases[] = {
        { "log.csv",     "log",     true  },
        /* ⛔ THE CASE r9 WAS WRITTEN FOR, AND IT PASSED r9's CHECK: strrchr
         * truncates at the dot and takes the trailing slash with it. */
        { "log.csv/",    "log",     true  },
        /* ⛔ THREE LEGAL CONFIGURATIONS r9 REFUSED. */
        { ".csv",        "",        false },
        { "sub/.csv",    "sub/",    false },
        { "noext/",      "noext/",  false },
        { "sub/log.csv", "sub/log", true  },
        { "noext",       "noext",   true  },
    };
    size_t i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        /* 64 > the firmware's 40-byte cap on this field. The exact cap is in
         * sd_card_manager.h, which is not host-includable (Harmony), and it is
         * irrelevant here: every input below is far shorter, and what is under
         * test is the string logic, not the bound. */
        char base[64];
        char manifest[sizeof(base) + sizeof(SD_MANIFEST_EXT) + 1];

        test_extract_base(base, sizeof(base), cases[i].file);

        /* the derivation is what the table says it is */
        ASSERT_BYTES(base, cases[i].base, strlen(cases[i].base) + 1u);

        /* r9's check on the intermediate -- recorded so the two columns that
         * disagree are visible, not so the behaviour is wanted */
        ASSERT_EQ(SdManifest_NameIsUsable(base) ? 1 : 0,
                  cases[i].baseLooksUsable ? 1 : 0);

        /* ⭐ THE INVARIANT THAT ACTUALLY HOLDS: the completed manifest name is
         * usable in EVERY row, including the three r9 refused. */
        (void)snprintf(manifest, sizeof(manifest), "%s" SD_MANIFEST_EXT, base);
        ASSERT_TRUE(SdManifest_NameIsUsable(manifest));
    }
}

/* ==========================================================================
 * #1124 r11 finding 4 -- a configured directory that carries a bucket's name
 * ========================================================================== */

/* FW_SD_BUCKET_PREFIX is injected by the Makefile, which reads
 * SD_CARD_MANAGER_BUCKET_PREFIX out of the REAL sd_card_manager.h at build
 * time and fails the build if it cannot find it. NOT hand-copied: a copied
 * constant that drifts keeps passing while checking nothing, and this guard's
 * whole correctness is "the prefix here is the prefix buckets are named with".
 * sd_card_manager.h itself is not host-includable (Harmony, FreeRTOS, FatFs). */
#ifndef FW_SD_BUCKET_PREFIX
#error "FW_SD_BUCKET_PREFIX must be -D'd from sd_card_manager.h by the Makefile"
#endif

/* A faithful model of SCPIStorageSD.c's SD_StripConfiguredDir. That file is
 * not host-compilable (libscpi, BoardConfig, the whole SCPI graph), so the
 * RETRIEVAL half of the round trip below is modelled here, the same way
 * test_943/test_970/test_756 model their subjects. The Makefile greps the real
 * function for the shape this mirrors and fails the BUILD if it drifts -- which
 * is what keeps this from becoming a test of its own model. */
static const char *strip_configured_dir_model(const char *p, size_t *pLen,
                                              const char *directory)
{
    size_t dirLen;

    if (p == NULL || pLen == NULL || directory == NULL) {
        return p;
    }
    dirLen = strlen(directory);
    while (dirLen > 0u && directory[dirLen - 1u] == '/') {
        dirLen--;
    }
    if (dirLen == 0u || *pLen <= dirLen + 1u) {
        return p;
    }
    if (strncmp(p, directory, dirLen) != 0 || p[dirLen] != '/') {
        return p;
    }
    *pLen -= (dirLen + 1u);
    p += dirLen + 1u;
    while (*pLen > 1u && *p == '/') {
        p++;
        (*pLen)--;
    }
    return p;
}

/* Record a name the way sd_AppendManifestLine does, then retrieve it the way
 * SD:GET/SD:CRC do, and report the path that would actually be opened. The
 * manifest's one promise is that this is the IDENTITY on the recorded file. */
static void record_then_retrieve(const char *fullPath, const char *dir,
                                 char *recordedOut, size_t recordedLen,
                                 char *openedOut, size_t openedLen)
{
    /* ⛔ #1124 r13 (item 8 = B): RECORD THE FULL PATH. This single line is the
     * whole fix -- the old `SdManifest_RelativeName(fullPath, dir)` stripped
     * the configured directory at RECORD time, and retrieval stripped it
     * again, so the two strips composed into a different file. */
    const char *rec = fullPath;
    size_t len = strlen(rec);
    const char *operand;

    (void)snprintf(recordedOut, recordedLen, "%s", rec);
    operand = strip_configured_dir_model(recordedOut, &len, dir);
    (void)snprintf(openedOut, openedLen, "%.*s/%s",
                   (int)SdManifest_CanonicalDirLen(dir), dir, operand);
}

TEST(a_recorded_name_retrieves_the_file_it_describes)
{
    /* ⛔ THE INVERSE PROPERTY the #924 manifest exists to provide: record a
     * file, hand the recorded name back, and the SAME file opens. This test
     * used to DOCUMENT the failure of that property and then assert the guard
     * that forbade the configuration; operator item 8 = B removed the guard and
     * fixed the property instead, so the test now asserts the property itself.
     *
     * The mechanism, worth stating because it is why dropping the guard is safe
     * rather than merely permitted: r11 recorded names RELATIVE to SD:DIR, and
     * retrieval ALSO strips SD:DIR. Two strips composed. With SD:DIR "P001" the
     * 65th file of a session lands at "P001/P001/exp-64.csv", was recorded as
     * "P001/exp-64.csv", and retrieval stripped the leading "P001/" again and
     * opened "P001/exp-64.csv" -- a different file, or none.
     *
     * Recording the FULL path means exactly ONE strip happens, so the round
     * trip is the identity for EVERY directory, including a bucket-named one.
     * That is a stronger statement than "the bad configuration is refused", and
     * it is what makes the refusal unnecessary rather than merely absent. */
    char recorded[96], opened[96];

    /* (1) the configuration the old guard refused: SD:DIR equal to a bucket's
     *     own name. Bucket 1 is FW_SD_BUCKET_PREFIX "001" = "P001". */
    record_then_retrieve("P001/P001/exp-64.csv", "P001",
                         recorded, sizeof(recorded), opened, sizeof(opened));
    ASSERT_EQ(strcmp(recorded, "P001/P001/exp-64.csv"), 0);
    ASSERT_EQ(strcmp(opened,   "P001/P001/exp-64.csv"), 0);

    /* ⛔ and the DOUBLE-STRIP outcome is now unreachable, asserted by name so a
     *    revert to relative recording fails here and not only in the pins. */
    ASSERT_TRUE(strcmp(opened, "P001/exp-64.csv") != 0);

    /* (2) an ordinary directory, over the same bucket component */
    record_then_retrieve("DAQiFi/P001/exp-64.csv", "DAQiFi",
                         recorded, sizeof(recorded), opened, sizeof(opened));
    ASSERT_EQ(strcmp(recorded, "DAQiFi/P001/exp-64.csv"), 0);
    ASSERT_EQ(strcmp(opened,   "DAQiFi/P001/exp-64.csv"), 0);

    /* (3) a bucket-0 file, where no bucket component appears at all */
    record_then_retrieve("DAQiFi/exp.csv", "DAQiFi",
                         recorded, sizeof(recorded), opened, sizeof(opened));
    ASSERT_EQ(strcmp(recorded, "DAQiFi/exp.csv"), 0);
    ASSERT_EQ(strcmp(opened,   "DAQiFi/exp.csv"), 0);

    /* (4) a directory whose name is a bucket PREFIX but not a bucket, which the
     *     old guard admitted -- kept so the identity is shown to hold across
     *     the whole range the guard used to discriminate, not just at its ends */
    record_then_retrieve("P/P001/exp-1.csv", "P",
                         recorded, sizeof(recorded), opened, sizeof(opened));
    ASSERT_EQ(strcmp(recorded, "P/P001/exp-1.csv"), 0);
    ASSERT_EQ(strcmp(opened,   "P/P001/exp-1.csv"), 0);
}



/* ------------------------------------------------------------------ *
 * #1124 r14: the manifest-open delimiter guard, modelled.
 *
 * sd_card_manager.c is not host-compilable, so the guard's predicate is
 * COPIED here and the Makefile pins the real one -- the same drift-guard
 * arrangement the formats use. Both arms, because r13 put the configured
 * DIRECTORY into every recorded name and the guard had only ever looked at
 * the file.
 * ------------------------------------------------------------------ */
static bool manifest_guard_refuses(const char *dir, const char *file)
{
    return strpbrk(dir, ",\r\n") != NULL || strpbrk(file, ",\r\n") != NULL;
}

/* What ef52cf04a shipped, kept ONLY as the negative control: the test must
 * show the two predicates DISAGREE on the reported case. */
static bool manifest_guard_refuses_r13(const char *dir, const char *file)
{
    (void)dir;
    return strpbrk(file, ",\r\n") != NULL;
}

TEST(a_delimiter_in_the_directory_would_corrupt_the_record)
{
    /* ⛔ THE FINDING. SD_ValidatePathParam rejects control characters,
     * backslash and colon -- but NOT a comma -- so SD:DIR "trial,1" is a
     * configurable value. Since r13 records the FULL path, the line becomes
     * four fields where the format has three, and it PARSES: the name reads
     * as "trial", the byte count as "1/exp.csv", the CRC as "20480". A
     * well-formed-looking lie is worse than a truncation, which is why the
     * guard refuses the sidecar rather than writing it. */
    char line[160];
    int n = SdManifest_FormatLine(line, sizeof(line), "trial,1/exp.csv",
                                  20480u, 0x1A2B3C4Du, false);
    ASSERT_TRUE(n > 0);

    /* count the fields the way a CSV reader would */
    {
        size_t commas = 0; const char *q;
        for (q = line; *q != '\0'; q++) { if (*q == ',') commas++; }
        /* three fields = two commas; the stray one in the directory makes three */
        ASSERT_EQ(commas, 3u);
    }

    /* so the guard must refuse it -- and the r13 guard did NOT */
    ASSERT_TRUE(manifest_guard_refuses("trial,1", "exp.csv"));
    ASSERT_TRUE(manifest_guard_refuses_r13("trial,1", "exp.csv") == false);

    /* the pre-existing half still holds: a comma in the FILE is refused by
     * both, so this change is additive and not a replacement */
    ASSERT_TRUE(manifest_guard_refuses("DAQiFi", "ex,p.csv"));
    ASSERT_TRUE(manifest_guard_refuses_r13("DAQiFi", "ex,p.csv"));

    /* CR and LF are in the set as belt-and-braces against a future
     * relaxation of SD_ValidatePathParam, which rejects them today */
    ASSERT_TRUE(manifest_guard_refuses("tri\ral", "exp.csv"));
    ASSERT_TRUE(manifest_guard_refuses("DAQiFi", "exp\n.csv"));

    /* and an ordinary configuration is admitted, so the guard is not simply
     * refusing everything */
    ASSERT_TRUE(manifest_guard_refuses("DAQiFi", "exp.csv") == false);
    ASSERT_TRUE(manifest_guard_refuses("DAQiFi/sub", "exp.csv") == false);
}

/* ---- #1124 r21: SdManifest_NameHasManifestExt --------------------------
 *
 * A REAL test, not a model: this helper is a pure function in the header, so
 * there is no excuse for asserting against a re-implementation of it.
 *
 * It decides which directory entries the r21 foreign-record scan reads. Both
 * error directions are live:
 *   - too NARROW (e.g. case-sensitive) and a real manifest reads as an
 *     ordinary file, so the scan inspects nothing and the guard silently
 *     passes everything;
 *   - too WIDE and the scan opens and parses arbitrary files, where an
 *     unparseable one is a FAULT and refuses a session that was fine.
 */
TEST(the_manifest_extension_test_accepts_what_we_write) {
    ASSERT_TRUE(SdManifest_NameHasManifestExt("exp.csv" SD_MANIFEST_EXT));
    ASSERT_TRUE(SdManifest_NameHasManifestExt("a" SD_MANIFEST_EXT));
    ASSERT_TRUE(SdManifest_NameHasManifestExt("exp.csv.mfst"));
}

/* FAT does not distinguish case, so an entry reaching us upper-cased is the
 * same file. A case-SENSITIVE test here would make the scan read nothing on a
 * card whose entries come back as 8.3 aliases -- a guard that inspects no
 * manifests passes every session, which is the silent-permissive direction. */
TEST(the_manifest_extension_test_ignores_case) {
    ASSERT_TRUE(SdManifest_NameHasManifestExt("EXP.CSV.MFST"));
    ASSERT_TRUE(SdManifest_NameHasManifestExt("exp.csv.MFST"));
    ASSERT_TRUE(SdManifest_NameHasManifestExt("exp.csv.MfSt"));
}

TEST(the_manifest_extension_test_rejects_everything_else) {
    ASSERT_FALSE(SdManifest_NameHasManifestExt("exp.csv"));
    ASSERT_FALSE(SdManifest_NameHasManifestExt("exp.mfs"));
    ASSERT_FALSE(SdManifest_NameHasManifestExt("exp.mfstx"));
    ASSERT_FALSE(SdManifest_NameHasManifestExt("mfst"));
    ASSERT_FALSE(SdManifest_NameHasManifestExt(""));
    ASSERT_FALSE(SdManifest_NameHasManifestExt(NULL));
    /* ⚠ The bare extension with no stem. Every manifest this firmware creates
     * is <something>.mfst, so a file called exactly ".mfst" is somebody
     * else's; reading it as a manifest would let its contents decide a
     * refusal. */
    ASSERT_FALSE(SdManifest_NameHasManifestExt(SD_MANIFEST_EXT));
}

/* ⭐ The property that actually matters at the call site: whatever name the
 * manifest construction produces, this test must accept its final segment.
 * Keyed off SdManifest_FormatLine's own extension macro rather than a literal,
 * so renaming the extension cannot leave the two disagreeing. */
TEST(the_extension_test_agrees_with_the_manifest_construction) {
    char path[128];
    const char *files[] = { "exp.csv", "log.txt", "a", "sub/exp.csv" };
    for (unsigned i = 0; i < 4; i++) {
        int n = snprintf(path, sizeof(path), "%s" SD_MANIFEST_EXT, files[i]);
        ASSERT_TRUE(n > 0 && (size_t)n < sizeof(path));
        ASSERT_TRUE(SdManifest_NameHasManifestExt(path));
    }
}

/* ---- #1124 r21: SdManifest_PartIndexOf -------------------------------------
 *
 * The pure half of the foreign-record guard, and the reason the expensive
 * question is asked once per session instead of once per rotation. Real tests,
 * not a model: it depends on nothing but <string.h>.
 *
 * Both error directions are live, and they are NOT symmetric:
 *   - a MISS (returning NONE for a name that really is one of our parts) means
 *     the bit is never set, the exact check never runs, and a foreign record is
 *     silently invalidated. Fails OPEN.
 *   - a false HIT only costs one exact scan, which then decides correctly.
 * So the tests below lean on the miss direction.
 */
TEST(part_index_recognises_the_names_a_session_writes) {
    /* index 0 is the configured name itself, directly in the directory */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp.csv", "DAQiFi", "exp.csv"), 0u);
    /* rotation parts, bucket 0 (the directory itself) and a bucket */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-1.csv", "DAQiFi", "exp.csv"), 1u);
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-65.csv", "DAQiFi", "exp.csv"), 65u);
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/P001/exp-65.csv", "DAQiFi", "exp.csv"), 65u);
    /* a trailing slash on the directory must not change the answer */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-7.csv", "DAQiFi/", "exp.csv"), 7u);
    /* a name with no extension at all */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/log-3", "DAQiFi", "log"), 3u);
}

/* FAT is case-insensitive, so a record reaching us upper-cased is the same
 * file. A case-SENSITIVE match here would MISS it -- the fail-open direction. */
TEST(part_index_folds_case_on_both_sides) {
    ASSERT_EQ(SdManifest_PartIndexOf("daqifi/EXP-1.CSV", "DAQiFi", "exp.csv"), 1u);
    ASSERT_EQ(SdManifest_PartIndexOf("DAQIFI/exp-1.csv", "daqifi", "EXP.CSV"), 1u);
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/Exp.Csv", "DAQiFi", "exp.csv"), 0u);
}

TEST(part_index_rejects_names_this_session_cannot_write) {
    /* another capture entirely */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/other.csv", "DAQiFi", "exp.csv"), SDMANIFEST_PART_NONE);
    /* right stem, wrong extension */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-1.txt", "DAQiFi", "exp.csv"), SDMANIFEST_PART_NONE);
    /* not an index */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-x.csv", "DAQiFi", "exp.csv"), SDMANIFEST_PART_NONE);
    /* no index at all */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-.csv", "DAQiFi", "exp.csv"), SDMANIFEST_PART_NONE);
    /* a different directory */
    ASSERT_EQ(SdManifest_PartIndexOf("OTHER/exp-1.csv", "DAQiFi", "exp.csv"), SDMANIFEST_PART_NONE);
    /* deeper than we ever write */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/a/b/exp-1.csv", "DAQiFi", "exp.csv"), SDMANIFEST_PART_NONE);
    /* a padded index is not what "%u" emits, so it is a name we can never
     * open -- NONE. */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-05.csv", "DAQiFi", "exp.csv"), SDMANIFEST_PART_NONE);
    /* ⛔⛔ #1124 r28 AUDIT MEDIUM, BOTH LEGS: THIS LINE USED TO REQUIRE THE
     * DEFECT, and the comment above it argued for the defect.
     *
     * It asserted `exp-0.csv` -> 0u, reasoned as "a bare 0 is legal digits
     * (and still not index 0's name)". The parenthetical is false. 0 IS index
     * 0 everywhere it matters -- it is the bit the scan sets for the
     * UNSUFFIXED target -- so returning it for `exp-0.csv` claimed a foreign
     * record named this session's `exp.csv`. And no session can write
     * "<stem>-0<ext>" at any counter value, because generateFilename's
     * counter==0 arm emits the unsuffixed name.
     *
     * The same misreading in the other consumer was the live half of the
     * finding: sd_NameIsRotationPartOfExistingManifest stripped "-0" and
     * refused a perfectly fresh `exp-0.csv` capture over an unrelated
     * `exp.csv.mfst`, with no opt-in able to override it.
     *
     * ⚠ So this assertion was changed because it was WRONG, not because the
     * fix was inconvenient -- the distinction this row has had to make before.
     * The old value is recorded here so the flip is auditable rather than
     * silent. */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-0.csv", "DAQiFi", "exp.csv"), SDMANIFEST_PART_NONE);
}

/* ⛔⛔ #1124 r23 AUDIT MEDIUM (:424): AN OVERFLOWING INDEX IS SKIPPED, NOT
 * FAULTED -- and the function no longer has a fault return at all.
 *
 * It used to set a `bool *fault` here, which the caller turned into a refusal,
 * so a foreign file called `exp-4294967296.csv` disarmed every capture in the
 * directory. The old reasoning was that an unrepresentable index means "we
 * cannot tell whether it is one of ours". We can tell, and the answer is no:
 * one session produces at most (MAX_BUCKET + 1) * MAX_DIR_FILES = 4160 parts,
 * so a value that does not fit a uint32_t is nowhere near a name we open.
 *
 * ⭐ THE INVARIANT this now encodes: A FOREIGN NAME CAN NEVER FAULT THE SCAN.
 * Only unreadable or unparseable manifest CONTENT may. A name is an INPUT --
 * its length, digits and spelling are the outside world's choice, and none of
 * them is an error condition for us. Three rounds produced three violations of
 * exactly this. */
TEST(part_index_skips_an_overflowing_index_rather_than_faulting) {
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-99999999999.csv", "DAQiFi", "exp.csv"), SDMANIFEST_PART_NONE);
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-4294967296.csv", "DAQiFi", "exp.csv"), SDMANIFEST_PART_NONE);
    /* the sentinel value itself, which cannot be returned as a real index */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-4294967295.csv", "DAQiFi", "exp.csv"), SDMANIFEST_PART_NONE);
    /* and absurd digit counts, which are just long strings */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-000000000000000000001.csv", "DAQiFi", "exp.csv"), SDMANIFEST_PART_NONE);
    /* ⛔ r28: THE BOUNDARY MOVED FROM uint32_t's TO OURS, and this assertion
     * moved with it. It used to require 4294967294u -- "the boundary stays a
     * NUMBER on the representable side" -- which was the right INVARIANT
     * (never a fault) expressed against the wrong ceiling. uint32_t is not a
     * property of this firmware; SDMANIFEST_PART_INDEX_MAX is, and it is
     * cross-tied by a _Static_assert to the highest index a session can write.
     *
     * ⭐ THE INVARIANT THIS TEST EXISTS FOR IS UNCHANGED AND STILL ASSERTED:
     * an out-of-range index is SKIPPED (NONE), never a fault, and the function
     * still has no fault channel to use. What changed is only where "out of
     * range" begins -- and narrowing it cannot lose a collision, because a
     * session whose own counter passes the limit falls back to the per-open
     * exact content scan (sd_card_manager.c's above-limit route). */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-4294967294.csv", "DAQiFi", "exp.csv"), SDMANIFEST_PART_NONE);
    /* the real boundary, both sides, by the one constant that defines it */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-10063.csv", "DAQiFi", "exp.csv"), SDMANIFEST_PART_INDEX_MAX);
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-10064.csv", "DAQiFi", "exp.csv"), SDMANIFEST_PART_NONE);
}

TEST(part_index_is_null_safe) {
    ASSERT_EQ(SdManifest_PartIndexOf(NULL, "DAQiFi", "exp.csv"), SDMANIFEST_PART_NONE);
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-1.csv", NULL, "exp.csv"), SDMANIFEST_PART_NONE);
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-1.csv", "DAQiFi", NULL), SDMANIFEST_PART_NONE);
}

/* ⭐⭐ THE ROUND TRIP, and the only arm that ties this parser to the FORMATTER.
 *
 * Every other test above encodes my belief about what generateFilename emits.
 * This one builds the name the same way it does -- "%.*s/%s-%u%s" for a part
 * and "%.*s/%s" for index 0 -- and requires the parser to recover the index.
 * If the formatter is ever changed, THIS is the arm that notices; a pin on the
 * format string would only notice an edit to the string it names.
 *
 * Checked across bucket 0 and a bucket, and over index boundaries where an
 * unpadded "%u" changes width. */
TEST(part_index_round_trips_the_filename_formatter) {
    const uint32_t idxs[] = { 1u, 2u, 9u, 10u, 11u, 64u, 65u, 99u, 100u, 999u,
                              1000u, 4096u, 4160u };
    const char *dir = "DAQiFi";
    const char *file = "exp.csv";
    const char *stem = "exp";
    const char *ext = ".csv";
    char path[128];
    unsigned i;
    int n;

    /* index 0 lands directly in the directory */
    n = snprintf(path, sizeof(path), "%s/%s", dir, file);
    ASSERT_TRUE(n > 0 && (size_t)n < sizeof(path));
    ASSERT_EQ(SdManifest_PartIndexOf(path, dir, file), 0u);

    for (i = 0; i < (sizeof(idxs) / sizeof(idxs[0])); i++) {
        /* bucket 0: the bucket path IS the directory */
        n = snprintf(path, sizeof(path), "%s/%s-%u%s", dir, stem,
                     (unsigned)idxs[i], ext);
        ASSERT_TRUE(n > 0 && (size_t)n < sizeof(path));
        ASSERT_EQ(SdManifest_PartIndexOf(path, dir, file), idxs[i]);
        /* bucket n: "<dir>/P%03u" */
        n = snprintf(path, sizeof(path), "%s/P%03u/%s-%u%s", dir, 1u, stem,
                     (unsigned)idxs[i], ext);
        ASSERT_TRUE(n > 0 && (size_t)n < sizeof(path));
        ASSERT_EQ(SdManifest_PartIndexOf(path, dir, file), idxs[i]);
    }
}

/* ⛔ THE COLLISION THAT MOTIVATES THE WHOLE GUARD, as a test rather than prose.
 *
 * A foreign session configured "exp-1.csv" records exactly that name, in
 * exp-1.csv.mfst -- a DIFFERENT manifest from ours, so neither the
 * manifest-name check nor r20's recorded-part check can see it -- and it is
 * OUR part 1. This is the record whose index must be recognised. */
TEST(a_foreign_base_name_that_is_one_of_our_parts_is_recognised) {
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-1.csv", "DAQiFi", "exp.csv"), 1u);
    /* ...and symmetrically, OUR base name is THEIR part 1, which is the case
     * r20's sd_NameIsRotationPartOfExistingManifest covers from the other
     * side. Both directions exist; neither subsumes the other. */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-1-1.csv", "DAQiFi", "exp-1.csv"), 1u);
}

/* ⛔⛔ #1124 r21 AUDIT HIGH (:378): THE INVERSE PROPERTY, OVER A SET OF
 * (stem, N, ext) THAT INCLUDES AN EMPTY STEM.
 *
 * The audit's finding: PartIndexOf returned NONE for an empty stem, while the
 * setter accepts ".csv" and the generator emits "-1.csv". So the function was
 * not the generator's inverse, a real part name read as "not one of ours", the
 * bit was never set and the collision went unseen -- the fail-OPEN direction.
 *
 * The earlier round_trips test used one stem and could not see this. This one
 * is the property the audit asked for, stated over a SET: for every (stem, N,
 * ext) the generator can produce, PartIndexOf(generate(...)) == N. An empty
 * stem and an empty extension are in the set because the setter admits both.
 */
TEST(part_index_inverts_the_generator_over_a_set_of_stems_and_extensions) {
    struct { const char *stem; const char *ext; } cases[] = {
        { "exp",  ".csv" },
        { "e",    ".csv" },
        { "",     ".csv" },   /* ⛔ the audit's case: SD:FILE ".csv" */
        { "log",  ".txt" },
        { "log",  ""     },   /* no extension at all */
        { "",     ".p"   },
        { "a.b",  ".csv" },   /* a dot inside the stem */
    };
    const uint32_t idxs[] = { 1u, 9u, 10u, 64u, 65u, 999u, 1000u, 4160u };
    const char *dir = "DAQiFi";
    char file[64], path[160];
    unsigned c, i;

    for (c = 0; c < (sizeof(cases) / sizeof(cases[0])); c++) {
        /* the configured name is exactly stem + ext */
        int fn = snprintf(file, sizeof(file), "%s%s", cases[c].stem, cases[c].ext);
        ASSERT_TRUE(fn > 0 && (size_t)fn < sizeof(file));

        /* index 0: generateFilename's counter==0 arm is "<dir>/<file>" */
        ASSERT_TRUE(snprintf(path, sizeof(path), "%s/%s", dir, file) > 0);
        ASSERT_EQ(SdManifest_PartIndexOf(path, dir, file), 0u);

        for (i = 0; i < (sizeof(idxs) / sizeof(idxs[0])); i++) {
            /* parts: "%.*s/%s-%u%s" over the bucket path, bucket 0 and bucket n */
            ASSERT_TRUE(snprintf(path, sizeof(path), "%s/%s-%u%s", dir,
                                 cases[c].stem, (unsigned)idxs[i],
                                 cases[c].ext) > 0);
            ASSERT_EQ(SdManifest_PartIndexOf(path, dir, file), idxs[i]);

            ASSERT_TRUE(snprintf(path, sizeof(path), "%s/P%03u/%s-%u%s", dir, 2u,
                                 cases[c].stem, (unsigned)idxs[i],
                                 cases[c].ext) > 0);
            ASSERT_EQ(SdManifest_PartIndexOf(path, dir, file), idxs[i]);
        }
    }
}

/* ⭐ The other half of the audit's property: a NEAR MISS must be NONE. Without
 * this the inverse property above is satisfied by a function that says "yes,
 * index N" to everything, which would refuse every session for no reason. */
TEST(part_index_says_none_to_a_near_miss) {
    /* empty stem, and these are the near misses that matter for it */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/.csv", "DAQiFi", ".csv"), 0u);
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/-1.csv", "DAQiFi", ".csv"), 1u);
    /* ...but a stem where the empty-stem session expects none */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/x-1.csv", "DAQiFi", ".csv"), SDMANIFEST_PART_NONE);
    /* wrong extension against an empty stem */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/-1.txt", "DAQiFi", ".csv"), SDMANIFEST_PART_NONE);
    /* the separator itself missing */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/1.csv", "DAQiFi", ".csv"), SDMANIFEST_PART_NONE);
    /* a no-extension session must not swallow an extended name */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/log-1.csv", "DAQiFi", "log"), SDMANIFEST_PART_NONE);
    /* ...and vice versa */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/log-1", "DAQiFi", "log.csv"), SDMANIFEST_PART_NONE);
    /* a stem that is a PREFIX of ours is not ours */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/ex-1.csv", "DAQiFi", "exp.csv"), SDMANIFEST_PART_NONE);
    /* ...nor one that EXTENDS ours */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/expx-1.csv", "DAQiFi", "exp.csv"), SDMANIFEST_PART_NONE);
}

/* ⛔⛔ #1124 r22 AUDIT MEDIUM (:2871): A LARGE INDEX IS A NUMBER, NOT A FAULT.
 *
 * The parser knows nothing about any session's part LIMIT, and must not: a
 * foreign session is entitled to a file called exp-5000.csv, and r21 FAULTed
 * on one -- which, FAULT being a refusal, disarmed every capture in that
 * directory. The parser's job is to say which index the name encodes; deciding
 * that an index is out of OUR range belongs to the caller, as a skip.
 *
 * So: a big index parses cleanly, and only an UNREPRESENTABLE one faults. The
 * boundary cases around the firmware's own limit (4161) are here to make the
 * separation of concerns explicit rather than incidental. */
TEST(part_index_treats_a_large_index_as_a_number_not_a_fault) {
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-5000.csv", "DAQiFi", "exp.csv"), 5000u);
    /* at, and either side of, the OLD 4160 part limit -- all just numbers, and
     * all still well inside the real ceiling */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-4160.csv", "DAQiFi", "exp.csv"), 4160u);
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-4161.csv", "DAQiFi", "exp.csv"), 4161u);
    /* ⛔ r28: 999999 is now out of range, so it is SKIPPED rather than
     * returned. The test's name is still exactly right -- it is a NUMBER, not
     * a FAULT -- and that is what this asserts: a value past the ceiling comes
     * back as NONE through the ordinary return, with no fault channel involved
     * and no refusal caused. Only the ceiling changed, from uint32_t's to the
     * firmware's own (SDMANIFEST_PART_INDEX_MAX). */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-999999.csv", "DAQiFi", "exp.csv"), SDMANIFEST_PART_NONE);
    /* ...and in a bucket, which is where a long foreign session's parts live */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/P078/exp-5000.csv", "DAQiFi", "exp.csv"), 5000u);
}

/* ⛔⛔ #1124 r22 FOLLOW-UP: THE TWO STAGES OF THE GATE MUST AGREE.
 *
 * The guard is bit-then-compare: SdManifest_PartIndexOf decides whether
 * SdManifest_PathsEqualCaseInsensitive is ever consulted. So the prefilter must
 * recognise every spelling the comparator considers equal, or the careful stage
 * is simply skipped and the guard's strength collapses to the cheap one's.
 *
 * ⭐ THIS IS THE PROPERTY, not a list of cases: for each spelling the
 * COMPARATOR calls equal to the canonical path, the PREFILTER must return the
 * SAME index. Both halves are asserted on every row, so the test cannot pass by
 * the comparator and the prefilter being wrong together -- and a future
 * equivalence added to one and not the other fails here.
 */
TEST(the_prefilter_recognises_every_spelling_the_comparator_calls_equal) {
    const char *canonical = "DAQiFi/exp-1.csv";
    const char *spellings[] = {
        "DAQiFi/exp-1.csv",         /* the canonical form itself */
        "DAQiFi//exp-1.csv",        /* E2 duplicated separator */
        "/DAQiFi/exp-1.csv",        /* E4 leading separator */
        "DAQiFi/exp-1.csv/",        /* E3 terminating separator */
        "DAQiFi/exp-1.csv.",        /* E1 trailing dot */
        "DAQiFi/exp-1.csv ",        /* E1 trailing space */
        "DAQiFi./exp-1.csv",        /* E1 on the DIRECTORY segment */
        "DAQIFI/EXP-1.CSV",         /* E5 case */
        "//DAQiFi///exp-1.csv//",   /* all of them at once */
    };
    unsigned i;

    for (i = 0; i < (sizeof(spellings) / sizeof(spellings[0])); i++) {
        /* the comparator calls it the same path... */
        ASSERT_TRUE(SdManifest_PathsEqualCaseInsensitive(spellings[i], canonical));
        /* ...so the prefilter must give it the same index */
        ASSERT_EQ(SdManifest_PartIndexOf(spellings[i], "DAQiFi", "exp.csv"), 1u);
    }
}

/* The same agreement with the DIRECTORY spelled oddly, which is the half r21
 * got wrong by comparing the configured directory as one string. */
TEST(the_prefilter_agrees_however_the_configured_directory_is_spelled) {
    const char *dirs[] = { "DAQiFi", "DAQiFi/", "DAQiFi//", "/DAQiFi", "DAQiFi." };
    unsigned i;
    for (i = 0; i < (sizeof(dirs) / sizeof(dirs[0])); i++) {
        ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-1.csv", dirs[i], "exp.csv"), 1u);
        ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp.csv", dirs[i], "exp.csv"), 0u);
        ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/P003/exp-9.csv", dirs[i], "exp.csv"), 9u);
    }
    /* and the configured NAME spelled with a trailing dot is the same name */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp-1.csv", "DAQiFi", "exp.csv."), 1u);
}

/* ⭐ THE CONTROL. Normalising must not make UNRELATED names match -- otherwise
 * the two arms above are satisfied by a prefilter that says "index 1" to
 * everything, which would refuse ordinary sessions. Each row here is a name the
 * comparator also calls DIFFERENT from the canonical one. */
TEST(normalisation_does_not_make_unrelated_names_match) {
    const char *canonical = "DAQiFi/exp-1.csv";
    const char *others[] = {
        "DAQiFi/other.csv.",
        "DAQiFi//other-1.csv",
        "OTHER/exp-1.csv",
        "DAQiFi/sub/other/exp-1.csv",   /* deeper than we write */
        "DAQiFi/exp-1.txt",
    };
    unsigned i;
    for (i = 0; i < (sizeof(others) / sizeof(others[0])); i++) {
        ASSERT_FALSE(SdManifest_PathsEqualCaseInsensitive(others[i], canonical));
        ASSERT_EQ(SdManifest_PartIndexOf(others[i], "DAQiFi", "exp.csv"),
                  SDMANIFEST_PART_NONE);
    }
}

/* ⛔ #1124 r23 AUDIT HIGH (:346): A DOUBLED SEPARATOR INSIDE A MULTI-SEGMENT
 * DIRECTORY -- "D//sub" -- which is the exact shape the audit named.
 *
 * The r22-follow-up rows cover a doubled separator between the directory and
 * the leaf; this covers one BETWEEN TWO DIRECTORY SEGMENTS, which the
 * segment-wise walk handles by the same mechanism but which no existing row
 * exercised. Asserted the same way: the comparator calls it the same path, so
 * the prefilter must give it the same index. */
TEST(a_doubled_separator_inside_the_directory_is_recognised) {
    const char *canonical = "DAQiFi/sub/exp-1.csv";
    const char *spellings[] = {
        "DAQiFi//sub/exp-1.csv",        /* the audit's shape */
        "DAQiFi/sub//exp-1.csv",
        "DAQiFi//sub//exp-1.csv",
        "/DAQiFi//sub/exp-1.csv/",
        "DAQiFi/./sub/exp-1.csv",       /* NOT equivalent -- see below */
    };
    unsigned i;
    /* the first four are the same path to the comparator AND to the prefilter */
    for (i = 0; i < 4; i++) {
        ASSERT_TRUE(SdManifest_PathsEqualCaseInsensitive(spellings[i], canonical));
        ASSERT_EQ(SdManifest_PartIndexOf(spellings[i], "DAQiFi/sub", "exp.csv"), 1u);
    }
    /* ⚠ A '.' COMPONENT IS NOT HANDLED, by either stage, and that is
     * deliberate and consistent: FatFs's create_name() does not collapse it,
     * the comparator's enumerated equivalences (E1-E5) do not include it, and
     * the overwrite opt-in is REFUSED outright for a configured directory
     * containing one (SD_DirHasNonCanonicalForm). So both stages agree it is a
     * different path, which is the property that matters -- the two stages
     * never disagree, whatever the answer. */
    ASSERT_FALSE(SdManifest_PathsEqualCaseInsensitive(spellings[4], canonical));
    ASSERT_EQ(SdManifest_PartIndexOf(spellings[4], "DAQiFi/sub", "exp.csv"),
              SDMANIFEST_PART_NONE);

    /* and the directory given with the doubled separator instead */
    ASSERT_EQ(SdManifest_PartIndexOf(canonical, "DAQiFi//sub", "exp.csv"), 1u);
    ASSERT_EQ(SdManifest_PartIndexOf(canonical, "DAQiFi/sub/", "exp.csv"), 1u);
}

/* ⛔⛔ #1124 r24 AUDIT HIGH (:479): AN UNSUFFIXED FIRST FILE CAN LIVE IN A
 * BUCKET, and I had asserted the opposite.
 *
 * The removed row above read: "index 0 ONLY directly in the directory --
 * generateFilename's counter==0 arm runs exactly once, while bucket 0 IS the
 * directory". The first clause is true; the conclusion does not follow. It
 * confuses "the counter is 0 only once" with "the counter is 0 only in bucket
 * 0".
 *
 * MEASURED at sd_card_manager.c: the bucket-advance loop runs BEFORE
 * generateFilename and advances whenever the current bucket is full, including
 * on the session's FIRST file. A configured directory already holding
 * MAX_DIR_FILES files makes the session start in P001 and write
 * `D/P001/exp.csv`.
 *
 * ⚠ So the test ENCODED MY WRONG BELIEF and defended it. A fixture asserting
 * that a real behaviour cannot happen is worse than no fixture: it makes the
 * fail-open look deliberate to the next reader, which is exactly how this
 * survived a round. */
TEST(an_unsuffixed_first_file_inside_a_bucket_is_index_zero) {
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/P001/exp.csv", "DAQiFi", "exp.csv"), 0u);
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/P064/exp.csv", "DAQiFi", "exp.csv"), 0u);
    /* still index 0 directly in the directory, which is the common case */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/exp.csv", "DAQiFi", "exp.csv"), 0u);
    /* and the normalised spellings of the bucketed form agree, as everywhere */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi//P001/exp.csv", "DAQiFi", "exp.csv"), 0u);
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/P001/exp.csv.", "DAQiFi", "exp.csv"), 0u);
    /* ⭐ the control: a DIFFERENT name in a bucket is still not ours */
    ASSERT_EQ(SdManifest_PartIndexOf("DAQiFi/P001/other.csv", "DAQiFi", "exp.csv"),
              SDMANIFEST_PART_NONE);
    /* ...and the two stages still agree about the bucketed form */
    ASSERT_TRUE(SdManifest_PathsEqualCaseInsensitive("DAQiFi//P001/exp.csv",
                                                     "DAQiFi/P001/exp.csv"));
}

/* ⛔⛔ #1124 r28 AUDIT HIGH (:469): THE CONFIGURED NAME MAY CONTAIN SEPARATORS.
 *
 * SD:FILE accepts "P001/exp.csv" (the setter permits '/'), and generateFilename
 * strips only the last extension for baseFilename, so the session writes
 *     <dir> / [bucket] / <file's own directories> / <leaf>
 * -- note the BUCKET COMES FIRST, before the name's own subdirectory, because
 * generateFilename joins onto bucketPath.
 *
 * r27 treated `file` as ONE segment, so every one of these was NONE: names the
 * session WRITES, unmatched, no bit set, exact check never run. Fail-OPEN, and
 * independent of where the foreign manifest lived -- this failed even for a
 * directory the scan DOES read, which is why it is a bug and not the
 * subdirectory residual. */
TEST(a_configured_name_containing_separators_is_matched) {
    ASSERT_EQ(SdManifest_PartIndexOf("D/P001/exp.csv", "D", "P001/exp.csv"), 0u);
    ASSERT_EQ(SdManifest_PartIndexOf("D/P001/exp-1.csv", "D", "P001/exp.csv"), 1u);
    /* the bucket sits BEFORE the name's own directory */
    ASSERT_EQ(SdManifest_PartIndexOf("D/P002/P001/exp-7.csv", "D", "P001/exp.csv"), 7u);
    /* deeper configured names, and a two-level one */
    ASSERT_EQ(SdManifest_PartIndexOf("D/a/b/exp-3.csv", "D", "a/b/exp.csv"), 3u);
    ASSERT_EQ(SdManifest_PartIndexOf("D/P005/a/b/exp.csv", "D", "a/b/exp.csv"), 0u);
    /* and the normalised spellings still agree */
    ASSERT_EQ(SdManifest_PartIndexOf("D//P001/exp-1.csv", "D", "P001/exp.csv"), 1u);
    ASSERT_EQ(SdManifest_PartIndexOf("D/P001/exp-1.csv", "D", "P001//exp.csv"), 1u);
}

/* ⭐ THE CONTROLS. Without these the fix above is satisfied by a matcher that
 * ignores the directory part of the name entirely. */
TEST(a_configured_name_with_separators_still_rejects_near_misses) {
    /* right leaf, WRONG intermediate directory */
    ASSERT_EQ(SdManifest_PartIndexOf("D/P002/exp-1.csv", "D", "P001/exp.csv"),
              SDMANIFEST_PART_NONE);
    /* the name's directory missing altogether */
    ASSERT_EQ(SdManifest_PartIndexOf("D/exp-1.csv", "D", "P001/exp.csv"),
              SDMANIFEST_PART_NONE);
    /* a different leaf under the right directory */
    ASSERT_EQ(SdManifest_PartIndexOf("D/P001/other.csv", "D", "P001/exp.csv"),
              SDMANIFEST_PART_NONE);
    /* TWO intermediate segments where only one bucket is possible */
    ASSERT_EQ(SdManifest_PartIndexOf("D/P002/P003/P001/exp-1.csv", "D", "P001/exp.csv"),
              SDMANIFEST_PART_NONE);
    /* ⚠ AND THE ROW THAT WAS HERE WAS WRONG, caught by its own failure.
     * I asserted PartIndexOf("D/P001/exp-1.csv", "D", "exp.csv") == NONE on
     * the idea that a single-segment configured name admits no intermediate
     * directory. It admits exactly one -- the ROTATION BUCKET -- so that name
     * IS our part 1 in P001, and the code was right.
     *
     * Second time this round's lesson has caught me: a test asserting that a
     * real behaviour cannot happen (r24 :479 was the first). Keeping the
     * correct expectation here, and the genuine near-miss below it. */
    ASSERT_EQ(SdManifest_PartIndexOf("D/P001/exp-1.csv", "D", "exp.csv"), 1u);
    /* TWO intermediate segments, where only one bucket is possible */
    ASSERT_EQ(SdManifest_PartIndexOf("D/P002/P003/exp-1.csv", "D", "exp.csv"),
              SDMANIFEST_PART_NONE);
}

/* ⛔ #1124 r28 AUDIT MEDIUM (:2728): A PADDED SUFFIX IS NOT OUR PART.
 * generateFilename uses "%u", which never pads, so `exp-01.csv` is a name this
 * firmware cannot produce. PartIndexOf has rejected it since r21; this asserts
 * it stays rejected, because the sibling consumer in sd_card_manager.c did NOT
 * have the rule and refused a fresh exp-01.csv over an unrelated exp.csv.mfst. */
TEST(a_padded_index_is_never_one_of_our_parts) {
    ASSERT_EQ(SdManifest_PartIndexOf("D/exp-01.csv", "D", "exp.csv"), SDMANIFEST_PART_NONE);
    ASSERT_EQ(SdManifest_PartIndexOf("D/exp-007.csv", "D", "exp.csv"), SDMANIFEST_PART_NONE);
    ASSERT_EQ(SdManifest_PartIndexOf("D/P001/exp-01.csv", "D", "exp.csv"), SDMANIFEST_PART_NONE);
    /* ⛔ r28: "-0" JOINS THE PADDED CASES RATHER THAN BEING THE EXCEPTION TO
     * THEM. This line used to assert 0u, under "but a bare 0 ... is fine". It
     * is not fine: counter 0 emits the UNSUFFIXED name, so no session writes
     * "<stem>-0<ext>", and returning 0 handed a foreign record the index of
     * this session's own unsuffixed target. One leading-zero test now covers
     * both, which is also why the shared parser tests `d[0] == '0'` alone
     * instead of the old `d[0] == '0' && run > 1`. */
    ASSERT_EQ(SdManifest_PartIndexOf("D/exp-0.csv", "D", "exp.csv"), SDMANIFEST_PART_NONE);
    /* ...ordinary indices are unaffected */
    ASSERT_EQ(SdManifest_PartIndexOf("D/exp-10.csv", "D", "exp.csv"), 10u);
}

/* ⛔⛔⛔ #1124 r28: THE SHARED PART-SUFFIX PARSER, TESTED AS ITSELF.
 *
 * It is tested directly as well as through SdManifest_PartIndexOf because it
 * now has TWO consumers and the second one (the manager's recorded-part check)
 * is not host-includable. Testing it here is the only place both consumers'
 * shared behaviour can be pinned by value rather than by grep. */
TEST(part_suffix_parser_accepts_only_what_the_formatter_emits) {
    const char *d = "1";
    ASSERT_EQ(SdManifest_ParsePartIndexDigits(d, d + 1), 1u);
    d = "42";
    ASSERT_EQ(SdManifest_ParsePartIndexDigits(d, d + 2), 42u);
    /* padding: "%u" never emits it */
    d = "01";
    ASSERT_EQ(SdManifest_ParsePartIndexDigits(d, d + 2), SDMANIFEST_PART_NONE);
    d = "007";
    ASSERT_EQ(SdManifest_ParsePartIndexDigits(d, d + 3), SDMANIFEST_PART_NONE);
    /* the lone zero: counter 0 emits the UNSUFFIXED name, so this is no part */
    d = "0";
    ASSERT_EQ(SdManifest_ParsePartIndexDigits(d, d + 1), SDMANIFEST_PART_NONE);
    /* non-digits */
    d = "1x";
    ASSERT_EQ(SdManifest_ParsePartIndexDigits(d, d + 2), SDMANIFEST_PART_NONE);
    /* empty run, and a reversed/degenerate range */
    d = "1";
    ASSERT_EQ(SdManifest_ParsePartIndexDigits(d, d), SDMANIFEST_PART_NONE);
    ASSERT_EQ(SdManifest_ParsePartIndexDigits(d + 1, d), SDMANIFEST_PART_NONE);
    ASSERT_EQ(SdManifest_ParsePartIndexDigits(NULL, NULL), SDMANIFEST_PART_NONE);
}

/* ⭐ THE RANGE IS THE POST-CONDITION, and it is asserted at both edges by the
 * one constant that defines it -- not by a literal, so moving the ceiling
 * cannot leave this test asserting the old one. */
TEST(part_suffix_parser_bounds_the_index_to_what_a_session_can_write) {
    char buf[32];
    uint32_t i;

    snprintf(buf, sizeof(buf), "%u", (unsigned)SDMANIFEST_PART_INDEX_MAX);
    ASSERT_EQ(SdManifest_ParsePartIndexDigits(buf, buf + strlen(buf)),
              SDMANIFEST_PART_INDEX_MAX);
    snprintf(buf, sizeof(buf), "%u", (unsigned)SDMANIFEST_PART_INDEX_MAX + 1u);
    ASSERT_EQ(SdManifest_ParsePartIndexDigits(buf, buf + strlen(buf)),
              SDMANIFEST_PART_NONE);
    /* an arbitrarily long digit run neither overflows nor is accepted */
    ASSERT_EQ(SdManifest_ParsePartIndexDigits("99999999999999999999",
                                               "99999999999999999999" + 20),
              SDMANIFEST_PART_NONE);
    /* and every index a session CAN write round-trips through the formatter's
     * inverse -- the property the two consumers both depend on */
    for (i = 1u; i <= 64u; i++) {
        snprintf(buf, sizeof(buf), "%u", (unsigned)i);
        ASSERT_EQ(SdManifest_ParsePartIndexDigits(buf, buf + strlen(buf)), i);
    }
}

/* ⛔⛔⛔ #1124 r28 AUDIT HIGH, BOTH LEGS: THE ALIAS-RISK PREDICATE. */
TEST(alias_risk_sees_a_tilde_anywhere_in_the_path) {
    ASSERT_TRUE(SdManifest_PathHasAliasRisk("LONGDI~1"));
    ASSERT_TRUE(SdManifest_PathHasAliasRisk("LONGDI~1/exp.csv"));
    ASSERT_TRUE(SdManifest_PathHasAliasRisk("D/EXP-1~1.CSV"));
    ASSERT_TRUE(SdManifest_PathHasAliasRisk("~"));
    ASSERT_FALSE(SdManifest_PathHasAliasRisk("LongDirectoryName/exp.csv"));
    ASSERT_FALSE(SdManifest_PathHasAliasRisk(""));
    ASSERT_FALSE(SdManifest_PathHasAliasRisk(NULL));
}

/* ⛔⛔ AND THE LENGTH-TAKING FORMS MUST NOT READ PAST THE LENGTH.
 *
 * This is the half with a real hazard behind it. SCPI_ParamCharacters returns
 * a pointer INTO the command buffer with a length and NO terminator, so the
 * setters call the ...Len entry points. A first draft of this fix called the
 * strlen forms on that operand, which reads the rest of the command line --
 * it would have refused a perfectly good name because a later argument
 * happened to contain '~', and read out of bounds when the operand sat at the
 * end of the buffer.
 *
 * ⭐ Asserted with a DELIBERATELY UNTERMINATED buffer, because a test using a
 * C string cannot tell the two entry points apart -- it would pass either way,
 * which is exactly how the draft looked correct. */
TEST(the_length_taking_predicates_stop_at_the_length) {
    /* no NUL anywhere in this array */
    const char raw[8] = { 'e', 'x', 'p', '~', 'b', 'a', 'd', '!' };

    /* the '~' is at index 3, so a 3-byte operand must NOT see it */
    ASSERT_FALSE(SdManifest_PathHasAliasRiskLen(raw, 3u));
    ASSERT_TRUE(SdManifest_PathHasAliasRiskLen(raw, 4u));
    ASSERT_FALSE(SdManifest_PathHasAliasRiskLen(raw, 0u));
    ASSERT_FALSE(SdManifest_PathHasAliasRiskLen(NULL, 4u));

    /* the manifest extension, bounded the same way: the first 8 bytes of
     * "exp.mfst.csv" ARE a manifest name, the whole 12 are not */
    ASSERT_TRUE(SdManifest_NameHasManifestExtLen("exp.mfst.csv", 8u));
    ASSERT_FALSE(SdManifest_NameHasManifestExtLen("exp.mfst.csv", 12u));
    /* and a name whose extension lies PAST the length is not matched */
    ASSERT_FALSE(SdManifest_NameHasManifestExtLen("exp.csv.mfst", 7u));
    ASSERT_TRUE(SdManifest_NameHasManifestExtLen("exp.csv.mfst", 12u));
}

/* ⛔⛔⛔ #1124 r28 AUDIT HIGH (sd_card_manager.c:3894): A DATA FILE MAY NOT BEAR
 * THE MANIFEST EXTENSION, because the manifest name is "<file>" + ".mfst" and
 * so a data file called "exp.csv.mfst" IS the manifest name of a session
 * configured as "exp.csv". Recording the former leaves "exp.csv.mfst.mfst"
 * describing it; a later "exp.csv" session recreates "exp.csv.mfst" as its own
 * manifest while that record survives, asserting a dead capture's size and
 * CRC over bytes it never saw.
 *
 * ⭐ The refusal also makes an EXISTING invariant true rather than assumed: the
 * foreign scan identifies manifests by name AND by first-line content, and
 * those two can only agree while no data file carries the extension. */
TEST(the_manifest_extension_is_recognised_whatever_its_case) {
    ASSERT_TRUE(SdManifest_NameHasManifestExt("exp.csv.mfst"));
    ASSERT_TRUE(SdManifest_NameHasManifestExt("exp.csv.MFST"));
    ASSERT_TRUE(SdManifest_NameHasManifestExt("exp.csv.MfSt"));
    ASSERT_TRUE(SdManifest_NameHasManifestExt("sub/exp.csv.mfst"));
    /* a session named "exp.csv.mfst" would produce THIS, which is the
     * manifest-of-a-manifest the refusal exists to prevent */
    ASSERT_TRUE(SdManifest_NameHasManifestExt("exp.csv.mfst.mfst"));
    ASSERT_FALSE(SdManifest_NameHasManifestExt("exp.csv"));
    ASSERT_FALSE(SdManifest_NameHasManifestExt("exp.mfst.csv"));
    /* the bare extension is NOT a manifest -- every writer emits
     * <something>.mfst, so a file called ".mfst" is some other file and
     * reading it as a manifest would let its contents decide a refusal */
    ASSERT_FALSE(SdManifest_NameHasManifestExt(".mfst"));
    ASSERT_FALSE(SdManifest_NameHasManifestExt(""));
    ASSERT_FALSE(SdManifest_NameHasManifestExt(NULL));
}

/* ⛔⛔⛔ #1124 r29 AUDIT HIGH (:232): TWO QUESTIONS ABOUT ONE STRING, WITH
 * OPPOSITE CORRECT ANSWERS FOR THE BARE EXTENSION.
 *
 * r28 refused a logging name whose TAIL is the manifest extension by reusing
 * the foreign scan's predicate. That predicate deliberately answers NO for a
 * name that IS exactly ".mfst" -- every writer emits <something>.mfst, so a
 * bare extension is some other file and reading it as a manifest would let its
 * contents decide a refusal. The setter needed YES for the same string:
 * `SD:FILE ".mfst"` gave extractBaseFilename an empty stem, rotation targeted
 * "-1.mfst", and with the grant that truncated the manifest of a session named
 * "-1".
 *
 * ⭐⭐ THIS TEST ASSERTS BOTH ANSWERS SIDE BY SIDE, which is the only shape
 * that defends the split. Each predicate tested alone passes after a
 * well-meaning "unification" that breaks the other; asserting the PAIR on the
 * same input makes the disagreement itself the thing under test. The bare
 * extension is the ONLY input where they differ, and that is asserted too --
 * so this also fails if the split is widened into a general divergence. */
TEST(the_two_manifest_name_questions_disagree_only_on_the_bare_extension) {
    /* THE discriminating input: reserved as a logging target, NOT a manifest
     * to read. */
    ASSERT_TRUE(SdManifest_NameReservedForManifests(".mfst"));
    ASSERT_FALSE(SdManifest_NameHasManifestExt(".mfst"));
    /* case-folded, same disagreement */
    ASSERT_TRUE(SdManifest_NameReservedForManifests(".MFST"));
    ASSERT_FALSE(SdManifest_NameHasManifestExt(".MFST"));

    /* EVERYWHERE ELSE THEY MUST AGREE. A divergence beyond the bare extension
     * would mean the scan and the setter disagree about ordinary names, which
     * is a different defect from the one being fixed. */
    const char *const agree[] = {
        "exp.csv.mfst", "exp.csv.MFST", "exp.csv.MfSt", "sub/exp.csv.mfst",
        "exp.csv.mfst.mfst", "a.mfst",
        "exp.csv", "exp.mfst.csv", "exp", "", "mfst", ".mfs", "x.mfs",
    };
    size_t i;
    for (i = 0u; i < sizeof(agree) / sizeof(agree[0]); i++) {
        ASSERT_EQ(SdManifest_NameReservedForManifests(agree[i]) ? 1 : 0,
                  SdManifest_NameHasManifestExt(agree[i]) ? 1 : 0);
    }
    ASSERT_FALSE(SdManifest_NameReservedForManifests(NULL));
    ASSERT_FALSE(SdManifest_NameHasManifestExt(NULL));

    /* the length-taking form stops at the length, same as its sibling */
    ASSERT_TRUE(SdManifest_NameReservedForManifestsLen("exp.mfst.csv", 8u));
    ASSERT_FALSE(SdManifest_NameReservedForManifestsLen("exp.mfst.csv", 12u));
    /* and the bare extension via an UNTERMINATED buffer */
    {
        const char raw[6] = { '.', 'm', 'f', 's', 't', 'X' };
        ASSERT_TRUE(SdManifest_NameReservedForManifestsLen(raw, 5u));
        ASSERT_FALSE(SdManifest_NameReservedForManifestsLen(raw, 4u));
    }
}

/* ⭐ THE PROPERTY THAT MAKES THE EXTENSION TEST A COMPLETE FIX, asserted
 * rather than argued: if a configured name's last extension is not the
 * manifest extension, then NO name that session writes can be reserved --
 * neither the unsuffixed name nor any part at any counter value. Built from
 * the real formatter's inverse so it cannot drift from how parts are named. */
TEST(a_session_whose_extension_is_not_the_manifest_one_can_never_write_a_reserved_name) {
    static const char *const stems[] = { "exp", "", "a.b", "-1", "log.csv" };
    static const char *const exts[]  = { ".csv", "", ".mfs", ".txt", ".mfstx" };
    char name[64];
    size_t si, ei;
    uint32_t n;

    for (si = 0u; si < sizeof(stems) / sizeof(stems[0]); si++) {
        for (ei = 0u; ei < sizeof(exts) / sizeof(exts[0]); ei++) {
            /* the configured name itself (counter 0 emits exactly this) */
            snprintf(name, sizeof(name), "%s%s", stems[si], exts[ei]);
            ASSERT_FALSE(SdManifest_NameReservedForManifests(name));
            /* and its parts, which is "%s-%u%s" */
            for (n = 1u; n <= 3u; n++) {
                snprintf(name, sizeof(name), "%s-%u%s", stems[si], (unsigned)n,
                         exts[ei]);
                ASSERT_FALSE(SdManifest_NameReservedForManifests(name));
            }
        }
    }
    /* ...and the converse: once the extension IS the manifest one, both the
     * name and every part are reserved -- which is why refusing it at the
     * setter forecloses all of them at once. */
    ASSERT_TRUE(SdManifest_NameReservedForManifests(".mfst"));
    ASSERT_TRUE(SdManifest_NameReservedForManifests("exp.mfst"));
    for (n = 1u; n <= 3u; n++) {
        snprintf(name, sizeof(name), "-%u.mfst", (unsigned)n);
        ASSERT_TRUE(SdManifest_NameReservedForManifests(name));
        snprintf(name, sizeof(name), "exp-%u.mfst", (unsigned)n);
        ASSERT_TRUE(SdManifest_NameReservedForManifests(name));
    }
}

/* ⛔⛔⛔ #1124 r30 AUDIT HIGH, blind leg: NON-ASCII IS REFUSED BECAUSE THE
 * ALIAS FatFs STORES FOR IT CAN BE PURE ASCII.
 *
 * The decisive fact is in the FatFs we ship, not in this file: TBL_CT437
 * begins {0x80,0x9A,0x45,0x41,...}, so create_name's `ExCvt[wc & 0x7F]` maps
 * 0x82 (e-acute) to 0x45 -- ASCII 'E' -- and sets no NS_LOSS, so the stored
 * alias is a clean `E.CSV` with no "~N" tail. `SD:FILE "E.csv",1` is then pure
 * ASCII, passes the opt-in's ASCII restriction, and truncates the earlier
 * capture through the alias while every textual record comparison says the two
 * names differ.
 *
 * ⚠ WHAT THIS TEST CAN AND CANNOT SHOW. It tests OUR predicate, not FatFs's
 * table -- the table is vendor code and is quoted above with its file so the
 * claim is checkable, not re-implemented here. A host test that modelled CP437
 * would be asserting my reading of the table back to myself, which is the
 * circularity this row has been caught by before. */
TEST(non_ascii_is_detected_in_a_name_or_a_directory) {
    ASSERT_TRUE(SdManifest_PathHasNonAscii("\x82.csv"));
    ASSERT_TRUE(SdManifest_PathHasNonAscii("exp\x90.csv"));
    ASSERT_TRUE(SdManifest_PathHasNonAscii("D\xA0/exp.csv"));
    ASSERT_FALSE(SdManifest_PathHasNonAscii("exp.csv"));
    ASSERT_FALSE(SdManifest_PathHasNonAscii("sub/exp.csv"));
    ASSERT_FALSE(SdManifest_PathHasNonAscii("E.csv"));   /* the ALIAS is ASCII */
    ASSERT_FALSE(SdManifest_PathHasNonAscii(""));
    ASSERT_FALSE(SdManifest_PathHasNonAscii(NULL));

    /* ⭐ THE SIGNED-CHAR TRAP, asserted rather than trusted: `char` is SIGNED
     * on this target, so a byte >= 0x80 compares as NEGATIVE and a `c > 0x7E`
     * test written on plain char reads FALSE -- the predicate would pass every
     * high byte. Every one of the 18 CP437 bytes that alias to pure ASCII is
     * >= 0x80, so getting this wrong would disable the check precisely where it
     * matters. */
    {
        static const char high[18] = {
            '\x82','\x83','\x85','\x88','\x89','\x8A','\x8B','\x8C','\x8D',
            '\x93','\x95','\x96','\x97','\x98','\xA0','\xA1','\xA2','\xA3'
        };
        size_t i;
        for (i = 0u; i < sizeof(high); i++) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%c.csv", high[i]);
            ASSERT_TRUE(SdManifest_PathHasNonAscii(buf));
        }
    }

    /* the control range is already refused upstream, and this agrees rather
     * than holding a second, weaker opinion about the same bytes */
    ASSERT_TRUE(SdManifest_PathHasNonAscii("exp\x01.csv"));
    ASSERT_TRUE(SdManifest_PathHasNonAscii("exp\x7F.csv"));

    /* length-taking form stops at the length, like its siblings */
    {
        const char raw[6] = { 'e', 'x', 'p', '\x82', 'b', 'd' };
        ASSERT_FALSE(SdManifest_PathHasNonAsciiLen(raw, 3u));
        ASSERT_TRUE(SdManifest_PathHasNonAsciiLen(raw, 4u));
        ASSERT_FALSE(SdManifest_PathHasNonAsciiLen(raw, 0u));
        ASSERT_FALSE(SdManifest_PathHasNonAsciiLen(NULL, 4u));
    }
}

int main(void)
{
    printf("=== #924 SD session integrity manifest ===\n");
    RUN(non_ascii_is_detected_in_a_name_or_a_directory);
    RUN(the_two_manifest_name_questions_disagree_only_on_the_bare_extension);
    RUN(a_session_whose_extension_is_not_the_manifest_one_can_never_write_a_reserved_name);
    RUN(part_suffix_parser_accepts_only_what_the_formatter_emits);
    RUN(part_suffix_parser_bounds_the_index_to_what_a_session_can_write);
    RUN(alias_risk_sees_a_tilde_anywhere_in_the_path);
    RUN(the_length_taking_predicates_stop_at_the_length);
    RUN(the_manifest_extension_is_recognised_whatever_its_case);
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
    RUN(session_shape_round_trips);
    RUN(paths_equal_case_insensitive_matches_across_case);
    RUN(paths_equal_case_insensitive_rejects_real_differences);
    RUN(paths_equal_case_insensitive_null_safety);
    RUN(first_line_check_accepts_a_real_rendered_line);
    RUN(first_line_check_accepts_a_rendered_PARTIAL_line);
    RUN(paths_equal_normalises_the_way_fatfs_does);
    RUN(manifest_name_must_fit_the_retrieval_operand);
    RUN(first_line_check_accepts_empty_content);
    RUN(first_line_check_rejects_unrelated_data);
    RUN(first_line_check_rejects_near_misses);
    RUN(first_line_check_rejects_a_disk_full_fragment);
    RUN(fresh_file_accounting_is_zero_bytes_and_empty_crc);
    RUN(fresh_file_accounting_renders_as_the_empty_file_it_describes);
    RUN(fresh_file_accounting_null_safety);
    RUN(canonical_dir_len_strips_a_trailing_separator_run);
    RUN(every_construction_shape_is_slash_safe_with_a_trailing_slash_dir);
    RUN(canonical_construction_agrees_with_the_fatfs_comparator);
    RUN(a_file_name_must_not_end_in_a_separator);
    RUN(the_directory_half_strips_where_the_name_half_refuses);
    RUN(the_manifest_name_is_usable_however_the_file_is_spelled);
    RUN(a_recorded_name_retrieves_the_file_it_describes);
    RUN(a_delimiter_in_the_directory_would_corrupt_the_record);
    RUN(the_manifest_extension_test_accepts_what_we_write);
    RUN(the_manifest_extension_test_ignores_case);
    RUN(the_manifest_extension_test_rejects_everything_else);
    RUN(the_extension_test_agrees_with_the_manifest_construction);
    RUN(part_index_recognises_the_names_a_session_writes);
    RUN(part_index_folds_case_on_both_sides);
    RUN(part_index_rejects_names_this_session_cannot_write);
    RUN(part_index_skips_an_overflowing_index_rather_than_faulting);
    RUN(part_index_is_null_safe);
    RUN(part_index_round_trips_the_filename_formatter);
    RUN(a_foreign_base_name_that_is_one_of_our_parts_is_recognised);
    RUN(part_index_inverts_the_generator_over_a_set_of_stems_and_extensions);
    RUN(part_index_says_none_to_a_near_miss);
    RUN(part_index_treats_a_large_index_as_a_number_not_a_fault);
    RUN(the_prefilter_recognises_every_spelling_the_comparator_calls_equal);
    RUN(the_prefilter_agrees_however_the_configured_directory_is_spelled);
    RUN(normalisation_does_not_make_unrelated_names_match);
    RUN(a_doubled_separator_inside_the_directory_is_recognised);
    RUN(an_unsuffixed_first_file_inside_a_bucket_is_index_zero);
    RUN(a_configured_name_containing_separators_is_matched);
    RUN(a_configured_name_with_separators_still_rejects_near_misses);
    RUN(a_padded_index_is_never_one_of_our_parts);

    /* #1124 r38: outcome pins for the truncated-first-line give-up site. */
    RUN(r38_truncated_first_line_is_not_fully_visible);
    RUN(r38_short_read_without_terminator_is_fully_visible);
    RUN(r38_terminator_present_is_fully_visible_even_on_a_full_read);
    RUN(r38_empty_and_null_are_fully_visible);
    RUN(r38_content_rejections_stay_clear_not_fault);
    return TEST_SUMMARY();
}
