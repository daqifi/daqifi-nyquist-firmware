/* ==========================================================================
 * test_1055_stream_end_summary_budget.c
 *
 * #1055 / operator ruling 5->A: the session-end loss summary in
 * services/streaming.c (Streaming_EmitSessionSummary) was ONE LOG_E whose
 * worst-case formatted length was 212 bytes against Logger's 125-byte content
 * ceiling. Logger truncates the TAIL, so what an operator actually lost was
 * the per-interface breakdown AND the trailing "(all post-grace)" qualifier --
 * the words that say these are STEADY counters rather than totals. A number
 * read as a total when it is a post-grace subset is a misleading number, so
 * the truncation did not merely shorten the message, it changed what it said.
 *
 * WHAT IS UNDER TEST, and what is deliberately NOT
 *
 * This measures the THREE format strings the split produced, against the
 * ceiling derived from the real Util/Logger.h. It is NOT a general LOG_E
 * scanner: the fleet-wide over-limit checker is #1110's, per the same ruling
 * that split this work, and duplicating it here would put two checkers with
 * two models of the same property in two PRs.
 *
 * THE CONTROL IS THE POINT. test_the_pre_split_line_did_not_fit asserts the
 * ORIGINAL format EXCEEDS the ceiling. Without it this suite would pass just
 * as happily against the unsplit firmware and prove nothing -- a fix that
 * leaves every check passing has no regression test. The original format is
 * reproduced here verbatim precisely so the failing case still exists after
 * the source stopped containing it.
 *
 * WHY THE LITERALS ARE DUPLICATED HERE
 *
 * streaming.c is not host-compilable (FreeRTOS, Harmony, the board graph), so
 * the three formats are copied. A copy that drifts is a test of itself, so the
 * Makefile greps streaming.c for each of these three literals and fails the
 * BUILD if one is absent -- a reword must come back as a build error that
 * forces re-measurement, not as a silently stale pass.
 *
 * Run: make -C tests/host run
 * ========================================================================== */
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "test_framework.h"

/* FW_LOG_MESSAGE_SIZE is injected by the Makefile from the real Util/Logger.h.
 * Not hand-copied: this whole file is an assertion about a budget, and a
 * budget checked against a stale capacity checks nothing. */
#ifndef FW_LOG_MESSAGE_SIZE
#error "FW_LOG_MESSAGE_SIZE must be -D'd from Util/Logger.h by the Makefile"
#endif

/* Logger appends "\r\n\0" to a message that does not already end in a newline
 * (Util/Logger.c's CRLF fixup: buffer[size]='\r', [size+1]='\n', [size+2]=0),
 * so the CONTENT a message may carry is three bytes less than the buffer.
 * gen_1029_log_budget.py derives the same 125 from the same two facts. */
#define LOG_CONTENT_CEILING  (FW_LOG_MESSAGE_SIZE - 3)

/* ------------------------------------------------------------------ *
 * The formats, exactly as streaming.c carries them.
 * ------------------------------------------------------------------ */
#define FMT_HEADLINE \
    "Stream end (post-grace): lost %u/%llu samples (%u%%)"
#define FMT_BYTES \
    "Stream end (post-grace) bytes lost: USB=%u WiFi=%u SD=%u"
#define FMT_SAMPLES \
    "Stream end (post-grace) samples lost: encFail=%u encDrop=%u dioDrop=%u eos=%u"

/* The pre-split single line. Kept ONLY as the negative control below. */
#define FMT_PRE_SPLIT \
    "Stream end: lost %u/%llu samples (%u%%), USB=%u WiFi=%u SD=%u bytes, " \
    "encFail=%u encDrop=%u dioDrop=%u eos=%u (all post-grace)"

/* ------------------------------------------------------------------ *
 * Worst-case formatted length.
 *
 * Widths are the maximum decimal expansion of the promoted argument types
 * streaming.c actually passes -- `unsigned` (10 digits, 4294967295) and
 * `unsigned long long` (20 digits, 18446744073709551615). "%%" is one literal
 * byte and NOT a conversion, which is the distinction a naive '%' count gets
 * wrong: the headline would otherwise be charged for a fourth argument it does
 * not take.
 *
 * An unmodelled conversion returns SIZE_MAX rather than a plausible number, so
 * a new specifier cannot quietly be scored as if it were free. Returning 0 or
 * skipping it would make this a clean-bill generator for exactly the case it
 * cannot evaluate.
 * ------------------------------------------------------------------ */
#define W_U     10u
#define W_LLU   20u
#define UNMODELLED  ((size_t)-1)

static size_t worst_case_len(const char *fmt)
{
    size_t n = 0u;
    const char *p = fmt;

    while (*p != '\0') {
        if (*p != '%') {
            n++;
            p++;
            continue;
        }
        if (strncmp(p, "%%", 2) == 0) {
            n += 1u;            /* one literal '%' */
            p += 2;
        } else if (strncmp(p, "%llu", 4) == 0) {
            n += W_LLU;
            p += 4;
        } else if (strncmp(p, "%u", 2) == 0) {
            n += W_U;
            p += 2;
        } else {
            return UNMODELLED;
        }
    }
    return n;
}

/* Count real conversions (so "%%" does not inflate the tally). */
static unsigned conversion_count(const char *fmt)
{
    unsigned c = 0u;
    const char *p = fmt;

    while (*p != '\0') {
        if (*p != '%') {
            p++;
            continue;
        }
        if (strncmp(p, "%%", 2) == 0) {
            p += 2;
        } else if (strncmp(p, "%llu", 4) == 0) {
            c++;
            p += 4;
        } else if (strncmp(p, "%u", 2) == 0) {
            c++;
            p += 2;
        } else {
            return 0u;          /* unmodelled -- caller's assertion will fail */
        }
    }
    return c;
}

TEST(the_length_model_is_itself_correct)
{
    /* The model is the instrument every other assertion here depends on, so
     * it is checked against hand-computable cases FIRST. A measurement tool
     * that was never verified is how a wrong baseline gets believed. */
    ASSERT_EQ(worst_case_len(""), 0u);
    ASSERT_EQ(worst_case_len("abc"), 3u);
    ASSERT_EQ(worst_case_len("%u"), (size_t)W_U);
    ASSERT_EQ(worst_case_len("%llu"), (size_t)W_LLU);

    /* "%%" is ONE byte, not a conversion -- the distinction the headline
     * depends on for its "(%u%%)" tail. */
    ASSERT_EQ(worst_case_len("%%"), 1u);
    ASSERT_EQ(worst_case_len("a%%b"), 3u);
    ASSERT_EQ(conversion_count("%%"), 0u);
    ASSERT_EQ(conversion_count("%u%%"), 1u);

    /* an unmodelled specifier must be LOUD, never scored as zero */
    ASSERT_TRUE(worst_case_len("%s") == UNMODELLED);
    ASSERT_TRUE(worst_case_len("%d") == UNMODELLED);

    /* and the ceiling is the one the firmware actually has */
    ASSERT_EQ((unsigned)FW_LOG_MESSAGE_SIZE, 128u);
    ASSERT_EQ((unsigned)LOG_CONTENT_CEILING, 125u);
}

TEST(the_pre_split_line_did_not_fit)
{
    /* ⛔ THE CONTROL. 212 bytes against a 125-byte ceiling -- 87 bytes over,
     * and Logger drops them from the TAIL, which is where both the breakdown
     * and the "(all post-grace)" qualifier lived.
     *
     * This arm is what makes the rest of this file a regression test rather
     * than a description: it FAILS on the pre-#1055 firmware. */
    const size_t n = worst_case_len(FMT_PRE_SPLIT);

    ASSERT_TRUE(n != UNMODELLED);
    ASSERT_EQ(n, 212u);
    ASSERT_TRUE(n > (size_t)LOG_CONTENT_CEILING);

    /* and name what was lost: the qualifier is in the overflowing tail */
    ASSERT_TRUE(strstr(FMT_PRE_SPLIT, "(all post-grace)") != NULL);
    ASSERT_TRUE((size_t)(strstr(FMT_PRE_SPLIT, "(all post-grace)")
                         - FMT_PRE_SPLIT) > 0u);
}

TEST(each_summary_line_fits_the_logger_ceiling)
{
    const char *const fmts[] = { FMT_HEADLINE, FMT_BYTES, FMT_SAMPLES };
    size_t i;

    for (i = 0u; i < sizeof(fmts) / sizeof(fmts[0]); i++) {
        const size_t n = worst_case_len(fmts[i]);
        ASSERT_TRUE(n != UNMODELLED);
        ASSERT_TRUE(n <= (size_t)LOG_CONTENT_CEILING);
    }

    /* The measured numbers, pinned exactly rather than "<= 125": a future edit
     * that eats the headroom should have to look at it. Two lines could only
     * be made to fit by cutting the labels to encF/encD/dioD for 3 bytes of
     * margin, which is why this is three. */
    ASSERT_EQ(worst_case_len(FMT_HEADLINE), 83u);
    ASSERT_EQ(worst_case_len(FMT_BYTES), 80u);
    ASSERT_EQ(worst_case_len(FMT_SAMPLES), 109u);
}

TEST(the_split_lost_none_of_the_ten_quantities)
{
    /* ⛔ The hazard a pure length check cannot see: splitting a message is
     * also an opportunity to DROP a field, and every length assertion above
     * would be happier for it. So account for the arguments.
     *
     * The pre-split line took ten: totalSampleLoss, totalAttempted,
     * lossPercent, usb/wifi/sd bytes, encFail, encDrop, dioDrop, eos. */
    ASSERT_EQ(conversion_count(FMT_PRE_SPLIT), 10u);

    ASSERT_EQ(conversion_count(FMT_HEADLINE), 3u);   /* loss/attempted/percent */
    ASSERT_EQ(conversion_count(FMT_BYTES), 3u);      /* USB/WiFi/SD            */
    ASSERT_EQ(conversion_count(FMT_SAMPLES), 4u);    /* encFail/encDrop/dio/eos*/
    ASSERT_EQ(conversion_count(FMT_HEADLINE)
              + conversion_count(FMT_BYTES)
              + conversion_count(FMT_SAMPLES), 10u);

    /* and the one 64-bit field stayed 64-bit: totalAttempted is a uint64
     * sample count, and demoting it to %u in a reword would silently wrap
     * past 4.29e9 samples -- reachable in a long session. */
    ASSERT_TRUE(strstr(FMT_HEADLINE, "%llu") != NULL);
    ASSERT_EQ(conversion_count(FMT_HEADLINE), 3u);

    /* every label from the original survives somewhere in the three */
    {
        const char *const labels[] = { "USB=", "WiFi=", "SD=", "encFail=",
                                       "encDrop=", "dioDrop=", "eos=" };
        size_t i;
        for (i = 0u; i < sizeof(labels) / sizeof(labels[0]); i++) {
            const bool present =
                strstr(FMT_HEADLINE, labels[i]) != NULL ||
                strstr(FMT_BYTES, labels[i]) != NULL ||
                strstr(FMT_SAMPLES, labels[i]) != NULL;
            ASSERT_TRUE(present);
            /* and it was in the original, so this is a SURVIVAL claim and not
             * a tautology over labels the split invented */
            ASSERT_TRUE(strstr(FMT_PRE_SPLIT, labels[i]) != NULL);
        }
    }
}

TEST(every_line_says_post_grace_on_its_own)
{
    /* Three separate log records: an operator may see any ONE of them, and a
     * steady-counter number read as a total is misleading. Stating the
     * qualifier once -- on the headline only -- would recreate the original
     * defect for whichever line arrived alone. */
    ASSERT_TRUE(strstr(FMT_HEADLINE, "(post-grace)") != NULL);
    ASSERT_TRUE(strstr(FMT_BYTES, "(post-grace)") != NULL);
    ASSERT_TRUE(strstr(FMT_SAMPLES, "(post-grace)") != NULL);

    /* and each is still identifiable as the same event */
    ASSERT_TRUE(strstr(FMT_HEADLINE, "Stream end") != NULL);
    ASSERT_TRUE(strstr(FMT_BYTES, "Stream end") != NULL);
    ASSERT_TRUE(strstr(FMT_SAMPLES, "Stream end") != NULL);
}

int main(void)
{
    printf("=== #1055: session-end loss summary vs Logger's content ceiling ===\n");
    RUN(the_length_model_is_itself_correct);
    RUN(the_pre_split_line_did_not_fit);
    RUN(each_summary_line_fits_the_logger_ceiling);
    RUN(the_split_lost_none_of_the_ten_quantities);
    RUN(every_line_says_post_grace_on_its_own);
    return TEST_SUMMARY();
}
