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

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

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

#ifdef __cplusplus
}
#endif

#endif /* UTIL_SD_MANIFEST_H */
