#include "Logger.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "NullLockProvider.h"
#include "StackList.h"
#include <xc.h>
#ifndef min
    #define min(x,y) x <= y ? x : y
#endif // min

#ifndef max
    #define max(x,y) x >= y ? x : y
#endif // min
#define UNUSED(x) (void)(x)

static StackList m_Data;
static StackList* m_ListPtr = NULL;
#if ENABLE_ICSP_REALTIME_LOG == 1 && !defined(__DEBUG)
static void InitICSPLogging(void);
static void LogMessageICSP(const char* buffer, int len);
#endif

static void InitList()
{
    if (m_ListPtr == NULL)
    {
        // Initialize with DropOnOverflow = true to create a circular buffer
        // This allows new messages to overwrite old ones when the buffer is full
        StackList_Initialize(&m_Data, true, &g_NullLockProvider);
        m_ListPtr = &m_Data;
        
        #if ENABLE_ICSP_REALTIME_LOG == 1 && !defined(__DEBUG)
        InitICSPLogging();
        #endif
    }
}

#if ENABLE_ICSP_REALTIME_LOG == 1 && !defined(__DEBUG)

// Maximum timeout: ~1ms at 200MHz system clock
// Prevents infinite loop if UART hardware fails or is disconnected
#define UART_TX_TIMEOUT 200000

/**
 * Initialize UART4 for ICSP debug logging
 * Pin 4 of ICSP header (RB0) is configured as U4TX @ 921600bps
 */
static void InitICSPLogging(void)
{
    // Config bits are automatically set to allow this when ENABLE_ICSP_REALTIME_LOG == 1
    
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

    /* Configure UART4: 8-N-1 */
    U4MODE = 0x8;           // BRGH = 1 for high-speed mode
    U4STASET = (_U4STA_UTXEN_MASK | _U4STA_UTXISEL1_MASK);

    /* BAUD Rate: 921600 bps @ 100MHz peripheral clock */
    /* U4BRG = (PBCLK / (4 * BAUD)) - 1 = (100MHz / (4 * 921600)) - 1 = 26 */
    U4BRG = 26;

    /* Enable UART4 */
    U4MODESET = _U4MODE_ON_MASK;
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
#endif /* ENABLE_ICSP_REALTIME_LOG == 1 && !defined(__DEBUG) */

static int LogMessageImpl(const char* message)
{
    //UNUSED(message);

    if (message == NULL)
    {
        return 0;
    }
    
    InitList();

    // Check if message needs newline appended
    int len = strlen(message);
    if (len > 0 && len < STACK_LIST_NODE_SIZE - 2) {
        char buffer[STACK_LIST_NODE_SIZE];
        strcpy(buffer, message);
        
        // Ensure message ends with \r\n for PuTTY compatibility
        if (len >= 2 && buffer[len-2] == '\r' && buffer[len-1] == '\n') {
            // Already has \r\n, use as-is
        } else if (len >= 1 && buffer[len-1] == '\n') {
            // Has just \n, add \r before it
            if (len < STACK_LIST_NODE_SIZE - 2) {
                // Shift the \n and add \r
                buffer[len-1] = '\r';
                buffer[len] = '\n';
                buffer[len+1] = '\0';
                len++;
            }
        } else {
            // No newline, add \r\n
            if (len < STACK_LIST_NODE_SIZE - 3) {
                buffer[len] = '\r';
                buffer[len+1] = '\n';
                buffer[len+2] = '\0';
                len += 2;
            }
        }
        
        #if ENABLE_ICSP_REALTIME_LOG == 1 && !defined(__DEBUG)
        LogMessageICSP(buffer, len);
        #endif//#if ENABLE_ICSP_REALTIME_LOG == 1 && !defined(__DEBUG)
        
        if (StackList_PushBack(m_ListPtr, (const uint8_t*)buffer, (size_t)len))
        {
            return len;
        }
    } else {
        // Message too long or empty, use as-is
        int count = min(len, STACK_LIST_NODE_SIZE);
        
        #if ENABLE_ICSP_REALTIME_LOG == 1 && !defined(__DEBUG)
        LogMessageICSP(message, len);
        #endif//#if ENABLE_ICSP_REALTIME_LOG == 1 && !defined(__DEBUG)

        if (StackList_PushBack(m_ListPtr, (const uint8_t*)message, (size_t)count))
        {
            return count;
        }
    }
    
    return 0;
}

static int LogMessageFormatImpl(const char* format, va_list args)
{
    //UNUSED(format);
    //UNUSED(args);
   
    if (format == NULL)
    {
        return 0;
    }
    
    if (strstr(format, "%") == NULL)
    {
        // Not actually a format string!
        return LogMessageImpl(format);
    }
    
    char buffer[STACK_LIST_NODE_SIZE];
    int size = vsnprintf(buffer, STACK_LIST_NODE_SIZE-3, format, args);
    
    // Ensure message ends with \n (SCPI standard uses LF only)
    if (size > 0) {
        // Check if message already ends with newline
        if (size >= 2 && buffer[size-2] == '\r' && buffer[size-1] == '\n') {
            // Has \r\n, convert to just \n
            buffer[size-2] = '\n';
            buffer[size-1] = '\0';
            size--;
        } else if (size >= 1 && buffer[size-1] == '\n') {
            // Already has \n, do nothing
        } else {
            // No newline, add \n
            buffer[size] = '\n';
            buffer[size+1] = '\0';
            size++;
        }
    }
    
    buffer[size] = '\0';
    if (size > 0)
    {
       return LogMessageImpl(buffer); 
    }
    
    return size;
}

int LogMessage(const char* format, ...)
{
    //UNUSED(format);
    va_list args;
    va_start(args, format);
    int result = LogMessageFormatImpl(format, args);
    va_end(args);
    
    return result;
}

size_t LogMessageCount()
{
    InitList();
    return StackList_Size(m_ListPtr);
}

size_t LogMessagePop(uint8_t* buffer, size_t maxSize)
{
    //UNUSED(buffer);
    //UNUSED(maxSize);
    
    InitList();
    return StackList_PopFront(m_ListPtr, buffer, maxSize);
}


