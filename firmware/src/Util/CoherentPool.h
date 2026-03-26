#ifndef _COHERENT_POOL_H
#define _COHERENT_POOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Default coherent pool size: 41KB
// Must accommodate DMA-safe buffers: SD circular (32KB) + SD write (8KB) + headroom
#define COHERENT_POOL_DEFAULT_SIZE  (41U * 1024U)

// Alignment for all pool allocations (cache line size on PIC32MZ)
#define COHERENT_POOL_ALIGNMENT     16U

// Maximum number of named partitions for debugging
#define COHERENT_POOL_MAX_PARTITIONS 8

typedef struct {
    const char* name;
    uint8_t*    ptr;
    uint32_t    size;
} CoherentPoolPartition;

typedef struct {
    uint32_t totalSize;
    uint32_t usedBytes;
    uint32_t partitionCount;
    CoherentPoolPartition partitions[COHERENT_POOL_MAX_PARTITIONS];
} CoherentPoolInfo;

/**
 * @brief Initialize the coherent pool. Call once at boot before any allocations.
 */
void CoherentPool_Init(void);

/**
 * @brief Reset the pool, freeing all partitions. All previously returned
 *        pointers become invalid. Call only when no DMA transfers are active.
 */
void CoherentPool_Reset(void);

/**
 * @brief Allocate a named partition from the coherent pool.
 *
 * Returns a 16-byte aligned pointer to DMA-safe coherent memory.
 * Allocation is bump-only — memory is freed only by CoherentPool_Reset().
 *
 * @param name  Human-readable name for debugging (not copied — must be a string literal)
 * @param size  Number of bytes to allocate
 * @return Pointer to coherent memory, or NULL if pool is exhausted
 */
uint8_t* CoherentPool_Alloc(const char* name, uint32_t size);

/**
 * @brief Query remaining free bytes in the pool.
 */
uint32_t CoherentPool_FreeBytes(void);

/**
 * @brief Query total pool size.
 */
uint32_t CoherentPool_TotalSize(void);

/**
 * @brief Get snapshot of all current partitions for diagnostics.
 */
void CoherentPool_GetInfo(CoherentPoolInfo* info);

#ifdef __cplusplus
}
#endif

#endif /* _COHERENT_POOL_H */
