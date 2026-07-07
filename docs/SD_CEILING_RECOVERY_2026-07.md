# SD-only Streaming Ceiling — Bottleneck Analysis & Recovery Options

**Issue:** #315 — "dynamic priority or alternate mechanism for full SD-only ceiling recovery"
**Date:** 2026-07 · **Scope:** NQ1, analysis only (no code change)
**Author epistemic tags:** **V** = verified from primary source (code file:line / datasheet), **E** = empirical from a test we ran, **I** = inference/hypothesis linking V and E.

---

## TL;DR / Recommendation

The #315 hypothesis is *"is `app_SDCardTask` (pri 5) starved by the encoder (pri 6)?"* — and *"would a dynamic priority boost (like the READ path's pri-7 boost) recover the SD-only ceiling?"*

**Answer: partly-yes to the diagnosis, but no to the priority-boost fix.**

- The encoder-starves-SD effect is **real but already spent**: PR #314's `taskYIELD()`→`vTaskDelay(1)` change (streaming.c:1078-1086) is exactly the "let the pri-5 SD task get CPU" fix. It recovered the configs that were CPU-starved (SD CSV 16ch fully, SD PB 1×T1 partially) and did nothing for the rest — because the rest are **not** encoder-starved. **(V + E)**
- The configs that did *not* recover are bounded by two limiters a task-priority change cannot beat:
  1. **Raw SPI-mode write bandwidth** (byte-bound: high-channel CSV). **(V + E)**
  2. **ADC-ISR starvation of the SD writer** — the scan's per-conversion data-ready ISRs (pri 1) + EOS task (pri 9) preempt the SD task *regardless of its task priority*, because ISRs and pri-9 tasks sit above any priority the SD task could be boosted to. This is the mechanism #574 later characterized and turned into the `Streaming_SdAdditiveCap_NQ1` cap. **(V, streaming.h:162-196)**
- A dynamic priority boost of the SD task from 5→6 was **already tried in #314 (its Option A) and hung the firmware** on the first SD session — a documented deadlock under equal-priority round-robin between the FPU-registered encoder and the non-FPU SD task. **(N, from the #315 ticket body / #314)**

**Recommendation:** do **not** invest in a WRITE-path dynamic priority boost. It targets a bottleneck (encoder CPU contention) that #314 already relieved, and it cannot touch the two limiters that actually cap the remaining configs. Instead:

1. **First, measure** (ticket Option C) to attribute each un-recovered config to *SPI-saturated* vs *ISR-starved* vs *something else* — this is cheap (DIO probes already exist) and de-risks any further work. See the Measurement Plan below.
2. For **byte-bound (CSV / high-channel)** configs, the only real lever is **SPI throughput** — and #487 already delivered a free bump (SPI4 16.67 → 21 MHz); a DMA-mode / BRG re-fit is the follow-up.
3. For **scan-armed PB** configs, the `#574` SD-additive cap is already the *honest* ceiling. There is nothing to "recover" — those rates were never sustainable; they were previously enforced *above* the true ceiling and silently dropped. **(V, streaming.h:167-169)**

---

## 1. The SD write path (verified architecture)

**Task & cadence** — `app_SDCardTask`, priority **5**, 1024-word stack (app_freertos.c:781-787). Its steady-state loop (app_freertos.c:398-424) is a **1 ms polled** cycle:

```
DRV_SDSPI_Tasks(...)          // SPI transaction pump
sd_card_manager_ProcessState() // circular-buffer → writeBuffer → SYS_FS_FileWrite
SYS_FS_Tasks()
vTaskDelay(1)                  // SD_CARD_MANAGER_TASK_DELAY_MS = 1  (sd_card_manager.h:32)
```
**(V)**

**Producer/consumer split:**
- The **encoder** (`streaming_Task`, priority **6**, streaming.c:2467) encodes samples and calls `Streaming_WriteWithRetry(..)` → `CircularBuf_...` to push encoded bytes into the SD **circular buffer** (32 KB default, sd_card_manager.h:16). **(V)**
- The **SD task** (pri 5) drains that circular buffer in `SD_CARD_MANAGER_PROCESS_STATE_WRITE_TO_FILE` (sd_card_manager.c:1085-1152): up to `SD_CARD_MANAGER_MAX_CHUNKS_PER_CYCLE = 4` chunks/cycle (sd_card_manager.h:31), **sector-aligned** extraction (`maxExtract = (…/512)*512`, lines 1096-1100), then a **synchronous, blocking** `SYS_FS_FileWrite` inside `SDCardWrite()` (sd_card_manager.c:206-219). **(V)**

**Priority map around the SD writer** (from CLAUDE.md Task Priority Map + code):

| Context | Priority | Preempts SD task? |
|---|---|---|
| Streaming deferred / EOS / AD7609 tasks | **9** | **Yes** |
| `app_USBDeviceTask` (self-boosts, app_freertos.c:248) | **7** | Yes |
| `streaming_Task` (encoder) | **6** | Yes |
| **`app_SDCardTask`** | **5** | — |
| T2 per-conversion data-ready ISRs | ISR (pri 1 vector) | **Yes (ISR always preempts tasks)** |
| ADC EOS interrupt | ISR (pri 9 vector) | **Yes** |

**Key structural fact:** the SD writer sits *below* the encoder and *far below* the ADC ISR/EOS machinery. Any task-priority boost is capped by the fact that **ISRs and the pri-9 deferred/EOS tasks preempt every task priority the SD writer could hold.** **(V/I)**

---

## 2. Why the SD-only ceiling is lower than it "could" be — three limiters

### Limiter A — Encoder CPU contention (pri 6 over pri 5) — **already relieved by #314**

**Mechanism (V):** the encoder at pri 6 preempts the SD task at pri 5. If the encoder never yields, the SD task cannot drain the circular buffer; it fills; `Streaming_WriteWithRetry` (streaming.c:1057-1101) enters Phase 2 (50× `vTaskDelay(1)`) and Phase 3 (up to 10 s backoff), and eventually the sample pool exhausts → `QueueDroppedSamples`.

**What #314 did (V):** replaced the Phase-2 `taskYIELD()` (a no-op across priorities — streaming.c:1079-1081) with `vTaskDelay(1)`, which *blocks* the encoder for a tick so the pri-5 SD task actually gets CPU to drain. This is the standard "let the lower-priority consumer run" pattern (#312).

**Evidence it is spent (E, from #314's on-hardware table in the ticket):**

| Config | #308 (regressed) | #314 | pre-#308 | Recovery |
|---|---:|---:|---:|---|
| SD PB 1×T1 | 10k | 11k | 13k | Partial (+10 %) |
| SD PB 5×T1 | 7k | 7k | — | None |
| SD PB 11×T2 | 5k | 5k | — | None |
| SD CSV 1×T1 | 10k | 10k | 11k | None |
| SD CSV 16ch | 1k | **2k** | 2k | **Full** |

Only the configs that were *actually* Phase-2-bound (CSV 16ch, PB 1×T1) moved. The rest hit Phase-1 success (encoder's `writeFn` succeeds on the first busy-spin, streaming.c:1064-1066) and never reach the retry code #314 touched — so they are **not** encoder-starved. **(I from V+E)**

### Limiter B — Raw SPI-mode write bandwidth (byte-bound configs)

**Mechanism (V):** `SDCardWrite()` is a synchronous blocking `SYS_FS_FileWrite` over `DRV_SDSPI` on **SPI4** (SPI-mode card, 1-bit, shared bus with WINC). Sector-aligned writes measure **~500 KB/s** (CLAUDE.md "SD Card Sector-Aligned Writes"; ~320 KB/s before alignment). **(V/E)**

**Why it caps high-channel CSV first (I):** CSV emits 2-3× the bytes/sample of ProtoBuf. At 16ch the wire byte-rate reaches the ~500 KB/s SPI ceiling at a *low* sample rate (~2 kHz), which is exactly where the SD CSV 16ch cell tops out. No scheduling change moves a byte-bound wall — the SPI bus is the constraint, not CPU. **(I from V+E)**

**#487 already helped here (V):** SPI4 rides PBCLK2 with `BRG=1`, giving `84 MHz/(2·2) = 21 MHz` at the 252 MHz clock tree — up from 16.67 MHz (BRG=2) at the old 200 MHz tree (CLAUDE.md Clock Tree). That is a free ~26 % SPI-clock bump the current caps (fitted at 200 MHz) do not yet reflect.

### Limiter C — ADC-ISR starvation of the SD writer (scan-armed PB configs) — the #574 finding

**Mechanism (V, streaming.h:162-196):** for scan-armed configs (any enabled T2 user channel, or OBDiag monitoring), the MODULE7 scan fires a **per-conversion data-ready ISR** per T2 channel (pri-1 vector) **plus** a per-scan **EOS task wake at pri 9**. Both preempt the SD task (pri 5). The higher the sample rate, the more ISR/EOS CPU is stolen from the SD writer, so a scan-armed config sustains a **lower** zero-loss SD rate than a pure-T1 (no-scan) config at the same channel count.

`Streaming_SdAdditiveCap_NQ1` models this as `period_ns = 93539 + 63468·armed + 7959·nT1 + 4615·nT2user` and is `min()`'d into the SD cap (streaming.c:509-518). Before #574 these configs were enforced *above* their true ceiling and dropped silently (`SdDroppedBytes > 0`). **(V)**

**Why a priority boost cannot fix this (I):** the starving contexts are an **ISR (pri-1 vector)** and a **pri-9 task**. The SD task can be boosted at most to 7 (above the encoder) before it would starve USB/SCPI — and 7 is still below both the EOS task (9) and every ISR. So a boost changes nothing about the #574 mechanism. This is the crux of why #315's proposed lever misses the dominant limiter for these configs. **(I from V)**

---

## 3. Options considered

### Option A — Dynamic priority boost of the SD task (the #315 proposal)

Boost `app_SDCardTask` (or the encoder's write phase) during SD-only streaming, mirroring the READ-path boost.

**Precedent (V):** the READ path already does this — `SD_CARD_MANAGER_PROCESS_STATE_READ_FROM_FILE` boosts the SD task to **7** for the duration of a file transfer, then restores (sd_card_manager.c:1355-1358, 1467). **But that is a fundamentally different situation:** a READ (SCPI `SD:GET`) is a **one-shot bulk transfer with no encoder running concurrently** — streaming is not active, so nothing at pri 6 needs the CPU and there is no convoy partner. Boosting is free there. **(V/I)**

**Why it does not transfer to the WRITE path:**
- During streaming the encoder (pri 6) **must** run to produce data. Boost the SD task to ≥6 and a long synchronous `SYS_FS_FileWrite` (Limiter B) now **blocks the encoder**, starving the producer → the sample pool exhausts → `QueueDroppedSamples`. You move the drop from the SD circular buffer to the sample pool; net ceiling unchanged (or worse). **(I)**
- It still sits below the ISR/EOS starvation (Limiter C) — no help for scan-armed PB. **(V/I)**
- **It was already tried and hung.** #314's Option A (SD 5→6 equal-priority) "hung partway through the first SD stream session … equal-priority round-robin between the FPU-registered encoder and the non-FPU SD task appears to introduce a deadlock under load" (#315 ticket body / #314). Equal-priority time-slicing between a task that saves FPU context and one that does not, both contending on the SD mutex + circular buffer, is a real hazard. **(N)**

**Verdict: reject.** High risk (proven hang), addresses the one limiter (A) that #314 already relieved, misses B and C.

### Option B — Split encode (pri 6) from write (pri 5): dedicated writer task

Encoder produces into an intermediate queue at pri 6; a separate pri-5 writer owns all transport writes.

**Assessment (I):** this decouples encode-timing from write-contention cleanly, but (1) it is an architectural change with a new intermediate queue = new RAM (firmware static headroom is ~500 B above the 8192 min-stack — a new task stack + queue is a non-trivial RAM ask that likely does **not** fit without displacing something), and (2) it still does not beat Limiter B (SPI bytes) or Limiter C (ISR starvation). The encoder→SD path *already* has the circular buffer as its decoupling queue; adding a second one mostly re-shapes latency, not throughput. **Not worth the RAM or the risk given B/C dominate.**

### Option C — Measure first (attribute each un-recovered config)

Instrument the pipeline to prove *which* limiter binds each config before committing to any rework. This is the ticket's own Option C and is the **prerequisite** for B or the SPI work.

**Assessment: strongly recommended as the immediate next step** — cheap (DIO probe framework #301/#307 already exists), zero RAM/risk, and it converts the I-tagged attributions above into E-tagged facts.

### Option D — SPI throughput work (the real lever for byte-bound configs)

Raise the effective SPI write bandwidth: DMA-mode SD writes, larger sector-multiple writes, BRG re-fit at 252 MHz.

**Assessment (I):** this is the *only* option that moves Limiter B, and #487 already handed us a free 16.67→21 MHz clock bump the caps don't yet reflect. A 252 MHz re-fit of both `Streaming_TransportMaxFreq(SD, …)` and `Streaming_SdAdditiveCap_NQ1` is a scoped, measurable win with no scheduling risk. **Recommended follow-up, gated on Option C's attribution.** (True 4-bit SDIO DMA is a hardware-path change the SPI-mode card cannot use — out of scope.)

### Option E — Larger buffers

**Assessment (V/E):** buffers are burst absorbers, not wire-rate raisers — established repeatedly (WiFi 64 KB sweet-spot finding; CLAUDE.md #229 buffer sweep "no throughput difference"). A bigger SD circular buffer smooths GC stalls but does not raise the steady-state ceiling. **Reject as a ceiling fix.**

---

## 4. Recommendation

1. **Do not build the WRITE-path dynamic priority boost (Option A).** It is high-risk (proven hang), redundant with #314 for Limiter A, and blind to Limiters B and C.
2. **Run Option C (measure) next** to attribute each un-recovered config (SD PB 5×T1, SD PB 11×T2, SD CSV 1×T1) to SPI-saturated vs ISR-starved. Cheap, zero-risk, converts inference to evidence.
3. **Then, if attribution shows byte-bound headroom, pursue Option D (252 MHz SPI cap re-fit + DMA/write-size tuning).** This is the only lever that moves a byte-bound wall, and #487 already gave a free clock bump.
4. **Accept the #574 SD-additive cap as the honest ceiling for scan-armed PB configs.** Those rates were never sustainable; "recovery" there means removing the cap, which re-introduces silent drops. The correct future work for them is a 252 MHz re-fit of the additive model, not a scheduling change.

---

## 5. Measurement plan (Option C — the immediate next step)

**Goal:** for each un-recovered config, decide *SPI-saturated* vs *SD-CPU-starved* vs *encoder-starved*.

**Instrument (DIO probe framework, #301/#307 — no new RAM):**
- Probe 1: SD task `WRITE_TO_FILE` entry/exit → **SD write duty cycle** (fraction of wall time the SD task spends inside `SYS_FS_FileWrite`). Near 100 % ⇒ **SPI-saturated (Limiter B)**; scheduling changes are futile.
- Probe 2: SD circular-buffer fill % sampled each SD cycle. Persistently near full while duty < 100 % ⇒ **SD task not getting CPU (Limiter A/C)**.
- Probe 3: encoder `Streaming_WriteWithRetry` phase reached (1/2/3). Phase-1 success ⇒ encoder is *not* the constraint (rules out A). Phase-2/3 ⇒ back-pressure is real.

**SCPI-observable corroboration (no probes needed, run on the free USB board):**
- `SYST:STR:STATS?` after a fixed-duration soak at the enforced cap and at cap+1 step: read `SdDroppedBytes` (transport-bound) vs `QueueDroppedSamples` (pool exhaustion = producer starved) vs `ScanStaleDropped` (scan freeze). The dominant non-zero counter names the limiter.
- Cross the OBDiag axis: run each config OBDiag=0 vs OBDiag=1. A large ceiling drop with OBDiag=1 at the *same* channel count isolates the **scan-ISR** contribution (Limiter C), since only the monitoring scan load changes.

**Test-suite home:** add a characterization recipe under `daqifi-python-test-suite/benchmarks/315_sd_ceiling/` built on `test_harness.py` primitives (`StreamingMeasurement`, `FastReader`, `build_result_row`), driven by `test_overnight_characterization.py --at-cap --walk-down` over the un-recovered configs. A/B OBDiag and PB/CSV. The output CSV's dominant drop-counter column is the attribution.

**Decision rule:**
- Probe-1 duty ≈ 100 % (or `SdDroppedBytes` dominates) → **Limiter B** → go to Option D (SPI), stop considering scheduling.
- Probe-2 fill high + Probe-1 duty < 100 % + big OBDiag delta → **Limiter C** → the #574 cap is correct; only a 252 MHz additive re-fit applies.
- Phase-2/3 reached with low OBDiag delta → residual **Limiter A** → revisit encoder yield tuning (still *not* a priority boost).

---

## References

- `firmware/src/app_freertos.c` — SD task creation (781-787), loop (398-424), USB self-boost (248)
- `firmware/src/services/sd_card_services/sd_card_manager.c` — `SDCardWrite` (206-219), `WRITE_TO_FILE` (1085-1152), READ-path pri-7 boost (1355-1358, 1467)
- `firmware/src/services/sd_card_services/sd_card_manager.h` — `TASK_DELAY_MS` (32), `MAX_CHUNKS_PER_CYCLE` (31), circular/wbuffer sizes (14-16)
- `firmware/src/services/streaming.c` — encoder task pri 6 (2467), `Streaming_WriteWithRetry` (1057-1101), SD-additive cap application (509-518)
- `firmware/src/services/streaming.h` — `Streaming_SdAdditiveCap_NQ1` + rationale (162-196), `Streaming_TransportMaxFreq(SD,…)` (277-286)
- PR #314 (partial recovery table, Option A hang), PR #308 (regression), PR #312/#146 (SD priority / READ boost), #574 (SD-additive cap), #487 (252 MHz clock tree / SPI 21 MHz), #301/#307 (DIO probe framework)
- CLAUDE.md — Task Priority Map, "SD Card Sector-Aligned Writes", Clock Tree, "Streaming Frequency Capping"
