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
    /* Set ENABLE_ICSP_REALTIME_LOG=1 in project defines to enable real-time UART logging via ICSP.
     * 
     * This feature outputs live debug logs through the ICSP pins (for in-house debugging only).
     * When enabled, pin 4 of the ICSP header (RB0) is configured as U4TX @ 921600bps (8-N-1).
     * 
     * WARNING: This interferes with standard ICSP programming functionality.
     * Only works in non-debug builds (when __DEBUG is not defined).
     * Must be disabled (set to 0) before releasing to customers.
     * 
     * Define in project settings or makefile: -DENABLE_ICSP_REALTIME_LOG=1
     */
    #ifndef ENABLE_ICSP_REALTIME_LOG
        #define ENABLE_ICSP_REALTIME_LOG 0
    #endif
   
    #if defined(ENABLE_ICSP_REALTIME_LOG) && (ENABLE_ICSP_REALTIME_LOG == 1)
    #warning "ENABLE_ICSP_REALTIME_LOG is ENABLED. Set to 0 before release."
    #endif

    
    
    /**
     * @brief Library-level logging control macros and logging API
     * Provides compile-time logging enable/disable control at a per-library/module level.
     * -------------------------------
     * HOW TO USE LOG ENABLE/DISABLE:
     * -------------------------------
     * 1. Each library/module can define its own LOG_xxx flag
     *    (e.g., LOG_WIFI, LOG_SD) as either LOG_ENABLE or LOG_DISABLE.
     * 2. At the first line in the library/module source file(.c), define a local alias:
     *        #define LOG_EN LOG_WIFI
     *    This links the module's logging to its specific enable flag.
     * 3. When LOG_EN == LOG_ENABLE:
     *        - LOG_D(), LOG_I(), and LOG_E() macros will call LogMessage()
     *          and produce output.
     *    When LOG_EN == LOG_DISABLE:
     *        - LOG_D(), LOG_I(), and LOG_E() macros compile to nothing
     *          (zero runtime cost).
     * 4. Change the master switches below to enable/disable logs per module:
     *        #define LOG_WIFI    LOG_DISABLE   // Turn off WiFi logs
     *        #define LOG_SD      LOG_ENABLE    // Keep SD logs on
     *
     * Example in wifi_manager.c:
     *    #define LOG_EN LOG_WIFI
     *    LOG_D("WiFi connected, IP=%s\n", ipAddress);
     */
    
    //Log enable definitions
    #define LOG_DISABLE         0
    #define LOG_ENABLE          1

    //The master switches to enable/disable logs per module: 
    #define LOG_POWER           LOG_ENABLE
    #define LOG_WIFI            LOG_ENABLE
    #define LOG_BQ24297         LOG_ENABLE
    #define LOG_SD              LOG_ENABLE 
    #define LOG_USB             LOG_ENABLE
    #define LOG_SCPI            LOG_ENABLE

/**
 * Logs a formatted message
 * @param format
 * @return The number of characters written
 */
int LogMessage(const char* format, ...);

size_t LogMessageCount();

size_t LogMessagePop(uint8_t* buffer, size_t maxSize);


#if defined(LOG_EN) && (LOG_EN == LOG_ENABLE)
    #define LOG_D(fmt,...) LogMessage(fmt, ##__VA_ARGS__)
    #define LOG_E(fmt,...) LogMessage(fmt, ##__VA_ARGS__)
    #define LOG_I(fmt,...) LogMessage(fmt, ##__VA_ARGS__)
#else
    #define LOG_D(fmt,...)
    #define LOG_E(fmt,...)
    #define LOG_I(fmt,...)
#endif//#if (LOG_EN == LOG_ENABLE)

#ifdef	__cplusplus
}
#endif

#endif	/* LOGGER_H */

