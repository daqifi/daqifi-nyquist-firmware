#ifndef SDSPI_1217_SYS_TIME_H
#define SDSPI_1217_SYS_TIME_H
#include <stdbool.h>
#include <stdint.h>
#include "system/system.h"
typedef uintptr_t SYS_TIME_HANDLE;
#define SYS_TIME_HANDLE_INVALID ((SYS_TIME_HANDLE)(-1))
typedef enum { SYS_TIME_SINGLE, SYS_TIME_PERIODIC } SYS_TIME_CALLBACK_TYPE;
typedef void (*SYS_TIME_CALLBACK)(uintptr_t context);
typedef enum { SYS_TIME_SUCCESS, SYS_TIME_ERROR } SYS_TIME_RESULT;
SYS_TIME_HANDLE SYS_TIME_CallbackRegisterMS(SYS_TIME_CALLBACK cb, uintptr_t ctx, uint32_t ms, SYS_TIME_CALLBACK_TYPE type);
SYS_TIME_RESULT SYS_TIME_TimerDestroy(SYS_TIME_HANDLE h);
uint64_t SYS_TIME_Counter64Get(void);
uint32_t SYS_TIME_FrequencyGet(void);
uint32_t SYS_TIME_MSToCount(uint32_t ms);
#endif
