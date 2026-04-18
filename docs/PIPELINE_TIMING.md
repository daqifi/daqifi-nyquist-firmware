# Streaming Pipeline Timing Measurements

Scope-captured timing measurements of the streaming pipeline, gathered via
the DIO debug probe framework (`firmware/src/HAL/DioProbe.*`, issue #301).

This document is a living record. Add new capture sessions below the
existing ones — do not overwrite prior data. Each session should note
firmware SHA, board variant, streaming config, and analyzer details so
measurements remain comparable over time.

## How to capture

1. Hook a logic analyzer to DIO_0..DIO_9 (10 probes, or subset).
2. Power the device: `SYST:POW:STAT 1`.
3. Assign probes — pipeline default map:
   ```
   SYST:DIOP:ASS 0,TOGGLE   ; streaming timer ISR entry
   SYST:DIOP:ASS 1,TOGGLE   ; ADC EOS ISR entry
   SYST:DIOP:ASS 2,TOGGLE   ; deferred task wake
   SYST:DIOP:ASS 3,PULSE    ; deferred: alloc + channel loop + queue push
   SYST:DIOP:ASS 4,PULSE    ; deferred: ADC trigger + DIO trigger
   SYST:DIOP:ASS 5,TOGGLE   ; EOS task wake
   SYST:DIOP:ASS 6,PULSE    ; EOS task: result read loop
   SYST:DIOP:ASS 7,TOGGLE   ; encoder task wake
   SYST:DIOP:ASS 8,PULSE    ; encoder: encode call
   SYST:DIOP:ASS 9,PULSE    ; encoder: output write
   ```
   Or `SYST:DIOP:PIPEL TOGGLE` (then swap specific probes to PULSE as
   desired).
4. Enable channel(s), start stream: e.g. `ENAble:VOLTage:DC 1,1` then
   `SYST:StartStreamData 1000`.
5. Capture 5–10 seconds on the analyzer. Record stats per probe:
   - TOGGLE probes: `fmean`, `fmin`, `fmax`, `Tstd`, edge counts.
   - PULSE probes: all of the above plus `TposMean/Min/Max`, `TnegMean`.
6. Stop: `SYST:StopStreamData`.

## Probe map reference

| Probe | DIO | Mode | What it measures |
|---:|---:|:---:|---|
| 0 | 0 | TOGGLE | `Streaming_TimerHandler` entry — true hardware timer rate |
| 1 | 1 | TOGGLE | `ADC_EOSInterruptCB` entry — ADC end-of-scan ISR rate |
| 2 | 2 | TOGGLE | `_Streaming_Deferred_Interrupt_Task` wake after `ulTaskNotifyTake` |
| 3 | 3 | PULSE | Deferred task: sample pool alloc + channel loop + queue push |
| 4 | 4 | PULSE | Deferred task: `ADC_TriggerConversion` + `DIO_StreamingTrigger` |
| 5 | 5 | TOGGLE | `MC12bADC_EosInterruptTask` wake |
| 6 | 6 | PULSE | EOS task: MC12b_ReadResult loop (skips + reads across 16 channels) |
| 7 | 7 | TOGGLE | `streaming_Task` wake (encoder task) |
| 8 | 8 | PULSE | `streaming_Task`: encoder call (CSV/PB/JSON) |
| 9 | 9 | PULSE | `streaming_Task`: USB/WiFi/SD output write |

## Jitter model

Jitter grows as work moves down the scheduling hierarchy. This pattern
repeats across captures — expect ISR work to be ~tens of nanoseconds,
priority-8 tasks ~hundreds, priority-2 tasks ~microseconds.

| Layer | Expected Tstd order of magnitude |
|---|---:|
| Hardware ISR (priority 1) | tens of ns |
| FreeRTOS priority 8 task wake | 30–200 ns |
| FreeRTOS priority 8 task work | 20–30 ns (under light load) |
| FreeRTOS priority 2 task wake | ~1 µs |
| FreeRTOS priority 2 task work | 10+ µs (preemption-dominated) |

## Capture sessions

---

### Session 1 — 2026-04-17, 1 kHz / 1 Type-1 ADC ch / PB / USB

- **Firmware**: `feat/301-dio-debug-framework` branch, HEAD `265e5580`
  + DIO probe framework (uncommitted, this PR)
- **Board**: NQ1
- **Analyzer**: 8-channel Saleae Logic 8
- **Stream config**: 1 kHz, 1 Type-1 ADC channel (`ENA:VOLT:DC 1,1`),
  Protocol Buffers encoding, USB only
- **Firmware stats**: 127,170 samples / 127,170 TimerISRCalls, 0 drops,
  1,266,281 bytes streamed over ~127 seconds

#### Toggle probes (ISR rate verification)

| Probe | Description | fmean | Tstd | Nedges/ΔT |
|---:|---|---:|---:|---:|
| 0 | Timer ISR entry | 500.0002 Hz | **18.3 ns** | 7939 / 7.94s |
| 1 | EOS ISR entry | 500.0002 Hz | **18.3 ns** | 7926 / 7.93s |
| 2 | Deferred task wake | 500.0002 Hz | **149 ns** | 7863 / 7.86s |
| 5 | EOS task wake | 500.0002 Hz | **29 ns** | 7876 / 7.88s |
| 7 | Encoder task wake | 500.0000 Hz | **920 ns** | 9028 / 9.03s |

All TOGGLE probes exactly 1:1 with 1 kHz timer (500 Hz square wave,
rising+falling counts balanced). Zero spurious edges across ~8 seconds
of capture — signal isolation working.

#### Pulse probes (work duration)

| Probe | Description | Tpos mean | Tpos min | Tpos max | Tstd |
|---:|---|---:|---:|---:|---:|
| 3 | Alloc + channel loop + queue push | **9.89 µs** | 9.76 µs | 10.76 µs | 149 ns |
| 4 | ADC + DIO trigger | **23.22 µs** | 23.04 µs | 29.36 µs | 209 ns |
| 6 | EOS task: read loop | **13.06 µs** | 12.88 µs | 17.48 µs | 25 ns |
| 8 | Encode (PB, 1 ch) | **26.13 µs** | 17.48 µs | 74.08 µs | 14.17 µs |
| 9 | Output write (USB) | **4.07 µs** | 0.56 µs | 39.64 µs | 11.67 µs |

#### CPU budget at this config

| Stage | Width (mean) | % of 1 ms tick |
|---|---:|---:|
| Deferred: alloc + loop + push (P3) | 9.89 µs | 0.99% |
| Deferred: ADC + DIO trigger (P4) | 23.22 µs | 2.32% |
| EOS task: read loop (P6) | 13.06 µs | 1.31% |
| Encoder: encode call (P8) | 26.13 µs | 2.61% |
| Encoder: output write (P9) | 4.07 µs | 0.41% |
| **Measured total** | **76.37 µs** | **7.64%** |

Remaining ~92% is FreeRTOS idle + uninstrumented paths (USB DMA callbacks,
WiFi driver, power/UI task, etc.).

#### Key findings

1. **Encode is the dominant per-stage cost** even at 1 channel PB (26 µs,
   2.6% of tick). Scales near-linearly with channel count — it's the
   primary bottleneck at the high-rate / high-channel-count ceilings
   shown in CLAUDE.md's characterization table.
2. **P4 is mostly DIO, not ADC.** `MC12b_TriggerConversion` is close to
   a no-op when hardware triggering is active (#282). The 23 µs is
   dominated by `DIO_StreamingTrigger` scanning 16 DIO channels each tick.
   Confirmation test: repeat with `ENA:DIO:GLOB 0` — probe 4 width should
   drop to ~1–2 µs.
3. **EOS path has lower jitter than deferred path** (29 ns vs 149 ns for
   wake, 25 ns vs 209 ns for work). EOS task runs in a quiescent window
   ~2 µs after the timer tick; deferred task races with priority-2
   traffic that was preempted mid-cycle. May also reflect FPU save cost
   in deferred (test pattern `sin()`).
4. **Encoder wake jitter ~1 µs (probe 7), inter-arrival ±6% at probe 9.**
   That's the scheduler's price for running encoder at priority 2. Under
   load (WiFi+SD active) this jitter will grow; threshold at which it
   causes backlog is a future measurement.
5. **Framework overhead is negligible.** Per-toggle cost is
   `if (!gDioProbeAnyActive) return` + slot copy + `GPIO_PortToggle`
   — measured Tstd on P0/P1 matches what you'd expect from pure hardware
   (18 ns), so the probe itself is not a significant contributor.

---

---

### Session 2 — 2026-04-17, 13 kHz / 1 Type-1 ADC ch / PB / USB

Same hardware and firmware as Session 1. Streamed at the firmware's
frequency cap for 1 Type-1 channel (13 kHz), which corresponds to the
constraint-model ceiling `min(ISR_MAX=13000, TYPE1_AGG=55000/1,
BUDGET=110000/(6+1)=15714)`.

- **Stream config**: 1 kHz → **13 kHz**, otherwise identical to Session 1
- **Firmware stats**: 3,710,493 samples / 3,710,493 TimerISRCalls / **0 drops**
  across the full 285-second session. 36,893,794 bytes streamed.

#### Toggle probes (wake rate & ISR rate)

| Probe | Description | fmean (Hz) | Tstd | Edge count |
|---:|---|---:|---:|---:|
| 0 | Timer ISR entry | 6496.87 (→ **12,994 Hz ISR**) | **912 ns** | 125,259 / 9.64s |
| 1 | EOS ISR entry | 499.76 (→ **1,000 Hz**) | 2.2 µs | 9,593 / 9.60s |
| 2 | Deferred task wake | 6496.87 (→ 12,994 Hz) | **5.42 µs** | 122,517 / 9.43s |
| 5 | EOS task wake | 499.76 (→ 1,000 Hz) | 4.16 µs | 9,334 / 9.34s |

**Probe 1 (EOS ISR) is at ~1 kHz, not 13 kHz.** Expected post-PR #290 —
Type 1 dedicated-module conversions no longer fire EOS (the residual
1 kHz rate is the divided-rate shared/OBDiag scan path).

#### Pulse probes (work duration)

| Probe | Description | Tpos mean | Tpos min | Tpos max | Tstd |
|---:|---|---:|---:|---:|---:|
| 3 | Alloc + channel loop + queue push | 9.81 µs | 5.80 µs | **73.56 µs** | 5.82 µs |
| 4 | ADC + DIO trigger | **8.85 µs** (↓ from 23.22) | 5.92 µs | 59.72 µs | 15.6 µs |
| 8 | Encode (PB, 1 ch) | 36.0 µs | 10.0 µs | **349 µs** | 42.4 µs |
| 9 | Output write (USB) | **0.91 µs** (↓ from 4.07) | 240 ns | 206 µs | 63 µs |

**Probe 8 only fires at 10,454 Hz (vs 13,000 Hz timer).** No drops occur
because `streaming_Task` early-continues on empty queue — encoder runs
in bursts and drains multiple accumulated samples when it gets CPU,
then idles when queue is empty. Total encoded packets still match
total timer ticks.

#### Jitter comparison: 1 kHz → 13 kHz

| Source | 1 kHz Tstd | 13 kHz Tstd | Ratio |
|---|---:|---:|---:|
| Timer ISR entry (P0) | 18 ns | 912 ns | **50×** |
| EOS ISR entry (P1) | 18 ns | 2.2 µs | **122×** |
| Deferred task wake (P2) | 149 ns | 5.42 µs | **36×** |
| EOS task wake (P5) | 29 ns | 4.16 µs | **144×** |
| Encoder wake (P7) | 920 ns | *not captured* | — |
| Encoder work (P8 Tstd) | 14.17 µs | 42.4 µs | 3× |

#### Key findings

1. **Zero drops at 13 kHz** — the constraint-model ceiling is real and
   sustainable. TimerISRCalls == TotalSamplesStreamed invariant holds
   across 3.7M samples.
2. **Probe 4 work dropped 23 µs → 8.85 µs** at the higher rate. Likely
   cause: `ChannelScanFreqDiv` skips the shared-scan trigger at most
   high-rate ticks — only DIO scan cost remains. Validate by capturing
   with a shared channel enabled; P4 should split into two distinct
   widths depending on whether that tick includes a shared scan.
3. **Probe 9 work dropped 4.07 µs → 910 ns.** Small PB packets (~8 B)
   pile into the USB circular buffer via fast memcpy; DMA flush rarely
   triggers. Max 206 µs confirms DMA path is still there, just rare.
4. **Encoder produces at 10.5 kHz, input is 13 kHz.** Reconciled by
   early-continue-on-empty-queue — encoder processes in bursts. This is
   why firmware stats show 0 drops despite the probe 8 rate mismatch.
5. **Priority-8 task wake jitter blew up 36–144×.** Even tasks that
   don't run often (EOS task at 1 kHz) suffer jitter from surrounding
   13 kHz load — the scheduler isn't quiet enough to wake cleanly.
6. **Encoder tail latency is the key bottleneck.** P8 max 349 µs is
   4.5× the 77 µs tick budget. At higher channel counts this max will
   grow until it exhausts the sample pool.
7. **ISR-in-ISR contention was NOT the dominant timer-ISR jitter cause**
   (as originally hypothesized during the capture session). Post-#290
   EOS only fires ~1 kHz, so timer/EOS collisions happen only 1 in 13
   ticks. The 912 ns P0 Tstd is more likely cache + bus contention from
   the background tasks running constantly.

---

### Session 3 — 2026-04-17, 13 kHz / 1 T1 ch / PB / USB / **midscale test pattern**

Intended as the "FPU contribution" follow-up (TODO #1) — rerun Session 2
with `SYST:STR:TESTpattern 2` to eliminate the `sin()` call in test
pattern sample generation, then compare P2/P3 jitter.

- **Firmware**: main `68023a70` (framework merged)
- **Board**: NQ1 | **Analyzer**: Saleae Logic 8 (10 ch)
- **Stream config**: 13 kHz, 1 Type-1 ADC ch (`ENA:VOLT:DC 1,1`),
  Protocol Buffers, USB only, `SYST:STR:TESTpattern 2`
- **Firmware stats**: 40,390,253 samples / 40,390,253 TimerISRCalls /
  **0 drops** / 441,761,451 bytes / ~52 min session

#### Per-probe comparison vs Session 2

| Probe | Metric | Session 2 (default) | Session 3 (pattern 2) | Δ |
|---:|---|---:|---:|---:|
| 0 TOGGLE | Tstd | 912 ns | **1,016 ns** | +11% |
| 1 TOGGLE | Tstd | 2.2 µs | **3.5 µs** | +59% |
| 2 TOGGLE | Tstd | 5.42 µs | **5.54 µs** | +2% |
| 3 PULSE | Tpos mean | 9.81 µs | **9.21 µs** | -6% |
| 3 PULSE | Tstd | 5.82 µs | **6.24 µs** | +7% |
| 4 PULSE | Tpos mean | 8.85 µs | **9.41 µs** | +6% |
| 4 PULSE | Tstd | 15.6 µs | **11.3 µs** | -28% |
| 5 TOGGLE | Tstd | 4.16 µs | **4.94 µs** | +19% |
| 6 PULSE | Tpos mean | — | 13.88 µs | new |
| 7 TOGGLE | Tstd | — | 132.8 µs | new |
| 8 PULSE | Tpos mean | 36.0 µs | **52.95 µs** | +47% |
| 8 PULSE | Tpos max | 349 µs | **479 µs** | +37% |
| 9 PULSE | Tpos mean | 910 ns | **10.0 µs** | +1000% |

#### Key findings — null result on FPU, methodology correction

1. **No FPU signal in P2/P3.** Deferred task wake (P2) and work (P3)
   jitter are essentially unchanged. The TODO hypothesis was wrong:
   **Session 2 did not use pattern 6 (sine)**, so there was no `sin()`
   call to eliminate. Switching pattern 0→2 replaces a real ADC read
   with a constant (`adcMax/2`) but neither path touches FPU in
   `Streaming_GenerateTestValue`.
2. **P8/P9 widened, but not from FPU.** Encode mean +47%, output write
   mean +1000%. Driver: with a constant value, the encoder task
   coalesces differently — the P8 invocation rate (8558 Hz vs 10454 Hz
   in Session 2) means each burst drains more samples, so per-burst
   work inflates. Not an FPU effect; PB encoding of constant 2047 is if
   anything cheaper than the varying Session 2 values.
3. **The real FPU test is pattern 6 (sine) vs pattern 2 (midscale).**
   Pattern 6 runs `sin()` per sample in the deferred task (P3 region) —
   that's where the FPU work lives. See Session 4 TODO below.
4. **P7 encoder wake jitter blew up to 133 µs.** Not captured in
   Session 2 but is the highest scheduler-induced jitter on any probe.
   Priority-2 task under burst-coalesce scheduling.

---

### Session 4 — 2026-04-17, 13 kHz / 1 T1 ch / PB / USB / **sine test pattern (FPU)**

Corrected FPU test: same config as Session 3 but `SYST:STR:TESTpattern 6`
(sine). Pattern 6 runs `sin()` + 2× double multiply + 1× double add +
1× double→int conversion per sample per channel
(`Streaming_GenerateTestValue` in `streaming.c:222–228`). This is where
the hardware FPU actually gets exercised in the pipeline — before the
encoder, inside the deferred task (priority 8).

- **Firmware**: main `68023a70`
- **Board**: NQ1 | **Analyzer**: Saleae Logic 8
- **Stream config**: 13 kHz, 1 T1 ch, PB / USB, `SYST:STR:TESTpattern 6`
- **Firmware stats**: 5,314,025 samples / 5,314,025 TimerISRCalls /
  **0 drops** / 57,666,604 bytes

#### Pattern 2 (no FPU) vs Pattern 6 (FPU) at 13 kHz

| Probe | Metric | Pattern 2 | Pattern 6 | Δ | FPU signal? |
|---:|---|---:|---:|---:|:---:|
| 0 TOGGLE | Timer ISR Tstd | 1,016 ns | **1,066 ns** | +5% | no |
| 1 TOGGLE | EOS ISR Tstd | 3,515 ns | **3,502 ns** | 0% | no |
| 2 TOGGLE | Deferred wake Tstd | 5.54 µs | 5.53 µs | 0% | no |
| 3 PULSE | **Alloc+loop+push Tpos mean** | **9.21 µs** | **12.08 µs** | **+31% (+2.87 µs)** | **YES** |
| 3 PULSE | **Alloc+loop+push Tstd** | 6.24 µs | **11.57 µs** | **+85%** | **YES** |
| 4 PULSE | ADC+DIO trigger Tpos mean | 9.41 µs | 9.58 µs | +2% | no |
| 5 TOGGLE | EOS task wake Tstd | 4.94 µs | 4.96 µs | 0% | no |
| 6 PULSE | EOS read loop Tpos mean | 13.88 µs | 13.80 µs | 0% | no |
| 7 TOGGLE | Encoder wake Tstd | 132.8 µs | **157.4 µs** | +18% | indirect |
| 8 PULSE | Encode Tpos mean | 52.95 µs | **59.70 µs** | +13% | indirect |
| 9 PULSE | Output write Tpos mean | 10.04 µs | 11.63 µs | +16% | indirect |

#### Key findings — FPU cost is real and localized to P3

1. **P3 is the FPU probe.** Deferred task work grew 9.21 → 12.08 µs
   mean when the only change was adding `sin()` + double mul/add per
   sample. That's **~2.87 µs of FPU work per tick**. At 1 enabled
   channel that's essentially the cost of one sin() call plus a
   few `mul.d`/`add.d`/`cvt.l.d` instructions — consistent with
   PIC32MZ EF's hardware FPU latency (sin is software-emulated via
   libm table lookup + polynomial, but the multiplies are hardware).
2. **P3 Tstd grew 85%** (6.24 → 11.57 µs). The `sin()` path hits
   libm table lookup + polynomial eval — branch density is higher
   than integer path, so per-tick variance rises. Not FPU context
   save; FPU save on FreeRTOS context switch is bounded (32× 64-bit
   store/load) and doesn't appear on P3 which stays within one task
   invocation.
3. **ISRs are unaffected.** P0 and P1 Tstd within 5% across patterns.
   FPU work is in task context; ISR handlers never use FPU. This is
   exactly what the config-matrix predicts.
4. **P4/P5/P6 unchanged.** ADC trigger, EOS task wake, and EOS read
   loop don't use FPU, don't move with pattern change.
5. **P7/P8/P9 inflated indirectly.** The +2.87 µs per-tick cost in
   P3 shifts the encoder input timing, which changes burst coalescing.
   P7 wake jitter +18%, P8 mean +13%, P9 mean +16% — all downstream
   of the P3 FPU work, none are themselves FPU-bound.
6. **Null hypothesis for FreeRTOS FPU save rejected** (in this
   config). If saving the 32× 64-bit FPU registers on every task
   switch contributed noticeable jitter, we'd see it on P2 wake
   (deferred task wake latency). P2 Tstd is identical across patterns
   — the FPU save/restore path is not a jitter driver here.

#### Implication for production benchmarking

- **FPU cost is pattern-6-only.** Patterns 0 (real ADC), 1 (counter),
  2 (midscale), 3 (fullscale), 4 (walking), 5 (triangle) are all
  integer arithmetic — zero FPU ops in the sample-generation path.
  Only pattern 6 (sine) and pattern 0 when `VoltagePrecision > 0` via
  a float-capable encoder (CSV/JSON, not PB) exercise the FPU.
- **All production ceiling benchmarks (CLAUDE.md characterization
  table) use pattern 3 (fullscale).** Rationale: pattern 3 yields the
  largest PB varint encoding (worst case for encoder throughput) and
  is integer — no FPU. Therefore the ~2.87 µs/tick P3 inflation seen
  here does not appear in those numbers.
- **This measurement is bounded.** Session 4 quantifies the maximum
  FPU contribution the pipeline can experience under the current code
  paths. Users running sine synthesis OR CSV/JSON with voltage
  conversion pay this cost; PB users and integer patterns do not.
- Follow-up: same sine test at 16 T1 channels — P3 should grow
  ~linearly with FPU ops per tick. Sub-linear growth implies FPU
  pipeline overlap; super-linear implies cache effects.

---

## Follow-up captures to run

- [ ] **FPU channel scaling**: Session 4 at 16 T1 channels. P3 should
      grow ~linearly with FPU ops per tick. Sub-linear growth implies
      FPU pipeline overlap; super-linear implies cache effects.
- [ ] **DIO isolation test**: `ENA:DIO:GLOB 0` does **not** gate the
      per-channel SFR writes — it only gates the push to the DIO
      sample queue. To actually measure P4's DIO-vs-ADC split, build
      with `DIO_PROBE_ENABLE_MASK=0xFC00` (owns channels 10-15 as
      ad-hoc probes so all 16 slots are owned → `DIO_WriteStateSingle`
      skipped in the loop) and rerun.
- [ ] **Shared-scan split**: Session 2 + enable a Type-2 channel
      (e.g. `ENA:VOLT:DC 0,1`), verify P4 shows bimodal width
      (shared-scan ticks vs non-shared ticks).
- [ ] **Channel scaling**: 16 Type-1 ADC channels @ 1 kHz. Does P3 scale
      linearly with channel count?
- [ ] **Encode format scaling**: same config, compare P8 widths for
      PB / CSV / JSON.
- [ ] **Load test**: encode with WiFi + SD both active, watch P7 jitter
      and P9 tail.
- [ ] **Over-ceiling probe**: `SYST:STR:BENCH 1` + 20 kHz / 1 ch to force
      failure, record where drops begin and which stage saturates first.
- [ ] **Type 2 only**: stream with a MODULE7 channel only, watch P1 for
      scan-rate vs tick-rate relationship.
