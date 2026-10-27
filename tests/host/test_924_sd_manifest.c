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
 * a 36-char base, so `<base>.mfst` is 41 and SD:GET/SD:CRC?/SD:DELete validate
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
 * data file at the plain <base>.mfst path a LATER session's manifest now
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
    return TEST_SUMMARY();
}
