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

#define LOG_D(fmt,...) LogMessage(fmt, ##__VA_ARGS__)
#define LOG_E(fmt,...) LogMessage(fmt, ##__VA_ARGS__)
#define LOG_I(fmt,...) LogMessage(fmt, ##__VA_ARGS__)

#ifdef	__cplusplus
}
#endif

#endif	/* LOGGER_H */

