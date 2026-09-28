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
 * @return Number of characters written (excluding the NUL), or -1.
 */
static inline int SdManifest_FormatLine(char *out, size_t outLen,
                                        const char *name,
                                        uint64_t bytes, uint32_t crc)
{
    int n;

    if (out == NULL || outLen == 0u || name == NULL) {
        return -1;
    }
    out[0] = '\0';

    /* '\n' only, not "\r\n": the manifest is a machine-read sidecar, not one
     * of the device's CSV payloads, and a bare LF is what `str.splitlines()` /
     * `csv.reader` / `wc -l` all agree on. */
    n = snprintf(out, outLen, "%s,%llu,0x%08X\n", name,
                 (unsigned long long)bytes, (unsigned)crc);
    if (n < 0 || (size_t)n >= outLen) {
        out[0] = '\0';
        return -1;
    }
    return n;
}

/**
 * @brief The part of `fullPath` that names the file relative to `directory`.
 *
 * Returns a pointer INTO `fullPath` -- no copy, no allocation. When the prefix
 * does not match, the FULL path is returned rather than a guess: a manifest
 * naming a file by a path that is merely longer than expected is still usable
 * by a human and by `SD:GET`, whereas a name assembled from a wrong assumption
 * is not.
 *
 * @param fullPath  e.g. "DAQiFi/P001/experiment-65.csv"
 * @param directory e.g. "DAQiFi"
 * @return e.g. "P001/experiment-65.csv"
 */
static inline const char *SdManifest_RelativeName(const char *fullPath,
                                                  const char *directory)
{
    size_t dirLen;

    if (fullPath == NULL) {
        return NULL;
    }
    if (directory == NULL) {
        return fullPath;
    }
    dirLen = strlen(directory);
    if (dirLen == 0u) {
        return fullPath;
    }
    if (strncmp(fullPath, directory, dirLen) != 0) {
        return fullPath;
    }
    /* The separator must be there. Without this test "DAQiFi2/x.csv" would
     * match the directory "DAQiFi" and be recorded as "/x.csv". */
    if (fullPath[dirLen] != '/') {
        return fullPath;
    }
    /* "DAQiFi/" with nothing after it is not a file name; hand back the whole
     * string rather than an empty one. */
    if (fullPath[dirLen + 1u] == '\0') {
        return fullPath;
    }
    return fullPath + dirLen + 1u;
}

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
    while (*a != '\0' && *b != '\0') {
        char ca = *a;
        char cb = *b;
        if (ca >= 'A' && ca <= 'Z') {
            ca = (char)(ca - 'A' + 'a');
        }
        if (cb >= 'A' && cb <= 'Z') {
            cb = (char)(cb - 'A' + 'a');
        }
        if (ca != cb) {
            return false;
        }
        a++;
        b++;
    }
    return (*a == '\0') && (*b == '\0');
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

    return data[i] == '\n';
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
                                                   uint32_t *crcOut)
{
    if (bytesOut != NULL) {
        *bytesOut = 0u;
    }
    if (crcOut != NULL) {
        *crcOut = CRC32_Init();
    }
}

#ifdef __cplusplus
}
#endif

#endif /* UTIL_SD_MANIFEST_H */
