/* #1206 host stub: a recursion counter the test reads back, so it can assert
 * the NVM mutex is released on every exit path, timeouts included. */
#ifndef NVM1206_STUB_SEMPHR_H
#define NVM1206_STUB_SEMPHR_H
#include "FreeRTOS.h"
SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t *buf);
int xSemaphoreTakeRecursive(SemaphoreHandle_t m, TickType_t wait);
int xSemaphoreGiveRecursive(SemaphoreHandle_t m);
#endif
