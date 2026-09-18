/* ==========================================================================
 * test_1000_sd_log_arm_budget.c -- issue #1000
 *
 * WHAT IS UNDER TEST
 *
 * SCPI_StartStreamingClaimed's #942 refusal arm (SCPIInterface.c). When
 * sd_card_manager_UpdateSettingsForStreamingLog() refuses -- the caller lost
 * a race to the #589 suspend check between the guard and the arm -- the arm
 * releases its claim and emits ONE LOG_E naming the cause, because every
 * refusal on this path returns the same -200 and the log line is the only
 * thing that makes it actionable (the standing rule: errors go to the log,
 * SCPI returns the code, the user runs SYST:LOG?).
 *
 * The cause text comes from SD_SuspendReasonText() (SCPIStorageSD.c), or
 * from the call site's own fallback string when that returns NULL. So the
 * emitted line is
 *
 *     <prefix> <reason> CRLF
 *
 * and Logger CUTS it. LogMessageFormatImpl (Logger.c) is
 *
 *     char buffer[LOG_MESSAGE_SIZE];                        // 128
 *     size = vsnprintf(buffer, LOG_MESSAGE_SIZE - 2, format, args);
 *     size = min((LOG_MESSAGE_SIZE - 3), size);
 *     ... then a \r\n fixup that appends CRLF when the (possibly cut)
 *         content does not already end in one ...
 *
 * so the whole formatted line -- prefix, interpolated reason AND the literal
 * CRLF at the end of the format string -- survives intact up to
 * LOG_MESSAGE_SIZE - 3 = 125 bytes. Past that, vsnprintf keeps the first 125
 * bytes and the fixup staples a CRLF onto the stump: the message still LOOKS
 * well-formed, and the tail of the reason is simply gone. There is no
 * error, no counter, and nothing at the call site can see it.
 *
 * (125 is the conservative bound, not the exact one. An overrun of exactly
 * one or two bytes falls in a degenerate window where the fixup happens to
 * lose only the LF, or to put both CRLF bytes back and reproduce the intended
 * line perfectly. mirror_cuts_exactly_where_logger_does pins that window so
 * it is recorded rather than tripped over; everything else here holds to the
 * <= 125 bound, which is the one that is true for any reason length.)
 *
 * THE DEFECT (#1000). The prefix was 88 characters:
 *
 *     "Cannot start SD logging - could not arm the write "
 *     "(#942: raced the #589 suspend check): "
 *
 * 125 - 88 - 2 (CRLF) = 35 bytes left for the reason. Every string
 * SD_SuspendReasonText() can return is longer than that -- 46, 58 and 76 --
 * so all three were cut, the shortest included. The 76-character quarantine
 * reason lost its whole second half, which is the half carrying
 * "SYST:STOR:SD:ENAble 1", the command that clears a quarantine. The prefix
 * spent its budget restating two issue numbers that a reader of the log
 * cannot act on, and cut the part they can.
 *
 * THE FIX shortens the PREFIX to 27 characters -- "SD log arm refused
 * (#942): " -- leaving 96 for the reason. The #942/#589 narrative moved into
 * a source comment at that call site. Nothing about SD_SuspendReasonText()'s
 * strings changed, and could not usefully have: the ceiling is shared across
 * callers with different prefix costs, so shortening the reasons to fit the
 * worst prefix would have degraded every other caller's message too.
 *
 * WHAT THIS TEST PROVES
 *
 * SCPIInterface.c is not includable on a host (libscpi, FreeRTOS, the whole
 * Harmony board/driver graph), which is the same constraint test_943 /
 * test_953 / test_1004 work under. Those re-implement a loop or cascade
 * SHAPE and compare pre-fix and post-fix verdicts. There is no shape here --
 * the subject is a length -- so instead of modelling one, this test
 * reproduces Logger's truncation for real (the same vsnprintf, the same
 * bound, the same clamp, the same CRLF fixup) and runs the ACTUAL strings
 * through it, then asserts the emitted bytes equal the intended bytes.
 *
 * Nothing load-bearing is a magic number:
 *
 *   - LOG_MESSAGE_SIZE and BOTH of Logger.c's reservations (the vsnprintf
 *     bound and the clamp) are EXTRACTED from the real Logger.h / Logger.c
 *     at build time into gen_1000_log_budget.h. Lower the ceiling in the
 *     firmware and this test re-derives against the new one and fails if a
 *     line no longer fits -- it does not merely ask to be updated.
 *   - The shipped prefix and the call site's NULL fallback are EXTRACTED the
 *     same way, from SCPIInterface.c. Grow the prefix past its budget and
 *     this test fails on the real string, with no copy to keep in step.
 *   - The three SD_SuspendReasonText() strings ARE a copy, and are the only
 *     one. They belong to that function, not to this call site, and #1001 is
 *     building the general mechanism that measures them against every caller.
 *     Until then the Makefile greps each full return statement out of
 *     SCPIStorageSD.c and FAILS THE BUILD if any has drifted, so a stale copy
 *     cannot pass quietly -- and a reason that grew would then have to be
 *     re-derived here, where it is measured.
 *
 * The old 88-character prefix is kept as a labelled historical control, so
 * the suite carries its own proof that these assertions bite: the same
 * harness, on the same four reasons, must find three of them cut.
 *
 * AND ONE BOUNDARY WORTH BEING EXACT ABOUT. Three of the four reachable
 * strings were cut by the old prefix. The fourth -- the NULL fallback, "the
 * SD task is not accepting work", 33 characters -- FIT: 88 + 33 + 2 = 123,
 * two bytes inside the ceiling. It is easy, and wrong, to summarise #1000 as
 * "the old prefix cut everything", and `old_prefix_did_not_cut_the_null_
 * fallback` exists so the suite states the true boundary rather than the
 * tidier one.
 *
 * It is not pedantry. The NULL case is the one a casual bench reproduction
 * reaches first -- no quarantine, no WiFi FW update, no WiFi stream, so
 * SD_SuspendReasonText() returns NULL -- and on the broken firmware it
 * printed a complete, healthy-looking line. "I reproduced it and the log was
 * fine, so the bug is not real" was a conclusion the defect actively invited,
 * and the arithmetic here is what refutes it.
 * ========================================================================== */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "test_framework.h"

/* Generated by the Makefile from the real firmware sources -- see the
 * $(SDLOG_BIN) recipe. Holds FW_LOG_MESSAGE_SIZE, FW_LOG_VSNPRINTF_RESERVE,
 * FW_LOG_CLAMP_RESERVE, FW_1000_PREFIX and FW_1000_NULL_FALLBACK. */
#include "gen_1000_log_budget.h"

/* --------------------------------------------------------------------------
 * Logger.c's truncation, reproduced exactly.
 * ------------------------------------------------------------------------ */

/* The largest formatted line, CRLF included, that survives whole. */
#define LOG_BUDGET (FW_LOG_MESSAGE_SIZE - FW_LOG_CLAMP_RESERVE)

/* Mirror of LogMessageFormatImpl() up to the point it hands `buffer` to
 * LogMessageAdd(). Returns the emitted length and fills `out` (which must be
 * FW_LOG_MESSAGE_SIZE bytes) with the NUL-terminated emitted message -- i.e.
 * what SYST:LOG? would later print, not what the caller asked for.
 *
 * The \r\n fixup's three branches are carried over verbatim even though our
 * formats always end in CRLF, because whether the FIRST branch is taken is
 * exactly what distinguishes "fit" from "cut": an uncut line still ends in
 * the format's own CRLF and is left alone, while a cut one ends in whatever
 * byte 125 of the reason happened to be and gets a CRLF stapled on. Dropping
 * the branches would make a cut line look merely shorter instead of
 * mis-terminated, which is the difference the device actually exhibits. */
static int fw_log_emit(char *out, const char *format, ...)
{
    int size;
    char buffer[FW_LOG_MESSAGE_SIZE];
    va_list args;

    if (format == NULL) {
        return 0;
    }

    va_start(args, format);
    size = vsnprintf(buffer, FW_LOG_MESSAGE_SIZE - FW_LOG_VSNPRINTF_RESERVE,
                     format, args);
    va_end(args);

    if (size <= 0) {
        return 0;
    }

    if (size > (int)LOG_BUDGET) {
        size = (int)LOG_BUDGET;
    }

    if (size >= 2 && buffer[size - 2] == '\r' && buffer[size - 1] == '\n') {
        /* Already has \r\n */
    } else if (size >= 1 && buffer[size - 1] == '\n') {
        buffer[size - 1] = '\r';
        buffer[size]     = '\n';
        buffer[size + 1] = '\0';
        size++;
    } else {
        buffer[size]     = '\r';
        buffer[size + 1] = '\n';
        buffer[size + 2] = '\0';
        size += 2;
    }

    memcpy(out, buffer, (size_t)size + 1U);
    return size;
}

/* --------------------------------------------------------------------------
 * The strings.
 * ------------------------------------------------------------------------ */

/* The three SD_SuspendReasonText() returns, in the order that function tests
 * them. COPIED from SCPIStorageSD.c; the Makefile greps each full return
 * statement and fails the build on any drift. */
#define REASON_QUARANTINE  "SD quarantined after a bus jam - reseat the card, " \
                           "then SYST:STOR:SD:ENAble 1"
#define REASON_WIFI_FW     "a WiFi firmware update owns SPI4 - retry when it completes"
#define REASON_WIFI_STREAM "WiFi streaming owns SPI4 - SYST:STR:STOP first"

/* The command a quarantined user needs, and the exact thing the old prefix
 * cut off. Its survival is this test's headline. */
#define QUARANTINE_REMEDY  "SYST:STOR:SD:ENAble 1"

/* The pre-#1000 prefix, verbatim from git history. A frozen historical
 * constant -- it is not in the tree any more and cannot drift. */
#define OLD_PREFIX "Cannot start SD logging - could not arm the write " \
                   "(#942: raced the #589 suspend check): "

#define NEW_FORMAT FW_1000_PREFIX "%s\r\n"
#define OLD_FORMAT OLD_PREFIX     "%s\r\n"

/* Every string reachable through `why` at the #942 refusal: the three
 * SD_SuspendReasonText() returns, plus the call site's own fallback for the
 * NULL return. */
typedef struct {
    const char *name;
    const char *reason;
} ReasonCase;

static const ReasonCase kReasons[] = {
    { "quarantine",    REASON_QUARANTINE      },
    { "wifi-fw-update", REASON_WIFI_FW        },
    { "wifi-streaming", REASON_WIFI_STREAM    },
    { "null-fallback", FW_1000_NULL_FALLBACK  },
};
#define N_REASONS ((int)(sizeof(kReasons) / sizeof(kReasons[0])))

/* The line the call site MEANT to emit, before Logger got to it. */
static void intended_line(char *out, size_t n, const char *prefix,
                          const char *reason)
{
    snprintf(out, n, "%s%s\r\n", prefix, reason);
}

/* --------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------ */

/* The extraction must not be able to pass by finding nothing. An empty or
 * partial prefix would sail through every budget assertion below while
 * establishing nothing at all, so pin the properties that make it the real
 * line: non-empty, tagged with the issue it refuses under, and ending in the
 * separator that the "%s" follows. The Makefile's round-trip grep pins the
 * rest -- it reassembles LOG_E("<prefix>%s\r\n", and requires that exact text
 * to appear in SCPIInterface.c, so a silently truncated extraction fails the
 * build before reaching here. */
TEST(extracted_prefix_is_the_real_one)
{
    const size_t plen = strlen(FW_1000_PREFIX);

    ASSERT_TRUE(plen > 0);
    ASSERT_TRUE(strstr(FW_1000_PREFIX, "#942") != NULL);
    ASSERT_TRUE(plen >= 2 && strcmp(FW_1000_PREFIX + plen - 2, ": ") == 0);

    /* The fallback is extracted from the same line and must be non-empty
     * for the same reason. */
    ASSERT_TRUE(strlen(FW_1000_NULL_FALLBACK) > 0);

    /* The extracted prefix must actually be the short one. Stated as a
     * bound, not as its exact length, so an editorial reword of the prefix
     * is not a test failure -- only a prefix that has grown back toward the
     * old one is. */
    ASSERT_TRUE(plen < strlen(OLD_PREFIX));
}

/* Logger's own constants must have come out of Logger.h / Logger.c as
 * numbers that make sense together. A failed extraction yields 0, which
 * makes LOG_BUDGET negative and every other test fail loudly -- this one
 * says WHY. The clamp reserve has to be the larger of the two reservations,
 * because it is the clamp, not the vsnprintf bound, that sets the surviving
 * length. */
TEST(logger_ceiling_was_extracted_coherently)
{
    ASSERT_TRUE(FW_LOG_MESSAGE_SIZE >= 32);
    ASSERT_TRUE(FW_LOG_VSNPRINTF_RESERVE >= 1);
    ASSERT_TRUE(FW_LOG_CLAMP_RESERVE > FW_LOG_VSNPRINTF_RESERVE);
    ASSERT_TRUE(LOG_BUDGET > (int)strlen(FW_1000_PREFIX) + 2);
}

/* The mirror must cut in the same place Logger does, or every verdict below
 * is measured against the wrong boundary -- so pin the whole neighbourhood of
 * that boundary rather than one case on each side of it.
 *
 * The expected columns are what Logger.c genuinely produces, and two rows are
 * counter-intuitive enough that this table exists mainly to record them:
 *
 *   L == LOG_BUDGET + 1 -- vsnprintf keeps the body and the CR and drops the
 *     LF; the fixup then sees ...'x','\r' (neither "\r\n" nor a trailing
 *     '\n') and staples a fresh CRLF on. The body is intact and the line is
 *     mis-terminated with a doubled CR. Nothing of the MESSAGE is lost.
 *   L == LOG_BUDGET + 2 -- vsnprintf keeps exactly the body and drops both
 *     CRLF bytes; the fixup then appends exactly what was dropped. The
 *     emitted line EQUALS the intended one. Logger accidentally repairs an
 *     overrun of precisely two.
 *
 * So "fits" is not one inequality, and a test that assumed it was would
 * assert something false two bytes past the budget. What this suite relies on
 * is the conservative half -- L <= LOG_BUDGET always survives whole -- and the
 * degenerate window is pinned here so it is recorded behaviour rather than a
 * surprise to whoever next edits LogMessageFormatImpl.
 *
 * The body-byte count tells the honest story across every row: exactly
 * min(bodyLen, LOG_BUDGET) bytes of body survive, whatever becomes of the
 * terminator. */
TEST(mirror_cuts_exactly_where_logger_does)
{
    static const struct {
        int bodyLen;
        int expectEmittedLen;
        int expectEqual;
    } kCases[] = {
        { LOG_BUDGET - 3, LOG_BUDGET - 1, 1 },   /* comfortably inside       */
        { LOG_BUDGET - 2, LOG_BUDGET,     1 },   /* exactly at the budget    */
        { LOG_BUDGET - 1, LOG_BUDGET + 2, 0 },   /* +1: only the LF is lost  */
        { LOG_BUDGET,     LOG_BUDGET + 2, 1 },   /* +2: repaired by accident */
        { LOG_BUDGET + 1, LOG_BUDGET + 2, 0 },   /* +3: a body byte is lost  */
        { LOG_BUDGET * 2, LOG_BUDGET + 2, 0 },   /* far past                 */
    };
    const int nCases = (int)(sizeof(kCases) / sizeof(kCases[0]));
    int c;

    for (c = 0; c < nCases; c++) {
        char body[FW_LOG_MESSAGE_SIZE * 4];
        char intended[FW_LOG_MESSAGE_SIZE * 4];
        char emitted[FW_LOG_MESSAGE_SIZE];
        int  emittedLen;
        int  survived = 0;
        int  expectSurvived;
        int  i;

        memset(body, 'x', (size_t)kCases[c].bodyLen);
        body[kCases[c].bodyLen] = '\0';

        intended_line(intended, sizeof(intended), body, "");
        emittedLen = fw_log_emit(emitted, "%s\r\n", body);

        ASSERT_EQ(emittedLen, kCases[c].expectEmittedLen);
        ASSERT_EQ((int)strlen(emitted), kCases[c].expectEmittedLen);
        ASSERT_EQ(strcmp(emitted, intended) == 0, kCases[c].expectEqual);

        for (i = 0; emitted[i] != '\0'; i++) {
            if (emitted[i] == 'x') {
                survived++;
            }
        }
        expectSurvived = (kCases[c].bodyLen < LOG_BUDGET)
                       ? kCases[c].bodyLen : LOG_BUDGET;
        ASSERT_EQ(survived, expectSurvived);

        /* However it ends up terminated, it always does end in CRLF -- which
         * is exactly why a cut line is indistinguishable from a short one at
         * the far end of SYST:LOG?. */
        ASSERT_TRUE(emittedLen >= 2);
        ASSERT_TRUE(emitted[emittedLen - 2] == '\r'
                    && emitted[emittedLen - 1] == '\n');
    }
}

/* THE FIX. Every reachable (prefix, reason) pair emits byte-for-byte what
 * the call site asked for. This is the whole invariant, asserted against the
 * emitted bytes rather than against an arithmetic prediction of them. */
TEST(shipped_prefix_emits_every_reason_intact)
{
    int i;

    for (i = 0; i < N_REASONS; i++) {
        char intended[FW_LOG_MESSAGE_SIZE * 2];
        char emitted[FW_LOG_MESSAGE_SIZE];

        intended_line(intended, sizeof(intended), FW_1000_PREFIX,
                      kReasons[i].reason);
        (void)fw_log_emit(emitted, NEW_FORMAT, kReasons[i].reason);

        if (strcmp(emitted, intended) != 0) {
            printf("    reason '%s' was cut: wanted %u bytes, emitted '%s'\n",
                   kReasons[i].name, (unsigned)strlen(intended), emitted);
        }
        ASSERT_TRUE(strcmp(emitted, intended) == 0);
    }
}

/* The same invariant stated as the budget arithmetic, so a failure reports a
 * number a maintainer can act on (how much over, and which reason) rather
 * than only "the bytes differ". */
TEST(shipped_prefix_keeps_every_pair_inside_the_budget)
{
    int i;

    for (i = 0; i < N_REASONS; i++) {
        int total = (int)strlen(FW_1000_PREFIX)
                  + (int)strlen(kReasons[i].reason)
                  + 2;                            /* the format's own CRLF */

        if (total > LOG_BUDGET) {
            printf("    reason '%s': %d bytes, %d over the %d-byte budget\n",
                   kReasons[i].name, total, total - LOG_BUDGET, LOG_BUDGET);
        }
        ASSERT_TRUE(total <= LOG_BUDGET);
    }
}

/* THE HEADLINE. A quarantine does not clear on its own -- SYST:STOR:SD:ENAble
 * 1 is the only thing that clears it -- so the message whose entire job is to
 * say that has to still contain it when it reaches SYST:LOG?. Asserted both
 * ways on the same harness: present under the shipped prefix, absent under
 * the old one. */
TEST(quarantine_remedy_survives_now_and_did_not_before)
{
    char emittedNew[FW_LOG_MESSAGE_SIZE];
    char emittedOld[FW_LOG_MESSAGE_SIZE];

    (void)fw_log_emit(emittedNew, NEW_FORMAT, REASON_QUARANTINE);
    (void)fw_log_emit(emittedOld, OLD_FORMAT, REASON_QUARANTINE);

    ASSERT_TRUE(strstr(emittedNew, QUARANTINE_REMEDY) != NULL);
    ASSERT_TRUE(strstr(emittedOld, QUARANTINE_REMEDY) == NULL);

    /* The old line opened correctly and then stopped, mid-word, 37 bytes into
     * a 76-byte reason -- it did not even reach "reseat". That is why nothing
     * downstream could tell it had been cut: it still ends in a CRLF and
     * still reads like a sentence. */
    ASSERT_TRUE(strstr(emittedOld, "SD quarantined after a bus jam") != NULL);
    ASSERT_TRUE(strstr(emittedOld, "reseat") == NULL);
}

/* The historical control: on the same harness and the same strings, the old
 * prefix cut all three SD_SuspendReasonText() returns. This is what makes the
 * two tests above non-vacuous -- the harness demonstrably rejects something. */
TEST(old_prefix_cut_all_three_suspend_reasons)
{
    const char *cutReasons[] = {
        REASON_QUARANTINE, REASON_WIFI_FW, REASON_WIFI_STREAM
    };
    int i;

    for (i = 0; i < 3; i++) {
        char intended[FW_LOG_MESSAGE_SIZE * 2];
        char emitted[FW_LOG_MESSAGE_SIZE];

        intended_line(intended, sizeof(intended), OLD_PREFIX, cutReasons[i]);
        (void)fw_log_emit(emitted, OLD_FORMAT, cutReasons[i]);

        ASSERT_TRUE(strcmp(emitted, intended) != 0);
        ASSERT_TRUE((int)strlen(OLD_PREFIX) + (int)strlen(cutReasons[i]) + 2
                    > LOG_BUDGET);
    }
}

/* ...but NOT the NULL fallback. 88 + 33 + 2 = 123, two bytes inside the
 * ceiling, so the one case a casual bench reproduction is most likely to
 * reach looked perfectly healthy on the broken firmware. Asserted explicitly
 * rather than left to the tidier summary ("the old prefix cut everything"),
 * because a test that agreed with the tidier summary would be asserting
 * something false -- and because the healthy-looking NULL line is exactly
 * what would make someone close #1000 as unreproducible. */
TEST(old_prefix_did_not_cut_the_null_fallback)
{
    char intended[FW_LOG_MESSAGE_SIZE * 2];
    char emitted[FW_LOG_MESSAGE_SIZE];
    int  total = (int)strlen(OLD_PREFIX)
               + (int)strlen(FW_1000_NULL_FALLBACK) + 2;

    intended_line(intended, sizeof(intended), OLD_PREFIX,
                  FW_1000_NULL_FALLBACK);
    (void)fw_log_emit(emitted, OLD_FORMAT, FW_1000_NULL_FALLBACK);

    ASSERT_TRUE(strcmp(emitted, intended) == 0);
    ASSERT_TRUE(total <= LOG_BUDGET);
}

/* The budget has to have teeth against the SHIPPED prefix, not only against
 * the frozen historical one -- otherwise a prefix that grew back toward 88
 * characters by some other route could pass while only the control failed.
 * So take the real prefix, pad it until the worst-case line sits exactly at
 * the budget, and then push it over.
 *
 * `extra` steps 0 -> 3, skipping 1 and 2 deliberately: those land in the
 * degenerate window pinned by mirror_cuts_exactly_where_logger_does, where an
 * overrun of one loses only the LF and an overrun of two is repaired outright.
 * Neither loses a byte of the reason, so neither is the regression this is
 * guarding against. Three past the budget is the first length at which the
 * reason itself starts disappearing -- and because the remedy sits at the END
 * of the quarantine string, it is the first byte to go. */
TEST(growing_the_shipped_prefix_is_detected_at_the_boundary)
{
    static const struct {
        int extra;
        int expectEqual;
        int expectRemedy;
    } kSteps[] = {
        { 0, 1, 1 },   /* exactly at the budget: intact, remedy present */
        { 3, 0, 0 },   /* past the degenerate window: cut, remedy gone  */
    };
    const int   nSteps  = (int)(sizeof(kSteps) / sizeof(kSteps[0]));
    const char *worst   = REASON_QUARANTINE;
    const int   prefLen = (int)strlen(FW_1000_PREFIX);
    int         pad;
    int         s;

    /* Padding that puts the worst-case line exactly at the budget. A negative
     * value means the shipped prefix is ALREADY over -- the very regression
     * this file exists to catch, reported directly by the tests above. Fail
     * here too, then RETURN: the loop below would otherwise memset a negative
     * length, and a test that crashes on the defect is not a test that
     * reports it. (Found by running this suite against the reverted 88-byte
     * prefix -- which is the only reason it is known that ASSERT_TRUE records
     * and continues rather than aborting.) */
    pad = LOG_BUDGET - prefLen - (int)strlen(worst) - 2;
    ASSERT_TRUE(pad >= 0);
    if (pad < 0) {
        printf("    shipped prefix is %d bytes over budget against the "
               "longest reason; the growth sweep cannot run\n", -pad);
        return;
    }

    for (s = 0; s < nSteps; s++) {
        char fmt[FW_LOG_MESSAGE_SIZE * 8];
        char intended[FW_LOG_MESSAGE_SIZE * 4];
        char grown[FW_LOG_MESSAGE_SIZE * 2];
        char emitted[FW_LOG_MESSAGE_SIZE];
        int  n = pad + kSteps[s].extra;

        memcpy(grown, FW_1000_PREFIX, (size_t)prefLen);
        memset(grown + prefLen, '.', (size_t)n);
        grown[prefLen + n] = '\0';

        snprintf(fmt, sizeof(fmt), "%s%%s\r\n", grown);
        intended_line(intended, sizeof(intended), grown, worst);
        (void)fw_log_emit(emitted, fmt, worst);

        ASSERT_EQ(strcmp(emitted, intended) == 0, kSteps[s].expectEqual);
        ASSERT_EQ(strstr(emitted, QUARANTINE_REMEDY) != NULL,
                  kSteps[s].expectRemedy);
    }
}

int main(void)
{
    int i;

    printf("#1000 -- SD log arm refusal, prefix vs Logger's %d-byte ceiling\n",
           LOG_BUDGET);
    printf("  LOG_MESSAGE_SIZE=%d  reserves=%d/%d  (from Logger.h/Logger.c)\n",
           FW_LOG_MESSAGE_SIZE, FW_LOG_VSNPRINTF_RESERVE, FW_LOG_CLAMP_RESERVE);
    printf("  shipped prefix (%u B): \"%s\"\n",
           (unsigned)strlen(FW_1000_PREFIX), FW_1000_PREFIX);
    printf("  old prefix     (%u B)\n", (unsigned)strlen(OLD_PREFIX));
    for (i = 0; i < N_REASONS; i++) {
        int oldTotal = (int)strlen(OLD_PREFIX)
                     + (int)strlen(kReasons[i].reason) + 2;
        int newTotal = (int)strlen(FW_1000_PREFIX)
                     + (int)strlen(kReasons[i].reason) + 2;
        printf("  %-15s reason=%3u B   old=%3d %-4s   new=%3d %-4s\n",
               kReasons[i].name, (unsigned)strlen(kReasons[i].reason),
               oldTotal, (oldTotal > LOG_BUDGET) ? "CUT" : "fits",
               newTotal, (newTotal > LOG_BUDGET) ? "CUT" : "fits");
    }
    printf("---------------------------------------------\n");

    RUN(extracted_prefix_is_the_real_one);
    RUN(logger_ceiling_was_extracted_coherently);
    RUN(mirror_cuts_exactly_where_logger_does);
    RUN(shipped_prefix_emits_every_reason_intact);
    RUN(shipped_prefix_keeps_every_pair_inside_the_budget);
    RUN(quarantine_remedy_survives_now_and_did_not_before);
    RUN(old_prefix_cut_all_three_suspend_reasons);
    RUN(old_prefix_did_not_cut_the_null_fallback);
    RUN(growing_the_shipped_prefix_is_detected_at_the_boundary);
    return TEST_SUMMARY();
}
