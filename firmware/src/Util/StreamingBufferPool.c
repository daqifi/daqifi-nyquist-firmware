#define LOG_LVL LOG_LEVEL_DEBUG
#define LOG_MODULE LOG_MODULE_GENERAL

#include "StreamingBufferPool.h"
#include "Logger.h"
#include "state/data/AInSample.h"
#include <string.h>

/* Static pool array — lives in BSS, not heap. No fragmentation risk,
 * no malloc, no fatal hook. Size must fit in RAM alongside .data,
 * FreeRTOS heap (75KB), and coherent pool (92KB). */
#define STATIC_POOL_SIZE (194U * 1024U)
static uint8_t gPoolStorage[STATIC_POOL_SIZE];

static uint8_t* gPool = NULL;
static uint32_t gPoolSize = 0;

/* Current partition offsets and sizes */
static uint32_t gUsbSize = 0;
static uint32_t gWifiSize = 0;
static uint32_t gEncoderSize = 0;
static uint32_t gSdCircularSize = 0;
static uint32_t gSampleCount = 0;

/* Per-sample memory cost: data struct + free-list index */
#define SAMPLE_BYTES (sizeof(AInPublicSampleList_t) + sizeof(int16_t))

bool StreamingBufferPool_Init(uint32_t defaultUsbSize, uint32_t defaultWifiSize,
                              uint32_t defaultEncoderSize, uint32_t defaultSdCircularSize,
                              uint32_t defaultSampleCount) {
    if (gPool != NULL) return true;

    /* Static array — no heap allocation, no fragmentation, no fatal hook.
     * Lives in BSS alongside FreeRTOS heap and coherent pool. */
    gPool = gPoolStorage;
    gPoolSize = STATIC_POOL_SIZE;
    LOG_I("StreamingBufferPool: %u bytes (static)", (unsigned)gPoolSize);

    StreamingBufferPool_Partition(defaultUsbSize, defaultWifiSize,
                                  defaultEncoderSize, defaultSdCircularSize,
                                  defaultSampleCount);
    return true;
}

void StreamingBufferPool_Partition(uint32_t usbSize, uint32_t wifiSize,
                                   uint32_t encoderSize, uint32_t sdCircularSize,
                                   uint32_t sampleCount) {
    if (gPool == NULL) return;

    /* Clamp buffer minimums */
    if (usbSize < STREAMING_USB_MIN) usbSize = STREAMING_USB_MIN;
    if (wifiSize < STREAMING_WIFI_MIN) wifiSize = STREAMING_WIFI_MIN;
    if (encoderSize < ENCODER_BUFFER_MIN) encoderSize = ENCODER_BUFFER_MIN;
    if (sdCircularSize < STREAMING_SD_CIRCULAR_MIN) sdCircularSize = STREAMING_SD_CIRCULAR_MIN;

    /* Ensure buffers fit in pool — fall back to minimums if needed */
    uint32_t bufTotal = usbSize + wifiSize + encoderSize + sdCircularSize;
    if (bufTotal > gPoolSize) {
        usbSize = STREAMING_USB_MIN;
        wifiSize = STREAMING_WIFI_MIN;
        encoderSize = ENCODER_BUFFER_MIN;
        sdCircularSize = STREAMING_SD_CIRCULAR_MIN;
        bufTotal = usbSize + wifiSize + encoderSize + sdCircularSize;
    }

    /* Guard against pool too small for even the minimums */
    if (bufTotal >= gPoolSize) {
        LOG_E("Pool too small: %u bytes, need %u for minimums",
              (unsigned)gPoolSize, (unsigned)bufTotal);
        gUsbSize = 0; gWifiSize = 0; gEncoderSize = 0;
        gSdCircularSize = 0; gSampleCount = 0;
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
    gEncoderSize = encoderSize;
    gSdCircularSize = sdCircularSize;
    gSampleCount = sampleCount;

    LOG_I("Pool partition: USB=%u WiFi=%u enc=%u sdCirc=%u samples=%u (of %u max, %u pool)",
          (unsigned)usbSize, (unsigned)wifiSize, (unsigned)encoderSize,
          (unsigned)sdCircularSize, (unsigned)sampleCount, (unsigned)maxSamples,
          (unsigned)gPoolSize);
}

void StreamingBufferPool_GetEncoder(uint8_t** buf, uint32_t* size) {
    *buf = (gPool != NULL) ? gPool + gUsbSize + gWifiSize : NULL;
    *size = gEncoderSize;
}

void StreamingBufferPool_GetSdCircular(uint8_t** buf, uint32_t* size) {
    *buf = (gPool != NULL) ? gPool + gUsbSize + gWifiSize + gEncoderSize : NULL;
    *size = gSdCircularSize;
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
    /* Layout: [USB | WiFi | encoder | SD_circular | <align> | samplePool[count] | nextFree[count]] */
    uintptr_t base = (uintptr_t)gPool;
    uintptr_t off = (uintptr_t)(gUsbSize + gWifiSize + gEncoderSize + gSdCircularSize);

    /* Align sample pool start to 4 bytes (uint32_t members in AInSample) */
    off = (off + 3U) & ~3U;

    uintptr_t nextFreeOff = off + gSampleCount * sizeof(AInPublicSampleList_t);
    /* int16_t needs 2-byte alignment */
    nextFreeOff = (nextFreeOff + 1U) & ~1U;

    /* Bounds check (all values are offsets from pool start, not addresses) */
    uintptr_t end = nextFreeOff + gSampleCount * sizeof(int16_t);
    if (end > gPoolSize) {
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
uint32_t StreamingBufferPool_EncoderSize(void) { return gEncoderSize; }
uint32_t StreamingBufferPool_SdCircularSize(void) { return gSdCircularSize; }
uint32_t StreamingBufferPool_SampleCount(void) { return gSampleCount; }
