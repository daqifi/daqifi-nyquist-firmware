#include "Logger.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <ctype.h>
#include <xc.h>
#include "queue.h"
#include "task.h"
#include "state/data/BoardData.h"


/**
 * @file Logger.c
 * @brief Logging buffer implementation using FreeRTOS APIs.
 *
 * This module provides a thread-safe logging system for embedded applications.
 * It maintains a circular buffer of up to LOG_MAX_ENTRY_COUNT messages.
 *
 * Key Features:
 * - Stores up to 64 log entries (defined by LOG_MAX_ENTRY_COUNT)
 * - Each message is limited to LOG_MESSAGE_SIZE bytes
 * - Automatically drops the oldest message when the buffer is full
 * - Ensures all messages end with "\r\n" for terminal compatibility (e.g., PuTTY)
 * - Supports printf-style formatting via LogMessage()
 * - Optionally transmits logs over UART4 (ICSP pin) if enabled
 * - Dumps all stored messages to SCPI interface when LogMessageDump() is called
 * - Runtime-configurable per-module log levels via SCPI (SYST:LOG:LEV)
 *
 * Usage:
 * - Call LogMessageInit() once at startup (automatically handled on first log)
 * - Use LogMessage("format", ...) to add messages
 * - Call LogMessageDump(context) to flush and reset the buffer
 * - Use Logger_SetLevel() / SYST:LOG:LEV to change log verbosity at runtime
 */

#ifndef min
    #define min(x,y) (((x) <= (y)) ? (x) : (y))
#endif

#ifndef max
    #define max(x,y) (((x) >= (y)) ? (x) : (y))
#endif
#define UNUSED(x) (void)(x)

/* ── Runtime log levels ──────────────────────────────────────────────
 * One entry per LogModule_t, initialized to LOG_LEVEL_ERROR (matching
 * previous compile-time default). Written by SCPI task, read by all
 * tasks. Single-byte reads/writes are atomic on PIC32MZ.
 */
volatile uint8_t gLogLevels[LOG_MODULE_COUNT] = {
    [LOG_MODULE_POWER]   = LOG_LEVEL_ERROR,
    [LOG_MODULE_WIFI]    = LOG_LEVEL_ERROR,
    [LOG_MODULE_SD]      = LOG_LEVEL_ERROR,
    [LOG_MODULE_USB]     = LOG_LEVEL_ERROR,
    [LOG_MODULE_SCPI]    = LOG_LEVEL_ERROR,
    [LOG_MODULE_ADC]     = LOG_LEVEL_ERROR,
    [LOG_MODULE_DAC]     = LOG_LEVEL_ERROR,
    [LOG_MODULE_STREAM]  = LOG_LEVEL_ERROR,
    [LOG_MODULE_ENCODER] = LOG_LEVEL_ERROR,
    [LOG_MODULE_GENERAL] = LOG_LEVEL_ERROR,
};

/** Runtime ceilings per module — Logger_SetLevel() clamps to these.
 *  All modules allow full DEBUG via SCPI.
 *  WARNING: Do not call LOG_E/LOG_I/LOG_D from ISR context — LogIsInISR()
 *  detection fails when Harmony clears MIPS EXL/ERL (issue #191). The fix
 *  is a deferred logging task (also #191). Until then, ensure no log calls
 *  exist in true ISR handlers. */
static const uint8_t gLogCeilings[LOG_MODULE_COUNT] = {
    [LOG_MODULE_POWER]   = LOG_LEVEL_DEBUG,
    [LOG_MODULE_WIFI]    = LOG_LEVEL_DEBUG,
    [LOG_MODULE_SD]      = LOG_LEVEL_DEBUG,
    [LOG_MODULE_USB]     = LOG_LEVEL_DEBUG,
    [LOG_MODULE_SCPI]    = LOG_LEVEL_DEBUG,
    [LOG_MODULE_ADC]     = LOG_LEVEL_DEBUG,
    [LOG_MODULE_DAC]     = LOG_LEVEL_DEBUG,
    [LOG_MODULE_STREAM]  = LOG_LEVEL_DEBUG,
    [LOG_MODULE_ENCODER] = LOG_LEVEL_DEBUG,
    [LOG_MODULE_GENERAL] = LOG_LEVEL_DEBUG,
};

/** Short names for SCPI display and lookup (must match LogModule_t order) */
static const char* const gLogModuleNames[LOG_MODULE_COUNT] = {
    [LOG_MODULE_POWER]   = "POWER",
    [LOG_MODULE_WIFI]    = "WIFI",
    [LOG_MODULE_SD]      = "SD",
    [LOG_MODULE_USB]     = "USB",
    [LOG_MODULE_SCPI]    = "SCPI",
    [LOG_MODULE_ADC]     = "ADC",
    [LOG_MODULE_DAC]     = "DAC",
    [LOG_MODULE_STREAM]  = "STREAM",
    [LOG_MODULE_ENCODER] = "ENCODER",
    [LOG_MODULE_GENERAL] = "GENERAL",
};

void Logger_SetLevel(LogModule_t module, uint8_t level) {
    if (module >= LOG_MODULE_COUNT) return;
    if (level > LOG_LEVEL_DEBUG) level = LOG_LEVEL_DEBUG;
    /* Clamp to compile-time ceiling */
    if (level > gLogCeilings[module]) level = gLogCeilings[module];
    gLogLevels[module] = level;
}

uint8_t Logger_GetLevel(LogModule_t module) {
    if (module >= LOG_MODULE_COUNT) return LOG_LEVEL_NONE;
    return gLogLevels[module];
}

void Logger_SetAllLevels(uint8_t level) {
    for (int i = 0; i < LOG_MODULE_COUNT; i++) {
        Logger_SetLevel((LogModule_t)i, level);
    }
}

const char* Logger_GetModuleName(LogModule_t module) {
    if (module >= LOG_MODULE_COUNT) return "?";
    return gLogModuleNames[module];
}

bool Logger_FindModule(const char* name, size_t len, LogModule_t* module) {
    if (!name || len == 0 || !module) return false;
    for (int i = 0; i < LOG_MODULE_COUNT; i++) {
        const char* candidate = gLogModuleNames[i];
        size_t clen = strlen(candidate);
        if (clen != len) continue;
        /* Case-insensitive compare (toupper both sides for robustness) */
        bool match = true;
        for (size_t j = 0; j < len; j++) {
            if (toupper((unsigned char)name[j]) != toupper((unsigned char)candidate[j])) {
                match = false;
                break;
            }
        }
        if (match) {
            *module = (LogModule_t)i;
            return true;
        }
    }
    return false;
}

uint8_t Logger_GetCeiling(LogModule_t module) {
    if (module >= LOG_MODULE_COUNT) return LOG_LEVEL_NONE;
    return gLogCeilings[module];
}

LogBuffer logBuffer;

#if defined(ENABLE_ICSP_REALTIME_LOG) && (ENABLE_ICSP_REALTIME_LOG == 1) && !defined(__DEBUG)
static void InitICSPLogging(void);
static void LogMessageICSP(const char* buffer, int len);
#endif
static int LogMessageAdd(const char *message);
static inline bool LogIsInISR(void);

/* ISR deferred queue — initialized in LogIsrInit(), used by LogMessage's
 * ISR-aware early path and the drain task. */
static QueueHandle_t gIsrLogQueue = NULL;


#if defined(ENABLE_ICSP_REALTIME_LOG) && (ENABLE_ICSP_REALTIME_LOG == 1) && !defined(__DEBUG)

// Maximum timeout: ~1ms at 200MHz system clock
// Prevents infinite loop if UART hardware fails or is disconnected
#define UART_TX_TIMEOUT 200000

/**
 * Initialize UART4 for ICSP debug logging
 * Pin 4 of ICSP header (RB0) is configured as U4TX @ 921600bps
 */
static void InitICSPLogging(void)
{
    static bool initialized = false;
    if (initialized) {
        return;  // Prevent re-initialization
    }
    
    // Config bits are automatically set to allow this when ENABLE_ICSP_REALTIME_LOG == 1
    
    /* Enter critical section for SYSKEY operations */
    uint32_t int_status = __builtin_disable_interrupts();
    
    /* Unlock system for PPS configuration */
    SYSKEY = 0x00000000U;
    SYSKEY = 0xAA996655U;
    SYSKEY = 0x556699AAU;

    CFGCONbits.IOLOCK = 0U;
    CFGCONbits.PMDLOCK = 0U;

    /* Configure RB0 as UART4 TX */
    RPB0R = 2;              // RB0 -> U4TX
    ANSELBbits.ANSB0 = 0;   // Digital mode
    TRISBbits.TRISB0 = 0;   // Output
    PMD5bits.U4MD = 0;      // Enable UART4 module

    /* Lock back the system after PPS configuration */
    CFGCONbits.IOLOCK = 1U;
    CFGCONbits.PMDLOCK = 1U;
    SYSKEY = 0x33333333U;
    
    /* Restore interrupt state */
    __builtin_mtc0(12, 0, int_status);

    /* Configure UART4: 8-N-1 */
    U4MODE = 0x8;           // BRGH = 1 for high-speed mode
    U4STASET = (_U4STA_UTXEN_MASK | _U4STA_UTXISEL1_MASK);

    /* BAUD Rate: 921600 bps @ 100MHz peripheral clock */
    /* U4BRG = (PBCLK / (4 * BAUD)) - 1 = (100MHz / (4 * 921600)) - 1 = 26 */
    U4BRG = 26;

    /* Enable UART4 */
    U4MODESET = _U4MODE_ON_MASK;
    
    /* Mark as initialized */
    initialized = true;
}

/**
 * Send log message through ICSP pin using UART4
 */
static void LogMessageICSP(const char* buffer, int len)
{
    size_t processedSize = 0;
    uint8_t* lBuffer = (uint8_t*)buffer;
    uint32_t timeout_counter;
    
    while(len > processedSize){
        
        /* Wait while TX buffer is full with timeout */
        timeout_counter = 0;
        while ((U4STA & _U4STA_UTXBF_MASK) && (timeout_counter < UART_TX_TIMEOUT)) {
            timeout_counter++;
        }
        
        // Abort transmission on timeout to prevent system hang
        if (timeout_counter >= UART_TX_TIMEOUT) {
            break;
        }

        /* Send byte */
        U4TXREG = *lBuffer++;
        processedSize++;
    }
}
#endif /* defined(ENABLE_ICSP_REALTIME_LOG) && (ENABLE_ICSP_REALTIME_LOG == 1) && !defined(__DEBUG) */

/**
 * @brief Internal helper to format a message using va_list and ensure it ends with \r\n.
 * 
 * @param format Format string
 * @param args   va_list of arguments
 * @return int   Number of characters written, or 0 on failure
 */
static int LogMessageFormatImpl(const char* format, va_list args)
{
    int size;
    char buffer[LOG_MESSAGE_SIZE];

    if (format == NULL) {
        return 0;
    }

    // Reserve 3 bytes for \r\n\0 - guarantees room to append
    size = vsnprintf(buffer, LOG_MESSAGE_SIZE - 2, format, args);
    if (size <= 0){
        size = 0;
        return size;
    }

    // Clamp to actual buffer content (vsnprintf returns what it would have written)
    size = min((LOG_MESSAGE_SIZE - 3), size);

    // Ensure message ends with \r\n (always have room due to -3 reservation)
    if (size >= 2 && buffer[size-2] == '\r' && buffer[size-1] == '\n') {
        // Already has \r\n
    } else if (size >= 1 && buffer[size-1] == '\n') {
        // Has \n only, convert to \r\n
        buffer[size-1] = '\r';
        buffer[size] = '\n';
        buffer[size+1] = '\0';
        size++;
    } else {
        // No newline, append \r\n
        buffer[size] = '\r';
        buffer[size+1] = '\n';
        buffer[size+2] = '\0';
        size += 2;
    }

    if (size > 0) {
        return LogMessageAdd(buffer);
    }

    return size;
}

/**
 * @brief Adds a formatted log message to the buffer.
 *        ISR-aware: detects context automatically.
 *        - Task context: full vsnprintf formatting, mutex-protected buffer.
 *        - ISR context: copies raw format string to deferred queue (no
 *          vsnprintf — too heavy for ISR stack). Format args are ignored.
 *
 * @param format Format string (printf-style; args ignored in ISR context)
 * @param ...    Variable arguments (only used from task context)
 * @return int   Number of characters written, or 0 on failure
 */
int LogMessage(const char* format, ...)
{
    if (format == NULL) return 0;

    /* ISR path: minimal work — copy string, queue, return */
    if (LogIsInISR() && gIsrLogQueue != NULL) {
        LogEntry entry;
        size_t len = strlen(format);
        if (len == 0) return 0;
        if (len > LOG_MESSAGE_SIZE - 3) len = LOG_MESSAGE_SIZE - 3;
        memcpy(entry.message, format, len);
        entry.message[len] = '\r';
        entry.message[len + 1] = '\n';
        entry.message[len + 2] = '\0';

        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xQueueSendFromISR(gIsrLogQueue, &entry, &xHigherPriorityTaskWoken);
        portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
        return (int)len;
    }

    /* Task path: full vsnprintf formatting */
    va_list args;
    va_start(args, format);
    int result = LogMessageFormatImpl(format, args);
    va_end(args);

    return result;
}

/**
 * @brief Returns the current number of messages in the log buffer.
 *        Note: uint8_t read is atomic on PIC32, no mutex needed.
 *
 * @return size_t Number of stored log messages
 */
size_t LogMessageCount(void)
{
    return logBuffer.count;
}

/**
 * @brief Initializes the log buffer and its mutex (idempotent, thread-safe).
 *        Safe to call multiple times - subsequent calls have no effect.
 */
void LogMessageInit(void) {
    // Fast path: already initialized
    if (logBuffer.mutex != NULL) {
        return;
    }

    // Create mutex outside critical section (FreeRTOS API needs interrupts)
    SemaphoreHandle_t newMutex = xSemaphoreCreateMutex();
    if (newMutex == NULL) {
        // Critical failure: halt system - mutex creation failed
        __builtin_software_breakpoint();
        while(1);
    }

    // Critical section only for pointer assignment and buffer init
    taskENTER_CRITICAL();
    if (logBuffer.mutex == NULL) {
        // We won the race - initialize buffer and assign mutex
        logBuffer.head = 0;
        logBuffer.tail = 0;
        logBuffer.count = 0;
        logBuffer.mutex = newMutex;
        taskEXIT_CRITICAL();

        // Initialize ISR deferred logging (must be after mutex is set)
        LogIsrInit();
    } else {
        // Another thread won - discard our mutex
        taskEXIT_CRITICAL();
        vSemaphoreDelete(newMutex);
    }
}

/**
 * @brief Check if currently executing in ISR context.
 *        Uses FreeRTOS port's uxInterruptNesting counter, which is
 *        maintained by the assembly ISR wrapper (ISR_Support.h) and is
 *        reliable even when Harmony clears MIPS EXL/ERL bits.
 * @return true if in ISR, false otherwise
 */
static inline bool LogIsInISR(void) {
    return uxInterruptNesting != 0;
}

/**
 * @brief Internal helper to add a message to the log buffer.
 *        Also sends the message over ICSP UART if enabled.
 *        Safe to call from ISR context (logs via ICSP only, buffer skipped).
 *
 * @param message Null-terminated message string
 * @return int    Number of characters written, or 0 on failure
 */
static int LogMessageAdd(const char *message) {

    static volatile bool isLoggerInitialized = false;
    int message_len;

    if (message == NULL) {
        return 0;
    }

    message_len = strlen(message);

    if ((message_len == 0) || (message_len >= LOG_MESSAGE_SIZE))
        return 0;

    // ISR context: cannot use mutex — only ICSP output.
    // Use LOG_E_ISR/LOG_I_ISR/LOG_D_ISR for deferred ISR logging.
    if (LogIsInISR()) {
        #if defined(ENABLE_ICSP_REALTIME_LOG) && (ENABLE_ICSP_REALTIME_LOG == 1) && !defined(__DEBUG)
        LogMessageICSP(message, message_len);
        #endif
        return 0;  // Cannot buffer from ISR
    }

    // Thread-safe lazy initialization
    // Note: LogMessageInit() and InitICSPLogging() are idempotent and handle
    // their own thread-safety. We avoid calling them inside critical section
    // because they use FreeRTOS APIs that require interrupts enabled.
    if (!isLoggerInitialized) {
        LogMessageInit();
        #if defined(ENABLE_ICSP_REALTIME_LOG) && (ENABLE_ICSP_REALTIME_LOG == 1) && !defined(__DEBUG)
        InitICSPLogging();
        #endif
        isLoggerInitialized = true;
    }

    #if defined(ENABLE_ICSP_REALTIME_LOG) && (ENABLE_ICSP_REALTIME_LOG == 1) && !defined(__DEBUG)
    LogMessageICSP(message, message_len);
    #endif

    if (xSemaphoreTake(logBuffer.mutex, portMAX_DELAY) == pdTRUE) {
        strncpy(logBuffer.entries[logBuffer.head].message, message, message_len);
        logBuffer.entries[logBuffer.head].message[message_len] = '\0';
        logBuffer.head = (logBuffer.head + 1) % LOG_MAX_ENTRY_COUNT;

        if (logBuffer.count < LOG_MAX_ENTRY_COUNT) {
            logBuffer.count++;
        } else {
            logBuffer.tail = (logBuffer.tail + 1) % LOG_MAX_ENTRY_COUNT;
        }

        xSemaphoreGive(logBuffer.mutex);
        return message_len;
    }
    return 0;
}


/**
 * @brief Dumps all log messages to the SCPI interface and clears the buffer.
 *        Uses pop-and-print pattern to avoid holding mutex during I/O.
 *
 * @param context SCPI context used to write and flush messages
 */
void LogMessageDump(scpi_t * context) {

    char tempBuffer[LOG_MESSAGE_SIZE];
    bool hasMessage;

    if (context == NULL || context->interface == NULL || context->interface->write == NULL) {
        return;
    }

    if (logBuffer.mutex == NULL) {
        return;
    }

    // Pop-and-print loop: mutex held only during memory copy, not I/O
    do {
        hasMessage = false;

        // Critical section: pop one message from buffer
        if (xSemaphoreTake(logBuffer.mutex, portMAX_DELAY) == pdTRUE) {
            if (logBuffer.count > 0) {
                // Copy message to local stack buffer
                strncpy(tempBuffer, logBuffer.entries[logBuffer.tail].message, LOG_MESSAGE_SIZE - 1);
                tempBuffer[LOG_MESSAGE_SIZE - 1] = '\0';

                // Advance tail and decrement count
                logBuffer.tail = (logBuffer.tail + 1) % LOG_MAX_ENTRY_COUNT;
                logBuffer.count--;

                hasMessage = true;
            }
            xSemaphoreGive(logBuffer.mutex);
        }

        // I/O section: write message (mutex released)
        if (hasMessage) {
            context->interface->write(context, tempBuffer, strlen(tempBuffer));
            if (context->interface->flush) {
                context->interface->flush(context);
            }
        }
    } while (hasMessage);
}

/**
 * @brief Clears all log messages from the buffer (thread-safe).
 */
void LogMessageClear(void) {

    if (logBuffer.mutex == NULL) {
        return;
    }

    if (xSemaphoreTake(logBuffer.mutex, portMAX_DELAY) == pdTRUE) {
        logBuffer.head = 0;
        logBuffer.tail = 0;
        logBuffer.count = 0;
        xSemaphoreGive(logBuffer.mutex);
    }
}

/* ── ISR-safe deferred logging ───────────────────────────────────
 * Messages from ISR context are queued via xQueueSendFromISR and
 * drained into the main log buffer by a low-priority task.
 * This avoids the mutex-in-ISR crash (issue #191).
 */

static TaskHandle_t  gIsrLogTaskHandle = NULL;

#define LOG_ISR_TASK_STACK  128   /* words (512 bytes — only does queue drain) */
#define LOG_ISR_TASK_PRIO   1     /* lowest priority, runs when nothing else does */

/**
 * @brief Deferred ISR log drain task.
 *        Blocks on the ISR queue and forwards messages to the main log buffer.
 */
static void LogIsrDrainTask(void* pvParameters) {
    (void)pvParameters;
    LogEntry entry;

    for (;;) {
        if (xQueueReceive(gIsrLogQueue, &entry, portMAX_DELAY) == pdTRUE) {
            /* LogMessageAdd handles mutex, ICSP output, etc. */
            LogMessageAdd(entry.message);
        }
    }
}

/**
 * @brief Initialize the ISR log queue and drain task (idempotent).
 */
void LogIsrInit(void) {
    if (gIsrLogQueue != NULL) return;

    gIsrLogQueue = xQueueCreate(LOG_ISR_QUEUE_DEPTH, sizeof(LogEntry));
    if (gIsrLogQueue == NULL) return;

    if (xTaskCreate(LogIsrDrainTask, "logISR", LOG_ISR_TASK_STACK,
                    NULL, LOG_ISR_TASK_PRIO, &gIsrLogTaskHandle) != pdPASS) {
        vQueueDelete(gIsrLogQueue);
        gIsrLogQueue = NULL;
    }
}

/* LogMessageFromISR removed — LogMessage() is now ISR-aware via
 * uxInterruptNesting check. Use LOG_E/LOG_I/LOG_D from any context. */