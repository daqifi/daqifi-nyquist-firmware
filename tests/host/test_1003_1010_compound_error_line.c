/* ==========================================================================
 * test_1003_1010_compound_error_line.c
 *
 * #1003 ("A compound message's error line is written into the unterminated
 * query response, corrupting the reply") and #1010 ("An error inside a
 * compound program message is glued onto the previous result") -- same root
 * defect, reported independently.
 *
 * Root cause: processCommand() (parser.c) wrote a compound-message ';'
 * separator SPECULATIVELY, before knowing whether the unit it precedes would
 * succeed. SCPI_ErrorPushEx() -> SCPI_ErrorEmit() (error.c) then emits an
 * error SYNCHRONOUSLY, through the very same interface->write() a query
 * result uses, while SCPI_Parse() defers a successful query's own line
 * ending to a single end-of-message writeNewLine(). Three shapes, all
 * reachable on `main` from any callback that raises after an earlier unit in
 * the same compound message succeeded -- not specific to one command:
 *   1. is_query unit fails: the speculative ';' is already on the wire,
 *      immediately ahead of the error text ("value;**ERROR: ...").
 *   2. non-query unit fails: no separator was ever written (the speculative
 *      write is gated on is_query), so the error glues straight onto the
 *      prior result ("value**ERROR: ...").
 *   3. an undefined header is rejected directly in SCPI_Parse(), bypassing
 *      processCommand()'s separator logic entirely -- same glue as (2).
 * Every case also leaves a spurious blank line at message end: the error's
 * own hardcoded line ending plus SCPI_Parse()'s deferred writeNewLine() both
 * fire, because nothing marks the still-open result as "closed".
 *
 * Fix shape (see docs/BUILD_AND_TOOLCHAIN.md's #1003/#1010 rows):
 *   - New scpi_t.pending_delimiter (types.h). processCommand() ARMS it
 *     instead of writing ';' immediately.
 *   - SCPI_ErrorEmit() (error.c) discards a still-armed flag before it can
 *     reach the wire, and -- when an earlier unit's result is unterminated
 *     (first_output == FALSE) at the moment a real error (err != 0) is
 *     emitted -- writes SCPI_LINE_ENDING to close it out first, then marks
 *     it closed so the deferred writeNewLine() does not add a second, blank
 *     line.
 *   - writeNewLine() re-arms first_output = TRUE after writing, and
 *     SCPI_Init() sets first_output = TRUE over the zeroing memset, so an
 *     error raised OUTSIDE a parse (SCPI_Input()'s own buffer-overrun push,
 *     or simply before the first parse ever runs) does not inherit a stale
 *     FALSE and get a spurious leading blank line of its own.
 *   - The armed flag is consumed at the ONE point every write funnels
 *     through regardless of path: SCPI_FlushPendingDelimiter()
 *     (SCPIInterface.c), which both transports' interface->write()
 *     implementations (SCPI_USB_Write, SCPI_TCP_Write) call before their own
 *     payload. That covers a query's SCPI_ResultXxx() output AND a
 *     callback's direct interface->write() calls (SYSTem:STORage:SD:LISt?,
 *     SYST:LOG?, and the rest of the ~20 direct-write query callbacks) with
 *     the SAME mechanism, so the compound separator lands correctly for
 *     either shape without auditing each callback individually.
 *
 * Round 1 fix (Qodo /agentic_review on this PR, "Storage failures still add
 * blank lines"): the FIRST cut of the above used !first_output, not
 * line_open, to decide whether an error needs a closing line first --
 * first_output answers "has ANY unit in this compound message produced
 * output yet" (message-level), which is the wrong question for THIS
 * decision. A direct-write callback that closes its OWN line before raising
 * (SCPIStorageSD.c's SCPI_CheckSDCardPresent() writes a message that already
 * ends "\r\n", then pushes an error) was still seeing first_output FALSE
 * from an EARLIER, unrelated unit's still-open result, and got a second,
 * spurious "\r\n" inserted ahead of its already-terminated diagnostic. New
 * scpi_t.line_open, tracked at the same transport-write funnel
 * (SCPI_TrackLineOpen(), called after the real payload write) answers the
 * narrower, correct question -- does the wire right now end in a terminator
 * -- for BOTH write shapes. See TestTerminatedFailQuery() below, which
 * mirrors SCPI_CheckSDCardPresent()'s exact shape.
 *
 * Host-testability: same situation as #999's own test (see that file's
 * header) -- SCPIInterface.c, UsbCdc.c and wifi_tcp_server.c are not
 * includable on a host (FreeRTOS + the full USB/WiFi driver graph), but
 * parser.c/error.c (the actual vendored fix) are plain portable C and link
 * standalone. TestWrite() below re-implements SCPI_FlushPendingDelimiter()'s
 * one decision (not merely something similar -- the identical
 * "len > 0 && pending_delimiter -> write ';', clear flag, then the payload"
 * shape) so this test exercises the REAL parser.c/error.c and a faithful
 * stand-in for the transport side. The Makefile's run_1003_tests recipe
 * greps the real SCPIInterface.c/UsbCdc.c/wifi_tcp_server.c to keep that
 * stand-in honest -- see its own comment for what it pins.
 *
 * "Prove the fix fails before it passes" (per this fire's brief) was done
 * interactively, not as a committed test: parser.c/error.c/types.h were
 * reverted to pristine origin/main and a throwaway probe using this same
 * capture-everything shape was run against them, reproducing all three wire
 * shapes above byte-for-byte before the fix was reapplied. Reported in the
 * PR body rather than carried here, because a pre-fix build of THIS file
 * cannot even compile (pending_delimiter does not exist yet on that source).
 * ========================================================================== */
#include "test_framework.h"

#include <stdio.h>
#include <string.h>

#include "scpi/scpi.h"

/* ---- minimal scpi_interface_t: mirrors the #1003/#1010 fix's transport
 * side (SCPI_USB_Write/SCPI_TCP_Write + SCPI_FlushPendingDelimiter +
 * SCPI_TrackLineOpen), capture everything else --------------------------- */

#define CAPTURE_SIZE 512

typedef struct {
    char   data[CAPTURE_SIZE];
    size_t len;
} WriteCapture;

static WriteCapture gCapture;

static size_t TestWrite(scpi_t * context, const char * data, size_t len) {
    WriteCapture * cap = (WriteCapture *) context->user_context;
    /* Mirrors SCPI_FlushPendingDelimiter() (SCPIInterface.c) exactly: a
     * pending separator is flushed ahead of a non-empty payload, and only
     * then. A failed unit that never writes anything never reaches this at
     * all -- SCPI_ErrorEmit() (error.c) discards the flag first. */
    if (len > 0 && context->pending_delimiter) {
        context->pending_delimiter = FALSE;
        if (cap != NULL && cap->len < CAPTURE_SIZE) {
            cap->data[cap->len++] = ';';
        }
    }
    if (cap != NULL) {
        size_t n = len;
        if (cap->len + n > CAPTURE_SIZE) {
            n = CAPTURE_SIZE - cap->len;
        }
        memcpy(cap->data + cap->len, data, n);
        cap->len += n;
    }
    /* Mirrors SCPI_TrackLineOpen() (SCPIInterface.c) exactly, against the
     * SAME data/len this call received -- not whatever SCPI_FlushPending
     * Delimiter() wrote above (the ';' never ends in a terminator, and this
     * payload write always determines the final state). */
    if (len > 0) {
        size_t termLen = strlen(SCPI_LINE_ENDING);
        if (len >= termLen && memcmp(data + len - termLen, SCPI_LINE_ENDING, termLen) == 0) {
            context->line_open = FALSE;
        } else {
            context->line_open = TRUE;
        }
    }
    return len;
}

/* Mirrors SCPI_USB_Error/SCPI_TCP_Error verbatim: same format string, same
 * single write through context->interface->write() -- so an error's own
 * text also passes through TestWrite() above, exactly as in production. */
static int TestError(scpi_t * context, int_fast16_t err) {
    if (err != 0) {
        char ip[100];
        const char * err_str = SCPI_ErrorTranslate(err);
        if (err_str == NULL) {
            err_str = "Unknown";
        }
        int n = snprintf(ip, sizeof(ip), "**ERROR: %d, \"%s\"\r\n", (int32_t) err, err_str);
        if (n > 0) {
            context->interface->write(context, ip, (size_t) n);
        }
    }
    return 0;
}

static scpi_result_t TestControl(scpi_t * context, scpi_ctrl_name_t ctrl, scpi_reg_val_t val) {
    (void) context; (void) ctrl; (void) val;
    return SCPI_RES_OK;
}

static scpi_result_t TestFlush(scpi_t * context) {
    (void) context;
    return SCPI_RES_OK;
}

static scpi_interface_t gTestInterface = {
    .error = TestError,
    .write = TestWrite,
    .control = TestControl,
    .flush = TestFlush,
    .reset = NULL,
};

/* ---- test command table -------------------------------------------------
 * *OK?         -- succeeds via SCPI_ResultCharacters (the "mediated" path
 *                 the majority of query callbacks use). A COMMON command
 *                 (leading '*'), used as every scenario's first unit for the
 *                 same reason every #1003/#1010 reproduction below leads
 *                 with *IDN?: composeCompoundCommand() (utils.c) treats a
 *                 second unit that starts with neither '*' nor ':' as a
 *                 RELATIVE header under the previous unit's own path --
 *                 "TEST:OK?;TEST:FAIL?" is composed into "TEST:TEST:FAIL?"
 *                 and (correctly) rejected as an undefined header, not what
 *                 this file means to test. "Previous command was common
 *                 command -- nothing to do" (utils.c) short-circuits that
 *                 composition, exactly as it does for real *IDN?-led
 *                 compound messages, so every unit after the first is
 *                 written out in full below.
 * TEST:FAIL?   -- a query that always fails without writing anything, e.g.
 *                 SCPI_NotImplemented (SYSTem:COMMunicate:LAN:DNS2?, #1003's
 *                 own reproduction).
 * TEST:FAILSET -- a non-query command that always fails without writing
 *                 anything, e.g. an argument-range reject
 *                 (SYSTem:COMMunicate:LAN:MAC with a bad value).
 * TEST:DIRECT? -- succeeds by writing straight through
 *                 context->interface->write(), bypassing SCPI_ResultXxx() --
 *                 the shape SYSTem:STORage:SD:LISt?, SYST:LOG? and ~18 other
 *                 registered query callbacks use, and exactly the shape that
 *                 broke a round-1 attempt at this fix (moving the ';' write
 *                 into SCPI_ResultXxx()'s own delimiter helper silently
 *                 dropped it for every one of these). This fix's flush
 *                 point (interface->write() itself) does not have that gap.
 * TEST:TERMFAIL? -- mirrors SCPIStorageSD.c's SCPI_CheckSDCardPresent()
 *                 exactly: writes an ALREADY CRLF-terminated diagnostic
 *                 straight through interface->write(), then pushes an
 *                 error. Regression case for this PR's own round-1 Qodo
 *                 finding ("Storage failures still add blank lines") --
 *                 see line_open in types.h/error.c.
 * ------------------------------------------------------------------------- */
static scpi_result_t TestOkQuery(scpi_t * context) {
    SCPI_ResultCharacters(context, "OK", 2);
    return SCPI_RES_OK;
}

static scpi_result_t TestFailQuery(scpi_t * context) {
    (void) context;
    return SCPI_RES_ERR;
}

static scpi_result_t TestFailSet(scpi_t * context) {
    (void) context;
    return SCPI_RES_ERR;
}

static scpi_result_t TestDirectQuery(scpi_t * context) {
    context->interface->write(context, "DIRECT", 6);
    return SCPI_RES_OK;
}

#define TEST_TERMFAIL_MSG "\r\nError !! Diagnostic\r\n"

static scpi_result_t TestTerminatedFailQuery(scpi_t * context) {
    context->interface->write(context, TEST_TERMFAIL_MSG, strlen(TEST_TERMFAIL_MSG));
    SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
    return SCPI_RES_ERR;
}

static const scpi_command_t gTestCommands[] = {
    {.pattern = "*OK?", .callback = TestOkQuery},
    {.pattern = "TEST:FAIL?", .callback = TestFailQuery},
    {.pattern = "TEST:FAILSET", .callback = TestFailSet},
    {.pattern = "TEST:DIRECT?", .callback = TestDirectQuery},
    {.pattern = "TEST:TERMFAIL?", .callback = TestTerminatedFailQuery},
    SCPI_CMD_LIST_END,
};

static const scpi_unit_def_t gTestUnits[] = {
    SCPI_UNITS_LIST_END,
};

/* ---- helpers -------------------------------------------------------------
 * Same shape as test_999_scpi_context_storage_isolation.c's FeedLine/
 * InitTestContext -- real commands always arrive newline-terminated in one
 * SCPI_Input() call (microrl appends ENDL before calling execute()). */
static void FeedLine(scpi_t * context, const char * line) {
    char buf[160];
    size_t n = strlen(line);
    memcpy(buf, line, n);
    buf[n] = '\n';
    SCPI_Input(context, buf, (int) (n + 1));
}

static void InitTestContext(scpi_t * context, char * inputBuffer, size_t inputBufferLen,
                             scpi_error_t * errorQueue, int16_t errorQueueLen) {
    SCPI_Init(context, gTestCommands, &gTestInterface, gTestUnits,
              "TEST", "1003", "0", "00-00",
              inputBuffer, inputBufferLen,
              errorQueue, errorQueueLen);
    context->user_context = &gCapture;
}

#define ASSERT_CAPTURE_EQ(expected) do {                                   \
    const char * _exp = (expected);                                       \
    size_t _expLen = strlen(_exp);                                        \
    ASSERT_EQ(gCapture.len, _expLen);                                      \
    ASSERT_BYTES(gCapture.data, _exp, _expLen);                            \
} while (0)

#define NEW_TEST_CONTEXT(ctxVar) \
    static char ctxVar##_inputBuffer[512]; \
    static scpi_error_t ctxVar##_errorQueue[17]; \
    scpi_t ctxVar; \
    memset(&gCapture, 0, sizeof(gCapture)); \
    InitTestContext(&ctxVar, ctxVar##_inputBuffer, sizeof(ctxVar##_inputBuffer), \
                     ctxVar##_errorQueue, 17)

/* ---- #1003 shape 1 / #1010: is_query unit fails after a success --------- */

TEST(query_fails_after_success_terminates_cleanly) {
    NEW_TEST_CONTEXT(ctx);

    FeedLine(&ctx, "*OK?;TEST:FAIL?");

    /* The first query's value is terminated before the error text, no
     * stray ';' introduces the error, and no extra blank line follows --
     * #1003's three acceptance bullets, this shape. */
    ASSERT_CAPTURE_EQ("OK\r\n**ERROR: -200, \"Execution error\"\r\n");

    /* #1003 acceptance: "the error queue is unaffected -- still exactly one
     * entry per reject". */
    ASSERT_EQ(SCPI_ErrorCount(&ctx), 1);
}

/* ---- #1003 shape 2 / #1010's own reproduction: non-query unit fails ----- */

TEST(command_fails_after_success_gets_a_separator) {
    NEW_TEST_CONTEXT(ctx);

    FeedLine(&ctx, "*OK?;TEST:FAILSET");

    /* Pre-fix this glued directly onto the prior result with NO separator
     * at all (main never wrote one for a non-query unit). */
    ASSERT_CAPTURE_EQ("OK\r\n**ERROR: -200, \"Execution error\"\r\n");
}

/* ---- #1003 shape 3 / #1010's illustrated case: undefined header -------- */

TEST(undefined_header_after_success_terminates_cleanly) {
    NEW_TEST_CONTEXT(ctx);

    /* Mirrors #1010's own hardware reproduction, "*IDN?;FOO:BAR?". This
     * path bypasses processCommand() entirely (SCPI_Parse() pushes
     * SCPI_ERROR_UNDEFINED_HEADER directly), which is exactly why #1003's
     * fix lives in SCPI_ErrorEmit() rather than only in processCommand(). */
    FeedLine(&ctx, "*OK?;NOSUCH:HEADER?");

    ASSERT_CAPTURE_EQ("OK\r\n**ERROR: -113, \"Undefined header\"\r\n");
}

/* ---- round-1 Qodo finding: a direct writer that already terminates its
 * own diagnostic before raising must not get a second, spurious blank line
 * from an EARLIER, unrelated unit's still-open result. Mirrors
 * SCPIStorageSD.c's SCPI_CheckSDCardPresent() exactly. ------------------- */

TEST(terminated_direct_write_failure_after_success_has_no_blank_line) {
    NEW_TEST_CONTEXT(ctx);

    FeedLine(&ctx, "*OK?;TEST:TERMFAIL?");

    /* *OK? left first_output FALSE (its own "OK" is still awaiting the
     * deferred end-of-message newline) but the wire is now properly closed
     * -- TEST:TERMFAIL? closed ITS OWN line before raising. A first-cut fix
     * keyed on !first_output would have inserted a second "\r\n" here,
     * between the diagnostic and the error line. */
    ASSERT_CAPTURE_EQ("OK;" TEST_TERMFAIL_MSG "**ERROR: -200, \"Execution error\"\r\n");
}

TEST(terminated_direct_write_failure_as_single_command_unchanged) {
    NEW_TEST_CONTEXT(ctx);

    FeedLine(&ctx, "TEST:TERMFAIL?");

    /* Single-command control: no earlier unit, first_output was already
     * TRUE, so this shape was never broken by the round-1 finding -- pinned
     * anyway so a future change to line_open can't quietly break it. */
    ASSERT_CAPTURE_EQ(TEST_TERMFAIL_MSG "**ERROR: -200, \"Execution error\"\r\n");
}

/* ---- #1003 acceptance: "the single-command case is unchanged" ---------- */

TEST(single_command_error_is_unchanged) {
    NEW_TEST_CONTEXT(ctx);

    FeedLine(&ctx, "TEST:FAIL?");

    /* Exactly one **ERROR line, no leading blank line from the new
     * termination logic (first_output is still TRUE -- nothing precedes
     * this error in the message). */
    ASSERT_CAPTURE_EQ("**ERROR: -200, \"Execution error\"\r\n");
}

/* ---- #1010 acceptance: "two successful queries keep their ';' separator
 * -- that part is correct today and must not regress" -------------------- */

TEST(two_successful_queries_keep_semicolon_mediated_path) {
    NEW_TEST_CONTEXT(ctx);

    FeedLine(&ctx, "*OK?;*OK?");

    ASSERT_CAPTURE_EQ("OK;OK\r\n");
}

/* Same acceptance bullet, but the SECOND unit writes through the
 * direct-write path (interface->write() straight from the callback) instead
 * of SCPI_ResultXxx() -- the shape a naive "move the ';' write into
 * SCPI_ResultXxx()'s delimiter helper" fix regresses (found by Qodo against
 * PR #1096's round-1 cut: "*IDN?;SYST:LOG? ran together with no
 * separator"). This fix's flush point is interface->write() itself, which
 * both paths funnel through, so there is nothing direct-write-specific to
 * regress. */
TEST(two_successful_queries_keep_semicolon_direct_write_path) {
    NEW_TEST_CONTEXT(ctx);

    FeedLine(&ctx, "*OK?;TEST:DIRECT?");

    ASSERT_CAPTURE_EQ("OK;DIRECT\r\n");
}

/* ---- a third unit after a failure is not further corrupted ------------- */

TEST(success_after_a_failure_starts_clean) {
    NEW_TEST_CONTEXT(ctx);

    FeedLine(&ctx, "*OK?;TEST:FAIL?;*OK?");

    ASSERT_CAPTURE_EQ("OK\r\n**ERROR: -200, \"Execution error\"\r\nOK\r\n");
}

/* ---- SCPI_Init()'s first_output = TRUE fix: an error before the very
 * first parse must not get a spurious leading blank line ----------------- */

TEST(error_before_any_parse_has_no_leading_blank_line) {
    NEW_TEST_CONTEXT(ctx);

    /* No FeedLine() at all -- push directly, the way a boot-time or
     * pre-parse diagnostic would. Without SCPI_Init()'s explicit
     * first_output = TRUE (it is FALSE straight out of the zeroing memset),
     * SCPI_ErrorEmit() would misread that as "an earlier unit's result is
     * still unterminated" and prefix this first-ever error with "\r\n". */
    SCPI_ErrorPush(&ctx, SCPI_ERROR_EXECUTION_ERROR);

    ASSERT_CAPTURE_EQ("**ERROR: -200, \"Execution error\"\r\n");
}

/* ---- writeNewLine()'s first_output re-arm: an error between two SEPARATE
 * top-level parses must not inherit the previous parse's stale FALSE ----- */

TEST(error_between_two_parses_has_no_leading_blank_line) {
    NEW_TEST_CONTEXT(ctx);

    FeedLine(&ctx, "*OK?");
    ASSERT_CAPTURE_EQ("OK\r\n");
    memset(&gCapture, 0, sizeof(gCapture));

    /* This mirrors SCPI_Input()'s own input-buffer-overrun push, which can
     * fire between parses rather than during one. Without writeNewLine()'s
     * re-arm, first_output stays FALSE forever after the parse above (that
     * function writes the line ending but never used to reset the flag),
     * and this unrelated, later error would wrongly inherit it. */
    SCPI_ErrorPush(&ctx, SCPI_ERROR_EXECUTION_ERROR);

    ASSERT_CAPTURE_EQ("**ERROR: -200, \"Execution error\"\r\n");
}

int main(void) {
    RUN(query_fails_after_success_terminates_cleanly);
    RUN(command_fails_after_success_gets_a_separator);
    RUN(undefined_header_after_success_terminates_cleanly);
    RUN(terminated_direct_write_failure_after_success_has_no_blank_line);
    RUN(terminated_direct_write_failure_as_single_command_unchanged);
    RUN(single_command_error_is_unchanged);
    RUN(two_successful_queries_keep_semicolon_mediated_path);
    RUN(two_successful_queries_keep_semicolon_direct_write_path);
    RUN(success_after_a_failure_starts_clean);
    RUN(error_before_any_parse_has_no_leading_blank_line);
    RUN(error_between_two_parses_has_no_leading_blank_line);
    return test_summary();
}
