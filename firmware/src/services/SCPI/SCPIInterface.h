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
     * @param context SCPI context
     * @param fmt printf format string
     */
    static inline void scpi_printf(scpi_t *context, const char *fmt, ...) {
        char buf[192];
        va_list args;
        va_start(args, fmt);
        int n = vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        if (n > 0) {
            size_t len = ((size_t)n < sizeof(buf)) ? (size_t)n : sizeof(buf) - 1;
            context->interface->write(context, buf, len);
        }
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

#ifdef	__cplusplus
}
#endif

#endif	/* SCPIINTERFACE_H */

