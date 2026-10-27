/**
 * @file SdManifest.h
 * @brief #924: the two pure text operations the per-session SD integrity
 *        manifest needs, split out so a host test can compile the SAME TEXT
 *        the firmware does.
 *
 * The manifest itself lives in sd_card_manager.c: one file per streaming
 * session, one line per stream file, appended at the point that file is
 * closed. A line is
 *
 *     <name>,<bytes>,0x<CRC32>\n
 *
 * where <name> is the file's path RELATIVE to the configured SD directory --
 * exactly the spelling `SYSTem:STORage:SD:CRC?` and `SYSTem:STORage:SD:GET`
 * take as their operand, so a host can feed a manifest line straight back to
 * the device. For a session that stays in bucket 0 that is just
 * `experiment-3.csv`; once #689 bucketing rolls, it is `P001/experiment-65.csv`.
 *
 * WHY A HEADER AND NOT PART OF sd_card_manager.c. That file is not compilable
 * on a host (Harmony, FreeRTOS, FatFs, the whole board graph), so anything
 * inside it can only be tested by re-implementing its shape -- which is how
 * tests/host/test_943 and test_953 have to work, and which cannot catch a
 * format-string edit. These two operations depend on nothing but <stdio.h> and
 * <string.h>, so splitting them out makes them directly testable, the same
 * trade #889 made for AD7609Scale.h and #164 for JSON_StringEscape.h.
 *
 * Header-only (`static inline`) rather than a .c: adding a translation unit
 * means editing the MPLAB X project (nbproject/configurations.xml) and
 * regenerating makefiles, which a header avoids entirely. It has exactly one
 * firmware consumer, so there is no duplication to pay for.
 */
#ifndef UTIL_SD_MANIFEST_H
#define UTIL_SD_MANIFEST_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "CRC32.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Render one manifest line into `out`.
 *
 * ALL-OR-NOTHING. A line that does not fit writes nothing and returns -1
 * rather than emitting a truncated one: a manifest line whose NAME has been
 * cut is not a weaker record, it is a WRONG one -- it names a file that does
 * not exist and attributes a real CRC to it. The caller logs and skips.
 *
 * @param out     Destination buffer (NUL-terminated on success, emptied on
 *                failure).
 * @param outLen  Size of `out` in bytes.
 * @param name    File name relative to the configured directory.
 * @param bytes   Byte count of that file.
 * @param crc     FINALIZED CRC-32 of that file (post CRC32_Finalize).
 * @param isPrefix Append SD_MANIFEST_PREFIX_MARK, meaning the record and the
 *                file on the card may disagree about the length IN EITHER
 *                DIRECTION. See the block inside this function for the four
 *                conditions that set it and which direction each implies.
 * @return Number of characters written (excluding the NUL), or -1.
 */
/* #924 (round-3 audit HIGH): the marker appended when the record describes a
 * PREFIX of the file rather than the whole file. See `isPrefix` below. Its
 * length is a named term of SD_MANIFEST_LINE_MAX in sd_card_manager.c -- do not
 * change one without the other. */
/* #924: extension of the per-session integrity manifest. Distinct from every
 * payload extension the device writes (.csv / .pb / .json) on purpose -- host
 * tooling that globs for stream files (download_sd_files.py,
 * analyze_split_files.py) must not pick it up, and its own name must not be
 * mistakable for a split part. The base name is the SESSION's base name, so
 * `experiment.csv` logs to `experiment.mfst` alongside `experiment.csv`,
 * `experiment-1.csv`, ... -- and a session logging under a different base
 * gets its own manifest rather than overwriting this one. */
#define SD_MANIFEST_EXT                    ".mfst"

#define SD_MANIFEST_PREFIX_MARK  ",PARTIAL"

/**
 * @brief Will a manifest named `<base><ext>` fit the operand every retrieval
 *        command validates against?
 *
 * #924 (round-5 audit): SD:GET, SD:CRC? and SD:DELete all validate their
 * operand against SD_CARD_MANAGER_CONF_FILE_NAME_LEN_MAX, and the manifest's
 * basename is the stream file's base plus this module's extension. A 40-char
 * FILE ending ".csv" gives a 36-char base and a 41-char manifest name -- ONE
 * over -- so the manifest the feature writes could not be fetched by the
 * command that exists to fetch it.
 *
 * THE POINT OF PUTTING THE ARITHMETIC HERE is that the defect was an
 * OFF-BY-ONE in reasoning, not a missing check, and an off-by-one is only
 * really fixed once its BOUNDARY is asserted. Inside sd_OpenSessionManifest()
 * this would be unreachable from the host suite (a static function needing
 * SYS_FS); here test_924 nails baseLen == budget and budget + 1 directly.
 *
 * `<=` is deliberate: a name exactly AT the cap is fetchable, because the
 * operand check is itself `<=`. Addition is done so it cannot wrap: extLen is
 * compared against the cap first, so baseLen + extLen is never formed when the
 * extension alone would already exceed it.
 */
static inline bool SdManifest_NameFitsOperand(size_t baseLen, size_t extLen,
                                              size_t operandCap)
{
    if (extLen > operandCap) {
        return false;   /* no base, however short, could fit */
    }
    return baseLen <= (operandCap - extLen);
}

/*
 * #1124: the ONE canonicalising accessor every path construction goes through.
 *
 * Returns the length of `dir` with trailing '/' stripped, for use as
 *   snprintf(out, n, "%.*s/%s", (int)SdManifest_CanonicalDirLen(d), d, name)
 * so no construction can emit "dir//name" whatever the configured directory
 * ends with.
 *
 * ⛔ CANONICALISES AT USE, NOT AT ENTRY. Normalising in the SCPI setter was
 * rejected because it misses every other writer: an NVM restore populates the
 * settings struct directly, and `opDirectory` is written by the operand path.
 * Normalising where the value is CONSUMED covers all writers by construction.
 *
 * ⛔ TAKES THE DIRECTORY AS A PARAMETER rather than reading the settings
 * global, because the sources are not one field. Measured on
 * sd_card_manager.c: `gpSDCardSettings->directory` (4 sites),
 * `gpSDCardSettings->opDirectory` (via sd_ListDirTarget), the walk stack's
 * `gListDirStack[].path` (seeded from either), and `gSDCardData.bucketPath`
 * (built from `directory`, which is how generateFilename inherits a slash
 * without ever naming the field). An accessor reading the global would have
 * covered 4 of 9 join sites.
 *
 * A run of trailing slashes collapses: "d///" and "d" both give 1 for "d".
 * An all-slash directory gives 0, which yields an absolute "/name" -- the same
 * path "/" + name denotes, and the only sensible reading of a directory that
 * names the root.
 */
/*
 * #1124 r9: is this usable as the FILE-NAME half of a path join?
 *
 * ⛔ THE REMEDY HERE IS REJECT, NOT STRIP, AND THAT ASYMMETRY IS THE POINT.
 * A trailing '/' on a DIRECTORY is a redundant spelling of the same directory,
 * so SdManifest_CanonicalDirLen() strips it. A trailing '/' on a FILE NAME is
 * not a redundant spelling of anything -- "log.csv/" does not name a file.
 * Stripping it would silently accept a malformed operand and invent a name the
 * caller never asked for, so this refuses instead.
 *
 * That is why a blanket "canonicalise every path operand" would have been
 * actively wrong, and why the operand matrix had to be settled before any code:
 * the two halves of one join need opposite treatments.
 *
 * What a trailing '/' actually costs, measured rather than assumed: FatFs
 * refuses the resulting open ("<dir>/log.csv/.mfst" reads log.csv as a
 * directory), so the loss is the MANIFEST plus a diagnostic that blames the
 * open rather than the name. Not corruption -- which is exactly why rejecting
 * at the name is worth doing: it moves the error to where the cause is.
 *
 * Rejects: NULL, empty, and any trailing '/' run. An INTERIOR '/' is left
 * alone -- SD_ValidatePathParam permits subdirectory components, and that is a
 * separate question from whether the last component names a file.
 */
static inline bool SdManifest_NameIsUsable(const char *name)
{
    size_t len;

    if (name == NULL) {
        return false;
    }
    len = strlen(name);
    if (len == 0u) {
        return false;
    }
    return name[len - 1u] != '/';
}

static inline size_t SdManifest_CanonicalDirLen(const char *dir)
{
    size_t len;

    if (dir == NULL) {
        return 0u;
    }
    len = strlen(dir);
    while (len > 0u && dir[len - 1u] == '/') {
        len--;
    }
    return len;
}

static inline int SdManifest_FormatLine(char *out, size_t outLen,
                                        const char *name,
                                        uint64_t bytes, uint32_t crc,
                                        bool isPrefix)
{
    int n;

    if (out == NULL || outLen == 0u || name == NULL) {
        return -1;
    }
    out[0] = '\0';

    /* '\n' only, not "\r\n": the manifest is a machine-read sidecar, not one
     * of the device's CSV payloads, and a bare LF is what `str.splitlines()` /
     * `csv.reader` / `wc -l` all agree on. */
    /* #924 (round-3 audit HIGH): `isPrefix` is appended as a FOURTH field
     * rather than by altering the first three, so a reader indexing [0..2] is
     * unaffected and the CRC stays VERIFIABLE -- truncate the file to `bytes`
     * and the value still matches. That is the property a bare "invalid"
     * marker would have thrown away.
     *
     * ⛔ WHAT IT MEANS (broadened at #1124 r12; the earlier wording was
     * narrower than the code and is the stale text that round flagged). It
     * says: `crc` is the CRC32 of the first `bytes` bytes as this firmware
     * counted them, and THE FILE ON THE CARD MAY DISAGREE WITH THAT COUNT IN
     * EITHER DIRECTION. Not just "the file may be longer" -- two of the four
     * setters mean it may be SHORTER, and a reader told only about "longer"
     * would mis-handle them.
     *
     * The four setters, and which direction each implies (sd_card_manager.c):
     *
     *  1. a DATA write returned an error (SDCardWrite). Harmony's
     *     SYS_FS_FileWrite overwrites FatFs's partial `*bw` with 0xFFFFFFFF on
     *     any error (sys_fs.c:2442-2448) even though f_write accumulates it
     *     and keeps it across ABORT (ff.c:4041), so THE COMMITTED COUNT IS
     *     DESTROYED BEFORE THE FIRMWARE CAN SEE IT -> the file may be LONGER
     *     than `bytes`.
     *  2. the stream-file HEADER write returned an error (#1124 r12 review
     *     leg). Same mechanism as 1; it is a separate setter only because the
     *     header is the one stream-file byte range that does not pass through
     *     SDCardWrite() -> the file may be LONGER.
     *  3. a FileSync before unmount FAILED (#1124 r10 finding 3). FF_FS_TINY
     *     is 0 (ffconf.h:226), so FatFs holds a per-file sector cache, and
     *     counted bytes may still be sitting in it when the unmount discards
     *     them -> the file may be SHORTER than `bytes`.
     *  4. a FileClose during unmount FAILED (#1124 r10 finding 3). That close
     *     did not flush either, so the same doubt and the same direction as 3
     *     -> the file may be SHORTER.
     *
     * Counting the OFFERED bytes instead would be a CRC over bytes that may
     * not be on the card; omitting the line would make the manifest disagree
     * with the file list, which this file's append site already argues
     * against. A truthful "these two may not agree" record is the only one of
     * the three that is both correct and checkable.
     *
     * A VERIFIER should therefore treat a marked line as: read min(bytes,
     * filesize) bytes, CRC them, and expect a match only if filesize >= bytes.
     * An unmarked line is the strong claim -- filesize == bytes AND the CRC
     * covers all of it. */
    n = snprintf(out, outLen, "%s,%llu,0x%08X%s\n", name,
                 (unsigned long long)bytes, (unsigned)crc,
                 isPrefix ? SD_MANIFEST_PREFIX_MARK : "");
    if (n < 0 || (size_t)n >= outLen) {
        out[0] = '\0';
        return -1;
    }
    return n;
}

/* ⛔ #1124 r13 / OPERATOR ITEM 8 = B: SdManifest_RelativeName() and
 * SdManifest_DirShadowsBucketName() were REMOVED here.
 *
 * The ruling (issuecomment-6450419546) records every file by its FULL path and
 * drops the P###-folder-name refusal, which left both helpers without a caller:
 * RelativeName existed only to slice the configured directory off a recorded
 * name, and DirShadowsBucketName only to refuse the directory name that made
 * that slice ambiguous. Deleted rather than left unused, because an uncalled
 * helper with tests beside it reads as live code to the next reader -- and this
 * module has already lost a round to exactly that.
 *
 * What replaced them is one line in sd_AppendManifestLine: it passes
 * gSDCardData.filePath to SdManifest_FormatLine. Pinned in tests/host/Makefile
 * together with the absence of both helpers, so the pair cannot drift apart. */

/**
 * @brief Case-insensitive equality for two NUL-terminated ASCII paths.
 *
 * (post-merge audit, defect 2): FatFs's own duplicate-open detection
 * (FF_FS_LOCK, ffconf.h) is case-insensitive -- `foo.MFST` and `foo.mfst`
 * name the SAME file to the filesystem -- while sd_card_manager.c's
 * self-collision guard (the manifest path vs. this session's own stream
 * file) used to compare with plain `strcmp`. A configured stream name that
 * differs from the manifest path only in case slipped past that guard,
 * FatFs then refused the manifest's own f_open as the duplicate it is, and
 * because manifestOpenAttempted latches on the first try, the session ran to
 * completion with zero integrity records and no diagnostic. Comparing the
 * way the filesystem does closes that gap.
 *
 * ASCII only, matching str_ci_equal in wifi_services/mdns_responder.c:
 * strcasecmp isn't used elsewhere in this firmware, so don't assume the
 * XC32 newlib exposes it.
 *
 * @return true iff a and b are both NULL, or both non-NULL and equal
 *         ignoring ASCII case.
 */
static inline bool SdManifest_PathsEqualCaseInsensitive(const char *a,
                                                         const char *b)
{
    if (a == NULL || b == NULL) {
        return a == b;
    }
    /* #924: COMPARE PATHS THE WAY FatFs RESOLVES THEM.
     *
     * ⛔ ROUND 6 FOUND THIS MODELLED create_name()'s SNIPPING AND NOT ITS
     * SEGMENTATION. The lesson is not "read more": a citation bounds the claim
     * to the question that retrieved it. Round 5 asked "how does FatFs
     * NORMALISE a name?" and quoted ff.c:2895-2901 correctly. The unasked
     * question -- "how does FatFs SEGMENT a path?" -- is answered fifteen lines
     * ABOVE those. So the equivalences are now ENUMERATED ONCE, from
     * follow_path() and create_name() TOGETHER, with reachability stated per
     * line, and each line has its own test. Add to the list, not to the code.
     *
     *   E1  trailing SPACES and DOTS, per segment          ff.c:2895-2899
     *       "foo.mfst " == "foo.mfst" == "foo."
     *       REACHABLE: SD_ValidatePathParam accepts both.
     *   E2  DUPLICATED separators collapse                 ff.c:2878
     *       "DAQiFi//foo" == "DAQiFi/foo"
     *       ⛔ WAS reachable from config alone, and is NO LONGER, as of #1124:
     *       the path was built dir + '/' + file, so a `directory` ending in
     *       '/' produced it with no user error (the validator's segment checks
     *       only test length 1 and 2, so a zero-length segment passes). Every
     *       construction now goes through SdManifest_CanonicalDirLen(), so
     *       both operands this comparator is handed at sd_card_manager.c are
     *       canonical and this arm is unreachable FROM OUR OWN CONSTRUCTION.
     *
     *       It is kept deliberately, and not as dead weight: what this function
     *       promises is equivalence THE WAY FatFs COMPUTES IT, not equivalence
     *       over the strings our current single caller happens to pass. Nothing
     *       couples "construction is canonical" to "the comparator may assume
     *       it", so narrowing the comparator would make a second caller unsafe
     *       with no check firing -- and the failure is silent, costing a whole
     *       session's integrity records with no diagnostic. The loop's
     *       termination invariant also depends on the separator skip (see
     *       below); removing it reintroduces a hang, not just a wrong answer.
     *   E3  a TERMINATING separator is ignored             ff.c:2880
     *       "DAQiFi/foo/" == "DAQiFi/foo"
     *       Not produced by our own two snprintf sites; implemented because
     *       the other operand (filePath) is built elsewhere and this is free.
     *   E4  LEADING separators are stripped                ff.c:3069
     *       "/DAQiFi/foo" == "DAQiFi/foo"
     *       NOT REACHABLE: SD_ValidatePathParam rejects a leading '/' or '\\'
     *       (absolute paths). Implemented anyway so this function is correct
     *       independent of its callers.
     *   E5  case-insensitive                               FF_FS_LOCK compare
     *       REACHABLE; this is the post-merge defect-2 fix.
     *
     * ⚠ DELIBERATELY NOT MODELLED, each because the validator makes it
     * unreachable -- if SD_ValidatePathParam is ever relaxed, these become
     * live and belong on the list above:
     *   N1  '\\' is also a separator to FatFs (IsSeparator, ff.c:51). The
     *       validator rejects '\\' outright, so only '/' is handled here.
     *   N2  "." and ".." are dot segments (ff.c:2885, live because
     *       FF_FS_RPATH==2). The validator rejects them as whole segments.
     *
     * ⛔ AND WHAT IS *NOT* NORMALISED, which the tests pin because getting it
     * wrong is worse than the bug: LEADING spaces and dots are SIGNIFICANT.
     * The snip loop only decrements the END index (ff.c:2898 `di--`) and never
     * advances the start, so the LFN keeps its leading characters.
     * create_name()'s leading-space removal at :2904 is in the 8.3
     * SFN-generation path and sets NS_LOSS -- which is what makes the LFN
     * authoritative -- and does not change the long name's identity. So " foo"
     * and "foo" are DIFFERENT files.
     *
     * ⚠ A CORRECTION TO ROUND 5's OWN COMMENT, which asserted the opposite of
     * the vendor source: it said an all-snipped or trailing-separator segment
     * yields FR_INVALID_NAME so "the open fails either way". ff.c:2880 carries
     * a comment reading "Ignore terminating separator" -- the open SUCCEEDS on
     * the real file. That false claim is what made the gap read as deliberate.
     * (The vendor comment is described rather than quoted with its delimiters:
     * a nested opener is -Wcomment, which is -Werror in the firmware build.)
     * FR_INVALID_NAME (ff.c:2901) applies only to a segment that is EMPTY
     * AFTER SNIPPING and is not merely a separator run, e.g. "foo/   /bar". */
    for (;;) {
        size_t na, nb, ea, eb, i;

        /* E4: leading separators, and E2: any run between segments */
        while (*a == '/') { a++; }
        while (*b == '/') { b++; }

        /* E3: a run of separators at the very end terminates both sides */
        if (*a == '\0' || *b == '\0') {
            return *a == '\0' && *b == '\0';
        }

        /* ⛔ LOOP TERMINATION DEPENDS ON THE SKIP ABOVE, and that dependency
         * was implicit until a mutation test HUNG for 200 s instead of
         * failing. After the separator skip, `*a` is neither '/' nor NUL, so
         * na >= 1 and the `a += na` below ADVANCES. Remove or reorder the skip
         * and na becomes 0 on a path like "a//b", `a += 0` spins, and this
         * function never returns -- on the SCPI task, which is worse than any
         * wrong answer it could give. If you restructure this loop, keep a
         * guaranteed advance of at least one character per iteration. */
        na = 0u; nb = 0u;
        while (a[na] != '\0' && a[na] != '/') { na++; }
        while (b[nb] != '\0' && b[nb] != '/') { nb++; }

        /* E1: snip trailing ' ' and '.' exactly as create_name() does */
        ea = na; eb = nb;
        while (ea > 0u && (a[ea - 1u] == ' ' || a[ea - 1u] == '.')) { ea--; }
        while (eb > 0u && (b[eb - 1u] == ' ' || b[eb - 1u] == '.')) { eb--; }

        if (ea != eb) {
            return false;
        }
        for (i = 0u; i < ea; i++) {                 /* E5 */
            char ca = a[i];
            char cb = b[i];
            if (ca >= 'A' && ca <= 'Z') {
                ca = (char)(ca - 'A' + 'a');
            }
            if (cb >= 'A' && cb <= 'Z') {
                cb = (char)(cb - 'A' + 'a');
            }
            if (ca != cb) {
                return false;
            }
        }
        a += na;
        b += nb;
    }
}

/**
 * @brief True if `data` (the first `len` bytes read back from an
 * already-existing file) is empty, or begins with a line
 * SdManifest_FormatLine could have produced: a run of one or more characters
 * containing none of ',' CR or LF, then a comma, then one or more decimal
 * digits, then ",0x", then exactly 8 hex digits, then '\n'.
 *
 * (post-merge audit, defect 1): sd_OpenSessionManifest() opens the manifest
 * path with SYS_FS_FILE_OPEN_WRITE, which FatFs (ff.c, FA_CREATE_ALWAYS)
 * truncates on open -- and until this check existed, nothing asked whether
 * some OTHER, unrelated file already sat at that exact path before doing so.
 * The module's own accepted model is that reusing a base filename across
 * sessions overwrites the EARLIER SESSION'S MANIFEST at that path (see the
 * file-level comment above), so a pre-existing file whose first line matches
 * this shape is treated as "one of ours" and the open proceeds unchanged.
 * A file that does NOT match -- for instance a real data file an earlier
 * session left at this exact path because its OWN configured name happened
 * to collide with ITS manifest, or anything else that landed there by any
 * other means -- must be refused rather than silently destroyed.
 *
 * A HEURISTIC, not proof of provenance: an unrelated file whose first line
 * coincidentally matches this exact shape would still pass. That is an
 * accepted limitation of a check that must run once, at session start,
 * without parsing an arbitrarily large file first.
 *
 * @param data First bytes of the existing file's content. Need not be
 *             NUL-terminated. May be NULL only when len is 0.
 * @param len  Number of valid bytes in `data`.
 */
static inline bool SdManifest_FirstLineLooksLikeManifest(const char *data,
                                                          size_t len)
{
    size_t i = 0u;
    size_t nameLen = 0u;
    size_t digits = 0u;
    size_t h;

    if (len == 0u) {
        return true;   /* nothing there to be destroyed either way */
    }
    if (data == NULL) {
        return false;
    }

    /* <name>: at least one character, none of them a delimiter the format
     * itself cannot represent (mirrors sd_OpenSessionManifest's own
     * ",\r\n" refusal check on the CONFIGURED name). */
    while (i < len && data[i] != ',' && data[i] != '\r' && data[i] != '\n') {
        i++;
        nameLen++;
    }
    if (nameLen == 0u || i >= len || data[i] != ',') {
        return false;
    }
    i++;

    /* <bytes>: one or more decimal digits. */
    while (i < len && data[i] >= '0' && data[i] <= '9') {
        i++;
        digits++;
    }
    if (digits == 0u || i >= len || data[i] != ',') {
        return false;
    }
    i++;

    /* "0x" then exactly 8 hex digits then '\n'. */
    if (i + 10u >= len) {
        return false;   /* '0','x',8 hex digits, '\n' == 11 bytes, i-relative */
    }
    if (data[i] != '0' || (data[i + 1u] != 'x' && data[i + 1u] != 'X')) {
        return false;
    }
    i += 2u;
    for (h = 0u; h < 8u; h++) {
        char c = data[i + h];
        bool isHex = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') ||
                     (c >= 'a' && c <= 'f');
        if (!isHex) {
            return false;
        }
    }
    i += 8u;

    /* #924 (round-4 audit): THE OPTIONAL FOURTH FIELD. A record written after a
     * faulted write carries SD_MANIFEST_PREFIX_MARK before the newline.
     *
     * This is the ONE in-repo reader of a manifest line, DERIVED rather than
     * recalled: SdManifest.h declares five functions and this is the only one
     * that parses the line's shape, reached from exactly one call site (the
     * overwrite-safety probe in sd_card_manager.c). Without this arm, a
     * manifest whose FIRST line is a PARTIAL record stops looking like a
     * manifest, the probe declines to overwrite it, and the session silently
     * continues with no manifest at all.
     *
     * IT FAILS CLOSED, which is why adding the field DEGRADED rather than
     * corrupted: an unrecognised line makes the probe refuse, and refusing when
     * unsure is that probe's whole job. Had the marker altered one of the first
     * three fields in place, an un-updated reader would have MIS-PARSED instead
     * of declining -- the fourth-field shape is what bounded the blast radius
     * of its own incomplete rollout.
     *
     * Length from sizeof(SD_MANIFEST_PREFIX_MARK), never a literal: the
     * marker's width is already a named term of SD_MANIFEST_LINE_MAX, and a
     * second hand-copied constant is the one that goes stale. */
    if (data[i] == '\n') {
        return true;
    }
    {
        const size_t markLen = sizeof(SD_MANIFEST_PREFIX_MARK) - 1u;
        size_t k;
        if (i + markLen >= len) {
            return false;   /* no room for the mark AND its newline */
        }
        for (k = 0u; k < markLen; k++) {
            if (data[i + k] != SD_MANIFEST_PREFIX_MARK[k]) {
                return false;
            }
        }
        return data[i + markLen] == '\n';
    }
}

/**
 * @brief Arm the per-file byte counter and running CRC for a file that has
 * just become current -- 0 bytes, and the CRC of nothing.
 *
 * (post-merge audit, defect 3): sd_card_manager.c's OPEN_FILE state calls
 * this at BOTH places a new file becomes the one gSDCardData.filePath and
 * gSDCardData.fileHandle name: a normal open (after which bytes accumulate
 * as they are actually written) and an ABORTED rotation-open, where the
 * session is torn down before WRITE_TO_FILE ever runs, so the file the
 * truncating open just created stays genuinely empty. Before this existed,
 * only the first site reset these two fields; the second (added later, when
 * an in-flight teardown could abort a rotation open) reused neither this
 * function nor the reset it performs, so currentFileBytes/fileCrcRunning
 * were left describing the file the rotation was RETIRING. UNMOUNT_DISK then
 * closes the new, empty file and appends a manifest line for it
 * UNCONDITIONALLY using whatever these two fields hold -- so the line named
 * the new file but carried the old one's byte count and CRC: a record that
 * is wrong, not merely incomplete.
 *
 * Sharing one function between both call sites (rather than repeating the
 * same two assignments) is what a grep guard can hold onto: `tests/host`
 * fails the build if either site stops calling it, the same way this repo
 * already gates SdManifest_FormatLine/SdManifest_RelativeName call sites in
 * sd_card_manager.c (see tests/host/Makefile's $(MANIFEST_BIN) rule).
 *
 * @param bytesOut Set to 0.
 * @param crcOut   Set to CRC32_Init() -- CRC32_Finalize of this, unfolded,
 *                 is the CRC of an empty byte range (0x00000000).
 */
static inline void SdManifest_FreshFileAccounting(uint64_t *bytesOut,
                                                   uint32_t *crcOut,
                                                   bool *isPrefixOut)
{
    if (bytesOut != NULL) {
        *bytesOut = 0u;
    }
    if (crcOut != NULL) {
        *crcOut = CRC32_Init();
    }
    /* #924 (round-3 audit HIGH): cleared with the other two, HERE, because this
     * is already the one place per-file accounting is reset. A separate reset
     * site would be a second thing to remember, and this file's lifecycle has
     * now cost three rounds to exactly that. */
    if (isPrefixOut != NULL) {
        *isPrefixOut = false;
    }
}

#ifdef __cplusplus
}
#endif

#endif /* UTIL_SD_MANIFEST_H */
