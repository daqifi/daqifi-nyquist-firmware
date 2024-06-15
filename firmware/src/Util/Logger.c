#include "Logger.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "NullLockProvider.h"
#include "StackList.h"
#include "system/debug/sys_debug.h"

#define UNUSED(x) (void)(x)

static StackList m_Data;
static StackList* m_ListPtr = NULL;

static void InitList()
{
    if (m_ListPtr == NULL)
    {
        StackList_Initialize(&m_Data, false, &g_NullLockProvider);
        m_ListPtr = &m_Data;
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

    int count = min(strlen(message), STACK_LIST_NODE_SIZE);
    if (StackList_PushBack(m_ListPtr, (const uint8_t*)message, (size_t)count))
    {
        return count;
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
    int size = vsnprintf(buffer, STACK_LIST_NODE_SIZE-1, format, args);
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

#ifndef SYS_CMD_ENABLE

int SYS_CMD_MESSAGE(const char* message)
{
    return LogMessageImpl(message);
}

int SYS_CMD_PRINT(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    int result = LogMessageFormatImpl(format, &args);
    va_end(args);
    
    return result;
}
#endif

extern void vAssertCalled( const char * pcFile, unsigned long ulLine )
{
volatile char *pcFileName;
volatile unsigned long ulLineNumber;

	/* Prevent things that are useful to view in the debugger from being
	optimised away. */
	pcFileName = ( char * ) pcFile;
	( void ) pcFileName;
	ulLineNumber = ulLine;
    SYS_DEBUG_BreakPoint();
	/* Set ulLineNumber to 0 in the debugger to break out of this loop and
	return to the line that triggered the assert. */
	while( ulLineNumber != 0 )
	{
		__asm volatile( "NOP" );
		__asm volatile( "NOP" );
		__asm volatile( "NOP" );
		__asm volatile( "NOP" );
		__asm volatile( "NOP" );
	}
}