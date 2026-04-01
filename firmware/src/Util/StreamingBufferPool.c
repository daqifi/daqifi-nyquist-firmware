#define LOG_LVL LOG_LEVEL_DEBUG
#define LOG_MODULE LOG_MODULE_GENERAL

#include "StreamingBufferPool.h"
#include "Logger.h"
#include "osal/osal.h"
#include "state/data/AInSample.h"
#include <string.h>

static uint8_t* gPool = NULL;
static uint32_t gPoolSize = 0;

/* Current partition offsets and sizes */
static uint32_t gUsbSize = 0;
static uint32_t gWifiSize = 0;
static uint32_t gSampleCount = 0;

/* Per-sample memory cost: data struct + free-list index */
#define SAMPLE_BYTES (sizeof(AInPublicSampleList_t) + sizeof(int16_t))

bool StreamingBufferPool_Init(uint32_t defaultUsbSize, uint32_t defaultWifiSize,
                              uint32_t defaultSampleCount) {
    if (gPool != NULL) return true;

    /* Grab all available heap minus a reserve for task stacks, queues,
     * FreeRTOS kernel, WiFi driver, and transient allocations. */
    size_t freeHeap = xPortGetFreeHeapSize();
    if (freeHeap <= STREAMING_POOL_HEAP_RESERVE) {
        LOG_E("StreamingBufferPool: insufficient heap (%u free, need > %u)",
              (unsigned)freeHeap, (unsigned)STREAMING_POOL_HEAP_RESERVE);
        return false;
    }
    /* Measured heap budget (see issue #229 debug logs):
     *   Before pool:         332440 free
     *   Before TasksCreate:   34024 free (need for task stacks)
     *   After all tasks:      10184 free
     *   After WiFi driver:     5456 free
     *
     * Between pool alloc and task creation: streaming/ADC/logger inits
     * consume ~16KB for queues and tasks (sample pool is IN the pool).
     * Reserve = 16KB inits + 34KB task stacks + 5KB final margin = 55KB.
     */
    uint32_t reserve = 60U * 1024U;
    if (freeHeap <= reserve) {
        LOG_E("StreamingBufferPool: only %u free, need > %u reserve",
              (unsigned)freeHeap, (unsigned)reserve);
        return false;
    }
    uint32_t poolSize = (uint32_t)(freeHeap - reserve);

    gPool = (uint8_t*)OSAL_Malloc(poolSize);
    if (gPool == NULL) {
        LOG_E("StreamingBufferPool: alloc %u failed", (unsigned)poolSize);
        return false;
    }
    gPoolSize = poolSize;
    LOG_I("StreamingBufferPool: %u bytes at %p (heap was %u, reserve %u)",
          (unsigned)gPoolSize, (void*)gPool,
          (unsigned)freeHeap, (unsigned)STREAMING_POOL_HEAP_RESERVE);

    StreamingBufferPool_Partition(defaultUsbSize, defaultWifiSize,
                                  defaultSampleCount);
    return true;
}

void StreamingBufferPool_Partition(uint32_t usbSize, uint32_t wifiSize,
                                   uint32_t sampleCount) {
    if (gPool == NULL) return;

    /* Clamp buffer minimums */
    if (usbSize < STREAMING_USB_MIN) usbSize = STREAMING_USB_MIN;
    if (wifiSize < STREAMING_WIFI_MIN) wifiSize = STREAMING_WIFI_MIN;

    /* Ensure buffers fit in pool — fall back to minimums if needed */
    uint32_t bufTotal = usbSize + wifiSize;
    if (bufTotal > gPoolSize) {
        usbSize = STREAMING_USB_MIN;
        wifiSize = STREAMING_WIFI_MIN;
        bufTotal = usbSize + wifiSize;
    }

    /* Guard against pool too small for even the minimums */
    if (bufTotal >= gPoolSize) {
        LOG_E("Pool too small: %u bytes, need %u for minimums",
              (unsigned)gPoolSize, (unsigned)bufTotal);
        gUsbSize = 0; gWifiSize = 0; gSampleCount = 0;
        return;
    }

    /* Remaining space goes to sample pool (minus alignment padding) */
    uint32_t alignedBufTotal = (bufTotal + 3U) & ~3U;  /* align sample start */
    uint32_t remaining = gPoolSize - alignedBufTotal;
    uint32_t maxSamples = (uint32_t)(remaining / SAMPLE_BYTES);
    if (maxSamples > MAX_AIN_SAMPLE_COUNT) maxSamples = MAX_AIN_SAMPLE_COUNT;

    if (sampleCount == 0 || sampleCount > maxSamples) {
        sampleCount = maxSamples;
    }
    if (sampleCount < MIN_AIN_SAMPLE_COUNT && maxSamples >= MIN_AIN_SAMPLE_COUNT) {
        sampleCount = MIN_AIN_SAMPLE_COUNT;
    }

    gUsbSize = usbSize;
    gWifiSize = wifiSize;
    gSampleCount = sampleCount;

    LOG_I("Pool partition: USB=%u WiFi=%u samples=%u (of %u max, %u bytes pool)",
          (unsigned)usbSize, (unsigned)wifiSize, (unsigned)sampleCount,
          (unsigned)maxSamples, (unsigned)gPoolSize);
}

void StreamingBufferPool_GetUsb(uint8_t** buf, uint32_t* size) {
    *buf = gPool;
    *size = gUsbSize;
}

void StreamingBufferPool_GetWifi(uint8_t** buf, uint32_t* size) {
    *buf = (gPool != NULL) ? gPool + gUsbSize : NULL;
    *size = gWifiSize;
}

void StreamingBufferPool_GetSamplePool(void** poolBuf, int16_t** nextFreeBuf,
                                        uint32_t* count) {
    if (gPool == NULL || gSampleCount == 0) {
        *poolBuf = NULL;
        *nextFreeBuf = NULL;
        *count = 0;
        return;
    }
    /* Layout: [USB | WiFi | <align> | samplePool[count] | nextFree[count]] */
    uintptr_t base = (uintptr_t)gPool;
    uintptr_t off = (uintptr_t)(gUsbSize + gWifiSize);

    /* Align sample pool start to 4 bytes (uint32_t members in AInSample) */
    off = (off + 3U) & ~3U;

    uintptr_t nextFreeOff = off + gSampleCount * sizeof(AInPublicSampleList_t);
    /* int16_t needs 2-byte alignment */
    nextFreeOff = (nextFreeOff + 1U) & ~1U;

    /* Bounds check */
    uintptr_t end = nextFreeOff + gSampleCount * sizeof(int16_t);
    if (end - (uintptr_t)gPool > gPoolSize) {
        *poolBuf = NULL; *nextFreeBuf = NULL; *count = 0;
        return;
    }

    *poolBuf = (void*)(base + off);
    *nextFreeBuf = (int16_t*)(base + nextFreeOff);
    *count = gSampleCount;
}

uint32_t StreamingBufferPool_TotalSize(void)  { return gPoolSize; }
uint32_t StreamingBufferPool_UsbSize(void)    { return gUsbSize; }
uint32_t StreamingBufferPool_WifiSize(void)   { return gWifiSize; }
uint32_t StreamingBufferPool_SampleCount(void) { return gSampleCount; }
