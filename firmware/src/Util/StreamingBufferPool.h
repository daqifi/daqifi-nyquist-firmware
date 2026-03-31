#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Streaming Buffer Pool — a single contiguous heap allocation that is
 * partitioned into USB and WiFi circular buffers.  Sizes can be changed
 * between streaming sessions without heap fragmentation.
 *
 * Boot:   StreamingBufferPool_Init() allocates the pool from FreeRTOS heap.
 * Start:  StreamingBufferPool_Partition() re-carves USB/WiFi regions.
 * Run:    Each module uses its region via CircularBuf_InitExternal().
 *
 * The pool is never freed — it lives for the lifetime of the device.
 */

/** Pool size.  Replaces separate USB (16KB) + WiFi (14KB) heap allocations
 *  with one contiguous block.  48KB allows USB=32KB + WiFi=14KB (USB-only mode)
 *  or USB=16KB + WiFi=28KB (WiFi-only mode) within available heap. */
#define STREAMING_POOL_SIZE (48U * 1024U)

/** Minimum buffer sizes (from module constraints) */
#define STREAMING_USB_MIN   4096   // USBCDC_WBUFFER_SIZE
#define STREAMING_WIFI_MIN  1400   // SOCKET_BUFFER_MAX_LENGTH

/**
 * Allocate the pool from FreeRTOS heap and set default partition.
 * Call once at boot before USB/WiFi task creation.
 * @param defaultUsbSize  Initial USB buffer size
 * @param defaultWifiSize Initial WiFi buffer size
 * @return true if allocation succeeded
 */
bool StreamingBufferPool_Init(uint32_t defaultUsbSize, uint32_t defaultWifiSize);

/** Get current USB buffer region (valid after Init or Partition) */
void StreamingBufferPool_GetUsb(uint8_t** buf, uint32_t* size);

/** Get current WiFi buffer region (valid after Init or Partition) */
void StreamingBufferPool_GetWifi(uint8_t** buf, uint32_t* size);

/**
 * Re-partition the pool into USB and WiFi regions.
 * Both circular buffers are reset (empty) after this call.
 * Must only be called when streaming is stopped.
 *
 * @param usbSize   Desired USB circular buffer size (clamped to min/available)
 * @param wifiSize  Desired WiFi circular buffer size (clamped to min/available)
 * @param outUsbBuf [out] Pointer to USB region
 * @param outUsbLen [out] Actual USB region size
 * @param outWifiBuf [out] Pointer to WiFi region
 * @param outWifiLen [out] Actual WiFi region size
 */
void StreamingBufferPool_Partition(
    uint32_t usbSize, uint32_t wifiSize,
    uint8_t** outUsbBuf, uint32_t* outUsbLen,
    uint8_t** outWifiBuf, uint32_t* outWifiLen);

/** Query total pool size */
uint32_t StreamingBufferPool_TotalSize(void);

/** Query current USB partition size */
uint32_t StreamingBufferPool_UsbSize(void);

/** Query current WiFi partition size */
uint32_t StreamingBufferPool_WifiSize(void);

#ifdef __cplusplus
}
#endif
