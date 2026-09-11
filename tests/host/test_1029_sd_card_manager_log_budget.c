/* ==========================================================================
 * test_1029_sd_card_manager_log_budget.c -- issue #1029
 *
 * WHAT IS UNDER TEST
 *
 * Util/Logger.c's LogMessageFormatImpl formats every LOG_E/LOG_I/LOG_D into
 * a fixed 128-byte frame and SILENTLY discards whatever does not fit:
 *
 *     char buffer[LOG_MESSAGE_SIZE];                       // 128
 *     size = vsnprintf(buffer, LOG_MESSAGE_SIZE - 2, format, args);
 *     if (size <= 0) { return 0; }
 *     size = min((LOG_MESSAGE_SIZE - 3), size);            // clamp to 125
 *     ... then append \r\n unless the text already ends in one ...
 *
 * vsnprintf returns what it WOULD have written, and the clamp throws that
 * excess away, so a formatted message longer than LOG_MESSAGE_SIZE - 3 loses
 * its TAIL. Nothing anywhere reports it: SYST:LOG? shows a sentence that
 * simply stops. Whatever the author put LAST is what the operator never sees.
 *
 * Three sd_card_manager.c LOG_E messages put the REMEDY last, and all three
 * were over the ceiling:
 *
 *   site 1  bucket name taken by a non-directory   104 fixed + path -> 149
 *   site 2  no writable bucket remains             207 fixed + 5 subs -> ~298
 *   site 3  writes hang while reads work           192 fixed, no subs -> 192
 *
 * Site 3 has no substitutions at all, so it truncated identically on every
 * firing since it was written; sites 1 and 2 truncated as soon as the
 * configured SD directory name grew (site 1) or always (site 2).
 *
 * THE FIX shortens all three so their WORST CASE fits in 125 bytes, keeping
 * the remedy and dropping narrative that a source comment can carry instead.
 * Site 2 also drops four of its five substitutions (see its comment in
 * sd_card_manager.c for why each was droppable).
 *
 * HOW IT IS TESTED
 *
 * The messages' substitution is gSDCardData.bucketPath, which
 * sd_BuildBucketPath() builds as `<dir>` (bucket 0) or `<dir>/P<NNN>`
 * (bucket != 0) from a directory the SD manager bounds to
 * SD_CARD_MANAGER_CONF_DIR_NAME_LEN_MAX. So the worst case is computable
 * without hardware -- which is the whole reason this is a host test and not a
 * bench test (same call as #1000, this ticket's sibling: a message-LENGTH
 * defect, not a control-flow one).
 *
 * This file therefore:
 *
 *   1. builds the worst-case bucketPath by actually formatting
 *      sd_BuildBucketPath's own format string with a maximum-length
 *      directory and the maximum bucket number -- no arithmetic guess;
 *   2. runs the three SHIPPED message texts through a mirror of
 *      LogMessageFormatImpl (real vsnprintf, same bound, same clamp, same
 *      CRLF cascade) and asserts the emitted bytes equal the intended bytes
 *      and still contain each message's remedy;
 *   3. runs the three HISTORICAL (pre-fix) texts through the identical
 *      mirror at the identical substitutions and asserts they DO overrun,
 *      DO get truncated, and DO lose remedy text -- naming, per site,
 *      exactly which words survived and which did not. Only site 2 lost
 *      every word of remedy; site 1 kept the first of its two clauses and
 *      site 3 kept the first of its two causes, and a test that asserted a
 *      flat "the remedy is gone" would have been wrong about two sites out
 *      of three while still going green on the fixed tree.
 *
 * Step 3 is not decoration. Without it every assertion here would pass on a
 * tree where #1029 was never fixed -- a test that cannot fail. It is what
 * makes this file evidence rather than a restatement.
 *
 * NOTHING HERE IS HAND-COPIED FROM FIRMWARE EXCEPT THE HISTORICAL TEXTS
 *
 * gen_1029_log_budget.py reads LOG_MESSAGE_SIZE, both of Logger.c's
 * reservations, the SD directory/bucket constants, sd_BuildBucketPath's
 * format string and the three live LOG_E format strings out of the real
 * sources at build time into gen_1029_log_budget.h, round-tripping each
 * literal back into the source bytes to prove the extraction is faithful,
 * and FAILS THE BUILD with a named diagnostic on anything it cannot find,
 * finds twice, or cannot reproduce. A stale copy cannot pass silently here.
 *
 * The three HISTORICAL texts are the exception and are frozen literals
 * below, clearly marked -- they cannot be extracted because the fix deleted
 * them from the source. They are what git shows at 1cbf476f2 (the commit
 * this branch is based on).
 *
 * FIDELITY -- what this mirror is NOT
 *
 * 1. logger_format_mirror_v() reproduces LogMessageFormatImpl's body
 *    statement for statement: the same `char buffer[LOG_MESSAGE_SIZE]`, the
 *    same vsnprintf bound, the same min() clamp, the same three-branch CRLF
 *    fixup with the same index arithmetic. It calls the host's real
 *    vsnprintf, so the "would have written" length is computed by a real
 *    printf implementation rather than estimated. What it does NOT do is
 *    call LogMessageAdd() -- the circular buffer, its mutex, the ISR-context
 *    detour and the SYST:LOG? drain all need FreeRTOS. Those paths copy the
 *    already-formatted string; they cannot un-truncate it, which is why the
 *    boundary this test is about is entirely inside the function modelled.
 * 2. The host's vsnprintf is glibc, the firmware's is XC32's newlib. The
 *    only behaviour relied on is C99 7.21.6.12: the return value is the
 *    length the full output WOULD have had, and at most n-1 characters are
 *    written. Both implementations are C99-conforming for that, and none of
 *    these messages uses a locale- or implementation-defined conversion
 *    (%s and %u only).
 * 3. gLogLevels gating (LOG_E only calls LogMessage when the module's level
 *    is >= ERROR) is not modelled: it decides WHETHER a message is emitted,
 *    never how much of it survives.
 * 4. The ISR path (LogMessage's LogIsInISR() branch) is not modelled either.
 *    It has its own, different bound (LOG_MESSAGE_SIZE - 3 by a bounded scan,
 *    format args ignored) and none of these three sites runs in an ISR --
 *    they are all in the SD task.
 * 5. The historical site-2 message took five substitutions. Its numeric args
 *    are modelled at their maxima (curBucket at SD_CARD_MANAGER_MAX_BUCKET,
 *    then MAX_DIR_FILES and MAX_BUCKET, which are compile-time constants).
 *    That only makes the historical message LONGER, and it is already 207
 *    fixed bytes -- 82 over the ceiling before a single substitution -- so
 *    the "it truncated" verdict does not depend on the arg values chosen.
 * ========================================================================== */

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "test_framework.h"
#include "gen_1029_log_budget.h"

#if defined(__GNUC__)
#  define PRINTF_LIKE(fmtArg, firstVarArg) \
       __attribute__((format(printf, fmtArg, firstVarArg)))
#else
#  define PRINTF_LIKE(fmtArg, firstVarArg)
#endif

/* Generous scratch for the UNTRUNCATED rendering of any message here. The
 * longest is the historical site-2 text at 207 fixed bytes plus ~130 of
 * substitutions. */
#define FULL_BUF 512

/* ==========================================================================
 * The historical (pre-#1029) message texts -- FROZEN LITERALS.
 *
 * These are NOT extracted from firmware source, and cannot be: the fix
 * deleted them. They are transcribed from the pre-fix revision of
 * sd_card_manager.c (branch base 1cbf476f2), concatenated exactly as the C
 * preprocessor did. If you are here because a reviewer asked "how do we know
 * these are what shipped?" -- `git show 1cbf476f2:firmware/src/services/
 * sd_card_services/sd_card_manager.c` is the answer, and the three comments
 * at the fixed sites quote them as well.
 *
 * They exist so this file can demonstrate the defect it targets rather than
 * only assert the property that replaced it.
 * ========================================================================== */
#define HIST_MSG_BUCKET_NOT_DIR                                               \
    "[SD] #689 bucket name '%s' is taken by a non-directory - "                \
    "rename or remove it, or use a different directory"

#define HIST_MSG_BUCKET_UNUSABLE                                              \
    "[SD] WRITE refused: no writable bucket under '%s' "                       \
    "(active '%s', bucket %u, %u per bucket, max %u) - FatFs "                 \
    "file-create wedges large directories (#689). Use a "                      \
    "larger SD:MAXSize, a different directory, or clear the "                  \
    "card."

#define HIST_MSG_WRITES_HANG                                                  \
    "[SD] operations hanging while reads work - likely (a) target "            \
    "directory too large (#689: larger SD:MAXSize / clear the card) "          \
    "or (b) an SPI-mode-incompatible card (wiki: SD-Card-Compatibility)"       \
    "\r\n"

/* ==========================================================================
 * Mirror of Util/Logger.c's LogMessageFormatImpl
 * ========================================================================== */

/* Logger.c's own local helper (Logger.c:39). */
#define MIRROR_MIN(x, y) (((x) <= (y)) ? (x) : (y))

typedef enum {
    CRLF_ALREADY_PRESENT = 0,   /* text already ended \r\n          */
    CRLF_FROM_BARE_LF    = 1,   /* text ended \n, converted to \r\n */
    CRLF_APPENDED        = 2    /* no newline, \r\n appended        */
} CrlfBranch;

typedef struct {
    char       emitted[FW_LOG_MESSAGE_SIZE]; /* the frame the device would hold */
    int        intended;   /* vsnprintf's return: what the text WANTED to be   */
    int        emittedLen; /* LogMessageFormatImpl's final `size`              */
    CrlfBranch branch;
} LoggerEmit;

static int logger_format_mirror_v(LoggerEmit *out, const char *format, va_list args)
{
    int size;
    char *buffer = out->emitted;          /* char buffer[LOG_MESSAGE_SIZE]; */

    memset(out->emitted, 0, sizeof(out->emitted));
    out->intended   = 0;
    out->emittedLen = 0;
    out->branch     = CRLF_APPENDED;

    if (format == NULL) {
        return 0;
    }

    /* Reserve 3 bytes for \r\n\0 - guarantees room to append */
    size = vsnprintf(buffer, FW_LOG_MESSAGE_SIZE - FW_LOG_VSNPRINTF_RESERVE,
                     format, args);
    out->intended = size;
    if (size <= 0) {
        size = 0;
        return size;
    }

    /* Clamp to actual buffer content (vsnprintf returns what it would have written) */
    size = MIRROR_MIN((FW_LOG_MESSAGE_SIZE - FW_LOG_CLAMP_RESERVE), size);

    /* Ensure message ends with \r\n (always have room due to -3 reservation) */
    if (size >= 2 && buffer[size - 2] == '\r' && buffer[size - 1] == '\n') {
        out->branch = CRLF_ALREADY_PRESENT;
    } else if (size >= 1 && buffer[size - 1] == '\n') {
        buffer[size - 1] = '\r';
        buffer[size]     = '\n';
        buffer[size + 1] = '\0';
        size++;
        out->branch = CRLF_FROM_BARE_LF;
    } else {
        buffer[size]     = '\r';
        buffer[size + 1] = '\n';
        buffer[size + 2] = '\0';
        size += 2;
        out->branch = CRLF_APPENDED;
    }

    out->emittedLen = size;
    return size;   /* the real code returns LogMessageAdd(buffer) */
}

static int logger_format_mirror(LoggerEmit *out, const char *format, ...)
    PRINTF_LIKE(2, 3);

static int logger_format_mirror(LoggerEmit *out, const char *format, ...)
{
    va_list args;
    int r;
    va_start(args, format);
    r = logger_format_mirror_v(out, format, args);
    va_end(args);
    return r;
}

/* What the device WOULD show if nothing were clipped: the complete formatted
 * text with Logger's CRLF fixup applied to it. Comparing the emitted frame
 * against this is what turns "the length is under the ceiling" into "the
 * bytes on the wire are the bytes the author wrote". */
static void crlf_fixup_of(char *out, size_t outSize, const char *full)
{
    size_t len = strlen(full);
    if (len >= 2 && full[len - 2] == '\r' && full[len - 1] == '\n') {
        snprintf(out, outSize, "%s", full);
    } else if (len >= 1 && full[len - 1] == '\n') {
        snprintf(out, outSize, "%.*s\r\n", (int)(len - 1), full);
    } else {
        snprintf(out, outSize, "%s\r\n", full);
    }
}

/* ==========================================================================
 * Worst-case substitution values
 * ========================================================================== */

/* Build a bucketPath exactly as sd_BuildBucketPath's bucket != 0 branch does,
 * from a `dirLen`-character directory name. */
static void build_bucket_path(char *out, size_t outSize, size_t dirLen, unsigned bucket)
{
    char dir[FW_SD_DIR_NAME_LEN_MAX + 1];

    if (dirLen > FW_SD_DIR_NAME_LEN_MAX) {
        dirLen = FW_SD_DIR_NAME_LEN_MAX;   /* the SD manager refuses longer */
    }
    memset(dir, 'x', dirLen);
    dir[dirLen] = '\0';
    (void)snprintf(out, outSize, FW_SD_BUCKET_PATH_FMT, dir, bucket);
}

/* ==========================================================================
 * Per-site emitters. Each formats with a LITERAL format string so the
 * compiler's own -Wformat checks the argument list against the extracted
 * text -- a substitution added to a firmware message that this test did not
 * follow becomes a build error, not a silently wrong measurement. (The
 * generator refuses to emit the header in that case too; this is the second
 * of the two belts.)
 * ========================================================================== */

typedef struct {
    LoggerEmit emit;
    char       full[FULL_BUF];       /* the untruncated formatted text      */
    char       expected[FULL_BUF];   /* full + Logger's CRLF fixup          */
} SiteRender;

static void finish(SiteRender *r)
{
    crlf_fixup_of(r->expected, sizeof(r->expected), r->full);
}

static void render_fixed_not_dir(SiteRender *r, const char *path)
{
    logger_format_mirror(&r->emit, FW_MSG_BUCKET_NOT_DIR, path);
    (void)snprintf(r->full, sizeof(r->full), FW_MSG_BUCKET_NOT_DIR, path);
    finish(r);
}

static void render_fixed_unusable(SiteRender *r, const char *path)
{
    logger_format_mirror(&r->emit, FW_MSG_BUCKET_UNUSABLE, path);
    (void)snprintf(r->full, sizeof(r->full), FW_MSG_BUCKET_UNUSABLE, path);
    finish(r);
}

static void render_fixed_writes_hang(SiteRender *r)
{
    logger_format_mirror(&r->emit, FW_MSG_WRITES_HANG);
    (void)snprintf(r->full, sizeof(r->full), "%s", FW_MSG_WRITES_HANG);
    finish(r);
}

static void render_hist_not_dir(SiteRender *r, const char *path)
{
    logger_format_mirror(&r->emit, HIST_MSG_BUCKET_NOT_DIR, path);
    (void)snprintf(r->full, sizeof(r->full), HIST_MSG_BUCKET_NOT_DIR, path);
    finish(r);
}

static void render_hist_unusable(SiteRender *r, const char *dir, const char *path)
{
    logger_format_mirror(&r->emit, HIST_MSG_BUCKET_UNUSABLE, dir, path,
                         (unsigned)FW_SD_MAX_BUCKET,
                         (unsigned)FW_SD_MAX_DIR_FILES,
                         (unsigned)FW_SD_MAX_BUCKET);
    (void)snprintf(r->full, sizeof(r->full), HIST_MSG_BUCKET_UNUSABLE, dir, path,
                   (unsigned)FW_SD_MAX_BUCKET,
                   (unsigned)FW_SD_MAX_DIR_FILES,
                   (unsigned)FW_SD_MAX_BUCKET);
    finish(r);
}

static void render_hist_writes_hang(SiteRender *r)
{
    logger_format_mirror(&r->emit, HIST_MSG_WRITES_HANG);
    (void)snprintf(r->full, sizeof(r->full), "%s", HIST_MSG_WRITES_HANG);
    finish(r);
}

/* ==========================================================================
 * Shared assertions
 * ========================================================================== */

/* A message SURVIVES when vsnprintf never had to drop anything (its
 * would-have-written length is within the clamp) AND the frame the device
 * holds is byte-identical to the complete text plus Logger's CRLF. */
static void assert_survives(SiteRender *r)
{
    ASSERT_TRUE(r->emit.intended > 0);
    ASSERT_TRUE(r->emit.intended <= FW_LOG_CONTENT_MAX);
    ASSERT_EQ(strcmp(r->emit.emitted, r->expected), 0);
    /* Whole means whole: the frame is the complete text, not a prefix of it
     * that happens to contain the needles the callers go on to look for. */
    ASSERT_EQ((int)strlen(r->emit.emitted), r->emit.emittedLen);
}

static void assert_truncated(SiteRender *r)
{
    ASSERT_TRUE(r->emit.intended > FW_LOG_CONTENT_MAX);
    ASSERT_TRUE(strcmp(r->emit.emitted, r->expected) != 0);
    /* Everything past the clamp is gone, and the frame is exactly the clamp
     * plus the appended CRLF -- i.e. the loss is silent, not flagged. */
    ASSERT_EQ(r->emit.emittedLen, FW_LOG_CONTENT_MAX + 2);
}

static void assert_has(const SiteRender *r, const char *needle)
{
    ASSERT_TRUE(strstr(r->emit.emitted, needle) != NULL);
}

/* The remedy must be in the INTENDED text (otherwise the historical message
 * never offered it and this proves nothing) and absent from what was emitted. */
static void assert_remedy_lost(const SiteRender *r, const char *needle)
{
    ASSERT_TRUE(strstr(r->full, needle) != NULL);
    ASSERT_TRUE(strstr(r->emit.emitted, needle) == NULL);
}

/* ==========================================================================
 * Tests -- the mirror itself
 * ========================================================================== */

/* The mirror is the instrument every other test reads through, so prove it
 * reproduces Logger's boundary and all three of its CRLF branches before
 * trusting a single verdict from it. */
TEST(mirror_reproduces_loggers_ceiling_and_crlf_cascade)
{
    char filler[FW_LOG_MESSAGE_SIZE + 8];
    LoggerEmit e;

    /* (a) short text, no newline -> \r\n appended, nothing lost */
    logger_format_mirror(&e, "%s", "hello");
    ASSERT_EQ(e.intended, 5);
    ASSERT_EQ(e.emittedLen, 7);
    ASSERT_EQ(e.branch, CRLF_APPENDED);
    ASSERT_EQ(strcmp(e.emitted, "hello\r\n"), 0);

    /* (b) exactly at the ceiling -> still whole */
    memset(filler, 'a', FW_LOG_CONTENT_MAX);
    filler[FW_LOG_CONTENT_MAX] = '\0';
    logger_format_mirror(&e, "%s", filler);
    ASSERT_EQ(e.intended, FW_LOG_CONTENT_MAX);
    ASSERT_EQ(e.emittedLen, FW_LOG_CONTENT_MAX + 2);
    ASSERT_EQ((int)strlen(e.emitted), FW_LOG_CONTENT_MAX + 2);
    ASSERT_TRUE(strncmp(e.emitted, filler, FW_LOG_CONTENT_MAX) == 0);

    /* (c) one byte over -> that byte is gone, silently */
    memset(filler, 'a', FW_LOG_CONTENT_MAX + 1);
    filler[FW_LOG_CONTENT_MAX + 1] = '\0';
    logger_format_mirror(&e, "%s", filler);
    ASSERT_EQ(e.intended, FW_LOG_CONTENT_MAX + 1);
    ASSERT_EQ(e.emittedLen, FW_LOG_CONTENT_MAX + 2);   /* clamp + CRLF */
    ASSERT_EQ((int)strlen(e.emitted), FW_LOG_CONTENT_MAX + 2);

    /* (d) text that already ends \r\n -> no second CRLF */
    logger_format_mirror(&e, "%s", "done\r\n");
    ASSERT_EQ(e.branch, CRLF_ALREADY_PRESENT);
    ASSERT_EQ(e.emittedLen, 6);
    ASSERT_EQ(strcmp(e.emitted, "done\r\n"), 0);

    /* (e) bare \n -> promoted to \r\n */
    logger_format_mirror(&e, "%s", "done\n");
    ASSERT_EQ(e.branch, CRLF_FROM_BARE_LF);
    ASSERT_EQ(e.emittedLen, 6);
    ASSERT_EQ(strcmp(e.emitted, "done\r\n"), 0);

    /* The frame never overruns the device's 128-byte LogEntry. */
    ASSERT_TRUE(e.emittedLen < FW_LOG_MESSAGE_SIZE);
}

/* The conversion lists this file formats against. gen_1029_log_budget.py
 * refuses to emit the header if a shipped message's conversions changed;
 * this asserts the same thing from the test's side, so the contract is
 * stated where the argument lists actually live. */
TEST(shipped_messages_take_the_conversions_this_test_models)
{
    ASSERT_EQ(strcmp(FW_MSG_BUCKET_NOT_DIR_CONV, "s"), 0);
    ASSERT_EQ(strcmp(FW_MSG_BUCKET_UNUSABLE_CONV, "s"), 0);
    ASSERT_EQ(strcmp(FW_MSG_WRITES_HANG_CONV, ""), 0);
}

/* ==========================================================================
 * Tests -- the worst-case substitution
 * ========================================================================== */

/* The worst case is FORMATTED, not arithmetic: sd_BuildBucketPath's own
 * format string, a maximum-length directory, the maximum bucket number. */
TEST(worst_case_bucket_path_is_derived_from_the_real_format)
{
    char path[FW_SD_BUCKET_PATH_MAXLEN];
    size_t derived;

    build_bucket_path(path, sizeof(path), FW_SD_DIR_NAME_LEN_MAX,
                      (unsigned)FW_SD_MAX_BUCKET);

    /* "/" is the only literal text between the two conversions in
     * FW_SD_BUCKET_PATH_FMT; the prefix and the digit count come from the
     * generated header. If the format gains more literal text, this fails --
     * and so must the budget be re-derived. */
    derived = (size_t)FW_SD_DIR_NAME_LEN_MAX
            + strlen("/") + strlen(FW_SD_BUCKET_PREFIX)
            + (size_t)FW_SD_BUCKET_DIGITS;
    ASSERT_EQ(strlen(path), derived);

    /* And the bucket number really did fit in the padded field -- a
     * MAX_BUCKET that outgrew %03u would silently widen every path. */
    ASSERT_TRUE(strstr(path, FW_SD_BUCKET_PREFIX) != NULL);
    ASSERT_TRUE((size_t)snprintf(NULL, 0, "%u", (unsigned)FW_SD_MAX_BUCKET)
                <= (size_t)FW_SD_BUCKET_DIGITS);

    printf("    worst-case bucketPath = %u bytes (%d-char dir + \"/\" + \"%s\""
           " + %d digits)\n",
           (unsigned)strlen(path), FW_SD_DIR_NAME_LEN_MAX,
           FW_SD_BUCKET_PREFIX, FW_SD_BUCKET_DIGITS);
}

/* ==========================================================================
 * Tests -- the shipped messages fit, with their remedies intact
 * ========================================================================== */

TEST(shipped_messages_fit_at_the_worst_case_substitution)
{
    char path[FW_SD_BUCKET_PATH_MAXLEN];
    SiteRender r;

    build_bucket_path(path, sizeof(path), FW_SD_DIR_NAME_LEN_MAX,
                      (unsigned)FW_SD_MAX_BUCKET);

    /* site 1 -- remedy: rename/remove the offending name, or move directory */
    render_fixed_not_dir(&r, path);
    assert_survives(&r);
    assert_has(&r, "rename/remove");
    assert_has(&r, "different dir");
    printf("    site 1 (sd_card_manager.c:%d): %3d/%d bytes, margin %d\n",
           FW_MSG_BUCKET_NOT_DIR_LINE, r.emit.intended, FW_LOG_CONTENT_MAX,
           FW_LOG_CONTENT_MAX - r.emit.intended);

    /* site 2 -- remedy: grow the split size, change directory, clear card */
    render_fixed_unusable(&r, path);
    assert_survives(&r);
    assert_has(&r, "SD:MAXSize");
    assert_has(&r, "clear card");
    printf("    site 2 (sd_card_manager.c:%d): %3d/%d bytes, margin %d\n",
           FW_MSG_BUCKET_UNUSABLE_LINE, r.emit.intended, FW_LOG_CONTENT_MAX,
           FW_LOG_CONTENT_MAX - r.emit.intended);

    /* site 3 -- BOTH remedies: the #689 one and the #589 wiki pointer. This
     * message takes no substitutions, so this single case is its whole
     * domain: what is measured here is what every firing emits. */
    render_fixed_writes_hang(&r);
    assert_survives(&r);
    assert_has(&r, "SD:MAXSize");
    assert_has(&r, "SD-Card-Compatibility");
    /* It already ends \r\n, so Logger must not append a second one. */
    ASSERT_EQ(r.emit.branch, CRLF_ALREADY_PRESENT);
    printf("    site 3 (sd_card_manager.c:%d): %3d/%d bytes, margin %d\n",
           FW_MSG_WRITES_HANG_LINE, r.emit.intended, FW_LOG_CONTENT_MAX,
           FW_LOG_CONTENT_MAX - r.emit.intended);
}

/* The worst case is not the only case that has to fit. Sweep every directory
 * length the SD manager will accept, against the bucket-0 path form (the
 * directory verbatim) and several bucket!=0 forms including the widest. */
TEST(shipped_messages_fit_at_every_reachable_substitution)
{
    static const unsigned buckets[] = { 1u, 9u, 10u, 63u, (unsigned)FW_SD_MAX_BUCKET };
    char path[FW_SD_BUCKET_PATH_MAXLEN];
    SiteRender r;
    size_t dirLen;
    size_t b;
    /* -1 so the bucket-0 form (the directory verbatim, which is what
     * sd_BuildBucketPath emits there) is swept alongside the bucket!=0 ones
     * rather than in a duplicated block that could drift away from them. */
    int bucketIdx;

    for (dirLen = 0; dirLen <= (size_t)FW_SD_DIR_NAME_LEN_MAX; dirLen++) {
        for (bucketIdx = -1;
             bucketIdx < (int)(sizeof(buckets) / sizeof(buckets[0]));
             bucketIdx++) {
            if (bucketIdx < 0) {
                memset(path, 0, sizeof(path));
                memset(path, 'x', dirLen);
            } else {
                b = (size_t)bucketIdx;
                build_bucket_path(path, sizeof(path), dirLen, buckets[b]);
            }

            render_fixed_not_dir(&r, path);
            assert_survives(&r);
            assert_has(&r, "rename/remove");
            assert_has(&r, "different dir");

            render_fixed_unusable(&r, path);
            assert_survives(&r);
            assert_has(&r, "SD:MAXSize");
            assert_has(&r, "clear card");
        }
    }
}

/* ==========================================================================
 * Tests -- the historical messages did NOT fit (this is what can fail)
 * ========================================================================== */

TEST(historical_messages_lost_their_remedy_at_the_worst_case)
{
    char dir[FW_SD_DIR_NAME_LEN_MAX + 1];
    char path[FW_SD_BUCKET_PATH_MAXLEN];
    SiteRender r;

    memset(dir, 'x', FW_SD_DIR_NAME_LEN_MAX);
    dir[FW_SD_DIR_NAME_LEN_MAX] = '\0';
    build_bucket_path(path, sizeof(path), FW_SD_DIR_NAME_LEN_MAX,
                      (unsigned)FW_SD_MAX_BUCKET);

    /* Site 1 -- the cut landed INSIDE the remedy rather than before it. The
     * operator saw "... - rename or remove it, or u" and nothing further, so
     * the first clause survived and the second -- the one that says where
     * else the data could go -- did not. Asserting BOTH halves is the point:
     * "it was truncated" would be just as true of a cut that lost nothing
     * anyone needed, and would make this test weaker than it looks. */
    render_hist_not_dir(&r, path);
    assert_truncated(&r);
    ASSERT_TRUE(strstr(r.emit.emitted, "rename or remove") != NULL);
    assert_remedy_lost(&r, "different directory");
    printf("    site 1 was %3d/%d bytes, %d OVER (kept the first remedy"
           " clause, lost the second)\n", r.emit.intended,
           FW_LOG_CONTENT_MAX, r.emit.intended - FW_LOG_CONTENT_MAX);

    /* Site 2 -- 82 bytes over before a single substitution, so the cut lands
     * inside the SECOND substitution and every word of remedy is gone. */
    render_hist_unusable(&r, dir, path);
    assert_truncated(&r);
    assert_remedy_lost(&r, "SD:MAXSize");
    assert_remedy_lost(&r, "clear the card");
    assert_remedy_lost(&r, "different directory");
    printf("    site 2 was %3d/%d bytes, %d OVER (lost every word of remedy)\n",
           r.emit.intended, FW_LOG_CONTENT_MAX,
           r.emit.intended - FW_LOG_CONTENT_MAX);

    /* Site 3 -- the cut falls between its two causes, so the #689 remedy
     * survived and the whole #589 branch, wiki pointer included, did not.
     * That is the worst shape of the three: what reaches the operator reads
     * like a COMPLETE and confident diagnosis of the wrong thing. */
    render_hist_writes_hang(&r);
    assert_truncated(&r);
    ASSERT_TRUE(strstr(r.emit.emitted, "larger SD:MAXSize") != NULL);
    assert_remedy_lost(&r, "SD-Card-Compatibility");
    assert_remedy_lost(&r, "SPI-mode-incompatible");
    printf("    site 3 was %3d/%d bytes, %d OVER (kept the #689 cause,"
           " lost the #589 one entirely)\n", r.emit.intended,
           FW_LOG_CONTENT_MAX, r.emit.intended - FW_LOG_CONTENT_MAX);
}

/* Site 3 took no substitutions, so its overrun was not a corner case reached
 * by an unlucky configuration -- it truncated identically on EVERY firing,
 * from the day it was written. Assert exactly that: the emitted frame is the
 * same whatever the device state, and it is short. */
TEST(historical_writes_hang_truncated_on_every_firing)
{
    SiteRender a, b;

    render_hist_writes_hang(&a);
    render_hist_writes_hang(&b);

    ASSERT_EQ(strcmp(a.emit.emitted, b.emit.emitted), 0);
    ASSERT_EQ(a.emit.intended, b.emit.intended);
    assert_truncated(&a);
    /* The operator saw the symptom and the #689 half, and nothing else. */
    ASSERT_TRUE(strstr(a.emit.emitted, "operations hanging") != NULL);
    ASSERT_TRUE(strstr(a.emit.emitted, "SPI-mode-incompatible") == NULL);
}

/* Site 1's historical message did not need a maximum-length directory to
 * break: find the shortest one that loses the remedy and assert it is well
 * inside what an operator can configure -- while the shipped message still
 * fits at that same length (and at every other). */
TEST(historical_bucket_not_dir_broke_at_an_ordinary_directory_length)
{
    char path[FW_SD_BUCKET_PATH_MAXLEN];
    SiteRender r;
    size_t dirLen;
    int firstBad = -1;

    for (dirLen = 0; dirLen <= (size_t)FW_SD_DIR_NAME_LEN_MAX; dirLen++) {
        build_bucket_path(path, sizeof(path), dirLen, 1u);
        render_hist_not_dir(&r, path);
        if (r.emit.intended > FW_LOG_CONTENT_MAX) {
            firstBad = (int)dirLen;
            break;
        }
    }

    ASSERT_TRUE(firstBad >= 0);
    ASSERT_TRUE(firstBad <= FW_SD_DIR_NAME_LEN_MAX);

    /* At that same length the shipped message is whole, remedy included. */
    build_bucket_path(path, sizeof(path), (size_t)firstBad, 1u);
    render_fixed_not_dir(&r, path);
    assert_survives(&r);
    assert_has(&r, "rename/remove");

    printf("    historical site 1 began truncating at a %d-character directory"
           " (limit is %d)\n", firstBad, FW_SD_DIR_NAME_LEN_MAX);
}

int main(void)
{
    printf("#1029 -- sd_card_manager.c LOG_E budget vs Logger's %d-byte ceiling\n",
           FW_LOG_CONTENT_MAX);
    printf("---------------------------------------------\n");
    RUN(mirror_reproduces_loggers_ceiling_and_crlf_cascade);
    RUN(shipped_messages_take_the_conversions_this_test_models);
    RUN(worst_case_bucket_path_is_derived_from_the_real_format);
    RUN(shipped_messages_fit_at_the_worst_case_substitution);
    RUN(shipped_messages_fit_at_every_reachable_substitution);
    RUN(historical_messages_lost_their_remedy_at_the_worst_case);
    RUN(historical_writes_hang_truncated_on_every_firing);
    RUN(historical_bucket_not_dir_broke_at_an_ordinary_directory_length);
    return TEST_SUMMARY();
}
