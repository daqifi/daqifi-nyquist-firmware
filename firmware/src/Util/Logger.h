/* 
 * File:   Logger.h
 * Author: Daniel
 *
 * Created on October 5, 2016, 5:56 PM
 */

#include <stdbool.h>
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
     * @brief Log verbosity levels (higher = more verbose)
     * @{
     */
    #define LOG_LEVEL_NONE      0  /**< No logging - all macros compile to nothing */
    #define LOG_LEVEL_ERROR     1  /**< Errors only - unexpected failures, hardware errors */
    #define LOG_LEVEL_INFO      2  /**< Errors + Info - state changes, significant events */
    #define LOG_LEVEL_DEBUG     3  /**< All logging - verbose diagnostics, data flow tracing */
    /** @} */

    /**
     * @name Module IDs for runtime log level control
     * @brief Each module has a runtime log level in gLogLevels[], controllable
     *        via SCPI (SYST:LOG:LEV). Compile-time ceiling (LOG_LEVEL_xxx
     *        defines below) gates which macros are compiled in; runtime level
     *        controls which actually execute.
     * @{
     */
    typedef enum {
        LOG_MODULE_POWER = 0,   /**< PowerApi, BQ24297 */
        LOG_MODULE_WIFI,        /**< wifi_manager, wifi_tcp_server, WINC driver */
        LOG_MODULE_SD,          /**< sd_card_manager */
        LOG_MODULE_USB,         /**< UsbCdc */
        LOG_MODULE_SCPI,        /**< SCPIInterface, SCPILAN, SCPIADC, SCPIDAC, etc. */
        LOG_MODULE_ADC,         /**< ADC.c, AD7609 */
        LOG_MODULE_DAC,         /**< DAC7718 */
        LOG_MODULE_STREAM,      /**< streaming.c */
        LOG_MODULE_ENCODER,     /**< NanoPB_Encoder, csv_encoder, JSON_Encoder */
        LOG_MODULE_GENERAL,     /**< app_freertos, tasks, CircularBuffer, etc. */
        LOG_MODULE_COUNT        /**< Must be last */
    } LogModule_t;
    /** @} */

    /**
     * @brief Runtime log levels array — one entry per LogModule_t.
     *        Initialized to LOG_LEVEL_ERROR. Volatile: written by SCPI task,
     *        read by all tasks. Single-word reads are atomic on PIC32MZ.
     */
    extern volatile uint8_t gLogLevels[LOG_MODULE_COUNT];

    /**
     * @brief Set the runtime log level for a specific module.
     *        Clamps to the module's compile-time ceiling (e.g. USB stays
     *        at ERROR due to issue #191).
     * @param module  Module ID (0 to LOG_MODULE_COUNT-1)
     * @param level   Desired level (LOG_LEVEL_NONE to LOG_LEVEL_DEBUG)
     */
    void Logger_SetLevel(LogModule_t module, uint8_t level);

    /**
     * @brief Get the current runtime log level for a module.
     */
    uint8_t Logger_GetLevel(LogModule_t module);

    /**
     * @brief Set all modules to the same runtime log level.
     *        Each module is still clamped to its compile-time ceiling.
     */
    void Logger_SetAllLevels(uint8_t level);

    /**
     * @brief Get the short name string for a module ID (e.g. "POWER", "WIFI").
     * @return Static string, or "?" if invalid.
     */
    const char* Logger_GetModuleName(LogModule_t module);

    /**
     * @brief Look up a module ID by name (case-insensitive).
     * @param name    Module name string
     * @param len     Length of name string
     * @param module  Output: module ID if found
     * @return true if found, false if unknown name
     */
    bool Logger_FindModule(const char* name, size_t len, LogModule_t* module);

    /**
     * @brief Get the compile-time ceiling for a module.
     */
    uint8_t Logger_GetCeiling(LogModule_t module);

    /**
     * @name One-shot log suppression
     * @brief Bit indices for LOG_x_ONCE macros. Each index represents a
     *        unique log call site that fires at most once until reset.
     *        Primary use: ISR context where a high-frequency error would
     *        flood the 8-entry deferred queue. Also works from task context.
     *        Reset automatically on SYST:LOG? (dump) and SYST:LOG:CLEAR.
     *
     *        The RMW on gLogOneShot (|=) is not protected by a critical
     *        section. On PIC32MZ, a higher-priority ISR could race and
     *        lose a bit — worst case is one extra duplicate message per
     *        race. Acceptable for a logging system.
     * @{
     */
    typedef enum {
        LOG_ONCE_USB_WRITE_MISMATCH = 0,  /**< UsbCdc.c: USB CDC write length mismatch */
        /* Add new entries above this line */
        LOG_ONCE_COUNT                     /**< Must be <= 32 */
    } LogOnceBit_t;

    #if (LOG_ONCE_COUNT > 32)
        #error "LogOnceBit_t exceeds 32 entries; gLogOneShot is uint32_t"
    #endif

    /**
     * @brief One-shot bitmask — bit N set means LOG_x_ONCE(N,...) has
     *        already fired and is suppressed until reset.
     */
    extern volatile uint32_t gLogOneShot;

    /**
     * @brief Clear all one-shot suppression bits.
     *        Called automatically from LogMessageDump() and LogMessageClear().
     *        32-bit write is atomic on PIC32MZ.
     */
    void Logger_ResetOneShots(void);
    /** @} */

    /**
     * @name Session-scoped one-shot log suppression
     * @brief Bit indices for LOG_x_SESSION macros. Each index represents a
     *        unique log call site that fires at most once per streaming session.
     *        Reset at streaming start via Logger_ResetSessionOneShots().
     *        Used by the streaming engine to prevent flooding the 64-message
     *        log buffer with repeated per-sample errors (pool exhaustion,
     *        buffer overflows, encoder failures, etc.).
     * @{
     */
    typedef enum {
        LOG_SESSION_POOL_EXHAUST = 0,     /**< streaming.c: sample pool exhausted */
        LOG_SESSION_QUEUE_OVERFLOW,        /**< streaming.c: sample queue full */
        LOG_SESSION_USB_DROP,              /**< streaming.c: USB buffer overflow */
        LOG_SESSION_WIFI_DROP,             /**< streaming.c: WiFi buffer overflow */
        LOG_SESSION_SD_DROP,               /**< streaming.c: SD write overflow */
        LOG_SESSION_ENCODER_FAIL,          /**< streaming.c: encoder returned 0 bytes */
        LOG_SESSION_STREAM_STARTED,        /**< streaming.c: "Streaming started" info */
        /* Add new entries above this line */
        LOG_SESSION_COUNT                  /**< Must be <= 32 */
    } LogSessionBit_t;

    #if (LOG_SESSION_COUNT > 32)
        #error "LogSessionBit_t exceeds 32 entries; gSessionOneShot is uint32_t"
    #endif

    /**
     * @brief Session-scoped one-shot bitmask — bit N set means
     *        LOG_x_SESSION(N,...) has already fired this session.
     */
    extern volatile uint32_t gSessionOneShot;

    /**
     * @brief Clear all session one-shot bits.
     *        Called from Streaming_ClearStats() at streaming start.
     *        32-bit write is atomic on PIC32MZ.
     */
    void Logger_ResetSessionOneShots(void);
    /** @} */

    /**
     * @brief Get the count of ISR log messages dropped (queue full or
     *        not initialized). Diagnostic hint — not a precise counter
     *        due to non-atomic RMW from ISR.
     */
    uint32_t Logger_GetIsrDropCount(void);

    /**
     * @name Per-module compile-time ceilings
     * @brief These define which LOG_E/LOG_I/LOG_D calls are compiled into the
     *        binary. Set to LOG_LEVEL_DEBUG to allow full runtime control.
     *        Set to LOG_LEVEL_NONE to strip all logging for a module.
     *        The runtime level (gLogLevels[]) is clamped to this ceiling.
     * @{
     */
    #ifndef LOG_LEVEL_POWER
        #define LOG_LEVEL_POWER     LOG_LEVEL_DEBUG
    #endif
    #ifndef LOG_LEVEL_WIFI
        #define LOG_LEVEL_WIFI      LOG_LEVEL_DEBUG
    #endif
    #ifndef LOG_LEVEL_BQ24297
        #define LOG_LEVEL_BQ24297   LOG_LEVEL_DEBUG
    #endif
    #ifndef LOG_LEVEL_SD
        #define LOG_LEVEL_SD        LOG_LEVEL_DEBUG
    #endif
    #ifndef LOG_LEVEL_USB
        #define LOG_LEVEL_USB       LOG_LEVEL_DEBUG
    #endif
    /* LOG_E/LOG_I/LOG_D are ISR-aware: they detect ISR context via
     * FreeRTOS uxInterruptNesting and route through a deferred queue
     * (no mutex, no vsnprintf in ISR). Format args are ignored in ISR
     * context — use static strings. See issue #191. */
    #ifndef LOG_LEVEL_SCPI
        #define LOG_LEVEL_SCPI      LOG_LEVEL_DEBUG
    #endif
    #ifndef LOG_LEVEL_ADC
        #define LOG_LEVEL_ADC       LOG_LEVEL_DEBUG
    #endif
    #ifndef LOG_LEVEL_DAC
        #define LOG_LEVEL_DAC       LOG_LEVEL_DEBUG
    #endif
    /** @} */

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

/**
 * @brief Initialize the ISR log queue and deferred drain task.
 *        Called from LogMessageInit(). Safe to call multiple times.
 */
void LogIsrInit(void);

/** @brief Depth of the ISR log queue (number of messages) */
#define LOG_ISR_QUEUE_DEPTH 8

// ── Per-file compile-time ceiling ────────────────────────────────
// Each .c file defines LOG_LVL to its module's compile-time ceiling
// BEFORE including Logger.h (e.g. #define LOG_LVL LOG_LEVEL_WIFI).
// If not defined, defaults to LOG_LEVEL_ERROR (safe for Harmony files).
#ifndef LOG_LVL
    #define LOG_LVL LOG_LEVEL_ERROR
#endif

// Each .c file defines LOG_MODULE to its LogModule_t enum value
// BEFORE including Logger.h (e.g. #define LOG_MODULE LOG_MODULE_WIFI).
// If not defined, defaults to LOG_MODULE_GENERAL.
#ifndef LOG_MODULE
    #define LOG_MODULE LOG_MODULE_GENERAL
#endif

// Helper to safely discard arguments when logging disabled
#define LOG_NOOP(...) do { } while(0)

// LOG_E: compiled in if compile-time ceiling >= ERROR, checks runtime level
#if (LOG_LVL >= LOG_LEVEL_ERROR)
    #define LOG_E(fmt,...) do { if (gLogLevels[LOG_MODULE] >= LOG_LEVEL_ERROR) LogMessage(fmt, ##__VA_ARGS__); } while(0)
#else
    #define LOG_E(...) LOG_NOOP(__VA_ARGS__)
#endif

// LOG_I: compiled in if compile-time ceiling >= INFO, checks runtime level
#if (LOG_LVL >= LOG_LEVEL_INFO)
    #define LOG_I(fmt,...) do { if (gLogLevels[LOG_MODULE] >= LOG_LEVEL_INFO) LogMessage(fmt, ##__VA_ARGS__); } while(0)
#else
    #define LOG_I(...) LOG_NOOP(__VA_ARGS__)
#endif

// LOG_D: compiled in if compile-time ceiling >= DEBUG, checks runtime level
#if (LOG_LVL >= LOG_LEVEL_DEBUG)
    #define LOG_D(fmt,...) do { if (gLogLevels[LOG_MODULE] >= LOG_LEVEL_DEBUG) LogMessage(fmt, ##__VA_ARGS__); } while(0)
#else
    #define LOG_D(...) LOG_NOOP(__VA_ARGS__)
#endif

// NOTE: LOG_E/LOG_I/LOG_D are ISR-aware — they automatically detect ISR
// context via uxInterruptNesting and route through the deferred queue.
// No separate ISR macros needed. Format args are ignored in ISR context
// (raw format string is logged). Use static strings in ISR handlers.

// ── One-shot variants ───────────────────────────────────────────────
// LOG_x_ONCE(bit, fmt, ...): like LOG_x but fires only once per bit
// until reset (SYST:LOG? or SYST:LOG:CLEAR). Intended for ISR context
// to prevent queue flooding, but works anywhere.
// ISR guard: skips vararg evaluation in ISR context (use static strings).
// Bounds check: bit must be < 32 (uint32_t bitmask).

#if (LOG_LVL >= LOG_LEVEL_ERROR)
    #define LOG_E_ONCE(bit, fmt,...) do { \
        if ((unsigned)(bit) < 32u && \
            gLogLevels[LOG_MODULE] >= LOG_LEVEL_ERROR && \
            !(gLogOneShot & (1u << (bit)))) { \
            gLogOneShot |= (1u << (bit)); \
            if (uxInterruptNesting != 0u) { \
                LogMessage(fmt); \
            } else { \
                LogMessage(fmt, ##__VA_ARGS__); \
            } \
        } \
    } while(0)
#else
    #define LOG_E_ONCE(bit, ...) LOG_NOOP(__VA_ARGS__)
#endif

#if (LOG_LVL >= LOG_LEVEL_INFO)
    #define LOG_I_ONCE(bit, fmt,...) do { \
        if ((unsigned)(bit) < 32u && \
            gLogLevels[LOG_MODULE] >= LOG_LEVEL_INFO && \
            !(gLogOneShot & (1u << (bit)))) { \
            gLogOneShot |= (1u << (bit)); \
            if (uxInterruptNesting != 0u) { \
                LogMessage(fmt); \
            } else { \
                LogMessage(fmt, ##__VA_ARGS__); \
            } \
        } \
    } while(0)
#else
    #define LOG_I_ONCE(bit, ...) LOG_NOOP(__VA_ARGS__)
#endif

#if (LOG_LVL >= LOG_LEVEL_DEBUG)
    #define LOG_D_ONCE(bit, fmt,...) do { \
        if ((unsigned)(bit) < 32u && \
            gLogLevels[LOG_MODULE] >= LOG_LEVEL_DEBUG && \
            !(gLogOneShot & (1u << (bit)))) { \
            gLogOneShot |= (1u << (bit)); \
            if (uxInterruptNesting != 0u) { \
                LogMessage(fmt); \
            } else { \
                LogMessage(fmt, ##__VA_ARGS__); \
            } \
        } \
    } while(0)
#else
    #define LOG_D_ONCE(bit, ...) LOG_NOOP(__VA_ARGS__)
#endif

// ── Session-scoped one-shot variants ────────────────────────────────
// LOG_x_SESSION(bit, fmt, ...): like LOG_x but fires only once per
// streaming session. Reset via Logger_ResetSessionOneShots() at stream
// start. Uses gSessionOneShot bitmask (separate from gLogOneShot).
// Task-context only — no ISR guard needed.

#if (LOG_LVL >= LOG_LEVEL_ERROR)
    #define LOG_E_SESSION(bit, fmt,...) do { \
        if ((unsigned)(bit) < 32u && \
            gLogLevels[LOG_MODULE] >= LOG_LEVEL_ERROR && \
            !(gSessionOneShot & (1u << (bit)))) { \
            gSessionOneShot |= (1u << (bit)); \
            LogMessage(fmt, ##__VA_ARGS__); \
        } \
    } while(0)
#else
    #define LOG_E_SESSION(bit, ...) LOG_NOOP(__VA_ARGS__)
#endif

#if (LOG_LVL >= LOG_LEVEL_INFO)
    #define LOG_I_SESSION(bit, fmt,...) do { \
        if ((unsigned)(bit) < 32u && \
            gLogLevels[LOG_MODULE] >= LOG_LEVEL_INFO && \
            !(gSessionOneShot & (1u << (bit)))) { \
            gSessionOneShot |= (1u << (bit)); \
            LogMessage(fmt, ##__VA_ARGS__); \
        } \
    } while(0)
#else
    #define LOG_I_SESSION(bit, ...) LOG_NOOP(__VA_ARGS__)
#endif

#if (LOG_LVL >= LOG_LEVEL_DEBUG)
    #define LOG_D_SESSION(bit, fmt,...) do { \
        if ((unsigned)(bit) < 32u && \
            gLogLevels[LOG_MODULE] >= LOG_LEVEL_DEBUG && \
            !(gSessionOneShot & (1u << (bit)))) { \
            gSessionOneShot |= (1u << (bit)); \
            LogMessage(fmt, ##__VA_ARGS__); \
        } \
    } while(0)
#else
    #define LOG_D_SESSION(bit, ...) LOG_NOOP(__VA_ARGS__)
#endif


#ifdef	__cplusplus
}
#endif

#endif	/* LOGGER_H */

