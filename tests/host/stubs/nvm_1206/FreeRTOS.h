/* #1206 host stub: the types nvm.c's recursive mutex uses. */
#ifndef NVM1206_STUB_FREERTOS_H
#define NVM1206_STUB_FREERTOS_H
#include <stdint.h>
typedef struct { int unused; } StaticSemaphore_t;
typedef void *SemaphoreHandle_t;
typedef uint32_t TickType_t;
#define portMAX_DELAY ((TickType_t)0xFFFFFFFFu)
#endif
