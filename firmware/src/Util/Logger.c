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
    #define min(x,y) x <= y ? x : y
#endif // min

#ifndef max
    #define max(x,y) x >= y ? x : y
#endif // min
#define UNUSED(x) (void)(x)


LogBuffer logBuffer;

#if defined(ENABLE_ICSP_REALTIME_LOG) && (ENABLE_ICSP_REALTIME_LOG == 1) && !defined(__DEBUG)
static void InitICSPLogging(void);
static void LogMessageICSP(const char* buffer, int len);
#endif
static bool LogMessageAdd(const char *message);


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

    size = vsnprintf(buffer, LOG_MESSAGE_SIZE - 3, format, args);
    if (size <= 0){ 
        size = 0;
        return size;
    }
    
    size = min((LOG_MESSAGE_SIZE-3), size);
   
    // Ensure message ends with \r\n
    if (size >= 2 && buffer[size-2] == '\r' && buffer[size-1] == '\n') {
        // Already has \r\n
    } else if (size >= 1 && buffer[size-1] == '\n') {
        if (size < LOG_MESSAGE_SIZE - 2) {
            buffer[size-1] = '\r';
            buffer[size] = '\n';
            buffer[size+1] = '\0';
            size++;
        }
    } else {
        if (size < LOG_MESSAGE_SIZE - 3) {
            buffer[size] = '\r';
            buffer[size+1] = '\n';
            buffer[size+2] = '\0';
            size += 2;
        }
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
 * 
 * @return size_t Number of stored log messages
 */
size_t LogMessageCount()
{
    return logBuffer.count;
}

/**
 * @brief Initializes the log buffer and its mutex.
 *        This function is safe to call multiple times.
 */
void LogMessageInit(void) {
    logBuffer.head = 0;
    logBuffer.tail = 0;
    logBuffer.count = 0;
    
    if(logBuffer.mutex == NULL)
    logBuffer.mutex = xSemaphoreCreateMutex();
}

/**
 * @brief Internal helper to add a message to the log buffer.
 *        Also sends the message over ICSP UART if enabled.
 * 
 * @param message Null-terminated message string
 * @return true   If the message was added successfully
 * @return false  If the message was invalid or mutex acquisition failed
 */
static bool LogMessageAdd(const char *message) {
    
    static bool bInit = false;
    static uint32_t test = 0;
    int message_len = strlen(message);
    
    if((message_len == 0) || (message_len>= LOG_MESSAGE_SIZE))
    return false;
    
    if(!bInit){
        LogMessageInit();
        #if defined(ENABLE_ICSP_REALTIME_LOG) && (ENABLE_ICSP_REALTIME_LOG == 1) && !defined(__DEBUG)
        InitICSPLogging();
        #endif
        bInit = true;
    }
    
    #if defined(ENABLE_ICSP_REALTIME_LOG) && (ENABLE_ICSP_REALTIME_LOG == 1) && !defined(__DEBUG)
    LogMessageICSP(message, message_len);
    #endif
    
    if (xSemaphoreTake(logBuffer.mutex, portMAX_DELAY) == pdTRUE) {
        strcpy(logBuffer.entries[logBuffer.head].message, message);
        logBuffer.entries[logBuffer.head].message[message_len] = '\0';
        logBuffer.head = (logBuffer.head + 1) % LOG_MAX_ENTRY_COUNT;

        if (logBuffer.count < LOG_MAX_ENTRY_COUNT) {
            logBuffer.count++;
        } else {
            logBuffer.tail = (logBuffer.tail + 1) % LOG_MAX_ENTRY_COUNT;
        }

        xSemaphoreGive(logBuffer.mutex);
        return true;
    }
    return false;
}


/**
 * @brief Dumps all log messages to the SCPI interface and resets the buffer.
 * 
 * @param context SCPI context used to write and flush messages
 */
void LogMessageDump(scpi_t * context) {

    if (xSemaphoreTake(logBuffer.mutex, portMAX_DELAY) == pdTRUE) {
        uint8_t index = logBuffer.tail;
        for (uint8_t i = 0; i < logBuffer.count; i++) {

            context->interface->write(context, logBuffer.entries[index].message, strlen(logBuffer.entries[index].message));
            
            // Flush after each message to prevent buffer overflow
            // This ensures the USB buffer has time to process each message
            if (context->interface->flush) {
                context->interface->flush(context);
            }
             
            index = (index + 1) % LOG_MAX_ENTRY_COUNT;
        }
        
        LogMessageInit(); // reset the log buffer 
        xSemaphoreGive(logBuffer.mutex);
    }
}