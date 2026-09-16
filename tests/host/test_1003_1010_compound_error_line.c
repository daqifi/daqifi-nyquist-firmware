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
 * #1115 CORRECTION: the "That covers ... (SYSTem:STORage:SD:LISt?, ...)"
 * claim two paragraphs up was wrong. SD:LISt?'s payload is delivered by the
 * SD task (sd_card_manager_DataReadyCB(), app_freertos.c) straight into
 * UsbCdc_WriteToBuffer()/wifi_tcp_server_WriteBuffer() -- it never reaches
 * interface->write() at all, so neither pending_delimiter nor line_open was
 * ever tracked for it. See the #1115 section below (TEST:BYPASS? and its
 * tests) for the fix and the four findings it closes.
 *
 * Host-testability: same situation as #999's own test (see that file's
 * header) -- SCPIInterface.c, UsbCdc.c and wifi_tcp_server.c are not
 * includable on a host (FreeRTOS + the full USB/WiFi driver graph), but
 * parser.c/error.c (the actual vendored fix) are plain portable C and link
 * standalone. TestWrite() below re-implements SCPI_FlushPendingDelimiter()'s
 * one decision (not merely something similar -- the identical
 * "pending_delimiter -> write ';', clear flag, then the payload" shape,
 * #1115: no longer gated on len > 0, see TestWrite()'s own comment) so this
 * test exercises the REAL parser.c/error.c and a faithful stand-in for the
 * transport side. The Makefile's run_1003_tests recipe
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
     * pending separator is flushed the moment it is armed, regardless of
     * len. #1115: this used to also require len > 0 ("ahead of a non-empty
     * payload, and only then"), which was exactly what let an empty-but-
     * successful query result's own armed separator go un-flushed (writeData(),
     * parser.c, used to return before ever calling interface->write() for
     * len == 0). Both writeData() and this flush now make a deliberate
     * zero-length call specifically so that case gets the same one chance
     * every other unit gets. A failed unit that never writes anything never
     * reaches this at all -- SCPI_ErrorEmit() (error.c) discards the flag
     * first. */
    if (context->pending_delimiter) {
        context->pending_delimiter = FALSE;
        if (cap != NULL && cap->len < CAPTURE_SIZE) {
            cap->data[cap->len++] = ';';
        }
    }
    if (cap != NULL && len > 0) {
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
 *                 the shape SYST:LOG? and ~19 other registered query
 *                 callbacks use (SD:LISt? does NOT -- see the #1115
 *                 correction above and TEST:BYPASS? below), and exactly the
 *                 shape that broke a round-1 attempt at this fix (moving the
 *                 ';' write into SCPI_ResultXxx()'s own delimiter helper
 *                 silently dropped it for every one of these). This fix's
 *                 flush point (interface->write() itself) does not have
 *                 that gap.
 * TEST:TERMFAIL? -- mirrors SCPIStorageSD.c's SCPI_CheckSDCardPresent()
 *                 exactly: writes an ALREADY CRLF-terminated diagnostic
 *                 straight through interface->write(), then pushes an
 *                 error. Regression case for this PR's own round-1 Qodo
 *                 finding ("Storage failures still add blank lines") --
 *                 see line_open in types.h/error.c.
 * TEST:EMPTY?  -- #1115 findings 1/2: succeeds via SCPI_ResultCharacters()
 *                 with len == 0 (the exact shape of SYSTem:COMMunicate:
 *                 UART:READ? 0 and SYST:LOG? on an empty buffer) --
 *                 exercises writeData()'s (parser.c) real zero-length flush
 *                 path directly, no mock needed.
 * TEST:PARTIALERR? -- #1115 finding 3: mirrors SCPI_SysInfoGet()
 *                 (SCPIInterface.c) exactly -- pushes a real error
 *                 mid-callback (synchronously emitted, error.c) but keeps
 *                 going, writing real payload straight through
 *                 interface->write() afterward, then still reports
 *                 success. Exercises processCommand()'s (parser.c) real
 *                 line_open/first_output reconciliation directly.
 * TEST:BYPASS? -- #1115 finding 0: mirrors SCPIStorageSD.c's
 *                 SCPI_StorageSDListDir() exactly, AS FIXED by this PR --
 *                 an explicit zero-length flush through the real funnel,
 *                 then a payload write that lands straight in the capture
 *                 buffer bypassing TestWrite() entirely (SD:LIST?'s async
 *                 SD-task callback has no scpi_t* and cannot reach
 *                 interface->write() at all -- the same reason this
 *                 function, not TestWrite(), does the bypass here), then a
 *                 hand-reconciled line_open = TRUE. SCPIStorageSD.c and
 *                 app_freertos.c are not host-includable (FreeRTOS + the
 *                 full SD driver graph) -- see this file's header -- so
 *                 this is a faithful stand-in for the ACTUAL fix's shape,
 *                 the same way TEST:DIRECT?/TEST:TERMFAIL? stand in for
 *                 their production callbacks.
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

static scpi_result_t TestEmptyQuery(scpi_t * context) {
    SCPI_ResultCharacters(context, "", 0);
    return SCPI_RES_OK;
}

#define TEST_PARTIALERR_PAYLOAD "PAYLOAD"

static scpi_result_t TestPartialErrQuery(scpi_t * context) {
    /* Mirrors SCPI_SysInfoGet's (SCPIInterface.c) exact shape: an optional
     * parameter parser failure pushes a real SCPI error SYNCHRONOUSLY
     * (SCPI_ErrorPush() -> SCPI_ErrorEmit(), error.c, which writes the
     * error text to the wire immediately), but the callback substitutes a
     * default and keeps going -- writing a real payload straight through
     * interface->write() afterward -- then still reports success. */
    SCPI_ErrorPush(context, SCPI_ERROR_DATA_TYPE_ERROR);
    context->interface->write(context, TEST_PARTIALERR_PAYLOAD, strlen(TEST_PARTIALERR_PAYLOAD));
    return SCPI_RES_OK;
}

#define TEST_BYPASS_PAYLOAD "\r\n__END_OF_LIST__ OK"

static scpi_result_t TestBypassQuery(scpi_t * context) {
    /* Mirrors SCPIStorageSD.c's SCPI_StorageSDListDir(), AS FIXED by this
     * PR: flush any armed separator through the real funnel BEFORE the
     * bypass write starts (SCPI_StorageSDListDir does this by calling
     * interface->write(context, "", 0) itself, strictly before arming the
     * SD task), then the payload write goes straight to the wire outside
     * interface->write() (sd_card_manager_DataReadyCB() -> sd_reply_write_
     * usb/tcp, app_freertos.c, have no scpi_t* to call it with -- here,
     * straight into gCapture instead of through TestWrite()), then a
     * hand-reconciled line_open = TRUE (none of sd_card_manager.c's
     * SD_LIST_END_OK/INCOMPLETE/FAILED markers end in SCPI_LINE_ENDING,
     * exactly like TEST_BYPASS_PAYLOAD below). */
    context->interface->write(context, "", 0);
    WriteCapture * cap = (WriteCapture *) context->user_context;
    if (cap != NULL) {
        size_t n = strlen(TEST_BYPASS_PAYLOAD);
        if (cap->len + n > CAPTURE_SIZE) {
            n = CAPTURE_SIZE - cap->len;
        }
        memcpy(cap->data + cap->len, TEST_BYPASS_PAYLOAD, n);
        cap->len += n;
    }
    context->line_open = TRUE;
    return SCPI_RES_OK;
}

static const scpi_command_t gTestCommands[] = {
    {.pattern = "*OK?", .callback = TestOkQuery},
    {.pattern = "TEST:FAIL?", .callback = TestFailQuery},
    {.pattern = "TEST:FAILSET", .callback = TestFailSet},
    {.pattern = "TEST:DIRECT?", .callback = TestDirectQuery},
    {.pattern = "TEST:EMPTY?", .callback = TestEmptyQuery},
    {.pattern = "TEST:PARTIALERR?", .callback = TestPartialErrQuery},
    {.pattern = "TEST:BYPASS?", .callback = TestBypassQuery},
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

/* ==========================================================================
 * #1115 round-1 audit findings (PR #1115, https://github.com/daqifi/
 * daqifi-nyquist-firmware/pull/1115 comment 5702308898): the choke-point
 * design above is sound only if EVERY write path funnels through it, and
 * the audit found real gaps. See this fire's own report for the full
 * enumeration of write paths; these four tests pin the fix for each
 * CONFIRMED finding, and were run against the pre-fix head (dd732511a,
 * this PR's round-1 commit) to confirm each one FAILS there before the
 * fix (reported in the PR body, not carried here -- same convention the
 * #1003/#1010 tests above document in this file's own header).
 * ========================================================================== */

/* ---- finding 0: SD:LIST? bypasses the funnel entirely -------------------
 * (TEST:BYPASS? stands in for SCPI_StorageSDListDir(); see its own comment
 * above for exactly what it mirrors and why it can't be the real function
 * on a host build.) -------------------------------------------------------- */

TEST(bypass_writer_gets_its_leading_separator_and_closes_the_line) {
    NEW_TEST_CONTEXT(ctx);

    FeedLine(&ctx, "*OK?;TEST:BYPASS?");

    /* Pre-fix (no entry flush, no line_open reconciliation): the SD task's
     * bypass write never sees the armed ';', so it lands AFTER the listing
     * instead of before it, and the wire ends "...OK" with no closing CRLF
     * -- "OK\r\n__END_OF_LIST__ OK;\r\n" (matches the audit's own
     * reproduction of the real SD:LIST? corruption byte-for-byte, with
     * "OK" standing in for *OPC?'s "1"). */
    ASSERT_CAPTURE_EQ("OK;" TEST_BYPASS_PAYLOAD "\r\n");
}

TEST(bypass_writer_as_only_unit_is_unaffected) {
    NEW_TEST_CONTEXT(ctx);

    FeedLine(&ctx, "TEST:BYPASS?");

    /* No predecessor -- the entry flush is a genuine no-op (pending_delimiter
     * was never armed). Pinned so a future change can't quietly make the
     * single-command case grow a stray leading ';'. */
    ASSERT_CAPTURE_EQ(TEST_BYPASS_PAYLOAD "\r\n");
}

/* ---- findings 1 and 2: an empty successful query loses its own
 * compound-response field (same root cause, two different real callbacks --
 * SYSTem:COMMunicate:UART:READ? 0 and SYST:LOG? on an empty buffer; TEST:
 * EMPTY? exercises the one shared fix, writeData()'s real zero-length flush
 * path, directly -- no mock needed for this one). ------------------------- */

TEST(empty_successful_query_keeps_its_own_separator_slot) {
    NEW_TEST_CONTEXT(ctx);

    FeedLine(&ctx, "*OK?;TEST:EMPTY?;*OK?");

    /* Pre-fix: writeData() returned before ever calling interface->write()
     * for the len==0 result, so TEST:EMPTY?'s own armed pending_delimiter
     * was never flushed -- it stayed armed and was silently ASSIGNED over
     * (not merged with) the third unit's own freshly computed value
     * (parser.c:~152, both units happen to compute TRUE here, so the
     * overwrite is invisible in the boolean but the flush that should have
     * fired for unit 2 never did) -- "OK;OK\r\n" (one semicolon, the empty
     * middle field silently dropped) instead of "OK;;OK\r\n" (two
     * semicolons -- one per boundary between three units). */
    ASSERT_CAPTURE_EQ("OK;;OK\r\n");
}

TEST(empty_successful_query_as_last_unit_still_gets_leading_separator) {
    NEW_TEST_CONTEXT(ctx);

    FeedLine(&ctx, "*OK?;TEST:EMPTY?");

    /* Same root cause, no third unit to expose the dropped field via a
     * missing ';' -- pinned anyway so the entry-flush-only half of the fix
     * (TEST:EMPTY? as the LAST unit, no unit afterward to "inherit" a
     * correct-by-coincidence value) can't silently regress. */
    ASSERT_CAPTURE_EQ("OK;\r\n");
}

/* ---- finding 3: output after an error loses its terminator and the next
 * unit's separator (TEST:PARTIALERR? mirrors SCPI_SysInfoGet() exactly --
 * see its own comment above). ---------------------------------------------- */

TEST(payload_after_partial_error_keeps_next_units_separator) {
    NEW_TEST_CONTEXT(ctx);

    FeedLine(&ctx, "*OK?;TEST:PARTIALERR?;*OK?");

    /* Pre-fix: SCPI_ErrorEmit() (error.c) forced first_output = TRUE the
     * moment the -104 was pushed, and processCommand() (parser.c) skipped
     * the branch that would have cleared it because cmd_error was set --
     * even though the callback went on to write real payload afterward.
     * The third unit's own pending_delimiter arm then read the wrongly-TRUE
     * first_output and came up FALSE, fusing "OK" straight onto "PAYLOAD"
     * with no separator: "...PAYLOADOK\r\n" instead of "...PAYLOAD;OK\r\n". */
    ASSERT_CAPTURE_EQ("OK\r\n**ERROR: -104, \"Data type error\"\r\nPAYLOAD;OK\r\n");
}

TEST(payload_after_partial_error_still_gets_final_terminator) {
    NEW_TEST_CONTEXT(ctx);

    FeedLine(&ctx, "*OK?;TEST:PARTIALERR?");

    /* Same finding, the other symptom: with no unit after it to expose the
     * missing separator, the wrongly-TRUE first_output instead made the
     * deferred end-of-message writeNewLine() (parser.c) skip the payload's
     * own closing CRLF entirely -- pre-fix the wire ended bare
     * "...PAYLOAD", unterminated, ready to run into whatever the next
     * command's response put on the wire next. */
    ASSERT_CAPTURE_EQ("OK\r\n**ERROR: -104, \"Data type error\"\r\nPAYLOAD\r\n");
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
    RUN(bypass_writer_gets_its_leading_separator_and_closes_the_line);
    RUN(bypass_writer_as_only_unit_is_unaffected);
    RUN(empty_successful_query_keeps_its_own_separator_slot);
    RUN(empty_successful_query_as_last_unit_still_gets_leading_separator);
    RUN(payload_after_partial_error_keeps_next_units_separator);
    RUN(payload_after_partial_error_still_gets_final_terminator);
    return test_summary();
}
