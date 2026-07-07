/* ==========================================================================
 * Host-test stub for firmware/src/Util/Logger.h.
 *
 * The real Logger.h drags in FreeRTOS.h, semphr.h and libscpi types — none of
 * which exist on the PC host. CircularBuffer.c only ever calls LOG_E() (one
 * OSAL_Malloc-failure path), so a no-op variadic macro is all that's needed.
 * Defining these here (rather than pulling the real header) keeps the unit
 * under test free of firmware/RTOS dependencies.
 * ========================================================================== */
#ifndef LOGGER_HOST_STUB_H
#define LOGGER_HOST_STUB_H

#define LOG_E(...) ((void)0)
#define LOG_I(...) ((void)0)
#define LOG_D(...) ((void)0)

#endif /* LOGGER_HOST_STUB_H */
