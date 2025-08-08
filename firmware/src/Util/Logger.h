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
     * This feature outputs live debug logs through the ICSP pins (intended for in-house developer use only).
     * When enabled, pin 4 of the ICSP header (RB0) will be configured as U4TX to transmit UART logs @ 921600bps (8-N-1).
     * WARNING: This interferes with the standard ICSP debug/programming functionality and it will only be 
     * applied in production builds.
     *  
     * It is useful during development and troubleshooting, but must be disabled in production builds.
     * Note: 'ENABLE_ICSP_REALTIME_LOG' does NOT affect the existing SCPI logging functionality.
     */
    #define ENABLE_ICSP_REALTIME_LOG 0
   
    /* Reminder for developers: disable USE_USART_LOGGING after debugging is complete. */
    #if ENABLE_ICSP_REALTIME_LOG == 1
    #warning "ENABLE_ICSP_REALTIME_LOG is ENABLED. Please set to 0 before releasing to customers."
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

