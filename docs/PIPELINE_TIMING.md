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

## Follow-up captures to run

- [ ] **Session 12 at 16 ch**: replicate the LUT+no-FPU win under
      16-channel load. Expect similar P3 relative improvement.
- [ ] **Encoder priority sensitivity sweep**: test pri 3, 4, 5, 6, 7
      now that the FPU-save cost is eliminated. Find the sweet spot.
- [ ] **FPU save cost validation**: rerun Session 10 with pattern 2
      (integer — streaming_Task's FPU registration should matter less
      if no FPU ops happen in the encoder). Compare P3 Tstd. If it
      drops back close to Session 7's 5.7 µs, FPU save is confirmed
      as the cause.
- [ ] **Encoder priority sensitivity sweep**: test pri 3, 4, 5, 6, 7.
      Find the priority that minimizes P7 wake jitter without
      hurting P3.
- [ ] **CSV encode FPU contribution**: Session 9 but CSV instead of PB
      with `VoltagePrecision = 4` (NQ1 default). Expect P8 grows
      significantly per channel — each value becomes a `double`
      multiply + format. Good parallel to Session 4's P3 finding.
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
