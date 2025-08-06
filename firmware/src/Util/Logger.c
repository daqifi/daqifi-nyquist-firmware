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
static void LogMessageICSP(const char* buffer, int len);

static void InitList()
{
    if (m_ListPtr == NULL)
    {
        // Initialize with DropOnOverflow = true to create a circular buffer
        // This allows new messages to overwrite old ones when the buffer is full
        StackList_Initialize(&m_Data, true, &g_NullLockProvider);
        m_ListPtr = &m_Data;
        
        #if ENABLE_ICSP_REALTIME_LOG == 1 && !defined(__DEBUG)
        // Initialize U4TX pin for live debug logs through the ICSP pins if enabled
        
        /* Unlock system for PPS configuration */
        SYSKEY = 0x00000000U;
        SYSKEY = 0xAA996655U;
        SYSKEY = 0x556699AAU;

        CFGCONbits.IOLOCK = 0U;
        CFGCONbits.PMDLOCK = 0U;

        RPB0R = 2;          // RB0 - U4TX
        ANSELBbits.ANSB0 = 0;
        TRISBbits.TRISB0 = 0;
        PMD5bits.U4MD = 0;

        /* Lock back the system after PPS configuration */
        CFGCONbits.IOLOCK = 1U;
        CFGCONbits.PMDLOCK = 1U;
        SYSKEY = 0x33333333U;

        /* Set up UxMODE bits */
        /* STSEL  = 0 */
        /* PDSEL = 0 */
        /* UEN = 0 */

        U4MODE = 0x8;
        /* Enable UART1 Receiver and Transmitter */
        U4STASET = (_U4STA_UTXEN_MASK | _U4STA_UTXISEL1_MASK );

        /* BAUD Rate register Setup */
        U4BRG = 26;

        /* Turn ON UART4 */
        U4MODESET = _U4MODE_ON_MASK;
        #endif//#if ENABLE_ICSP_REALTIME_LOG == 1 && !defined(_DEBUG)
    }
}

static void LogMessageICSP(const char* buffer, int len)
{
    size_t processedSize = 0;
    uint8_t* lBuffer = (uint8_t*)buffer;
    
    while(len > processedSize){
        
        /* Wait while TX buffer is full */
        while (U4STA & _U4STA_UTXBF_MASK);

        /* 8-bit mode */
        U4TXREG = *lBuffer++;
        processedSize++;
    }
}

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
        #endif//#if ENABLE_ICSP_REALTIME_LOG == 1 && !defined(_DEBUG)
        
        if (StackList_PushBack(m_ListPtr, (const uint8_t*)buffer, (size_t)len))
        {
            return len;
        }
    } else {
        // Message too long or empty, use as-is
        int count = min(len, STACK_LIST_NODE_SIZE);
        
        #if ENABLE_ICSP_REALTIME_LOG == 1 && !defined(__DEBUG)
        LogMessageICSP(message, len);
        #endif//#if ENABLE_ICSP_REALTIME_LOG == 1 && !defined(_DEBUG)

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


