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
 * A static BSS array that holds ALL streaming-related memory:
 * USB circular buffer, WiFi circular buffer, sample pool array,
 * and sample free-list array.  Partitioned at each stream start
 * based on active interfaces — no malloc, no fragmentation.
 *
 * Layout after partition:
 *   [USB circular | WiFi circular | encoder buf | <align> | samplePool[] | nextFree[]]
 *
 * Boot:   StreamingBufferPool_Init() sets default partition.
 * Start:  StreamingBufferPool_Partition() re-carves all regions.
 * Run:    Each module uses its region via external pointers.
 *
 * RAM budget: PIC32MZ has 512KB. This pool (276KB) + FreeRTOS heap (75KB)
 * + coherent pool (42KB) + BSS/data (~30KB) + ISR stack (8KB) = ~434KB.
 */

/** Minimum buffer sizes (from module constraints) */
#define STREAMING_USB_MIN       4096   /* USBCDC_WBUFFER_SIZE */
#define STREAMING_WIFI_MIN      1400   /* SOCKET_BUFFER_MAX_LENGTH */
#define ENCODER_BUFFER_MIN      1024   /* Must fit at least one encoded sample set */
#define ENCODER_BUFFER_DEFAULT  8192   /* Optimal for USB; SD benefits from 16384 */

/**
 * Initialize the pool and set default partition.
 * Call once at boot before USB/WiFi/sample pool init.
 *
 * @param defaultUsbSize   Initial USB buffer size
 * @param defaultWifiSize  Initial WiFi buffer size
 * @param defaultSampleCount Initial sample pool depth
 * @return true (always succeeds — static memory)
 */
bool StreamingBufferPool_Init(uint32_t defaultUsbSize, uint32_t defaultWifiSize,
                              uint32_t defaultEncoderSize, uint32_t defaultSampleCount);

/**
 * Re-partition the pool.  All regions are reset (empty).
 * Must only be called when streaming is stopped.
 *
 * @param usbSize   Desired USB circular buffer size (clamped to min/available)
 * @param wifiSize  Desired WiFi circular buffer size (clamped to min/available)
 * @param sampleCount Desired sample pool depth (0 = maximize with remaining space)
 */
void StreamingBufferPool_Partition(uint32_t usbSize, uint32_t wifiSize,
                                   uint32_t encoderSize, uint32_t sampleCount);

/** Get current USB buffer region */
void StreamingBufferPool_GetUsb(uint8_t** buf, uint32_t* size);

/** Get current WiFi buffer region */
void StreamingBufferPool_GetWifi(uint8_t** buf, uint32_t* size);

/** Get current encoder buffer region */
void StreamingBufferPool_GetEncoder(uint8_t** buf, uint32_t* size);

/** Get current sample pool region and count */
void StreamingBufferPool_GetSamplePool(void** poolBuf, int16_t** nextFreeBuf,
                                        uint32_t* count);

/** Query total pool size */
uint32_t StreamingBufferPool_TotalSize(void);
/** Query current USB partition size */
uint32_t StreamingBufferPool_UsbSize(void);
/** Query current WiFi partition size */
uint32_t StreamingBufferPool_WifiSize(void);
/** Query current encoder buffer size */
uint32_t StreamingBufferPool_EncoderSize(void);
/** Query current sample pool count */
uint32_t StreamingBufferPool_SampleCount(void);

#ifdef __cplusplus
}
#endif
