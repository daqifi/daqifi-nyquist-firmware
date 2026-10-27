# Memory architecture

> Split out of `CLAUDE.md` on 2026-09-10. That file is loaded into **every turn of
> every agent**, and at ~38k tokens it was ~13% of all token spend; this material is
> reference — needed when you work in this area, not on every task. **It is
> unchanged, not summarised.** Read it in full before changing anything it
> describes, and update it here rather than re-adding it to `CLAUDE.md`.

The four memory regions, the RAM and heap budgets, the dynamic sample pool, and the `SYSTem:MEMory:*` runtime configuration surface.

---

### Memory Considerations

### Four Memory Regions

The firmware uses four distinct memory regions, each with different properties:

1. **Streaming Buffer Pool** (**193,024 B** static BSS — `STATIC_POOL_SIZE` is `(194 * 1024) - 1024 - 512 - 512 - 512 - 2048 - 512 - 512`, **seven** tokens, so the "194KB" label is 5.5 KB high; `firmware/src/Util/StreamingBufferPool.c`. ⛔ **BOTH PARENTS OF THIS MERGE PRINTED 193,536 FROM SIX TOKENS WITH THE SAME MULTISET, AND THEIR SIXTH TOKEN PAID FOR DIFFERENT THINGS**: this branch's for **#956**'s `wifiTcpOverBytesExtra`/`wifiTcpInflightOverflow` (two `uint32_t` in the client struct inside the static `gTcpServerContext`), main's for **#914**'s `sd_card_manager.c` `gSdAsyncError[]` (16 B: a `{int32_t code; uint32_t generation;}` pair per SD reply target, USB and WiFi-TCP, handing a GET open-failure's deferred SCPI error to the owning transport at its next command boundary). The merged tree holds **both** static sets, so both payments are owed and the union takes a seventh token: 198,656 −1024 (#665) −512 (#824) −512 (#925) −2048 (#999) −512 (#958) −512 (#956) −512 (#914) = **193,024**. Agreeing on the 193,536 both parents printed would have left 512 B unpaid and one static set unaccounted for. ⚠ **main's own bullet was internally stale here** — its header read "194,048 B" with a five-token macro while its body already said "193,536 after #914"; the header was not updated when #914 landed. Earlier history unchanged: 197,632, then 197,120 after #824 paid 512 B for `streaming.c`'s `gSdHeaderBytes`, 196,608 after #925 paid 512 B for `app_freertos.c`'s `gSdBusRecoveries`, 194,560 after **#999** paid 2,048 B for the two per-transport `ScpiContextStorage` instances (`UsbCdc.c`'s `gUsbScpiStorage` + `wifi_tcp_server.c`'s `gTcpScpiStorage`, 546 B each, replacing one shared pair — see `SCPIInterface.h`), and 194,048 after **#958** paid 512 B for `SCPIStorageSD.c`'s `gBenchNameSeq`. ⭐ Net-BSS effect is a DIFFERENT number from pool capacity and only the latter feeds the table below: #824 moved 512 B (net unchanged); #925 and #958 each trimmed 512 B for one 4-byte `uint32_t` (net −508 B); #999 trims 2,048 B for a 546 B net addition (net −1,502 B, not −2,048 — an earlier revision credited the full 2,048 to Free and overstated headroom by 546 B); #956 trims 512 B for 8 B (net −504 B); #914 trims 512 B for 16 B (net −496 B). #958 is the one payment where the link **succeeded without it** (504 B of stack slack on that branch base, map `stack … 0x21f8 8696` against `_min_stack_size` 8192); it was paid anyway, because slack on one branch base at one moment is not slack on the merge target. **On `main` at 78a0ab07 a single new `uint32_t` does not fit**: the link succeeds there and fails with `8208 bytes needed, 8200 bytes available` the moment one is added; #999's 546 B addition before its own trim dropped that to `8208 needed, 8176 available` — still failing by 32 B — measured A/B in a SHA-pinned worktree (base `1faf6758d` links, head `937528950` does not). The 8208 is the project's linker stack-size 8192 plus stackguard 16 from `configurations.xml`, so it is fixed and only "available" moves. How much slack a trim buys past the failing point has never been measured beyond one data point each time; the operative fact is that a modest new static breaks the link, so each new static needs its own payment, and this pool is where the last **six** came from. #999's 2,048 B (larger than the five 512 B payments) is a judgment call, not a measured requirement — BSS growth does not map 1:1 onto stack-region "available" (546 B added cost only 24 B of availability) — so re-confirm with a real link before relying on the exact number. **THE LINK IS THE ARBITER**: this merge was relinked and `.bss.gPoolStorage` read out of the image rather than reasoned about.)
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
| Streaming Buffer Pool | 193,536 | Static BSS (`STATIC_POOL_SIZE` = 194KB - 1KB - 512B - 512B - 512B - 2KB - 512B) |
| FreeRTOS Heap | 74,000 | Static BSS |
| Coherent Pool | 126,976 | Static coherent (KSEG1) |
| USB coherent struct | ~2,000 | Static coherent |
| Other BSS/data (globals) | ~30,591 | Static BSS — includes #999's **net +546 B** (two 546 B `ScpiContextStorage` instances replacing one shared 546 B pair), #958's 4 B `gBenchNameSeq`, #956's **net +8 B** (`wifiTcpOverBytesExtra`/`wifiTcpInflightOverflow`, two `uint32_t` in the client struct inside the static `gTcpServerContext`, before alignment), #914's **16 B** `gSdAsyncError[]` — ⚠ which main's own version of this row omitted while its pool bullet named it — and #1094's **net +17 B** (≤24 B once the `uint64_t` forces alignment padding): `detectForceReq`/`detectForceAck`/`detectForceAt`/`detectForceEver` added to `DRV_SDSPI_OBJ`, counted once because `DRV_SDSPI_INSTANCES_NUMBER` is `1U` |
| ISR stack | 8,192 | Linker-allocated |
| **Total used** | **~435,807** | ⚠ **DERIVED, NOT SUMMED.** Obtained by applying this merge's three deltas to the branch's figure: +17 (#1094), +16 (#914's `gSdAsyncError[]`), −512 (the seventh pool token). **It does NOT itemise the net BSS of the other commits this merge brings in** (#1048, #1129, and #1106 beyond `gSdAsyncError[]`), so treat it as approximate and recompute from the rows before relying on it. `used + free` is held at 523,800 by construction, so the pair moves together and the invariant proves nothing about either. |
| **Free (linker headroom)** | **~87,993** | ⚠ Derived as 523,800 − used; see the note above. |

> The `~` rows are approximations, so the column does **not** add up to `Total
> used` exactly and never has — adding the rows as printed gives ~435,766. What
> this table tracks is the **delta** against the previously documented figures,
> not an exact accounting: pre-#999 it read 196,608 / ~438,800 / ~85,000, and
> #999 moves that by −1,502 B net (−2,048 pool, +546 storage), #958 by a further −508 B net (−512 pool, +4 for `gBenchNameSeq`), #956 by a further −504 B net (−512 pool, +8 for the two `gTcpServerContext` counters), #914 by a further −496 B net (−512 pool, +16 for `gSdAsyncError[]`), and #1094 by **+17 B** (no pool trim; see the row above). Do not "fix" the
> column to sum; re-derive it from a real link if you need an exact number.

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

All USB, WiFi, **SD**, encoder, and sample pool memory comes from the unified Streaming Buffer Pool (193,536 B static BSS) — the SD *circular* buffer is in this pool; the separate SD *DMA write* buffer is not, it comes from the coherent pool. Setting any value carves it from the pool; remaining space goes to the sample pool. Setting any field to a non-zero value disables auto-balance for all fields.

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
| Sample pool CAPACITY (slots @16ch)¹ | 1,551 | 1,100 | 1,911 | 1,053 |

¹ **Capacity, not the depth in use.** These are how many slots the leftover pool space holds — what `StreamingBufferPool_Partition` computes and logs as `samples=<n>x<elementBytes>`. Note the log's second number is the **element** size (72 at 16ch), not the per-sample cost — the free-list entry is counted separately, which is why the divisor below is 74 while the log shows 72. Reconciling `1600 x 72` against a `/ 74` formula otherwise looks like an inconsistency; it is not. The depth actually available is min(that, whatever the FreeRTOS sample queue could be grown to), which on the bench came out at **1100** because the queue resize was skipped for want of ~1 KB of heap (#828). That is **not** an invariant: `AInSampleList_InitializeExternal` only clamps when it tries to GROW the queue and the heap will not stretch, so a device with more free heap grows it — and once grown it stays grown for later sessions. Read the number, do not assume it.

  All four are derived from this table's own buffer values: `(193,024 - (USB + WiFi + SD + encoder circular)) / 74`, the 74 being the 16-channel element (72 B) plus its free-list entry. Only the stream-pool rows enter the sum — the three coherent-pool DMA rows come from a different 124 KB region. The formula was **confirmed against the device** at the pre-#824 pool size: with 197,632 it gives 1,600 for USB-only, and the firmware logged `samples=1600x72 (of 1600 max, 197632 pool)` on the bench 2026-08-21 alongside `SamplePoolCount=1100` for the queue-limited depth. #824 trimmed 512 B, so each column drops ~7 slots; **#925** and **#958** trimmed 512 B each, ~21 slots between the three; **#999** trimmed 2,048 B, ~28 slots more. The same arithmetic reproduces the rows each side published before they were combined (196,096 → 1,579 / 1,127 / 1,939 / 1,081; 194,560 → 1,558 / 1,107 / 1,918 / 1,060). **#956** and **#914** then each trimmed a further 512 B — the two payments this merge unions — taking the pool to **193,024** and each column down ~14 slots from the 194,048 set.

**Whether that costs anything depends on the column, and earlier revisions of this paragraph said it never did.** The usable depth is the LESSER of the partitioned capacity and the FreeRTOS sample queue's depth (#828), so a payment is free only where the partitioned capacity stays above the queue. The #1027 pre-merge audit found this for the 512 B payments, and noted that in USB+SD an allocation of 1,082 failed after #958 where 1,088 had succeeded before it. A revision of this paragraph on `main` then called #999's payment "effectively nil cost" by the margin argument, which in USB+SD it was not. All four columns were **bench-confirmed 2026-09-13, crc 4C22BC41** (16ch PB, lane nq-a's board, serial `7E2898F46200E8A7`, one auto-balanced `SYST:STReam:BENCHmark 1` run per column, `SYST:MEM:FREE?` read after `SYST:STReam:STOP`, cross-checked against the per-transport buffer-size getters and the firmware's own `Pool partition:` log line): **USB-only 1,551, WiFi-only 1,100, SD-only 1,911, USB+SD 1,053 — at a pool of 194,048**, every predicted figure landing exactly, including the two columns that had been arithmetic-only at every pool size previously shipped. That run's `SamplePoolClampedSlots` read 0 in all four columns rather than the ~486 the 2026-09-08 USB-only reading showed, an artifact of that boot's sample queue having already grown past every column's need (the queue never shrinks back), not a change to the capacity figures. Earlier figures still in circulation (~1,618 / ~1,178 / ~1,921 / ~1,086) used the old, wrong 512-byte SD-circular value and are wrong twice over.

> ⚠ **THE BENCH CONFIRMATION ABOVE WAS TAKEN AT 194,048 AND THIS MERGE MOVES THE POOL TWO PAYMENTS OFF IT.** By the section's own rule (512/74 = ~7 slots at 16 channels) the four columns become approximately **USB-only ~1,537, WiFi-only ~1,086, SD-only ~1,897, USB+SD ~1,039** at 193,024 — **DERIVED FROM THE BENCH-CONFIRMED 194,048 FIGURES BY THAT RULE, not recomputed from the buffer table and not measured.** Only the USB-only column reproduces exactly from `(pool − circulars)/74`, so the buffer set behind the other three is not established and all four are approximate. Re-confirm `SYST:MEM:FREE?`'s `SamplePoolPartitioned` against ~1,537 (USB-only) on a device flashed at this size before relying on any of them.

> ⛔ **AND WiFi-ONLY HAS CROSSED, WHICH IS THE PART THAT DOES NOT DEPEND ON THE ±1.** At 194,048 it sat at exactly 1,100 — the default queue depth — and both parents warned that the next 512 B payment would make the pool the binding constraint there. **This merge contains two such payments.** WiFi-only falls to ~1,086, below the clamp, joining USB+SD which was already below it at ~1,039. So **two of the four columns are now pool-bound rather than one**, and each future 512 B payment costs ~7 *usable* slots in both. Count both columns before paying again; there is no longer a column with headroom.


**Implementation:** `firmware/src/Util/StreamingBufferPool.c` (unified pool), `firmware/src/Util/CoherentPool.c` (DMA pool), `firmware/src/services/streaming.c` (`ComputeAutoBuffers`), `firmware/src/state/data/AInSample.c` (`InitializeExternal`), `firmware/src/services/SCPI/SCPIInterface.c` (SCPI callbacks), `firmware/src/state/runtime/StreamingRuntimeConfig.h` (MemoryConfig struct), `firmware/src/config/default/driver/winc/dev/spi/wdrv_winc_spi.c` (WiFi SPI staging)

### Other Memory Constraints
- DMA buffers must be cache-aligned and in KSEG1 (coherent pool or coherent attribute)
- Bootloader reserves memory at 0x9D000000
- Application starts at 0x9D000480 (after bootloader)
- USB struct must remain `__attribute__((coherent))` — moving DMA buffers to pool causes boot failure
