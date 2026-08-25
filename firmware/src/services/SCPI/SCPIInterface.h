#include "libraries/scpi/libscpi/inc/scpi/scpi.h"
#include "Util/Logger.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#ifndef SCPIINTERFACE_H
#define	SCPIINTERFACE_H

#ifdef	__cplusplus
extern "C" {
#endif

    /**
     * One-time init for the shared SCPI response buffer mutex (statically
     * allocated, so this cannot fail). MUST be called exactly once during
     * app boot, before any transport creates its SCPI context and before
     * any SCPI command can be dispatched. Idempotent — safe to call more
     * than once, subsequent calls are no-ops.
     * Separate name from libscpi's `SCPI_Init(context)` which initializes
     * a per-transport context object.
     */
    void SCPI_ResponseBuf_Init(void);

    /**
     * #852: create the mutex that serialises OPER/QUES status-register
     * updates across the two SCPI transports. MUST be called before any
     * SCPI command can be dispatched; CreateSCPIContext calls it too, which
     * is what makes that guarantee structural (no context, no dispatch).
     * Idempotent -- subsequent calls are no-ops.
     */
    void SCPI_StatusLock_Init(void);

    /**
     * Build the device-wide model and serial-number strings used in *IDN?
     * (#436).  Reads BoardConfig once and writes module-scope statics that
     * CreateSCPIContext later passes to SCPI_Init.  MUST be called from
     * app_SystemInit, AFTER InitBoardConfig populates boardSerialNumber
     * AND BEFORE any transport task is created (USB/WiFi/SCPI tasks all
     * call CreateSCPIContext during their own initialization).  The
     * scheduler IS already running by this point — app_SystemInit runs
     * inside the priority-1 APP_FREERTOS_Tasks task — but no other task
     * has reached CreateSCPIContext yet because their xTaskCreate calls
     * happen later in the same app_SystemInit body, sequentially.
     * Idempotent: subsequent calls just rewrite the same value.
     */
    void SCPI_InitIdentification(void);

    /* #833: compute the program-image CRC that CONF:CAP:JSON? reports as
     * identity.firmware_crc32. Call ONCE from main(), BEFORE the scheduler
     * starts: it takes ~120 ms (measured on the bench -- a cold
     * CONF:CAP:JSON? was 128 ms against 5-12 ms warm), which is invisible in
     * a multi-second boot but far too long to spend inside a SCPI query, and
     * doing it here means the cache is written by a single caller with no
     * task running -- so the USB/WiFi SCPI concurrency question never
     * arises. Safe to call more than once; the second call is a no-op. */
    void SCPI_PrecomputeFirmwareImageCrc32(void);

    /**
     * Creates a new SCPI context object.
     * This allows us to have multiple independent consoles.
     * @param interface Defines the SCPI callback functions
     * @param user_context Additional information to pass to the client
     * @return A newly created SCPI context
     */
    scpi_t CreateSCPIContext(scpi_interface_t* interface, void* user_context);

    /**
     * Size of the shared SCPI response scratch buffer. Sized to hold the
     * largest known SCPI response (DaqifiOutMessage protobuf, currently
     * 2008 bytes). All callers that need ≥256 B of scratch should use this
     * buffer rather than stack-allocating, especially those reachable on
     * the TCP path where WifiTask stack is tight (see #347).
     */
    #define SCPI_RESPONSE_BUF_SIZE 2048U

    /**
     * #475 step 4 — minimum free heap (bytes) required to accept a new
     * streaming session start.  Refuses SYST:STR:START when
     * xPortGetFreeHeapSize() is below this threshold.  Designed to keep
     * the WiFi accept() and event-callback allocation path from
     * starving when prior pressure left the heap pinched.
     *
     * **2500 (lowered from 10000, 2026-05-31, user decision).** The
     * original 10 KB was an unmeasured "first defensive cut": boot-idle
     * HeapFree is only ~13 KB (CLAUDE.md "Heap Allocation Map"), so a
     * 10 KB floor sat just ~3 KB below idle and false-blocked legitimate
     * starts the moment any per-session pressure (the #490 leak) ate into
     * headroom — exactly what was observed while validating #520 (heap
     * slid to ~4.3 KB after a test session, blocking all WiFi starts).
     * The ~7 KB figure the 10 KB was built around is our own #475 note,
     * NOT a measured allocation profile.  2500 keeps a minimal guard
     * against a start with almost no heap while letting legitimate
     * streaming proceed; the real fix for accumulated pressure is the
     * #490 per-session leak, not a high start-time floor.  TRADEOFF: this
     * is below the ~7 KB the #475 symptom was noted at, so it relies on
     * #490 being the actual cause rather than a too-low start floor.
     * Re-raise (and/or measure the WINC accept() heap low-water) if a
     * #475-class TCP-unreachable wedge recurs at low heap.
     */
    #define MIN_HEAP_FREE_FOR_STREAM_START_BYTES 2500U

    /**
     * Acquire the shared SCPI response scratch buffer.
     * Blocks until the buffer mutex is available (portMAX_DELAY). Caller
     * MUST pair every successful Take with exactly one Give.
     * Returns NULL only if the mutex hasn't been created yet (shouldn't
     * happen after CreateSCPIContext has run); callers should treat NULL
     * as an internal error and return SCPI_RES_ERR.
     * @return Pointer to the shared buffer (SCPI_RESPONSE_BUF_SIZE bytes)
     *         or NULL on internal error.
     */
    uint8_t* SCPI_ResponseBuf_Take(void);

    /**
     * Release the shared SCPI response scratch buffer.
     * Must only be called after a successful SCPI_ResponseBuf_Take.
     */
    void SCPI_ResponseBuf_Give(void);

    /*! Function pointer type for transport-level write (no SCPI context) */
    typedef size_t (*ScpiTransportWriteFn)(const char* data, size_t len);

    /*!
     * Write SCPI response data with retry on buffer-full backpressure.
     * Retries up to 200 times with 5ms between attempts (1s max).
     * Handles partial writes.
     * @param writeFn  Transport write function (USB or WiFi buffer write)
     * @param data     Data to write
     * @param len      Number of bytes to write
     * @return Total bytes written (may be < len if retries exhausted)
     */
    size_t SCPI_WriteWithRetry(ScpiTransportWriteFn writeFn,
                               const char* data, size_t len);

    /**
     * Printf-style helper for writing formatted text to a SCPI response.
     * Uses an internal 192-byte buffer; each call is one write.
     *
     * @param context SCPI context
     * @param fmt printf format string
     *
     * #744: overflow used to be SILENT — it wrote sizeof(buf)-1 bytes, i.e.
     * dropped the last character, with no log, no SCPI error, and a void
     * return the caller could not check. That is the worst possible shape for
     * this particular buffer, because the dropped character is usually the
     * separating comma of a CONF:CAP:JSON? chunk: losing it does not shorten
     * one field, it makes the ENTIRE ~8 KB response unparseable, since the
     * next chunk's members run together with no separator.
     *
     * It already bit once. The `timing` chunk reached 189 of 192 bytes on the
     * shipped build and exactly 192 on the legacy 200 MHz build — it truncated
     * in one supported configuration, and was caught by an audit doing the
     * arithmetic across every rate, not by a test or a build warning.
     *
     * Truncation is now loud (LOG_E naming the format string and the length
     * needed) and detectable: the return value is vsnprintf's, so a caller
     * that cares can compare it against the buffer size. The output behaviour
     * is unchanged, so no existing caller has to react.
     *
     * @return bytes the format needed (>= sizeof(buf) means the response was
     *         truncated and is corrupt), or vsnprintf's negative error.
     */
    static inline int scpi_printf(scpi_t *context, const char *fmt, ...) {
        char buf[192];
        va_list args;
        va_start(args, fmt);
        int n = vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        if (n < 0) {
            LOG_E("scpi_printf: encoding error (fmt starts '%.32s')", fmt);
            return n;
        }
        if ((size_t)n >= sizeof(buf)) {
            LOG_E("scpi_printf TRUNCATED: needed %d of %u bytes - response is "
                  "corrupt, not merely short (fmt starts '%.48s')",
                  n, (unsigned)sizeof(buf), fmt);
            context->interface->write(context, buf, sizeof(buf) - 1);
            return n;
        }
        if (n > 0) {
            context->interface->write(context, buf, (size_t)n);
        }
        return n;
    }

    /**
     * Write a voltage value to SCPI output respecting VoltagePrecision.
     * precision 0: integer millivolts via SCPI_ResultInt32
     * precision 1-10: volts with N decimal places via SCPI_ResultCharacters
     * @param context SCPI context
     * @param voltage_v Voltage in volts
     * @param precision VoltagePrecision setting (0-10)
     */
    static inline void SCPI_ResultVoltage(scpi_t *context,
                                           double voltage_v,
                                           uint8_t precision) {
        if (precision == 0) {
            double voltage_mv = voltage_v * 1000.0;
            int32_t mv;
            if (voltage_mv > (double)INT32_MAX) mv = INT32_MAX;
            else if (voltage_mv < (double)INT32_MIN) mv = INT32_MIN;
            else mv = (int32_t)(voltage_mv >= 0.0 ? voltage_mv + 0.5 : voltage_mv - 0.5);
            SCPI_ResultInt32(context, mv);
        } else {
            char buf[32];
            int len = snprintf(buf, sizeof(buf), "%.*f", (int)precision, voltage_v);
            if (len > 0 && (size_t)len < sizeof(buf)) {
                SCPI_ResultCharacters(context, buf, (size_t)len);
            } else {
                SCPI_ResultDouble(context, voltage_v);
            }
        }
    }

    /**
     * Push a SCPI EXECUTION_ERROR with a logged reason (#262).
     * Every execution error should be discoverable via SYST:LOG? in addition
     * to SYST:ERR?. The reason string identifies the command and failure mode.
     * Expands LOG_E at the call site, so LOG_MODULE must be defined (SCPI files
     * all define LOG_MODULE_SCPI at the top).
     * @param context SCPI context
     * @param reason  short human-readable rejection reason, e.g. "streaming active"
     */
    static inline void SCPI_ExecutionError(scpi_t *context, const char *reason) {
        LOG_E("SCPI exec error: %s", reason);
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
    }

    /**
     * #847: report a refused streaming config-change claim.
     *
     * ORDERING CONTRACT (#862). A setter that can be refused by the claim must
     * take the claim BEFORE it looks at its arguments, and must report every
     * outcome from inside it. Mid-stream that makes the refusal independent of
     * the argument: `CONF:ADC:SAMC:DEDicated 99999` and `CONF:ADC:SAMC:DEDicated 5`
     * both answer -200, and a client reading the error queue learns the one
     * fact it can act on -- the device is streaming -- instead of being sent to
     * re-check a value that was never going to be applied.
     *
     * The split this replaced was the defect, not either ordering on its own.
     * Five setters validated first and so answered -222/-224 mid-stream while
     * three answered -200 on the same image, which made the error code useless
     * for deciding WHY a setter was refused: the answer depended on which
     * setter you called.
     *
     * Concretely: `Streaming_BeginConfigChange()` is the first statement of the
     * entry function, the body lives in a separate `...Claimed` function, and
     * `Streaming_EndConfigChange()` is on the single path out. Splitting the
     * body out is what makes the release unconditional -- these bodies have
     * multiple error returns, and a release written before each one is a leak
     * waiting for the next branch to be added.
     *
     * Two consequences that are deliberate, not oversights:
     *  - the claim is now held across the body's argument parsing and its
     *    LOG_I/LOG_E calls. Both are bounded and neither blocks, and the claim
     *    is a flag rather than a critical section, so this costs a concurrent
     *    START only microseconds of "config change in flight".
     *  - a mid-stream setter is refused without consuming its parameters. That
     *    is already how CONF:ADC:CHANnel and the seven SYSTem:MEMory:* setters
     *    behave (#847/#857) and libscpi discards the rest of the line either
     *    way.
     *
     * Written as a per-command wrapper at each site rather than one shared
     * runner. SCPI_MemRunClaimed is the runner form and works because its seven
     * bodies share a signature and a file; these do not -- SamcSetCommon's body
     * needs an `isDedicated` argument a `scpi_result_t (*)(scpi_t *)` cannot
     * carry, and SCPIADC.c cannot see a static in SCPIInterface.c. Promoting
     * the runner into this header would also move the target that
     * tools/lint/scpi_claim_path.py asserts on, which is a separate change
     * (#864) and not one to make while fixing an ordering bug.
     *
     * Shared so every converted cap-input setter reports the same two refusal
     * reasons the same way -- the old inline `IsEnabled || Running` guards each
     * wrote their own message, and the "another change is in flight" case is
     * new with the claim and would otherwise have been silent at some sites.
     *
     * Takes a bool rather than StreamingCfgClaim so this header does not have
     * to pull in streaming.h (which every SCPI translation unit would then
     * inherit, cap math included); the callers already have the enum in scope.
     * The cost is that the bool collapses "not OK" into exactly two messages --
     * if a THIRD refusal reason is ever added to StreamingCfgClaim it will
     * report as "streaming is active", so add a parameter here at the same
     * time rather than letting it fall through.
     *
     * @param context SCPI context
     * @param busy    true when the claim was refused because ANOTHER config
     *                change holds it, false when a session is armed/running
     * @param what    the command name, e.g. "CONF:ADC:CHANnel"
     */
    static inline scpi_result_t SCPI_RejectCfgClaim(scpi_t *context, bool busy,
                                                    const char *what) {
        if (busy) {
            LOG_E("%s rejected: another streaming config change is in flight "
                  "(retry)", what);
        } else {
            LOG_E("%s rejected: streaming is active (stop streaming first)",
                  what);
        }
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }

    /**
     * @brief The three outcomes of reading an OPTIONAL parameter (#874).
     *
     * `SCPI_ParamInt32(context, &v, FALSE)` collapses two of them into one
     * FALSE and the caller cannot tell them apart:
     *
     *   1. the parameter is ABSENT -- the caller used the short form; or
     *   2. the parameter is PRESENT but is not an integer.
     *
     * Commands that overload their argument count read that FALSE as case 1.
     * Where the short form does something DIFFERENT -- a bitmask write, an
     * all-channel write, an all-channel query -- case 2 therefore executes the
     * OTHER FORM using the first argument as its operand, and the client is
     * told only "-104", never that its request was replaced.
     * `CONF:ADC:CHAN 0,BANANA` disabled every analog channel that way (#874);
     * `DIO:PORt:STATe 3,BANANA` drove every DIO line from a mask.
     */
    typedef enum {
        SCPI_OPT_ABSENT = 0,   /**< no token -- the short form was asked for */
        SCPI_OPT_PRESENT,      /**< a token, and it parsed */
        SCPI_OPT_BAD           /**< a token, and it did NOT parse -- reject */
    } SCPI_OptionalParam;

    /**
     * @brief Read an optional integer, distinguishing ABSENT from UNPARSEABLE.
     *
     * THE CONTRACT (#874) is written down here ONCE; callers point at this
     * comment instead of restating it. Take the short-form branch on
     * SCPI_OPT_ABSENT only, and return SCPI_RES_ERR on SCPI_OPT_BAD.
     *
     * PRESENCE is decided by SCPI_Parameter(), not by the error queue, and
     * that is the whole point of the helper. An earlier revision asked
     * SCPI_ParamErrorOccurred() instead, which is NOT equivalent: libscpi
     * queues nothing for a decimal token that contains no integer digits.
     * `ParamSignToUInt32` (libscpi/src/parser.c) hands `.5` to `strBaseToInt32`,
     * which consumes zero digits and returns 0, so the parse fails SILENTLY --
     * `CONF:ADC:CHAN 0,.5` still reached the mask branch and disabled every
     * channel with no error at all. (Found by the #875 pre-merge audit.)
     *
     * On SCPI_OPT_BAD an error is guaranteed to be queued: libscpi's own
     * -104 where it pushed one, and ours where it did not -- so the client
     * always has something to read from SYSTem:ERRor?.
     *
     * The same "present-but-unparseable is a reject" idiom already guards
     * CONF:ADC:THREshold (#683), CONF:ADC:THREshold:CLEar, and the SPI/UART
     * optional arguments in SCPILAN.c; this helper is the shared home those
     * comments would have had if it had existed.
     *
     * @param context SCPI context
     * @param value   written only on SCPI_OPT_PRESENT
     */
    static inline SCPI_OptionalParam SCPI_OptionalParamInt32(scpi_t *context,
                                                             int32_t *value) {
        scpi_parameter_t param;
        if (!SCPI_Parameter(context, &param, FALSE)) {
            /* No token. SCPI_Parameter DOES queue an error for a malformed
             * separator (`CHAN 0,,`), and that is a reject, not an absence. */
            return SCPI_ParamErrorOccurred(context) ? SCPI_OPT_BAD
                                                    : SCPI_OPT_ABSENT;
        }
        if (!SCPI_ParamIsNumber(&param, FALSE)) {
            /* Classify exactly as SCPI_ParamInt32 does, because this helper
             * REPLACES it and must not quietly widen what the command accepts.
             * SCPI_ParamToInt32 alone would take the SUFFIX token type and
             * parse its leading digits, so `DIO:PORt:STATe 0,1V` would have
             * become state 1 instead of -131 (#875 pre-merge audit). */
            SCPI_ErrorPush(context, SCPI_ParamIsNumber(&param, TRUE)
                                    ? SCPI_ERROR_SUFFIX_NOT_ALLOWED
                                    : SCPI_ERROR_DATA_TYPE_ERROR);
            return SCPI_OPT_BAD;
        }
        if (!SCPI_ParamToInt32(context, &param, value)) {
            if (!SCPI_ParamErrorOccurred(context)) {
                /* The silent-failure case above. Push what libscpi would have
                 * pushed for a non-numeric token so every rejection is
                 * classifiable through SYSTem:ERRor?. */
                SCPI_ErrorPush(context, SCPI_ERROR_DATA_TYPE_ERROR);
            }
            return SCPI_OPT_BAD;
        }
        return SCPI_OPT_PRESENT;
    }

    /**
     * @brief `SCPI_OptionalParamInt32` for a double-valued optional parameter.
     *
     * Same contract, same reason -- see that comment. SCPI_ParamToDouble has
     * the same silent-failure shape (`strToDouble` returning 0 characters
     * consumed), so the queued-error guarantee is provided the same way.
     */
    static inline SCPI_OptionalParam SCPI_OptionalParamDouble(scpi_t *context,
                                                              double *value) {
        scpi_parameter_t param;
        if (!SCPI_Parameter(context, &param, FALSE)) {
            return SCPI_ParamErrorOccurred(context) ? SCPI_OPT_BAD
                                                    : SCPI_OPT_ABSENT;
        }
        if (!SCPI_ParamIsNumber(&param, FALSE)) {
            /* Same classification as SCPI_ParamDouble -- see the integer
             * helper above. `SOUR:VOLT:LEV 0,5mV` must be -131, not 5 V. */
            SCPI_ErrorPush(context, SCPI_ParamIsNumber(&param, TRUE)
                                    ? SCPI_ERROR_SUFFIX_NOT_ALLOWED
                                    : SCPI_ERROR_DATA_TYPE_ERROR);
            return SCPI_OPT_BAD;
        }
        if (!SCPI_ParamToDouble(context, &param, value)) {
            if (!SCPI_ParamErrorOccurred(context)) {
                SCPI_ErrorPush(context, SCPI_ERROR_DATA_TYPE_ERROR);
            }
            return SCPI_OPT_BAD;
        }
        return SCPI_OPT_PRESENT;
    }

#ifdef	__cplusplus
}
#endif

#endif	/* SCPIINTERFACE_H */

