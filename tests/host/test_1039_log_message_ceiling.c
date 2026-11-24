/* ==========================================================================
 * test_1039_log_message_ceiling.c -- issue #1039 / PR #1116
 *
 * WHAT IS UNDER TEST
 *
 * The four LOG_E messages PR #1116 shortened: app_freertos.c's #716
 * clock-mismatch warning, SCPIADC.c's not-addressable-channel refusal, and
 * wifi_manager.c's two SD-vs-WiFi hints. All four are operator-visible
 * through SYST:LOG? (the standing rule: errors go to the log, SCPI returns
 * the code, the user reads the log for why), so shortening them is a
 * behaviour change, not a comment edit -- see the PR's withdrawn-exemption
 * comment (issuecomment-5702648759) for the full reasoning. This is the
 * regression guard that withdrawal calls for.
 *
 * THE CEILING, RE-DERIVED FROM SOURCE (not trusted from the PR body or from
 * PR #1110's own accounting). LogMessageFormatImpl (Util/Logger.c) is:
 *
 *     char buffer[LOG_MESSAGE_SIZE];                          // 128, Logger.h:308
 *     size = vsnprintf(buffer, LOG_MESSAGE_SIZE - 2, format, args);   // Logger.c:292
 *     size = min((LOG_MESSAGE_SIZE - 3), size);                       // Logger.c:299
 *     ... then a \r\n fixup appends CRLF (2 bytes) when the (possibly cut)
 *         content does not already end in one, or converts a trailing lone
 *         '\n' to '\r\n' (1 byte), or leaves an existing "\r\n" alone ...
 *
 * vsnprintf's own contract is "return how many bytes I *would* have written
 * with no limit", so the 126-byte call bound does not by itself say what
 * survives -- the min() clamp on the NEXT line is what actually sets the
 * surviving length, to 128 - 3 = 125. The "-3" is 2 bytes for a worst-case
 * \r\n fixup plus 1 for the NUL the fixup's own indexing needs room for
 * (buffer[size] / buffer[size+1] / buffer[size+2] must all stay inside the
 * 128-byte array), which is exactly what the source comment above the
 * vsnprintf call says ("Reserve 3 bytes for \r\n\0 - guarantees room to
 * append").
 *
 * 125 matches what PR #1116's own body claims for the same two lines. That
 * agreement is a useful cross-check, not the derivation -- the derivation is
 * the arithmetic above, read straight off Util/Logger.h and Util/Logger.c
 * and pinned against them by this file's Makefile recipe (see
 * $(LOGCEIL_BIN)): a firmware change to any of the three numbers fails the
 * BUILD, not just this comment going stale.
 *
 * WHY THIS IS INDEPENDENT OF PR #1110's PARKED TOOLING
 *
 * tests/host/gen_1039_log_budget.py and tools/lint/log_budget.py accumulated
 * 13 confirmed defects across two audit rounds on PR #1110 (a
 * codepoint-vs-byte bug in the generator, an unspliced extract_site(), and
 * more) while the four message edits themselves drew zero findings in
 * either round -- which is why #1116 exists as the split-out PR (see its
 * description) and why this guard does not import, generate from, or link
 * against any part of that tooling. Nothing here parses a diff, scans a
 * directory, or classifies a call site; it is four hardcoded string copies
 * measured against one hand-derived ceiling, the same shape as
 * test_1000_sd_log_arm_budget.c's own headline tests.
 *
 * TECHNIQUE
 *
 * firmware/src/app_freertos.c, SCPIADC.c and wifi_manager.c are not
 * includable on a host (FreeRTOS, Harmony, the whole board/driver graph),
 * the same constraint test_1000_sd_log_arm_budget.c documents for
 * SCPIInterface.c. Util/Logger.c is not linkable either (it pulls in the
 * UART/ICSP transport). So LogMessageFormatImpl's truncation is mirrored
 * here -- same vsnprintf call, same clamp, same three-way CRLF fixup -- and
 * run against the ACTUAL message text, copied verbatim from this PR's diff,
 * with worst-case values substituted for every %u/%d. The Makefile recipe
 * greps each format string's literal source lines back out of the three
 * touched files and fails the build if any no longer matches exactly (the
 * same technique $(SDLOG_BIN)'s own REASON_* guard uses for
 * SD_SuspendReasonText()'s three return strings), so an edit to any of
 * these messages without a matching edit here fails the build rather than
 * silently leaving this test measuring text the device no longer emits.
 *
 * As a frozen historical control -- proving the harness actually
 * distinguishes "fits" from "cut" rather than passing regardless (the same
 * technique test_1000's OLD_PREFIX uses) -- the pre-#1039 text for all four
 * sites is copied here too, verbatim from git history / PR #1116's diff,
 * and asserted to overflow the SAME ceiling under the SAME harness.
 * ========================================================================== */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "test_framework.h"

/* --------------------------------------------------------------------------
 * Logger's ceiling. Each number is pinned against the real firmware source
 * by a grep guard in this file's Makefile recipe -- see the derivation in
 * the header comment above.
 * ------------------------------------------------------------------------ */

/* Util/Logger.h:308 -- char buffer[LOG_MESSAGE_SIZE], the whole log line
 * including its NUL terminator. */
#define FW_LOG_MESSAGE_SIZE 128

/* Util/Logger.c:292 -- the vsnprintf() call bound (LOG_MESSAGE_SIZE - 2).
 * Does not by itself set the surviving length -- see FW_LOG_CLAMP_RESERVE. */
#define FW_LOG_VSNPRINTF_RESERVE 2

/* Util/Logger.c:299 -- the clamp (LOG_MESSAGE_SIZE - 3). This is the
 * reserve that actually sets what survives: 3 bytes held back so the
 * \r\n\0 fixup always has room to append. */
#define FW_LOG_CLAMP_RESERVE 3

/* The largest formatted CONTENT (vsnprintf's own body, before any \r\n
 * fixup) that survives LogMessageFormatImpl whole. 128 - 3 = 125. */
#define LOG_BUDGET (FW_LOG_MESSAGE_SIZE - FW_LOG_CLAMP_RESERVE)

/* --------------------------------------------------------------------------
 * LogMessageFormatImpl, reproduced up to the point it hands `buffer` to
 * LogMessageAdd() -- same three branches, same order, same arithmetic.
 * ------------------------------------------------------------------------ */
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
        /* already CRLF-terminated */
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

/* Worst-case substitution values: %u on a 32-bit unsigned maxes at
 * "4294967295" (10 bytes); %d on a 32-bit signed maxes at "-2147483648"
 * (11 bytes). Written via arithmetic so no source literal exceeds INT_MAX. */
#define WORST_U 4294967295U
#define WORST_D (-2147483647 - 1)

/* --------------------------------------------------------------------------
 * The four shipped format strings -- copied verbatim from PR #1116's diff.
 * The Makefile recipe greps each one's literal source lines back out of the
 * real files and fails the build on any drift.
 * ------------------------------------------------------------------------ */

/* app_freertos.c, app_SystemInit()'s #716 clock-mismatch LOG_E. */
#define MSG1_FORMAT \
    "Clock mismatch (#716): PBCLK3 %u Hz, built for %u Hz - " \
    "reprogram via PICkit/IPE, not a firmware update."

/* SCPIADC.c, ADCChanEnableSetClaimed()'s not-addressable-channel refusal.
 *
 * Still byte-for-byte what the device emits, which is the only thing this
 * suite measures -- but since #888 it is ASSEMBLED rather than written out
 * in one literal. The nine channel-resolve sites in SCPIADC.c share one
 * AdcChannelResolve helper whose format is "%s: channel %d not
 * addressable%s"; this site supplies "CONFigure:ADC:CHANnel" as cmd and
 * "; two-arg form is ..." as hint, and the concatenation is the string
 * below. The Makefile recipe therefore pins the helper's format line AND
 * this site's cmd/hint arguments as two separate positional block checks;
 * see the comment above LOGCEIL_BIN's recipe for why both are required. */
#define MSG2_FORMAT \
    "CONFigure:ADC:CHANnel: channel %d not addressable; " \
    "two-arg form is <channel>,<state>, one-arg form is a " \
    "<mask>."

/* wifi_manager.c, MainState()'s SD-present hint (no substitutions). */
#define MSG3_TEXT \
    "WiFi down and an SD card IS present on the shared bus - likely bus-incompatible; remove it (wiki: SD-Card-Compatibility)"

/* Same file, the SPI4-clear twin (no substitutions). */
#define MSG4_TEXT \
    "WiFi down but SPI4 probes read CLEAR - remove any SD card; transfer-speed interference is not probe-visible"

/* --------------------------------------------------------------------------
 * Frozen historical controls: the pre-#1039 text, verbatim from git history
 * (PR #1116's diff). Not in the tree any more and cannot drift -- kept only
 * so this suite can prove the harness rejects something.
 * ------------------------------------------------------------------------ */

#define OLD_MSG1_FORMAT \
    "Clock mismatch (#716): PBCLK3 is %u Hz, image built for %u Hz. " \
    "Device configuration words hold an older PLL and cannot be " \
    "updated by a firmware update - reprogram with a PICkit/IPE to " \
    "reach the intended clock. Rates are derived from the ACTUAL " \
    "clock, so streaming is accurate but ceilings are lower."

#define OLD_MSG2_FORMAT \
    "CONF:ADC:CHAN: channel %d not addressable (not a settable " \
    "analog channel). The two-arg form is <channel>,<state>; use " \
    "the one-arg <mask> form to enable channels by bitmask."

#define OLD_MSG3_TEXT \
    "WiFi unreachable and an SD card IS present on the shared bus - the card is likely bus-incompatible; remove it (wiki: SD-Card-Compatibility)"

#define OLD_MSG4_TEXT \
    "WiFi unreachable but SPI4 probes read CLEAR - if an SD card is inserted, try removing it (transfer-speed interference is not probe-visible)"

/* --------------------------------------------------------------------------
 * Compares fw_log_emit's output against the caller's full intended content.
 * None of the eight formats above contains an embedded '\n', so every one
 * of them takes fw_log_emit's "no newline yet -> append \r\n" branch and the
 * two trailing bytes are always the fixup's own CRLF, never message content
 * -- checked explicitly below rather than assumed. Returns 1 iff the
 * content survived whole (right length, right bytes, valid CRLF); 0 if it
 * was cut. A cut line still ends in a well-formed CRLF, which is exactly
 * why nothing downstream of Logger can tell the difference by itself.
 * ------------------------------------------------------------------------ */
static int content_intact(const char *emitted, int emittedLen, const char *intended)
{
    size_t wantLen = strlen(intended);

    if (emittedLen < 2) {
        return 0;
    }
    if (!(emitted[emittedLen - 2] == '\r' && emitted[emittedLen - 1] == '\n')) {
        return 0;
    }
    if ((size_t)(emittedLen - 2) != wantLen) {
        return 0;
    }
    return memcmp(emitted, intended, wantLen) == 0;
}

/* --------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------ */

TEST(logger_ceiling_pinned_from_source)
{
    ASSERT_EQ(FW_LOG_MESSAGE_SIZE, 128);
    ASSERT_EQ(FW_LOG_VSNPRINTF_RESERVE, 2);
    ASSERT_EQ(FW_LOG_CLAMP_RESERVE, 3);
    ASSERT_EQ(LOG_BUDGET, 125);
}

TEST(shipped_msg1_clock_mismatch_fits_intact)
{
    char intended[512];
    char emitted[FW_LOG_MESSAGE_SIZE];
    int emittedLen;

    snprintf(intended, sizeof(intended), MSG1_FORMAT, WORST_U, WORST_U);
    emittedLen = fw_log_emit(emitted, MSG1_FORMAT, WORST_U, WORST_U);

    ASSERT_EQ((int)strlen(intended), 119);   /* PR #1116's own hand count; margin 6 */
    ASSERT_TRUE((int)strlen(intended) <= LOG_BUDGET);
    ASSERT_TRUE(content_intact(emitted, emittedLen, intended));
}

TEST(shipped_msg2_chan_not_addressable_fits_intact)
{
    char intended[512];
    char emitted[FW_LOG_MESSAGE_SIZE];
    int emittedLen;

    snprintf(intended, sizeof(intended), MSG2_FORMAT, WORST_D);
    emittedLen = fw_log_emit(emitted, MSG2_FORMAT, WORST_D);

    ASSERT_EQ((int)strlen(intended), 120);   /* margin 5 */
    ASSERT_TRUE((int)strlen(intended) <= LOG_BUDGET);
    ASSERT_TRUE(content_intact(emitted, emittedLen, intended));
}

TEST(shipped_msg3_sd_present_fits_intact)
{
    char emitted[FW_LOG_MESSAGE_SIZE];
    int emittedLen = fw_log_emit(emitted, MSG3_TEXT);

    ASSERT_EQ((int)strlen(MSG3_TEXT), 120);  /* margin 5, no substitutions */
    ASSERT_TRUE((int)strlen(MSG3_TEXT) <= LOG_BUDGET);
    ASSERT_TRUE(content_intact(emitted, emittedLen, MSG3_TEXT));
}

TEST(shipped_msg4_spi4_clear_fits_intact)
{
    char emitted[FW_LOG_MESSAGE_SIZE];
    int emittedLen = fw_log_emit(emitted, MSG4_TEXT);

    ASSERT_EQ((int)strlen(MSG4_TEXT), 107);  /* margin 18, no substitutions */
    ASSERT_TRUE((int)strlen(MSG4_TEXT) <= LOG_BUDGET);
    ASSERT_TRUE(content_intact(emitted, emittedLen, MSG4_TEXT));
}

TEST(old_msg1_clock_mismatch_was_cut)
{
    char intended[512];
    char emitted[FW_LOG_MESSAGE_SIZE];
    int emittedLen;

    snprintf(intended, sizeof(intended), OLD_MSG1_FORMAT, WORST_U, WORST_U);
    emittedLen = fw_log_emit(emitted, OLD_MSG1_FORMAT, WORST_U, WORST_U);

    ASSERT_EQ((int)strlen(intended), 315);   /* 190 over LOG_BUDGET */
    ASSERT_TRUE((int)strlen(intended) > LOG_BUDGET);
    ASSERT_FALSE(content_intact(emitted, emittedLen, intended));
    ASSERT_EQ(emittedLen, LOG_BUDGET + 2);
}

TEST(old_msg2_chan_not_addressable_was_cut)
{
    char intended[512];
    char emitted[FW_LOG_MESSAGE_SIZE];
    int emittedLen;

    snprintf(intended, sizeof(intended), OLD_MSG2_FORMAT, WORST_D);
    emittedLen = fw_log_emit(emitted, OLD_MSG2_FORMAT, WORST_D);

    ASSERT_EQ((int)strlen(intended), 181);   /* 56 over LOG_BUDGET */
    ASSERT_TRUE((int)strlen(intended) > LOG_BUDGET);
    ASSERT_FALSE(content_intact(emitted, emittedLen, intended));
    ASSERT_EQ(emittedLen, LOG_BUDGET + 2);
}

TEST(old_msg3_sd_present_was_cut)
{
    char emitted[FW_LOG_MESSAGE_SIZE];
    int emittedLen = fw_log_emit(emitted, OLD_MSG3_TEXT);

    ASSERT_EQ((int)strlen(OLD_MSG3_TEXT), 139);  /* 14 over LOG_BUDGET */
    ASSERT_TRUE((int)strlen(OLD_MSG3_TEXT) > LOG_BUDGET);
    ASSERT_FALSE(content_intact(emitted, emittedLen, OLD_MSG3_TEXT));
    ASSERT_EQ(emittedLen, LOG_BUDGET + 2);
}

TEST(old_msg4_spi4_clear_was_cut)
{
    char emitted[FW_LOG_MESSAGE_SIZE];
    int emittedLen = fw_log_emit(emitted, OLD_MSG4_TEXT);

    ASSERT_EQ((int)strlen(OLD_MSG4_TEXT), 139);  /* 14 over LOG_BUDGET */
    ASSERT_TRUE((int)strlen(OLD_MSG4_TEXT) > LOG_BUDGET);
    ASSERT_FALSE(content_intact(emitted, emittedLen, OLD_MSG4_TEXT));
    ASSERT_EQ(emittedLen, LOG_BUDGET + 2);
}

/* THE BOUNDARY, demonstrated on the shipped text itself (not just the old,
 * already-retired copies above) so the guard has teeth against a FUTURE
 * growth of a currently-fitting message, not only against the historical
 * one. MSG4 carries the largest margin (18 bytes) of the four, so it is the
 * one with room to pad up TO the boundary and still demonstrate a genuine
 * "grows past the ceiling" failure rather than starting past it.
 *
 * `extra` steps 0 -> 3. This test's format is bare "%s" (no baked-in CRLF),
 * unlike test_1000_sd_log_arm_budget.c's growing_the_shipped_prefix_...
 * sibling, whose format is "%s%s\r\n" -- there the format's OWN trailing
 * CRLF competes with the clamp for the same 2 bytes of slack, which is what
 * creates that test's degenerate +1/+2 window (an overrun of 1 or 2 costs
 * only the format's own terminator, not the caller's content). No such
 * competition exists here: with nothing but the message itself ahead of the
 * clamp, `min(LOG_BUDGET, size)` cuts message BYTES starting at the very
 * first byte past LOG_BUDGET. So every one of +0/+1/+2/+3 is worth pinning,
 * and +1 is already a genuine loss, not a fixup-only artifact (Qodo
 * /agentic_review, PR #1116: the original three-row table here wrongly
 * carried over the sibling test's degenerate-window reasoning without
 * checking it depended on that trailing "\r\n"). */
TEST(growing_a_shipped_message_is_detected_at_the_boundary)
{
    static const struct { int extra; int expectIntact; } kSteps[] = {
        { 0, 1 },   /* exactly at LOG_BUDGET: intact                        */
        { 1, 0 },   /* 1 past: the clamp already drops the last message byte */
        { 2, 0 },   /* 2 past: two message bytes dropped                    */
        { 3, 0 },   /* 3 past: three message bytes dropped                  */
    };
    const int nSteps  = (int)(sizeof(kSteps) / sizeof(kSteps[0]));
    const size_t base = strlen(MSG4_TEXT);
    const int pad      = LOG_BUDGET - (int)base;
    int s;

    /* A negative pad means MSG4_TEXT is ALREADY over LOG_BUDGET -- the very
     * regression this file exists to catch, already reported by
     * shipped_msg4_spi4_clear_fits_intact above. Fail here too, then
     * RETURN: the loop below would otherwise memset() a negative length
     * (cast to a huge size_t) into a 256-byte stack buffer. Found by running
     * this exact scenario as this PR's mutation demonstration -- the
     * unguarded version segfaulted instead of reporting a clean failure,
     * which is the same crash-vs-report gap
     * test_1000_sd_log_arm_budget.c's own growing_the_shipped_prefix_...
     * comment already documents finding for its sibling prefix. */
    ASSERT_TRUE(pad >= 0);
    if (pad < 0) {
        printf("    MSG4_TEXT is %d bytes over LOG_BUDGET; the growth sweep"
               " cannot run\n", -pad);
        return;
    }

    for (s = 0; s < nSteps; s++) {
        char grown[FW_LOG_MESSAGE_SIZE * 2];
        char emitted[FW_LOG_MESSAGE_SIZE];
        int  emittedLen;
        int  n = pad + kSteps[s].extra;

        memcpy(grown, MSG4_TEXT, base);
        memset(grown + base, '.', (size_t)n);
        grown[base + (size_t)n] = '\0';

        emittedLen = fw_log_emit(emitted, "%s", grown);

        ASSERT_EQ(content_intact(emitted, emittedLen, grown), kSteps[s].expectIntact);
    }
}

int main(void)
{
    printf("#1039 -- four shortened LOG_E messages vs Logger's %d-byte ceiling\n",
           LOG_BUDGET);
    printf("  LOG_MESSAGE_SIZE=%d  vsnprintf reserve=%d  clamp reserve=%d"
           "  (Util/Logger.h:308, Util/Logger.c:292,299)\n",
           FW_LOG_MESSAGE_SIZE, FW_LOG_VSNPRINTF_RESERVE, FW_LOG_CLAMP_RESERVE);
    printf("---------------------------------------------\n");

    RUN(logger_ceiling_pinned_from_source);
    RUN(shipped_msg1_clock_mismatch_fits_intact);
    RUN(shipped_msg2_chan_not_addressable_fits_intact);
    RUN(shipped_msg3_sd_present_fits_intact);
    RUN(shipped_msg4_spi4_clear_fits_intact);
    RUN(old_msg1_clock_mismatch_was_cut);
    RUN(old_msg2_chan_not_addressable_was_cut);
    RUN(old_msg3_sd_present_was_cut);
    RUN(old_msg4_spi4_clear_was_cut);
    RUN(growing_a_shipped_message_is_detected_at_the_boundary);
    return TEST_SUMMARY();
}
