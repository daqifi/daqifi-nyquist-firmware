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
    
/**
 * Logs a formatted message
 * @param format
 * @return The number of characters written
 */
int LogMessage(const char* format, ...);

size_t LogMessageCount();

size_t LogMessagePop(uint8_t* buffer, size_t maxSize);

#ifndef SYS_CMD_ENABLE
#undef SYS_CMD_MESSAGE
#undef SYS_CMD_PRINT
int SYS_CMD_MESSAGE(const char* message);
int SYS_CMD_PRINT(const char* format, ...);
#endif

#ifdef	__cplusplus
}
#endif

#endif	/* LOGGER_H */

