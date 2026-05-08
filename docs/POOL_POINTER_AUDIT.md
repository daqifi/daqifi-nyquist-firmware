# Streaming / Coherent Pool Publisher-Pointer Volatile Audit

**Tracking issue:** #430
**Branch:** `audit/430-pool-pointer-volatile`
**Date:** 2026-05-08
**Scope:** Pointers published by `StreamingBufferPool_Partition()` and
`CoherentPool_Alloc()` and consumed by encoder / transports across
task boundaries.

## Why this audit

PR #420 surfaced the question: when a "set-once" shared pointer is
published from one context and dereferenced from another at -O3, does
GCC ever cache the pointer value in a register on the consumer side
and continue using a stale address after the publisher swaps it?

For ADC config pointers (`gpBoardConfig` / `gpBoardRuntimeConfig` /
`gpBoardData` in `firmware/src/HAL/ADC.c`), the answer was empirically
"sometimes yes" — the #354 ch15 regression was traced to that exact
shape. Distinct from the data-volatile shape that #410 already audited.

The streaming and coherent pools have a similar shape: at every
`SYST:StartStreamData`, `StreamingBufferPool_Partition()` re-carves
194 KB of pool storage and updates several base pointers; `CoherentPool`
gets reset and re-allocated for SD-write / USB-write / WiFi-SPI DMA
buffers. If any consumer caches its pool-derived pointer in a register
across the partition swap, it continues writing to a region that's
been reassigned to a different consumer.

## Methodology

For each globally-stored pointer that is **written by Init / Partition /
Reset / setter** and **read by encoder / transport / DMA driver from a
different context**:

1. Identify the storage location and qualifier (`volatile T *` vs
   `T * volatile` vs plain `T *`)
2. Identify all consumers and the context they run in
3. Identify the synchronization between publisher and consumer
4. Tag the finding:
   - **REQUIRED-volatile** — concrete case where -O3 may register-cache
     the pointer across publish, no other barrier prevents it
   - **SAFE-via-barrier** — already protected by mutex / function-call
     boundary / quiesce-wait, no qualifier needed today
   - **SAFE-already-volatile** — qualifier already applied
   - **PRECAUTION** — safe today but a future refactor (LTO,
     `__attribute__((always_inline))`) could expose a hazard

## Inventory

### Publishers

| File | Function | Context | What it publishes |
|---|---|---|---|
| `Util/StreamingBufferPool.c` | `StreamingBufferPool_Init` | Boot, single-thread | Initial pool partition |
| `Util/StreamingBufferPool.c` | `StreamingBufferPool_Partition` | USB SCPI task (pri 7) at `SYST:StartStreamData` | New pool layout (USB/WiFi/encoder/SD-circular/sample regions) |
| `Util/CoherentPool.c` | `CoherentPool_Init` | Boot, single-thread | Empty pool |
| `Util/CoherentPool.c` | `CoherentPool_Reset` | USB SCPI task at re-partition (after quiesce) | Resets bump allocator |
| `Util/CoherentPool.c` | `CoherentPool_Alloc` | USB SCPI task during re-partition; also boot init | Allocates a partition slice |

### Consumer pointer-storage sites

| Setter | Backing storage | Consumer context | Synchronization with setter |
|---|---|---|---|
| `Streaming_SetEncoderBuffer` | `static uint8_t* volatile buffer; static volatile uint32_t bufferSize;` (`firmware/src/services/streaming.c` lines 177–178) | Streaming task (pri 6, `streaming_Task`) | **Volatile pointer** + Re-partition only when `Running == false` |
| `UsbCdc_SetWriteBuffer` | `gRunTimeUsbSttings.wCirbuf.{buf_ptr,buf_size,insertPtr,removePtr,...}` (in `__attribute__((coherent))` struct) | Streaming task (encoder), USB ProcessBytes (USB Device task pri 1) | **`wMutex` mutex** taken in setter and consumer |
| `UsbCdc_SetDmaWriteBuffer` | `gRunTimeUsbSttings.{dmaWriteBuffer, dmaWriteBufferSize}` (in `__attribute__((coherent))` struct) | `UsbCdc_SendBuffer` (called from streaming task per-packet) | No mutex; relies on pre-call `WaitIdle` in `SCPI_QuiesceAndResetCoherentPool` + per-packet function-call boundary |
| `wifi_tcp_server_SetWriteBuffer` | `gpServerData->client.wCirbuf.*` | Streaming task (encoder), wifi task (TCP drain) | **`client.wMutex` mutex** + `tcpInFlight` drain wait |
| `sd_card_manager_SetCircularBuffer` | `gSdSharedBuffer / gSdSharedBufferSize` (file-static in `sd_card_manager.c`) | SD task (pri 5) + streaming task | **`wMutex` mutex** |
| `sd_card_manager_SetWriteBuffer` | `gSDCardData.{writeBuffer,writeBufferSize,writeBufferLength,sdCardWriteBufferOffset}` | SD task | **`wMutex` mutex** |
| `WDRV_WINC_SPI_SetBuffer` | `static uint8_t* alignedBuffer; static uint32_t alignedBufferSize;` (file-static in `wdrv_winc_spi.c`, **non-volatile**) | WINC driver task (pri 2, calls `WDRV_WINC_SPISend/Receive`) | `WDRV_WINC_SPI_WaitIdle` called pre-setter; per-call function-call boundary on consumer side |

### Pool-internal pointers (StreamingBufferPool.c file-statics)

`gPool`, `gPoolSize`, `gUsbSize`, `gWifiSize`, `gEncoderSize`,
`gSdCircularSize`, `gSampleCount`, `gSampleElementSize` are all written
by `_Init` / `_Partition` and read by `_GetUsb` / `_GetWifi` / `_GetEncoder`
/ `_GetSdCircular` / `_GetSamplePool`. Both setter and getters are
called only from the **same task** (USB SCPI handler) in a sequential
function-call chain (`Partition()` → `GetUsb()` → `SetWriteBuffer()` →
…). Cross-task readers receive the **derived** pointer through a setter
call, never read these file-statics directly. Same-task sequential =
no concurrency window.

### Pool-internal pointers (CoherentPool.c file-statics)

`gCoherentPool` (the array itself), `gPoolOffset`, `gPartitionCount`,
`gPartitions[]` are written by `_Init` / `_Reset` / `_Alloc` and read
by `_Alloc` / `_FreeBytes` / `_GetInfo`. Same-task sequential. The file
header explicitly documents this (lines 28–32):
> *"Thread safety: No mutex needed. All `CoherentPool_Alloc` calls
> happen during single-threaded init… or after `_Reset` with all DMA
> consumers quiesced."*

## Per-pointer findings

### 1. `Streaming_SetEncoderBuffer` → `buffer` / `bufferSize`

**Tag: SAFE-already-volatile.**

`buffer` is declared `static uint8_t* volatile buffer` (line 177) —
**pointer-volatile** form, exactly what `T * volatile` from CLAUDE.md's
atomicity-rules section recommends for set-once-then-shared pointers.
`bufferSize` is `static volatile uint32_t bufferSize` (line 178). Every
encoder access reloads from memory.

No change required. This is the model for how the others *should*
look if any of them ever turns up a concrete hazard.

### 2. `UsbCdc_SetWriteBuffer` → `gRunTimeUsbSttings.wCirbuf.*`

**Tag: SAFE-via-barrier.**

Setter takes `gRunTimeUsbSttings.wMutex`, swaps all the cirbuf fields
atomically, releases. Consumer (`UsbCdc_Write` / `CircularBuf_*` from
streaming task) takes the same mutex to read/write the cirbuf. Mutex
take/give in FreeRTOS go through `port.c` function calls, which are
full optimization barriers in GCC.

No change required.

### 3. `UsbCdc_SetDmaWriteBuffer` → `gRunTimeUsbSttings.dmaWriteBuffer / Size`

**Tag: SAFE-via-barrier (PRECAUTION on LTO/always_inline).**

Storage is in a `__attribute__((coherent))` struct → KSEG1 → no
D-cache staleness (per MIPS32 microAptiv ordering rules, KSEG1 stores
are program-ordered). Setter has no mutex, but per
`SCPI_QuiesceAndResetCoherentPool`:

1. SD-write mode forced to NONE; SD task drained to idle
2. WiFi SPI awaited idle via `WDRV_WINC_SPI_WaitIdle(1000)`
3. USB write transfer handle awaited idle (`SCPIInterface.c:3308–3314`)
4. **Then** `_SetDmaWriteBuffer` is called

After this, the streaming task is also idle (pre-condition: `Running ==
false` for re-partition) so no consumer is mid-dereference. When
streaming resumes, the encoder task comes through `xTaskNotifyTake` /
event-loop entry — a function-call boundary that re-loads the global.

Each consumer access (`UsbCdc_SendBuffer` lines 333, 361, 370) is a
fresh read from `gRunTimeUsbSttings.dmaWriteBuffer` — no local caching
across calls.

**Precaution:** if a future refactor adds `__attribute__((always_inline))`
to `UsbCdc_SendBuffer` and inlines it into a hot streaming-task loop,
the function-call boundary disappears and the global may be register-
cached. Re-verify codegen via `xc32-objdump -d` if either happens.

### 4. `wifi_tcp_server_SetWriteBuffer` → `gpServerData->client.wCirbuf.*`

**Tag: SAFE-via-barrier.**

Setter waits for `tcpInFlight == false` (TCP send drain), then takes
`client.wMutex`, swaps cirbuf fields, releases. Consumer (encoder
write path) takes the same mutex. Mutex provides barrier.

No change required.

### 5. `sd_card_manager_SetCircularBuffer` → `gSdSharedBuffer / Size`

**Tag: SAFE-via-barrier.**

Setter takes `gSDCardData.wMutex`, swaps `gSdSharedBuffer` /
`gSdSharedBufferSize`, then re-initializes the circular buffer
descriptor (`CircularBuf_InitExternal`), releases. Consumers (SD task
read/write paths, streaming task write-buffer paths) take the same
mutex. Mutex provides barrier.

No change required.

### 6. `sd_card_manager_SetWriteBuffer` → `gSDCardData.{writeBuffer,…}`

**Tag: SAFE-via-barrier.**

Setter takes `gSDCardData.wMutex` (same one), swaps `writeBuffer`,
`writeBufferSize`, `writeBufferLength`, `sdCardWriteBufferOffset`,
releases. SD task (pri 5) reads these inside the same mutex. Barrier
applies.

No change required.

### 7. `WDRV_WINC_SPI_SetBuffer` → `alignedBuffer / alignedBufferSize`

**Tag: SAFE-via-barrier (PRECAUTION on LTO/always_inline).**

Storage is plain `static uint8_t* alignedBuffer = NULL;` — **no
volatile, no mutex**. Per `SCPI_QuiesceAndResetCoherentPool`:

1. `WDRV_WINC_SPI_WaitIdle(1000)` blocks until `transferTxHandle` and
   `transferRxHandle` are both `INVALID` (no in-flight DMA; the WINC
   driver task is between SPI ops)
2. Pool reset
3. Setter publishes new pointer

Each consumer access (`WDRV_WINC_SPISend` line 163, 164, 165;
`WDRV_WINC_SPIReceive` line 200, 201, 212) re-reads `alignedBuffer`
fresh on function entry. Inside each function there's a blocking
`OSAL_SEM_Pend` call — a function-call boundary that's a full
optimization barrier even at -O3. So even if `WDRV_WINC_SPISend` were
inlined into a tight loop, the inner `OSAL_SEM_Pend` would still
prevent register-caching across iterations.

**Precaution:** the pointer would be more honestly typed as
`static uint8_t* volatile alignedBuffer` to make the cross-context
publication explicit and survive future inlining or LTO. It's the
same situation as `gpUsbCircular` / `gpWifiCircular` / etc. in the
USB and TCP server — those happen to also be in mutex-protected
structures, but `alignedBuffer` is bare. Consider applying
`T * volatile` here as defense-in-depth alongside any future #429
work that touches the WINC SPI staging.

## Findings summary

| Tag | Count | Pointers |
|---|---|---|
| SAFE-already-volatile | 1 | `buffer` (Streaming encoder) |
| SAFE-via-barrier | 4 | `wCirbuf.*` (USB), `client.wCirbuf.*` (WiFi TCP), `gSdSharedBuffer` (SD circular), `gSDCardData.writeBuffer` (SD write) |
| SAFE-via-barrier (PRECAUTION) | 2 | `dmaWriteBuffer` (USB DMA), `alignedBuffer` (WINC SPI staging) |
| **REQUIRED-volatile** | **0** | — |

## Recommendations

### No code change required today

Every publisher pointer is either already correctly qualified or
protected by a synchronization mechanism (mutex, function-call
boundary, or quiesce-wait) that defeats the specific -O3 register-
caching hazard #420 was concerned about.

### Optional defensive hardening (low priority)

Convert `alignedBuffer` and `alignedBufferSize` in
`firmware/src/config/default/driver/winc/dev/spi/wdrv_winc_spi.c`
from plain non-volatile statics to `T * volatile` and
`volatile uint32_t`. Cost: 2 lines. Benefit: documents the
publish-from-other-context intent and survives any future LTO or
`always_inline` propagation that would erase the function-call
boundary protection currently in place.

This is **NOT** load-bearing today and not justified on its own —
flag it for inclusion alongside any other change to that file (e.g.
if #429's `__atomic_fetch_or` work ever touches WINC SPI, this is a
free fold-in).

### Watch list for future work

If any of these refactors land, re-run this audit (or at least
re-inspect codegen with `xc32-objdump -d`):

1. **LTO** is enabled in the build (currently off). LTO can erase
   call-site barriers across translation units.
2. **`__attribute__((always_inline))`** is added to any of:
   `UsbCdc_SendBuffer`, `WDRV_WINC_SPISend`, `WDRV_WINC_SPIReceive`.
3. **`OSAL_SEM_Pend`** or **`xSemaphoreTake`** is replaced with a
   spin-loop / non-call primitive that doesn't act as a compiler
   barrier.
4. The **partition swap is moved off the SCPI task** to a context that
   could overlap with a still-running streaming session (e.g. an MCU
   "auto-resize" feature). At that point all of the SAFE-via-barrier
   findings may need re-assessment.

## What's *not* in scope

- The CoherentPool / StreamingBufferPool **internal** statics
  (`gPool`, `gPoolSize`, `gPoolOffset`, etc.) are read only from
  the same task that writes them — single-task sequential, no
  cross-context window.
- Data fields hanging off these pointers (e.g. circular-buffer
  insert/remove offsets) — those are the **data-volatile** shape
  covered by #410's `VOLATILE_AUDIT.md`.
- Pointers that aren't pool-managed (e.g. `gpRuntimeConfigStream`
  in `streaming.c`) — different shape; not in #430's scope. Worth
  filing a separate audit if anyone is concerned, but no concrete
  hazard observed.
