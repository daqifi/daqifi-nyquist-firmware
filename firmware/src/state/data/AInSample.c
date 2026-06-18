/*! @file AInSample.c
 * @brief Analog input sample queue and object pool implementation.
 *
 * Implements a FreeRTOS queue for streaming analog input samples between the
 * ADC interrupt handler and the streaming task. Uses an object pool with O(1)
 * free list allocation to eliminate heap overhead at high sample rates.
 *
 * Two initialization paths:
 * - AInSampleList_InitializeExternal(): Pool memory from StreamingBufferPool
 *   (static BSS, zero fragmentation). Used at boot and re-partitioned at each
 *   stream start. FreeRTOS queue is reused across sessions.
 * - AInSampleList_Initialize(): Legacy heap allocation path (fallback only).
 *
 * Performance characteristics:
 * - Queue: Standard FreeRTOS queue (thread-safe, ISR-safe)
 * - Pool allocation: O(1) via free list (no fragmentation, deterministic timing)
 * - Pool size: Configurable (default DEFAULT_AIN_SAMPLE_COUNT = 1100)
 *
 * @author Javier Longares Abaiz - When Technology becomes art.
 * @web www.javierlongares.com
 */

#include "AInSample.h"
#include "FreeRTOS.h"
#include "queue.h"
#include <string.h>  // For memset
#include "Util/Logger.h"

// =============================================================================
// Lock-free SPSC ring of sample pointers (#525)
// =============================================================================
// Replaces the FreeRTOS queue on the streaming hot path.  Single producer =
// the streaming-timer ISR (TIMER_5, pri 3); single consumer = the encoder task
// (streaming_Task, pri 6).  The producer makes ZERO FreeRTOS API calls, so the
// timer ISR can never touch a kernel task list / event list — this structurally
// eliminates the #525 ready-list-corruption class (the original notify-give
// configASSERT AND the xQueueSendFromISR -> xTaskRemoveFromEventList surface).
//
// Correctness on this target (single-core, in-order MIPS32, write-back D-cache):
//   - ringHead is written ONLY by the producer; ringTail ONLY by the consumer.
//     A 32-bit aligned store/load is atomic on PIC32MZ, so neither index can be
//     observed torn.
//   - The producer writes ringSlots[head] BEFORE publishing ringHead; `volatile`
//     forbids the compiler from reordering those two stores, and an in-order
//     core retires them in program order.  Producer and consumer are the SAME
//     core, so the consumer (which gates on the volatile ringHead) always sees a
//     committed slot before it sees the head advance — no cache/barrier issue.
//   - Symmetrically the consumer reads ringSlots[tail] before publishing ringTail.
//   - Capacity convention: the ring holds (ringCapacity - 1) usable entries; it
//     is FULL when advancing head would collide with tail, EMPTY when head==tail.
//     ringCapacity is therefore allocated as (usable slot count + 1).
//
// Lifecycle (alloc / free / resize of ringSlots) mirrors the old queue: it
// happens ONLY in Initialize / InitializeExternal / Destroy, all of which run in
// task context while the streaming timer is stopped — so the producer ISR can
// never race a teardown of its own backing store (no handle-capture needed on
// the producer side; the consumer side still captures under a critical section
// to stay robust against a Destroy from another task).
static AInPublicSampleList_t** ringSlots = NULL;  // [ringCapacity] pointers (heap)
static uint32_t ringCapacity = 0;                 // = queueSize + 1 (0 = none)
static volatile uint32_t ringHead = 0;            // producer-owned write index
static volatile uint32_t ringTail = 0;            // consumer-owned read index

//! Number of usable slots (= ringCapacity - 1). Kept for resize decisions.
static uint32_t queueSize = 0;

//! Advance a ring index with wraparound.
static inline uint32_t ring_next(uint32_t idx) {
    uint32_t n = idx + 1u;
    return (n >= ringCapacity) ? 0u : n;
}

// =============================================================================
// Object Pool (from StreamingBufferPool static BSS, or FreeRTOS heap fallback)
// =============================================================================
static uint8_t* samplePoolBase = NULL;   // Raw byte pointer (stride-indexed)
static uint32_t poolCapacity = 0;
static size_t poolElementStride = 0;     // Bytes per element (runtime-sized)
static volatile uint32_t poolAllocCount = 0;     // Currently allocated (in use)
static volatile uint32_t poolMaxAllocCount = 0;  // High-water mark (max ever in use)

// =============================================================================
// Free List Data Structures (O(1) allocation/deallocation)
// =============================================================================
static int16_t freeHead = -1;
static int16_t* nextFree = NULL;

static SemaphoreHandle_t poolMutex = NULL;
static volatile bool poolActive = false;
static bool poolOwnsMemory = false;  // false = external (StreamingBufferPool)

void AInSampleList_Initialize(size_t maxSize, bool dropOnOverflow){

    (void)dropOnOverflow;

    // Destroy previous resources if re-initializing (any size change or same size).
    // Must destroy before creating new queue to prevent orphaning the old handle.
    if (samplePoolBase != NULL) {
        AInSampleList_Destroy();
    }

    // Clamp pool size to compile-time limits
    if (maxSize < MIN_AIN_SAMPLE_COUNT) maxSize = MIN_AIN_SAMPLE_COUNT;
    if (maxSize > MAX_AIN_SAMPLE_COUNT) maxSize = MAX_AIN_SAMPLE_COUNT;

    // Heap fallback uses max-size elements (all 16 channels)
    size_t elemSize = AInSampleList_ElementSize(MAX_AIN_PUBLIC_CHANNELS);

    // Clamp further to what the heap can actually fit.
    if (samplePoolBase == NULL) {
        size_t perSample = elemSize + sizeof(int16_t);
        size_t heapAvail = xPortGetFreeHeapSize();
        // Reserve 10KB for queue + FreeRTOS overhead + alignment padding
        size_t usable = (heapAvail > 10240) ? (heapAvail - 10240) : 0;
        uint32_t maxFit = (uint32_t)(usable / perSample);

        if (maxSize > maxFit) {
            maxSize = (maxFit >= MIN_AIN_SAMPLE_COUNT) ? maxFit : MIN_AIN_SAMPLE_COUNT;
        }
    }

    // Allocate the lock-free ring (queueSize usable slots => queueSize+1 capacity)
    queueSize = maxSize;
    ringCapacity = maxSize + 1u;
    ringHead = 0;
    ringTail = 0;
    ringSlots = (AInPublicSampleList_t**)pvPortMalloc(ringCapacity * sizeof(AInPublicSampleList_t*));
    configASSERT(ringSlots != NULL);

    // Allocate pool from heap with max-channel element size
    if (samplePoolBase == NULL) {
        poolCapacity = maxSize;
        poolElementStride = elemSize;

        samplePoolBase = (uint8_t*)pvPortMalloc(poolCapacity * poolElementStride);
        if (samplePoolBase == NULL) {
            LOG_E("Sample pool alloc failed (%u samples, %u bytes)",
                  (unsigned)poolCapacity,
                  (unsigned)(poolCapacity * poolElementStride));
            vPortFree(ringSlots);
            ringSlots = NULL;
            ringCapacity = 0;
            queueSize = 0;
            poolCapacity = 0;
            configASSERT(0);
            return;
        }

        nextFree = (int16_t*)pvPortMalloc(poolCapacity * sizeof(int16_t));
        if (nextFree == NULL) {
            vPortFree(samplePoolBase);
            samplePoolBase = NULL;
            vPortFree(ringSlots);
            ringSlots = NULL;
            ringCapacity = 0;
            queueSize = 0;
            poolCapacity = 0;
            LOG_E("Sample pool nextFree alloc failed");
            configASSERT(0);
            return;
        }
        poolOwnsMemory = true;
    }

    // Initialize object pool mutex
    if (poolMutex == NULL) {
        poolMutex = xSemaphoreCreateMutex();
        configASSERT(poolMutex != NULL);
    }

    // Build free list chain: 0 → 1 → 2 → ... → N-1 → -1
    for (uint32_t i = 0; i < poolCapacity - 1; i++) {
        nextFree[i] = (int16_t)(i + 1);
    }
    nextFree[poolCapacity - 1] = -1;  // End of list
    freeHead = 0;  // Start at first entry

    /* #525: publish under a critical section (barrier) — see InitializeExternal. */
    taskENTER_CRITICAL();
    poolActive = true;
    taskEXIT_CRITICAL();
}

void AInSampleList_InitializeExternal(void* poolMem, int16_t* freeMem,
                                       size_t maxSize, size_t elementSize) {
    if (poolMem == NULL || freeMem == NULL || maxSize == 0 || elementSize == 0) {
        LOG_E("Sample pool external init: NULL or zero (%p, %p, %u, %u)",
              poolMem, freeMem, (unsigned)maxSize, (unsigned)elementSize);
        return;
    }

    if (maxSize < MIN_AIN_SAMPLE_COUNT) maxSize = MIN_AIN_SAMPLE_COUNT;
    if (maxSize > MAX_AIN_SAMPLE_COUNT) maxSize = MAX_AIN_SAMPLE_COUNT;

    // Create mutex if first call (boot-time malloc)
    if (poolMutex == NULL) {
        poolMutex = xSemaphoreCreateMutex();
        configASSERT(poolMutex != NULL);
    }

    // Serialize reconfiguration — blocks any in-flight alloc/free
    xSemaphoreTake(poolMutex, portMAX_DELAY);
    poolActive = false;

    // Drain/reset the ring if present (reuse the backing array to avoid malloc).
    // The timer is stopped here (reconfigure runs only between sessions), so the
    // producer ISR is quiescent — resetting the indices is sufficient to drop any
    // stale pointers into the about-to-be-replaced pool memory.
    if (ringSlots != NULL) {
        ringHead = 0;
        ringTail = 0;

        // Reuse the existing ring array when the new size fits (avoids runtime
        // malloc). Only reallocate if new size exceeds current capacity — this
        // can only happen via explicit SYST:MEM:SAMP:POOL > boot default.
        if (maxSize > queueSize) {
            size_t needed = (maxSize + 1u) * sizeof(AInPublicSampleList_t*) + 80;
            size_t freeHeap = xPortGetFreeHeapSize();
            if (freeHeap < needed + 1024) {
                LOG_E("Sample ring resize skipped: need %u, heap free %u",
                      (unsigned)needed, (unsigned)freeHeap);
                maxSize = queueSize;  // Keep old ring, clamp pool to fit
            } else {
                LOG_I("Sample ring resize: %u -> %u slots (heap free %u)",
                      (unsigned)queueSize, (unsigned)maxSize, (unsigned)freeHeap);
                AInPublicSampleList_t** newRing = (AInPublicSampleList_t**)
                    pvPortMalloc((maxSize + 1u) * sizeof(AInPublicSampleList_t*));
                configASSERT(newRing != NULL);
                vPortFree(ringSlots);
                ringSlots = newRing;
                ringCapacity = maxSize + 1u;
                queueSize = maxSize;
            }
        }
    } else {
        // First init — must allocate the ring (boot-time malloc, heap is fresh)
        queueSize = maxSize;
        ringCapacity = maxSize + 1u;
        ringHead = 0;
        ringTail = 0;
        ringSlots = (AInPublicSampleList_t**)
            pvPortMalloc(ringCapacity * sizeof(AInPublicSampleList_t*));
        configASSERT(ringSlots != NULL);
    }

    // Swap to externally provided memory (from StreamingBufferPool)
    samplePoolBase = (uint8_t*)poolMem;
    nextFree = freeMem;
    poolCapacity = maxSize;
    poolElementStride = elementSize;
    poolOwnsMemory = false;
    poolAllocCount = 0;
    poolMaxAllocCount = 0;

    // Build free list chain
    for (uint32_t i = 0; i < poolCapacity - 1; i++) {
        nextFree[i] = (int16_t)(i + 1);
    }
    nextFree[poolCapacity - 1] = -1;
    freeHead = 0;

    /* #525: publish under a critical section. AllocateFromPool/FreeToPool no
     * longer take poolMutex (they use critical sections), so the mutex give
     * below no longer orders these field writes for them. The taskENTER_CRITICAL()
     * call is a compiler barrier that commits the field writes above before
     * poolActive goes true, so a concurrent alloc/free can't see poolActive=true
     * with stale samplePoolBase/freeHead. */
    taskENTER_CRITICAL();
    poolActive = true;
    taskEXIT_CRITICAL();
    xSemaphoreGive(poolMutex);

    LOG_I("Sample pool: %u samples × %u bytes (external memory)",
          (unsigned)poolCapacity, (unsigned)poolElementStride);
}

/**
 * @brief Destroys the sample ring and resets the object pool.
 *
 * Thread safety note: We do NOT take poolMutex at the top of this function.
 * Instead, we use poolActive (32-bit atomic write on PIC32MZ) as a fast-path
 * guard that immediately blocks new AllocateFromPool/FreeToPool calls without
 * mutex contention. The mutex is only taken later (step 5) as a synchronization
 * barrier to ensure in-flight operations complete before freeing memory.
 * This is safe because Destroy is only called from Streaming_Start (task context,
 * streaming stopped) when no ISR or task is allocating from the pool.
 *
 * Teardown sequence (order matters):
 * 1. Block new pool operations (poolActive = false — atomic on PIC32MZ)
 * 2. Atomically capture and nullify the ring backing array
 * 3. Free the ring array
 * 4. Take mutex as barrier, free pool memory
 */
void AInSampleList_Destroy()
{
    // Step 1: Block new pool operations (atomic 32-bit write, no mutex needed)
    poolActive = false;

    // Step 2: Atomically capture and nullify the ring array to block consumers.
    // The producer ISR is quiescent here (timer stopped), so only a stray
    // task-context consumer could race — the captured-pointer pattern in the
    // consumer functions makes that a clean NULL-check, not a use-after-free.
    taskENTER_CRITICAL();
    AInPublicSampleList_t** ring = ringSlots;
    ringSlots = NULL;
    ringCapacity = 0;
    ringHead = 0;
    ringTail = 0;
    taskEXIT_CRITICAL();

    // Step 3: Free the ring backing array (stale pointers inside are moot —
    // the whole sample pool is about to be freed/repartitioned).
    if (ring != NULL) {
        vPortFree(ring);
    }

    // Step 4: Free pool memory with proper synchronization.
    // Take mutex as barrier if available; free unconditionally either way
    // to prevent heap leaks if mutex creation failed during init.
    if (poolMutex != NULL) {
        xSemaphoreTake(poolMutex, portMAX_DELAY);
    }
    if (poolOwnsMemory) {
        if (samplePoolBase != NULL) {
            vPortFree(samplePoolBase);
        }
        if (nextFree != NULL) {
            vPortFree(nextFree);
        }
    }
    samplePoolBase = NULL;
    nextFree = NULL;
    poolCapacity = 0;
    poolElementStride = 0;
    freeHead = -1;
    if (poolMutex != NULL) {
        xSemaphoreGive(poolMutex);
    }

    queueSize = 0;
}

bool AInSampleList_PushBack(const AInPublicSampleList_t* pData){
    // Task-context single-producer push.  NOTE: while streaming, the timer ISR
    // is the producer (AInSampleList_PushBackFromISR); this task-context variant
    // currently has NO callers and must never be used concurrently with the ISR
    // producer — two producers would corrupt the single-producer ring.
    if (pData == NULL) {
        return false;
    }
    bool ok = false;
    taskENTER_CRITICAL();
    if (ringSlots != NULL && ringCapacity != 0u) {
        uint32_t head = ringHead;
        uint32_t next = head + 1u;
        if (next >= ringCapacity) next = 0u;
        if (next != ringTail) {            // not full
            ringSlots[head] = (AInPublicSampleList_t*)pData;
            ringHead = next;               // publish after slot write
            ok = true;
        }
    }
    taskEXIT_CRITICAL();
    return ok;
}

bool AInSampleList_PopFront( AInPublicSampleList_t** ppData)
{
    if (ppData == NULL) {
        return false;
    }
    // Capture the ring base + capacity under a critical section to get a coherent
    // non-NULL snapshot vs a concurrent Destroy.  The consumer is single (encoder
    // task) so ringTail is owned here; ringHead is read as a volatile snapshot.
    taskENTER_CRITICAL();
    AInPublicSampleList_t** ring = ringSlots;
    uint32_t cap = ringCapacity;
    taskEXIT_CRITICAL();
    if (ring == NULL || cap == 0u) {
        return false;
    }
    uint32_t tail = ringTail;
    if (tail == ringHead) {
        return false;                      // empty
    }
    *ppData = ring[tail];
    uint32_t next = tail + 1u;
    if (next >= cap) next = 0u;
    ringTail = next;                        // publish after slot read
    return true;
}

bool AInSampleList_PeekFront(AInPublicSampleList_t** ppData)
{
    if (ppData == NULL) {
        return false;
    }
    taskENTER_CRITICAL();
    AInPublicSampleList_t** ring = ringSlots;
    taskEXIT_CRITICAL();
    if (ring == NULL) {
        return false;
    }
    uint32_t tail = ringTail;
    if (tail == ringHead) {
        return false;                      // empty
    }
    *ppData = ring[tail];                   // peek — do NOT advance ringTail
    return true;
}

size_t AInSampleList_Size()
{
    taskENTER_CRITICAL();
    uint32_t cap = ringCapacity;
    taskEXIT_CRITICAL();
    if (cap == 0u) {
        return 0;
    }
    uint32_t head = ringHead;
    uint32_t tail = ringTail;
    return (head >= tail) ? (head - tail) : (head + cap - tail);
}

bool AInSampleList_IsEmpty()
{
    taskENTER_CRITICAL();
    AInPublicSampleList_t** ring = ringSlots;
    taskEXIT_CRITICAL();
    if (ring == NULL) {
        return true;
    }
    return (ringHead == ringTail);
}


// ============================================================================
// Object Pool Implementation (dynamically allocated from heap)
// ============================================================================

AInPublicSampleList_t* AInSampleList_AllocateFromPool() {
    AInPublicSampleList_t* result = NULL;

    /* #525: use a CRITICAL SECTION, not a mutex. A blocking mutex here suspended
     * the scheduler on contention (this runs in the pri-9 deferred task; the
     * pri-6 encoder calls FreeToPool) — and the streaming-timer ISR's
     * vTaskNotifyGiveFromISR landing in that vTaskSuspendAll() window hit the
     * FreeRTOS pending-ready path, corrupting the notified task's xEventListItem
     * and tripping configASSERT (FreeRTOS_tasks.c:8261) -> hard wedge. A critical
     * section is the correct primitive for this O(1) free-list pop: it briefly
     * masks interrupts <= configMAX_SYSCALL_INTERRUPT_PRIORITY but NEVER suspends
     * the scheduler, so the notify-give can't race the kernel lists. Kept short.
     * poolActive is re-checked INSIDE the section so a concurrent reconfigure
     * (Destroy / InitializeExternal) can't free the pool mid-pop. */
    taskENTER_CRITICAL();
    if (poolActive && freeHead >= 0 && samplePoolBase != NULL && poolElementStride > 0) {
        int idx = freeHead;
        freeHead = nextFree[idx];  // Move head to next free
        result = (AInPublicSampleList_t*)(samplePoolBase + (size_t)idx * poolElementStride);
        poolAllocCount++;
        if (poolAllocCount > poolMaxAllocCount) {
            poolMaxAllocCount = poolAllocCount;
        }
        // Clear entire element to ensure no stale data (bounded: one element)
        memset(result, 0, poolElementStride);
    }
    taskEXIT_CRITICAL();

    return result;
}

void AInSampleList_FreeToPool(AInPublicSampleList_t* pSample) {
    if (pSample == NULL) {
        return;
    }

    /* #525: critical section, not a mutex (see AllocateFromPool for the wedge
     * mechanism). ALL of the gate check, stride/bounds validation, double-free
     * guard, and free-list push run inside the one section so a concurrent
     * reconfigure can't free/repartition the pool between validation and push,
     * and so the section is consistent + short. Never suspends the scheduler. */
    taskENTER_CRITICAL();
    if (poolActive && samplePoolBase != NULL && poolElementStride > 0 &&
        poolAllocCount > 0) {
        ptrdiff_t byteOff = (uint8_t*)pSample - samplePoolBase;
        if (byteOff >= 0 && (byteOff % (ptrdiff_t)poolElementStride) == 0) {
            ptrdiff_t index = byteOff / (ptrdiff_t)poolElementStride;
            if ((uint32_t)index < poolCapacity) {
                // O(1) deallocation: push to free list head. The poolAllocCount>0
                // gate above prevents a double-free from corrupting the free list
                // (a duplicate entry would hand the same slot out twice -> torn data).
                poolAllocCount--;
                nextFree[index] = freeHead;
                freeHead = (int16_t)index;
            }
        }
    }
    taskEXIT_CRITICAL();
}

/* ---- ISR-context pool/queue producers (#525) ------------------------------
 * The streaming-timer ISR (TIMER_5, priority 3) is now the SOLE producer of
 * stream samples — the per-tick deferred task was removed because its
 * ulTaskNotifyTake reblock window was the notify-give configASSERT race site
 * (FreeRTOS_tasks.c:8261). These variants run in that ISR.
 *
 * No critical section is needed on the free-list here. The ISR runs at IPL 3,
 * which excludes every task (tasks run at IPL 0); the only CONSUMER — the
 * pri-6 encoder's AInSampleList_FreeToPool — takes taskENTER_CRITICAL, which
 * raises IPL to configMAX_SYSCALL_INTERRUPT_PRIORITY (4) >= 3 and so masks
 * this ISR. Producer and consumer are therefore mutually exclusive by
 * interrupt-priority level — freeHead, nextFree[], and poolAllocCount are
 * never touched concurrently. (The timer ISR is the only ISR that allocates
 * from this pool — the EOS task and ADC data-ready ISRs do not touch it.)
 */
/* #525 diagnostic+guard: counts times the ISR alloc saw an out-of-range
 * freeHead / nextFree entry (i.e. a clobbered free-list).  If this is >0 after
 * a wedge-repro run, the pool free-list IS being corrupted; the guard below
 * prevents the wild pointer + memset that would otherwise clobber FreeRTOS RAM
 * (the observed vTaskSwitchContext TLBL exception). Readable via mdb. */
volatile uint32_t gPoolBadHead = 0;

AInPublicSampleList_t* AInSampleList_AllocateFromPoolFromISR(void) {
    AInPublicSampleList_t* result = NULL;
    if (poolActive && samplePoolBase != NULL && poolElementStride > 0) {
        int idx = freeHead;
        if (idx >= 0 && (uint32_t)idx < poolCapacity) {
            int next = nextFree[idx];  // -1 = end-of-list, else a valid index
            if (next >= -1 && next < (int)poolCapacity) {
                freeHead = (int16_t)next;  // Move head to next free
                result = (AInPublicSampleList_t*)(samplePoolBase + (size_t)idx * poolElementStride);
                poolAllocCount++;
                if (poolAllocCount > poolMaxAllocCount) {
                    poolMaxAllocCount = poolAllocCount;
                }
                memset(result, 0, poolElementStride);
            } else {
                gPoolBadHead++;  // clobbered nextFree[] entry — bail, no wild write
            }
        } else if (idx != -1) {
            gPoolBadHead++;      // clobbered freeHead (not the -1 empty sentinel)
        }
        // idx == -1 → genuine empty pool → result stays NULL (normal drop)
    }
    return result;
}

void AInSampleList_FreeToPoolFromISR(AInPublicSampleList_t* pSample) {
    if (pSample == NULL) {
        return;
    }
    if (poolActive && samplePoolBase != NULL && poolElementStride > 0 &&
        poolAllocCount > 0) {
        ptrdiff_t byteOff = (uint8_t*)pSample - samplePoolBase;
        if (byteOff >= 0 && (byteOff % (ptrdiff_t)poolElementStride) == 0) {
            ptrdiff_t index = byteOff / (ptrdiff_t)poolElementStride;
            if ((uint32_t)index < poolCapacity) {
                poolAllocCount--;
                nextFree[index] = freeHead;
                freeHead = (int16_t)index;
            }
        }
    }
}

/* ISR producer — the SOLE producer of the SPSC ring (#525).
 *
 * This is the crux of the structural fix: it makes ZERO FreeRTOS API calls.
 * The former xQueueSendFromISR is gone, so the streaming-timer ISR can no longer
 * reach xTaskRemoveFromEventList / the kernel ready+event lists by ANY path —
 * removing the last kernel-list-corruption surface in the timer ISR (the prior
 * notify-give configASSERT was already gone; this closes the queue surface too).
 *
 * Single-producer ring push (no lock needed — see the ring contract at the top
 * of this file): write the slot, then publish ringHead.  `volatile` orders the
 * two stores; the in-order core retires them in program order; the consumer is
 * the same core, so it observes a committed slot before the advanced head.
 *
 * No handle/base capture: ringSlots is allocated/freed only by Initialize /
 * InitializeExternal / Destroy, which run in task context while the streaming
 * timer is stopped — this ISR cannot race a teardown of its own backing store. */
bool AInSampleList_PushBackFromISR(const AInPublicSampleList_t* pData) {
    if (pData == NULL || ringSlots == NULL || ringCapacity == 0u) {
        return false;
    }
    uint32_t head = ringHead;              // producer-owned; plain read is fine
    uint32_t next = head + 1u;
    if (next >= ringCapacity) next = 0u;
    if (next == ringTail) {                // ring full — consumer can't keep up
        return false;
    }
    ringSlots[head] = (AInPublicSampleList_t*)pData;
    ringHead = next;                       // publish AFTER the slot store
    return true;
}

uint32_t AInSampleList_PoolInUse(void) {
    return poolAllocCount;
}

uint32_t AInSampleList_PoolMaxUsed(void) {
    return poolMaxAllocCount;
}

void AInSampleList_PoolResetMaxUsed(void) {
    poolMaxAllocCount = poolAllocCount;
}

size_t AInSampleList_PoolCapacity(void) {
    return poolCapacity;
}

size_t AInSampleList_PoolElementSize(void) {
    return poolElementStride;
}
