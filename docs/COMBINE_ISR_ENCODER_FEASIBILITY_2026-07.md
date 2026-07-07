# Feasibility: Combining the ISR-Deferred Task and the Encoder Task

**Ticket:** [#252 — perf: reduce context-switch overhead — combine ISR deferred + encoder tasks](https://github.com/daqifi/daqifi-nyquist-firmware/issues/252)
**Date:** 2026-07
**Status:** Feasibility analysis (doc-only). **Recommendation: keep the two tasks separate.**

Every load-bearing claim below is tagged **V** (verified from primary source — code/datasheet),
**E** (empirical — a test we ran), or **I** (inference/hypothesis) per the repo's debugging-discipline rule.

---

## 1. What the pipeline actually is today

The streaming pipeline is **two FreeRTOS tasks** plus the streaming timer ISR. The ticket text
describes them as "priority 8" and "priority 2" — those numbers are **stale**; the shipped code
is different. The verified topology:

| Stage | Task | Priority | Stack (words) | FPU? | Role |
|---|---|---|---|---|---|
| Timer ISR | `TSTimerCB` → `vTaskNotifyGiveFromISR` | (T5 vector, pri 3) | ISR stack | no | Wakes the deferred task, nothing else |
| Collect | `_Streaming_Deferred_Interrupt_Task` | **9** | 512 (214 peak) | **no (pure integer)** | Reads ADC (T1 ARDY-gated + T2 LATEST), builds one `AInSample`, pushes to sample list, notifies encoder |
| Encode + output | `streaming_Task` | **6** | 1392 (692 peak) | **yes** | Pops sample(s), CSV/JSON/PB encode, writes USB/WiFi/SD circular buffers |

**V** `firmware/src/services/streaming.c:2464-2478` — `xTaskCreate(streaming_Task, …, 6, …)` and
`xTaskCreate(_Streaming_Deferred_Interrupt_Task, …, 9, …)`. Priorities are the **literals 9 and 6**,
not the stale `#define STREAMING_ISR_TASK_PRIORITY 8` at line 71 (that macro is not passed to the
create call — a documentation hazard worth deleting, but not this ticket's job).

**V** The two tasks are **not** coupled by a full FreeRTOS message queue for the *signal*. The
deferred task pushes the sample into the sample **object pool / list** (`AInSampleList_PushBack`,
`streaming.c:805`) and then raises a **task notification** (`xTaskNotifyGive(gStreamingTaskHandle)`,
`streaming.c:916`). The timer ISR notifies the deferred task via `vTaskNotifyGiveFromISR`
(`streaming.c:923`). So the per-tick cost the ticket wants to remove is:

```
timer ISR  --notify-->  deferred task (pri 9)  --pool push + notify-->  encoder task (pri 6)
```

**I** That is **two task activations per tick**, not "two context switches per sample via
xQueueSend/xQueueReceive" as the ticket states. The sample pool is O(1) free-list allocation, not a
copying queue; the signalling is a direct-to-task notification (single word, no queue object). The
ticket's premise ("eliminate the FreeRTOS queue") is partly already true — there is no per-sample
message queue to eliminate, only a notification and a pool hand-off.

---

## 2. Why the split exists (the deliberate design)

Three independent, documented reasons. All three are load-bearing; a merge sacrifices all three.

### 2.1 The deferred task is intentionally **pure-integer** (no FPU) — #369

**V** `streaming.c:559-566` header comment: *"No `portTASK_USES_FLOATING_POINT()` — this task is
pure integer. sin() in test pattern 6 was replaced with a Q0.16 LUT. Keeping FPU registration off
saves 32× 64-bit register save+restore on every context switch with another FPU-registered task
(notably streaming_Task …)."*

**V** CLAUDE.md "Atomicity & Concurrency Rules" / Task Priority Map: `_Streaming_Deferred_Interrupt_Task`
is explicitly listed **pure-integer** and this is called "intentional — saves 32 × 64-bit register
save/restore per context switch." The encoder `streaming_Task` **is** FPU-registered because CSV/JSON
at `VoltagePrecision > 0` and `ADC_ConvertToVoltage()` execute `mul.d`/`div.d`.

**V** The PIC32MZ2048EFM144 has a hardware **64-bit double-precision FPU (32 registers)**.
`configUSE_TASK_FPU_SUPPORT = 1`; a task that calls `portTASK_USES_FLOATING_POINT()` gets its
32×64-bit FPU context (256 bytes) saved and restored on **every** context switch into/out of it.

**Consequence of merging:** the combined task *must* be FPU-registered (it does the encoding math).
That FPU save/restore would then land on the **hot ISR-deferral path** — the exact path #369 worked
to keep FPU-free. Every timer tick would pay the 32×64-bit context-save cost that today only the
lower-rate encoder path pays.

### 2.2 The deferred task is **priority 9** for deterministic sample timing

**V** CLAUDE.md Task Priority Map: capture tasks at priority 9 "preempt everything to guarantee
deterministic sample timing." The encoder sits at 6 deliberately — *below* USB (7) so SCPI stays
responsive during streaming, *above* SD (5) and WiFi/WINC (1-2) so encoding isn't starved.

**V** `docs/PIPELINE_TIMING.md` §"Jitter model": jitter grows monotonically as work moves down the
scheduling hierarchy — priority-9 wake jitter is order-nanoseconds-to-single-µs; the encoder wake
(probe 7) is order-µs and grows with transport load.

**E** `docs/PIPELINE_TIMING.md` Session 2→3 (13 kHz, 1×T1): deferred-task wake/work jitter (probes
2/3) stayed ~5-6 µs Tstd while the **encoder** path (probe 8) ran 36→53 µs mean with 349→479 µs
**max** excursions. The encoder path is already the jittery one; it routinely blocks tens-to-hundreds
of µs waiting on USB/SD/WiFi back-pressure.

**Consequence of merging:** to encode, the merged task runs at the encoder's effective priority (or
the sample-collection determinism is lost). If we keep it at 9, then a slow encode (a USB circular
buffer that's momentarily full, an SD 512-byte sector write, a WiFi SPI stall) **blocks the next
timer tick's ADC read** — because the same task that must service the tick is stuck in output
back-pressure. Today that back-pressure is absorbed *downstream* of sample collection: the deferred
task keeps reading the ADC on time and pushes into the pool; the encoder drains at its own pace and
drops **bytes** (counted) rather than **samples** (timing).

### 2.3 The split decouples "sample timing" from "output back-pressure"

**V** `streaming.c` back-pressure handling: the encoder's `Streaming_WriteWithRetry` uses
`vTaskDelay(1)` (not `taskYIELD()`) in its retry loop so the **lower-priority SD task actually gets
CPU** to drain its circular buffer (#312, `streaming.c:1078-1081`). This retry-with-sleep pattern is
only safe **because it runs in a task that is not on the sample-collection critical path.** If the
same task also owned the ADC read, a `vTaskDelay(1)` inside output retry would stall sampling for a
whole RTOS tick (1 ms = up to 15 missed ticks at 15 kHz).

**V** The stats architecture is built on this separation: `QueueDroppedSamples` (pool exhausted =
encoder too slow) is a **distinct, separately-counted** failure mode from `UsbDroppedBytes` /
`SdDroppedBytes` / `WifiDroppedBytes` (transport too slow). CLAUDE.md "Distinguishing failure modes":
the whole diagnostic model — *is the timer rate-limited, or is the encoder behind, or is the
transport behind?* — depends on those being observable at a task boundary. Merging collapses the two
sides into one task and destroys that observability.

---

## 3. The overhead the merge would actually save

**Claim in the ticket:** "~10-20 µs each context switch … ~200 µs/s at 16ch@5kHz … 8-12% of
per-sample budget."

Let's bound this honestly.

**V/I** A FreeRTOS context switch on PIC32MZ (252 MHz, #487; the tables were measured at 200 MHz) is
the `portSAVE_CONTEXT`/`portRESTORE_CONTEXT` of the MIPS integer register file plus scheduler
bookkeeping. For an **integer-only** switch this is on the order of **1-3 µs** (not 10-20 µs). The
10-20 µs figure in the ticket is not sourced and **I** believe it conflates *context-switch cost*
with *encoder wake-to-run latency* (which includes waiting behind higher-priority tasks and is
genuinely tens of µs — but that latency is **not** removed by merging; the work still has to run).

**E** `docs/PIPELINE_TIMING.md` measures the encoder **wake** jitter (probe 7) at ~1 µs mean at
1 kHz and the encoder inter-arrival at probe 9. Nothing in the measured record shows a per-sample
10-20 µs *switch* cost; the large numbers (36-479 µs at probe 8) are **output work + back-pressure**,
which merging does not eliminate.

**I** Realistic saving from the merge: **removing one task activation per tick** — i.e. one
notification + one integer context switch, ≈ **1-3 µs/tick**. At 5 kHz that is ≈ **5-15 ms/s of CPU**
(0.5-1.5% of one core-second), *not* the ~200 µs/s the ticket estimates for a single direction and
*not* an 8-12% ceiling lift. And that saving is **partly cancelled** by the new FPU save/restore now
required on the hot path (§2.1): a 256-byte FPU context save+restore per hot-path switch adds cost
back exactly where we removed it.

**I** Net: the merge trades a ~1-3 µs/tick integer-switch saving for (a) an FPU-save penalty on the
determinism-critical path, (b) loss of sample-vs-byte drop observability, and (c) coupling ADC-read
timing to output back-pressure. The upside is small and the downside is architectural.

---

## 4. Trade-off summary

| Dimension | Two-task (today) | Merged single task |
|---|---|---|
| Context switches / tick | 2 activations (ISR→collect→encode) | 1 activation (ISR→combined) |
| CPU saved by merge | — | **~1-3 µs/tick (I)**, partly cancelled by new FPU save |
| FPU on hot ISR-deferral path | **No** (#369) | **Yes** — 32×64-bit save/restore per hot switch |
| Sample-collection determinism | pri 9, decoupled from output | encode/output back-pressure now blocks next ADC read |
| Drop observability | `QueueDropped` (encoder) vs `*DroppedBytes` (transport) — separable | collapsed into one counter |
| `vTaskDelay(1)` output-retry (#312) | safe (off the sample path) | stalls sampling a full RTOS tick |
| Refactor risk | — | Major: rewrites the core streaming loop, pool lifecycle, stats |

---

## 5. Recommendation

**Keep the two tasks separate.** The split is deliberate and each half of it (pure-integer #369,
priority-9 determinism, back-pressure decoupling) is independently documented and load-bearing. The
merge's measured/inferred upside (~1-3 µs/tick) is small, is partly cancelled by the FPU-save it
forces onto the hot path, and it destroys the sample-vs-byte drop diagnostic model. **This is a
negative-expected-value refactor.**

### If throughput headroom is the real goal, prefer these lower-risk paths (in order):

1. **Batch pop + encode (the ticket's own "Alternative 3").** **I** This is the only proposal here
   with a favorable risk/reward. Keep both tasks; have `streaming_Task` drain *all* currently-available
   samples from the pool in one wake and encode them into one buffer fill (multiple CSV rows / PB
   messages), then one output write. This amortizes the **encoder-side** per-wake overhead (notify,
   flag checks, mutex acquire/release, USB CDC write-call count) across N samples **without** touching
   the deferred task, the FPU boundary, or the priority map. It stays inside the existing two-task
   pipeline and can be done one encoder at a time. This is where the ~8-12% the ticket wants most
   plausibly lives — in fewer, larger output writes, not in cutting a 1-3 µs switch.

2. **Do nothing on priority.** The ticket's "raise encoder to priority 6" alternative is a **no-op**:
   the encoder is **already** priority 6 (**V** `streaming.c:2467`). That alternative was written
   against the stale pri-2 assumption and is already satisfied.

3. **Leave the split intact.** Any future re-fit of the transport caps (#524 F3 model) or the WiFi
   AIMD work (#523) will move the ceilings more than shaving a context switch would.

### Follow-up hygiene (out of scope for #252, worth a chore ticket):
- Delete or correct the stale `#define STREAMING_ISR_TASK_PRIORITY 8` (`streaming.c:71`) — it does
  not match the literal `9` passed to `xTaskCreate` and misleads exactly the analysis this ticket
  asked for.

---

## Appendix: primary-source index

| Claim | Source |
|---|---|
| Deferred task pri 9, encoder pri 6 (literals) | `streaming.c:2464-2478` |
| Deferred task pure-integer, FPU rationale | `streaming.c:559-566`; CLAUDE.md Task Priority Map |
| Timer-ISR → deferred notify; deferred → encoder notify | `streaming.c:923`, `:916` |
| Sample pool push (not a copying queue) | `streaming.c:805` (`AInSampleList_PushBack`) |
| `vTaskDelay(1)` output-retry rationale (#312) | `streaming.c:1078-1081` |
| Jitter grows down the priority hierarchy | `docs/PIPELINE_TIMING.md` §"Jitter model" |
| Encoder-path jitter/back-pressure excursions | `docs/PIPELINE_TIMING.md` Sessions 2/3 (probes 7/8/9) |
| FPU = 32×64-bit hardware regs, save on registered tasks | CLAUDE.md "Hardware FPU" / "FreeRTOS Configuration" |
| Sample-drop vs byte-drop diagnostic model | CLAUDE.md "Distinguishing failure modes" |
