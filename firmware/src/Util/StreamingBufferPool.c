#define LOG_LVL LOG_LEVEL_DEBUG
#define LOG_MODULE LOG_MODULE_GENERAL

#include "StreamingBufferPool.h"
#include "Logger.h"
#include "osal/osal.h"
#include <string.h>

static uint8_t* gPool = NULL;
static uint32_t gPoolSize = 0;
static uint32_t gUsbSize = 0;
static uint32_t gWifiSize = 0;

bool StreamingBufferPool_Init(uint32_t defaultUsbSize, uint32_t defaultWifiSize) {
    if (gPool != NULL) return true;  // Already initialized

    gPool = (uint8_t*)OSAL_Malloc(STREAMING_POOL_SIZE);
    if (gPool == NULL) {
        LOG_E("StreamingBufferPool: failed to allocate %u bytes",
              (unsigned)STREAMING_POOL_SIZE);
        return false;
    }
    gPoolSize = STREAMING_POOL_SIZE;
    LOG_I("StreamingBufferPool: %u bytes at %p",
          (unsigned)gPoolSize, (void*)gPool);

    // Set default partition so USB/WiFi can init before first stream start
    uint8_t* usbBuf; uint32_t usbLen;
    uint8_t* wifiBuf; uint32_t wifiLen;
    StreamingBufferPool_Partition(defaultUsbSize, defaultWifiSize,
                                  &usbBuf, &usbLen, &wifiBuf, &wifiLen);
    return true;
}

void StreamingBufferPool_GetUsb(uint8_t** buf, uint32_t* size) {
    *buf = gPool;
    *size = gUsbSize;
}

void StreamingBufferPool_GetWifi(uint8_t** buf, uint32_t* size) {
    *buf = (gPool != NULL) ? gPool + gUsbSize : NULL;
    *size = gWifiSize;
}

void StreamingBufferPool_Partition(
    uint32_t usbSize, uint32_t wifiSize,
    uint8_t** outUsbBuf, uint32_t* outUsbLen,
    uint8_t** outWifiBuf, uint32_t* outWifiLen)
{
    if (gPool == NULL) {
        *outUsbBuf = NULL; *outUsbLen = 0;
        *outWifiBuf = NULL; *outWifiLen = 0;
        return;
    }

    // Clamp minimums
    if (usbSize < STREAMING_USB_MIN) usbSize = STREAMING_USB_MIN;
    if (wifiSize < STREAMING_WIFI_MIN) wifiSize = STREAMING_WIFI_MIN;

    // If requested total exceeds pool, scale down proportionally
    uint32_t total = usbSize + wifiSize;
    if (total > gPoolSize) {
        // Give each their minimum, split remaining by request ratio
        uint32_t remaining = gPoolSize - STREAMING_USB_MIN - STREAMING_WIFI_MIN;
        uint32_t usbExtra = usbSize - STREAMING_USB_MIN;
        uint32_t wifiExtra = wifiSize - STREAMING_WIFI_MIN;
        uint32_t extraTotal = usbExtra + wifiExtra;
        if (extraTotal > 0) {
            usbSize = STREAMING_USB_MIN + (uint32_t)((uint64_t)usbExtra * remaining / extraTotal);
            wifiSize = STREAMING_WIFI_MIN + (remaining - (usbSize - STREAMING_USB_MIN));
        } else {
            usbSize = STREAMING_USB_MIN;
            wifiSize = STREAMING_WIFI_MIN;
        }
    }

    gUsbSize = usbSize;
    gWifiSize = wifiSize;

    // USB at start of pool, WiFi after USB
    *outUsbBuf = gPool;
    *outUsbLen = usbSize;
    *outWifiBuf = gPool + usbSize;
    *outWifiLen = wifiSize;
}

uint32_t StreamingBufferPool_TotalSize(void) { return gPoolSize; }
uint32_t StreamingBufferPool_UsbSize(void)   { return gUsbSize; }
uint32_t StreamingBufferPool_WifiSize(void)  { return gWifiSize; }
