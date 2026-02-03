/* 
 * File:   Logger.h
 * Author: Daniel
 *
 * Created on October 5, 2016, 5:56 PM
 */

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include "FreeRTOS.h"
#include "semphr.h"
#include "libraries/scpi/libscpi/inc/scpi/types.h"

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
 * Provides compile-time logging control at a per-library/module level using log levels.
 *
 * -------------------------------
 * HOW TO USE LOG LEVELS:
 * -------------------------------
 * 1. Each library/module can define its own log level macro
 *    (e.g., LOG_LEVEL_WIFI, LOG_LEVEL_SD) to one of:
 *        LOG_LEVEL_NONE, LOG_LEVEL_ERROR, LOG_LEVEL_INFO, LOG_LEVEL_DEBUG
 *
 * 2. At the first line in the library/module source file (.c), define a local alias:
 *        #define LOG_LVL LOG_LEVEL_WIFI
 *    This links the module’s logging to its specific log level setting.
 *
 * 3. Depending on the selected LOG_LVL:
 *        - LOG_E() is enabled when LOG_LVL >= LOG_LEVEL_ERROR
 *        - LOG_I() is enabled when LOG_LVL >= LOG_LEVEL_INFO
 *        - LOG_D() is enabled when LOG_LVL >= LOG_LEVEL_DEBUG
 *    If LOG_LVL == LOG_LEVEL_NONE, all logging macros compile to nothing
 *    (zero runtime cost).
 *
 * 4. Change the per-module log level definitions below to control verbosity:
 *        #define LOG_LEVEL_WIFI    LOG_LEVEL_NONE    // Disable WiFi logs
 *        #define LOG_LEVEL_SD      LOG_LEVEL_DEBUG   // Enable detailed SD logs
 *
 * Example in wifi_manager.c:
 *    #define LOG_LVL LOG_LEVEL_WIFI
 *    LOG_I("WiFi connected, IP=%s\n", ipAddress);
 */
    
    
    /**
     * @name Log Level Definitions
     * @brief Compile-time log verbosity levels (higher = more verbose)
     * @{
     */
    #define LOG_LEVEL_NONE      0  /**< No logging - all macros compile to nothing */
    #define LOG_LEVEL_ERROR     1  /**< Errors only - unexpected failures, hardware errors */
    #define LOG_LEVEL_INFO      2  /**< Errors + Info - state changes, significant events */
    #define LOG_LEVEL_DEBUG     3  /**< All logging - verbose diagnostics, data flow tracing */
    /** @} */

    
    //Per-module logging level control
    #ifndef LOG_LEVEL_POWER
        #define LOG_LEVEL_POWER     LOG_LEVEL_ERROR
    #endif
    #ifndef LOG_LEVEL_WIFI
        #define LOG_LEVEL_WIFI      LOG_LEVEL_ERROR
    #endif
    #ifndef LOG_LEVEL_BQ24297
        #define LOG_LEVEL_BQ24297   LOG_LEVEL_ERROR
    #endif
    #ifndef LOG_LEVEL_SD
        #define LOG_LEVEL_SD        LOG_LEVEL_ERROR
    #endif
    #ifndef LOG_LEVEL_USB
        #define LOG_LEVEL_USB       LOG_LEVEL_ERROR
    #endif
    #ifndef LOG_LEVEL_SCPI
        #define LOG_LEVEL_SCPI      LOG_LEVEL_ERROR
    #endif
    #ifndef LOG_LEVEL_ADC
        #define LOG_LEVEL_ADC       LOG_LEVEL_ERROR
    #endif
    #ifndef LOG_LEVEL_DAC
        #define LOG_LEVEL_DAC       LOG_LEVEL_ERROR
    #endif

/** @brief Maximum number of log entries in circular buffer */
#define LOG_MAX_ENTRY_COUNT 64

/** @brief Maximum size of a single log message in bytes (including \r\n\0) */
#define LOG_MESSAGE_SIZE 128

/**
 * @brief Single log entry in the circular buffer
 */
typedef struct {
    char message[LOG_MESSAGE_SIZE];  /**< Null-terminated log message */
} LogEntry;

/**
 * @brief Circular log buffer with thread-safe access
 *
 * Implements a fixed-size circular buffer that drops the oldest entry
 * when full. Protected by a FreeRTOS mutex for thread safety.
 */
typedef struct {
    LogEntry entries[LOG_MAX_ENTRY_COUNT];  /**< Array of log entries */
    uint8_t head;                            /**< Index for next write */
    uint8_t tail;                            /**< Index of oldest entry */
    uint8_t count;                           /**< Current number of entries */
    SemaphoreHandle_t mutex;                 /**< Mutex for thread safety */
} LogBuffer;

/**
 * @brief Logs a formatted message to the circular buffer.
 *        Messages are automatically terminated with \r\n.
 *        Thread-safe; can be called from any task context.
 *        ISR-safe; detects ISR and outputs to ICSP only (if enabled).
 *
 * @param format Printf-style format string
 * @param ...    Variable arguments matching format specifiers
 * @return int   Number of characters written, or 0 on failure
 */
int LogMessage(const char* format, ...) __attribute__((format(printf, 1, 2)));

/**
 * @brief Returns the current number of messages in the log buffer.
 *
 * @return size_t Number of stored log messages (0 to LOG_MAX_ENTRY_COUNT)
 */
size_t LogMessageCount(void);

/**
 * @brief Dumps all log messages to SCPI interface and clears the buffer.
 *        Messages are written oldest-first, with flush after each message.
 *
 * @param context SCPI context for output (must not be NULL)
 */
void LogMessageDump(scpi_t * context);

/**
 * @brief Initializes the log buffer and mutex (idempotent).
 *        Called automatically on first log; safe to call multiple times.
 *        Subsequent calls have no effect (preserves buffered logs).
 */
void LogMessageInit(void);

/**
 * @brief Clears all log messages from the buffer (thread-safe).
 *        Does not affect the mutex or initialization state.
 */
void LogMessageClear(void);

// Helper macro to get the log level for the current module
// Each module should define LOG_LVL to its specific level (e.g., #define LOG_LVL LOG_LEVEL_WIFI)
// If not defined, defaults to ERROR level (safe for production, Harmony compatibility)
#ifndef LOG_LVL
    #define LOG_LVL LOG_LEVEL_ERROR
#endif

// Helper to safely discard arguments when logging disabled
#define LOG_NOOP(...) do { } while(0)

// LOG_E is enabled at ERROR level and above
#if (LOG_LVL >= LOG_LEVEL_ERROR)
    #define LOG_E(fmt,...) do { LogMessage(fmt, ##__VA_ARGS__); } while(0)
#else
    #define LOG_E(...) LOG_NOOP(__VA_ARGS__)
#endif

// LOG_I is enabled at INFO level and above
#if (LOG_LVL >= LOG_LEVEL_INFO)
    #define LOG_I(fmt,...) do { LogMessage(fmt, ##__VA_ARGS__); } while(0)
#else
    #define LOG_I(...) LOG_NOOP(__VA_ARGS__)
#endif

// LOG_D is enabled at DEBUG level
#if (LOG_LVL >= LOG_LEVEL_DEBUG)
    #define LOG_D(fmt,...) do { LogMessage(fmt, ##__VA_ARGS__); } while(0)
#else
    #define LOG_D(...) LOG_NOOP(__VA_ARGS__)
#endif


#ifdef	__cplusplus
}
#endif

#endif	/* LOGGER_H */

