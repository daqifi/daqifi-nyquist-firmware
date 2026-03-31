#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Unified Streaming Memory Pool
 *
 * A single contiguous heap allocation that holds ALL streaming-related
 * memory: USB circular buffer, WiFi circular buffer, sample pool array,
 * and sample free-list array.  Partitioned at each stream start based on
 * active interfaces — no runtime malloc, no fragmentation.
 *
 * Layout after partition:
 *   [USB circular buf | WiFi circular buf | samplePool[] | nextFree[]]
 *
 * Boot:   StreamingBufferPool_Init() allocates the pool, sets defaults.
 * Start:  StreamingBufferPool_Partition() re-carves all regions.
 * Run:    Each module uses its region via external pointers.
 */

/** Minimum buffer sizes (from module constraints) */
#define STREAMING_USB_MIN   4096   /* USBCDC_WBUFFER_SIZE */
#define STREAMING_WIFI_MIN  1400   /* SOCKET_BUFFER_MAX_LENGTH */

/**
 * Heap reserved for everything outside the pool.
 *
 * Sum of all FreeRTOS task stacks created AFTER pool allocation
 * (words × 4), plus per-task TCB overhead, plus WiFi driver heap,
 * plus safety margin. If any task stack size changes, this
 * automatically stays correct.
 *
 * Task stacks are in words (PIC32MZ: 1 word = 4 bytes).
 */

/* tasks.c tasks (created by Harmony/system init) */
#define _RESERVE_TASKS_C  (1500 + 1024 + 160 + 144 + 144)  /* words: APP_FREERTOS, WDRV_WINC, AD7609, USB_DEV, USB_DRV */
/* app_freertos.c tasks */
#define _RESERVE_APP      (640 + 3072 + 1024 + 1024)        /* words: PowerUI, USB, WiFi, SD */
/* streaming.c tasks (created at Streaming_Init) */
#define _RESERVE_STREAM   (1392 + 512)                       /* words: encoder, deferred ISR */
/* ADC.c + Logger.c tasks */
#define _RESERVE_OTHER    (160 + 128)                        /* words: MC12bADC EOS, logISR drain */

#define _RESERVE_TOTAL_STACK_WORDS (_RESERVE_TASKS_C + _RESERVE_APP + _RESERVE_STREAM + _RESERVE_OTHER)
#define _RESERVE_TOTAL_STACK_BYTES (_RESERVE_TOTAL_STACK_WORDS * 4U)

#define _RESERVE_TCB_OVERHEAD  (14U * 400U)   /* ~400 bytes per TCB × 14 tasks */
#define _RESERVE_QUEUES_MISC   (6U * 1024U)   /* FreeRTOS queues, mutexes, kernel */
#define _RESERVE_WIFI_DRIVER   (18U * 1024U)  /* WINC1500 driver heap at power-up */
#define _RESERVE_MARGIN        (6U * 1024U)   /* heap_4 metadata + alignment + safety */

#define STREAMING_POOL_HEAP_RESERVE ( \
    _RESERVE_TOTAL_STACK_BYTES + \
    _RESERVE_TCB_OVERHEAD + \
    _RESERVE_QUEUES_MISC + \
    _RESERVE_WIFI_DRIVER + \
    _RESERVE_MARGIN \
)

/**
 * Allocate the pool (all available heap minus reserve) and set default
 * partition.  Call once at boot before USB/WiFi/sample pool init.
 *
 * @param defaultUsbSize   Initial USB buffer size
 * @param defaultWifiSize  Initial WiFi buffer size
 * @param defaultSampleCount Initial sample pool depth
 * @return true if allocation succeeded
 */
bool StreamingBufferPool_Init(uint32_t defaultUsbSize, uint32_t defaultWifiSize,
                              uint32_t defaultSampleCount);

/**
 * Re-partition the pool.  All regions are reset (empty).
 * Must only be called when streaming is stopped.
 *
 * @param usbSize   Desired USB circular buffer size (clamped to min/available)
 * @param wifiSize  Desired WiFi circular buffer size (clamped to min/available)
 * @param sampleCount Desired sample pool depth (0 = maximize with remaining space)
 */
void StreamingBufferPool_Partition(uint32_t usbSize, uint32_t wifiSize,
                                   uint32_t sampleCount);

/** Get current USB buffer region */
void StreamingBufferPool_GetUsb(uint8_t** buf, uint32_t* size);

/** Get current WiFi buffer region */
void StreamingBufferPool_GetWifi(uint8_t** buf, uint32_t* size);

/** Get current sample pool region and count */
void StreamingBufferPool_GetSamplePool(void** poolBuf, int16_t** nextFreeBuf,
                                        uint32_t* count);

/** Query total pool size */
uint32_t StreamingBufferPool_TotalSize(void);
/** Query current USB partition size */
uint32_t StreamingBufferPool_UsbSize(void);
/** Query current WiFi partition size */
uint32_t StreamingBufferPool_WifiSize(void);
/** Query current sample pool count */
uint32_t StreamingBufferPool_SampleCount(void);

#ifdef __cplusplus
}
#endif
