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

    /**
     * Printf-style helper for writing formatted text to a SCPI response.
     * Uses an internal 128-byte buffer; each call is one write.
     * @param context SCPI context
     * @param fmt printf format string
     */
    static inline void scpi_printf(scpi_t *context, const char *fmt, ...) {
        char buf[128];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        context->interface->write(context, buf, strlen(buf));
    }

#ifdef	__cplusplus
}
#endif

#endif	/* SCPIINTERFACE_H */

