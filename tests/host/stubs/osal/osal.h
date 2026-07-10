/* ==========================================================================
 * Host-test stub for Harmony osal/osal.h.
 *
 * CircularBuffer.c uses only OSAL_Malloc / OSAL_Free. On-target these are
 * FreeRTOS heap_4 wrappers; on the PC host we map them straight onto the C
 * library so the allocator paths (Init / Resize / Deinit) exercise real
 * malloc/free semantics.
 * ========================================================================== */
#ifndef OSAL_HOST_STUB_H
#define OSAL_HOST_STUB_H

#include <stdlib.h>

#define OSAL_Malloc(size) malloc((size))
#define OSAL_Free(ptr)    free((ptr))

#endif /* OSAL_HOST_STUB_H */
