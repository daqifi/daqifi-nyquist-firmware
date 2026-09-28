# PIC32MZ2048EFM144 reference

> Split out of `CLAUDE.md` on 2026-09-10. That file is loaded into **every turn of
> every agent**, and at ~38k tokens it was ~13% of all token spend; this material is
> reference — needed when you work in this area, not on every task. **It is
> unchanged, not summarised.** Read it in full before changing anything it
> describes, and update it here rather than re-adding it to `CLAUDE.md`.

MCU facts this firmware depends on: atomicity and concurrency rules, cache/DMA, the clock tree, the FPU, FreeRTOS configuration, the task priority map, and the silicon errata that affect us.

---

### MCU Reference: PIC32MZ2048EFM144

**Architecture**: MIPS32 M-Class (microAptiv), 252 MHz (chip-rated max, DS60001320H §39; raised from 200 MHz in #487), 32-bit data bus
**Flash**: 2MB | **RAM**: 512KB | **L1 Cache**: 16KB I-cache + 4KB D-cache
**Package**: 144-pin LQFP
**Datasheet**: [DS60001320](https://www.microchip.com/en-us/product/PIC32MZ2048EFM144)
**Errata**: [DS80000663](https://ww1.microchip.com/downloads/aemDocuments/documents/MCU32/ProductDocuments/Errata/PIC32MZ-Embedded-Connectivity-with-Floating-Point-Unit-Family-Silicon-Errata-DS80000663.pdf) (Rev R, silicon A1/A3/B2/B3)

### Atomicity & Concurrency Rules

- **32-bit reads and writes are atomic** on the PIC32MZ bus. A simple `x = 0` or `return x` on a `uint32_t`/`uint16_t` does NOT need a critical section.
- **Read-modify-write** (`x |= bit`, `x &= ~bit`, `x++`, `x += n`) is NOT atomic — use `taskENTER_CRITICAL()`/`taskEXIT_CRITICAL()`.
- **64-bit operations** (`uint64_t` increment, struct copy) always need a critical section.
- **`volatile`** is needed when a variable is written by one task/ISR and read by another — it prevents the compiler from caching the value in a register. `volatile` alone does NOT make RMW atomic; you still need critical sections for `+=`, `|=`, etc.
- **Set-once-then-shared pointers** (`static T *gp` set in `Init()`, dereferenced from tasks and ISRs): qualifier forms differ — `volatile T *p` (data-volatile) is the strictly-correct fix when `p->field` is RMW'd or read across task↔ISR boundaries, but cascades through every API taking a derived `T *`; `T * volatile p` (pointer-volatile) only forces pointer reloads and is **not** a concurrency guarantee. Rules: (1) cross-context RMW'd data → data-volatile (or volatile on the specific field); (2) `volatile` never makes RMW atomic — critical sections still required; (3) **don't add qualifiers speculatively** — Microchip's guidance (DS90003269A) prescribes critical sections/mutexes, not volatile, for shared data. The empirical audit (`docs/SET_ONCE_POINTER_AUDIT.md`, 2026-05-10) found the qualifier observable in codegen but redundant in this codebase: every consumer path crosses opaque call boundaries (FreeRTOS/PLIB/BoardData) that already force reloads at -O3 with no LTO on XC32 v4.60; volatile/no-volatile A/B'd to identical failure baselines. Re-audit if LTO, `always_inline` in hot loops, or a compiler change lands. Methodology lesson from that audit: **never conclude load-bearing-ness from n=1 bench trials** — multi-trial both branches first. (Related history: the #354 ch15 regression that motivated the #421 volatile speculation was actually fixed by a hardware TRGSRC register configuration — **PR #422 / `971fac37f`**, not by qualifiers. The hash previously given here, `96e7c840`, is not a valid object in this repo. #422 states the conclusion outright — *"Root cause is a hardware-class issue, not an optimizer issue"* — and its post-mortem exists specifically so the optimizer-bisect dead end is not re-walked — `docs/406_O3_INVESTIGATION.md`, **archived out of the working tree in `218cab263` and preserved at `971fac37f`**, so read it with `git show 971fac37f:docs/406_O3_INVESTIGATION.md`. Per **ADC FRM DS60001344E Register 22-19** a Class 1/2 input joins the MODULE7 scan only at `TRGSRC=0b011` (STRIG); Microchip's generated `plib_adchs.c` boots those channels at `TRGSRC=1`, and pre-#282 they worked only because the streaming task fired GSWTRG manually each cycle. The `-O1` vs `-O3` difference that framed the whole investigation was a timing coincidence, not a miscompile.)
- Do not add unnecessary critical sections around plain 32-bit stores/loads — it adds interrupt latency for no benefit.
- **ISR context vs task context**: `taskENTER_CRITICAL()`/`taskEXIT_CRITICAL()` is for **task context only**. Inside an ISR, use `taskENTER_CRITICAL_FROM_ISR()`/`taskEXIT_CRITICAL_FROM_ISR()`. In our firmware, hardware ISRs (streaming timer, ADC EOS) immediately defer to FreeRTOS tasks via `xTaskNotifyGive()`/`ulTaskNotifyTake()`, so all sample processing and critical section usage runs in task context — never directly in ISR handlers.
- **FPU in RTOS tasks**: Any task that uses floating-point (`float` or `double`) **must** call `portTASK_USES_FLOATING_POINT()` at the start of its task function, before any FP operations. This tells FreeRTOS to save/restore the 32×64-bit FPU registers on context switches. Without this call, FPU register state will be corrupted when switching between tasks. Currently registered: `app_USBDeviceTask`, `app_WifiTask`, `app_PowerAndUITask`, `streaming_Task`. **Pure-integer (no FPU):** `_Streaming_Deferred_Interrupt_Task` (intentional — saves 32 × 64-bit register save/restore per context switch), `app_SDCardTask`, `lWDRV_WINC_Tasks` (WINC driver — also runs the UDP announce / discovery callback). **Code that runs in a pure-integer task MUST NOT read or cast `double`/`float` fields** — that compiles to FPU instructions which pick up garbage from whichever FPU-using task last preempted. PR #369 fixed one instance of this (`adcMax = (uint32_t)Resolution - 1` in the streaming deferred task, producing 0x80000FE6 instead of 4095). Defense: store config values that pure-int tasks need to read as integer types (`uint32_t`, etc), not `double` — see `MC12bModuleConfig.Resolution` / `AD7609ModuleConfig.Resolution` in `AInConfig.h` for the canonical pattern.
- **`BoardRunTimeConfig_Get()` never returns NULL** — it indexes into a static array initialized at boot. Do not add NULL checks on its return value; no existing SCPI callback checks it, and adding guards creates inconsistency for zero safety benefit. Qodo/automated reviewers will repeatedly suggest this — ignore it.

### Cache & DMA

- **L1 data cache is write-back** — DMA buffers MUST use `__attribute__((coherent))` or be placed in KSEG1 (uncached) to avoid stale data.
- **Cache line size**: 16 bytes — DMA buffers sharing a cache line with other data will cause corruption. Use `__attribute__((aligned(16)))`.
- **No hardware cache coherency** — software must invalidate/flush cache around DMA transfers. Harmony drivers handle this for SPI, USB, etc.
- **Coherent attribute** (`__attribute__((coherent, aligned(16)))`) maps buffers to uncached address space — simplest solution for DMA buffers.

### Clock Tree (as configured — #487, 2026-07-01)

| Clock | Source | Frequency | Peripherals |
|-------|--------|-----------|-------------|
| SYSCLK | SPLL (24MHz POSC / 3 × 63 / 2) | 252 MHz | CPU, core timer (÷2 = 126 MHz) |
| PBCLK1 | SYSCLK/3 | 84 MHz | Watchdog |
| PBCLK2 | SYSCLK/3 | 84 MHz | I2C, UART, SPI2/4/6 |
| PBCLK3 | SYSCLK/3 | 84 MHz | Timers (FreeRTOS tick T1, streaming TMR4/5), OC, IC, ADC control clock |
| PBCLK5 | SYSCLK/3 | 84 MHz | Flash, Crypto, USB regs |

The PBxDIV /3 writes live in `SystemInit`/`initialization.c` (Harmony's `CLK_Initialize` only sets PMD, so without them the buses run at the reset-default /2 — which would be 126 MHz, over the 100 MHz bus spec). Single defines that must track the clock tree: `TIMER_CLOCK_FRQ` (TimerApi.h), `configPERIPHERAL_CLOCK_HZ` (FreeRTOSConfig.h), `CORE_TIMER_FREQUENCY` (plib_coretimer.h), `SYS_TIME_CPU_CLOCK_FREQUENCY` (configuration.h), `USE_FREQ_CONFIGURED_IN_CLOCK_MANAGER` (drv_spi_local.h).

**SPI4 (SD + WINC) is clocked from PBCLK2, not REFCLK1** — `MCLKSEL=0` in `plib_spi4_master.c`, and no `REFO1CON` configuration exists anywhere in the tree (the pre-#487 revision of this table claiming "REFCLK1 passthrough, MCLKSEL=1" was stale: the measured 16.67 MHz SPI4 clock matched PBCLK2=100/BRG=2 exactly, not REFCLK1=200). SPI4 BRG formula: `SPI_CLK = 84MHz / (2 × (BRG + 1))` — SDSPI's 20 MHz request now rounds to 21 MHz (BRG=1); at the old 100 MHz PBCLK2 it rounded to 16.67 MHz (BRG=2).

> **⚠️ The Session-24 throughput characterization tables live in `docs/STREAMING_AND_ADC.md`, immediately preceded there by the caveat that they pre-date #487 (measured at 200 MHz/100 MHz) and a list of what has and hasn't been re-fit for 252 MHz.** Read that caveat before trusting any rate in those tables; it moved there with the split so it sits next to the data it qualifies instead of pointing at it from this file.
>
> **⚠️ CAP WORK MUST PIN `CONFigure:VOLTage:PRECision` (#832 / test-suite #233).**
> `csv_encoder` takes an integer fast path (`int_to_str`) at precision **0** —
> the value a fresh NQ1 actually persists (see the ⚠️ under "Voltage Output
> Precision" in `docs/STREAMING_AND_ADC.md` — #910) — and formats a float per channel per sample at
> precision **4**, the value `NQ1BoardConfig.c` *declares* as the board default
> but that a fresh device never reaches. That is a first-order cost, not a
> rounding detail: an A/B at one rate measured
> precision 0 clean against precision **4 losing 10.8 %**, at byte rates within
> 1 % of each other — encoder CPU, not bandwidth. Precision is NVM-backed, so a
> board that some earlier test pinned and never restored silently changes what
> every subsequent ceiling means. A #832 CSV refit fitted to unpinned
> (precision-0) ceilings dropped **21,286 samples at its own new cap** when
> flashed. `test_overnight_characterization.py --precision <0-10|device>` pins it
> and records the READ-BACK value per row as `precision_actual`; use it for
> anything whose numbers will be fitted, and treat any CSV without that column as
> being of unknown precision.

### Hardware FPU

The PIC32MZ2048**EF**M144 has a hardware 64-bit double-precision FPU (Coprocessor 1). All floating-point arithmetic (`double` multiply, divide, add, convert) executes in hardware — no software emulation.

- **Compiler**: targeting `PIC32MZ2048EFM144` implicitly sets `__mips_hard_float = 1`
- **FreeRTOS**: `configUSE_TASK_FPU_SUPPORT = 1` in `FreeRTOSConfig.h`; saves/restores 32×64-bit FPU registers on context switches for tasks that call `portTASK_USES_FLOATING_POINT()`
- **Registered / pure-integer task lists**: see the FPU bullet in "Atomicity & Concurrency Rules" above.
- **ADC voltage conversion**: `ADC_ConvertToVoltage()` uses native `mul.d`, `div.d`, `add.d` instructions

### FreeRTOS Configuration

- **Tick rate**: 1000 Hz (1ms tick)
- **Preemptive** with time-slicing (equal-priority tasks round-robin)
- **Max priorities**: 10 (0=lowest, 9=highest)
- **Heap**: heap_4, 74KB (`configTOTAL_HEAP_SIZE` — 75000 cut to 74000 in #667)
- **ISR stack**: 8192 bytes
- **Kernel interrupt priority**: 1 (lowest)
- **Max syscall interrupt priority**: 4 (ISRs at priority 5+ are never disabled by FreeRTOS)

### Task Priority Map

| Priority | Task | Stack (words) | Peak Used | Notes |
|----------|------|--------------|-----------|-------|
| 9 | `_Streaming_Deferred_Interrupt_Task` | 512 | 214 | ISR deferral, sample collection (no FPU — uses Q16 LUT for sine pattern) |
| 9 | `MC12bADC_EosInterruptTask` | 256 | 80 | ADC end-of-scan deferred interrupt (stack raised 160→256 in #525 vsnprintf-margin follow-up) |
| 9 | `AD7609_DeferredInterruptTask` | 1024 | 76 | AD7609 BSY pin handler (stack raised 160→1024 for the BSY-stuck LOG_E/vsnprintf frame, #525 follow-up; NQ3-only) |
| 7 | `app_PowerAndUITask` | 640 | 226 | UI + BQ24297 power, FPU |
| 7 | `app_USBDeviceTask` | 3072 | 1290 | Hosts the USB SCPI handler chain, FPU; SCPI callbacks use shared response buffer (see below). **Created at pri 2, self-boosts to 7** via `vTaskPrioritySet(NULL, 7)` right after `UsbCdc_Initialize()` (`app_freertos.c`) — static xTaskCreate reads 2, runtime is 7, so USB SCPI stays above the encoder (6) during streaming. |
| 6 | `streaming_Task` | 1392 | 692 | Encodes PB/CSV/JSON + outputs, FPU (CSV/JSON at precision>0) |
| 6 | `F_USB_DEVICE_Tasks` | 144 | 72 | USB device stack. **Created at pri 2, self-boosts to 6** via `vTaskPrioritySet(NULL, 6)` on first iteration (`tasks.c` `F_USB_DEVICE_Tasks`). Static xTaskCreate reads pri 2 — runtime is 6. |
| 5 | `app_SDCardTask` | 1024 | 468 | SD mount/write/read/list/delete |
| 5 | `Iperf2TaskMain` | 512 (static) | n/p | Dedicated iperf2 pacing task (#377), `xTaskCreateStatic`. Adaptive 2 ms (active) / 50 ms (idle) cadence. No FPU. |
| 2 | `app_WifiTask` | 1500 | 780 | WiFi state machine + TCP + SCPI-over-TCP dispatch (post-#353 Opt 2: microrl + libscpi + handlers all run here instead of on WDRV_WINC_Tasks) |
| 2 | `F_DRV_USBHS_Tasks` | 144 | 72 | USB hardware driver |
| 2 | `fwUpdateTask` | 1024 | 62 | WiFi FW update (dynamic — created only during a WiFi FW-update session) |
| 1 | `lWDRV_WINC_Tasks` | 1024 | 320 | WINC1500 driver. PR #492 (#489 variant B): dropped 2→1 so `streaming_Task` (pri 6) preempts WINC by 5 levels and OSAL semaphores inside `WDRV_WINC_Tasks` do the yielding (Microchip reference pattern). Eliminated #491 BIMODAL catastrophic WiFi drops. |
| 1 | `lAPP_FREERTOS_Tasks` | 1500 | 1156 | Boot init (77% used) |
| 1 | `LogIsrDrainTask` | 256 | n/p | Deferred ISR-log drain (`"logISR"`, `LOG_ISR_TASK_PRIO`). Drains the ISR log queue; memcpy-only, no FPU. |

Stack sizes profiled under stress: 16ch@5kHz PB/CSV/JSON + SD file ops + WiFi TCP + power cycles. Sized at 2-3x measured peak. Query at runtime: `SYST:MEM:STACk?`

**Note**: SD directory listing uses iterative traversal with a bounded BSS-backed stack (`SD_CARD_MANAGER_MAX_LIST_DEPTH = 16`, ~4.3 KB total). Task stack usage is O(1) regardless of FAT32 tree depth. Trees deeper than 16 levels emit a diagnostic and skip the subtree.

**SCPI shared response buffer**: the constraint is not a flat byte count — it is that a *response-sized* buffer (the formatted reply text a callback is about to send back) does not belong on the stack; that is precisely what the shared buffer exists for. Any callback assembling such a reply MUST use `SCPI_ResponseBuf_Take()` / `SCPI_ResponseBuf_Give()` instead of a stack local. The shared buffer is a single 2048-byte static in BSS guarded by a statically-allocated mutex (`configSUPPORT_STATIC_ALLOCATION=1`). Rationale: the TCP → microrl → libscpi path already consumes ~800 words of `app_WifiTask` stack before the callback runs (`app_WifiTask` is 1500 words / 6000 B; measured peak was 780 words as of 2026-04 — re-verify via `SYST:MEM:STACk?` before relying on the exact figure, since nothing here depends on it staying at 780) — issue #347 hit exactly this when `app_WifiTask` was 1024 words.

An *ordinary* (non-reply) stack local is judged against that measured headroom, not against a fixed byte threshold — a prior revision of this rule stated a flat 256 B ceiling, which eight shipping call sites already exceed 3×. Concrete precedent, so a reviewer does not have to re-derive it: `DaqifiSettings` is used as a stack local — not through the shared buffer — at eight registered SCPI callback sites (`SCPILAN.c:671`, `:1424`, `:1443`, `:1479`; `SCPIADC.c:1402`, `:1477`; `SCPIInterface.c:6168`, `:6183`). It is **~796 B**: `AInCalArray` alone is `MAX_AIN_RUNTIME_CHANNEL`(48) × `sizeof(AInCalParam)`(16 B, `{double CalM; double CalB;}`) = 768 B, plus the union's type tag and an MD5-sized checksum field (`services/daqifi_settings.h`). That is accepted and correct, because it is a *config value* being read/written, not the reply text the shared buffer is for — treat a new stack local of similar shape and size the same way; a magic-number check against it is not the test. Current `SCPI_ResponseBuf` users (formatted replies, not working values): `SCPI_SysInfoGet`, `SCPI_SysInfoTextGet`, `SCPI_GetCommandHistory`, `SCPI_Help`, `SCPI_StorageSDBenchmark`.

**Scheduling implications**: Capture tasks at priority 9 preempt everything to guarantee deterministic sample timing. The encoder at priority 6 preempts WiFi/WINC/background (priority 2) and SD (priority 5), but stays below USB (7) so SCPI commands remain responsive during streaming. SD task at priority 5 sits above background transports but below encoder — prevents encoder from starving SD writes when USB+SD both active. Encoder's `Streaming_WriteWithRetry` uses `vTaskDelay(1)` (not `taskYIELD()`) in its retry loop so lower-priority SD actually gets CPU to drain circular buffer (#312). See `docs/PIPELINE_TIMING.md` for measurements (PR #308, Sessions 7-17).

### Known Silicon Errata (DS80000663 Rev R, silicon revs A1/A3/B2/B3; verified against PDF pages 6-15)

All items verified against the actual errata document. Only issues affecting features we use are listed, with exact workaround text from Microchip.

| Issue | Module | Rev | Summary | Our Status |
|-------|--------|-----|---------|------------|
| #1 | Oscillator | All | **REFCLK cannot divide inputs >100 MHz.** | **Safe.** REFCLK1 is not configured/used in the current tree at all (SPI4 rides PBCLK2, MCLKSEL=0 — see Clock Tree above); the erratum cannot apply. (Historical: PR #219's RODIV=0 passthrough followed the documented workaround while REFCLK1 was in use.) |
| #5 | Power-Saving | A1/A3 | Turning off REFCLK via PMD bits causes unpredictable behavior. | **Safe.** We never disable REFCLK via PMD. Not affected on B2/B3. |
| #6 | I2C | A1/A3 | Indeterminate I2C at >100 kHz and/or >500 bytes continuous. False collision detect, receive overflow, suspended transactions. All recoverable in software. | **Monitor.** I2C5 for BQ24297 at 100 kHz with mutex. Not affected on B2/B3 but good to know. |
| #8 | UART | All | **RX FIFO overflow → shift registers stop → UART loses sync.** Only recovery: toggle UART OFF/ON multiple times. | **Low risk.** Debug UART4 only. Workaround: ensure UART interrupt priority prevents RX overrun, or set URXISEL for earlier interrupt. |
| #9 | USB | All | USB won't function if USB PHY off in Sleep (USBSSEN=1). | **Safe.** Keep USBSSEN=0. |
| #16 | USB | All | No remote wake-up support (USBRIE in USBCRCON). | **N/A.** Inform host via USB descriptors. |
| #25/#26 | Crypto | All | Crypto DMA: no partial packets, no zero-length hash. | **N/A.** wolfSSL runs in software mode. |
| #27 | SPI | All | **SRMT bit falsely indicates TX complete** before last block shifts out. Does NOT affect Transmit Buffer Empty Interrupt (STXISEL=0). | **Safe.** Harmony SPI driver uses interrupts, not SRMT polling. |
| #37 | I2C | All | **SCL tLOW doesn't meet I2C spec at ≥400 kHz.** No workaround. | **Safe.** BQ24297 I2C at 100 kHz. Never use ≥400 kHz on this chip. |
| #38 | System Bus | B2/B3 | **Flash wait states at SYSCLK >184 MHz with ECC need 3 wait states** (not 2). <2% CPU impact with cache. | **Safe.** At 252 MHz (#487) we run `PRECONbits.PFMWS = 5` (set conservatively for bring-up; tune-down to the DS60001320H table value is a tracked follow-up) and `ECCCON = 3` in `initialization.c`. |
| #39 | ADC | All | Excessive current through VREF- when external reference used and VREF- > AVss. | **Monitor.** Workaround: connect VREF- to AVss. Check NQ3 board schematic. |
| #40 | USB | All | FLUSH bit (USBIENCSRx<19>) doesn't flush TX FIFO properly. | **Safe.** Harmony USB driver: set FLUSH + clear TXPKTRDY simultaneously, repeat twice. |
| #42 | DMA | All | **DMA half-full interrupt can fire twice** when cleared at n/2 byte, re-triggers at (n/2)+1. | **Safe.** Workaround: clear CHDHIF along with CHBCIF. Harmony DMA driver handles this. |
| #44 | Timer2-9 | All | Timer match coinciding with sleep entry → interrupt may not fire. No workaround. | **Safe.** We never enter sleep during streaming. |
| #45 | Flash RTSP | All | Run-Time Self Programming of Configuration Words broken. No workaround. | **Safe.** NVM settings use regular flash pages, not config words. |
