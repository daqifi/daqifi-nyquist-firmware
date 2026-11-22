#ifndef SDSPI_1217_OSAL_H
#define SDSPI_1217_OSAL_H
#include <stdint.h>
typedef enum { OSAL_RESULT_FAIL = 0, OSAL_RESULT_SUCCESS = 1 } OSAL_RESULT;
typedef uint32_t OSAL_CRITSECT_DATA_TYPE;
typedef enum { OSAL_CRIT_TYPE_LOW, OSAL_CRIT_TYPE_HIGH } OSAL_CRIT_TYPE;
#define OSAL_WAIT_FOREVER 0xFFFFU
#define OSAL_MUTEX_DECLARE(name) int name
#define OSAL_MUTEX_Create(m) ((void)(m), OSAL_RESULT_SUCCESS)
#define OSAL_MUTEX_Lock(m, t) ((void)(m), (void)(t), OSAL_RESULT_SUCCESS)
#define OSAL_MUTEX_Unlock(m) ((void)(m), OSAL_RESULT_SUCCESS)
#define OSAL_CRIT_Enter(t) ((void)(t), 0U)
#define OSAL_CRIT_Leave(t, v) ((void)(t), (void)(v))
#endif
