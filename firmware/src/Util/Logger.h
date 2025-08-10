/* 
 * File:   Logger.h
 * Author: Daniel
 *
 * Created on October 5, 2016, 5:56 PM
 */

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#ifndef LOGGER_H
#define	LOGGER_H

#ifdef	__cplusplus
extern "C" {
#endif
    
    
    /////////////////////////////////////////////////////
    /* Set to 1 to enable, or 0 to disable real-time UART logging via ICSP.
     * 
     * This feature outputs live debug logs through the ICSP pins (for in-house debugging only).
     * When enabled, pin 4 of the ICSP header (RB0) is configured as U4TX @ 921600bps (8-N-1).
     * 
     * WARNING: This interferes with standard ICSP programming functionality.
     * Only works in non-debug builds (when __DEBUG is not defined).
     * Must be disabled (set to 0) before releasing to customers.
     */
    #define ENABLE_ICSP_REALTIME_LOG 0
   
    #if ENABLE_ICSP_REALTIME_LOG == 1
    #warning "ENABLE_ICSP_REALTIME_LOG is ENABLED. Set to 0 before release."
    #endif

/**
 * Logs a formatted message
 * @param format
 * @return The number of characters written
 */
int LogMessage(const char* format, ...);

size_t LogMessageCount();

size_t LogMessagePop(uint8_t* buffer, size_t maxSize);

#define LOG_D(fmt,...) LogMessage(fmt, ##__VA_ARGS__)
#define LOG_E(fmt,...) LogMessage(fmt, ##__VA_ARGS__)
#define LOG_I(fmt,...) LogMessage(fmt, ##__VA_ARGS__)

#ifdef	__cplusplus
}
#endif

#endif	/* LOGGER_H */

