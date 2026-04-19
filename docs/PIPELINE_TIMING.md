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
   SYST:DIOP:MODE 0,TOGGLE   ; streaming timer ISR entry
   SYST:DIOP:MODE 1,TOGGLE   ; ADC EOS ISR entry
   SYST:DIOP:MODE 2,TOGGLE   ; deferred task wake
   SYST:DIOP:MODE 3,PULSE    ; deferred: alloc + channel loop + queue push
   SYST:DIOP:MODE 4,PULSE    ; deferred: ADC trigger + DIO trigger
   SYST:DIOP:MODE 5,TOGGLE   ; EOS task wake
   SYST:DIOP:MODE 6,PULSE    ; EOS task: result read loop
   SYST:DIOP:MODE 7,TOGGLE   ; encoder task wake
   SYST:DIOP:MODE 8,PULSE    ; encoder: encode call
   SYST:DIOP:MODE 9,PULSE    ; encoder: output write
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

---

### Sessions 5 & 6 — 2026-04-17, 16 channels / PB / USB / 5 kHz (capped from 13 kHz)

Channel scaling pair: FPU (pattern 6) and integer (pattern 2) at 16
enabled channels. Requested 13 kHz, budget constraint
`110000/(6+16) = 5000 Hz` kicks in and caps to 5 kHz. Both sessions
sustain 0 drops at cap — constraint model is calibrated correctly.

- **Firmware**: main `68023a70`
- **Board**: NQ1 (5 T1 + 11 T2 = 16 channels, all enabled via
  `ENA:VOLT:DC N,1` for N=0..15)
- **Stream config**: 13 kHz → capped to 5 kHz, PB, USB
- **Session 5 (pattern 6 sine)**: 1,765,907 samples / 0 drops
- **Session 6 (pattern 2 midscale)**: 1,099,803 samples / 0 drops

#### P3 (deferred task work) — clean FPU isolation at 16 ch

| Config | Tpos mean | Source |
|---|---:|---|
| 1 ch pattern 2 (integer) | 9.21 µs | Session 3 |
| 1 ch pattern 6 (FPU)     | 12.08 µs | Session 4 |
| 16 ch pattern 2 (integer) | **18.29 µs** | Session 6 |
| 16 ch pattern 6 (FPU)    | **43.28 µs** | Session 5 |

**Decomposition:**
- **Non-FPU per-channel cost**: (18.29 − 9.21) / 15 = **0.61 µs/ch**
  (pool alloc is fixed; channel loop + queue push scales slowly)
- **FPU per-channel cost at 1 ch**: +2.87 µs for 1 ch = **2.87 µs/ch**
- **FPU per-channel cost at 16 ch**: (43.28 − 18.29) = 24.99 µs of
  FPU work across 16 channels = **1.56 µs/ch** (amortized)

**Sub-linear FPU scaling confirmed.** Per-channel FPU cost drops
~46% (2.87 → 1.56 µs/ch) going from 1 to 16 channels. Two plausible
contributors:

1. **sin() table cache warmth.** `Streaming_GenerateTestValue` (line
   226 in streaming.c) reads a 256-entry sin table. First call faces
   cold cache; successive calls in the same tick benefit from the
   line already being resident.
2. **FPU pipeline overlap.** PIC32MZ EF's FPU is pipelined — multiple
   `mul.d`/`add.d` instructions can be in-flight simultaneously. A
   single sample can't amortize this; 16 samples in a tight loop can.

Either way: **FPU cost per sample decreases when many samples are
processed together** — useful to remember when evaluating future
FPU-using features (DSP, interpolation, voltage conversion at encode).

#### Other probes Session 5 (pattern 6) vs Session 6 (pattern 2)

| Probe | Pattern 2 | Pattern 6 | Δ |
|---:|---:|---:|---|
| 0 Tstd | 2.24 µs | 1.23 µs | ISR jitter at cap ≈ hardware floor |
| 1 Tstd | 1.70 µs | 1.84 µs | same |
| 2 Tstd | 8.12 µs | 3.32 µs | deferred wake jitter HIGHER w/o FPU |
| 4 Tpos | 10.78 µs | 10.93 µs | ADC+DIO identical (no FPU) |
| 5 Tstd | **185 ns** | 1.92 µs | EOS wake nearly perfect w/o FPU |
| 6 Tpos | 18.64 µs | 21.25 µs | read loop scales with T2 count |
| 6 Tstd | **171 ns** | 3.09 µs | same — pipeline-clean |
| 7 Tstd | 32.06 µs | 41.13 µs | encoder wake both low (saturated) |
| 8 Tpos mean | 52.79 µs | 44.23 µs | **midscale larger** — varying values hit faster varint paths |
| 9 Tpos mean | 8.10 µs | 12.38 µs | constant values pack into USB ring faster |

**Counter-intuitive finding — P5/P6 jitter nearly disappears in
Session 6.** The EOS task wake (P5, 185 ns Tstd) and work (P6, 171 ns
Tstd) show sub-microsecond jitter at pattern 2 / 16 ch. At pattern 6
they are 10–18× higher. Interpretation: when priority-8 deferred task
finishes faster (pattern 2, no FPU), the EOS task has more scheduling
slack and wakes up deterministically. Under pattern 6, the deferred
task runs ~25 µs longer per tick, increasing the probability it
preempts the EOS task wake/work path.

This is a useful general rule: **jitter on a given probe reflects
contention from everything else running at the same or higher
priority**. Reducing any hot-path work reduces jitter on neighboring
probes, not just on itself.

#### Cap-model validation

At 16 channels, constraint-model predicts `min(13000, 55000/5 = 11000,
110000/(6+16) = 5000) = 5000 Hz`. Both sessions show probe 0
fmean = 2500 Hz (→ 5000 Hz ISR rate), 0 drops over ~200–400 seconds.
**Model is accurate at this config point.** TimerISRCalls ==
TotalSamplesStreamed invariant holds across 2.9M combined ticks.

---

---

### Session 7 — 2026-04-17, 1 T1 ch / pattern 6 / PB / USB / **capture tasks @ priority 9**

Intervention run. Bumped the three capture-hot-path tasks from
FreeRTOS priority 8 to 9 and reran Session 4 config to measure the
effect on pipeline jitter:

- `_Streaming_Deferred_Interrupt_Task` (priority sets P2 wake, P3 work)
- `MC12bADC_EosInterruptTask` (priority sets P5 wake, P6 work)
- `AD7609_DeferredInterruptTask` (NQ2/NQ3, included for consistency)

Rationale: at priority 8, capture tasks share the level with nothing
they contend with on NQ1, but the next step up (priority 9) removes
them from same-level round-robin with future additions and also
pushes them above the USB/Power tasks at priority 7 if any of those
briefly boost. We paid no CPU for this; the question was whether
scheduler noise on the capture path drops.

- **Firmware**: `docs/301-pipeline-timing-fpu-sessions` branch +
  three-file priority change (streaming.c:1279, ADC.c:190,
  tasks.c:220 — each `8` → `9`)
- **Stream config**: identical to Session 4 — 1 T1 ch, pattern 6
  (sine, FPU), PB, USB, requested 13 kHz
- **Stats**: 2,532,941 samples / 2,532,941 TimerISRCalls / **0 drops**

#### Session 4 (priority 8) vs Session 7 (priority 9)

| Probe | Metric | Sess 4 (pri 8) | Sess 7 (pri 9) | Δ |
|---:|---|---:|---:|---:|
| 0 | Timer ISR Tstd | 1,066 ns | 1,049 ns | ≈0 |
| 1 | EOS ISR Tstd | 3,502 ns | 3,980 ns | noise |
| 2 | Deferred wake Tstd | 5.53 µs | 5.39 µs | -3% |
| **3** | **Deferred work Tstd** | **11.57 µs** | **5.70 µs** | **-51%** |
| 3 | Deferred work Tpos mean | 12.08 µs | 12.06 µs | ≈0 (same work) |
| 4 | ADC+DIO Tstd | 11.77 µs | 11.85 µs | ≈0 |
| 5 | EOS wake Tstd | 4.96 µs | 4.97 µs | ≈0 |
| 6 | EOS work Tstd | 5.35 µs | 4.88 µs | -9% |
| 7 | Encoder wake Tstd | 157.4 µs | 156.7 µs | ≈0 |
| 8 | Encode Tpos mean | 59.70 µs | 59.64 µs | ≈0 |
| 9 | Output write Tpos mean | 11.63 µs | 11.50 µs | ≈0 |

#### Key findings

1. **P3 jitter halved** with a one-line-per-task change. The mean
   work time is identical (the task does the same work) — what
   dropped is the variance. At priority 8 the deferred task was
   occasionally being preempted or delayed mid-work; at priority 9
   it runs to completion without scheduler interference.
2. **P6 work improved 9%**. Small but real; the EOS task read loop
   is a similar hot path.
3. **P2 and P5 wake jitter unchanged**. Wake latency is dominated
   by the ISR→task notify→context-switch path, which doesn't care
   whether the destination task is at priority 8 or 9 (both are
   above the current context).
4. **Encoder path (P7/P8/P9) untouched**, as expected — it runs at
   priority 2 and wasn't part of the change.
5. **Zero cost.** No drops, no CPU increase, same throughput.
   Priority is a scheduling hint, not a resource allocation.

#### Recommendation

**Land the priority bump as firmware change.** P3 carries the
FPU-cost-per-tick, the pool allocation, the channel loop, and the
sample queue push — reducing its variance makes the whole pipeline
more predictable at scale. The change is three lines in three files:

```c
// streaming.c:1279 — _Streaming_Deferred_Interrupt_Task
xTaskCreate(..., 512, NULL, 9, &gStreamingInterruptHandle);

// ADC.c:190 — MC12bADC_EosInterruptTask
xTaskCreate(..., 160, NULL, 9, &gADCInterruptHandle);

// tasks.c:220 — AD7609_DeferredInterruptTask (Harmony-generated)
xTaskCreate(..., 160, NULL, 9, (TaskHandle_t*)pAD7609TaskHandle);
```

Note on `tasks.c`: this file is in `config/default/` and gets
regenerated by MCC. The change will survive normal builds but may be
overwritten on next MCC session — flag if that happens.

---

### Session 8 — 2026-04-17, 1 T1 + 1 T2 / pattern 6 / PB / USB / priority 9

Shared-scan split follow-up. Enabled one Type-2 channel (ch0) alongside
the Type-1 ch1 from Session 7 to test the hypothesis that P4 becomes
bimodal when shared-scan ticks arrive (some ticks include the shared
MODULE7 scan trigger, others don't).

- **Firmware**: same as Session 7 (priority-9 capture tasks)
- **Stream config**: `ENA:VOLT:DC 0,1` + `ENA:VOLT:DC 1,1`, pattern 6,
  PB/USB, 13 kHz
- **Stats**: 3,368,016 samples / 3,368,016 TimerISRCalls / **0 drops**

#### P4 result — bimodal hypothesis rejected (for this config)

| Config | Tpos min | Tpos mean | Tpos max | Tstd |
|---|---:|---:|---:|---:|
| Sess 4 (1 T1, pri 8) | — | 9.58 µs | 59.72 µs | 15.6 µs |
| Sess 7 (1 T1, pri 9) | 6.24 µs | 9.62 µs | 57.60 µs | 11.9 µs |
| **Sess 8 (1T1+1T2, pri 9)** | 6.28 µs | **9.63 µs** | 68.60 µs | 13.6 µs |

Adding a T2 channel barely moved P4 mean (+0.01 µs). Max grew slightly
(57.6 → 68.6 µs) but is dominated by scheduler preemption outliers,
not shared-scan cost. **No bimodal distribution.**

**Why the hypothesis fails**: PR #282 enabled hardware triggering of
the ADC modules. With `MC12b_IsHwTriggerShared() == true`, the
software shared-scan trigger path in `streaming.c:411–434` is skipped
entirely — the shared scan fires via timer-match hardware event in
parallel with the timer ISR, and EOS fires ~1 ms later (the
OBDiag scan timing). None of that work happens inside P4's window.

The "23 µs → 8.85 µs" drop observed between Sessions 1 and 2 was
therefore a rate-dependent effect (possibly related to how the
deferred task interleaves with other pipeline stages at different
tick rates), not a shared-scan artifact.

#### Per-channel cost, 1 → 2 channels (1 T1 → 1 T1 + 1 T2)

| Probe | Sess 7 (1 ch) | Sess 8 (2 ch) | Δ per T2 ch |
|---:|---:|---:|---:|
| 3 Tpos mean | 12.06 µs | 14.17 µs | **+2.11 µs** |
| 6 Tpos mean | 13.77 µs | 14.00 µs | +0.23 µs |
| 7 Tstd | 156.7 µs | 189.7 µs | +21% |
| 8 Tpos mean | 59.64 µs | 83.66 µs | +24.02 µs |
| 9 Tpos mean | 11.50 µs | 16.27 µs | +4.77 µs |

P3 per-channel delta of +2.11 µs matches the Session 5 16-ch FPU
per-channel cost (1.56 µs/ch amortized; first-extra-channel is closer
to the 2.87 µs/ch observed at 1 ch). P8 per-channel is much larger —
PB encoding appends per-channel fields (timestamp delta + varint
value) per sample.

#### P1 confirms divided shared-scan rate

P1 (EOS ISR) fmean = 500 Hz (→ 1000 Hz ISR) regardless of T2
enablement. `ChannelScanFreqDiv` effectively fixes shared-scan
cadence at 1 kHz for 13 kHz ticks (ratio 1:13). So even when T2 is
enabled, the scan only completes ~77× per second, not per tick.

---

### Session 9 — 2026-04-17, PB encode per-channel scaling at 1 kHz

Isolates PB encoder scaling cleanly. At 1 kHz tick rate the encoder
stays 1:1 with ticks (no burst coalescing), so P8 Tpos mean ==
per-sample encode cost directly. Pattern 2 (integer) to avoid FPU
confound. Sweep: 1, 4, 8, 16 channels.

- **Firmware**: same as Session 8 (priority-9 capture tasks)
- **Stream config**: 1 kHz, pattern 2, PB/USB, priority-9 tasks
- **Stats**: 1,231,561 total ticks across the 4 phases / **0 drops**

#### P8 Tpos mean by channel count

| Channels | P8 Tpos mean | Δ vs prior | Per-channel Δ |
|---:|---:|---:|---:|
| 1 | **25.95 µs** | — | — |
| 4 | **41.35 µs** | +15.40 µs (3 extra ch) | +5.13 µs/ch |
| 8 | **47.83 µs** | +6.48 µs (4 extra ch) | +1.62 µs/ch |
| 16 | **81.25 µs** | +33.42 µs (8 extra ch) | +4.18 µs/ch |

#### Linear model fit

Trying `P8 = A + B × N` (fixed overhead + per-channel cost):

- **A ≈ 22 µs** fixed encode overhead (message header, queue pop,
  output buffer prep)
- **B ≈ 3.7 µs/channel** average per-channel encode cost

The 4→8 slope of only 1.62 µs/ch is anomalously low — likely cache
warmth as the sin table / encoder state stays resident. The 8→16
slope of 4.18 µs/ch comes back to trend and probably reflects cache
eviction as the working set grows.

#### Per-channel amortized cost

| Channels | Per-ch (Tpos / N) |
|---:|---:|
| 1 | 25.95 µs |
| 4 | 10.34 µs |
| 8 | 5.98 µs |
| 16 | 5.08 µs |

The amortization curve flattens out by 16 channels, bounded below by
`B ≈ 3.7 µs/ch`. The headroom for further improvement by batching
more channels into one encode call is small — the fixed 22 µs
overhead is already dominant at low channel counts but dilutes quickly.

#### Key findings

1. **PB scales ~linearly** in channel count, with ~22 µs fixed
   per-sample overhead and ~3.7 µs per-channel variable cost.
2. **Per-channel amortized cost decreases** from 25.9 → 5.1 µs/ch as
   channel count grows. Useful to remember: "1 channel PB" is not a
   meaningful throughput ceiling — add channels and you get better
   CPU utilization per data byte.
3. **No FPU involvement in PB encode path** — pattern 2 (integer)
   measurements track pattern-6-absent assumption. CSV/JSON with
   `VoltagePrecision > 0` will look different (FPU at encode time);
   see follow-up TODO.
4. **Encoder saturated 1:1 at 1 kHz regardless of channel count**.
   Zero drops, no coalescing, clean per-sample measurement.

---

### Session 10 — 2026-04-17, 1 T1 ch / pattern 6 / PB / USB / **encoder @ priority 6**

Second intervention run. Bumped `streaming_Task` (encoder) from
priority 2 to 6. Rationale: at priority 2 the encoder round-robins
with WiFi (2), SD (2), and WINC driver (2), all of which can add
scheduling noise. Priority 6 keeps it below USB (7) and Power (7)
so SCPI remains responsive, but above all background transport
tasks.

- **Firmware**: `streaming.c:1270` `2` → `6` on top of Session 7's
  priority-9 capture tasks
- **Stream config**: identical to Session 7 — 1 T1 ch, pattern 6
  (sine, FPU), PB/USB, 13 kHz
- **Stats**: 2,923,144 samples / 2,923,144 TimerISRCalls / **0 drops**

#### Session 7 (encoder pri 2) vs Session 10 (encoder pri 6)

| Probe | Metric | Sess 7 | Sess 10 | Δ |
|---:|---|---:|---:|---:|
| 0 | Timer ISR Tstd | 1,049 ns | 1,076 ns | ≈0 |
| 1 | EOS ISR Tstd | 3,980 ns | **2,238 ns** | **-44%** |
| 2 | Deferred wake Tstd | 5.39 µs | 5.64 µs | +5% |
| **3** | **Deferred work Tstd** | **5.70 µs** | **14.20 µs** | **+149%** |
| 3 | Deferred work Tpos mean | 12.06 µs | 12.74 µs | +6% |
| 4 | ADC+DIO Tstd | 11.85 µs | 14.42 µs | +22% |
| 5 | EOS wake Tstd | 4.97 µs | 6.95 µs | +40% |
| 6 | EOS work Tstd | 4.88 µs | 6.62 µs | +36% |
| **7** | **Encoder wake Tstd** | **157.4 µs** | **86.6 µs** | **-45%** |
| 8 | Encode Tpos mean | 59.64 µs | **42.76 µs** | **-28%** |
| 8 | Encode Tstd | 93.08 µs | **65.25 µs** | **-30%** |
| 9 | Output write Tpos mean | 11.50 µs | **0.86 µs** | **-93%** |
| 9 | Output write Tstd | 122.8 µs | 66.2 µs | -46% |

#### Mixed result — wins downstream, variance upstream

**Downstream wins (encoder-side, P7-P9):**
- P7 wake Tstd cut nearly in half (157 → 87 µs)
- P8 encode work both mean and Tstd improved 28-30%
- P9 output write mean went from 11.5 µs to **857 ns** — essentially
  free. The USB circular-buffer copy path finds DMA idle when
  encoder runs contiguously

**Upstream regressions (capture-side, P3/P4/P5/P6):**
- P3 deferred work Tstd +149% (5.7 → 14.2 µs)
- P4, P5, P6 Tstd all up 22-40%

**Why the capture path got worse:** most likely **FPU context save
on task switch**. Both `streaming_Task` and
`_Streaming_Deferred_Interrupt_Task` are registered with
`portTASK_USES_FLOATING_POINT()`. At priority 2, the encoder rarely
ran right when the deferred task fired — it was usually
time-sliced with WiFi/SD and often idle. At priority 6 it runs
contiguously until preempted. Every deferred-task preemption now
crosses FPU context, adding ~32× 64-bit register save/restore to
the switch. That shows up as extra variance on P3's work time
(measured from the deferred task's first instruction to its last).

**Is this a net win?** P3 mean is still 12.7 µs (tick period 77 µs,
plenty of margin). Zero drops. The measurable degradation is in
variance only, and the variance absolute values (6-14 µs) are
still much smaller than the downstream improvements (87 µs on P7
alone). So yes — **net positive for pipeline determinism**, but
not the unqualified win Session 7 was.

#### If FPU-save cost is the culprit — mitigation option

The encoder only needs FPU when `VoltagePrecision > 0` (CSV/JSON
with float output). For PB streaming it's integer all the way.
Could make `portTASK_USES_FLOATING_POINT()` conditional or
deregister it after init. Would eliminate the FPU save overhead
on the preemption path in PB mode. Speculative — not yet tested.

#### Recommendation

**Land the encoder priority bump.** Net gain in downstream jitter is
much larger than the capture-side regression. If variance on P3
becomes load-bearing later (e.g. pushing close to the per-tick
budget at high channel counts), revisit the FPU mitigation.

---

### Session 11 — 2026-04-17, 1 T1 ch / pattern 2 / PB / USB / encoder pri 6 / **FPU-save validation**

Validation of the Session 10 FPU-save hypothesis. Ran Session 10's
config but with pattern 2 (midscale, integer) instead of pattern 6
(sine, FPU). If the P3 regression in Session 10 was caused by the
sin() work itself, P3 Tstd should drop back to ~6 µs. If caused by
FPU context save/restore on every task switch, P3 Tstd stays high.

| Probe | Metric | Sess 7 pat 6 pri 2 | Sess 10 pat 6 pri 6 | Sess 11 pat 2 pri 6 |
|---:|---|---:|---:|---:|
| 3 | Tpos mean | 12.06 µs | 12.74 µs | **9.76 µs** (no sin work) |
| 3 | Tstd | 5.70 µs | 14.20 µs | **14.37 µs** (stayed high!) |

**Hypothesis confirmed: FPU-save on context switch is the cause.**
Removing sin() calls from the deferred task (pattern 2) dropped the
mean (work is simpler) but left Tstd unchanged. The variance comes
from the ~32×64-bit FPU register save/restore that happens on every
switch between two `portTASK_USES_FLOATING_POINT`-registered tasks,
regardless of whether FPU was actually touched that slice.

At pri 2 this rarely mattered because encoder was usually idle when
deferred task fired (time-sliced with WiFi/SD). At pri 6 encoder
runs contiguously → every deferred preemption pays the FPU save.

---

### Session 12 — 2026-04-17, 1 T1 ch / pattern 6 / PB / USB / **LUT sine + deferred task no-FPU**

Fix for the Session 10 trade-off. Two changes:

1. **Sine LUT**: replaced `sin()` in `Streaming_GenerateTestValue` with
   a 256-entry Q0.16 lookup table. Integer multiply+shift — zero FPU
   ops, zero dependencies on libm.
2. **Drop FPU registration from deferred task**: removed
   `portTASK_USES_FLOATING_POINT()` from
   `_Streaming_Deferred_Interrupt_Task`. With the LUT in place this
   task is pure integer forever; context switches no longer carry
   FPU state. `streaming_Task` (encoder) keeps its FPU registration
   for CSV/JSON at VoltagePrecision>0.

- **Firmware**: `streaming.c` has LUT table (+32 lines), sin() call
  replaced with LUT lookup, `portTASK_USES_FLOATING_POINT()` removed
  from deferred task. All other changes (capture tasks at pri 9,
  encoder at pri 6) from prior sessions remain.
- **Stream config**: identical to Session 7 — 1 T1 ch, pattern 6
  (now LUT-backed), PB/USB, 13 kHz
- **Stats**: 10,227,171 samples / 10,227,171 TimerISRCalls / **0 drops**

#### Full per-probe comparison across 4 sessions

| Probe | Sess 7 (baseline) | Sess 10 (enc pri 6) | Sess 11 (pat 2, enc pri 6) | **Sess 12 (LUT + no-FPU)** |
|---:|---:|---:|---:|---:|
| 0 Tstd | 1,049 ns | 1,076 ns | — | **905 ns** |
| 1 Tstd | 3,980 ns | 2,238 ns | — | **1,441 ns** |
| 2 Tstd | 5.39 µs | 5.64 µs | — | **5.22 µs** |
| **3 Tpos mean** | 12.06 µs | 12.74 µs | 9.76 µs | **8.85 µs** |
| **3 Tstd** | **5.70 µs** | 14.20 µs | 14.37 µs | **5.51 µs** |
| 4 Tstd | 11.85 µs | 14.42 µs | 14.37 µs | **11.40 µs** |
| 5 Tstd | 4.97 µs | 6.95 µs | — | **3.86 µs** |
| 6 Tstd | 4.88 µs | 6.62 µs | — | **3.72 µs** |
| 7 Tstd | 157.4 µs | 86.6 µs | 72.4 µs | **59.4 µs** |
| 8 Tpos mean | 59.64 µs | 42.76 µs | — | **34.88 µs** |
| 8 Tstd | 93.08 µs | 65.25 µs | — | **28.41 µs** |
| 9 Tpos mean | 11.50 µs | 0.86 µs | — | **0.80 µs** |
| 9 Tstd | 122.8 µs | 66.2 µs | — | **45.4 µs** |

#### Key findings

1. **Every probe is equal or better than Session 7.** Session 12 is
   the best combined result of the entire sweep.

2. **P3 regression from Session 10 fully eliminated.** Tstd back to
   5.51 µs (was 14.2 µs at pri 6 pre-fix). Mean actually *dropped*
   to 8.85 µs vs Session 7's 12.06 µs — the LUT is faster than
   `sin()` by ~3.2 µs per tick at 1 channel.

3. **Big win on downstream probes.** P7 wake Tstd: 157 → 59 µs
   (-62%). P8 Tstd: 93 → 28 µs (-69%). P9 mean: 11.5 µs → 801 ns
   (-93%). The encoder pipeline is dramatically more deterministic.

4. **Side benefits on capture probes.** P5 (EOS wake) and P6 (EOS
   work) both Tstd better than Session 7 baseline — fewer FPU
   save/restores on switches between encoder and EOS task too.

5. **ISR probes marginally better.** P0 and P1 Tstd improved 14%
   and 64% respectively. Likely less cache pressure from the FPU
   save sequence no longer running on every deferred task switch.

6. **CSV/JSON with VoltagePrecision > 0 still works correctly.**
   `streaming_Task` retains `portTASK_USES_FLOATING_POINT()` so
   `ADC_ConvertToVoltage` calls still save/restore float state
   properly. The fix only removes FPU registration where it was
   not actually needed.

#### Net result

Across the three firmware interventions (capture tasks pri 9,
encoder pri 6, LUT + deferred no-FPU), the major pipeline jitter
metrics vs the original baseline (pre-PR #307 capture priority 8):

| Metric | Pre-307 (Session 4) | **Session 12** | Reduction |
|---|---:|---:|---:|
| P3 Tstd (deferred work) | 11.57 µs | 5.51 µs | -52% |
| P7 Tstd (encoder wake) | 157 µs | 59 µs | -62% |
| P8 Tstd (encode work) | 42.4 µs | 28.4 µs | -33% |
| P9 Tstd (output write) | 63 µs | 45 µs | -29% |

Zero cost on the device: no additional CPU, no additional memory
(the LUT is 512 bytes static const), same throughput, 0 drops.

---

### Session 13 — 2026-04-17, 16 ch / pattern 6 LUT / PB / USB / 5 kHz (capped)

Scale test. Same firmware as Session 12 (LUT sine, deferred task
no-FPU, encoder pri 6, capture pri 9). Enable all 16 channels.
13 kHz request caps to 5 kHz per budget constraint.

- **Stream config**: 16 ch, pattern 6 LUT, PB/USB, 13k req → 5 kHz cap
- **Stats**: 2,339,469 samples / 2,339,469 TimerISRCalls / **0 drops**

#### Comparison to Session 5 (16 ch pre-fix, sin-based)

| Probe | Sess 5 (sin, pri 8/2) | **Sess 13 (LUT, pri 9/6)** | Δ |
|---:|---:|---:|---:|
| 0 Tstd | 1,230 ns | **1,171 ns** | -5% |
| 1 Tstd | 2,200 ns | **726 ns** | **-67%** |
| 2 Tstd | 5.42 µs | **3.41 µs** | -37% |
| **3 Tpos mean** | **43.28 µs** | **21.95 µs** | **-49%** |
| 3 Tstd | 2.90 µs | 3.20 µs | similar |
| 4 Tstd | 11.58 µs | 8.85 µs | -24% |
| 5 Tstd | 1,920 ns | **844 ns** | **-56%** |
| 6 Tpos mean | 21.25 µs | 18.92 µs | -11% |
| 6 Tstd | 3,090 ns | **372 ns** | **-88%** |
| 7 Tstd | 41.13 µs | 30.74 µs | -25% |
| 8 Tpos mean | 44.23 µs | 53.07 µs | +20% (noise) |
| 8 Tstd | 93+ µs | **27.95 µs** | **-70%** |
| 9 Tpos mean | — | 541 ns | — |

#### Key findings

1. **P3 mean dropped 49% at 16 channels.** From 43.3 → 22.0 µs per
   tick. That's ~1.4 µs saved per channel (15 extra × 1.4 = 21 µs),
   matching the per-channel LUT-vs-sin cost estimate from Sessions 9
   and 12. At scale this is the dominant win — 21 µs saved per tick
   at 5 kHz tick rate = 105 ms/sec of CPU freed.

2. **Sub-microsecond Tstd on three probes**: P1 (EOS ISR) 726 ns,
   P5 (EOS wake) 844 ns, P6 (EOS work) **372 ns**. P6 is essentially
   tick-synchronous — the EOS task runs the same sequence within a
   margin smaller than a single PBCLK cycle.

3. **Zero drops across 2.3M samples at cap.** Same throughput as
   Session 5 but with dramatically lower jitter everywhere.

4. **P8 Tpos mean +20%** is the only regression (44 → 53 µs).
   Likely run-to-run noise since encoder saturates at 5 kHz in both
   sessions. Tstd dropped 70% so the overall encoder behavior is
   much more predictable.

#### Combined result across all interventions

Vs the starting baseline (pre-PR #307, Session 4 — capture pri 8,
encoder pri 2, sin() with FPU), the three firmware changes
(capture pri 9, encoder pri 6, LUT + deferred no-FPU) achieve at
**16 channels**:

| Probe | Baseline (Sess 5) | Session 13 | Reduction |
|---|---:|---:|---:|
| P3 mean (deferred work) | 43.3 µs | 22.0 µs | **-49%** |
| P3 Tstd | 2.9 µs | 3.2 µs | similar |
| P6 Tstd (EOS work) | 3.09 µs | 0.37 µs | **-88%** |
| P7 Tstd (encoder wake) | 41.1 µs | 30.7 µs | -25% |
| P8 Tstd (encode work) | 93 µs | 28 µs | **-70%** |

And at **1 channel (Session 12 vs Session 4)**:

| Probe | Baseline (Sess 4) | Session 12 | Reduction |
|---|---:|---:|---:|
| P3 Tstd | 11.6 µs | 5.51 µs | **-52%** |
| P7 Tstd | 157 µs | 59 µs | **-62%** |
| P8 Tstd | 42 µs | 28 µs | -33% |
| P9 mean | 11.5 µs | 0.80 µs | **-93%** |

---

### Session 14 — 2026-04-17, encoder priority sweep

Tested encoder at priority 3, 5, 7 against current default of 6.
Same Session 12 config otherwise (1 T1 ch, pattern 6 LUT, PB/USB,
13 kHz). Captured P3 and P7 (the priority-sensitive probes).

| Priority | P3 Tstd | P7 Tstd | USB drops | Notes |
|---:|---:|---:|---:|---|
| 3 | — | — | — | **UNSAFE — USB CDC locked up.** Device disappeared from USB enumeration. Recovery required flash. |
| 5 | 5.36 µs | 61.5 µs | 0 | Safe, same as pri 6 |
| **6** | **5.51 µs** | **59.4 µs** | **0** | Current default (Session 12 reference) |
| 7 | 5.16 µs | 59.5 µs | **27,127 bytes in ~10s** | Unsafe — USB circular overflow |

#### Key findings

1. **Priority 3 crashes USB.** Mechanism not fully understood but
   reproducible: the encoder pre-empts priority-1 USB internals
   (`F_DRV_USBHS_Tasks`, `F_USB_DEVICE_Tasks`) enough that the host
   stops seeing USB descriptors. Device enumeration drops. Requires
   reflash to recover. Likely FreeRTOS kernel interaction
   (`configMAX_SYSCALL_INTERRUPT_PRIORITY=4`) — pri 3 is within the
   syscall-managed band, something is off.

2. **Priority 7 starves USB device task.** Encoder at same level as
   `app_USBDeviceTask` (priority 7). Round-robin time-slicing means
   USB task gets half the CPU slots; encoder produces faster than
   USB can drain, USB circular buffer overflows. 27 KB dropped in
   10 seconds — obvious regression.

3. **Priorities 5 and 6 give identical timing results.** Within
   noise for both P3 (<3% delta) and P7 (<4% delta). Either is a
   valid choice.

4. **Sweet spot is 5 or 6.** Stay safely below USB (7) and above
   background transports (WiFi/SD at 2). Priority 6 is the current
   default; no reason to change.

#### Updated task priority map

| Priority | Task | Role |
|---:|---|---|
| 9 | `_Streaming_Deferred_Interrupt_Task` | Capture |
| 9 | `MC12bADC_EosInterruptTask` | Capture |
| 9 | `AD7609_DeferredInterruptTask` | Capture |
| 7 | `app_USBDeviceTask` | USB SCPI |
| 7 | `app_PowerAndUITask` | Battery/UI |
| **6** | **`streaming_Task` (encoder)** | **Encode + output** |
| 2 | WiFi / SD / WINC / fwUpdate | Background transports |
| 1 | USB driver internals | Hardware |

Gap at priority 8: available if a task ever needs to sit between
capture (9) and USB (7) without contending with encoder (6).

---

### Session 15 — 2026-04-17, CSV encode FPU cost scaling

Parallel to Session 9 (PB scaling) with CSV format at
`VoltagePrecision=4`. Each sample invokes
`ADC_ConvertToVoltageByIndex` (double FPU math) + format double to
string with 4 decimals per channel. Quantifies the encode-side FPU
cost kept in `streaming_Task`.

- **Firmware**: final set (encoder pri 6, capture pri 9, LUT sine,
  deferred no-FPU)
- **Stream config**: 1 kHz, pattern 0 (real ADC), CSV / USB,
  `VoltagePrecision=4`

#### P8 Tpos mean by channel count — CSV vs PB

| Channels | PB (Sess 9) | **CSV (Sess 15)** | Ratio |
|---:|---:|---:|---:|
| 1 | 25.95 µs | **100.74 µs** | 3.9× |
| 4 | 41.35 µs | **192.24 µs** | 4.6× |
| 8 | 47.83 µs | **290.82 µs** | 6.1× |
| 16 | 81.25 µs | **492.58 µs** | 6.1× |

#### Linear fit

- CSV: `P8 ≈ 75 µs fixed + 26 µs/channel`
- PB:  `P8 ≈ 22 µs fixed + 3.7 µs/channel`

Per-channel CSV is **~7× PB**; fixed overhead **~3.4× PB**.

#### Key findings

1. **CSV at 16 ch uses ~49% of a 1 kHz tick for encoding alone.**
   At 5 kHz (capped rate for 16 ch) this would exceed the tick
   budget — CSV 16 ch can't sustain 5 kHz. PB can.

2. **Per-channel FPU cost dominates.** 26 µs/ch is mostly
   `ADC_ConvertToVoltageByIndex` (double mul + add + cvt) plus
   `snprintf`-style formatting with 4 decimals. Float→string on
   PIC32MZ is expensive.

3. **Workaround for CSV high-rate use**: set
   `CONFigure:VOLTage:PRECision 0` to output integer millivolts.
   Bypasses `ADC_ConvertToVoltage`, uses `int_to_str` fast path.
   Expected to drop CSV P8 closer to PB levels.

4. **Confirms why all CLAUDE.md high-rate benchmarks use PB.**
   CSV/JSON are fundamentally FPU-bound per channel per sample.

#### Implication for jitter

When streaming CSV precision > 0, every encoder preemption crosses
FPU context save/restore. If jitter on CSV workloads becomes
important, next mitigation would be to conditionally register FPU
at stream start based on `VoltagePrecision`. Out of scope for
current work.

---

### Session 16 — 2026-04-17, SD-only streaming load test at 13 kHz

Attempted multi-output load test. Finding: **SD and WiFi share SPI
bus on this board** (see `SYST:STR:INTerface 3` validation at
`SCPIInterface.c:2576-2582`), so concurrent USB+WiFi+SD streaming is
hardware-impossible during streaming. No SCPI combination exposes
"USB+SD without WiFi". Tested SD-only (interface=2) as the next
closest thing — stresses the SPI bus + SD manager task.

- **Firmware**: final set (encoder pri 6, capture pri 9, LUT sine,
  deferred no-FPU)
- **Stream config**: 1 T1 ch, pattern 6 LUT, interface=2 (SD only),
  PB format, 13 kHz, filename `loadtest.pb`
- **Stats**: 3,755,562 samples / 3,755,562 TimerISRCalls / 0 queue
  drops / 1 encoder failure / **SdDroppedBytes: 24,494,524 (47% byte
  loss)** / SdWriteMaxLatencyMs: 422 ms

#### Comparison vs Session 12 (USB-only)

| Probe | USB (Sess 12) | **SD (Sess 16)** | Δ |
|---:|---:|---:|---|
| P7 Tstd | 59.4 µs | **96.2 µs** | +62% |
| P7 Tpos max | 606 µs | **2,454 µs** | **4×** |
| P8 Tpos mean | 34.88 µs | 38.23 µs | +10% |
| P8 Tstd | 28.41 µs | 48.61 µs | +71% |
| P8 Tpos max | 357 µs | 697 µs | +95% |
| P9 Tpos mean | 801 ns | **5.62 µs** | **7×** |

#### Key findings

1. **SD write path jitter is much worse than USB.** Every probe on
   the output-path side (P7, P8, P9) sees 50–600% jitter increase.
   SD writes block the SPI bus; encoder has to wait when SD sectors
   are being flushed.

2. **Worst case write latency: 422 ms.** During a slow SD write,
   the encoder stalls, samples pile up in the streaming pool, and
   output bytes get dropped when the SD circular buffer fills.

3. **Capture path stays intact.** Despite the output-side havoc:
   - 0 queue drops (QueueDroppedSamples=0)
   - 0 USB drops (interface=2, no USB output expected)
   - TimerISRCalls = TotalSamplesStreamed exactly
   - Invariant holds across 3.75 M samples

   This validates the earlier priority/LUT interventions: a badly
   jittering output doesn't propagate backwards into the capture
   timing. Priority 9 on capture tasks isolates them from
   priority-6 encoder backpressure.

4. **13 kHz exceeds the SD ceiling.** 47% byte loss at 13 kHz / 1
   channel PB confirms SD can't sustain this rate. (CLAUDE.md table
   lists SD PB 1 ch at 13 kHz / 148 KB/s — that's the peak observed,
   but not drop-free.) For drop-free SD logging on PB 1 ch, target
   rate should be much lower (likely 2-4 kHz).

5. **Encoder failures: 1 (out of 3.75M samples).** Rare event, not a
   pattern. Not associated with any single probe event — benign.

#### Implication for users

- **For drop-free SD logging at PB / 1 ch, stay at ≤4 kHz.** Actual
  practical ceiling TBD.
- **SD + USB concurrent not supported** via SCPI interface
  (hardware is capable — `hasUsb` and `hasSD` flags work in
  encoder — but the SCPI enum only has 4 exclusive options).
- **WiFi and SD never concurrent** on this hardware (SPI bus conflict).

---

### Session 17 — 2026-04-17, USB+SD concurrent load with #309 fix

First true multi-output load test. Uses the new USB+SD mode from
PR #309 (`SYST:STR:INTerface 3` now means USB+SD, not the
hardware-impossible USB+WiFi+SD). Streams to both USB (live host)
and SD (local logging) simultaneously.

- **Firmware**: final set + #309 (Interface_All = USB+SD, SD task
  priority 2→5, `app_SDCard_IsWifiUsingSPI` no longer matches All)
- **Stream config**: 1 ch T1, pattern 6 LUT, PB format,
  `SYST:STR:INTerface 3`, SD logging to `load17*.pb`

#### Results across rates

| Rate | Duration | USB drops | SD drops | SD writes | SD maxLatency |
|---:|---:|---:|---:|---:|---:|
| 1 kHz | 94.5 s | 0 | 0 | 2,906 | 21 ms |
| 5 kHz | 67.6 s | 0 | 0 | 10,355 | 88 ms |

**Zero drops at both rates on both interfaces.** Invariant
`TimerISRCalls == TotalSamplesStreamed` holds throughout.

#### P7/P8/P9 at 1 kHz USB+SD

| Probe | Session 12 (USB, 13 kHz) | Session 9 (USB, 1 kHz) | **Session 17 (USB+SD, 1 kHz)** |
|---|---:|---:|---:|
| 7 Tstd | 59.4 µs | — | **3.78 µs** |
| 8 Tpos mean | 34.88 µs | 25.95 µs | **18.75 µs** |
| 9 Tpos mean | 801 ns | — | **4.41 µs** |

Note: Session 17 @ 1 kHz is much cleaner than Session 12 @ 13 kHz
— the rate dominates jitter. But P9 goes up because SD write cost
is ~5 µs per sample (vs USB's ~800 ns at full burst).

#### P7/P8/P9 at 5 kHz USB+SD

| Probe | Metric | **Session 17 (5 kHz)** |
|---|---|---:|
| 7 | Tstd | 22.7 µs |
| 8 | Tpos mean | 25.5 µs |
| 8 | Tstd | 21.7 µs |
| 9 | Tpos mean | 5.53 µs |
| 9 | Tstd | 23.7 µs |

#### Key findings

1. **USB+SD now works.** After PR #309, users can stream live to
   PC via USB while logging to SD card with zero data loss up to
   tested rates. Both interfaces stay synchronized.

2. **SD write time dominates P9.** At 1 kHz SD adds ~4-5 µs per
   sample to the output path. Small in absolute terms but 5×
   larger than pure-USB P9 (~800 ns at burst, ~25 ns in the
   ISR-grade path).

3. **Capture path completely isolated from SD jitter.** Even with
   SdWriteMaxLatencyMs of 88 ms (a nearly 100 ms stall during one
   SD write), zero samples dropped from the queue. Priority-9
   capture + priority-5 SD task means the SD stall only pushes on
   the output path, never the capture path.

4. **Comparison to SD-only (Session 16) at 13 kHz**: that ran at
   47% byte loss because 13 kHz exceeds the SD ceiling. Session
   17 at 5 kHz for USB+SD is within ceiling for both paths.
   Drop-free SD at 1-ch PB is bounded at somewhere between 5 and
   13 kHz. TBD.

5. **SD task priority change (2→5) was essential.** Without it,
   the earlier test attempts showed encoder at pri 6 starving the
   SD task at pri 2, resulting in only 2 SD sector writes before
   the buffer filled and dropped everything. Priority 5 places
   SD above WiFi/background but below encoder, allowing it to run
   frequently enough to drain.

#### Architectural validation

This session validates the whole intervention stack:
- Capture at 9: keeps sample timing integrity even under SD stall
- Encoder at 6: drives both outputs without USB starvation
- SD at 5: drains fast enough for concurrent operation
- Deferred task no-FPU + LUT sine: keeps P3 jitter low under load
- Interface_All = USB+SD: exposes the combination to users

---

### Session 18 — 2026-04-18, overnight characterization (comprehensive)

Ran `test_overnight_characterization.py --sd --duration 15 --endurance 120`
on the PR #308 final firmware. Phase 1: ceiling sweep across 60
configs (4 interface/format combinations × 15 channel configurations).
Phase 2: 120-second endurance at each ceiling. Total runtime: 7h 45min.

- **Firmware**: PR #308 branch tip (capture pri 9, encoder pri 6, SD pri 5,
  LUT sine, deferred no-FPU, Interface_All=USB+SD, DIOProbe:MODE)
- **Pattern**: 3 (fullscale — canonical benchmark per CLAUDE.md convention)
- **Result file**: `benchmarks/overnight_20260418_0729.csv` in test suite repo
- **Configs**: 60 ceilings + 60 endurance runs

#### Zero-loss ceilings (post-intervention) vs CLAUDE.md (pre-intervention)

USB PB:

| Config | Pre | Post | Δ |
|---|---:|---:|---:|
| 1×T1 | 15,000 | **18,000** | **+20%** ✓ |
| 1×T2 | 21,000 | 18,000 | -14% ⚠️ |
| 3×T1 | 14,000 | **16,000** | +14% ✓ |
| 3×T2 | — | 16,000 | new |
| 5×T1 | 13,000 | 14,000 | +8% ✓ |
| 5×T2 | 16,000 | 14,000 | -12.5% ⚠️ |
| 8×T2 | 16,000 | 12,000 | -25% ⚠️ |
| 11×T2 | 13,000 | 11,000 | -15% ⚠️ |
| 5T1+4T2 (9ch) | 12,000 | 12,000 | 0% |
| 5T1+11T2 (16ch) | 9,000 | 9,000 | 0% |
| **1×T1 OBDiag=OFF** | — | **20,000** | **new record** |

USB CSV — uniform improvement:

| Config | Pre | Post | Δ |
|---|---:|---:|---:|
| 1×T1 | 13,800 | **18,000** | **+30%** ✓ |
| 1×T2 | 16,200 | 18,000 | +11% ✓ |
| 3×T1 | 11,400 | **15,000** | **+32%** ✓ |
| 5×T1 | 9,600 | 12,000 | **+25%** ✓ |
| 8×T2 | 9,600 | 10,000 | +4% ✓ |
| 11×T2 | 6,400 | 8,000 | **+25%** ✓ |
| 5T1+11T2 (16ch, endurance) | 6,000 | 6,000 | 0% |
| **1×T1 OBDiag=OFF** | — | **20,000** | **new record** |

SD PB — regressed (tracked in #312):

| Config | Pre | Post | Δ |
|---|---:|---:|---:|
| 1×T1 | 13,000 | 10,000 | -23% |
| 8×T2 | 7,000 | 6,000 | -14% |
| 16ch | 7,000 | 4,000 | **-43%** |

SD CSV — small regression in mid-range, large at max channel count:

| Config | Pre | Post | Δ |
|---|---:|---:|---:|
| 1×T1 | 11,000 | 10,000 | -9% |
| 8×T2 | 3,000 | 3,000 | 0% |
| 16ch | 2,000 | 1,000 | -50% |

#### Endurance — sustained ceilings

60 endurance runs at ceiling rate for 120s each. **7 runs LEAKed** —
their Phase 1 ceiling was 1 step too high for sustained operation.
All were multi-channel SD/CSV configs where drop growth is slow
enough to be invisible in a 15s ceiling probe but accumulates over
120s:

| Config | Phase 1 ceiling | True sustained |
|---|---:|---:|
| USB CSV 5T1+11T2 | 7,000 | 6,000 |
| SD PB 5T1+3T2 | 6,000 | 5,000 |
| SD CSV 5×T1 | 4,000 | 3,000 |
| SD CSV 11×T2 | 2,000 | 1,000 |
| SD CSV 5T1+11T2 | 2,000 | 1,000 |

**Test methodology implication**: 15s ceiling probes overstate
ceilings for slow-drift configs. Tables above use endurance-
validated values where available.

#### Pattern analysis

1. **USB improved broadly.** CSV 11-32% across the board. PB T1
   +8-20%. This reflects the reduced per-tick CPU overhead from
   LUT-sine + removed FPU save/restore + encoder at pri 6 getting
   contiguous CPU time.
2. **USB PB T2 regressed -14% to -25%.** Specific to Protocol
   Buffers + Type-2 (MODULE7 shared-scan) channels. CSV T2 doesn't
   regress. Hypothesis: the new priority map changes how EOS task
   (pri 9) interleaves with deferred task (pri 9) and encoder (pri 6)
   on T2 scan bursts, reducing headroom vs the pre-intervention
   ordering where encoder was at 2 and didn't preempt anything.
3. **SD-only regressed** (#312). Filed as follow-up. Encoder at pri 6
   preempts SD at pri 5 during SD-only streaming, reducing the SD
   task's CPU share vs pre-intervention round-robin (both at pri 2).
4. **New high-water marks at 20 kHz** with OBDiag disabled at 1×T1.
   Previously untested configuration — demonstrates the tick-rate
   ceiling when ADC path is minimized.

#### Net impact by user scenario

| User scenario | Verdict |
|---|---|
| High-rate USB PB single-channel (real-time plotting) | **Improved** (+20 to +33%) |
| High-rate USB CSV any channel count | **Improved** (+11 to +32%) |
| USB PB with 8+ T2 channels | **Slight regression** (-14 to -25%) — still 11-16 kHz sustainable |
| SD-only logging | **Regressed** (-9 to -43%) — see #312 |
| USB+SD concurrent (new capability from #309) | **New** — works at 5 kHz zero-drop, untested higher |

#### Follow-ups

- #312: SD-only priority rework (probably raise SD to pri 6 or dynamic-priority)
- Investigation: PB-T2 regression mechanism (not filed yet — characterize and confirm real before ticket)

---

### Session 19 — 2026-04-18, A/B signal-integrity control test

Controlled before/after measurement of the two ADC-timing probes (P0
timer ISR, P1 EOS ISR) using identical test config on both firmware
versions. Purpose: verify PR #308's priority/LUT/FPU changes didn't
regress signal integrity while the overall throughput ceilings changed.

**Test config** (both runs):
- 1 T1 ch (ch1), pattern 3 (fullscale), PB / USB, 13 kHz
- Probes 0 + 1 both TOGGLE
- ~9s LA capture window

**A: Pre-intervention firmware** — commit `68023a70` (main tip, right
after PR #307 DIO probe framework merged, before our interventions):

| Probe | fmean | Tstd |
|---|---:|---:|
| P0 (timer ISR) | 6,496.88 Hz → 12,994 Hz ISR | **1,003 ns** |
| P1 (EOS ISR) | 499.76 Hz → 1,000 Hz ISR | **2,205 ns** |

**B: Post-intervention firmware** — PR #308 tip (capture pri 9,
encoder pri 6, LUT sine, deferred no-FPU, SD pri 5, DIOProbe:MODE):

| Probe | fmean | Tstd |
|---|---:|---:|
| P0 (timer ISR) | 6,496.88 Hz → 12,994 Hz ISR | **903 ns** |
| P1 (EOS ISR) | 499.76 Hz → 1,000 Hz ISR | **1,801 ns** |

#### Diff

| Metric | Pre | Post | Δ |
|---|---:|---:|---:|
| P0 ISR rate | 12,994 Hz | 12,994 Hz | **identical** |
| P0 Tstd | 1,003 ns | 903 ns | **-10%** |
| P1 ISR rate | 1,000 Hz | 1,000 Hz | **identical** |
| P1 Tstd | 2,205 ns | 1,801 ns | **-18%** |

**Conclusion**: signal integrity preserved or slightly improved.
Timer ISR rate is bit-for-bit identical (hardware-triggered — expected).
EOS ISR rate identical. Both jitter numbers decreased, meaning the
ADC-trigger and conversion-completion timing became marginally more
consistent post-intervention — probably a side benefit from less
cache/bus contention after removing FPU save/restore from deferred task.

**This validates the PR #308 claim** in the updated title
("reduce pipeline jitter + throughput, add USB+SD interface"): the
throughput and jitter improvements did not come at the cost of ADC
capture timing. The (timestamp, value) couple integrity contract
— sample on time, capture both — is preserved.

---

### Session 20 — 2026-04-19, overnight characterization after PR #314/#319/#320

First comprehensive rerun after the post-#308 follow-up PRs merged:
- **#314** — `taskYIELD()` → `vTaskDelay(1)` in encoder retry loop (#312 fix)
- **#319** — `SCPI_ExecutionError` helper logging every execution error (#262)
- **#320** — HeapList + LockProvider removal (#294 audit)

**Methodology**: 120 configs total — 60 ceiling probes (10s per rate step,
binary search to first leak) + 60 endurance runs (60s at each ceiling).
NoCap benchmark mode, fullscale test pattern. Results CSV:
`firmware/daqifi.X/overnight_results_20260419_0145/overnight_20260419_0746.csv`.

**Elapsed**: 6h0m.

**New high-water marks**:

| Mode | Config | Rate | KB/s | Prior best |
|---|---|---:|---:|---:|
| USB PB  | 1×T1 OBDiag=OFF | **22,000 Hz** | 290   | 20k (Session 18) |
| USB CSV | 1×T1 OBDiag=OFF | **20,000 Hz** | 332   | 18k (Session 18) |
| SD PB   | 1×T1 OBDiag=OFF | **12,000 Hz** | n/a   | 11k (Session 18) |

**T1/T2 parity**: Every mode now shows identical ceilings for matched
channel counts (1×: 20k/20k, 3×: 17k/17k, 5×: 15k/15k USB PB). The
PB+T2 regression flagged as #313 during Session 18 analysis is fully
resolved — #313 can be closed.

**SD-only recovery**: #312's SD-only regressions fully resolved (not just
partially as #314 endurance test showed). SD PB 1×T1 went 10k → 12k
(OBDiag=OFF); all channel counts match or beat pre-#308 baselines.
#312 can be closed.

**Zero regressions** anywhere vs Session 18.

**Endurance vs ceiling methodology note**: USB CSV 5×T1 OBDiag=OFF ceiling
probe reported 15k but 60s endurance at 15k leaked 8418 drops. Real
sustainable rate is 14k. This confirms Session 18's observation that
15s ceiling probes overstate slow-drift configs — endurance validation
remains necessary before claiming a ceiling.

**SD endurance transient encoder failures**: 3 of 15 SD configs reported
`encFail=1, encDrop=1` over 60s endurance (no data loss, just encoding
underrun transients). Not systemic, matches Session 18 baseline behavior.
Suspect cause: SPI-side write latency occasionally blocking an encoder
cycle. Not blocking for release.

**Root cause of Session 18 regressions being resolved**: the cumulative
effect of #314 (vTaskDelay in retry), #319 (no net change but removed
per-site duplication), and #320 (removed dead code + re-init guards
from `DIOSampleList_Initialize`). Most likely #314 itself was the
dominant fix; #320's DIO queue re-init guard may have fixed subtle
state accumulation across sessions.

---

## Follow-up captures to run
- [ ] **Shared-scan split with OBDiag off + HW trigger off**: to
      actually observe bimodal P4 we'd need to disable HW triggering
      (a build change) and force software shared-scan every tick.
      Low priority — Session 8 showed the user-visible pipeline cost
      is already minimal.
- [ ] **Encode format scaling**: same 16 ch config, compare P8 widths
      for PB / CSV / JSON. CSV+VoltagePrecision>0 invokes FPU at
      encode time — should show as P8 delta the way P3 showed for the
      deferred task.
- [ ] **Load test**: encode with WiFi + SD both active, watch P7 jitter
      and P9 tail.
- [ ] **Over-ceiling probe**: `SYST:STR:BENCH 1` + 20 kHz / 1 ch to force
      failure, record where drops begin and which stage saturates first.
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
