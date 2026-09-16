/* ==========================================================================
 * test_1096_direct_result_termination.c
 *
 * Issue #1096 (Qodo finding on PR #1096, round 3): "Compound failures gain
 * blank lines".
 *
 * WHAT THE DEFECT WAS.  Rounds 1 and 2 of PR #1096 gave direct-writing query
 * callbacks (the ones that format their own reply and push it through
 * context->interface->write instead of calling SCPI_ResultXxx) their top-level
 * ";" separator via SCPI_PrepareDirectResult(), which deliberately does NOT
 * touch context->first_output.  first_output is libscpi's "an UNTERMINATED
 * result is pending" flag, and SCPI_ErrorEmit() (error.c) reads it through
 * scpiParser_TerminatePendingOutput() to decide whether to close the current
 * line before the transport writes its "**ERROR: ..." sentence.
 *
 * For a SINGLE-command message that was right by accident: SCPI_Parse() had
 * just set first_output TRUE, so a self-terminated direct reply followed by an
 * error got its error on a fresh line with nothing between.  In a COMPOUND
 * message it was wrong: an earlier unit's SCPI_ResultXxx() value left the flag
 * FALSE, and the flag still said "unterminated" after the direct writer had
 * already ended its own line -- so the terminator was written a SECOND time:
 *
 *     *IDN?;SYSTem:STORage:SD:SPACe?        (no card in the slot)
 *
 *     DAQiFi,Nq1,<serial>,01-02;\r\nError !! No SD Card Detected\r\n
 *     \r\n                                    <-- the spurious blank line
 *     **ERROR: -200, "Execution error"\r\n
 *
 * THE FIX, and why it needs a test that can distinguish TWO shapes.  A single
 * global rule cannot be right.  "Direct output is always terminated" glues the
 * error onto a callback that left the wire mid-line (SYSTem:INFo? writes
 * "  DIO state: " then one byte per pin, and its __stalled_exit path raises an
 * execution error from inside exactly that loop).  "Direct output is never
 * terminated" is the pre-fix state, i.e. the blank line above.  So the caller
 * now states which it wrote, via SCPI_FinishDirectResult(context, terminated)
 * immediately after each actual write.  Both shapes are exercised below.
 *
 * WHY THIS TEST LINKS THE REAL LIBSCPI.  The mechanism under test lives
 * entirely in the vendored parser.c/error.c -- writeDelimiter(),
 * writeNewLine(), scpiParser_TerminatePendingOutput(), the post-callback
 * first_output flip in processCommand(), SCPI_PrepareDirectResult() and the new
 * SCPI_FinishDirectResult().  Those sources are plain portable C with no
 * FreeRTOS/Harmony dependency and build standalone (same set of 9 files
 * test_999 links -- see the Makefile's SCPI999_BIN comment).  The firmware
 * callbacks that USE the mechanism are not includable on the host
 * (SCPIInterface.c pulls in 42 headers spanning Harmony, FreeRTOS and the whole
 * driver graph), so this file registers SYNTHETIC callbacks that reproduce each
 * real callback's write SHAPE exactly, and asserts on the resulting wire bytes
 * BYTE FOR BYTE -- not "contains", not "no double newline anywhere".
 *
 * Mapping of synthetic callback -> real callback it stands in for:
 *
 *   TEST:VALue?    / TEST:VALTwo?  -> *IDN? and friends: succeed via
 *                                     SCPI_ResultCharacters (writeDelimiter
 *                                     owns their ";", first_output FALSE after)
 *   TEST:TERMfail?                 -> SCPI_CheckSDCardPresent (SD:SPACe?,
 *                                     SD:LISt? with no card), SYSTem:INFo? with
 *                                     BoardData missing, SYSTem:POWer:BQ:
 *                                     REGisters?/DIAGnostics? with I2C down:
 *                                     write "\r\n"-terminated text, then raise
 *   TEST:RAWfail?                  -> SYSTem:INFo?'s __stalled_exit reached
 *                                     from inside the channel-list / DIO-state
 *                                     loops: write a mid-line fragment, then
 *                                     raise
 *   TEST:DIRect?   / TEST:DIRTwo?  -> SYSTem:LOG?, SD:BENCH?: direct write that
 *                                     succeeds
 *   TEST:BINary?                   -> SYSTem:SYSInfoPB?: raw Protocol Buffer
 *                                     bytes, never line-framed
 *   TEST:TWOpart?                  -> the SD CID-info query: one unterminated
 *                                     write followed by a separate "\r\n"
 *   TEST:NQWrite                   -> SYSTem:POWer:BQ:ILIM and the other
 *                                     NON-query direct writers, which take part
 *                                     in neither half of the protocol
 *   TEST:FAIL                      -> any non-query command that returns
 *                                     SCPI_RES_ERR
 *
 * TestError() mirrors SCPI_USB_Error() (UsbCdc.c:1224) and SCPI_TCP_Error()
 * (wifi_tcp_server.c): same "**ERROR: %d, \"%s\"\r\n" format, written through
 * the SAME context->interface->write the results go through, skipped entirely
 * for err == 0.  That shared write path is the whole reason the defect exists.
 *
 * Build note: USE_DEVICE_DEPENDENT_ERROR_INFORMATION is forced to 0 on the
 * command line (see the Makefile) to match the firmware target, exactly as
 * test_999 does -- on a bare host build libscpi's config.h would auto-detect
 * SYSTEM_FULL_BLOWN via __unix__, changing sizeof(scpi_error_t) and pulling in
 * a malloc/free path the firmware never takes.
 *
 * Run with -v to print every scenario's wire bytes (escaped) rather than only
 * the mismatching ones.
 * ========================================================================== */
#include "test_framework.h"

#include <stdio.h>
#include <string.h>

#include "scpi/scpi.h"

/* ---- wire capture ------------------------------------------------------- */

#define WIRE_SIZE 1024

static char   gWire[WIRE_SIZE];
static size_t gWireLen;
static int    gVerbose;
static const char * gScenario = "";

static size_t TestWrite(scpi_t * context, const char * data, size_t len) {
    (void) context;
    size_t n = len;
    if (gWireLen + n > WIRE_SIZE) {
        n = WIRE_SIZE - gWireLen;
    }
    memcpy(gWire + gWireLen, data, n);
    gWireLen += n;
    /* Report the full length: these tests are about line structure, not about
     * the short-write path (SysInfoText_Write's own `whole` guard covers that
     * and is asserted separately by test_947). */
    return len;
}

/* Mirrors SCPI_USB_Error (UsbCdc.c:1224) / SCPI_TCP_Error: same format, same
 * interface->write, same "nothing at all for err == 0". */
static int TestError(scpi_t * context, int_fast16_t err) {
    char line[128];
    if (err != 0) {
        const char * text = SCPI_ErrorTranslate((int16_t) err);
        if (text == NULL) {
            text = "Unknown";
        }
        snprintf(line, sizeof(line), "**ERROR: %d, \"%s\"\r\n", (int32_t) err, text);
        context->interface->write(context, line, strlen(line));
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

/* ---- synthetic callbacks: one per real write SHAPE ---------------------- */

/* Stands in for *IDN? -- a query that produces its value through the
 * SCPI_ResultXxx family, so writeDelimiter() owns its separator and leaves
 * first_output FALSE ("a value is pending"). This is the unit whose leftover
 * state made the compound case differ from the single-command case. */
static scpi_result_t Cb_Value(scpi_t * context) {
    SCPI_ResultCharacters(context, "VAL", 3);
    return SCPI_RES_OK;
}

static scpi_result_t Cb_ValueTwo(scpi_t * context) {
    SCPI_ResultCharacters(context, "VAL2", 4);
    return SCPI_RES_OK;
}

/* SHAPE 1 -- the defect. Write "\r\n"-terminated text directly, then raise.
 * Byte-for-byte the shape of SCPI_CheckSDCardPresent (SCPIStorageSD.c): the
 * message even begins with a "\r\n" of its own, exactly as
 * SD_CARD_NOT_PRESENT_ERROR_MSG does. */
static scpi_result_t Cb_TerminatedThenFail(scpi_t * context) {
    static const char msg[] = "\r\nno media\r\n";
    SCPI_PrepareDirectResult(context);
    context->interface->write(context, msg, sizeof(msg) - 1);
    SCPI_FinishDirectResult(context, TRUE);
    SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
    return SCPI_RES_ERR;
}

/* SHAPE 2 -- the residual round 2 left open. Write a mid-line fragment, then
 * raise. Real instance: SYSTem:INFo?'s "  DIO state: " / per-pin digits,
 * followed by __stalled_exit's SCPI_ExecutionError. */
static scpi_result_t Cb_UnterminatedThenFail(scpi_t * context) {
    static const char msg[] = "partial";
    SCPI_PrepareDirectResult(context);
    context->interface->write(context, msg, sizeof(msg) - 1);
    SCPI_FinishDirectResult(context, FALSE);
    SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
    return SCPI_RES_ERR;
}

/* A direct writer that SUCCEEDS -- SYSTem:LOG?, SD:BENCH?. Round 1/round 2
 * regression material: these must keep their ";" and their trailing newline. */
static scpi_result_t Cb_Direct(scpi_t * context) {
    static const char msg[] = "DIR\r\n";
    SCPI_PrepareDirectResult(context);
    context->interface->write(context, msg, sizeof(msg) - 1);
    SCPI_FinishDirectResult(context, TRUE);
    return SCPI_RES_OK;
}

static scpi_result_t Cb_DirectTwo(scpi_t * context) {
    static const char msg[] = "DIR2\r\n";
    SCPI_PrepareDirectResult(context);
    context->interface->write(context, msg, sizeof(msg) - 1);
    SCPI_FinishDirectResult(context, TRUE);
    return SCPI_RES_OK;
}

/* SYSTem:SYSInfoPB? -- raw Protocol Buffer bytes. Not line-framed in any sense,
 * so it reports FALSE and relies on processCommand()'s post-callback flip plus
 * SCPI_Parse()'s closing writeNewLine() for its terminator, exactly as it did
 * before #1096. The payload deliberately contains no printable text. */
static scpi_result_t Cb_Binary(scpi_t * context) {
    static const char pb[] = { (char) 0x0A, (char) 0x02, (char) 0x08, (char) 0x01 };
    SCPI_PrepareDirectResult(context);
    context->interface->write(context, pb, sizeof(pb));
    SCPI_FinishDirectResult(context, FALSE);
    return SCPI_RES_OK;
}

/* The SD CID-info query: an unterminated field list, then a separate write of
 * the line ending. The one real call site that needs BOTH answers, and the
 * reason the state is per-WRITE rather than per-callback. */
static scpi_result_t Cb_TwoPart(scpi_t * context) {
    static const char body[] = "3,\"SD\",\"SU02G\"";
    SCPI_PrepareDirectResult(context);
    context->interface->write(context, body, sizeof(body) - 1);
    SCPI_FinishDirectResult(context, FALSE);
    context->interface->write(context, "\r\n", 2);
    SCPI_FinishDirectResult(context, TRUE);
    return SCPI_RES_OK;
}

/* A NON-query command that direct-writes (SYSTem:POWer:BQ:ILIM and friends).
 * Both helpers filter it out, so its wire shape is whatever it was before
 * #1003 -- no ";" and no line-state bookkeeping. Pinned here because removing
 * either filter changes it. */
static scpi_result_t Cb_NonQueryWrite(scpi_t * context) {
    static const char msg[] = "nq\r\n";
    SCPI_PrepareDirectResult(context);
    context->interface->write(context, msg, sizeof(msg) - 1);
    SCPI_FinishDirectResult(context, TRUE);
    return SCPI_RES_OK;
}

static scpi_result_t Cb_Fail(scpi_t * context) {
    (void) context;
    return SCPI_RES_ERR;
}

static const scpi_command_t gTestCommands[] = {
    /* The real *IDN?, straight out of the linked ieee488.c, so the headline
     * reproduction below is the message the issue actually reports rather than
     * a paraphrase of it. Its four SCPI_ResultMnemonic() calls render the idn
     * strings InitContext passes: "TEST,1096,0,00-00". */
    {.pattern = "*IDN?",         .callback = SCPI_CoreIdnQ},
    {.pattern = "TEST:VALue?",   .callback = Cb_Value},
    {.pattern = "TEST:VALTwo?",  .callback = Cb_ValueTwo},
    {.pattern = "TEST:TERMfail?", .callback = Cb_TerminatedThenFail},
    {.pattern = "TEST:RAWfail?", .callback = Cb_UnterminatedThenFail},
    {.pattern = "TEST:DIRect?",  .callback = Cb_Direct},
    {.pattern = "TEST:DIRTwo?",  .callback = Cb_DirectTwo},
    {.pattern = "TEST:BINary?",  .callback = Cb_Binary},
    {.pattern = "TEST:TWOpart?", .callback = Cb_TwoPart},
    {.pattern = "TEST:NQWrite",  .callback = Cb_NonQueryWrite},
    {.pattern = "TEST:FAIL",     .callback = Cb_Fail},
    SCPI_CMD_LIST_END,
};

static const scpi_unit_def_t gTestUnits[] = {
    SCPI_UNITS_LIST_END,
};

/* ---- harness ------------------------------------------------------------ */

static char         gInputBuffer[512];
static scpi_error_t gErrorQueue[17];

/* Mirrors CreateSCPIContext()'s SCPI_Init() call shape (SCPIInterface.c); the
 * real function is not includable on the host -- see test_999's header.
 *
 * ⚠️ On the message strings below: IEEE 488.2 compound paths are RELATIVE.
 * composeCompoundCommand() (utils.c:678) prepends the previous unit's path to
 * any unit that does not start with ':' or '*', so "TEST:VALue?;:TEST:DIRect?"
 * asks for "TEST:TEST:DIRect?" and is rejected as an undefined header before
 * processCommand ever runs. That is correct SCPI and nothing to do with this
 * issue, but it means every second-and-later TEST:* unit here carries an
 * explicit leading ':'. The reported reproduction does not need one because its
 * first unit is the common command *IDN?, which resets the path -- and that
 * exact shape is asserted below too. */
static void RunMessage(const char * label, scpi_t * context, const char * message) {
    char line[256];
    size_t n = strlen(message);

    gScenario = label;
    gWireLen = 0;
    memset(gWire, 0, sizeof(gWire));

    SCPI_Init(context, gTestCommands, &gTestInterface, gTestUnits,
              "TEST", "1096", "0", "00-00",
              gInputBuffer, sizeof(gInputBuffer),
              gErrorQueue, 17);

    /* Real commands arrive newline-terminated in one SCPI_Input() call
     * (microrl appends ENDL before calling execute), so match that shape. */
    memcpy(line, message, n);
    line[n] = '\n';
    SCPI_Input(context, line, (int) (n + 1));
}

/* Escape CR/LF and non-printables so a mismatch is readable in the log. */
static void Escape(const char * data, size_t len, char * out, size_t outSize) {
    size_t o = 0;
    /* 16 is the widest any single branch below can emit (the "\n" branch
     * writes 2 + 9), so the bound is checked against that, not against 5. */
    for (size_t i = 0; i < len && o + 16 < outSize; i++) {
        unsigned char c = (unsigned char) data[i];
        if (c == '\r')      { out[o++] = '\\'; out[o++] = 'r'; }
        else if (c == '\n') { out[o++] = '\\'; out[o++] = 'n'; o += (size_t) snprintf(out + o, outSize - o, "\n        "); }
        else if (c >= 0x20 && c < 0x7F) { out[o++] = (char) c; }
        else { o += (size_t) snprintf(out + o, outSize - o, "\\x%02X", c); }
    }
    out[o < outSize ? o : outSize - 1] = '\0';
}

static void DumpWire(const char * why, const char * data, size_t len) {
    char esc[4096];
    Escape(data, len, esc, sizeof(esc));
    printf("      %-8s %s [%u B]\n        %s\n", why, gScenario, (unsigned) len, esc);
}

/* Byte-for-byte compare of the whole captured message against `expected`.
 * Returns 1 on match; on mismatch prints both, escaped, and returns 0. */
static int WireIs(const char * expected) {
    size_t expLen = strlen(expected);
    if (gWireLen == expLen && memcmp(gWire, expected, expLen) == 0) {
        if (gVerbose) {
            DumpWire("wire", gWire, gWireLen);
        }
        return 1;
    }
    printf("      MISMATCH in scenario: %s\n", gScenario);
    DumpWire("expected", expected, expLen);
    DumpWire("actual", gWire, gWireLen);
    return 0;
}

/* ======================================================================== *
 * (a) REGRESSION BASELINE -- single command, terminated direct write then an
 *     error. This shape was already correct before #1096 (SCPI_Parse had just
 *     set first_output TRUE, so nothing was "pending"), and the fix must leave
 *     it byte-identical. If a future change makes the parser assume rather than
 *     ask, this is the assertion that notices.
 * ======================================================================== */
TEST(single_terminated_direct_then_error_has_no_blank_line) {
    scpi_t ctx;
    RunMessage("single TEST:TERMfail?", &ctx, "TEST:TERMfail?");
    ASSERT_TRUE(WireIs("\r\nno media\r\n**ERROR: -200, \"Execution error\"\r\n"));
}

/* ======================================================================== *
 * (b) THE BUG. Same callback, but preceded by a successful SCPI_ResultXxx
 *     query. Before the fix the earlier unit's pending-value state survived the
 *     direct write and scpiParser_TerminatePendingOutput() emitted a second
 *     line ending -- the blank line Qodo reported. Expect exactly one "\r\n"
 *     between the message and "**ERROR".
 * ======================================================================== */
TEST(compound_terminated_direct_then_error_has_no_blank_line) {
    scpi_t ctx;
    RunMessage("compound TEST:VALue?;:TEST:TERMfail?", &ctx,
               "TEST:VALue?;:TEST:TERMfail?");
    ASSERT_TRUE(WireIs("VAL;\r\nno media\r\n**ERROR: -200, \"Execution error\"\r\n"));
}

/* The reported reproduction, spelled the way the issue spells it: the REAL
 * *IDN? (ieee488.c, linked here) followed by a direct-write query that prints a
 * terminated message and raises. No leading ':' on the second unit because a
 * common command resets the compound path -- which is precisely why the issue
 * reproduces with this shape and not with "SYST:A?;SYST:B?". */
TEST(idn_then_terminated_direct_then_error_has_no_blank_line) {
    scpi_t ctx;
    RunMessage("compound *IDN?;TEST:TERMfail?", &ctx, "*IDN?;TEST:TERMfail?");
    ASSERT_TRUE(WireIs("TEST,1096,0,00-00;\r\nno media\r\n"
                       "**ERROR: -200, \"Execution error\"\r\n"));
}

/* A third unit after the failing one still gets its value out, on its own line
 * (the error closed the previous one, so no ";" is due). Guards against a fix
 * that suppressed the blank line by leaving the flag in a state the NEXT unit
 * then mis-reads. */
TEST(compound_terminated_direct_then_error_then_another_query) {
    scpi_t ctx;
    RunMessage("compound TEST:VALue?;:TEST:TERMfail?;:TEST:VALTwo?", &ctx,
               "TEST:VALue?;:TEST:TERMfail?;:TEST:VALTwo?");
    ASSERT_TRUE(WireIs("VAL;\r\nno media\r\n"
                       "**ERROR: -200, \"Execution error\"\r\n"
                       "VAL2\r\n"));
}

/* ======================================================================== *
 * (c) SHAPE 2 -- an UNTERMINATED direct write followed by an error. The error
 *     must be on its own line: exactly ONE line ending between the fragment and
 *     "**ERROR", never zero (glued, which is what main does today) and never
 *     two (the blank line (b) is about). Both positions.
 * ======================================================================== */
TEST(single_unterminated_direct_then_error_gets_exactly_one_newline) {
    scpi_t ctx;
    RunMessage("single TEST:RAWfail?", &ctx, "TEST:RAWfail?");
    ASSERT_TRUE(WireIs("partial\r\n**ERROR: -200, \"Execution error\"\r\n"));
}

TEST(compound_unterminated_direct_then_error_gets_exactly_one_newline) {
    scpi_t ctx;
    RunMessage("compound TEST:VALue?;:TEST:RAWfail?", &ctx,
               "TEST:VALue?;:TEST:RAWfail?");
    ASSERT_TRUE(WireIs("VAL;partial\r\n**ERROR: -200, \"Execution error\"\r\n"));
}

/* ======================================================================== *
 * (d) ROUND-1 / ROUND-2 REGRESSION SET. Every one of these must be unchanged
 *     by #1096; they are what rounds 1 and 2 of this PR established.
 * ======================================================================== */

/* Two SCPI_ResultXxx queries: writeDelimiter owns the ";". */
TEST(regression_two_result_queries_are_semicolon_separated) {
    scpi_t ctx;
    RunMessage("compound TEST:VALue?;:TEST:VALTwo?", &ctx,
               "TEST:VALue?;:TEST:VALTwo?");
    ASSERT_TRUE(WireIs("VAL;VAL2\r\n"));
}

/* A SCPI_ResultXxx query then a successful DIRECT writer ("*IDN?;SYST:LOG?"):
 * the ";" comes from SCPI_PrepareDirectResult, and the trailing line ending
 * from SCPI_Parse()'s closing writeNewLine() via the post-callback flip. */
TEST(regression_result_query_then_direct_query_is_semicolon_separated) {
    scpi_t ctx;
    RunMessage("compound TEST:VALue?;:TEST:DIRect?", &ctx,
               "TEST:VALue?;:TEST:DIRect?");
    ASSERT_TRUE(WireIs("VAL;DIR\r\n\r\n"));
}

/* Two consecutive direct writers. */
TEST(regression_two_direct_queries_are_semicolon_separated) {
    scpi_t ctx;
    RunMessage("compound TEST:DIRect?;:TEST:DIRTwo?", &ctx,
               "TEST:DIRect?;:TEST:DIRTwo?");
    ASSERT_TRUE(WireIs("DIR\r\n;DIR2\r\n\r\n"));
}

/* A single direct writer, for the trailing-newline baseline the two above
 * depend on. */
TEST(regression_single_direct_query_keeps_its_trailing_newline) {
    scpi_t ctx;
    RunMessage("single TEST:DIRect?", &ctx, "TEST:DIRect?");
    ASSERT_TRUE(WireIs("DIR\r\n\r\n"));
}

/* A failing NON-query after a successful query: the pending value must be
 * terminated before the error line (round 1's original case). */
TEST(regression_failing_nonquery_after_query_terminates_the_value) {
    scpi_t ctx;
    RunMessage("compound TEST:VALue?;:TEST:FAIL", &ctx, "TEST:VALue?;:TEST:FAIL");
    ASSERT_TRUE(WireIs("VAL\r\n**ERROR: -200, \"Execution error\"\r\n"));
}

/* An UNDEFINED header after a successful query -- the sibling path, rejected by
 * SCPI_Parse before processCommand ever runs (round 1's second case). */
TEST(regression_undefined_header_after_query_terminates_the_value) {
    scpi_t ctx;
    RunMessage("compound TEST:VALue?;NOSUCH:HEADER?", &ctx,
               "TEST:VALue?;NOSUCH:HEADER?");
    ASSERT_TRUE(WireIs("VAL\r\n**ERROR: -113, \"Undefined header\"\r\n"));
}

/* ======================================================================== *
 * (e) NON-CRLF-FRAMED OUTPUT -- SYSTem:SYSInfoPB?. Reporting FALSE keeps
 *     processCommand()'s post-callback flip and SCPI_Parse()'s closing
 *     writeNewLine() doing exactly what they did before #1096: supplying the
 *     terminator this reply has never carried itself. Both positions.
 * ======================================================================== */
TEST(binary_direct_query_still_gets_its_terminator) {
    scpi_t ctx;
    RunMessage("single TEST:BINary?", &ctx, "TEST:BINary?");
    ASSERT_TRUE(WireIs("\x0A\x02\x08\x01\r\n"));
}

TEST(compound_binary_direct_query_still_gets_its_terminator) {
    scpi_t ctx;
    RunMessage("compound TEST:VALue?;:TEST:BINary?", &ctx,
               "TEST:VALue?;:TEST:BINary?");
    ASSERT_TRUE(WireIs("VAL;\x0A\x02\x08\x01\r\n"));
}

/* ======================================================================== *
 * (f) TWO WRITES, TWO ANSWERS -- the SD CID-info query's shape. The state is
 *     per-write: FALSE between the field list and the line ending, TRUE after.
 * ======================================================================== */
TEST(two_part_direct_query_ends_terminated) {
    scpi_t ctx;
    RunMessage("single TEST:TWOpart?", &ctx, "TEST:TWOpart?");
    ASSERT_TRUE(WireIs("3,\"SD\",\"SU02G\"\r\n\r\n"));
}

TEST(compound_two_part_direct_query_ends_terminated) {
    scpi_t ctx;
    RunMessage("compound TEST:VALue?;:TEST:TWOpart?", &ctx,
               "TEST:VALue?;:TEST:TWOpart?");
    ASSERT_TRUE(WireIs("VAL;3,\"SD\",\"SU02G\"\r\n\r\n"));
}

/* ======================================================================== *
 * (g) NON-QUERY DIRECT WRITERS are filtered out of BOTH halves of the
 *     protocol, exactly as they were before #1003: no ";" and no line-state
 *     bookkeeping, so SCPI_Parse()'s closing newline still fires. Pinned
 *     because dropping the query filter from SCPI_FinishDirectResult silently
 *     removes that newline.
 * ======================================================================== */
TEST(nonquery_direct_write_is_unaffected_by_the_protocol) {
    scpi_t ctx;
    RunMessage("compound TEST:VALue?;:TEST:NQWrite", &ctx,
               "TEST:VALue?;:TEST:NQWrite");
    ASSERT_TRUE(WireIs("VALnq\r\n\r\n"));
}

/* ======================================================================== *
 * (h) The pure predicate SCPI_DirectResultEndsLine() -- the answer the generic
 *     writers (scpi_printf, SysInfoText_Write) hand to SCPI_FinishDirectResult.
 * ======================================================================== */
TEST(ends_line_predicate) {
    ASSERT_TRUE(SCPI_DirectResultEndsLine("x\r\n", 3));
    ASSERT_TRUE(SCPI_DirectResultEndsLine("\r\n", 2));
    ASSERT_FALSE(SCPI_DirectResultEndsLine("x\n", 2));   /* bare LF is not it */
    ASSERT_FALSE(SCPI_DirectResultEndsLine("x\r", 2));
    ASSERT_FALSE(SCPI_DirectResultEndsLine("\r\nx", 3)); /* leading, not trailing */
    ASSERT_FALSE(SCPI_DirectResultEndsLine("", 0));
    ASSERT_FALSE(SCPI_DirectResultEndsLine(NULL, 8));
}

int main(int argc, char ** argv) {
    gVerbose = (argc > 1 && strcmp(argv[1], "-v") == 0);

    RUN(single_terminated_direct_then_error_has_no_blank_line);
    RUN(compound_terminated_direct_then_error_has_no_blank_line);
    RUN(idn_then_terminated_direct_then_error_has_no_blank_line);
    RUN(compound_terminated_direct_then_error_then_another_query);
    RUN(single_unterminated_direct_then_error_gets_exactly_one_newline);
    RUN(compound_unterminated_direct_then_error_gets_exactly_one_newline);
    RUN(regression_two_result_queries_are_semicolon_separated);
    RUN(regression_result_query_then_direct_query_is_semicolon_separated);
    RUN(regression_two_direct_queries_are_semicolon_separated);
    RUN(regression_single_direct_query_keeps_its_trailing_newline);
    RUN(regression_failing_nonquery_after_query_terminates_the_value);
    RUN(regression_undefined_header_after_query_terminates_the_value);
    RUN(binary_direct_query_still_gets_its_terminator);
    RUN(compound_binary_direct_query_still_gets_its_terminator);
    RUN(two_part_direct_query_ends_terminated);
    RUN(compound_two_part_direct_query_ends_terminated);
    RUN(nonquery_direct_write_is_unaffected_by_the_protocol);
    RUN(ends_line_predicate);

    return TEST_SUMMARY();
}
