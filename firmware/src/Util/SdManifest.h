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
 * where <name> is the file's FULL path, configured directory included
 * (#1124 r13, operator item 8 = B). A host can still feed a manifest line
 * straight back to `SYSTem:STORage:SD:CRC?` or `SYSTem:STORage:SD:GET`,
 * because retrieval strips the configured-directory prefix -- so the full path
 * is stripped exactly ONCE and names the file it describes. For a session that
 * stays in bucket 0 that is `DAQiFi/experiment-3.csv`; once #689 bucketing
 * rolls, it is `DAQiFi/P001/experiment-65.csv`.
 *
 * ⚠ Through r12 this said RELATIVE to the configured directory, and that is
 * what made the record ambiguous: retrieval strips the directory too, so a
 * relative name was stripped TWICE and could open a different file. Recording
 * the full path is what retired the P###-folder-name refusal.
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
 * @param name    The file's FULL path, configured directory included -- NOT
 *                relative to it (#1124 r13). See the format note at the top
 *                of this header for why.
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
 * mistakable for a split part.
 *
 * ⛔ #1124 r17/r18: THE NAME IS THE FULL DATA FILENAME PLUS THIS EXTENSION, so
 * `experiment.csv` logs to `experiment.csv.mfst`. It is NOT the session's base
 * name: `<base>.mfst` was ambiguous across data files, because stripping the
 * last extension makes `alpha.csv` and `alpha.csv.json` claim overlapping
 * names. Deriving from the COMPLETE filename makes "is this the manifest for
 * that file?" decidable from the name alone, which is what the companion
 * parser keys on -- so this paragraph is a wire contract, not a note.
 *
 * One manifest per SESSION, holding one line per stream file; a rolled part
 * `experiment-1.csv` does not get its own. A session logging under a different
 * SD:FILE gets its own manifest rather than overwriting this one. */
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

/* #1124 r21: does `name` end in the manifest extension?
 *
 * Case-INSENSITIVELY, because FAT does not distinguish case: a manifest whose
 * 8.3 alias reaches us as "EXP.MFS"/"exp.mfst" is the same file either way, and
 * a case-sensitive test would read a real manifest as an ordinary file and so
 * scan nothing. ASCII only, for the reason SdManifest_PathsEqualCaseInsensitive
 * gives at length: strcasecmp is not used anywhere else in this firmware, so
 * do not assume the XC32 newlib exposes it.
 *
 * A name that is EXACTLY the extension with no stem (".mfst") is NOT a
 * manifest: SdManifest_FormatLine's callers always write <something>.mfst, so
 * a bare extension is some other file and reading it as a manifest would make
 * its contents decide a refusal. */
/* ⛔ #1124 r28: THE LENGTH-TAKING CORE EXISTS BECAUSE THE SCPI SETTER HAS NO
 * TERMINATOR. SCPI_ParamCharacters hands back a pointer INTO the command
 * buffer plus a length, not a C string -- which is why SD_ValidatePathParam
 * takes a length and why the '~' check this fix generalises was written with
 * memchr. A strlen-based predicate called on that operand reads past the
 * operand into the rest of the command line.
 *
 * So the rule has one body and two entry points, rather than a bounded copy at
 * the setter and a terminated copy at use. A second implementation of "does
 * this end in the manifest extension" is precisely the shape that cost this
 * row three rounds on the part-suffix rule. */
static inline bool SdManifest_NameHasManifestExtLen(const char *name,
                                                     size_t nameLen)
{
    const size_t extLen = sizeof(SD_MANIFEST_EXT) - 1u;
    const char *tail;
    const char *ext = SD_MANIFEST_EXT;
    size_t i;

    if (name == NULL) {
        return false;
    }
    if (nameLen <= extLen) {
        return false;
    }
    tail = name + (nameLen - extLen);
    for (i = 0u; i < extLen; i++) {
        char a = tail[i];
        char b = ext[i];
        if ((a >= 'A') && (a <= 'Z')) { a = (char)(a - 'A' + 'a'); }
        if ((b >= 'A') && (b <= 'Z')) { b = (char)(b - 'A' + 'a'); }
        if (a != b) {
            return false;
        }
    }
    return true;
}

static inline bool SdManifest_NameHasManifestExt(const char *name)
{
    if (name == NULL) {
        return false;
    }
    return SdManifest_NameHasManifestExtLen(name, strlen(name));
}

/* ⛔⛔⛔ #1124 r29 AUDIT HIGH (:232): THE SECOND QUESTION, WHICH NEEDS THE
 * OPPOSITE ANSWER FOR THE BARE EXTENSION — SO IT NEEDS ITS OWN PREDICATE.
 *
 * "Is this name RESERVED for manifests, so a logging session may not take it?"
 * That is NOT the question SdManifest_NameHasManifestExt answers, and r29
 * found the difference the hard way. The two consumers:
 *
 *   the foreign SCAN   "is this FILE on the card a manifest I should READ?"
 *                      bare ".mfst" -> NO. Every writer emits
 *                      <something>.mfst, so a bare extension is some other
 *                      file, and reading it as a manifest would let its
 *                      CONTENTS decide a refusal. The carve-out is correct
 *                      and deliberate.
 *   this one           "would configuring this name put this session's own
 *                      files on a manifest name?"
 *                      bare ".mfst" -> YES, and it is the worst case of all.
 *
 * ⛔ WHAT r28's FIX MISSED, because it reused the scan's predicate: with
 * SD:FILE ".mfst" the length carve-out accepted the name, extractBaseFilename
 * produced an EMPTY stem, and rotation targeted "-1.mfst" -- the manifest of
 * any session named "-1". With the overwrite grant that TRUNCATES another
 * capture's integrity record. r28's refusal checked the TAIL and a bare
 * extension has no tail to check.
 *
 * ⭐⭐ AND THE SHARED PREDICATE COULD NOT SIMPLY BE TIGHTENED. Dropping the
 * length carve-out would have fixed this caller and broken the scan, which
 * must keep answering NO for the same string. Two consumers, one spelling,
 * OPPOSITE correct answers -- so the discriminator is the QUESTION, not the
 * text, and the only sound fix is two predicates. They are deliberately named
 * after their questions rather than their mechanism, because two names
 * describing the same string operation is how the next reader picks the wrong
 * one. Pinned in tests/host/Makefile so they cannot be unified.
 *
 * ⭐ WHY THE EXTENSION TEST IS SUFFICIENT, which is what makes this a complete
 * fix rather than a patch for one spelling: a session's files are the
 * configured name itself and "<base>-<N><ext>", where <ext> is the configured
 * name's last extension. If that extension is not the manifest extension then
 * neither form can end in it -- a part ends in <ext>, or in a DIGIT when there
 * is no extension at all. So refusing the extension here forecloses every
 * name this session could place on a manifest path, at any counter value and
 * in any bucket.
 *
 * ⚠ AN EMPTY BASE IS NOT REFUSED, deliberately, and it was checked rather than
 * assumed: SD:FILE ".csv" gives parts "-1.csv" and
 * SdManifest_PartIndexOf("D/-1.csv", "D", ".csv") returns 1, while
 * ("D/.csv", "D", ".csv") returns 0 -- so the foreign-record guard already
 * matches them and there is no hazard to refuse. Restricting it anyway would
 * cost input for no demonstrated failure. */
static inline bool SdManifest_NameReservedForManifestsLen(const char *name,
                                                           size_t nameLen)
{
    const size_t extLen = sizeof(SD_MANIFEST_EXT) - 1u;
    const char *tail;
    size_t i;

    if (name == NULL) {
        return false;
    }
    /* `<` not `<=`: a name that IS exactly the extension is the case r29
     * found, and it is reserved. This one character is the whole difference
     * from the scan's predicate. */
    if (nameLen < extLen) {
        return false;
    }
    tail = name + (nameLen - extLen);
    for (i = 0u; i < extLen; i++) {
        /* ⚠ The fold is written out rather than calling SdManifest_FoldChar,
         * matching the sibling above for the same reason: FoldChar is defined
         * further DOWN this header, so calling it here is an implicit
         * declaration -- an error under the firmware's -Wall -Werror even
         * though a host .c including the header only warns. The host suite's
         * -Werror gate on this header is what caught it. */
        char a = tail[i];
        char b = SD_MANIFEST_EXT[i];
        if ((a >= 'A') && (a <= 'Z')) { a = (char)(a - 'A' + 'a'); }
        if ((b >= 'A') && (b <= 'Z')) { b = (char)(b - 'A' + 'a'); }
        if (a != b) {
            return false;
        }
    }
    return true;
}

static inline bool SdManifest_NameReservedForManifests(const char *name)
{
    if (name == NULL) {
        return false;
    }
    return SdManifest_NameReservedForManifestsLen(name, strlen(name));
}

/* ⛔⛔⛔ #1124 r28 AUDIT HIGH, BOTH LEGS (SCPIStorageSD.c:1119, this file:501):
 * COULD THIS PATH COMPONENT BE A FatFs 8.3 ALIAS?
 *
 * FatFs resolves a requested name against each directory entry's STORED 8.3
 * alias as well as its long name (`dir_find`'s SFN arm), so "LONGDI~1" and
 * "LongDirectoryName" can be the same directory while NO text transform
 * relates the two. Every record comparison in this row is textual, so an alias
 * anywhere in a configured path breaks the comparison in BOTH directions:
 *
 *   - the SESSION names the alias, a record holds the long name
 *     (r28 main :1119): PartIndexOf compares "LongDirectoryName/exp-1.csv"
 *     against a configured "LONGDI~1" prefix, returns NONE, no bit is set and
 *     the exact check never runs -- so rotation truncates a recorded file.
 *   - the SESSION names the long form, a record holds the alias
 *     (r28 blind :501): the record was written by an EARLIER session that was
 *     configured by alias, so it is a device-written record this firmware
 *     cannot match.
 *
 * ⛔ WHAT WAS WRONG BEFORE, AND IT WAS A CLAIM, NOT JUST A MISSING CHECK. r22
 * added this refusal to the overwrite opt-in's whitelist ONLY, and the
 * reasoning written down around it -- here, and in the PR's documented
 * residual -- was that alias exposure therefore required a HAND-EDITED or
 * third-party manifest, because "manifests this firmware writes always use
 * long names in every component". That is false. SYST:STOR:SD:DIR accepts
 * "LONGDI~1" with no opt-in anywhere in sight, every manifest path is built
 * from the CONFIGURED directory text, and so ordinary refuse-by-default
 * logging produces an alias-bearing record. The residual rested on a property
 * nothing enforced.
 *
 * ⭐ So the rule moves from the GRANT to the CONFIGURATION: no session logs
 * with '~' in its directory or its name, opt-in or not. That makes the
 * "device-written manifests use long names" claim TRUE BY CONSTRUCTION instead
 * of by assumption, which is the only version of it worth relying on.
 *
 * Whole-string rather than per-segment, deliberately: '~' is not a character
 * this firmware ever needs to write, so there is nothing to gain from being
 * precise about which component carries it.
 *
 * ⚠ THE RESIDUAL THAT REMAINS, stated exactly: a record written by SOMETHING
 * ELSE -- a third party, or a build of this firmware predating this check --
 * may still hold an alias while this session names the long form. That cannot
 * be closed by a string rule at all; it needs directory-entry identity. */
/* ⛔⛔⛔ #1124 r30 AUDIT HIGH, blind leg (SCPIStorageSD.c:1165): A NON-ASCII
 * NAME IS REFUSED FOR THE SAME REASON '~' IS, AND THE REASON IS SHARPER THAN
 * THE ONE THE RESIDUAL GAVE.
 *
 * The posted residual said this firmware folds case for ASCII only while FatFs
 * folds the OEM code page, so two spellings of an accented name can be
 * FatFs-equal and textually different -- and claimed the destructive route was
 * closed because the overwrite opt-in refused non-ASCII. r30 showed that
 * closes nothing, because THE ALIAS FatFs STORES FOR A NON-ASCII NAME CAN BE
 * PURE ASCII:
 *
 *   1. an ordinary, UNGRANTED session creates `\x82.csv` (CP437 e-acute);
 *   2. create_name converts the SFN char through `ExCvt[wc & 0x7F]`, and the
 *      TBL_CT437 we ship begins {0x80,0x9A,0x45,0x41,...}, so ExCvt[0x02] is
 *      0x45 -- ASCII 'E'. NS_LOSS is never set (0x45 is a legal SFN char), so
 *      there is NO "~N" tail: the stored alias is a clean `E.CSV`;
 *   3. `SD:FILE "E.csv",1` is PURE ASCII, so the opt-in's ASCII restriction
 *      passes and the grant is given;
 *   4. PartIndexOf compares `E.csv` against the record's 0x82 name, does not
 *      match, no foreign check runs, and FatFs resolves `E.csv` through the
 *      stored alias and TRUNCATES the original capture.
 *
 * ⭐ NOT ONE UNLUCKY CHARACTER: 18 of the 128 high CP437 bytes produce a
 * pure-ASCII SFN character -- 0x82/0x88/0x89/0x8A->'E', 0x83/0x85/0xA0->'A',
 * 0x8B/0x8C/0x8D/0xA1->'I', 0x93/0x95/0xA2->'O', 0x96/0x97/0xA3->'U',
 * 0x98->'Y'. Every accented vowel in CP437 has an unnumbered ASCII alias.
 *
 * ⛔ SO THE RESTRICTION MOVES FROM THE GRANT TO THE CONFIGURATION, exactly as
 * the '~' one did: no session logs with a non-ASCII directory or name, opt-in
 * or not, so the firmware can never CREATE a file whose stored alias collides
 * with a name a later session may legitimately ask for. Restricting only the
 * grant protected the truncation route and left CREATION open -- and creation
 * is what manufactures the alias.
 *
 * ⚠ WHAT THIS COSTS, stated because it is a real loss and not a free win: a
 * filename or directory containing an accented vowel can no longer be used for
 * logging at all. That is most realistic non-ASCII cases, which is why it was
 * put to the operator rather than taken as obvious. Modelling CP437 instead
 * was rejected: the table would have to track whatever FF_CODE_PAGE is set to
 * and would go silently wrong if that moved.
 *
 * ⚠ READ/CRC/DELETE ARE UNAFFECTED, deliberately -- as with '~'. Refusing to
 * read back or delete a file that already exists on the card would strand it,
 * which is worse than the hazard. The at-use check sits under
 * `!usesOperandName` for that reason.
 *
 * ASCII here means printable ASCII, 0x20..0x7E: the control range and 0x7F are
 * already refused by SD_ValidatePathParam, and treating them as "ASCII" here
 * would be a second, weaker opinion about the same bytes. */
static inline bool SdManifest_PathHasNonAsciiLen(const char *p, size_t len)
{
    const unsigned char *u = (const unsigned char *)p;
    size_t i;

    if (p == NULL) {
        return false;
    }
    for (i = 0u; i < len; i++) {
        /* unsigned char is load-bearing: `char` is SIGNED on this target, so a
         * byte >= 0x80 compares as NEGATIVE and `c > 0x7E` reads false. */
        if ((u[i] < 0x20u) || (u[i] > 0x7Eu)) {
            return true;
        }
    }
    return false;
}

static inline bool SdManifest_PathHasNonAscii(const char *p)
{
    if (p == NULL) {
        return false;
    }
    return SdManifest_PathHasNonAsciiLen(p, strlen(p));
}

static inline bool SdManifest_PathHasAliasRiskLen(const char *p, size_t len)
{
    return (p != NULL) && (memchr(p, '~', len) != NULL);
}

static inline bool SdManifest_PathHasAliasRisk(const char *p)
{
    if (p == NULL) {
        return false;
    }
    return SdManifest_PathHasAliasRiskLen(p, strlen(p));
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

/* #1124 r21: ASCII case-fold, one definition. Folded the same way
 * SdManifest_PathsEqualCaseInsensitive folds, and for the same reason: FAT does
 * not distinguish case, and strcasecmp is not assumed present in this XC32
 * newlib. */
static inline char SdManifest_FoldChar(char c)
{
    return ((c >= 'A') && (c <= 'Z')) ? (char)(c - 'A' + 'a') : c;
}

static inline bool SdManifest_SegmentEqualFolded(const char *a, const char *b,
                                                  size_t n)
{
    size_t i;
    for (i = 0u; i < n; i++) {
        if (SdManifest_FoldChar(a[i]) != SdManifest_FoldChar(b[i])) {
            return false;
        }
    }
    return true;
}

/* ⛔⛔ #1124 r22 AUDIT FOLLOW-UP: ONE DEFINITION OF "THE NEXT PATH SEGMENT".
 *
 * These exist so SdManifest_PartIndexOf and
 * SdManifest_PathsEqualCaseInsensitive normalise IDENTICALLY, because they are
 * two stages of ONE gate and the cheaper stage decides whether the careful one
 * runs at all.
 *
 * ⛔ WHY, measured: r21 put a once-per-session bitset prefilter in front of the
 * exact comparison to cut cost. The comparator models five FatFs equivalences
 * (E1-E5, enumerated against ff.c in its own header below). The prefilter
 * modelled NONE of them -- it compared whole strings with a plain case-folded
 * byte compare. Since a clear bit SKIPS the comparator, the guard's real
 * strength silently became the prefilter's: a record spelled `D//exp-1.csv` or
 * `D/exp-1.csv.` set no bit, the careful comparator was never consulted, and
 * the collision was missed.
 *
 * ⭐ ADDING A CHEAP STAGE IN FRONT OF A CAPABLE ONE DOES NOT INHERIT ITS
 * CAPABILITY -- IT CAPS IT. The guard is min(stages). It was invisible to every
 * test, because the tests used names OUR code produces, which are canonical by
 * construction; the gap only opens for a sidecar written by something else.
 *
 * Extracted rather than reimplemented, deliberately: a second normalisation
 * would be the fifth naming rule, and this row has four consecutive rounds of
 * evidence about what that costs. The comparator's behaviour is unchanged -- it
 * calls these instead of inlining the same steps, and its existing equivalence
 * tests are the proof. */

/* Trailing SPACES and DOTS are not part of a segment (E1, ff.c:2895-2899). */
static inline size_t SdManifest_SegmentTrimmedLen(const char *s, size_t len)
{
    while (len > 0u && (s[len - 1u] == ' ' || s[len - 1u] == '.')) {
        len--;
    }
    return len;
}

/* Advance `*p` to the next path segment, FatFs-style: runs of '/' are skipped,
 * covering LEADING (E4), DUPLICATED (E2) and TERMINATING (E3) separators in one
 * step. `*seg`/`*len` describe the segment with E1 already applied.
 *
 * ⚠ A segment that trims to ZERO length (all dots/spaces) is RETURNED as such
 * and not skipped, because that is what the comparator did before this
 * extraction; changing it would be a silent behaviour edit riding along with a
 * refactor. */
static inline bool SdManifest_NextSegment(const char **p, const char **seg,
                                           size_t *len)
{
    const char *s = *p;
    size_t n = 0u;

    while (*s == '/') {
        s++;
    }
    if (*s == '\0') {
        *p = s;
        return false;
    }
    while (s[n] != '\0' && s[n] != '/') {
        n++;
    }
    *seg = s;
    *len = SdManifest_SegmentTrimmedLen(s, n);
    *p = s + n;
    return true;
}

/* Two segments equal, E1 + E5. Lengths are the TRIMMED ones. */
static inline bool SdManifest_SegmentsEqual(const char *a, size_t la,
                                             const char *b, size_t lb)
{
    if (la != lb) {
        return false;
    }
    return SdManifest_SegmentEqualFolded(a, b, la);
}

/* ⛔⛔ #1124 r21 / THE PURE HALF OF THE FOREIGN-RECORD GUARD.
 *
 * Given a name RECORDED in some manifest, decide which part index of a session
 * based at `dir` + `file` that name could be -- i.e. "is this one of the names
 * that session will itself write, and if so, which one?"
 *
 * This exists so the expensive question is asked ONCE PER SESSION instead of
 * once per rotation. The caller reads each foreign manifest a single time at
 * the session's first open, maps every record through this function, and sets
 * a bit per index; each later open then tests one bit. See the cost note at
 * sd_ScanForeignManifests.
 *
 * ⭐ A BIT IS A NECESSARY CONDITION, NOT A SUFFICIENT ONE, and the caller must
 * treat it that way. The index does NOT determine the path: a part is written
 * to <bucket>/<stem>-<N><ext>, and which bucket depends on how full the
 * buckets are when that part is opened, which is not known at scan time. So a
 * record and a part can share dir, stem, ext and N and still be different
 * files -- `D/exp-1.csv` (a foreign session whose BASE name was exp-1.csv)
 * versus `D/P001/exp-1.csv` (our part 1, after bucket 0 filled). A set bit
 * therefore means "an exact check is owed for this index", never "refuse".
 *
 * Returns the index, or SDMANIFEST_PART_NONE when the name cannot be one of
 * that session's own names.
 *
 * ⛔⛔ THERE IS NO FAULT RETURN, AND THAT IS THE INVARIANT (r23 audit :424).
 * It had a `bool *fault` out-param, set when an index would not fit a
 * uint32_t. The caller turned that into a refusal, so a foreign file called
 * `exp-4294967296.csv` disarmed every capture in the directory. Removing the
 * parameter rather than leaving it always-false is deliberate: an out-param
 * that can never be true still invites the next reader to add a case to it,
 * and the property worth making un-addable is
 *
 *     A FOREIGN NAME CAN NEVER FAULT THE SCAN.
 *
 * Only unreadable or unparseable manifest CONTENT may do that. A name is an
 * INPUT: its length, its digits and its spelling are things the outside world
 * chooses, and none of them is an error condition for us. Three rounds
 * produced three violations of this (a manifest COUNT cap, an over-limit
 * index, an overflowing index), all of them a fail-closed branch reasoning
 * about someone else's name using our own limits.
 *
 * DELIBERATELY A SUPERSET in one respect: any single intermediate segment is
 * accepted as a possible bucket, rather than matching "P%03u" exactly, so this
 * header needs no dependency on sd_card_manager.h. Over-matching only costs an
 * extra exact check; under-matching would miss a collision, so the slack is on
 * the safe side.
 *
 * ⚠ LIMITATION, stated rather than papered over: the comparison is TEXTUAL
 * (case-folded). Every record this firmware writes comes from its own
 * canonical construction, so an exact textual match is sound for them. A
 * hand-edited or third-party .mfst could hold a name that is FatFs-EQUAL to a
 * part but textually different -- a trailing dot or space, a duplicated
 * separator, an 8.3 alias -- and this would not match it. Chasing the full
 * FatFs equivalence set here is the naming-rule treadmill that cost rounds
 * 16-19.
 *
 * ⛔⛔ AND THE SENTENCE THAT USED TO END THIS PARAGRAPH WAS FALSE, which is
 * worth more than the limitation itself. It read: "the exposure is narrow:
 * without the overwrite opt-in the per-open existence check still refuses an
 * existing target, so it takes a granted session AND a hand-edited manifest to
 * reach." Both halves failed on r28:
 *
 *   - "a granted session" -- no. A session ROTATES without any grant, and
 *     generateFilename derives each part name from the configured stem, so an
 *     unmatched record's name is recreated by ordinary refuse-by-default
 *     logging whenever its data file was deleted while the record was kept.
 *     The per-open existence check cannot refuse what does not exist.
 *   - "a hand-edited manifest" -- no. SYST:STOR:SD:DIR accepted "LONGDI~1"
 *     with no opt-in, and manifest paths are built from the configured
 *     directory TEXT, so this firmware WROTE alias-bearing records itself.
 *
 * Both are now closed at the configuration rather than argued away:
 * SdManifest_PathHasAliasRisk is applied in both SCPI setters and again at
 * use, so a '~' never enters a configured path. What remains is a record
 * written by something else -- a third party, or a build predating that check
 * -- which no string rule can resolve; it needs directory-entry identity.
 *
 * ⭐ THE GENERAL LESSON, because this is the second residual in this row
 * retracted for the same reason (the other was "nested buckets cannot arise"):
 * A RESIDUAL JUSTIFIED BY "THIS FIRMWARE CANNOT PRODUCE THAT" IS ONLY AS GOOD
 * AS THE CODE THAT REFUSES TO PRODUCE IT. If nothing enforces the premise, the
 * residual is not narrow -- it is unmeasured.
 */
#define SDMANIFEST_PART_NONE  0xFFFFFFFFu

/* ⛔⛔⛔ #1124 r28 AUDIT MEDIUM, BOTH LEGS (:2752): THE HIGHEST PART INDEX A
 * SESSION CAN EVER WRITE. Cross-tied by a _Static_assert in sd_card_manager.c
 * to SD_FOREIGN_PART_LIMIT - 1, which is where the number comes from: the
 * rotation ceiling (SD_CARD_MANAGER_MAX_SPLIT_FILES) plus the #958
 * discriminator's probe span (SD_CARD_MANAGER_UNIQUE_TARGET_MAX). It is
 * spelled literally here because this header is host-includable and must not
 * depend on the manager's; the assert is what keeps that from being a second
 * source of truth. Move either macro and the FIRMWARE BUILD breaks -- which is
 * the point, because a drift here is silent in every other instrument. */
#define SDMANIFEST_PART_INDEX_MAX  10063u

/* ⛔⛔⛔ #1124 r28: THE ONE PARSER FOR OUR "-%u" PART SUFFIX. THREE ROUNDS WENT
 * ON THIS PREDICATE BECAUSE IT EXISTED TWICE.
 *
 * `[d, digitsEnd)` is the digit run between the '-' and the extension. Returns
 * the index it denotes, or SDMANIFEST_PART_NONE when that run is not something
 * generateFilename's "%s-%u%s" can emit FOR A ROTATION PART.
 *
 * ⛔ WHY ONE FUNCTION AND NOT ONE RULE WRITTEN TWICE, measured over r21-r28:
 *
 *   r21  PartIndexOf learns to reject a PADDED suffix ("-01").
 *   r28  sd_NameIsRotationPartOfExistingManifest, one file away, is found
 *        still accepting it -- written at r20, never updated. Fixed by
 *        COPYING the rule across.
 *   r28  the copy is found incomplete in BOTH places, for two more reasons
 *        neither had: "-0" and an out-of-range index.
 *
 * The copy was the defect. Two consumers of one rule drift by default, and the
 * second round of copying would have been the fourth. So the rule now has a
 * single body and both call it; a future reason to reject lands in one place
 * or in neither.
 *
 * THE THREE REASONS A DIGIT RUN IS NOT OURS, each with its own consequence:
 *
 *  1. PADDED ("-01", "-007"). "%u" never pads, so no session emits it.
 *  2. ZERO ("-0"). counter 0 emits the UNSUFFIXED name -- generateFilename's
 *     counter==0 arm writes "<stem><ext>" -- so "<stem>-0<ext>" is a name no
 *     session can produce at any counter value. This is the r28 finding: the
 *     r21 rule tested `d[0]=='0' && run > 1`, which lets a LONE '0' through,
 *     and the two consumers then disagreed about what index 0 meant --
 *     PartIndexOf returned 0, the index reserved for the unsuffixed name, so a
 *     foreign `D/exp-0.csv` claimed to be our `D/exp.csv`.
 *  3. OUT OF RANGE (> SDMANIFEST_PART_INDEX_MAX, which includes anything that
 *     would not fit a uint32_t). No session reaches such a counter, and the
 *     bitset has no bit for it -- sd_ForeignBitTest already returns false
 *     above the limit, and a session whose own counter passes the limit falls
 *     back to the per-open exact content scan, so bounding here cannot lose a
 *     collision.
 *
 * ⛔ ALL THREE ARE "NONE", NEVER A FAULT. Both consumers reason about a name
 * the OUTSIDE WORLD chose: for PartIndexOf a foreign record, for the rotation
 * check a freshly configured filename. The r28 finding is the fourth and fifth
 * instance of a refusal fired by a foreign name -- `exp-0.csv` and
 * `exp-4294967296.csv` are both valid fresh filenames that the predicate
 * stripped down to an unrelated `exp.csv.mfst` and refused the capture over,
 * with no opt-in able to override it. See PartIndexOf's header: A FOREIGN NAME
 * CAN NEVER FAULT THE SCAN, and by the same argument it can never refuse a
 * capture it does not name.
 *
 * ⚠ NOT FOR BUCKET NAMES. Buckets are "P%03u" and `P001` is padded BY DESIGN,
 * so sd_DirLeafIsBucket and SD_DirHasBucketComponent need the OPPOSITE rule
 * and must not call this. That is the enumeration trap r28 named: "every
 * consumer" means every consumer of THIS rule, not every site that looks
 * similar. The Makefile pins both directions. */
static inline uint32_t SdManifest_ParsePartIndexDigits(const char *d,
                                                        const char *digitsEnd)
{
    uint32_t idx = 0u;

    if (d == NULL || digitsEnd == NULL || d >= digitsEnd) {
        return SDMANIFEST_PART_NONE;        /* no digits at all */
    }
    /* Reason 1 AND reason 2 in one test: a leading '0' is either padding or
     * the lone zero, and neither is our formatter's output. */
    if (d[0] == '0') {
        return SDMANIFEST_PART_NONE;
    }
    for (; d < digitsEnd; d++) {
        if ((*d < '0') || (*d > '9')) {
            return SDMANIFEST_PART_NONE;    /* "-foo" is not a part index */
        }
        /* Reason 3, bounded by OUR ceiling rather than by uint32_t's, so an
         * arbitrarily long digit run can neither overflow nor be accepted. */
        if (idx > ((SDMANIFEST_PART_INDEX_MAX - (uint32_t)(*d - '0')) / 10u)) {
            return SDMANIFEST_PART_NONE;
        }
        idx = (idx * 10u) + (uint32_t)(*d - '0');
    }
    /* ⛔ THE RANGE [1, SDMANIFEST_PART_INDEX_MAX] IS A POST-CONDITION OF THE
     * TWO GUARDS ABOVE, AND THERE IS DELIBERATELY NO THIRD TEST RESTATING IT.
     *
     * Lower bound: `d[0] == '0'` is rejected, so the first digit is 1-9 and
     * idx is >= 1 after one iteration and only grows. Upper bound: the
     * in-loop test rejects any digit that would carry idx past the ceiling.
     *
     * ⚠ A `if ((idx < 1u) || (idx > SDMANIFEST_PART_INDEX_MAX))` WAS written
     * here, as "belt and braces". A mutant (R83) then neutered it and the
     * whole suite still passed -- which is the proof that it was unreachable,
     * i.e. dead code pretending to be a safety net. It is removed rather than
     * kept with a comment, because an unreachable guard invites the next
     * reader to weaken a REACHABLE one in the belief that this still catches
     * it. R83 now mutates the in-loop ceiling, which is the line that
     * actually enforces. */
    return idx;
}

/* How many FatFs segments does `p` have? Uses the one iterator, so a doubled
 * or trailing separator does not change the count. */
static inline unsigned SdManifest_CountSegments(const char *p)
{
    const char *seg;
    size_t len = 0u;
    unsigned n = 0u;

    if (p == NULL) {
        return 0u;
    }
    while (SdManifest_NextSegment(&p, &seg, &len)) {
        n++;
    }
    return n;
}

static inline uint32_t SdManifest_PartIndexOf(const char *name,
                                               const char *dir,
                                               const char *file)
{
    const char *np;
    const char *fp;
    const char *seg;
    const char *leaf = NULL;
    size_t segLen = 0u;
    size_t leafLen = 0u;
    size_t stemLen;
    size_t extLen;
    const char *fileLeaf = NULL;
    size_t fileLeafLen = 0u;
    const char *ext;
    const char *dot;
    const char *d;
    const char *digitsEnd;
    unsigned nameSegs;
    unsigned dirSegs;
    unsigned fileSegs;
    unsigned i;
    bool bucketPresent;
    uint32_t idx = 0u;

    if (name == NULL || dir == NULL || file == NULL) {
        return SDMANIFEST_PART_NONE;
    }

    /* ---- 1. the SHAPE, counted before anything is compared ---------------
     * ⛔⛔ #1124 r28 AUDIT HIGH (:469): THE CONFIGURED NAME MAY ITSELF CONTAIN
     * SEPARATORS, and r27 treated it as ONE segment.
     *
     * SD:FILE accepts "P001/exp.csv" (the setter permits '/'), and
     * generateFilename then derives baseFilename by stripping only the last
     * extension -- "P001/exp" -- so the session writes
     *
     *     <dir> / [bucket] / <file's own directories> / <leaf>
     *
     * e.g. `D/P001/exp.csv` at index 0 and `D/P002/P001/exp-7.csv` for part 7
     * in bucket 2. Note the BUCKET COMES FIRST, before the name's own
     * subdirectory, because generateFilename joins onto bucketPath.
     *
     * Measured on r27: PartIndexOf("D/P001/exp.csv", "D", "P001/exp.csv") was
     * NONE -- a name that session WRITES, unmatched, so no bit was set and the
     * exact check never ran. Fail-OPEN, and independent of where the foreign
     * manifest lived: this one fails even for a directory we DO scan.
     *
     * ⭐ ANCHORED AT BOTH ENDS, which is what makes it unambiguous. `dir`
     * matches at the front and `file` matches at the back; whatever sits
     * between them is zero or one bucket segment. Matching left-to-right could
     * not tell the bucket `P001` from the name's own `P001`. */
    nameSegs = SdManifest_CountSegments(name);
    dirSegs  = SdManifest_CountSegments(dir);
    fileSegs = SdManifest_CountSegments(file);
    if (fileSegs == 0u) {
        return SDMANIFEST_PART_NONE;    /* no name to write */
    }
    if (nameSegs == (dirSegs + fileSegs)) {
        bucketPresent = false;
    } else if (nameSegs == (dirSegs + fileSegs + 1u)) {
        bucketPresent = true;           /* exactly one rotation bucket */
    } else {
        return SDMANIFEST_PART_NONE;    /* not a shape we ever write */
    }

    /* ---- 2. `dir` must match at the front, segment-wise ------------------ */
    np = name;
    {
        const char *dp = dir;
        for (i = 0u; i < dirSegs; i++) {
            const char *dseg;
            size_t dsegLen = 0u;
            if (!SdManifest_NextSegment(&dp, &dseg, &dsegLen)
                    || !SdManifest_NextSegment(&np, &seg, &segLen)
                    || !SdManifest_SegmentsEqual(seg, segLen, dseg, dsegLen)) {
                return SDMANIFEST_PART_NONE;
            }
        }
    }

    /* ---- 3. skip the bucket, whatever it is called ----------------------- */
    if (bucketPresent && !SdManifest_NextSegment(&np, &seg, &segLen)) {
        return SDMANIFEST_PART_NONE;
    }

    /* ---- 4. `file`'s own directories must match, then its leaf is the
     * pattern the last name segment has to satisfy. */
    fp = file;
    for (i = 0u; i < fileSegs; i++) {
        const char *fseg;
        size_t fsegLen = 0u;
        if (!SdManifest_NextSegment(&fp, &fseg, &fsegLen)
                || !SdManifest_NextSegment(&np, &seg, &segLen)) {
            return SDMANIFEST_PART_NONE;
        }
        if (i + 1u < fileSegs) {
            if (!SdManifest_SegmentsEqual(seg, segLen, fseg, fsegLen)) {
                return SDMANIFEST_PART_NONE;
            }
        } else {
            leaf = seg;
            leafLen = segLen;
            fileLeaf = fseg;
            fileLeafLen = fsegLen;
        }
    }
    if (leaf == NULL || fileLeaf == NULL || fileLeafLen == 0u) {
        return SDMANIFEST_PART_NONE;
    }

    /* ---- 5. index 0 is the configured leaf itself ------------------------
     * In the directory OR in a bucket (r24 :479): the bucket-advance loop runs
     * before generateFilename, so a full directory puts the session's FIRST
     * file inside a bucket. */
    if (SdManifest_SegmentsEqual(leaf, leafLen, fileLeaf, fileLeafLen)) {
        return 0u;
    }

    /* ---- 6. otherwise "<stem>-<N><ext>", N unpadded as "%u" emits -------- */
    dot = NULL;
    for (i = 0u; i < (unsigned)fileLeafLen; i++) {
        if (fileLeaf[i] == '.') {
            dot = fileLeaf + i;
        }
    }
    stemLen = (dot != NULL) ? (size_t)(dot - fileLeaf) : fileLeafLen;
    ext = (dot != NULL) ? dot : "";
    extLen = (dot != NULL) ? (fileLeafLen - stemLen) : 0u;

    if (leafLen < (stemLen + 2u + extLen)) {
        return SDMANIFEST_PART_NONE;
    }
    if (!SdManifest_SegmentEqualFolded(leaf, fileLeaf, stemLen)) {
        return SDMANIFEST_PART_NONE;
    }
    if (leaf[stemLen] != '-') {
        return SDMANIFEST_PART_NONE;
    }
    if (extLen != 0u
            && !SdManifest_SegmentEqualFolded(leaf + (leafLen - extLen), ext,
                                               extLen)) {
        return SDMANIFEST_PART_NONE;
    }
    /* ⛔ r28: THE SHARED PARSER, not a second copy of the rule. The padded /
     * zero / out-of-range reasons and the "never a fault" property are stated
     * once, in SdManifest_ParsePartIndexDigits, because this and
     * sd_NameIsRotationPartOfExistingManifest spent three rounds disagreeing
     * about them. r23's overflow note lives there too. */
    d = leaf + stemLen + 1u;
    digitsEnd = leaf + (leafLen - extLen);
    idx = SdManifest_ParsePartIndexDigits(d, digitsEnd);
    return idx;
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
    /* ⛔ r22: E1-E5 now come from SdManifest_NextSegment / _SegmentsEqual, so
     * SdManifest_PartIndexOf -- the prefilter that decides whether this
     * comparison runs at all -- normalises by the SAME definition. The steps
     * are unchanged; they moved. See the header on those helpers for the
     * defect that forced the extraction.
     *
     * ⛔ LOOP TERMINATION still depends on the separator skip, which now lives
     * inside NextSegment: it leaves `*p` past a segment of at least one
     * character, so each iteration ADVANCES. That dependency was implicit
     * until a mutation test HUNG for 200 s instead of failing -- on the SCPI
     * task, which is worse than any wrong answer. Keep the guaranteed advance
     * if you restructure either function. */
    for (;;) {
        const char *sa;
        const char *sb;
        size_t la = 0u;
        size_t lb = 0u;
        const bool ga = SdManifest_NextSegment(&a, &sa, &la);
        const bool gb = SdManifest_NextSegment(&b, &sb, &lb);

        /* E3: a run of separators at the very end terminates both sides */
        if (!ga || !gb) {
            return ga == gb;
        }
        if (!SdManifest_SegmentsEqual(sa, la, sb, lb)) {   /* E1 + E5 */
            return false;
        }
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
