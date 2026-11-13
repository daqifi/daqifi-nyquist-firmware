#ifndef HIF881_OSAL_H
#define HIF881_OSAL_H
/* #881 host-test stub: a counting semaphore that never blocks. */
#include <stdint.h>
typedef void *OSAL_SEM_HANDLE_TYPE;
typedef enum { OSAL_RESULT_FALSE = 0, OSAL_RESULT_FAIL = 0, OSAL_RESULT_TRUE = 1, OSAL_RESULT_SUCCESS = 1 } OSAL_RESULT;
typedef enum { OSAL_SEM_TYPE_BINARY, OSAL_SEM_TYPE_COUNTING } OSAL_SEM_TYPE;
#define OSAL_WAIT_FOREVER 0xFFFFFFFFu
static inline OSAL_RESULT OSAL_SEM_Create(OSAL_SEM_HANDLE_TYPE *s, OSAL_SEM_TYPE t, uint8_t max, uint8_t init)
{ static int sem; (void)t; (void)max; (void)init; *s = &sem; return OSAL_RESULT_TRUE; }
static inline OSAL_RESULT OSAL_SEM_Delete(OSAL_SEM_HANDLE_TYPE *s) { *s = 0; return OSAL_RESULT_TRUE; }
static inline OSAL_RESULT OSAL_SEM_Pend(OSAL_SEM_HANDLE_TYPE *s, uint32_t w) { (void)s; (void)w; return OSAL_RESULT_TRUE; }
static inline OSAL_RESULT OSAL_SEM_Post(OSAL_SEM_HANDLE_TYPE *s) { (void)s; return OSAL_RESULT_TRUE; }
#endif
