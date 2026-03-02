#include "libraries/scpi/libscpi/inc/scpi/scpi.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef SCPIINTERFACE_H
#define	SCPIINTERFACE_H

#ifdef	__cplusplus
extern "C" {
#endif

    /**
     * Creates a new SCPI context object.
     * This allows us to have multiple independent consoles.
     * @param interface Defines the SCPI callback functions
     * @param user_context Additional information to pass to the client
     * @return A newly created SCPI context
     */
    scpi_t CreateSCPIContext(scpi_interface_t* interface, void* user_context);

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

#ifdef	__cplusplus
}
#endif

#endif	/* SCPIINTERFACE_H */

