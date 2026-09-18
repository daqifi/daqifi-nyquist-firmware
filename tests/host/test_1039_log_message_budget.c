/* ==========================================================================
 * test_1039_log_message_budget.c -- issue #1039
 *
 * WHAT IS UNDER TEST
 *
 * The four LOG_E messages #1039 shortened, measured against the ceiling
 * Util/Logger.c actually cuts at:
 *
 *   app_freertos.c            the #716 clock-mismatch warning
 *   services/SCPI/SCPIADC.c   the CONF:ADC:CHAN not-addressable refusal
 *   wifi_manager.c  x2        the two #589 SPI4/SD-card hints
 *
 * LogMessageFormatImpl (Logger.c) is
 *
 *     char buffer[LOG_MESSAGE_SIZE];                        // 128
 *     size = vsnprintf(buffer, LOG_MESSAGE_SIZE - 2, format, args);
 *     size = min((LOG_MESSAGE_SIZE - 3), size);
 *     ... then a \r\n fixup that appends CRLF when the (possibly cut)
 *         content does not already end in one ...
 *
 * so at most LOG_MESSAGE_SIZE - 3 = 125 formatted bytes survive. Past that
 * the tail is dropped and a CRLF is stapled onto the stump: no error, no
 * return code, no marker, and the line still reads as a finished sentence.
 * Because a remedy is written last, the remedy is what disappears -- which
 * makes a truncated diagnostic worse than no diagnostic.
 *
 * NONE OF THESE FOUR FORMAT STRINGS ENDS IN CRLF, and that makes the
 * boundary simpler here than in #1000's test_1000_sd_log_arm_budget.c. There,
 * the format carried its own trailing CRLF, so an overrun of exactly one or
 * two bytes fell into a degenerate window where the fixup lost only the LF,
 * or replaced both bytes and reproduced the intended line by accident. With
 * no CRLF in the format there is no such window: the fixup always takes its
 * third branch and appends a fresh CRLF, so the message survives whole if and
 * only if its formatted length is <= 125, and the very first byte past that
 * is a byte of the message. mirror_cuts_exactly_where_logger_does pins that.
 *
 * WHAT THIS TEST PROVES, AND WHY IT IS HERE RATHER THAN IN THE PYTHON SUITE
 *
 * The same PR adds tools/lint/log_budget.py, a static gate that MODELS this
 * truncation: it sums a format string's fixed bytes and a conservative
 * per-specifier worst-case width. A model can be wrong in ways its own
 * self-test cannot see, because the self-test is written against the same
 * model. This test does not model anything -- it runs the REAL message texts
 * through real vsnprintf, the real clamp and the real fixup, and compares the
 * emitted bytes to the intended bytes. The two agree today; the point is that
 * they are independent, so a future edit that satisfies the arithmetic but
 * not the C would be caught.
 *
 * It is a tests/host test, not a daqifi-python-test-suite one, for a reason
 * that is about reachability rather than convenience. Three of these four
 * messages cannot be provoked on a healthy bench device at all: the clock
 * warning needs configuration words programmed for a different PLL (which a
 * firmware update cannot write), and the two wifi_manager.c hints need a WINC
 * init failure sustained past eight seconds AND a specific SPI4 probe verdict
 * AND a specific SD-card-attached state. Only the SCPIADC refusal is
 * reachable (CONF:ADC:CHAN <bad>,1 then SYST:LOG?). A device-side companion
 * could therefore cover one site of four, through a transport that truncates
 * the evidence it would be reading. Offline, all four are covered exactly.
 * This also matches the repo's own policy split (CLAUDE.md, "How we test"):
 * firmware-internal unit tests stay in this repo.
 *
 * NOTHING LOAD-BEARING IS A MAGIC NUMBER OR A COPY
 *
 * LOG_MESSAGE_SIZE, BOTH of Logger.c's reservations, and all four live format
 * strings are EXTRACTED from the real firmware sources at build time by
 * gen_1039_log_budget.py into gen_1039_log_budget.h. Re-word a message and
 * this test re-measures the new words. Grow one past the ceiling and it
 * FAILS, rather than asking to be updated. The generator anchors each site on
 * the CODE that gates it, requires the anchor to be unique, and refuses to
 * emit a fragment of a concatenated literal -- see its own docstring.
 *
 * The one thing that IS copied is the four PRE-FIX message texts, frozen
 * below. They cannot be extracted: the fix deleted them from the source. They
 * are what gives this test the ability to fail -- without them every
 * assertion here would pass just as happily on a tree where #1039 was never
 * fixed, because a suite that only ever confirms the good case is not
 * evidence. They are frozen historical constants and cannot drift.
 *
 * The mirror of LogMessageFormatImpl below is deliberately this file's own
 * copy rather than something shared with test_1000_sd_log_arm_budget.c.
 * Neither test can include Logger.c (it pulls in FreeRTOS), so each carries a
 * mirror; keeping them separate means a mistake in one does not silently
 * become the other's baseline too.
 * ========================================================================== */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "test_framework.h"

/* Generated by the Makefile from the real firmware sources -- see the
 * $(LOG1039_GEN) recipe. Holds FW_LOG_MESSAGE_SIZE, FW_LOG_VSNPRINTF_RESERVE,
 * FW_LOG_CLAMP_RESERVE and the four FW_1039_*_FMT message texts. */
#include "gen_1039_log_budget.h"

/* --------------------------------------------------------------------------
 * Logger.c's truncation, reproduced exactly.
 * ------------------------------------------------------------------------ */

/* The largest formatted line that survives whole. */
#define LOG_BUDGET (FW_LOG_MESSAGE_SIZE - FW_LOG_CLAMP_RESERVE)

/* Mirror of LogMessageFormatImpl() up to the point it hands `buffer` to
 * LogMessageAdd(). Fills `out` (which must be FW_LOG_MESSAGE_SIZE bytes) with
 * the NUL-terminated message that would actually be stored -- what SYST:LOG?
 * would later print, not what the caller asked for -- and returns its length.
 *
 * All three branches of the CRLF fixup are carried over even though every
 * format here takes the third, because which branch runs is part of what is
 * being modelled: drop the branches and a cut line would look merely shorter
 * instead of being re-terminated, which is not what the device does. */
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
 * The four sites.
 * ------------------------------------------------------------------------ */

/* The PRE-FIX texts, verbatim from git history (PR #1110's diff). Frozen: the
 * fix removed them from the source, so they cannot be extracted and cannot
 * drift. Their only job is to prove these assertions bite. */
#define OLD_CLOCK \
    "Clock mismatch (#716): PBCLK3 is %u Hz, image built for %u Hz. " \
    "Device configuration words hold an older PLL and cannot be " \
    "updated by a firmware update - reprogram with a PICkit/IPE to " \
    "reach the intended clock. Rates are derived from the ACTUAL " \
    "clock, so streaming is accurate but ceilings are lower."

#define OLD_ADCCHAN \
    "CONF:ADC:CHAN: channel %d not addressable (not a settable " \
    "analog channel). The two-arg form is <channel>,<state>; use " \
    "the one-arg <mask> form to enable channels by bitmask."

#define OLD_SDPRESENT \
    "WiFi unreachable and an SD card IS present on the shared bus - the " \
    "card is likely bus-incompatible; remove it (wiki: SD-Card-Compatibility)"

#define OLD_SDCLEAR \
    "WiFi unreachable but SPI4 probes read CLEAR - if an SD card is " \
    "inserted, try removing it (transfer-speed interference is not " \
    "probe-visible)"

/* The operator-facing remedy in each message: the bytes a reader has to still
 * have in front of them for the line to be worth emitting. Each is chosen to
 * appear in BOTH the old and the new wording, so the same needle can be
 * looked for on both sides of the fix -- which is what makes
 * every_remedy_was_lost_before_and_survives_now a comparison rather than two
 * unrelated assertions. */
#define REMEDY_CLOCK     "PICkit/IPE"
#define REMEDY_ADCCHAN   "<mask>"
#define REMEDY_SDPRESENT "(wiki: SD-Card-Compatibility)"
#define REMEDY_SDCLEAR   "transfer-speed interference is not probe-visible"

/* Worst-case substitution, applied two ways per site: `render` produces the
 * line the call site MEANT to emit (unbounded), `emit` pushes the same
 * arguments through the mirror. Both take the format as a parameter so the
 * boundary sweep can pad it. */
typedef void (*RenderFn)(char *out, size_t n, const char *fmt);
typedef int  (*EmitFn)(char *out, const char *fmt);

/* Two %u. The widest a 32-bit unsigned prints is "4294967295", 10 bytes. */
static void render_clock(char *out, size_t n, const char *fmt)
{
    snprintf(out, n, fmt, 4294967295U, 4294967295U);
}
static int emit_clock(char *out, const char *fmt)
{
    return fw_log_emit(out, fmt, 4294967295U, 4294967295U);
}

/* One %d. INT32_MIN is the widest a 32-bit signed prints, 11 bytes with its
 * sign -- deliberately wider than anything this call site can actually be
 * reached with (the guard above it has already rejected param1 outside
 * 0..255, so the reachable worst case is 3 bytes). Measuring the conservative
 * bound is the point: it is the number the static gate charges, and a message
 * that fits at 11 fits at 3. */
static void render_adcchan(char *out, size_t n, const char *fmt)
{
    snprintf(out, n, fmt, (int)INT32_MIN);
}
static int emit_adcchan(char *out, const char *fmt)
{
    return fw_log_emit(out, fmt, (int)INT32_MIN);
}

/* No substitutions at all -- which is what made the two wifi_manager.c sites
 * the worst of the four before the fix: with nothing interpolated there is no
 * input that makes them fit, so they truncated identically on EVERY firing.
 * Copied through "%s" rather than passed as a format, because they contain no
 * conversions and must not be reinterpreted as if they did. */
static void render_none(char *out, size_t n, const char *fmt)
{
    snprintf(out, n, "%s", fmt);
}
static int emit_none(char *out, const char *fmt)
{
    return fw_log_emit(out, "%s", fmt);
}

typedef struct {
    const char *name;
    const char *fmt;        /* extracted from the firmware source */
    const char *oldFmt;     /* frozen pre-fix text                */
    const char *remedy;     /* must survive now, was lost before  */
    RenderFn    render;
    EmitFn      emit;
} Site;

static const Site kSites[] = {
    { "clock-mismatch (#716)", FW_1039_CLOCK_FMT,     OLD_CLOCK,
      REMEDY_CLOCK,     render_clock,   emit_clock   },
    { "CONF:ADC:CHAN refusal", FW_1039_ADCCHAN_FMT,   OLD_ADCCHAN,
      REMEDY_ADCCHAN,   render_adcchan, emit_adcchan },
    { "SPI4 hint, card present", FW_1039_SDPRESENT_FMT, OLD_SDPRESENT,
      REMEDY_SDPRESENT, render_none,    emit_none    },
    { "SPI4 hint, bus clear",  FW_1039_SDCLEAR_FMT,   OLD_SDCLEAR,
      REMEDY_SDCLEAR,   render_none,    emit_none    },
};
#define N_SITES ((int)(sizeof(kSites) / sizeof(kSites[0])))

/* Roomy enough for a padded format and its rendering; the boundary sweep
 * prepends up to a ceiling's worth of filler. */
#define WORK 512

/* --------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------ */

/* Extraction has a failure mode that no length assertion can catch: finding
 * NOTHING, or finding only the first chunk of a concatenated literal. Either
 * would sail through every budget check below while establishing nothing at
 * all. The generator refuses to emit a fragment (it requires the literal run
 * to be followed by ',' or ')') and refuses an empty message; this test pins
 * the same properties on the header itself, so a hand-doctored
 * gen_1039_log_budget.h cannot pass either.
 *
 * The remedy check here is about the SOURCE text, not about truncation: a
 * re-word that deletes the operator's remedy outright would leave a message
 * that fits the ceiling perfectly and says nothing useful, which is the one
 * way to satisfy #1039 while defeating it. */
TEST(extracted_messages_are_the_real_ones)
{
    int i;

    for (i = 0; i < N_SITES; i++) {
        ASSERT_TRUE(strlen(kSites[i].fmt) > 0);
        if (strstr(kSites[i].fmt, kSites[i].remedy) == NULL) {
            printf("    site '%s' no longer contains its remedy '%s'\n",
                   kSites[i].name, kSites[i].remedy);
        }
        ASSERT_TRUE(strstr(kSites[i].fmt, kSites[i].remedy) != NULL);
        /* The frozen control must be the LONGER text, or it is not the
         * pre-fix wording and proves nothing. */
        ASSERT_TRUE(strlen(kSites[i].oldFmt) > strlen(kSites[i].fmt));
    }

    /* The substitution signatures the worst-case arguments above assume. The
     * generator already enforces these against the source; repeated here so
     * the header cannot be edited into disagreeing with the renderers. */
    ASSERT_TRUE(strstr(FW_1039_CLOCK_FMT, "%u") != NULL);
    ASSERT_TRUE(strstr(strstr(FW_1039_CLOCK_FMT, "%u") + 2, "%u") != NULL);
    ASSERT_TRUE(strstr(FW_1039_ADCCHAN_FMT, "%d") != NULL);
    ASSERT_TRUE(strchr(FW_1039_SDPRESENT_FMT, '%') == NULL);
    ASSERT_TRUE(strchr(FW_1039_SDCLEAR_FMT, '%') == NULL);
}

/* Logger's own constants must have come out of Logger.h / Logger.c as numbers
 * that make sense together. A failed extraction yields 0, which makes
 * LOG_BUDGET negative and every other test fail loudly -- this one says WHY.
 * The clamp reserve has to be the larger of the two, because it is the clamp,
 * not the vsnprintf bound, that sets the surviving length. */
TEST(logger_ceiling_was_extracted_coherently)
{
    ASSERT_TRUE(FW_LOG_MESSAGE_SIZE >= 32);
    ASSERT_TRUE(FW_LOG_VSNPRINTF_RESERVE >= 1);
    ASSERT_TRUE(FW_LOG_CLAMP_RESERVE > FW_LOG_VSNPRINTF_RESERVE);
    ASSERT_TRUE(LOG_BUDGET > 0);
}

/* The mirror must cut where Logger does, or every verdict below is measured
 * against the wrong boundary. For a format with no trailing CRLF the rule is
 * uncomplicated -- the fixup always appends a fresh CRLF, so the emitted
 * length is min(L, LOG_BUDGET) + 2 and the line is intact exactly when
 * L <= LOG_BUDGET. The row at L == LOG_BUDGET + 1 is the one worth being
 * explicit about: it loses a byte of the MESSAGE, with no degenerate window
 * to absorb it, unlike #1000's CRLF-terminated formats. */
TEST(mirror_cuts_exactly_where_logger_does)
{
    static const int kLens[] = {
        1, LOG_BUDGET - 2, LOG_BUDGET - 1, LOG_BUDGET,
        LOG_BUDGET + 1, LOG_BUDGET + 2, LOG_BUDGET * 2
    };
    const int nCases = (int)(sizeof(kLens) / sizeof(kLens[0]));
    int c;

    for (c = 0; c < nCases; c++) {
        char body[WORK];
        char emitted[FW_LOG_MESSAGE_SIZE];
        int  emittedLen;
        int  survived = 0;
        int  expectSurvived;
        int  i;

        memset(body, 'x', (size_t)kLens[c]);
        body[kLens[c]] = '\0';

        emittedLen = fw_log_emit(emitted, "%s", body);

        expectSurvived = (kLens[c] < LOG_BUDGET) ? kLens[c] : LOG_BUDGET;
        ASSERT_EQ(emittedLen, expectSurvived + 2);
        ASSERT_EQ((int)strlen(emitted), expectSurvived + 2);
        ASSERT_EQ(strcmp(emitted, body) == 0, 0);   /* CRLF is always added */

        for (i = 0; emitted[i] != '\0'; i++) {
            if (emitted[i] == 'x') {
                survived++;
            }
        }
        ASSERT_EQ(survived, expectSurvived);

        /* However it ends up terminated, it always does end in CRLF -- which
         * is exactly why a cut line is indistinguishable from a short one at
         * the far end of SYST:LOG?. */
        ASSERT_TRUE(emittedLen >= 2);
        ASSERT_TRUE(emitted[emittedLen - 2] == '\r'
                    && emitted[emittedLen - 1] == '\n');
    }
}

/* THE FIX. Each shipped message, at its worst-case substitution, emits
 * byte-for-byte what the call site asked for (plus the CRLF Logger always
 * appends). Asserted against the emitted bytes, not against an arithmetic
 * prediction of them. */
TEST(every_shipped_message_survives_at_its_worst_case)
{
    int i;

    for (i = 0; i < N_SITES; i++) {
        char intended[WORK];
        char wanted[WORK + 4];
        char emitted[FW_LOG_MESSAGE_SIZE];

        kSites[i].render(intended, sizeof(intended), kSites[i].fmt);
        snprintf(wanted, sizeof(wanted), "%s\r\n", intended);
        (void)kSites[i].emit(emitted, kSites[i].fmt);

        if (strcmp(emitted, wanted) != 0) {
            printf("    site '%s' was cut: wanted %u bytes, emitted '%s'\n",
                   kSites[i].name, (unsigned)strlen(wanted), emitted);
        }
        ASSERT_TRUE(strcmp(emitted, wanted) == 0);
    }
}

/* The same invariant stated as the budget arithmetic, so a failure reports a
 * number a maintainer can act on -- how much over, and which site -- rather
 * than only "the bytes differ". */
TEST(every_shipped_message_is_inside_the_budget)
{
    int i;

    for (i = 0; i < N_SITES; i++) {
        char intended[WORK];
        int  total;

        kSites[i].render(intended, sizeof(intended), kSites[i].fmt);
        total = (int)strlen(intended);

        if (total > LOG_BUDGET) {
            printf("    site '%s': %d bytes, %d over the %d-byte budget\n",
                   kSites[i].name, total, total - LOG_BUDGET, LOG_BUDGET);
        }
        ASSERT_TRUE(total <= LOG_BUDGET);
    }
}

/* THE HISTORICAL CONTROL. On the same mirror, with the same worst-case
 * arguments, all four PRE-FIX messages were cut. Without this the suite would
 * pass identically on a tree where #1039 was never fixed. */
TEST(the_pre_fix_messages_were_all_cut)
{
    int i;

    for (i = 0; i < N_SITES; i++) {
        char intended[WORK];
        char wanted[WORK + 4];
        char emitted[FW_LOG_MESSAGE_SIZE];

        kSites[i].render(intended, sizeof(intended), kSites[i].oldFmt);
        snprintf(wanted, sizeof(wanted), "%s\r\n", intended);
        (void)kSites[i].emit(emitted, kSites[i].oldFmt);

        ASSERT_TRUE((int)strlen(intended) > LOG_BUDGET);
        ASSERT_TRUE(strcmp(emitted, wanted) != 0);
    }
}

/* THE HEADLINE. A diagnostic that loses its remedy is worse than no
 * diagnostic, because it still reads as finished. Asserted both ways on the
 * same harness: every remedy is present under the shipped text and absent
 * under the old one. */
TEST(every_remedy_was_lost_before_and_survives_now)
{
    int i;

    for (i = 0; i < N_SITES; i++) {
        char emittedNew[FW_LOG_MESSAGE_SIZE];
        char emittedOld[FW_LOG_MESSAGE_SIZE];

        (void)kSites[i].emit(emittedNew, kSites[i].fmt);
        (void)kSites[i].emit(emittedOld, kSites[i].oldFmt);

        if (strstr(emittedNew, kSites[i].remedy) == NULL) {
            printf("    site '%s': remedy '%s' is NOT in the emitted line\n",
                   kSites[i].name, kSites[i].remedy);
        }
        ASSERT_TRUE(strstr(emittedNew, kSites[i].remedy) != NULL);
        ASSERT_TRUE(strstr(emittedOld, kSites[i].remedy) == NULL);

        /* And the old line still ended in CRLF and still read like a
         * sentence, which is why nothing downstream could tell. */
        ASSERT_TRUE(strlen(emittedOld) >= 2);
        ASSERT_TRUE(emittedOld[strlen(emittedOld) - 2] == '\r');
    }
}

/* The budget must have teeth against the SHIPPED text, not only against the
 * frozen control -- otherwise a message that grew back by some other route
 * could pass while only the control failed. So take each real message, pad it
 * at the FRONT (a longer prefix is how these messages grow in practice, and
 * padding at the end would only ever cost padding bytes), and walk it across
 * the boundary:
 *
 * Two boundaries, each walked from the last byte that works to the first that
 * does not:
 *
 *   THE LINE boundary -- pad to exactly the budget (the whole line is still
 *     emitted verbatim), then one byte more. With no CRLF in the format there
 *     is no degenerate window to absorb that byte, so the very first byte past
 *     the budget is a byte of the MESSAGE.
 *   THE REMEDY boundary -- pad until the remedy's last byte sits exactly on
 *     the budget (it survives), then one byte more (it does not). This is the
 *     one #1039 is actually about: a message can be cut for a while before
 *     the operator loses anything they can act on, and that slack differs per
 *     site, so it is computed from where each remedy really ends rather than
 *     assumed. It is also why the LINE boundary alone would not do -- for two
 *     of these four sites the remedy IS the tail, and for the other two there
 *     is room to spare.
 *
 * Every expectation below is a fixed 0 or 1, not a value re-derived from the
 * same arithmetic the mirror uses; only the pad that reaches each boundary is
 * computed. */
TEST(growing_any_shipped_message_is_detected_at_the_boundary)
{
    int i;

    for (i = 0; i < N_SITES; i++) {
        char intended[WORK];
        const char *found;
        int  baseLen;
        int  remedyEnd;
        int  padFit;
        int  padRemedy;
        int  s;
        struct { const char *what; int pad; int expectEqual;
                 int expectRemedy; } steps[4];

        kSites[i].render(intended, sizeof(intended), kSites[i].fmt);
        baseLen = (int)strlen(intended);
        found = strstr(intended, kSites[i].remedy);
        ASSERT_TRUE(found != NULL);
        remedyEnd = (int)(found - intended) + (int)strlen(kSites[i].remedy);

        /* The pad at which the whole line exactly fills the budget, and the
         * pad at which the remedy's last byte exactly fills it. */
        padFit    = LOG_BUDGET - baseLen;
        padRemedy = LOG_BUDGET - remedyEnd;

        /* A negative padFit means the shipped message is ALREADY over -- the
         * regression the tests above report directly. Fail here too and stop,
         * rather than memset a negative length. */
        ASSERT_TRUE(padFit >= 0);
        if (padFit < 0) {
            printf("    site '%s' is %d bytes over budget; the growth sweep "
                   "cannot run\n", kSites[i].name, -padFit);
            continue;
        }
        /* The remedy ends at or before the line does, so its boundary is at
         * or beyond the line's. */
        ASSERT_TRUE(padRemedy >= padFit);

        steps[0].what = "line fits exactly";
        steps[0].pad = padFit;         steps[0].expectEqual = 1;
        steps[0].expectRemedy = 1;
        steps[1].what = "one byte past the line";
        steps[1].pad = padFit + 1;     steps[1].expectEqual = 0;
        steps[1].expectRemedy = -1;    /* varies per site; not asserted */
        steps[2].what = "remedy ends exactly on the budget";
        steps[2].pad = padRemedy;      steps[2].expectEqual = -1;
        steps[2].expectRemedy = 1;
        steps[3].what = "one byte past the remedy";
        steps[3].pad = padRemedy + 1;  steps[3].expectEqual = 0;
        steps[3].expectRemedy = 0;

        for (s = 0; s < 4; s++) {
            char grown[WORK];
            char grownIntended[WORK];
            char wanted[WORK + 4];
            char emitted[FW_LOG_MESSAGE_SIZE];
            int  padFits;

            /* ASSERT_TRUE RECORDS a failure and carries on -- it bumps
             * g_current_failed and prints, it does not abort
             * (test_framework.h). So this bounds check cannot stand as a bare
             * assertion above the memset: were a future change to the padFit
             * / padRemedy arithmetic to make it false, the failure would be
             * logged and then execution would walk straight into
             * memset(grown, '.', (size_t)pad) with a negative pad, which
             * (size_t) turns into a multi-exabyte length -- a crash or a
             * smashed stack instead of the clean test failure that was
             * intended. Hold the verdict in a variable so the assertion still
             * counts on every step, then let it gate the unsafe work: the
             * same assert-then-skip shape the padFit guard above uses. */
            padFits = steps[s].pad >= 0
                      && steps[s].pad + (int)strlen(kSites[i].fmt)
                         < (int)sizeof(grown);
            ASSERT_TRUE(padFits);
            if (!padFits) {
                printf("    site '%s', step '%s': pad %d + %d format bytes "
                       "does not fit the %d-byte work buffer; step skipped\n",
                       kSites[i].name, steps[s].what, steps[s].pad,
                       (int)strlen(kSites[i].fmt), (int)sizeof(grown));
                continue;
            }
            memset(grown, '.', (size_t)steps[s].pad);
            strcpy(grown + steps[s].pad, kSites[i].fmt);

            kSites[i].render(grownIntended, sizeof(grownIntended), grown);
            snprintf(wanted, sizeof(wanted), "%s\r\n", grownIntended);
            (void)kSites[i].emit(emitted, grown);

            if (steps[s].expectEqual >= 0
                && (strcmp(emitted, wanted) == 0) != steps[s].expectEqual) {
                printf("    site '%s', step '%s' (pad %d): line equality was "
                       "%d\n", kSites[i].name, steps[s].what, steps[s].pad,
                       strcmp(emitted, wanted) == 0);
            }
            if (steps[s].expectRemedy >= 0
                && (strstr(emitted, kSites[i].remedy) != NULL)
                   != steps[s].expectRemedy) {
                printf("    site '%s', step '%s' (pad %d): remedy presence was "
                       "%d\n", kSites[i].name, steps[s].what, steps[s].pad,
                       strstr(emitted, kSites[i].remedy) != NULL);
            }
            if (steps[s].expectEqual >= 0) {
                ASSERT_EQ(strcmp(emitted, wanted) == 0, steps[s].expectEqual);
            }
            if (steps[s].expectRemedy >= 0) {
                ASSERT_EQ(strstr(emitted, kSites[i].remedy) != NULL,
                          steps[s].expectRemedy);
            }
        }
    }
}

int main(void)
{
    int i;

    printf("#1039 -- four LOG_E messages vs Logger's %d-byte ceiling\n",
           LOG_BUDGET);
    printf("  LOG_MESSAGE_SIZE=%d  reserves=%d/%d  (from Logger.h/Logger.c)\n",
           FW_LOG_MESSAGE_SIZE, FW_LOG_VSNPRINTF_RESERVE, FW_LOG_CLAMP_RESERVE);
    for (i = 0; i < N_SITES; i++) {
        char nowLine[WORK];
        char oldLine[WORK];

        kSites[i].render(nowLine, sizeof(nowLine), kSites[i].fmt);
        kSites[i].render(oldLine, sizeof(oldLine), kSites[i].oldFmt);
        printf("  %-24s old=%3d %-4s   new=%3d %-4s  (margin %d)\n",
               kSites[i].name,
               (int)strlen(oldLine),
               ((int)strlen(oldLine) > LOG_BUDGET) ? "CUT" : "fits",
               (int)strlen(nowLine),
               ((int)strlen(nowLine) > LOG_BUDGET) ? "CUT" : "fits",
               LOG_BUDGET - (int)strlen(nowLine));
    }
    printf("---------------------------------------------\n");

    RUN(extracted_messages_are_the_real_ones);
    RUN(logger_ceiling_was_extracted_coherently);
    RUN(mirror_cuts_exactly_where_logger_does);
    RUN(every_shipped_message_survives_at_its_worst_case);
    RUN(every_shipped_message_is_inside_the_budget);
    RUN(the_pre_fix_messages_were_all_cut);
    RUN(every_remedy_was_lost_before_and_survives_now);
    RUN(growing_any_shipped_message_is_detected_at_the_boundary);
    return TEST_SUMMARY();
}
