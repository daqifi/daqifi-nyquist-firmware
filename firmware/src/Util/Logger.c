#include "Logger.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <xc.h>
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
 *
 * Usage:
 * - Call LogMessageInit() once at startup (automatically handled on first log)
 * - Use LogMessage("format", ...) to add messages
 * - Call LogMessageDump(context) to flush and reset the buffer
 */

#ifndef min
    #define min(x,y) (((x) <= (y)) ? (x) : (y))
#endif

#ifndef max
    #define max(x,y) (((x) >= (y)) ? (x) : (y))
#endif
#define UNUSED(x) (void)(x)


LogBuffer logBuffer;

#if defined(ENABLE_ICSP_REALTIME_LOG) && (ENABLE_ICSP_REALTIME_LOG == 1) && !defined(__DEBUG)
static void InitICSPLogging(void);
static void LogMessageICSP(const char* buffer, int len);
#endif
static int LogMessageAdd(const char *message);


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
 *        Accepts printf-style formatting.
 * 
 * @param format Format string (printf-style)
 * @param ...    Variable arguments
 * @return int   Number of characters written, or 0 on failure
 */
int LogMessage(const char* format, ...)
{
    //UNUSED(format);
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
 * @brief Initializes the log buffer and its mutex (idempotent).
 *        Safe to call multiple times - subsequent calls have no effect.
 */
void LogMessageInit(void) {
    // Already initialized - do not wipe buffered logs
    if (logBuffer.mutex != NULL) {
        return;
    }

    logBuffer.head = 0;
    logBuffer.tail = 0;
    logBuffer.count = 0;

    logBuffer.mutex = xSemaphoreCreateMutex();
    if (logBuffer.mutex == NULL) {
        // Critical failure: halt system - mutex creation failed
        __builtin_software_breakpoint();
        while(1);
    }
}

/**
 * @brief Check if currently executing in ISR context (MIPS/PIC32).
 * @return true if in ISR, false otherwise
 */
static inline bool LogIsInISR(void) {
    // MIPS Status register: EXL (bit 1) or ERL (bit 2) set means exception/ISR
    uint32_t status = __builtin_mfc0(12, 0);
    return (status & 0x06) != 0;
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

    // ISR context: cannot use mutex or task APIs - only ICSP output
    if (LogIsInISR()) {
        #if defined(ENABLE_ICSP_REALTIME_LOG) && (ENABLE_ICSP_REALTIME_LOG == 1) && !defined(__DEBUG)
        LogMessageICSP(message, message_len);
        #endif
        return 0;  // Cannot buffer from ISR
    }

    // Double-checked locking for thread-safe initialization
    if (!isLoggerInitialized) {
        taskENTER_CRITICAL();
        if (!isLoggerInitialized) {
            LogMessageInit();
            #if defined(ENABLE_ICSP_REALTIME_LOG) && (ENABLE_ICSP_REALTIME_LOG == 1) && !defined(__DEBUG)
            InitICSPLogging();
            #endif
            isLoggerInitialized = true;
        }
        taskEXIT_CRITICAL();
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