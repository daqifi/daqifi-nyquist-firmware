# Memory architecture

> Split out of `CLAUDE.md` on 2026-09-10. That file is loaded into **every turn of
> every agent**, and at 38k tokens it was ~13% of all token spend; this material is
> reference — needed when you work in this area, not on every task. **It is
> unchanged, not summarised.** Read it in full before changing anything it
> describes, and update it here rather than re-adding it to `CLAUDE.md`.

The four memory regions, the RAM and heap budgets, the dynamic sample pool, and the `SYSTem:MEMory:*` runtime configuration surface.

---

### Memory Considerations

### Four Memory Regions

The firmware uses four distinct memory regions, each with different properties:

1. **Streaming Buffer Pool** (**197,120 B** static BSS — `STATIC_POOL_SIZE` is `(194 * 1024) - 1024 - 512`, so the "194KB" label is 1.5 KB high; `firmware/src/Util/StreamingBufferPool.c`. Was 197,632 until #824 trimmed a further 512 B to pay for `streaming.c`'s `gSdHeaderBytes`, the per-session SD file header — net BSS unchanged, the bytes just moved.)
   - Single `static uint8_t gPoolStorage[]` array, partitioned at each stream start
   - Contains: USB circular buffer + WiFi circular buffer + encoder buffer + SD circular buffer + sample pool + free-list
   - Layout: `[USB circ | WiFi circ | encoder | SD circ | <align> | samplePool[] | nextFree[]]`
   - Re-partitioned at each `StartStreamData` based on active interfaces
   - Zero runtime malloc — all resizing is pointer arithmetic within the pool
   - Auto-balance: USB-only → USB=64KB, WiFi=min; the sample pool takes what is left
   - Query: `SYST:MEM:FREE?` → `SamplePoolCount`, `SamplePoolBytes`

2. **Coherent Pool** (124KB static, DMA-safe, `firmware/src/Util/CoherentPool.c`)
   - Single `__attribute__((coherent, aligned(16)))` array in KSEG1 (uncached)
   - Bump allocator with named partitions, reset and re-partitioned at each stream start
   - Contains: SD DMA write buffer + USB DMA write buffer + WiFi SPI staging buffer. All three auto-balanced at stream start.
   - Query: `SYST:MEM:FREE?` → `CoherentPoolTotal`, `CoherentPoolFree`

3. **FreeRTOS Heap** (74KB, cached, `configTOTAL_HEAP_SIZE` in `FreeRTOSConfig.h` — 75000 was cut to 74000 in #667, because #666 + #667 co-resident overflowed the linker stack region by 208 B and the heap is BSS)
   - heap_4 (best-fit with coalescence), allocated from `.bss`
   - Contains: task stacks, FreeRTOS TCBs/queues/mutexes, sample FreeRTOS queue
   - Streaming buffers and sample pool are NOT in heap (moved to Streaming Buffer Pool)
   - Query: `SYST:MEM:FREE?` → `HeapTotal`, `HeapFree`, `HeapMinEverFree`

4. **USB Coherent Struct** (~2KB static, `gRunTimeUsbSttings __attribute__((coherent))`)
   - USB CDC DMA read buffer (512B) embedded in coherent struct
   - DMA write buffer pointer points into coherent pool (auto-balanced at stream start)
   - Must remain coherent for USB hardware driver DMA compatibility

### RAM Budget (PIC32MZ 512KB)

| Region | Bytes | Source |
|--------|------:|--------|
| Streaming Buffer Pool | 197,120 | Static BSS (`STATIC_POOL_SIZE` = 194KB - 1KB - 512B) |
| FreeRTOS Heap | 74,000 | Static BSS |
| Coherent Pool | 126,976 | Static coherent (KSEG1) |
| USB coherent struct | ~2,000 | Static coherent |
| Other BSS/data (globals) | ~30,000 | Static BSS |
| ISR stack | 8,192 | Linker-allocated |
| **Total used** | **~438,800** | |
| **Free (linker headroom)** | **~85,000** | |

### Heap Allocation Map (74KB total, ~62KB used at boot)

| Consumer | Bytes | Source |
|----------|------:|--------|
| Task stacks (per Task Priority Map + idle/timer daemon) | ~37,500 | `xTaskCreate` (profiled) |
| FreeRTOS TCBs, mutexes, kernel | ~5,000 | Kernel internals |
| Sample FreeRTOS queue | ~4,500 | `xQueueCreate` in `AInSample.c` |
| DIO sample queue | ~3,200 | `xQueueCreate` in `DIOSample.c` |
| WiFi event queue | ~480 | `xQueueCreate` in `wifi_manager.c` |
| Other queues/mutexes | ~3,000 | Various modules |
| **Total used** | **~62,000** | |
| **Free after boot** | **~13,000** | `xPortGetFreeHeapSize()` |

**Note**: Sample pool and circular buffers are in the Streaming Buffer Pool, NOT heap. `HeapMinEverFree` should stay above 0. Monitor via `SYST:MEM:FREE?`. Task stack health via `SYST:MEM:STACk?`.

### Dynamic Sample Pool

The sample pool lives inside the Streaming Buffer Pool (static BSS). It is re-partitioned at each `StartStreamData` — the pool depth adjusts automatically based on how much space remains after USB/WiFi/encoder buffers are carved out. O(1) free-list allocation is preserved.

- **Default size**: 1100 samples (`DEFAULT_AIN_SAMPLE_COUNT` in `AInSample.h`)
- **Range**: 100–10000 samples (`MIN_AIN_SAMPLE_COUNT`–`MAX_AIN_SAMPLE_COUNT`)
- **Memory per sample**: depends on enabled channels (compact pool): 1ch=14 bytes, 4ch=26 bytes, 8ch=42 bytes, 16ch=74 bytes (element + 2-byte free-list entry). Stride computed at stream start from `AInSampleList_ElementSize(channelCount)`.
- **Resize**: `StreamingBufferPool_Partition()` re-carves the pool, then `AInSampleList_InitializeExternal()` swaps the memory pointers. FreeRTOS queue is reused (not reallocated) across sessions.
- **When resized**: At each `StartStreamData` via `SCPI_StartStreaming`
- **Typical values**: **1100 usable**, and the reason is not what you would guess. Measured 2026-08-21, 16ch USB-only, straight from the device's own log:
  ```
  Pool partition: USB=65536 WiFi=1400 enc=8192 sdCirc=4096 samples=1600x72 (of 1600 max, 197632 pool)
  Sample queue resize skipped: need 6480, heap free 6536
  ```
  In auto mode `MemoryConfig.samplePoolCount` is **0**, which `StreamingBufferPool_Partition` reads as *maximize to fit* — so the partition really does carve **1600** slots. The depth then falls back to 1100 at the NEXT stage: `AInSampleList_InitializeExternal` will not grow the FreeRTOS sample queue unless `freeHeap >= needed + 1024`, and here 6536 < 6480 + 1024, so it logs `Sample queue resize skipped` and keeps the boot queue size (`DEFAULT_AIN_SAMPLE_COUNT` = 1100). `SYST:MEM:FREE?` reports that queue-limited number.
  Two consequences worth knowing: on that run the ~500-slot difference (~36 KB) was carved from the streaming pool but **not usable**, and the clamp is reported by `SYST:MEM:FREE?` as `SamplePoolPartitioned` (1600 here) alongside `SamplePoolCount` (the usable 1100), with the shortfall as `SamplePoolClampedSlots` (#828). Before that fix it was visible only as the `Sample queue resize skipped` log line above. The clamp is heap-state dependent, not fixed: it fires only when the queue must GROW and the heap will not stretch, and a queue that did grow stays grown, so the gap varies by device state rather than being a constant 500. Earlier revisions of this file claimed "~585" here and "~1,618" in the table below; both were wrong, and the table's figures are **capacity**, which is the 1600 the partition computes, not the depth you get
- **Peak usage**: Typically 2-4 samples (at 3kHz 16ch). Pool depth provides burst absorption headroom.

### SCPI Dynamic Memory Configuration

All setters reject changes while streaming is active (`SCPI_ERROR_EXECUTION_ERROR`).
Settings take effect at next `StartStreamData`. Runtime-only — not NVM-persisted, reset on reboot.

```bash
SYSTem:MEMory:SD:BUFfer <bytes>       # Set SD circular buffer size
SYSTem:MEMory:SD:BUFfer?              # Query (default: 32768)
SYSTem:MEMory:WIFI:BUFfer <bytes>     # Set WiFi circular buffer size
SYSTem:MEMory:WIFI:BUFfer?            # Query (default: 14000)
SYSTem:MEMory:USB:BUFfer <bytes>      # Set USB circular buffer size
SYSTem:MEMory:USB:BUFfer?             # Query (default: 16384)
SYSTem:MEMory:ENCoder:BUFfer <bytes>  # Set encoder buffer size
SYSTem:MEMory:ENCoder:BUFfer?         # Query (default: 8192)
SYSTem:MEMory:SAMPle:POOL <count>     # Set sample pool depth (0=auto)
SYSTem:MEMory:SAMPle:POOL?            # Query (default: 1100)
SYSTem:MEMory:FREE?                   # Full memory diagnostics
SYSTem:MEMory:AUTO                    # Auto-balance for enabled interfaces
```

All USB, WiFi, **SD**, encoder, and sample pool memory comes from the unified Streaming Buffer Pool (197,120 B static BSS) — the SD *circular* buffer is in this pool; the separate SD *DMA write* buffer is not, it comes from the coherent pool. Setting any value carves it from the pool; remaining space goes to the sample pool. Setting any field to a non-zero value disables auto-balance for all fields.

**Setter Bounds:**

| Command | Min | Max | Constraint |
|---------|----:|----:|------------|
| `SD:BUFfer` | 4096 | 65536 | Must be multiple of 512 (sector alignment). SD circular buffer is in streaming pool. |
| `WIFI:BUFfer` | 1400 | 65536 | Min = SOCKET_BUFFER_MAX_LENGTH |
| `USB:BUFfer` | 4096 | 65536 | Min = `USBCDC_WBUFFER_SIZE` (the circular buffer must hold one full USB CDC write). Distinct from `STREAMING_USB_MIN` (2048), which is only the partition's inactive-interface floor. |
| `ENCoder:BUFfer` | 0 or 1024 | 65536 | Encoder staging buffer (0 = auto). 8KB optimal for USB, 16KB helps SD throughput. |
| `SAMPle:POOL` | 0 or 100 | 10000 (`MAX_AIN_SAMPLE_COUNT`) | 0 = maximize with remaining pool space. Achievable count is partition-limited (channel-count dependent); the setter range is the architectural max. |

**`SYST:MEM:FREE?` Response Fields:**

| Field | Description |
|-------|-------------|
| `HeapTotal` | Total FreeRTOS heap (74000) |
| `HeapFree` | Currently free heap bytes |
| `HeapUsed` | Currently used heap bytes |
| `HeapMinEverFree` | Lowest heap free since boot (high-water mark) |
| `CoherentPoolTotal` | Total coherent pool (126976) |
| `CoherentPoolFree` | Free coherent pool bytes |
| `SdCircularSize` | Current SD circular buffer partition size |
| `SamplePoolCount` | Current **usable** sample pool depth — what the free-list and the FreeRTOS queue actually hold. May be lower than `SamplePoolPartitioned`; see below. |
| `SamplePoolPartitioned` | Slots the partition carved (#828). In auto mode (`samplePoolCount == 0` = *maximize to fit*) this is what fits in the leftover pool space, and it is the number the `Pool partition: … samples=<n>x<sz>` log line reports. **`0` is a fault indicator, not a pre-stream state** — the pool is partitioned at boot (`StreamingBufferPool_Init` in `app_freertos.c` calls `Partition` with `DEFAULT_AIN_SAMPLE_COUNT`), so a healthy device reports a non-zero value before it has ever streamed, equal to `SamplePoolCount` with `ClampedSlots` 0. The only path leaving it at 0 is the `Pool too small` bail-out, which also zeroes the buffer sizes. |
| `SamplePoolClampedSlots` | `Partitioned − Count`, i.e. slots carved but unusable (#828). Non-zero when `AInSampleList_InitializeExternal` declined to **grow** the FreeRTOS sample queue — it requires `freeHeap >= needed + 1024`, and on refusal keeps the previous queue size. Before #828 this shortfall appeared only as a `Sample queue resize skipped` LOG_E line. It is heap-state dependent, not a constant: a queue that did grow stays grown. |
| `SamplePoolBytes` | Sample pool data memory (count × per-channel-count stride) |
| `SampleNextFreeBytes` | Free-list array memory (count × 2) |
| `SampleQueueBytes` | FreeRTOS queue overhead estimate |

**Auto-balance** (`Streaming_ComputeAutoBuffers()`): active interfaces get compile-time circular-buffer defaults (USB=64KB, WiFi=96KB — `STREAMING_WIFI_WIFI_ONLY`, #497 — SD=32KB), inactive get minimums; encoder is 16KB when SD is active (larger writes reduce SPI overhead), 8KB otherwise; the three coherent-pool DMA buffers (SD write, USB write, WiFi SPI staging) split the 124KB coherent pool by weighted shares (SD=5, USB=3, WiFi=2 — single-active gets everything); the sample pool gets whatever stream-pool space remains. When all `MemoryConfig` fields are zero (boot default), this runs automatically at each `StartStreamData`; setting any field non-zero disables auto mode for all fields. The `SYST:MEM:AUTO` response reports the computed `SdDma=`, `UsbDma=`, `WifiDma=`, `Encoder=` sizes.

**Auto-Balance Buffer Sizing by Active Interface:**

> The inactive-SD circular figure is **4,096**, not the 512 an earlier revision
> of this table showed. `Streaming_ComputeAutoBuffers` hands inactive SD
> `STREAMING_SD_CIRCULAR_MIN`, and `StreamingBufferPool_Partition` clamps
> anything smaller up to it, so 512 is unreachable — and a `MEM:SD:BUFfer?`
> reading of 512 is the **fault signature of #703** (a shrunk SD circular
> silently breaking `SD:GET`), not a normal value. The per-column capacity
> figures below have been recomputed accordingly — see the footnote under the
> table for the derivation and the device log that confirms it.

The `StreamingInterface` enum exposes four combinations: `USB`, `WiFi`, `SD`, and `UsbAndSd` — WiFi is always solo (SPI bus shared with SD; USB+WiFi was never wired into the enum). Values below are for 16-channel `AInSampleList_ElementSize` (**72 B** + 2 B free-list = **74 B/sample**). The old "74 B + 2 B = 76" double-counted the free-list byte: 74 is already element+free-list. `AInSample.h` states the element size directly ("72 bytes (8-byte header + 16 x 4-byte values)"), and `SYST:MEM:FREE?` reports `SampleElementBytes=72` at 16 channels (measured 2026-08-21).

| Buffer | USB only | WiFi only | SD only | USB+SD |
|--------|---:|---:|---:|---:|
| USB circular (stream pool) | 65,536 | 2,048 | 2,048 | 65,536 |
| WiFi circular (stream pool) | 1,400 | 98,304 | 1,400 | 1,400 |
| SD circular (stream pool) | 4,096 | 4,096 | 32,768 | 32,768 |
| Encoder (stream pool) | 8,192 | 8,192 | 16,384 | 16,384 |
| SD DMA write (coherent) | 512 | 512 | 124,368 | 77,922 |
| USB DMA write (coherent) | 124,368 | 512 | 512 | 46,958 |
| WiFi SPI staging (coherent) | 2,048 | 125,904 | 2,048 | 2,048 |
| Sample pool CAPACITY (slots @16ch)¹ | 1,593 | 1,141 | 1,952 | 1,095 |

¹ **Capacity, not the depth in use.** These are how many slots the leftover pool space holds — what `StreamingBufferPool_Partition` computes and logs as `samples=<n>x<elementBytes>`. Note the log's second number is the **element** size (72 at 16ch), not the per-sample cost — the free-list entry is counted separately, which is why the divisor below is 74 while the log shows 72. Reconciling `1600 x 72` against a `/ 74` formula otherwise looks like an inconsistency; it is not. The depth actually available is min(that, whatever the FreeRTOS sample queue could be grown to), which on the bench came out at **1100** because the queue resize was skipped for want of ~1 KB of heap (#828). That is **not** an invariant: `AInSampleList_InitializeExternal` only clamps when it tries to GROW the queue and the heap will not stretch, so a device with more free heap grows it — and once grown it stays grown for later sessions. Read the number, do not assume it.

  All four are derived from this table's own buffer values: `(197,120 - (USB + WiFi + SD + encoder circular)) / 74`, the 74 being the 16-channel element (72 B) plus its free-list entry. Only the stream-pool rows enter the sum — the three coherent-pool DMA rows come from a different 124 KB region. The formula was **confirmed against the device** at the pre-#824 pool size: with 197,632 it gives 1,600 for USB-only, and the firmware logged `samples=1600x72 (of 1600 max, 197632 pool)` on the bench 2026-08-21 alongside `SamplePoolCount=1100` for the queue-limited depth. #824 trimmed the pool by 512 B, so each column drops by ~7 slots — off a *partitioned* capacity that already exceeds the usable depth by ~500, which is why the change costs nothing in practice. The figures above are the recomputed ones and have **not** been re-confirmed on the bench; expect `samples=1593x72 (of 1593 max, 197120 pool)` in that log line. Earlier figures still in circulation (~1,618 / ~1,178 / ~1,921 / ~1,086) were computed with the old, wrong 512-byte SD-circular value and are wrong twice over.


**Implementation:** `firmware/src/Util/StreamingBufferPool.c` (unified pool), `firmware/src/Util/CoherentPool.c` (DMA pool), `firmware/src/services/streaming.c` (`ComputeAutoBuffers`), `firmware/src/state/data/AInSample.c` (`InitializeExternal`), `firmware/src/services/SCPI/SCPIInterface.c` (SCPI callbacks), `firmware/src/state/runtime/StreamingRuntimeConfig.h` (MemoryConfig struct), `firmware/src/config/default/driver/winc/dev/spi/wdrv_winc_spi.c` (WiFi SPI staging)

### Other Memory Constraints
- DMA buffers must be cache-aligned and in KSEG1 (coherent pool or coherent attribute)
- Bootloader reserves memory at 0x9D000000
- Application starts at 0x9D000480 (after bootloader)
- USB struct must remain `__attribute__((coherent))` — moving DMA buffers to pool causes boot failure
