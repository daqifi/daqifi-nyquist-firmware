# Streaming & ADC architecture

> Split out of `CLAUDE.md` on 2026-09-10. That file is loaded into **every turn of
> every agent**, and at ~38k tokens it was ~13% of all token spend; this material is
> reference — needed when you work in this area, not on every task. **It is
> unchanged, not summarised.** Read it in full before changing anything it
> describes, and update it here rather than re-adding it to `CLAUDE.md`.

The acquisition and streaming pipeline: ADC read paths and ISR design, voltage precision, streaming statistics and loss accounting, OPER/QUES status registers, frequency capping, test patterns, and throughput benchmarking.

---

### Data Flow

1. **Acquisition Path**:
   - Hardware interrupts → HAL drivers → Sample buffers
   - ADC modules use DMA for high-speed acquisition
   - DIO uses interrupt-driven capture

2. **Streaming Path**:
   - Sample buffers → Encoder (JSON/CSV/ProtoBuf) → Output interface (USB/WiFi/SD)
   - Streaming engine manages buffer flow and encoding
   - Supports multiple simultaneous outputs

### ADC Architecture & ISR Design

The PIC32MZ ADCHS peripheral has two classes of ADC channels with different read strategies. (This section reflects the post-#541 architecture; #292's "T1 reads in the EOS task" topology and the older "CH3 batch ISR" topology described in earlier revisions are both gone. Full hardware-semantics record with FRM/datasheet citations: `docs/ADC_HW_SEMANTICS.md`.)

**Type 1 — Dedicated modules (simultaneous conversion):**
- NQ1 channels: ch4 (MODULE4), ch8 (MODULE0), ch10 (MODULE1), ch12 (MODULE2), ch14 (MODULE3)
- Each channel has its own SAR ADC module — all convert simultaneously on the streaming-timer hardware trigger (TMR5 match, #282); conversion completes ~1.3 µs later
- **Streaming reads (#541 D-A)**: the deferred streaming task reads `ADCDATAx` directly, gated on the per-input `ARDY` flag (sets at conversion end, clears on read — fresh-per-tick by construction, no interrupt dependency). A not-ready miss leaves that channel's validMask bit 0 for the tick and bumps the `T1ArdyMisses` stat (expected ~0; <0.1% under pool saturation)
- **Idle reads**: `MC12bADC_EosInterruptTask` still reads T1 into `BOARDDATA_AIN_LATEST` between sessions so `MEAS:VOLT:DC?` works; while streaming the deferred task refreshes LATEST itself
- The per-channel `ADC_DATA0-4` ISR handlers are stubs (#292 removed the batch ISR; ~5 µs entry/exit per tick was the T1 bottleneck)

**Type 2 — Shared MODULE7 (sequential mux scan):**
- NQ1 channels: ch0, ch1, ch2, ch3, ch5, ch6, ch7, ch9, ch11, ch13, ch15 (+8 internal monitoring channels; the dead temp sensor AN44 is never scanned — erratum 18)
- **Dynamic scan list (#541 D-B)**: `ADCCSS1/2` is rebuilt at every stream start = enabled T2 user channels ∪ (monitoring channels if OBDiag=1), applied via the FRM-documented `TRGSUSP`→`UPDRDY` online-update sequence; the idle list (all public T2 + monitoring) is restored at stream stop. Mid-stream `CONF:ADC:CHANnel` / `CONF:ADC:OBDiag` / `CONF:ADC:SAMC` are **rejected** so the session list can't go stale (#116)
- **Results**: per-channel data-ready ISRs (priority 1) read T2 user values into LATEST; the end-of-scan (EOS) interrupt fires once per completed full scan and wakes `MC12bADC_EosInterruptTask` (priority 9), which during streaming reads **monitoring channels only**
- **Scan-rate bound (#541 D-C)**: three distinct hardware limits gate the scan, all folded into the cap. (1) **Scan-busy bound**: retriggering the scan while in progress is documented-undefined (FRM §22.3.2) — this was #539, where EOS stopped firing above ~4.6 kHz and scanned data froze; the cap bounds the tick period to `1.1 × (N_active × (SAMC+2+14) × TAD7 + 6 µs)` (n=19 silicon anchor: timer→EOS 216 µs + 4,500 Hz clean; TAD7 = 100 ns, all terms read live). (2) **EOS-rate ceiling** (`ADC_EOS_RATE_MAX_HZ` = 10,400): independent of scan length, driving the end-of-scan interrupt/task machinery sustained above ~11.5–12 kHz **killed the USB peripheral** (enumerated-but-CDC-dead; hardware reset required) **on pre-#525 firmware**. ⚠️ **Re-evaluated 2026-06-19 (#557): this was NOT a silicon limit — it was the #525 EOS-task `vsnprintf` stack overflow** (high EOS rate → overruns `notifCount>1` → `vsnprintf` on the 160-word EOS stack → adjacent-TCB corruption → scheduler TLBL → CDC-dead wedge). #525 (v3.6.1) removed that `vsnprintf`; re-tested **GONE** on v3.6.1 — 1×T2 clean to 15 kHz, `EosOverruns` accrues harmlessly (n=3 + 120 s soak, fresh reflash). Bench anchors (pre-#525): an n=1 scan — in-spec for retrigger, T_busy 17 µs ≪ 83 µs period — wedged at 12,000 Hz on the plain *admitted* path, clean 10,000 × 60 s; n=7 clean at 11,500, wedge at 11,750; 10,400 soak-proven 120 s+. Pre-#541 firmware could never hit this: the static 19-input scan put EOS to sleep (#539) above 4.6 kHz, keeping its rate out of the fatal zone — dynamic CSS exposed it. (3) **Aggregate ADC-event-rate ceiling** (`ADC_EVENT_RATE_MAX_PER_S` = 60,000): each enabled T2 *user* channel fires a per-conversion data-ready ISR on top of the per-scan EOS; the combined event rate `f × (nUserT2 + 1)` is USB-fatal around ~66–72k events/s (11×T2 OBDiag=0 wedged at 6,000 Hz *admitted* = 72k/s, clean 5,500 × 60 s; 60k is 120 s-proven). Monitoring channels have no data-ready ISRs and don't count. **Both "fatal" limits were the same #525 bug (#545 superseded by #557)**; v3.6.1 survives far past them (11×T2 clean to 96k events/s, n=3 + 120 s soak). **Resolution (#563/#557, v3.6.2): for NQ1 the EOS-rate (10,400) and event-rate (60,000) caps are REPLACED by the fitted freeze-aware additive model `Streaming_AdcAdditiveCap_NQ1` (see "Streaming Frequency Capping"), min()'d only with the scan-busy #539 term.** The old caps' cap-headroom review found their real problem was the opposite of "too conservative": the drop-blind sweeps they were fitted against counted frozen scanned data as clean throughput, so the T2/scanned ceilings were *inflated*. The additive model + the new `ScanStaleDropped` freeze detector correct this — several T2 caps moved **down**, not up. The EOS/event constants survive only in `MC12b_ScanMaxFreq` for NQ2/NQ3 (no MODULE7 scan) pending a per-variant review. Current NQ1 enforced caps (v3.6.2, USB, live `CONF:CAP:JSON?` 2026-07-01): 1×T2 OBDiag=0 → 9,913 PB / 7,442 CSV (was "10,400 EOS-bound"); 11×T2 OBDiag=0 → 3,937 PB / 2,833 CSV (was "5,000 event-bound" — the *drop* was the freeze correction); 16ch OBDiag=1 → 3,518 PB / 2,000 CSV; 1×T1 OBDiag=0 → **15,798 PB** (no scan → additive `armed=0`). ⚠️ **The T2/16ch figures above are the v3.6.2 values and predate the 252 MHz refits** (#595/#600 raised PB transport + additive, #712 raised USB CSV transport, #715/#714 *lowered* pure-T1 PB); re-query `CONF:CAP:JSON?` before relying on them. The 1×T1 figure is corrected here because it is computable from source without a device — no scan is armed, so the cap is `min(additive, transport, ISR_MAX)` = `min(880e6/(52700+3000·1), 22000, 22000)` = **15,798**. It read 15,000 because that was the pre-#595 USB PB transport single value, which #595 raised to 22,000. (The #715 note further down says 15799; the firmware truncates rather than rounds — `880e6/55700` = 15798.9 → 15798 — so the two disagreed by one as well as by 798.) OBDiag=1 visibly lowers `CONF:CAP` — honest physics, monitoring rides the same scan. T1-only OBDiag=0 arms no scan (additive `armed` term = 0, no scan-busy bound), so its cap is `min(additive, transport, ISR_MAX)` — and since #595 raised USB PB transport to 22,000 the **additive term is the binder** there (15,798 < 22,000), not transport or ISR_MAX. This clause used to read "transport/ISR-bound only", which was true while the cap was 15,000: that WAS the transport single value. Raising transport above the additive term silently moved the binder, which is the same root cause as the stale 15,000 figure itself

**Flow per streaming timer tick:**
1. TMR5 match hardware-triggers Type 1 conversions + the MODULE7 scan (when armed); the timer ISR (`Streaming_Defer_Interrupt`) notifies the deferred task
2. Deferred task builds the sample: T1 via ARDY-gated direct `ADCDATAx` reads; T2 from `BOARDDATA_AIN_LATEST` (written by the T2 data-ready ISRs)
3. EOS fires at full-scan completion → `MC12bADC_EosInterruptTask` reads monitoring channels (OBDiag=1 sessions and idle)
4. Software-trigger path (`MC12b_TriggerConversion`) remains for idle polling and non-HW-trigger modes

**What a sample's timestamp means (#729):** the stamp is the acquisition **trigger** instant (`baseTS + N × periodTicks` since #722), not the conversion instant. `baseTS` is a *software* read of TMR6 at TIMER_5 ISR entry, not a hardware capture at the compare match, so every stamp carries one fixed, unmeasured µs-scale session-wide offset — inter-tick spacing is exact, absolute anchoring is not. T1 channels on the ARDY-direct path are same-tick *in steady state* (see the catch-up caveat below, which applies to them too). **Cached-path channels can carry an older conversion**, for two different reasons: NQ1 Type 2 because the shared MODULE7 scan armed at tick N completes *after* the tick-N deferred task has already read `BOARDDATA_AIN_LATEST`; NQ3 AD7609 because it has no MODULE7 scan at all — `ADC_HandleAD7609Interrupt` (`firmware/src/HAL/ADC.c:64`) publishes into LATEST (`ADC.c:89`) when its BSY deferred task runs, independently of the streaming tick. In **steady state** that skew is fixed and uniform; under **deferred-task catch-up** it is not — the stamp is counter-derived while the value is read live from the one-deep slot, so a backlogged iteration can emit a value *newer* than its own stamp (#722 fixed the stamp side only). It is still the only self-consistent choice given one `Timestamp` per packet, and it matches the `ScanStaleDropped` freshness model (#557/#563: data one scan behind is *defined* as valid current-tick data). Δt between samples is exact regardless; only absolute phase alignment against an external event needs to account for it. The per-config offset is not currently reported and has not been measured — see `docs/ADC_HW_SEMANTICS.md` § "Timestamp semantics".

**Key files:**
- `services/streaming.c` — deferred task T1 direct read, session CSS rebuild, scan-bound cap term
- `HAL/ADC/MC12bADC.c` — `MC12b_ComputeScanList` / `MC12b_ApplyScanList` / `MC12b_ScanMaxFreq` / `MC12b_DrainType1Results`, `MC12b_TriggerConversion`, hardware-trigger config
- `HAL/ADC.c` — `MC12bADC_EosInterruptTask` (monitoring + idle T1 reads)
- `config/default/interrupts.c` — T2 per-channel data-ready handlers, `ADC_EOS_Handler`

**Characterization results (O3, fullscale test pattern, NoCap benchmark mode):**

Current table = **Session 24 (2026-05-28 overnight + 2× targeted retry, 400 s endurance).** Methodology change vs Session 22: where Session 22 used a fresh 10 s ceiling sweep + 60 s endurance soak, Session 24 uses 400 s endurance soaks with iterative haircut from prior-night ceilings (12.5 % per pass, repeated until zero drops). Result: **substantially more conservative** numbers than Session 22 in many cells — these are *verified-safe steady-state rates* the device sustains for 400 s+ without losing a single byte. Fresh 10 s sweeps will still find higher rates that hold short-term; treat Session 22 as "burst ceiling" and Session 24 as "soak ceiling." Source CSVs: `daqifi-python-test-suite/benchmarks/overnight_20260528_0642.csv` + `_0827_boardE8A7.csv` + `_1604_retry3.csv`. Test-suite SHA: `22302ba` on `feat/full-stats-capture`.

> **This is descriptive endurance characterization, not the firmware cap.** These soak ceilings are an empirical record of what the device sustains across many configs (incl. OBDiag variants, OBDiag here = on-board diagnostics monitoring); the rate the firmware actually *enforces* is fitted separately — see **"Streaming Frequency Capping"** below, whose **"Fit basis (normative — the zero-loss sweep subset…)"** table is the canonical 1/5/10/16-ch subset the `Streaming_TransportMaxFreq` coefficients derive from. The two use different methods (soak-with-haircut vs zero-loss sweep escalator) and so report different numbers by design. Authoritative dataset for both: `daqifi-python-test-suite/benchmarks/`.

> **⚠️ The descriptive Session-24 USB and SD throughput tables that follow pre-date #487 — measured at 200 MHz/100 MHz — and so does the separate "Fit basis" table under "Streaming Frequency Capping" further down this file.** They are historical characterization, not the enforced caps. **The ENFORCED caps HAVE been re-fit for 252 MHz** (contrary to older revisions of this note): PB transport + additive were raised (**#595/#600** — USB PB single 15000→22000 curve 120000/(1+n), SD PB single 9000→13000 curve 99000/(4+n), ISR_MAX 16000→22000); USB CSV transport was raised (**#712**); and the pure-T1 PB additive was **lowered** (**#715/#714** — the 252 MHz PB refit had over-capped pure-T1 PB, silently dropping data at cap: USB PB 1×T1 19340→15799, SD PB 1×T1 9852→7900). **Still on the 200 MHz-era fit (real remaining headroom):** the CSV *additive* grid for **nT1 >= 2** (its **single-channel** case was re-fitted by **#832**, 10589 -> 15263), JSON (`CSV×0.5` placeholder except USB/NQ1, **#529**), the WiFi PB curve, and all NQ2/NQ3 caps (legacy 200 MHz envelope by design). The authoritative, current cap dataset is `daqifi-python-test-suite/benchmarks/` (e.g. `atcap_20260723_*.csv`), not any of the in-repo tables this note covers; the enforced values live in `firmware/src/services/streaming.h` (`Streaming_AdcAdditiveCap_NQ1` / `Streaming_SdAdditiveCap_NQ1` / `Streaming_TransportMaxFreq`). Cross-check those + the `#595/#600/#712/#715` PRs before running any 252 MHz cap work.

**USB** (400 s endurance soak, KB/s from `pc_kbps`):

| Config | PB Hz | PB KB/s | CSV Hz | CSV KB/s |
|--------|----------:|--------:|----------:|--------:|
| 1×T1 OBDiag=OFF        | 16,500 | 182 | 17,000 | 256  |
| 1×T1 OBDiag=ON         | 14,500 | 159 | 14,000 | 222  |
| 1×T2                   | 14,500 | 159 | 16,000 | 250  |
| 3×T1                   | 15,000 | 224 | 13,000 | 612  |
| 3×T2                   | 15,000 | 224 | 13,000 | 613  |
| 5×T1 OBDiag=OFF        | 16,000 | 303 | 13,000 | 968  |
| 5×T1 OBDiag=ON         | 14,000 | 265 | 11,000 | 869  |
| 5×T2                   | 14,000 | 266 | 11,000 | 868  |
| 8×T2                   | 12,000 | 300 |  9,000 | 1,140 |
| 11×T2                  | 11,000 | 341 |  8,000 | 1,386 |
| 5T1+3T2 (8ch)          | 12,000 | 299 |  9,000 | 1,139 |
| 5T1+5T2 (10ch)         | 11,000 | 315 |  8,000 | 1,266 |
| 5T1+11T2 OBDiag=ON (16ch)  |  9,000 | 368 |  6,000 | 1,518 |
| 5T1+11T2 OBDiag=OFF (16ch) | 11,000 | 449 |  7,000 | 1,798 |

Best wire rate observed: **USB CSV 5T1+11T2 OBD=OFF @ 7 kHz → 1,798 KB/s**.

**SD** (400 s endurance soak, interface=2, post-format clean card):

| Config | PB Hz | CSV Hz |
|--------|----------:|----------:|
| 1×T1 OBDiag=OFF        | 10,000 |  9,000 |
| 1×T1 OBDiag=ON         |  9,000 |  8,000 |
| 1×T2                   |  9,000 |  8,000 |
| 3×T1                   |  8,000 |  5,000 |
| 3×T2                   |  8,000 |  5,000 |
| 5×T1 OBDiag=OFF        |  8,000 |  4,000 |
| 5×T1 OBDiag=ON         |  7,000 |  3,000 |
| 5×T2                   |  7,000 |  3,500 |
| 8×T2                   |  6,000 |  2,500 |
| 11×T2                  |  6,000 |  1,500 |
| 5T1+3T2 (8ch)          |  6,000 |  2,000 |
| 5T1+5T2 (10ch)         |  6,000 |  1,500 |
| 5T1+11T2 OBDiag=ON (16ch)  |  5,000 |  1,000 |
| 5T1+11T2 OBDiag=OFF (16ch) |  5,000 |  1,500 |

**Why Session 24's USB cells read lower than Session 22's:** methodology, not regression — 400 s endurance + iterative haircut converges several kHz below a 10 s burst ceiling. Earlier session-by-session deltas (Sessions 18–23: jitter elimination via the #335 idle-gate, T1/T2 parity #313, SD PB recovery #312/#314, per-PR throughput-safety checks for #354/#356) are recorded in `daqifi-python-test-suite` `benchmarks/CHANGELOG.md` and the project memory files — consult those for history rather than this document.

> **⚠️ Caveat post-#541 (v3.6.0):** the T1 rows in these tables (and any pre-#541 measurement) were captured while T1 *values* froze above ~4.6 kHz — throughput numbers stand, but the streams carried stale data. The enforced caps were re-validated at-cap on the new read path (2026-06-12: USB 28/28, WiFi 18/18 cells, 120 s soaks, zero loss).

### Voltage Output Precision

Systemwide configurable voltage precision via `StreamingRuntimeConfig.VoltagePrecision`. Applies to CSV streaming, JSON streaming, and SCPI voltage queries (`MEAS:VOLT:DC?`, `SOUR:VOLT:LEV?`).

**SCPI Commands:**
```bash
CONFigure:VOLTage:PRECision <0-10>   # Set precision
CONFigure:VOLTage:PRECision?         # Query current precision
CONFigure:VOLTage:SAVE               # Persist to NVM (survives reboot)
CONFigure:VOLTage:LOAD               # Load from NVM
```

| Value | Output | Example |
|-------|--------|---------|
| 0 | Integer millivolts | `1221` (fast path, uses `int_to_str`) |
| 4 | Volts, 4 decimal places | `1.2207` (NQ1 default, 12-bit ADC) |
| 6 | Volts, 6 decimal places | `1.220703` (NQ3 default, 18-bit AD7609) |
| 7 | Volts, 7 decimal places | `1.2207031` (NQ2 default, 24-bit AD7173) |

**Board-specific defaults** (`tBoardConfig.DefaultVoltagePrecision`): NQ1=4, NQ3=6, NQ2=7 — but see the warning below: these are what the board config *declares*, not what a fresh device actually comes up with.

**NVM persistence**: Stored in `TopLevelSettings.voltagePrecision`. Saved via `CONF:DATA:SAVE`, loaded at boot from NVM.

⚠️ **A fresh device boots at precision 0, NOT the board default** (#910). This
file previously said "falls back to board config default on first boot"; that is
wrong. On the first-boot path `daqifi_settings_SaveToNvm` runs at
`app_freertos.c:593`, five lines *before* `InitBoardRuntimeConfig` at 598 — and
it captures precision from the not-yet-initialised runtime config, overwriting
the 4 that `LoadFactoryDeafult` just set with 0, in NVM and in the in-memory
`tmpTopLevelSettings`. The guard at line 628 is `if (savedPrec <= 10)`, and 0
satisfies it, so the already-corrupted 0 is what gets written into the runtime
config a few lines later — nothing along this path can tell "deliberately
integer millivolts" from "never initialised". Verified on hardware
2026-08-30/31: NQ1 reads `CONF:VOLT:PREC?` = 0 on v3.7.2 and v3.7.3, after a
PICkit flash and after an in-app bootloader update.

Practical consequences: **0 is the de facto shipped default** for every text
consumer of it — CSV/JSON streaming and the `MEAS:VOLT:DC?` / `SOUR:VOLT:LEV?`
replies — since ProtoBuf ships raw ADC codes and is agnostic to this setting;
daqifi-core and the desktop app have always seen 0 through those text paths, so
it is not safe to "fix" unilaterally (#910 explains the coordination needed).
And **any cap or ceiling measurement must pin
precision explicitly** — 0 takes the `int_to_str` fast path while 4 formats a
float per channel per sample, a first-order encoder cost, so an unpinned board
silently measures the cheap path (`--precision` in
`test_overnight_characterization.py`; see #832 / test-suite #233).

**Implementation:** `firmware/src/services/csv_encoder.c`, `firmware/src/services/JSON_Encoder.c`, `firmware/src/services/SCPI/SCPIInterface.h` (SCPI_ResultVoltage helper), `firmware/src/services/SCPI/SCPIADC.c`, `firmware/src/services/SCPI/SCPIDAC.c`

### Streaming Statistics & Buffer Overrun Tracking

The streaming engine tracks data loss at every stage of the pipeline. Statistics are accumulated per streaming session (cleared on `StartStreamData`, preserved after `StopStreamData`).

**SCPI Commands:**
```bash
SYSTem:STReam:STATS?       # Query all statistics
SYSTem:STReam:STATS:CLEar  # Reset all counters
```

**Response fields from `SYSTem:STReam:STATS?`:**
| Field | Type | Description |
|-------|------|-------------|
| `TotalSamplesStreamed` | uint64 | Samples successfully queued from ISR |
| `TotalBytesStreamed` | uint64 | Total bytes encoded (offered to outputs) |
| `QueueDroppedSamples` | uint32 | Samples lost due to pool exhaustion or full sample queue (pool defaults 1100, re-partitioned per session) |
| `UsbDroppedBytes` | uint32 | Data lost due to USB circular buffer full (16KB default; auto-balance raises it to 64KB whenever USB is active — `hasUsb` covers **USB+SD** too, which is why the auto-balance table shows 65,536 in that column as well) |
| `WifiDroppedBytes` | uint32 | Data lost due to WiFi circular buffer full (14KB default; auto-balance raises it to 96KB when WiFi is the only active interface — `STREAMING_WIFI_WIFI_ONLY`, #497) |
| `SdDroppedBytes` | uint32 | Data the SD path could not take. **There is no fixed retry count** — an earlier revision of this row said "3 retries", which matches nothing on this path (the SD manager's only retry constants are `SD_MOUNT_MAX_RETRIES` 10 and `SD_UNMOUNT_MAX_RETRIES` 40, neither governing writes; other subsystems have their own — `SCPI_WRITE_MAX_RETRIES`, `SPI_RETRY_COUNT` — none of them 3). The two real paths differ: **multi-output** (USB+SD) counts a short write **immediately, with no retry at all** (`streaming.c` — `LOG_E_SESSION "SD buffer overflow (multi-output no-retry)"`), which is deliberate so a stalled SD cannot block USB (#534/#536); **SD-only** goes through `Streaming_WriteWithRetry`, which spins, then backs off on `vTaskDelay(1)` up to `STREAM_WRITE_TIMEOUT_MS` (10 s) before counting. It no longer carries a split-file rotation strand: #823 made that visible here and **#824 eliminated it** by writing each file's header at open instead of through the ring (see "SD Card File Splitting" in `docs/SD_SUBSYSTEM.md`). The **stop-time** drain (`UNMOUNT_DISK`) is also counted now: it was the surviving twin of the #838 fix — the *rotation* drain's failure exits already called `Streaming_ReportSdDiscard()` before clearing, but the stop-time drain's identically-shaped exits (`sd_card_manager.c` "Error flushing pending write before unmount" and "Error draining buffer before unmount") logged once and zeroed the length with no report. **#915 closed this gap**: both stop-time exits now report their in-flight chunk the same way, and — because a write failure or an exhausted bounded-retry loop can also leave bytes the drain never got to extract from the ring at all — the `UNMOUNT_DISK` case additionally snapshots and reports any circular-buffer remainder after both drain loops, gated on either loop having logged an error or exhausted its retry bound (a clean drain leaves the ring empty and reports nothing). This was the last silent SD loss path named in this row. |
| `EncoderFailures` | uint32 | Encoding attempts that returned 0 bytes with data available |
| `TimerISRCalls` | uint64 | Actual streaming timer ISR entry count this session (#265). Invariant: `TimerISRCalls == TotalSamplesStreamed + QueueDroppedSamples`. |
| `EosOverruns` | uint32 | EOS notifications coalesced (>1 per wake) (#295). Task-behind-but-fresh — NOT a loss (excluded from loss total). |
| `CatchUpSamples` | uint32 | #735: iterations that began with another tick already pending, i.e. the deferred task was catching up. Those iterations may emit a value belonging to a LATER tick than their stamp — the stamp is counter-derived (#722) but the value is read live from the one-deep `BOARDDATA_AIN_LATEST` slot, which the priority-1 ADC ISRs keep overwriting during a backlog drain. An upper bound (a backlog makes the skew possible, not certain). **NOT a loss** — the sample is streamed and its value is real — so it is excluded from the loss total, like `EosOverruns`. `ScanStaleDropped` does not cover this: that fires when a scan fails to *complete*, and during catch-up the scan completes normally. **Measured 0 at the enforced cap for 1×T1, 5×T1, 11×T2, 16ch CSV and 16ch PB (2026-08-21); it becomes non-zero only well past cap under NOCAP, and even there stays under 0.1% while `QueueDroppedSamples` dominates.** So at any rate the firmware accepts, the value↔stamp association is not skewed. |
| `ScanStaleDropped` | uint32 | #557/#563 (NQ1, v3.6.2+): ticks where the shared MODULE7 scan was armed but its EOS had not fired since the prior trigger → that tick streamed **frozen** (stale) data. Counted as a genuine dropped sample (its post-grace subset, `ScanStaleDroppedSteady` below, folds into the session-end summary's loss total, unlike `EosOverruns`), lifetime raw value unchanged by grace. Reads ~0 with the cap in place; the freeze-aware leak detector behind the NQ1 additive cap. |
| `ScanStaleDroppedSteady` | uint32 | #1018: post-grace subset of `ScanStaleDropped` above — increments beside the raw counter (in the same TIMER_5 ISR) only once the startup grace window (`SYSTem:STReam:LOSS:GRACe`, default 3 s) has expired. This, not the raw counter, is the term the session-end `LOG_E` summary's "(all post-grace)" total actually uses — before #1018 the raw counter leaked into that total, so a scan freeze entirely inside the grace window was mislabeled as post-grace loss. |
| `SampleLossPercent` | uint32 | `QueueDroppedSamples / (Total + Dropped) * 100` |
| `ByteLossPercent` | uint32 | `(USB + WiFi + SD dropped) / TotalBytesStreamed * 100` |
| `WindowLossPercent` | uint32 | Sliding-window sample loss % (0-100), updated every N samples |

**Distinguishing failure modes** with the ISR counter (#265):
- `TimerISRCalls < freq × duration` → timer is rate-limited (PIC32MZ ~90 kHz hardware ceiling)
- `TimerISRCalls == TotalSamples + QueueDropped` → every ISR is accounted for; nothing lost between ISR and deferred task (should always hold)
- `QueueDroppedSamples > 0` → sample pool exhausted (encoder/output too slow for the rate the timer is firing)
- `UsbDroppedBytes / SdDroppedBytes > 0` → encoder is fine but transport can't keep up

**Thread safety:** `TotalSamplesStreamed`, `TotalBytesStreamed`, and `TimerISRCalls` are 64-bit counters (safe for million-year sessions). The first two are protected by `taskENTER_CRITICAL`/`taskEXIT_CRITICAL` on each increment and during snapshot reads. Drop counters remain 32-bit (atomic on PIC32MZ). `TimerISRCalls` lives in a separate `static volatile uint64_t gTimerISRCalls` global, incremented in true ISR context (TIMER_5 — the 32-bit TMR4/5 streaming-timer pair's vector, priority 3, ≤ max-syscall 4) by a single writer (no critical section needed because same-source can't preempt itself); the snapshot read uses `taskENTER_CRITICAL` which raises the syscall priority above the kernel-managed ISR threshold and blocks the timer, making the non-atomic 64-bit read coherent.

**Session-end logging:** When streaming stops, if any data was lost during the session, a `LOG_E` summary is automatically written with sample counts, per-buffer byte drops, and loss percentage. Retrieve via `SYST:LOG?`. #982: the summary is computed and printed by `Streaming_EmitSessionSummary()`, not inline in `Streaming_Stop()` — it is deferred (via a pending flag `Streaming_Stop()` sets) until after the stop-time SD teardown (`UNMOUNT_DISK`) has had a chance to report its own loss, so the summary covers stop-time SD drain loss instead of printing one step too early to see it. Called from `SCPI_PerformStreamingStop()` (after its bounded SD-idle wait) and, as a backstop for paths that bypass that function (chiefly the #397 auto-stop), from `Streaming_Start()`.

**Implementation:** `firmware/src/services/streaming.c` (StreamingStats struct), `firmware/src/services/SCPI/SCPIInterface.c` (SCPI callbacks)

### SCPI Status Registers (OPER & QUES)

The firmware populates the IEEE 488.2 `STATus:OPERation` and `STATus:QUEStionable` condition registers to reflect real-time device state during streaming.

**SCPI Commands:**
```bash
STATus:OPERation:CONDition?      # Current OPER condition bits (non-destructive)
STATus:OPERation?                # OPER event register (latched, clears on read)
STATus:OPERation:ENABle <mask>   # Set OPER event enable mask
STATus:OPERation:ENABle?         # Query OPER event enable mask

STATus:QUEStionable:CONDition?   # Current QUES condition bits (non-destructive)
STATus:QUEStionable?             # QUES event register (latched, clears on read)
STATus:QUEStionable:ENABle <mask>  # Set QUES event enable mask
STATus:QUEStionable:ENABle?      # Query QUES event enable mask

STATus:PRESet                    # Reset enable masks to default
```

**OPERation Condition Register Bits:**
| Bit | Value | Name | Description |
|-----|-------|------|-------------|
| 4   | 16    | Measuring | Streaming/measuring is active |
| 10  | 1024  | SD Logging | SD card logging is active |

Set on `StartStreamData`, cleared on `StopStreamData`.

**QUEStionable Condition Register Bits:**
| Bit | Value | Name | Description |
|-----|-------|------|-------------|
| 4   | 16    | Data Loss | Windowed sample loss >= 5% |
| 8   | 256   | USB Overflow | USB circular buffer overflow detected |
| 9   | 512   | WiFi Overflow | WiFi circular buffer overflow detected |
| 10  | 1024  | SD Overflow | SD write failure detected |
| 11  | 2048  | Encoder Fail | Encoder returned 0 bytes with data available |
| 12  | 4096  | Transport Down | All configured transports unhealthy >grace; streaming auto-stopped (#397). Cleared on next start. Grace tunable via `SYST:STR:CONSumer:GRACe <sec>` (5..300, default 60). LOG_E line `Streaming: all configured transports down >Ns — auto-stop` captures the stop reason. |

QUES bits are set in real-time during streaming and cleared when streaming stops. The QUES CONDition register reflects the *current* health; the QUES EVENt register latches transitions (clears on read).

**Windowed Data Flow Tracking:**
The `Data Loss` QUES bit (bit 4) uses a sliding double-buffer window to evaluate pipeline health independently of sample rate. Window size defaults to `clamp(frequency * 2, 20, 10000)` sample periods (~2 seconds of history). If the configured threshold percentage of samples in the window were dropped, the bit is set. The current window loss percentage is also reported as `WindowLossPercent` in `SYST:STR:STATS?`.

**Flow Window Configuration:**
```bash
SYSTem:STReam:LOSS:THREshold <pct>   # Set loss threshold 1-100% (default 5)
SYSTem:STReam:LOSS:THREshold?        # Query current threshold
SYSTem:STReam:LOSS:WINDow <samples>  # Set window size (0=auto, 20-10000=explicit)
SYSTem:STReam:LOSS:WINDow?           # Query current override (0=auto)
```

- **Threshold** takes effect immediately (next flow window check). Lower values (1-2%) for precision measurement; higher values (10-20%) for best-effort monitoring or noisy transports.
- **Window size** takes effect at next streaming start. Larger windows smooth out bursty loss; smaller windows detect faults faster. Auto mode (`0`) scales with frequency.
- Both settings survive across streaming sessions but reset to defaults on device reboot.

**Implementation:** `firmware/src/services/streaming.c` (gQuesBits, flow window), `firmware/src/services/SCPI/SCPIInterface.c` (OPER/QUES helpers, sync)

### Streaming Frequency Capping

The firmware computes a maximum safe streaming frequency as a `min()` of an **ADC/scan** term and a **per-interface/per-format TRANSPORT** term. Since #524 the cap is a **HARD limit**: `SYST:STR:START <freq>` above the cap is **rejected** with SCPI `-222` plus a `LOG_E` detail line stating the achievable max — the rate is never silently changed. Clients pre-validate against `current_max_rate_hz` (`CONF:CAP:JSON?` — equals this cap) or handle the error. Mid-stream `CONF:ADC:CHANnel` / `OBDiag` / `SAMC` are likewise **rejected** (#116/#541). `SYST:STR:BENCHmark 1/2` bypasses the cap — bench use only.

**The ADC/scan term is board-variant-specific (#563/#557):**
- **NQ1** (v3.6.2+): a fitted **freeze-aware additive model** `Streaming_AdcAdditiveCap_NQ1(nT1, nT2user, nMon, isPB, isJson, voltagePrecision)` (streaming.h), min()'d **only** with the SAMC-dependent **scan-busy** bound `MC12b_HardwareScanMaxFreq()` (the real FRM-documented **#539** limit). This **replaced** the old EOS-rate (10,400 Hz) / event-rate (60,000 ev/s) / tick-budget / `MC12b_ScanMaxFreq` terms: those old sweeps were **drop-blind** (counted frozen scanned data as clean → inflated T2 ceilings). The additive model was fitted to the 2026-06-22 freeze-aware sweep (the first to count `ScanStaleDropped` as loss) with a safe never-over margin (PB ×0.88, CSV ×0.80) and revalidated at-cap (see `validate_557_additive_cap.py` + `benchmarks/557_additive_cap/`). The former "USB-fatal" rationale for the EOS/event caps was the **#525** EOS-task `vsnprintf` overflow, re-tested **gone** on v3.6.1 (#557) — never a silicon limit.
- **NQ2/NQ3** (AD7609 — no MODULE7 scan): the legacy `min(ISR_MAX 16000, 55000/type1Count, 110000/(6+totalEnabled), MC12b_ScanMaxFreq)` formula, which still carries the (conservative, placeholder) EOS/event terms pending a per-variant headroom review.

**Effective limit (NQ1):** `min(Streaming_AdcAdditiveCap_NQ1(nT1, nT2user, nMon, isPB, isJson, voltagePrecision), MC12b_HardwareScanMaxFreq(scanCount) [only when a scan is armed], TransportMax(interface, encoding, n))`

The last two additive arguments are **#832** and both exist to keep a raise
inside the conditions it was measured under. `isJson` excludes JSON from the
pure-T1 CSV refit — it shares that branch and is *additive-bound* at USB 1ch,
so a CSV-measured raise would silently lift JSON's cap; it keeps the #563 law
until it has a precision-4 basis of its own (#529 follow-up).
`voltagePrecision` gates the refit to `<= 4`: 0 is `int_to_str` and 1..4 emit
fewer or equal characters than the precision-4 basis, while 5..10 emit **more**
and were never measured, so they fall through to the #563 law.
`CONFigure:VOLTage:PRECision` and `CONFigure:VOLTage:LOAD` are both
rejected while streaming
(same idiom as the `CONF:ADC:CHANnel` #116 guard) so a session cannot move onto
a costlier encoder after its rate was admitted — see #844 for the residual
start-window race, which every cap-input guard shares.

**Effective limit (NQ2/NQ3):** `min(ISR_MAX 16000, 55000/type1Count, 110000/(6+totalEnabled), TransportMax(interface, encoding, n), ScanBounds(scan list, SAMC))`

**Transport term** (`Streaming_TransportMaxFreq`, streaming.h — #524): single-channel special-cased + `A/(B+n)` for n≥2 ("F3"), fitted ≤ the measured zero-loss ceilings (tightness 86–100%).

**The four encodings** (`eStreamingEncoding`, `StreamingRuntimeConfig.h`; `SYST:STR:FORmat <n>`). Bytes/sample measured on the bench 2026-08-19, USB, pattern 3, well below any ceiling so the figures are honest per-sample sizes. Ratios are computed from the figures shown, so they recompute:

| n | encoding | 1ch | 5ch | 16ch | vs CSV @1ch / @16ch | cap treatment |
|--:|---|--:|--:|--:|---|---|
| 0 | ProtoBuffer | 10.9 B | 18.7 B | 40.4 B | 0.69× / 0.16× | own coefficients |
| 1 | Json | 49.3 B | 137.0 B | 381.6 B | **3.12× / 1.51×** | CSV coefficients ×0.5 (uncharacterized, #529) |
| 2 | Csv | 15.8 B | 78.9 B | 252.4 B | 1.00× / 1.00× | own coefficients |
| 3 | CsvCompact | 14.8 B | 35.6 B | 89.7 B | 0.94× / **0.36×** | CSV coefficients unchanged (conservative, #619) |

`CsvCompact` (#619) is CSV with ONE leading `timestamp` column instead of a per-channel `ain<N>_ts` column — the per-channel stamps are all the same value since the compact pool shares one timestamp per sample set. Opt-in; 0/1/2 are unchanged. Its cap uses the CSV coefficients unmodified, which is safe (it emits strictly fewer bytes) but means its wire-rate win is currently unreachable through the enforced cap.

JSON's ratio is **not** a flat "2–3×" — it is ~3.1× at one channel and ~1.5× at sixteen, because the per-row framing amortises as channels grow. It is also **pretty-printed across ~5 lines per sample**, which matters for any host-side row counting: neither CSV row counting nor protobuf frame parsing can see JSON samples (see `count_mode_for` in the test suite's `test_harness.py`).

An unrecognised format value is **rejected** with `-224` since #801/#802; before that it silently selected JSON.

`Streaming_TransportMaxFreq` (`firmware/src/services/streaming.h`) branches on
`isNQ1` for four of its eight interface/PB-or-CSV coefficient sets — the tables
below are split per variant rather than annotated, so each cell can be read
directly. Values below are transcribed from `streaming.h` at `d56642a38`
(USB PB `:405-406`, USB CSV `:462-463`, WiFi `:499-500`, SD `:512-513,522`,
USB+SD `:525,543`; the WiFi CSV `min(…, 3050)` clamp is applied after the
per-interface switch, at `:560`, identically for both variants).

**NQ1:**

| interface | single (n=1) PB / CSV | A/(B+n) PB | A/(B+n) CSV |
|-----------|----:|----:|----:|
| USB    | 22000 / 20000 | 120000/(1+n) | 90000/(1+n) |
| WiFi   | 8000 / 4675   | 139000/(30+n) | min(20000/(2+n), 3050) |
| SD     | 13000 / 7500  | 99000/(4+n) | 36000/(12+n) |
| USB+SD | 8000 / 6500   | 66000/(6+n)   | 15000/(0+n) |

**NQ2/NQ3:**

| interface | single (n=1) PB / CSV | A/(B+n) PB | A/(B+n) CSV |
|-----------|----:|----:|----:|
| USB    | 15000 / 15000 | 180000/(10+n) | 34000/(1+n) |
| WiFi   | 5175 / 4675   | 139000/(30+n) | min(20000/(2+n), 3050) |
| SD     | 9000 / 7500   | 150000/(15+n) | 36000/(12+n) |
| USB+SD | 8000 / 6500   | 66000/(6+n)   | 15000/(0+n) |

The two tables share nine of their sixteen cells (WiFi CSV both columns, SD
CSV both columns, USB+SD PB both columns, USB+SD CSV both columns, WiFi PB
curve) — `Streaming_TransportMaxFreq` does not branch on `isNQ1` for those
coefficients.

The other seven diverge because the 252 MHz refits were NQ1-only. **#595**
raised USB PB, SD PB and the WiFi PB **single** term (5175 → 8000) — but not
the WiFi PB multi-channel curve, which is why that curve is one of the nine
shared cells. **#712** (issue #562) raised USB CSV.

`#600` is deliberately absent here: it re-fitted the NQ1 *additive* PB cap
(`Streaming_AdcAdditiveCap_NQ1`) and never touched `Streaming_TransportMaxFreq`,
so it has no transport coefficient to its name. The caveat further up this file
groups it with #595 because that sentence covers transport **and** additive
together, which is correct at its scope and wrong at this one.

The USB+SD CSV single is **6500** for every variant, and is deliberately BELOW
the 2-channel point of its own curve: 8000 was a 200 MHz-era coefficient that
leaked SD data at cap in 5 of 5 soak rounds, because the #712 refit covered USB
only — #719 lowered it. `streaming.h` is authoritative; these tables are a
reading aid.

**Fit basis (normative — the zero-loss sweep subset the F3 coefficients derive from, Hz):**

| interface/fmt | 1ch | 5ch | 10ch | 16ch |
|---|--:|--:|--:|--:|
| USB PB | 15000 | 12000 | 9000 | 7000 |
| USB CSV | 15000 | 6000 | 6000 | 2000 |
| WiFi PB¹ | 5175 | 4000 | ~3600 | — |
| WiFi CSV¹ | 4675 | 2857 | 2000 | 1250 |
| SD PB | 9000 | 7500 | 6000 | 5000 |
| SD CSV | 7500 | 2500 | 3000 | 1500 |
| USB+SD PB | 8000 | 6000 | 5250 | 3000 |
| USB+SD CSV | 8000 | 3000 | 1500 | 1000 |

¹ WiFi basis = the 2026-06-11 honest-scan walk-down soaks (#540), **not** the original #524 sweep — every earlier WiFi basis was inflated ~1.5× by the #537 scan-skip bug (device did less work per tick) and, before #371, by silent uncounted drops. WiFi caps are worst-night-observed by policy (link varies ~1.5× night-to-night); AIMD (#523, parked) is the long-term answer. All WiFi/USB caps re-validated 120 s at-cap on the v3.6.0 read path (46/46 cells, zero loss), and ceiling probes (2026-06-12) measured the remaining transport-fit headroom: USB T1-only +13–29%, WiFi +25–35% (good-night), SD +6–20% — see `benchmarks/541_adc_read_path/SILICON_ANCHORS.md`.

**ADC cost (synthetic PAT3 fullscale vs real PAT0), representative:** USB PB 1ch 25000→15000 (−40%), 16ch 10000→7000 (−30%); SD PB 1ch 12000→9000 (−25%). Below the transport ceiling the ADC is ~free; above it, ADC ISR/EOS load (pri-9 EOS-task wakeups preempting the pri-6 encoder) competes with the encoder.

**History in one line each:** #520 introduced the WiFi-only budget term; #524 generalized it to all interfaces and closed a format-blind hole (high-channel CSV capped above its true ceiling = silent loss); #107 removed the legacy 1 kHz T2 mux throttle (scan never overruns to ≥40 kHz — though #541 later bounded scan *interrupt* rates, which is a different limit); #540 derated WiFi to the honest-scan soak basis; #541/#543 added the three scan bounds.

**Full data + traceability:** `daqifi-python-test-suite` `benchmarks/524_streaming_characterization/` (F3 fit + 3-run matrix), `benchmarks/107_t2_scan_characterization/` (18-pass T2 scan matrix), `benchmarks/atcap_*.csv` (soak validations), `benchmarks/541_adc_read_path/` (scan-bound silicon anchors).

**Implementation:** `firmware/src/services/streaming.h` (`Streaming_ComputeMaxFreq`, `Streaming_TransportMaxFreq`), `firmware/src/services/streaming.c` (`Streaming_ComputeMaxFreqForConfig*`), `firmware/src/HAL/ADC/MC12bADC.c` (`MC12b_ScanMaxFreq`), `firmware/src/services/SCPI/SCPIInterface.c` / `firmware/src/services/SCPI/SCPIADC.c` (enforcement).

### Test Pattern Streaming Mode

Test pattern mode replaces real ADC values with synthetic data for deterministic regression testing and benchmarking. The real ISR timing, ADC triggering, pool allocation, and full encoding pipeline are preserved — only the sample Value field is overridden.

**SCPI Commands:**
```bash
SYSTem:STReam:TEST:PATtern <pattern>   # Set pattern (0=off, 1-6)
SYSTem:STReam:TEST:PATtern?            # Query current pattern (0=disabled)
```

**Pattern Types:**

| Pattern | Name | Value Generated | Use Case |
|---------|------|----------------|----------|
| 0 | Off | Real ADC data | Normal operation |
| 1 | Counter | `(sampleCount + channelId) % (adcMax+1)` | Integrity verification (PC can predict exact values) |
| 2 | Midscale | `adcMax / 2` | Consistent encoding size across CSV/JSON/ProtoBuf |
| 3 | Fullscale | `adcMax` | Worst-case ProtoBuf variable-length encoding |
| 4 | Walking | `(sampleCount * (channelId+1)) % (adcMax+1)` | Multi-channel visual verification |
| 5 | Triangle | Ramps 0→adcMax→0, period=2*(adcMax+1) | Realistic waveform, staggered per channel |
| 6 | Sine | 256-sample sine wave, scaled to [0, adcMax] | Realistic signal testing, 45deg phase offset per channel |

- `adcMax` = max raw ADC code = Resolution - 1 (4095 for MC12bADC 12-bit, 262143 for AD7609 18-bit)
- Runtime-only (not persisted to NVM, resets on reboot)
- Sample counter resets at each `StartStreamData` for deterministic sessions
- Works with all encoding formats (CSV/JSON/ProtoBuf) and output interfaces (USB/WiFi/SD)

### Streaming Throughput Benchmarking

Two benchmark tools for measuring streaming pipeline throughput:

**1. Benchmark Mode** (`SYST:STR:BENCHmark`): Three levels, each isolating a different stage of the pipeline so you can locate the actual bottleneck.

```bash
SYSTem:STReam:BENCHmark <0|1|2>   # 0=OFF, 1=NOCAP, 2=PIPELINE
SYSTem:STReam:BENCHmark?           # Query current mode
```

| Level | Name | Frequency cap | ADC in loop | Encoder runs | Use when |
|---|---|---|---|---|---|
| **0** | OFF (normal) | Active (per-channel safe rate) | Yes (real conversions) | Yes | Production / data integrity testing |
| **1** | NOCAP | Bypassed (any rate up to 100 kHz) | Yes (real conversions) | Yes | Measuring **end-to-end** throughput including ADC overhead |
| **2** | PIPELINE | Bypassed | **NO — ADC entirely skipped** (bypasses `BoardData_Get(BOARDDATA_AIN_LATEST)`; encoder fed synthetic values directly) | Yes (synthetic data only) | Measuring **WiFi/USB/SD pipeline ceiling** with ADC overhead removed |

Use **NOCAP** for "what does the system actually deliver?" (the level documented ceilings are compared against) and **PIPELINE** to isolate encoder+transport cost from ADC cost (if PIPELINE ≫ NOCAP at the same Hz, the ADC path is contributing). PIPELINE requires a non-zero `SYST:STR:TEST:PATtern` (rejected otherwise — there's no ADC data to encode). A/B the two at the same rate with `SYST:STR:STATS:CLE` between runs; restore `BENCH 0` afterward.

> **⚠️ #544 (post-#541, re-evaluated 2026-06-19 — #557):** NOCAP/PIPELINE bypass the scan-rate bounds. Driving an armed scan past them was observed **USB-fatal** (hardware reset required) **on pre-#525 firmware** — now root-caused to the **#525** EOS-task `vsnprintf` stack overflow (NOT silicon) and **re-tested GONE on v3.6.1** (1×T2 clean to 15 kHz, 11×T2 to 96k events/s; n=3 + 120 s soak, fresh reflash). The remaining real bench hazard is the FRM-documented **#539 scan-busy** limit — still enforced (NQ1: `MC12b_HardwareScanMaxFreq`). #557's headroom review is **done** (#563): for NQ1 the EOS/event caps were replaced by the freeze-aware additive model, and a NOCAP run above the additive cap now surfaces frozen-scan ticks as `ScanStaleDropped` (a visible, accounted staleness metric) instead of silent stale data. The CDC-dead wedge itself is fixed.

**Empirical NOCAP-vs-PIPELINE curve (1×T1 PB on Tesla AP, fullscale, 6 s):**

| Rate | PIPELINE wire | NOCAP wire | ADC cost |
|----:|--------------:|-----------:|---------:|
| 1 kHz | 50 KB/s | 50 KB/s | 0 % |
| 2 kHz | 95 KB/s | 95 KB/s | 0 % |
| 3 kHz | 134 KB/s | 135 KB/s | 0 % |
| 5 kHz | 211 KB/s | 211 KB/s | 0 % |
| **8 kHz** | **194 KB/s** | **70 KB/s** | **–64 %** |
| **12 kHz** | **33 KB/s** | **1 KB/s** + `qd=1793` | **–97 %** |

**Below the Tesla wire ceiling (~5 kHz × 1 ch ≈ 210–230 KB/s), ADC is invisible** — encoder + WiFi keep up without contention. **Above wire ceiling, ADC pipeline (ISRs, EOS task, BoardData mutex) takes enough CPU that the encoder/output stalls.** NOCAP saturates earlier than PIPELINE because of this. The "ADC cost" reported by simple side-by-side numbers depends entirely on whether you tested at saturation or below — if anyone reports ADC as "free" without specifying rate, treat it skeptically.

**Throughput-claim discipline:** when reporting wire-rate measurements, always state the benchmark level and the test pattern. A "5 kHz / 230 KB/s" number is meaningless without "(NOCAP)" or "(PIPELINE)" — they measure different things. PIPELINE numbers represent the **upper bound** for streaming work that doesn't read the ADC; NOCAP numbers represent the **realistic** ceiling for actual data acquisition.

**Frequency-cap interaction:** in NOCAP and PIPELINE modes the freq cap is bypassed and `SYST:StartStreamData` accepts up to 100 kHz (the timer ISR itself tops out ~90 kHz — see the `TimerISRCalls` failure modes above). Everything downstream saturates far earlier — expect `QueueDroppedSamples > 0` (encoder/queue saturation) at very high rates.

**2. Self-Contained Throughput Test** (`SYST:STR:THRoughput`): Runs a complete benchmark internally — enables benchmark mode + test pattern, streams for the specified duration, stops, and returns all stats in one response.

```bash
SYSTem:STReam:THRoughput <freq>,<duration_sec>   # Run benchmark
```

Example: `SYST:STR:THR 5000,10` streams at 5kHz for 10 seconds.

**Note:** This command blocks the USB SCPI task for the duration. Use the Python test suite (`test_throughput_benchmark.py`) for reliable automated benchmarking.

**3. SD Write Benchmark** (pre-existing): Measures raw SD card write speed independent of streaming.

```bash
SYSTem:STORage:SD:BENCHmark <size_kb>,<pattern>  # Run (e.g., 1024,0)
SYSTem:STORage:SD:BENCHmark?                      # Query results: bytes,ms,bps
```

For current measured ceilings, use the **fit-basis table** in "Streaming Frequency Capping" above (the normative zero-loss subset the enforced caps derive from) and the Session-24 soak tables in "ADC Architecture" — older synthetic-pattern snapshots have been retired from this file.

**WiFi characterization — lessons that survive (the Session-23 table itself was retracted):**

- Pre-#371 WiFi numbers (Sessions 21–23) are **inflated and unusable**: `WifiDroppedBytes` read 0 while the streaming task silently dropped up to 86 % of encoded bytes (`hasWifi = (wifiSize >= 128)` skipped the whole WriteBuffer call under back-pressure). Never trust a pre-#371 WiFi figure; the current WiFi caps come from the 2026-06-11 honest-scan walk-down basis (#540) plus the 2026-06-12 T1 re-validation.
- The firmware path through WINC1500 sustains roughly 200–340 KB/s on the bench AP — a fraction of the module's 5–10 Mbps spec. **SPI4 clock is not the bottleneck** (measured 16.67 MHz, bus idle 92.9 % during a sustained stream — Saleae); the gap is host-side send pipelining (#361/#362/#363). SPI4 baud is shared with the SD card — never change it as a "WiFi fix" without SD validation.
- WiFi link capacity varies ~1.5× night-to-night; static caps are worst-night-observed by policy, runtime AIMD (#523, parked) is the way to harvest good-link headroom.
- **`WifiTcpPartialSends` has an expected elevated band around 23–26 KB/s of wire rate (#500), and `WifiPartialBytesMissing` is NOT a loss counter (#935/#956) — it is a ring-pairing artifact.** The `WifiTcpPartialSends` counter is near-zero below ~22 KB/s and low again by ~33 KB/s. It keys on **wire byte rate, not sample rate** — 1×T1 @ 2250 Hz (24.7 KB/s) and 5×T1 @ 1250 Hz (23.6 KB/s) peak together at roughly half the sample rate and five times the channels, and both quieten at the same *byte* rates on either side. That byte-rate keying is now **partly** explained rather than merely observed (see below), and the split matters: that equal-length sends cannot expose a mis-pair is derivable from the increment site (**V** — a zero length difference contributes nothing whichever slot is read), but the claim that send lengths actually VARY mid-band and SATURATE to a constant 1400 B above it is an unmeasured distributional assumption (**I**). The `sentBytes` histogram that would settle it was deliberately not built, so the byte-rate correlation itself remains unverified. It is **not** circular-buffer-size dependent: 8 K → 64 K of `SYSTem:MEMory:WIFI:BUFfer` moves the count ~14 %, against the one-to-two orders of magnitude (32×–170× across the measured cells) between the band and its neighbouring rates. (A 4096 setting is a *different* effect — too small to sustain the rate, so throughput collapses and `WifiDroppedBytes` goes non-zero; its lower partial count is an artefact of sending far less, not an improvement.)

  #935 re-measured `WifiTcpBytesSent`/`Confirmed`/`SendErrors`/`PartialSends`/`PartialBytesMissing` from a single `SYST:STR:STATS?` snapshot (all five read inside one critical section) in two independent 45 s runs inside the band: 1×T1 PB @2250 Hz gave `Sent=1,151,952 Confirmed=1,151,765 SendErrors=0 PartialSends=136 PartialBytesMissing=2,878`; 5×T1 PB @1250 Hz gave `Sent=1,116,497 Confirmed=1,116,307 SendErrors=0 PartialSends=253 PartialBytesMissing=7,003` (**E**, board `7E2873046200E891`, bench 2026-09-09). `Sent − Confirmed` was 187 B and 190 B respectively — **not** equal to `PartialBytesMissing` (2,878 / 7,003), and **not** zero as #500 originally reported either. Neither of #935's two hypothesized branches held.

  **Root cause (V, filed as #956).** `wifi_manager.c:839`'s `sendSize - sentBytes` assumes `inflightSizes[inflightTail]` (popped at `wifi_manager.c:821`) always belongs to the completion currently firing. It doesn't, for two independently-verified reasons: (1) the `WIFI_TCP_MAX_IN_FLIGHT` (4-slot) cap is checked **outside** the mutex the ring push is inside (`wifi_tcp_server.c:612` vs `:617`), and three tasks at different priorities (`streaming_Task` 6, `app_WifiTask` 2, `lWDRV_WINC_Tasks` 1) can race across that window and overrun the ring; (2) `SYST:STR:START` and `SYST:STR:STATS:CLEar` (`SCPIInterface.c:4646-4650`, `:3447-3450`) zero `inflightHead`/`inflightTail`/`inflightSizes[]` but **not** `tcpInFlight` — precisely the invariant break `ResetInflightRing()`'s own header comment (added by #519, a #517 audit follow-up) warns causes this exact symptom; #519 fixed the three teardown call sites (`wifi_tcp_server.c:471`, `:499`, `:516`) and missed these two SCPI sites, which predate it (#370). When the pairing is wrong, the counter sums the *positive* half of a length difference between two unrelated sends and silently discards the negative half (`wifi_manager.c:835`'s `<` test), so it grows with **no byte actually lost** — and the mis-pair is only visible when consecutive send lengths differ, which is exactly what happens mid-band and stops happening once sends saturate to a constant 1400 B above it or the ring rarely fills below it.

  **What to trust instead.** The counter that bounds real un-confirmed payload is `WifiTcpBytesSent − WifiTcpBytesConfirmed`. That difference **already includes payload still in flight** at snapshot time and must not have it added again: `wifiTcpBytesSent` is incremented when the send is issued (`wifi_tcp_server.c`, inside the ring-push critical section) and `wifiTcpBytesConfirmed` only when the completion fires (`wifi_manager.c`), so an outstanding send is counted in the first and not yet in the second (**V**). Outstanding payload is **nominally** bounded by `WIFI_TCP_MAX_IN_FLIGHT × WIFI_WBUFFER_SIZE` = 5,600 B — but treat that as design intent, **not a guaranteed ceiling**, and the reason is #956 itself: `wifi_tcp_server_TransmitBufferedData()` tests `tcpInFlight` **before** taking the mutex while `TcpServerFlush()` increments it inside a later critical section, so two producers can pass the same check at 3 and both increment, leaving five sends outstanding (**V** — the same unlocked cap that mis-pairs the ring). A difference below 5,600 B is therefore **consistent with zero loss** rather than proof of it — it is indistinguishable from sends that had not completed when the snapshot was taken. Judge permanent loss only from a **delta between two drained snapshots inside one uncontaminated epoch** — and an epoch begins at a reset taken with **nothing in flight**. "No reset inside the reading window" is **not** sufficient, which an earlier revision of this paragraph claimed: Both zero `BytesSent` and `BytesConfirmed` **without draining outstanding sends** (`SCPIInterface.c`) — the same reset asymmetry #956 names for the ring, applied to these two counters. a completion landing after such a reset adds to `Confirmed` while its `Sent` contribution was erased, and **nothing ever puts it back** — `wifiTcpBytesSent` is written in exactly three places, the `+=` at flush and the two resets — so **the offset is permanent for the rest of the session**. A later drain does not repair it, and a later reset-free window inherits it.

  Both directions are bad, and **the silent one is the likelier hazard**. `Confirmed > Sent` makes the difference negative, and since both are `uint64_t` an unsigned subtraction **wraps to ~1.8e19**, reading as catastrophic loss. But the offset can equally **cancel a real loss**: 100 B sent, `CLEar` before its completion, drain (`Sent=0, Confirmed=100`), then a clean window of 1400 issued / 1300 confirmed leaves `Sent == Confirmed == 1400` **with 100 bytes genuinely gone** — zero apparent loss, and no reset in that window. It is invisible in the partial counters too: after a `CLEar` the popped `sendSize` is `0`, so the `sendSize > 0` guard suppresses the partial-send flag and only `Confirmed` moves. Two drafts of this paragraph asserted a precondition that was not the real one; this is the third, and it states what CAN be trusted rather than another list of things to avoid. **Separately, and NOT a #956 artefact:** a genuine short send is **never retried** — `TcpServerFlush` zeroes `writeBufferLength` immediately after a successful `send()` and no resend path exists, so bytes the WINC did not accept are gone regardless of ring pairing. That is a second, independent risk source; the earlier revision of this section said so, this PR's first draft dropped the sentence, and the ring-pairing explanation does not subsume it. In both #935 runs the difference was 187 B and 190 B — ~0.016–0.017 % of bytes sent and **far inside** even that nominal figure, so those runs are consistent with zero real loss. They do not support #500's original claim of exact `Sent == Confirmed` equality either, which this measurement contradicts. [Two Qodo catches on PR #957, both verified against source rather than accepted on the reviewer's word: the double-count above, and then this paragraph citing 5,600 B as a hard bound while itself documenting the race that breaks it.] **Until #956 lands, do not read a `WifiTcpPartialSends` / `WifiPartialBytesMissing` rise as evidence of lost stream bytes** — it is a diagnostic-counter defect, not a data-path one. `WifiTcpSendErrors` and `WifiDroppedBytes` remain **E**-measured 0 throughout and still only cover negative `send()` returns and circular-buffer overflow respectively — neither one was ever positioned to see this. Basis (**E**): #500's original three bench sessions 2026-08-20 (band location) + #935's two bench sessions 2026-09-09 (the counter-identity re-measurement above), all Nq1 as STA on the bench AP.

Full benchmark history (CSVs + CHANGELOG.md) is version-controlled in `daqifi-python-test-suite/benchmarks/` — that repo is the authoritative record; update it, not this file, when new results are collected.

**Implementation:** `firmware/src/services/streaming.c` (benchmark mode, gTestPattern, Streaming_GenerateTestValue), `firmware/src/services/SCPI/SCPIInterface.c` (SCPI callbacks)
