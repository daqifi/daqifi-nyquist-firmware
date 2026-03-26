#include "CoherentPool.h"
#include <string.h>

// Single static coherent region for all DMA-safe buffer allocations.
// __attribute__((coherent)) places this in KSEG1 (uncached) address space.
// __attribute__((aligned(16))) ensures cache-line alignment for DMA safety.
static __attribute__((coherent, aligned(16)))
    uint8_t gCoherentPool[COHERENT_POOL_DEFAULT_SIZE];

// Bump allocator state
static uint32_t gPoolOffset = 0;
static uint32_t gPartitionCount = 0;
static CoherentPoolPartition gPartitions[COHERENT_POOL_MAX_PARTITIONS];

void CoherentPool_Init(void) {
    gPoolOffset = 0;
    gPartitionCount = 0;
    memset(gPartitions, 0, sizeof(gPartitions));
}

void CoherentPool_Reset(void) {
    gPoolOffset = 0;
    gPartitionCount = 0;
    memset(gPartitions, 0, sizeof(gPartitions));
}

/**
 * Thread safety: No mutex needed. All CoherentPool_Alloc calls happen during
 * single-threaded init (app_SystemInit → sd_card_manager_Init, before the
 * FreeRTOS scheduler starts other tasks). The pool is never allocated from
 * after init completes. If this changes in future, add a mutex here.
 */
uint8_t* CoherentPool_Alloc(const char* name, uint32_t size) {
    if (size == 0) {
        return NULL;
    }

    // Align offset up to COHERENT_POOL_ALIGNMENT
    uint32_t aligned = (gPoolOffset + COHERENT_POOL_ALIGNMENT - 1)
                       & ~(COHERENT_POOL_ALIGNMENT - 1);

    if (aligned + size > COHERENT_POOL_DEFAULT_SIZE) {
        return NULL;  // Pool exhausted
    }

    uint8_t* ptr = &gCoherentPool[aligned];
    gPoolOffset = aligned + size;

    // Record partition for diagnostics
    if (gPartitionCount < COHERENT_POOL_MAX_PARTITIONS) {
        gPartitions[gPartitionCount].name = name;
        gPartitions[gPartitionCount].ptr = ptr;
        gPartitions[gPartitionCount].size = size;
        gPartitionCount++;
    }

    return ptr;
}

uint32_t CoherentPool_FreeBytes(void) {
    uint32_t aligned = (gPoolOffset + COHERENT_POOL_ALIGNMENT - 1)
                       & ~(COHERENT_POOL_ALIGNMENT - 1);
    if (aligned >= COHERENT_POOL_DEFAULT_SIZE) {
        return 0;
    }
    return COHERENT_POOL_DEFAULT_SIZE - aligned;
}

uint32_t CoherentPool_TotalSize(void) {
    return COHERENT_POOL_DEFAULT_SIZE;
}

void CoherentPool_GetInfo(CoherentPoolInfo* info) {
    if (info == NULL) return;
    info->totalSize = COHERENT_POOL_DEFAULT_SIZE;
    info->usedBytes = gPoolOffset;
    info->partitionCount = gPartitionCount;
    memcpy(info->partitions, gPartitions, sizeof(gPartitions));
}
