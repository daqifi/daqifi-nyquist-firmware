/*-
 * BSD 2-Clause License
 *
 * Copyright (c) 2012-2018, Jan Breuer
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file   scpi_parser.c
 * @date   Thu Nov 15 10:58:45 UTC 2012
 *
 * @brief  SCPI parser implementation
 *
 *
 */

#include <ctype.h>
#include <string.h>

#include "scpi/config.h"
#include "scpi/parser.h"
#include "parser_private.h"
#include "lexer_private.h"
#include "scpi/error.h"
#include "scpi/constants.h"
#include "scpi/utils.h"

/**
 * Write data to SCPI output
 * @param context
 * @param data
 * @param len - length of data to be written
 * @return number of bytes written
 */
static size_t writeData(scpi_t * context, const char * data, size_t len) {
    if ((len > 0) && (data != NULL)) {
        return context->interface->write(context, data, len);
    } else {
        return 0;
    }
}

/**
 * Flush data to SCPI output
 * @param context
 * @return
 */
static int flushData(scpi_t * context) {
    if (context && context->interface && context->interface->flush) {
        return context->interface->flush(context);
    } else {
        return SCPI_RES_OK;
    }
}

/**
 * Write result delimiter to output
 * @param context
 * @return number of bytes written
 *
 * DAQiFi patch (issues #1003 / #1010) -- this function now owns BOTH result
 * separators, not just the intra-command ",".
 *
 * The separator used to be written at the top of processCommand(), BEFORE the
 * command's callback ran -- here as "!first_output && is_query", in pristine
 * upstream as writeSemicolon()'s "output_count > 0"; either way it is a
 * speculative write.  If the callback then fails and produces no value at all,
 * the ";" is already on the wire and ends up introducing the transports'
 * "**ERROR: ..." line instead of a value --
 *
 *     *IDN?;SYST:COMM:LAN:DNS2?
 *     -> DAQiFi,Nq1,<serial>,01-02;**ERROR: -200, "Execution error"
 *
 * -- so a client splitting the compound reply on ";" reads an error sentence
 * where it expected a field.  Worse, the sibling shape (an UNDEFINED header,
 * which SCPI_Parse rejects without ever entering processCommand) emitted no
 * separator at all, so the same class had two different spellings on the wire.
 *
 * output_count is reset to 0 at the top of every processCommand() and
 * incremented once per SCPI_ResultXxx() call, so at this point
 * "output_count == 0" means exactly "this command has not produced a value
 * yet" -- which makes this the one place that can emit the ";" at the moment a
 * value ACTUALLY appears rather than in the hope that one will.  first_output
 * is cleared here too, for the same reason: the flag must mean "something
 * unterminated is on the wire", which is what SCPI_ErrorEmit() (error.c) now
 * tests before writing an error line.
 */
static size_t writeDelimiter(scpi_t * context) {
    if (context->output_count > 0) {
        /* 2nd+ value of THIS command */
        return writeData(context, ",", 1);
    }

    /* First value of this command. If an earlier unit of the same program
     * message already wrote a value, this one has to be separated from it. */
    if (!context->first_output) {
        return writeData(context, ";", 1);
    }

    context->first_output = FALSE;
    return 0;
}

/**
 * Conditionaly write "New Line"
 * @param context
 * @return number of characters written
 */
static size_t writeNewLine(scpi_t * context) {
    if (!context->first_output) {
        size_t len;
#ifndef SCPI_LINE_ENDING
#error no termination character defined
#endif
        len = writeData(context, SCPI_LINE_ENDING, strlen(SCPI_LINE_ENDING));
        flushData(context);
        /* DAQiFi patch (issues #1003 / #1010): nothing is pending once the
         * line ending is out.  Upstream left first_output FALSE here, so it
         * stayed FALSE after any program message that produced output -- and
         * SCPI_ErrorPush() reached from OUTSIDE a parse (SCPI_Input()'s
         * input-buffer-overrun push below, SCPI_ERROR_INPUT_BUFFER_OVERRUN,
         * currently parser.c:427) would then see
         * a "pending result" that had already been terminated and prepend a
         * spurious blank line to its error.  SCPI_Parse() re-arms the flag on
         * entry, so upstream never depended on the value left behind. */
        context->first_output = TRUE;
        return len;
    } else {
        return 0;
    }
}

/**
 * DAQiFi patch (issues #1003 / #1010) -- terminate a result that is on the
 * wire but not yet newline-terminated.
 *
 * Called from SCPI_ErrorEmit() (error.c) immediately before the transports
 * write their "**ERROR: %d, \"%s\"\r\n" line.  Both SCPI_USB_Error()
 * (UsbCdc.c) and SCPI_TCP_Error() (wifi_tcp_server.c) push that text through
 * the SAME context->interface->write() that carries query results, and
 * SCPI_ErrorPushEx() calls them SYNCHRONOUSLY, mid-message -- so without this
 * the error text lands inside the preceding query's still-open response
 * ("...,01-02**ERROR: -113, ...").
 *
 * This is the choke point for every error path, not just the two in
 * processCommand(): an undefined header (SCPI_Parse), a callback that returns
 * SCPI_RES_ERR, and any firmware callback that calls SCPI_ErrorPush() during
 * argument validation all funnel through SCPI_ErrorEmit().
 *
 * It lives in parser.c rather than being open-coded in error.c so the line
 * ending, the flush, and the first_output bookkeeping stay in exactly one
 * place (writeNewLine); error.c has no visibility of any of the three.
 *
 * @param context
 */
void scpiParser_TerminatePendingOutput(scpi_t * context) {
    if (context != NULL) {
        /* writeNewLine() is itself conditional on first_output and re-arms it,
         * so this is a no-op when nothing is pending -- which is what keeps
         * the single-command error case byte-identical to before the patch. */
        writeNewLine(context);
    }
}

/**
 * DAQiFi patch (issues #1003 / #1010, extracted in #1096 round 3) -- is the
 * program-message unit currently being processed a QUERY that takes part in the
 * direct-result protocol?
 *
 * Extracted verbatim from SCPI_PrepareDirectResult()'s first two guards so that
 * SCPI_FinishDirectResult() below cannot drift away from them.  The two
 * functions are a matched pair -- one claims the separator before a direct
 * write, the other records what that write left on the wire -- and they must
 * agree EXACTLY on which units they apply to, or a callback gets its ";" from
 * one and its line-state bookkeeping from neither.  Behaviour of
 * SCPI_PrepareDirectResult() is unchanged; only the text moved.
 */
static scpi_bool_t directResultUnitIsQuery(scpi_t * context) {
    /* Not inside a program-message unit (or a context that has never parsed):
     * cmd_raw is zeroed by SCPI_Init's memset and set by SCPI_Parse. */
    if ((context->param_list.cmd_raw.data == NULL)
            || (context->param_list.cmd_raw.length == 0)) {
        return FALSE;
    }

    /* Same test processCommand() makes: only a query takes part in the ";"
     * protocol. */
    if (context->param_list.cmd_raw.data[context->param_list.cmd_raw.length - 1]
            != '?') {
        return FALSE;
    }

    return TRUE;
}

/**
 * DAQiFi patch (issues #1003 / #1010) -- claim this query's result separator on
 * behalf of a callback that writes its own reply.
 *
 * WHY THIS EXISTS.  writeDelimiter() above is the only place the top-level ";"
 * is now written, and it runs only from the SCPI_ResultXxx() family.  A large
 * minority of this firmware's registered QUERY callbacks never call
 * SCPI_ResultXxx(): they format their reply themselves and push it through
 * context->interface->write() directly (SYSTem:INFo?, SYSTem:LOG?,
 * SYSTem:STReam:STATS?, CONFigure:CAPabilities:JSON?, ... -- see the call sites
 * listed in docs/BUILD_AND_TOOLCHAIN.md).  Before this patch series those
 * replies got their ";" from the speculative write at the top of
 * processCommand(), which fired for EVERY query regardless of how the callback
 * produced output; deleting that write (the #1003 fix) therefore left them with
 * no separator at all, so "*IDN?;SYST:LOG?" -- two commands that both SUCCEED
 * -- ran together on the wire.  This function gives those callbacks the same
 * separator, on the same terms, without the speculation: they call it
 * immediately before their FIRST actual write, so the ";" still only appears
 * when bytes really follow it.
 *
 * CONTRACT for callers.  Call it immediately before the first byte you write.
 * Calling it earlier re-creates exactly the #1003 defect (a ";" introducing an
 * error line instead of a value) if the callback then fails without writing.
 * It is safe to call more than once -- the second and later calls in the same
 * program-message unit are no-ops -- so a callback with several write sites may
 * simply call it at each of them rather than reasoning about which one runs
 * first.  It is equally safe to call from a helper shared with NON-query
 * commands: those are filtered out here (see below).
 *
 * WHY output_count IS THE IDEMPOTENCE MARKER.  processCommand() resets
 * output_count to 0 before each unit and every SCPI_ResultXxx() increments it,
 * so "> 0" already means exactly "this unit has produced a value"; taking it to
 * 1 here says the same thing about a value this unit wrote itself.  Nothing
 * else in libscpi reads output_count (only writeDelimiter, and writeSemicolon
 * below, which the DAQiFi fork does not call).  The visible consequence is that
 * a callback which direct-writes AND then calls SCPI_ResultXxx() gets a ","
 * between the two -- the correct SCPI separator for two values of one command.
 * No registered callback does that today (every mixed one -- SYSTem:LOG:LEVel?,
 * SYSTem:LOG:CMDHistory?, SYSTem:STORage:SD:SPACe? -- picks one path or the
 * other on mutually exclusive branches), so no reply changes shape because of
 * it.
 *
 * WHY IT DOES NOT TOUCH first_output, although writeDelimiter() does.  This
 * function writes the separator and NOTHING else; who owns first_output is left
 * exactly where upstream and processCommand() put it (the post-callback flip on
 * a SUCCESSFUL query).  That asymmetry is deliberate and it is load-bearing.
 * first_output FALSE means "an UNTERMINATED result is on the wire", which is
 * what SCPI_ErrorEmit() and the closing writeNewLine() both act on -- and a
 * direct writer's text, unlike a SCPI_ResultXxx() value, is usually
 * SELF-terminated ("...\r\n" from scpi_printf, from the SD "no card" message,
 * from SYSTem:INFo?'s sections).  Clearing the flag here would therefore claim
 * a pending terminator that is not pending, and every direct-write query that
 * writes text and THEN fails -- SYSTem:INFo? with no BoardData,
 * SYSTem:POWer:BQ:REGisters? with the I2C bus down, SYSTem:STORage:SD:SPACe?
 * with no card -- would have a BLANK LINE inserted between its text and its
 * "**ERROR" line by scpiParser_TerminatePendingOutput().  That is defect (C) of
 * this very issue, re-created by its own fix, and it is exactly the case
 * processCommand()'s flip deliberately does not cover: it runs only when the
 * callback returned SCPI_RES_OK.  Leaving the flag alone keeps every
 * single-command reply byte-identical to origin/main.
 *
 * SUPERSEDED IN PART by SCPI_FinishDirectResult() below (issue #1096 round 3).
 * The paragraph above is why this function must not GUESS at first_output; it
 * is not an argument for never setting it.  The caller knows what it wrote, so
 * it now says so explicitly after the write, and the two shapes -- terminated
 * and unterminated -- are handled separately instead of by one global
 * assumption.  What is written here still holds: THIS function writes the
 * separator and nothing else, and never touches first_output.
 *
 * The residual named here -- a direct writer that emits UNTERMINATED text and
 * then fails would have its error glued on -- is what SCPI_FinishDirectResult()
 * closes.  It was NOT hypothetical: SYSTem:INFo?'s channel-list and DIO-state
 * loops write mid-line fragments ("  DIO state: ", "1", ",") and its
 * __stalled_exit path raises an execution error straight after one of them.
 *
 * WHY THE QUERY TEST.  The deleted speculative write was gated on is_query, and
 * so is this, using the same expression over the same bytes.  It matters
 * because several firmware helpers that direct-write are shared between queries
 * and plain commands (scpi_printf(), SCPI_CheckSDCardPresent()), and because a
 * dozen NON-query commands emit human-readable reports through
 * context->interface->write (SYSTem:STORage:SD:GET, SYSTem:POWer:BQ:ILIM,
 * HELP, ...).  Those have never been ";"-separated, before or after #1003;
 * giving them one here would be an unrelated wire change.  Filtering inside
 * this function -- rather than at each call site -- is what makes the helper
 * safe to drop into a shared writer.
 *
 * @param context
 */
void SCPI_PrepareDirectResult(scpi_t * context) {
    if (context == NULL) {
        return;
    }

    if (!directResultUnitIsQuery(context)) {
        return;
    }

    /* Already separated -- this unit has produced a value. */
    if (context->output_count > 0) {
        return;
    }

    /* The ";" is needed iff an earlier unit of the same program message already
     * wrote a value. Same condition as writeDelimiter's output_count == 0 arm,
     * open-coded rather than delegated because that arm also assigns
     * first_output, which this function must not -- see the note above. */
    if (!context->first_output) {
        writeData(context, ";", 1);
    }
    context->output_count = 1;
}

/**
 * DAQiFi patch (issue #1096 Qodo finding, round 3) -- does this exact run of
 * bytes end at a line boundary?
 *
 * A convenience for the direct writers whose termination is a property of the
 * BYTES rather than of the call site: scpi_printf() renders whatever format
 * string its ~200 callers hand it (CONFigure:CAPabilities:JSON? deliberately
 * emits unterminated JSON fragments -- "\"channels\":[", "," -- while
 * SYSTem:STReam:STATS? emits one "Key=value\r\n" line per call), and
 * SysInfoText_Write (SCPIInterface.c) is one funnel for ~90 chunks of which
 * some are whole lines and some are mid-line fragments.  Those call sites
 * cannot answer SCPI_FinishDirectResult()'s question with a literal, but they
 * CAN answer it exactly, about the bytes they actually put on the wire -- which
 * is still the caller stating what it wrote, not the parser guessing.
 *
 * It lives here so the comparison is made against SCPI_LINE_ENDING, the same
 * macro writeNewLine() emits, rather than against a "\r\n" hard-coded five
 * times over in firmware/src/services/SCPI/.
 *
 * It takes no scpi_t deliberately: it is a pure predicate over bytes, with no
 * business reading or writing parser state.
 *
 * @param data start of the bytes that were written
 * @param len number of bytes that were written
 * @return TRUE iff the last strlen(SCPI_LINE_ENDING) bytes are that ending
 */
scpi_bool_t SCPI_DirectResultEndsLine(const char * data, size_t len) {
    size_t endingLen;

    if (data == NULL) {
        return FALSE;
    }

#ifndef SCPI_LINE_ENDING
#error no termination character defined
#endif
    endingLen = strlen(SCPI_LINE_ENDING);
    if ((endingLen == 0) || (len < endingLen)) {
        return FALSE;
    }

    return (memcmp(&data[len - endingLen], SCPI_LINE_ENDING, endingLen) == 0)
            ? TRUE : FALSE;
}

/**
 * DAQiFi patch (issue #1096 Qodo finding, round 3) -- record what a direct
 * write left on the wire, so the next error line lands correctly.
 *
 * THE DEFECT THIS CLOSES.  first_output FALSE means "an UNTERMINATED result is
 * on the wire"; SCPI_ErrorEmit() (error.c) reads it through
 * scpiParser_TerminatePendingOutput() to decide whether to close the line
 * before the transports write "**ERROR: ...".  Rounds 1 and 2 left the flag
 * strictly alone across a direct write (see the long note on
 * SCPI_PrepareDirectResult above), which is correct for a SINGLE-command
 * message -- SCPI_Parse() has just set first_output TRUE, so a
 * self-terminated direct reply followed by an error got its error on a fresh
 * line with nothing between.  In a COMPOUND message it is wrong: an earlier
 * unit's SCPI_ResultXxx() value left the flag FALSE, and the flag still said
 * "unterminated" after the direct writer had already ended its own line, so
 * the terminator was written a SECOND time and
 *
 *     *IDN?;SYSTem:STORage:SD:SPACe?      (no card in the slot)
 *
 * came back with a blank line wedged between the message and its error:
 *
 *     DAQiFi,Nq1,<serial>,01-02;\r\nError !! No SD Card Detected\r\n
 *     \r\n
 *     **ERROR: -200, "Execution error"\r\n
 *
 * The same shape reaches SYSTem:INFo? with BoardData missing,
 * SYSTem:POWer:BQ:REGisters? and SYSTem:POWer:BQ:DIAGnostics? with the I2C bus
 * down -- every direct-write query that prints an explanation and THEN raises.
 *
 * WHY A PARAMETER AND NOT A RULE.  Neither global assumption works.  "Direct
 * output is always terminated" (clear the flag unconditionally in
 * SCPI_PrepareDirectResult) glues the error onto callbacks that write mid-line
 * -- SYSTem:INFo? writes "  DIO state: ", then one byte per pin, and its
 * __stalled_exit path raises an execution error from inside exactly that loop.
 * "Direct output is never terminated" is the pre-#1096 state, i.e. the blank
 * line above.  Only the code that produced the bytes knows which it wrote, so
 * it is the code that produced the bytes that says.
 *
 * CONTRACT for callers.  Call it immediately AFTER each actual write, inside
 * the same guard the write is inside, passing TRUE iff the bytes that reached
 * the transport ended with SCPI_LINE_ENDING.  A write that does not happen
 * gets no call: leaving the flag alone is what correctly preserves an EARLIER
 * unit's pending state.  A short write ended the line only if the whole of it
 * got through, so callers that can detect one (SysInfoText_Write) must fold
 * that in.  SCPI_DirectResultEndsLine() above answers the question for callers
 * whose bytes are not known at compile time.
 *
 * NOT idempotent, unlike its Prepare counterpart, and deliberately so: a
 * callback with several write sites calls it once per write, and the LAST call
 * -- the state the wire is actually in -- wins.
 *
 * INTERACTION WITH processCommand()'s POST-CALLBACK FLIP, which is unchanged
 * and must stay that way.  That flip exists to give a direct-write query the
 * trailing SCPI_LINE_ENDING that SCPI_Parse()'s closing writeNewLine() emits;
 * it runs only after a SUCCESSFUL callback and only when first_output is TRUE,
 * so passing TRUE here re-arms it and the closing newline still appears exactly
 * where it did before.  Note what that means for a callback that SUCCEEDS: the
 * flip absorbs the difference (TRUE -> flip clears the flag -> closing newline
 * fires; FALSE -> flip is a no-op -> closing newline fires), so the argument is
 * inert on that path.  It decides the wire only when an error is raised in the
 * same unit AFTER the write, which is before the flip -- which is exactly the
 * bug above.  SYSTem:SYSInfoPB? therefore passes FALSE on principle rather than
 * on today's consequences: raw Protocol Buffer bytes are not line-framed at
 * all (a payload can even END in 0x0D 0x0A without that meaning anything), so
 * FALSE is the true description of the wire and stays correct if that callback
 * ever gains an error path after its write.
 *
 * WHY THE QUERY FILTER.  Same reason SCPI_PrepareDirectResult has one, over the
 * same shared helpers (scpi_printf(), SCPI_CheckSDCardPresent()): a non-query
 * command's direct output has never taken part in this protocol, before or
 * after #1003, and giving it line-state bookkeeping here would silently drop
 * the trailing newline SCPI_Parse() currently emits after, say,
 * "*IDN?;SYSTem:POWer:BQ:ILIM 1".  The residual is stated plainly: a NON-query
 * direct writer that prints terminated text and then raises still gets the
 * blank line this function removes for queries.  That is unchanged behaviour,
 * not new, and changing it is a wire change for a dozen commands that this
 * issue did not look at.
 *
 * @param context
 * @param terminated TRUE iff the bytes just written ended with SCPI_LINE_ENDING
 */
void SCPI_FinishDirectResult(scpi_t * context, scpi_bool_t terminated) {
    if (context == NULL) {
        return;
    }

    if (!directResultUnitIsQuery(context)) {
        return;
    }

    /* The whole point: first_output is the "something unterminated is pending"
     * flag, and the caller has just told us whether that is now true. */
    context->first_output = terminated ? TRUE : FALSE;
}

/**
 * Conditionaly write ";"
 * @param context
 * @return number of characters written
 */
static size_t writeSemicolon(scpi_t * context) {
    if (context->output_count > 0) {
        return writeData(context, ";", 1);
    } else {
        return 0;
    }
}

/**
 * Process command
 * @param context
 */
static scpi_bool_t processCommand(scpi_t * context) {
    const scpi_command_t * cmd = context->param_list.cmd;
    lex_state_t * state = &context->param_list.lex_state;
    scpi_bool_t result = TRUE;
    scpi_bool_t is_query = context->param_list.cmd_raw.data[context->param_list.cmd_raw.length - 1] == '?';

    /* DAQiFi patch (issues #1003 / #1010): the speculative
     *
     *     if (!context->first_output && is_query) writeData(context, ";", 1);
     *
     * that stood here is GONE.  It ran before the callback, so a failing
     * callback still got its ";" written and the error line that followed was
     * introduced by a value separator.  The ";" now comes out of
     * writeDelimiter() at the moment a value is actually produced -- see the
     * long note on that function.
     *
     * The first_output flip below is deliberately KEPT, and is not redundant
     * with writeDelimiter()'s: several firmware query callbacks (SD:LIST?,
     * SD:GET?, SYST:LOG?, ...) bypass SCPI_ResultXxx() entirely and write
     * straight to context->interface->write, so writeDelimiter() never runs
     * for them.  Dropping the flip would silently remove the trailing
     * SCPI_LINE_ENDING that SCPI_Parse()'s closing writeNewLine() emits for
     * those replies today -- a wire change well outside these two issues.
     *
     * Those same callbacks now ALSO call SCPI_PrepareDirectResult() (above) to
     * claim their ";" at the moment they write, and SCPI_FinishDirectResult()
     * (issue #1096) immediately after each write to say whether it ended the
     * line.  A reply that DID end its line leaves first_output TRUE, so this
     * flip fires and the closing writeNewLine() still appends the trailing
     * SCPI_LINE_ENDING those replies have always carried; a reply that did not
     * (SYSTem:SYSInfoPB?'s Protocol Buffer bytes, a mid-line fragment) leaves
     * it FALSE, this flip is a no-op, and the closing writeNewLine() terminates
     * the message instead.  Either way the wire is unchanged from before
     * #1096 -- which is the property that makes those two functions safe.
     *
     * The flip additionally carries the two cases neither helper can reach: a
     * direct-write query whose reply leaves the device OUT OF BAND rather than
     * through context->interface->write (SYSTem:STORage:SD:LISt?, whose listing
     * is pushed by the SD task through sd_card_manager_DataReadyCB), and any
     * query callback that succeeds without producing output at all. */
    context->cmd_error = FALSE;
    context->output_count = 0;
    context->input_count = 0;
    context->arbitrary_remaining = 0;

    /* if callback exists - call command callback */
    if (cmd->callback != NULL) {
        if ((cmd->callback(context) != SCPI_RES_OK)) {
            if (!context->cmd_error) {
                SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
            }
            result = FALSE;
        } else {
            if (context->cmd_error) {
                result = FALSE;
            } else {
                if(context->first_output && is_query) {
                    context->first_output = FALSE;
                }
            }
        }
    }

    /* set error if command callback did not read all parameters */
    if (state->pos < (state->buffer + state->len) && !context->cmd_error) {
        SCPI_ErrorPush(context, SCPI_ERROR_PARAMETER_NOT_ALLOWED);
        result = FALSE;
    }

    return result;
}

/**
 * Cycle all patterns and search matching pattern. Execute command callback.
 * @param context
 * @result TRUE if context->paramlist is filled with correct values
 */
static scpi_bool_t findCommandHeader(scpi_t * context, const char * header, int len) {
    int32_t i;
    const scpi_command_t * cmd;

    for (i = 0; context->cmdlist[i].pattern != NULL; i++) {
        cmd = &context->cmdlist[i];
        if (matchCommand(cmd->pattern, header, len, NULL, 0, 0)) {
            context->param_list.cmd = cmd;
            return TRUE;
        }
    }
    return FALSE;
}

/**
 * Parse one command line
 * @param context
 * @param data - complete command line
 * @param len - command line length
 * @return FALSE if there was some error during evaluation of commands
 */
scpi_bool_t SCPI_Parse(scpi_t * context, char * data, int len) {
    scpi_bool_t result = TRUE;
    scpi_parser_state_t * state;
    int r;
    scpi_token_t cmd_prev = {SCPI_TOKEN_UNKNOWN, NULL, 0};

    if (context == NULL) {
        return FALSE;
    }

    state = &context->parser_state;
    context->output_count = 0;
    context->first_output = TRUE;

    while (1) {
        r = scpiParser_detectProgramMessageUnit(state, data, len);

        if (state->programHeader.type == SCPI_TOKEN_INVALID) {
            SCPI_ErrorPush(context, SCPI_ERROR_INVALID_CHARACTER);
            result = FALSE;
        } else if (state->programHeader.len > 0) {

            composeCompoundCommand(&cmd_prev, &state->programHeader);

            if (findCommandHeader(context, state->programHeader.ptr, state->programHeader.len)) {

                context->param_list.lex_state.buffer = state->programData.ptr;
                context->param_list.lex_state.pos = context->param_list.lex_state.buffer;
                context->param_list.lex_state.len = state->programData.len;
                context->param_list.cmd_raw.data = state->programHeader.ptr;
                context->param_list.cmd_raw.position = 0;
                context->param_list.cmd_raw.length = state->programHeader.len;

                result &= processCommand(context);
                cmd_prev = state->programHeader;
            } else {
                /* place undefined header with error */
                /* calculate length of errorenous header and trim \r\n */
                size_t r2 = r;
                while (r2 > 0 && (data[r2 - 1] == '\r' || data[r2 - 1] == '\n')) r2--;
                SCPI_ErrorPushEx(context, SCPI_ERROR_UNDEFINED_HEADER, data, r2);
                result = FALSE;
            }
        }

        if (r < len) {
            data += r;
            len -= r;
        } else {
            break;
        }

    }

    /* conditionally write new line */
    writeNewLine(context);

    return result;
}

/**
 * Initialize SCPI context structure
 * @param context
 * @param commands
 * @param interface
 * @param units
 * @param idn1
 * @param idn2
 * @param idn3
 * @param idn4
 * @param input_buffer
 * @param input_buffer_length
 * @param error_queue_data
 * @param error_queue_size
 */
void SCPI_Init(scpi_t * context,
        const scpi_command_t * commands,
        scpi_interface_t * interface,
        const scpi_unit_def_t * units,
        const char * idn1, const char * idn2, const char * idn3, const char * idn4,
        char * input_buffer, size_t input_buffer_length,
        scpi_error_t * error_queue_data, int16_t error_queue_size) {
    memset(context, 0, sizeof (*context));
    /* DAQiFi patch (issues #1003 / #1010): the memset above leaves
     * first_output FALSE, i.e. "a result is pending", on a context that has
     * never parsed anything.  SCPI_Parse() sets it TRUE on entry so upstream
     * never noticed, but SCPI_ErrorEmit() now reads the flag and an error
     * raised BEFORE the first parse (SCPI_Input()'s input-buffer-overrun push)
     * would otherwise be prefixed with a blank line. */
    context->first_output = TRUE;
    context->cmdlist = commands;
    context->interface = interface;
    context->units = units;
    context->idn[0] = idn1;
    context->idn[1] = idn2;
    context->idn[2] = idn3;
    context->idn[3] = idn4;
    context->buffer.data = input_buffer;
    context->buffer.length = input_buffer_length;
    context->buffer.position = 0;
    SCPI_ErrorInit(context, error_queue_data, error_queue_size);
}

#if USE_DEVICE_DEPENDENT_ERROR_INFORMATION && !USE_MEMORY_ALLOCATION_FREE

/**
 * Initialize context's
 * @param context
 * @param data
 * @param len
 * @return
 */
void SCPI_InitHeap(scpi_t * context,
        char * error_info_heap, size_t error_info_heap_length) {
    scpiheap_init(&context->error_info_heap, error_info_heap, error_info_heap_length);
}
#endif

/**
 * Interface to the application. Adds data to system buffer and try to search
 * command line termination. If the termination is found or if len=0, command
 * parser is called.
 *
 * @param context
 * @param data - data to process
 * @param len - length of data
 * @return
 */
scpi_bool_t SCPI_Input(scpi_t * context, const char * data, int len) {
    scpi_bool_t result = TRUE;
    size_t totcmdlen = 0;
    int cmdlen = 0;

    if (len == 0) {
        context->buffer.data[context->buffer.position] = 0;
        result = SCPI_Parse(context, context->buffer.data, context->buffer.position);
        context->buffer.position = 0;
    } else {
        int buffer_free;

        buffer_free = context->buffer.length - context->buffer.position;
        if (len > (buffer_free - 1)) {
            /* Input buffer overrun - invalidate buffer */
            context->buffer.position = 0;
            context->buffer.data[context->buffer.position] = 0;
            SCPI_ErrorPush(context, SCPI_ERROR_INPUT_BUFFER_OVERRUN);
            return FALSE;
        }
        memcpy(&context->buffer.data[context->buffer.position], data, len);
        context->buffer.position += len;
        context->buffer.data[context->buffer.position] = 0;


        while (1) {
            cmdlen = scpiParser_detectProgramMessageUnit(&context->parser_state, context->buffer.data + totcmdlen, context->buffer.position - totcmdlen);
            totcmdlen += cmdlen;

            if (context->parser_state.termination == SCPI_MESSAGE_TERMINATION_NL) {
                result = SCPI_Parse(context, context->buffer.data, totcmdlen);
                memmove(context->buffer.data, context->buffer.data + totcmdlen, context->buffer.position - totcmdlen);
                context->buffer.position -= totcmdlen;
                totcmdlen = 0;
            } else {
                if (context->parser_state.programHeader.type == SCPI_TOKEN_UNKNOWN
                        && context->parser_state.termination == SCPI_MESSAGE_TERMINATION_NONE) break;
                if (totcmdlen >= context->buffer.position) break;
            }
        }
    }

    return result;
}

/* writing results */

/**
 * Write raw string result to the output
 * @param context
 * @param data
 * @return
 */
size_t SCPI_ResultCharacters(scpi_t * context, const char * data, size_t len) {
    size_t result = 0;
    result += writeDelimiter(context);
    result += writeData(context, data, len);
    context->output_count++;
    return result;
}

/**
 * Return prefix of nondecimal base
 * @param base
 * @return
 */
static const char * getBasePrefix(int8_t base) {
    switch (base) {
        case 2: return "#B";
        case 8: return "#Q";
        case 16: return "#H";
        default: return NULL;
    }
}

/**
 * Write signed/unsigned 32 bit integer value in specific base to the result
 * @param context
 * @param val
 * @param base
 * @param sign
 * @return
 */
static size_t resultUInt32BaseSign(scpi_t * context, uint32_t val, int8_t base, scpi_bool_t sign) {
    char buffer[32 + 1];
    const char * basePrefix;
    size_t result = 0;
    size_t len;

    len = UInt32ToStrBaseSign(val, buffer, sizeof (buffer), base, sign);
    basePrefix = getBasePrefix(base);

    result += writeDelimiter(context);
    if (basePrefix != NULL) {
        result += writeData(context, basePrefix, 2);
    }
    result += writeData(context, buffer, len);
    context->output_count++;
    return result;
}

/**
 * Write signed/unsigned 64 bit integer value in specific base to the result
 * @param context
 * @param val
 * @param base
 * @param sign
 * @return
 */
static size_t resultUInt64BaseSign(scpi_t * context, uint64_t val, int8_t base, scpi_bool_t sign) {
    char buffer[64 + 1];
    const char * basePrefix;
    size_t result = 0;
    size_t len;

    len = UInt64ToStrBaseSign(val, buffer, sizeof (buffer), base, sign);
    basePrefix = getBasePrefix(base);

    result += writeDelimiter(context);
    if (basePrefix != NULL) {
        result += writeData(context, basePrefix, 2);
    }
    result += writeData(context, buffer, len);
    context->output_count++;
    return result;
}

/**
 * Write signed 32 bit integer value to the result
 * @param context
 * @param val
 * @return
 */
size_t SCPI_ResultInt32(scpi_t * context, int32_t val) {
    return resultUInt32BaseSign(context, val, 10, TRUE);
}

/**
 * Write unsigned 32 bit integer value in specific base to the result
 * Write signed/unsigned 32 bit integer value in specific base to the result
 * @param context
 * @param val
 * @return
 */
size_t SCPI_ResultUInt32Base(scpi_t * context, uint32_t val, int8_t base) {
    return resultUInt32BaseSign(context, val, base, FALSE);
}

/**
 * Write signed 64 bit integer value to the result
 * @param context
 * @param val
 * @return
 */
size_t SCPI_ResultInt64(scpi_t * context, int64_t val) {
    return resultUInt64BaseSign(context, val, 10, TRUE);
}

/**
 * Write unsigned 64 bit integer value in specific base to the result
 * @param context
 * @param val
 * @return
 */
size_t SCPI_ResultUInt64Base(scpi_t * context, uint64_t val, int8_t base) {
    return resultUInt64BaseSign(context, val, base, FALSE);
}

/**
 * Write float (32 bit) value to the result
 * @param context
 * @param val
 * @return
 */
size_t SCPI_ResultFloat(scpi_t * context, float val) {
    char buffer[32];
    size_t result = 0;
    size_t len = SCPI_FloatToStr(val, buffer, sizeof (buffer));
    result += writeDelimiter(context);
    result += writeData(context, buffer, len);
    context->output_count++;
    return result;
}

/**
 * Write double (64bit) value to the result
 * @param context
 * @param val
 * @return
 */
size_t SCPI_ResultDouble(scpi_t * context, double val) {
    char buffer[32];
    size_t result = 0;
    size_t len = SCPI_DoubleToStr(val, buffer, sizeof (buffer));
    result += writeDelimiter(context);
    result += writeData(context, buffer, len);
    context->output_count++;
    return result;
}

/**
 * Write string within "" to the result
 * @param context
 * @param data
 * @return
 */
size_t SCPI_ResultText(scpi_t * context, const char * data) {
    size_t result = 0;
    size_t len = strlen(data);
    const char * quote;
    result += writeDelimiter(context);
    result += writeData(context, "\"", 1);
    while ((quote = strnpbrk(data, len, "\""))) {
        result += writeData(context, data, quote - data + 1);
        result += writeData(context, "\"", 1);
        len -= quote - data + 1;
        data = quote + 1;
    }
    result += writeData(context, data, len);
    result += writeData(context, "\"", 1);
    context->output_count++;
    return result;
}

/**
 * SCPI-99:21.8 Device-dependent error information.
 * Write error information with the following syntax:
 * <Error/event_number>,"<Error/event_description>[;<Device-dependent_info>]"
 * The maximum string length of <Error/event_description> plus <Device-dependent_info>
 * is SCPI_STD_ERROR_DESC_MAX_STRING_LENGTH (255) characters.
 *
 * @param context
 * @param error
 * @return
 */
size_t SCPI_ResultError(scpi_t * context, scpi_error_t * error) {
    size_t result = 0;
    size_t outputlimit = SCPI_STD_ERROR_DESC_MAX_STRING_LENGTH;
    size_t step = 0;
    const char * quote;

    const char * data[SCPIDEFINE_DESCRIPTION_MAX_PARTS];
    size_t len[SCPIDEFINE_DESCRIPTION_MAX_PARTS];
    size_t i;

    data[0] = SCPI_ErrorTranslate(error->error_code);
    len[0] = strlen(data[0]);

#if USE_DEVICE_DEPENDENT_ERROR_INFORMATION
    data[1] = error->device_dependent_info;
#if USE_MEMORY_ALLOCATION_FREE
    len[1] = error->device_dependent_info ? strlen(data[1]) : 0;
#else
    SCPIDEFINE_get_parts(&context->error_info_heap, data[1], &len[1], &data[2], &len[2]);
#endif
#endif

    result += SCPI_ResultInt32(context, error->error_code);
    result += writeDelimiter(context);
    result += writeData(context, "\"", 1);

    for (i = 0; (i < SCPIDEFINE_DESCRIPTION_MAX_PARTS) && data[i] && outputlimit; i++) {
        if (i == 1) {
            result += writeSemicolon(context);
            outputlimit -= 1;
        }
        if (len[i] > outputlimit) {
            len[i] = outputlimit;
        }

        while ((quote = strnpbrk(data[i], len[i], "\""))) {
            if ((step = quote - data[i] + 1) >= outputlimit) {
                len[i] -= 1;
                outputlimit -= 1;
                break;
            }
            result += writeData(context, data[i], step);
            result += writeData(context, "\"", 1);
            len[i] -= step;
            outputlimit -= step + 1;
            data[i] = quote + 1;
            if (len[i] > outputlimit) {
                len[i] = outputlimit;
            }
        }

        result += writeData(context, data[i], len[i]);
        outputlimit -= len[i];
    }
    result += writeData(context, "\"", 1);

    return result;
}

/**
 * Write arbitrary block header with length
 * @param context
 * @param len
 * @return
 */
size_t SCPI_ResultArbitraryBlockHeader(scpi_t * context, size_t len) {
    size_t result = 0;
    char block_header[12];
    size_t header_len;
    block_header[0] = '#';
    SCPI_UInt32ToStrBase((uint32_t) len, block_header + 2, 10, 10);

    header_len = strlen(block_header + 2);
    block_header[1] = (char) (header_len + '0');

    context->arbitrary_remaining = len;
    result  = writeDelimiter(context);
    result += writeData(context, block_header, header_len + 2);
    return result;
}

/**
 * Add data to arbitrary block
 * @param context
 * @param data
 * @param len
 * @return
 */
size_t SCPI_ResultArbitraryBlockData(scpi_t * context, const void * data, size_t len) {

    if (context->arbitrary_remaining < len) {
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return 0;
    }

    context->arbitrary_remaining -= len;

    if (context->arbitrary_remaining == 0) {
        context->output_count++;
    }

    return writeData(context, (const char *) data, len);
}

/**
 * Write arbitrary block program data to the result
 * @param context
 * @param data
 * @param len
 * @return
 */
size_t SCPI_ResultArbitraryBlock(scpi_t * context, const void * data, size_t len) {
    size_t result = 0;
    result += SCPI_ResultArbitraryBlockHeader(context, len);
    result += SCPI_ResultArbitraryBlockData(context, data, len);
    return result;
}

/**
 * Write boolean value to the result
 * @param context
 * @param val
 * @return
 */
size_t SCPI_ResultBool(scpi_t * context, scpi_bool_t val) {
    return resultUInt32BaseSign(context, val ? 1 : 0, 10, FALSE);
}

/* parsing parameters */

/**
 * Invalidate token
 * @param token
 * @param ptr
 */
static void invalidateToken(scpi_token_t * token, char * ptr) {
    token->len = 0;
    token->ptr = ptr;
    token->type = SCPI_TOKEN_UNKNOWN;
}

/**
 * Get one parameter from command line
 * @param context
 * @param parameter
 * @param mandatory
 * @return
 */
scpi_bool_t SCPI_Parameter(scpi_t * context, scpi_parameter_t * parameter, scpi_bool_t mandatory) {
    lex_state_t * state;

    if (!parameter) {
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return FALSE;
    }

    invalidateToken(parameter, NULL);

    state = &context->param_list.lex_state;

    if (state->pos >= (state->buffer + state->len)) {
        if (mandatory) {
            SCPI_ErrorPush(context, SCPI_ERROR_MISSING_PARAMETER);
        } else {
            parameter->type = SCPI_TOKEN_PROGRAM_MNEMONIC; /* TODO: select something different */
        }
        return FALSE;
    }
    if (context->input_count != 0) {
        scpiLex_Comma(state, parameter);
        if (parameter->type != SCPI_TOKEN_COMMA) {
            invalidateToken(parameter, NULL);
            SCPI_ErrorPush(context, SCPI_ERROR_INVALID_SEPARATOR);
            return FALSE;
        }
    }

    context->input_count++;

    scpiParser_parseProgramData(&context->param_list.lex_state, parameter);

    switch (parameter->type) {
        case SCPI_TOKEN_HEXNUM:
        case SCPI_TOKEN_OCTNUM:
        case SCPI_TOKEN_BINNUM:
        case SCPI_TOKEN_PROGRAM_MNEMONIC:
        case SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA:
        case SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA_WITH_SUFFIX:
        case SCPI_TOKEN_ARBITRARY_BLOCK_PROGRAM_DATA:
        case SCPI_TOKEN_SINGLE_QUOTE_PROGRAM_DATA:
        case SCPI_TOKEN_DOUBLE_QUOTE_PROGRAM_DATA:
        case SCPI_TOKEN_PROGRAM_EXPRESSION:
            return TRUE;
        default:
            invalidateToken(parameter, NULL);
            SCPI_ErrorPush(context, SCPI_ERROR_INVALID_STRING_DATA);
            return FALSE;
    }
}

/**
 * Detect if parameter is number
 * @param parameter
 * @param suffixAllowed
 * @return
 */
scpi_bool_t SCPI_ParamIsNumber(scpi_parameter_t * parameter, scpi_bool_t suffixAllowed) {
    switch (parameter->type) {
        case SCPI_TOKEN_HEXNUM:
        case SCPI_TOKEN_OCTNUM:
        case SCPI_TOKEN_BINNUM:
        case SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA:
            return TRUE;
        case SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA_WITH_SUFFIX:
            return suffixAllowed;
        default:
            return FALSE;
    }
}

/**
 * DAQiFi patch (issue #880) -- an integer parameter must be a WHOLE decimal token.
 *
 * strtol()/strtoul() stop at the first character they cannot use and report how
 * far they got, so upstream libscpi's "converted at least one character" test
 * accepts a token it only partly read:
 *
 *     "-0.5" -> 0      (stops at '.')      "0.5" -> 0
 *     "1E3"  -> 1      (stops at 'E')      "5 V" -> 5   (suffixed token)
 *
 * Each of those was handed to the caller as its value with nothing queued, so a
 * fractional or exponent-form channel index silently addressed a DIFFERENT
 * channel than the client asked for -- the same user-visible failure as an
 * out-of-range index, which the callbacks already reject (#877).  No guard
 * inside a callback can see it: by the time one runs, the value is the
 * truncated integer.
 *
 * Require the conversion to have consumed the whole token instead, and push
 * -104 when it did not -- the error libscpi itself pushes one layer up
 * (ParamSignUInt32) for a token that is not numeric at all.
 *
 * `consumed` is taken as int, matching scpi_token_t::len's own type, so the
 * comparison is not signed-vs-unsigned (this tree builds -Werror). The value
 * is a count of characters within one token of the SCPI input buffer, so it
 * cannot approach INT_MAX.
 *
 * Only the two decimal arms need this.  The HEXNUM/OCTNUM/BINNUM tokens carry a
 * ptr past the "#H"/"#Q"/"#B" prefix and a len covering only digits valid in
 * that base (scpiLex_NondecimalNumericData), so their conversion always
 * consumes the whole token and the check would be a no-op there.
 */
static scpi_bool_t DaqifiIntTokenFullyConsumed(scpi_t * context, const scpi_parameter_t * parameter, int consumed) {
    if ((consumed > 0) && (consumed == parameter->len)) {
        return TRUE;
    }
    SCPI_ErrorPush(context, SCPI_ERROR_DATA_TYPE_ERROR);
    return FALSE;
}

/**
 * Convert parameter to signed/unsigned 32 bit integer
 * @param context
 * @param parameter
 * @param value result
 * @param sign
 * @return TRUE if succesful
 */
static scpi_bool_t ParamSignToUInt32(scpi_t * context, scpi_parameter_t * parameter, uint32_t * value, scpi_bool_t sign) {

    if (!value) {
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return FALSE;
    }

    switch (parameter->type) {
        case SCPI_TOKEN_HEXNUM:
            return strBaseToUInt32(parameter->ptr, value, 16) > 0 ? TRUE : FALSE;
        case SCPI_TOKEN_OCTNUM:
            return strBaseToUInt32(parameter->ptr, value, 8) > 0 ? TRUE : FALSE;
        case SCPI_TOKEN_BINNUM:
            return strBaseToUInt32(parameter->ptr, value, 2) > 0 ? TRUE : FALSE;
        case SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA:
        case SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA_WITH_SUFFIX:
            /* DAQiFi #880: the whole token, not a truncated prefix. */
            if (sign) {
                return DaqifiIntTokenFullyConsumed(context, parameter,
                        (int) strBaseToInt32(parameter->ptr, (int32_t *) value, 10));
            } else {
                return DaqifiIntTokenFullyConsumed(context, parameter,
                        (int) strBaseToUInt32(parameter->ptr, value, 10));
            }
        default:
            return FALSE;
    }
}

/**
 * Convert parameter to signed/unsigned 64 bit integer
 * @param context
 * @param parameter
 * @param value result
 * @param sign
 * @return TRUE if succesful
 */
static scpi_bool_t ParamSignToUInt64(scpi_t * context, scpi_parameter_t * parameter, uint64_t * value, scpi_bool_t sign) {

    if (!value) {
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return FALSE;
    }

    switch (parameter->type) {
        case SCPI_TOKEN_HEXNUM:
            return strBaseToUInt64(parameter->ptr, value, 16) > 0 ? TRUE : FALSE;
        case SCPI_TOKEN_OCTNUM:
            return strBaseToUInt64(parameter->ptr, value, 8) > 0 ? TRUE : FALSE;
        case SCPI_TOKEN_BINNUM:
            return strBaseToUInt64(parameter->ptr, value, 2) > 0 ? TRUE : FALSE;
        case SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA:
        case SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA_WITH_SUFFIX:
            /* DAQiFi #880: the whole token, not a truncated prefix. */
            if (sign) {
                return DaqifiIntTokenFullyConsumed(context, parameter,
                        (int) strBaseToInt64(parameter->ptr, (int64_t *) value, 10));
            } else {
                return DaqifiIntTokenFullyConsumed(context, parameter,
                        (int) strBaseToUInt64(parameter->ptr, value, 10));
            }
        default:
            return FALSE;
    }
}

/**
 * Convert parameter to signed 32 bit integer
 * @param context
 * @param parameter
 * @param value result
 * @return TRUE if succesful
 */
scpi_bool_t SCPI_ParamToInt32(scpi_t * context, scpi_parameter_t * parameter, int32_t * value) {
    return ParamSignToUInt32(context, parameter, (uint32_t *) value, TRUE);
}

/**
 * Convert parameter to unsigned 32 bit integer
 * @param context
 * @param parameter
 * @param value result
 * @return TRUE if succesful
 */
scpi_bool_t SCPI_ParamToUInt32(scpi_t * context, scpi_parameter_t * parameter, uint32_t * value) {
    return ParamSignToUInt32(context, parameter, value, FALSE);
}

/**
 * Convert parameter to signed 64 bit integer
 * @param context
 * @param parameter
 * @param value result
 * @return TRUE if succesful
 */
scpi_bool_t SCPI_ParamToInt64(scpi_t * context, scpi_parameter_t * parameter, int64_t * value) {
    return ParamSignToUInt64(context, parameter, (uint64_t *) value, TRUE);
}

/**
 * Convert parameter to unsigned 32 bit integer
 * @param context
 * @param parameter
 * @param value result
 * @return TRUE if succesful
 */
scpi_bool_t SCPI_ParamToUInt64(scpi_t * context, scpi_parameter_t * parameter, uint64_t * value) {
    return ParamSignToUInt64(context, parameter, value, FALSE);
}

/**
 * Convert parameter to float (32 bit)
 * @param context
 * @param parameter
 * @param value result
 * @return TRUE if succesful
 */
scpi_bool_t SCPI_ParamToFloat(scpi_t * context, scpi_parameter_t * parameter, float * value) {
    scpi_bool_t result;
    uint32_t valint;

    if (!value) {
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return FALSE;
    }

    switch (parameter->type) {
        case SCPI_TOKEN_HEXNUM:
        case SCPI_TOKEN_OCTNUM:
        case SCPI_TOKEN_BINNUM:
            result = SCPI_ParamToUInt32(context, parameter, &valint);
            *value = valint;
            break;
        case SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA:
        case SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA_WITH_SUFFIX:
            result = strToFloat(parameter->ptr, value) > 0 ? TRUE : FALSE;
            break;
        default:
            result = FALSE;
    }
    return result;
}

/**
 * Convert parameter to double (64 bit)
 * @param context
 * @param parameter
 * @param value result
 * @return TRUE if succesful
 */
scpi_bool_t SCPI_ParamToDouble(scpi_t * context, scpi_parameter_t * parameter, double * value) {
    scpi_bool_t result;
    uint64_t valint;

    if (!value) {
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return FALSE;
    }

    switch (parameter->type) {
        case SCPI_TOKEN_HEXNUM:
        case SCPI_TOKEN_OCTNUM:
        case SCPI_TOKEN_BINNUM:
            result = SCPI_ParamToUInt64(context, parameter, &valint);
            *value = valint;
            break;
        case SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA:
        case SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA_WITH_SUFFIX:
            result = strToDouble(parameter->ptr, value) > 0 ? TRUE : FALSE;
            break;
        default:
            result = FALSE;
    }
    return result;
}

/**
 * Read floating point float (32 bit) parameter
 * @param context
 * @param value
 * @param mandatory
 * @return
 */
scpi_bool_t SCPI_ParamFloat(scpi_t * context, float * value, scpi_bool_t mandatory) {
    scpi_bool_t result;
    scpi_parameter_t param;

    if (!value) {
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return FALSE;
    }

    result = SCPI_Parameter(context, &param, mandatory);
    if (result) {
        if (SCPI_ParamIsNumber(&param, FALSE)) {
            SCPI_ParamToFloat(context, &param, value);
        } else if (SCPI_ParamIsNumber(&param, TRUE)) {
            SCPI_ErrorPush(context, SCPI_ERROR_SUFFIX_NOT_ALLOWED);
            result = FALSE;
        } else {
            SCPI_ErrorPush(context, SCPI_ERROR_DATA_TYPE_ERROR);
            result = FALSE;
        }
    }
    return result;
}

/**
 * Read floating point double (64 bit) parameter
 * @param context
 * @param value
 * @param mandatory
 * @return
 */
scpi_bool_t SCPI_ParamDouble(scpi_t * context, double * value, scpi_bool_t mandatory) {
    scpi_bool_t result;
    scpi_parameter_t param;

    if (!value) {
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return FALSE;
    }

    result = SCPI_Parameter(context, &param, mandatory);
    if (result) {
        if (SCPI_ParamIsNumber(&param, FALSE)) {
            SCPI_ParamToDouble(context, &param, value);
        } else if (SCPI_ParamIsNumber(&param, TRUE)) {
            SCPI_ErrorPush(context, SCPI_ERROR_SUFFIX_NOT_ALLOWED);
            result = FALSE;
        } else {
            SCPI_ErrorPush(context, SCPI_ERROR_DATA_TYPE_ERROR);
            result = FALSE;
        }
    }
    return result;
}

/**
 * Read signed/unsigned 32 bit integer parameter
 * @param context
 * @param value
 * @param mandatory
 * @param sign
 * @return
 */
static scpi_bool_t ParamSignUInt32(scpi_t * context, uint32_t * value, scpi_bool_t mandatory, scpi_bool_t sign) {
    scpi_bool_t result;
    scpi_parameter_t param;

    if (!value) {
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return FALSE;
    }

    result = SCPI_Parameter(context, &param, mandatory);
    if (result) {
        if (SCPI_ParamIsNumber(&param, FALSE)) {
            result = ParamSignToUInt32(context, &param, value, sign);
        } else if (SCPI_ParamIsNumber(&param, TRUE)) {
            SCPI_ErrorPush(context, SCPI_ERROR_SUFFIX_NOT_ALLOWED);
            result = FALSE;
        } else {
            SCPI_ErrorPush(context, SCPI_ERROR_DATA_TYPE_ERROR);
            result = FALSE;
        }
    }
    return result;
}

/**
 * Read signed/unsigned 64 bit integer parameter
 * @param context
 * @param value
 * @param mandatory
 * @param sign
 * @return
 */
static scpi_bool_t ParamSignUInt64(scpi_t * context, uint64_t * value, scpi_bool_t mandatory, scpi_bool_t sign) {
    scpi_bool_t result;
    scpi_parameter_t param;

    if (!value) {
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return FALSE;
    }

    result = SCPI_Parameter(context, &param, mandatory);
    if (result) {
        if (SCPI_ParamIsNumber(&param, FALSE)) {
            result = ParamSignToUInt64(context, &param, value, sign);
        } else if (SCPI_ParamIsNumber(&param, TRUE)) {
            SCPI_ErrorPush(context, SCPI_ERROR_SUFFIX_NOT_ALLOWED);
            result = FALSE;
        } else {
            SCPI_ErrorPush(context, SCPI_ERROR_DATA_TYPE_ERROR);
            result = FALSE;
        }
    }
    return result;
}

/**
 * Read signed 32 bit integer parameter
 * @param context
 * @param value
 * @param mandatory
 * @return
 */
scpi_bool_t SCPI_ParamInt32(scpi_t * context, int32_t * value, scpi_bool_t mandatory) {
    return ParamSignUInt32(context, (uint32_t *) value, mandatory, TRUE);
}

/**
 * Read unsigned 32 bit integer parameter
 * @param context
 * @param value
 * @param mandatory
 * @return
 */
scpi_bool_t SCPI_ParamUInt32(scpi_t * context, uint32_t * value, scpi_bool_t mandatory) {
    return ParamSignUInt32(context, value, mandatory, FALSE);
}

/**
 * Read signed 64 bit integer parameter
 * @param context
 * @param value
 * @param mandatory
 * @return
 */
scpi_bool_t SCPI_ParamInt64(scpi_t * context, int64_t * value, scpi_bool_t mandatory) {
    return ParamSignUInt64(context, (uint64_t *) value, mandatory, TRUE);
}

/**
 * Read unsigned 64 bit integer parameter
 * @param context
 * @param value
 * @param mandatory
 * @return
 */
scpi_bool_t SCPI_ParamUInt64(scpi_t * context, uint64_t * value, scpi_bool_t mandatory) {
    return ParamSignUInt64(context, value, mandatory, FALSE);
}

/**
 * Read character parameter
 * @param context
 * @param value
 * @param len
 * @param mandatory
 * @return
 */
scpi_bool_t SCPI_ParamCharacters(scpi_t * context, const char ** value, size_t * len, scpi_bool_t mandatory) {
    scpi_bool_t result;
    scpi_parameter_t param;

    if (!value || !len) {
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return FALSE;
    }

    result = SCPI_Parameter(context, &param, mandatory);
    if (result) {
        switch (param.type) {
            case SCPI_TOKEN_SINGLE_QUOTE_PROGRAM_DATA:
            case SCPI_TOKEN_DOUBLE_QUOTE_PROGRAM_DATA:
                *value = param.ptr + 1;
                *len = param.len - 2;
                break;
            default:
                *value = param.ptr;
                *len = param.len;
                break;
        }

        /* TODO: return also parameter type (ProgramMnemonic, ArbitraryBlockProgramData, SingleQuoteProgramData, DoubleQuoteProgramData */
    }

    return result;
}

/**
 * Get arbitrary block program data and returns pointer to data
 * @param context
 * @param value result pointer to data
 * @param len result length of data
 * @param mandatory
 * @return
 */
scpi_bool_t SCPI_ParamArbitraryBlock(scpi_t * context, const char ** value, size_t * len, scpi_bool_t mandatory) {
    scpi_bool_t result;
    scpi_parameter_t param;

    if (!value || !len) {
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return FALSE;
    }

    result = SCPI_Parameter(context, &param, mandatory);
    if (result) {
        if (param.type == SCPI_TOKEN_ARBITRARY_BLOCK_PROGRAM_DATA) {
            *value = param.ptr;
            *len = param.len;
        } else {
            SCPI_ErrorPush(context, SCPI_ERROR_DATA_TYPE_ERROR);
            result = FALSE;
        }
    }

    return result;
}

scpi_bool_t SCPI_ParamCopyText(scpi_t * context, char * buffer, size_t buffer_len, size_t * copy_len, scpi_bool_t mandatory) {
    scpi_bool_t result;
    scpi_parameter_t param;
    size_t i_from;
    size_t i_to;
    char quote;

    if (!buffer || !copy_len) {
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return FALSE;
    }

    result = SCPI_Parameter(context, &param, mandatory);
    if (result) {

        switch (param.type) {
            case SCPI_TOKEN_SINGLE_QUOTE_PROGRAM_DATA:
            case SCPI_TOKEN_DOUBLE_QUOTE_PROGRAM_DATA:
                quote = param.type == SCPI_TOKEN_SINGLE_QUOTE_PROGRAM_DATA ? '\'' : '"';
                for (i_from = 1, i_to = 0; i_from < (size_t) (param.len - 1); i_from++) {
                    if (i_from >= buffer_len) {
                        break;
                    }
                    buffer[i_to] = param.ptr[i_from];
                    i_to++;
                    if (param.ptr[i_from] == quote) {
                        i_from++;
                    }
                }
                *copy_len = i_to;
                if (i_to < buffer_len) {
                    buffer[i_to] = 0;
                }
                break;
            default:
                SCPI_ErrorPush(context, SCPI_ERROR_DATA_TYPE_ERROR);
                result = FALSE;
        }
    }

    return result;
}

/**
 * Convert parameter to choice
 * @param context
 * @param parameter - should be PROGRAM_MNEMONIC
 * @param options - NULL terminated list of choices
 * @param value - index to options
 * @return
 */
scpi_bool_t SCPI_ParamToChoice(scpi_t * context, scpi_parameter_t * parameter, const scpi_choice_def_t * options, int32_t * value) {
    size_t res;
    scpi_bool_t result = FALSE;

    if (!options || !value) {
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return FALSE;
    }

    if (parameter->type == SCPI_TOKEN_PROGRAM_MNEMONIC) {
        for (res = 0; options[res].name; ++res) {
            if (matchPattern(options[res].name, strlen(options[res].name), parameter->ptr, parameter->len, NULL)) {
                *value = options[res].tag;
                result = TRUE;
                break;
            }
        }

        if (!result) {
            SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        }
    } else {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_TYPE_ERROR);
    }

    return result;
}

/**
 * Find tag in choices and returns its first textual representation
 * @param options specifications of choices numbers (patterns)
 * @param tag numerical representatio of choice
 * @param text result text
 * @return TRUE if succesfule, else FALSE
 */
scpi_bool_t SCPI_ChoiceToName(const scpi_choice_def_t * options, int32_t tag, const char ** text) {
    int i;

    for (i = 0; options[i].name != NULL; i++) {
        if (options[i].tag == tag) {
            *text = options[i].name;
            return TRUE;
        }
    }

    return FALSE;
}

/*
 * Definition of BOOL choice list
 */
const scpi_choice_def_t scpi_bool_def[] = {
    {"OFF", 0},
    {"ON", 1},
    SCPI_CHOICE_LIST_END /* termination of option list */
};

/**
 * Read BOOL parameter (0,1,ON,OFF)
 * @param context
 * @param value
 * @param mandatory
 * @return
 */
scpi_bool_t SCPI_ParamBool(scpi_t * context, scpi_bool_t * value, scpi_bool_t mandatory) {
    scpi_bool_t result;
    scpi_parameter_t param;
    int32_t intval;

    if (!value) {
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return FALSE;
    }

    result = SCPI_Parameter(context, &param, mandatory);

    if (result) {
        if (param.type == SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA) {
            /* DAQiFi #880: honour the return. It was discarded, which was
             * survivable while this conversion could only fail on a token
             * SCPI_Parameter had already accepted -- but a partly-consumed
             * decimal now fails here, and ignoring that returned TRUE with
             * `*value` set from the TRUNCATED integer while -104 sat in the
             * queue. `SCPI_ParamBool("1.5")` answered TRUE, so a boolean
             * setter would have applied a value from a rejected token.
             *
             * Unreachable in this firmware today -- nothing in services/SCPI
             * calls SCPI_ParamBool, and no libscpi-internal registered command
             * does either (grepped) -- but it is a public API in the file this
             * patch already diverges in, and leaving it inconsistent is how
             * the divergence rots. */
            result = SCPI_ParamToInt32(context, &param, &intval);
            if (result) {
                *value = intval ? TRUE : FALSE;
            }
        } else {
            result = SCPI_ParamToChoice(context, &param, scpi_bool_def, &intval);
            if (result) {
                *value = intval ? TRUE : FALSE;
            }
        }
    }

    return result;
}

/**
 * Read value from list of options
 * @param context
 * @param options
 * @param value
 * @param mandatory
 * @return
 */
scpi_bool_t SCPI_ParamChoice(scpi_t * context, const scpi_choice_def_t * options, int32_t * value, scpi_bool_t mandatory) {
    scpi_bool_t result;
    scpi_parameter_t param;

    if (!options || !value) {
        SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
        return FALSE;
    }

    result = SCPI_Parameter(context, &param, mandatory);
    if (result) {
        result = SCPI_ParamToChoice(context, &param, options, value);
    }

    return result;
}

/**
 * Parse one parameter and detect type
 * @param state
 * @param token
 * @return
 */
int scpiParser_parseProgramData(lex_state_t * state, scpi_token_t * token) {
    scpi_token_t tmp;
    int result = 0;
    int wsLen;
    int suffixLen;
    int realLen = 0;
    realLen += scpiLex_WhiteSpace(state, &tmp);

    if (result == 0) result = scpiLex_NondecimalNumericData(state, token);
    if (result == 0) result = scpiLex_CharacterProgramData(state, token);
    if (result == 0) {
        result = scpiLex_DecimalNumericProgramData(state, token);
        if (result != 0) {
            wsLen = scpiLex_WhiteSpace(state, &tmp);
            suffixLen = scpiLex_SuffixProgramData(state, &tmp);
            if (suffixLen > 0) {
                token->len += wsLen + suffixLen;
                token->type = SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA_WITH_SUFFIX;
                result = token->len;
            }
        }
    }

    if (result == 0) result = scpiLex_StringProgramData(state, token);
    if (result == 0) result = scpiLex_ArbitraryBlockProgramData(state, token);
    if (result == 0) result = scpiLex_ProgramExpression(state, token);

    realLen += scpiLex_WhiteSpace(state, &tmp);

    return result + realLen;
}

/**
 * Skip all parameters to correctly detect end of command line.
 * @param state
 * @param token
 * @param numberOfParameters
 * @return
 */
int scpiParser_parseAllProgramData(lex_state_t * state, scpi_token_t * token, int * numberOfParameters) {

    int result;
    scpi_token_t tmp;
    int paramCount = 0;

    token->len = -1;
    token->type = SCPI_TOKEN_ALL_PROGRAM_DATA;
    token->ptr = state->pos;


    for (result = 1; result != 0; result = scpiLex_Comma(state, &tmp)) {
        token->len += result;

        result = scpiParser_parseProgramData(state, &tmp);
        if (tmp.type != SCPI_TOKEN_UNKNOWN) {
            token->len += result;
        } else {
            token->type = SCPI_TOKEN_UNKNOWN;
            token->len = 0;
            paramCount = -1;
            break;
        }
        paramCount++;
    }

    if (numberOfParameters != NULL) {
        *numberOfParameters = paramCount;
    }
    return token->len;
}

/**
 * Skip complete command line - program header and parameters
 * @param state
 * @param buffer
 * @param len
 * @return
 */
int scpiParser_detectProgramMessageUnit(scpi_parser_state_t * state, char * buffer, int len) {
    lex_state_t lex_state;
    scpi_token_t tmp;
    int result = 0;

    lex_state.buffer = lex_state.pos = buffer;
    lex_state.len = len;
    state->numberOfParameters = 0;

    /* ignore whitespace at the begginig */
    scpiLex_WhiteSpace(&lex_state, &tmp);

    if (scpiLex_ProgramHeader(&lex_state, &state->programHeader) >= 0) {
        if (scpiLex_WhiteSpace(&lex_state, &tmp) > 0) {
            scpiParser_parseAllProgramData(&lex_state, &state->programData, &state->numberOfParameters);
        } else {
            invalidateToken(&state->programData, lex_state.pos);
        }
    } else {
        invalidateToken(&state->programHeader, lex_state.buffer);
        invalidateToken(&state->programData, lex_state.buffer);
    }

    if (result == 0) result = scpiLex_NewLine(&lex_state, &tmp);
    if (result == 0) result = scpiLex_Semicolon(&lex_state, &tmp);

    if (!scpiLex_IsEos(&lex_state) && (result == 0)) {
        lex_state.pos++;

        state->programHeader.len = 1;
        state->programHeader.type = SCPI_TOKEN_INVALID;

        invalidateToken(&state->programData, lex_state.buffer);
    }

    if (SCPI_TOKEN_SEMICOLON == tmp.type) {
        state->termination = SCPI_MESSAGE_TERMINATION_SEMICOLON;
    } else if (SCPI_TOKEN_NL == tmp.type) {
        state->termination = SCPI_MESSAGE_TERMINATION_NL;
    } else {
        state->termination = SCPI_MESSAGE_TERMINATION_NONE;
    }

    return lex_state.pos - lex_state.buffer;
}

/**
 * Check current command
 *  - suitable for one handle to multiple commands
 * @param context
 * @param cmd
 * @return
 */
scpi_bool_t SCPI_IsCmd(scpi_t * context, const char * cmd) {
    const char * pattern;

    if (!context->param_list.cmd) {
        return FALSE;
    }

    pattern = context->param_list.cmd->pattern;
    return matchCommand(pattern, cmd, strlen(cmd), NULL, 0, 0);
}

#if USE_COMMAND_TAGS

/**
 * Return the .tag field of the matching scpi_command_t
 * @param context
 * @return
 */
int32_t SCPI_CmdTag(scpi_t * context) {
    if (context->param_list.cmd) {
        return context->param_list.cmd->tag;
    } else {
        return 0;
    }
}
#endif /* USE_COMMAND_TAGS */

scpi_bool_t SCPI_Match(const char * pattern, const char * value, size_t len) {
    return matchCommand(pattern, value, len, NULL, 0, 0);
}

scpi_bool_t SCPI_CommandNumbers(scpi_t * context, int32_t * numbers, size_t len, int32_t default_value) {
    return matchCommand(context->param_list.cmd->pattern, context->param_list.cmd_raw.data, context->param_list.cmd_raw.length, numbers, len, default_value);
}

/**
 * If SCPI_Parameter() returns FALSE, this function can detect, if the parameter
 * is just missing (TRUE) or if there was an error during processing of the command (FALSE)
 * @param parameter
 * @return
 */
scpi_bool_t SCPI_ParamIsValid(scpi_parameter_t * parameter) {
    return parameter->type == SCPI_TOKEN_UNKNOWN ? FALSE : TRUE;
}

/**
 * Returns TRUE if there was an error during parameter handling
 * @param context
 * @return
 */
scpi_bool_t SCPI_ParamErrorOccurred(scpi_t * context) {
    return context->cmd_error;
}

/**
 * Result binary array and swap bytes if needed (native endiannes != required endiannes)
 * @param context
 * @param array
 * @param count
 * @param item_size
 * @param format
 * @return
 */
static size_t produceResultArrayBinary(scpi_t * context, const void * array, size_t count, size_t item_size, scpi_array_format_t format) {

    if (SCPI_GetNativeFormat() == format) {
        switch (item_size) {
            case 1:
            case 2:
            case 4:
            case 8:
                return SCPI_ResultArbitraryBlock(context, array, count * item_size);
            default:
                SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
                return 0;
        }
    } else {
        size_t result = 0;
        size_t i;
        switch (item_size) {
            case 1:
            case 2:
            case 4:
            case 8:
                result += SCPI_ResultArbitraryBlockHeader(context, count * item_size);
                break;
            default:
                SCPI_ErrorPush(context, SCPI_ERROR_SYSTEM_ERROR);
                return 0;
        }

        switch (item_size) {
            case 1:
                result += SCPI_ResultArbitraryBlockData(context, array, count);
                break;
            case 2:
                for (i = 0; i < count; i++) {
                    uint16_t val = SCPI_Swap16(((uint16_t*) array)[i]);
                    result += SCPI_ResultArbitraryBlockData(context, &val, item_size);
                }
                break;
            case 4:
                for (i = 0; i < count; i++) {
                    uint32_t val = SCPI_Swap32(((uint32_t*) array)[i]);
                    result += SCPI_ResultArbitraryBlockData(context, &val, item_size);
                }
                break;
            case 8:
                for (i = 0; i < count; i++) {
                    uint64_t val = SCPI_Swap64(((uint64_t*) array)[i]);
                    result += SCPI_ResultArbitraryBlockData(context, &val, item_size);
                }
                break;
        }

        return result;
    }
}


#define RESULT_ARRAY(func) do {\
    size_t result = 0;\
    if (format == SCPI_FORMAT_ASCII) {\
        size_t i;\
        for (i = 0; i < count; i++) {\
            result += func(context, array[i]);\
        }\
    } else {\
        result = produceResultArrayBinary(context, array, count, sizeof(*array), format);\
    }\
    return result;\
} while(0)

/**
 * Result array of signed 8bit integers
 * @param context
 * @param array
 * @param count
 * @param format
 * @return
 */
size_t SCPI_ResultArrayInt8(scpi_t * context, const int8_t * array, size_t count, scpi_array_format_t format) {
    RESULT_ARRAY(SCPI_ResultInt8);
}

/**
 * Result array of unsigned 8bit integers
 * @param context
 * @param array
 * @param count
 * @param format
 * @return
 */
size_t SCPI_ResultArrayUInt8(scpi_t * context, const uint8_t * array, size_t count, scpi_array_format_t format) {
    RESULT_ARRAY(SCPI_ResultUInt8);
}

/**
 * Result array of signed 16bit integers
 * @param context
 * @param array
 * @param count
 * @param format
 * @return
 */
size_t SCPI_ResultArrayInt16(scpi_t * context, const int16_t * array, size_t count, scpi_array_format_t format) {
    RESULT_ARRAY(SCPI_ResultInt16);
}

/**
 * Result array of unsigned 16bit integers
 * @param context
 * @param array
 * @param count
 * @param format
 * @return
 */
size_t SCPI_ResultArrayUInt16(scpi_t * context, const uint16_t * array, size_t count, scpi_array_format_t format) {
    RESULT_ARRAY(SCPI_ResultUInt16);
}

/**
 * Result array of signed 32bit integers
 * @param context
 * @param array
 * @param count
 * @param format
 * @return
 */
size_t SCPI_ResultArrayInt32(scpi_t * context, const int32_t * array, size_t count, scpi_array_format_t format) {
    RESULT_ARRAY(SCPI_ResultInt32);
}

/**
 * Result array of unsigned 32bit integers
 * @param context
 * @param array
 * @param count
 * @param format
 * @return
 */
size_t SCPI_ResultArrayUInt32(scpi_t * context, const uint32_t * array, size_t count, scpi_array_format_t format) {
    RESULT_ARRAY(SCPI_ResultUInt32);
}

/**
 * Result array of signed 64bit integers
 * @param context
 * @param array
 * @param count
 * @param format
 * @return
 */
size_t SCPI_ResultArrayInt64(scpi_t * context, const int64_t * array, size_t count, scpi_array_format_t format) {
    RESULT_ARRAY(SCPI_ResultInt64);
}

/**
 * Result array of unsigned 64bit integers
 * @param context
 * @param array
 * @param count
 * @param format
 * @return
 */
size_t SCPI_ResultArrayUInt64(scpi_t * context, const uint64_t * array, size_t count, scpi_array_format_t format) {
    RESULT_ARRAY(SCPI_ResultUInt64);
}

/**
 * Result array of floats
 * @param context
 * @param array
 * @param count
 * @param format
 * @return
 */
size_t SCPI_ResultArrayFloat(scpi_t * context, const float * array, size_t count, scpi_array_format_t format) {
    RESULT_ARRAY(SCPI_ResultFloat);
}

/**
 * Result array of doubles
 * @param context
 * @param array
 * @param count
 * @param format
 * @return
 */
size_t SCPI_ResultArrayDouble(scpi_t * context, const double * array, size_t count, scpi_array_format_t format) {
    RESULT_ARRAY(SCPI_ResultDouble);
}

/*
 * Template macro to generate all SCPI_ParamArrayXYZ function
 */
#define PARAM_ARRAY_TEMPLATE(func) do{\
    if (format != SCPI_FORMAT_ASCII) return FALSE;\
    for (*o_count = 0; *o_count < i_count; (*o_count)++) {\
        if (!func(context, &data[*o_count], mandatory)) {\
            break;\
        }\
        mandatory = FALSE;\
    }\
    return mandatory ? FALSE : TRUE;\
}while(0)

/**
 * Read list of values up to i_count
 * @param context
 * @param data - array to fill
 * @param i_count - number of elements of data
 * @param o_count - real number of filled elements
 * @param mandatory
 * @return TRUE on success
 */
scpi_bool_t SCPI_ParamArrayInt32(scpi_t * context, int32_t *data, size_t i_count, size_t *o_count, scpi_array_format_t format, scpi_bool_t mandatory) {
    PARAM_ARRAY_TEMPLATE(SCPI_ParamInt32);
}

/**
 * Read list of values up to i_count
 * @param context
 * @param data - array to fill
 * @param i_count - number of elements of data
 * @param o_count - real number of filled elements
 * @param mandatory
 * @return TRUE on success
 */
scpi_bool_t SCPI_ParamArrayUInt32(scpi_t * context, uint32_t *data, size_t i_count, size_t *o_count, scpi_array_format_t format, scpi_bool_t mandatory) {
    PARAM_ARRAY_TEMPLATE(SCPI_ParamUInt32);
}

/**
 * Read list of values up to i_count
 * @param context
 * @param data - array to fill
 * @param i_count - number of elements of data
 * @param o_count - real number of filled elements
 * @param mandatory
 * @return TRUE on success
 */
scpi_bool_t SCPI_ParamArrayInt64(scpi_t * context, int64_t *data, size_t i_count, size_t *o_count, scpi_array_format_t format, scpi_bool_t mandatory) {
    PARAM_ARRAY_TEMPLATE(SCPI_ParamInt64);
}

/**
 * Read list of values up to i_count
 * @param context
 * @param data - array to fill
 * @param i_count - number of elements of data
 * @param o_count - real number of filled elements
 * @param mandatory
 * @return TRUE on success
 */
scpi_bool_t SCPI_ParamArrayUInt64(scpi_t * context, uint64_t *data, size_t i_count, size_t *o_count, scpi_array_format_t format, scpi_bool_t mandatory) {
    PARAM_ARRAY_TEMPLATE(SCPI_ParamUInt64);
}

/**
 * Read list of values up to i_count
 * @param context
 * @param data - array to fill
 * @param i_count - number of elements of data
 * @param o_count - real number of filled elements
 * @param mandatory
 * @return TRUE on success
 */
scpi_bool_t SCPI_ParamArrayFloat(scpi_t * context, float *data, size_t i_count, size_t *o_count, scpi_array_format_t format, scpi_bool_t mandatory) {
    PARAM_ARRAY_TEMPLATE(SCPI_ParamFloat);
}

/**
 * Read list of values up to i_count
 * @param context
 * @param data - array to fill
 * @param i_count - number of elements of data
 * @param o_count - real number of filled elements
 * @param mandatory
 * @return TRUE on success
 */
scpi_bool_t SCPI_ParamArrayDouble(scpi_t * context, double *data, size_t i_count, size_t *o_count, scpi_array_format_t format, scpi_bool_t mandatory) {
    PARAM_ARRAY_TEMPLATE(SCPI_ParamDouble);
}
