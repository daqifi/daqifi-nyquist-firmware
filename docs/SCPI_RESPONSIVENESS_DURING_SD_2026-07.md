# SCPI Responsiveness During SD-Card Streaming — Investigation (#170)

Investigation of issue [#170](https://github.com/daqifi/daqifi-nyquist-firmware/issues/170):
*"Evaluate SCPI responsiveness during long SD-card streaming sessions."* The ticket
reports intermittent symptoms during SD streaming — LEDs stop blinking, button presses
ignored, SCPI possibly delayed — and hypothesizes that `app_SdCardTask` consumes enough
CPU to starve SCPI/UI tasks.

This is an **analysis + measurement recipe** (no firmware change). Every load-bearing
claim is tagged **V** (verified from source/datasheet), **E** (empirical — a test we ran),
or **I** (inference/hypothesis, with the experiment that would close the gap), per the
CLAUDE.md debugging-discipline rule.

**Bottom line up front:** By the FreeRTOS priority architecture, the SD writer task
(pri 5) sits **below** both the SCPI-handling USB task (pri 7) and the LED/button
`PowerAndUITask` (pri 7). A priority-5 task **cannot** starve priority-7 tasks on a
preemptive kernel. The ticket's stated mechanism ("SD task starves UI/SCPI") is therefore
architecturally implausible for a *generic* SCPI query (`*IDN?`, `SYST:POW:STAT?`) or for
LED/button service. If the LED/button symptom is real and reproducible, the only tasks
that can preempt pri-7 are the **priority-9 capture tasks** (streaming deferred task + ADC
EOS task), whose CPU load scales with **sample rate**, not with the SD interface — i.e. it
is a streaming-rate/CPU-budget question already governed by the frequency caps, not an SD
problem. A concrete measurement recipe to confirm/deny is in §6.

---

## 1. Task / priority map (V)

From `firmware/src/app_freertos.c` (`app_TasksCreate`, and each task's own body) and the
CLAUDE.md Task Priority Map:

| Pri | Task | Relevant to #170 | Source |
|----:|------|------------------|--------|
| 9 | `_Streaming_Deferred_Interrupt_Task` | Builds each sample per timer tick | streaming.c |
| 9 | `MC12bADC_EosInterruptTask` | ADC end-of-scan servicing | HAL/ADC.c |
| 7 | `app_PowerAndUITask` | **LEDs + button** (the reported symptom) | app_freertos.c:458 |
| 7 | `app_USBDeviceTask` | **USB SCPI** command processing | app_freertos.c:242 |
| 6 | `streaming_Task` | Encoder (PB/CSV/JSON) + output write | streaming.c |
| 5 | `app_SDCardTask` | **SD write path** (the suspected culprit) | app_freertos.c:373 |
| 2 | `app_WifiTask` | WiFi + SCPI-over-TCP | app_freertos.c:256 |

Two subtleties verified in source, because they matter for the argument:

- **`app_USBDeviceTask` is created at priority 2, then boosts itself to 7** after
  `UsbCdc_Initialize()` completes (`vTaskPrioritySet(NULL, 7)`, app_freertos.c:248). Its
  steady-state loop is `UsbCdc_ProcessState(); vTaskDelay(1);` — so SCPI is serviced at
  priority 7 on a ~1 ms poll cadence. (V)
- **`app_PowerAndUITask` runs at priority 7**, loop = `Button_Tasks(); LED_Tasks();
  Power_Tasks(); vTaskDelay(100);` (app_freertos.c:458-469). LEDs/button are serviced
  every ~100 ms at priority 7. (V)

**Consequence (V + I):** On a preemptive kernel, a ready pri-7 task always runs before any
ready pri-5 or pri-6 task. So neither `app_SDCardTask` (5) nor `streaming_Task` (6) can
delay `app_PowerAndUITask` or the USB SCPI task by CPU contention. The only tasks that can
preempt pri 7 are the two pri-9 capture tasks. This is the central structural fact of this
investigation.

---

## 2. Does the SD task hog CPU or hold interrupts off? (V)

`app_SDCardTask` PROCESS loop (app_freertos.c:397-421):

```c
DRV_SDSPI_Tasks(sysObj.drvSDSPI0);
sd_card_manager_ProcessState();
SYS_FS_Tasks();
vTaskDelay(SD_CARD_MANAGER_TASK_DELAY_MS / portTICK_PERIOD_MS);   // = 1 ms
```

- It **yields every cycle** via `vTaskDelay(1)` (`SD_CARD_MANAGER_TASK_DELAY_MS = 1`,
  sd_card_manager.h:32). It is not a tight busy-loop. (V)
- Even without the yield, at pri 5 it is fully preemptible by pri 6/7/9. (V)
- The actual block-write to the card happens synchronously inside `SYS_FS_Tasks()` /
  `DRV_SDSPI_Tasks()` **on this pri-5 task's stack** — but that time is preemptible; it
  does not disable interrupts and does not raise priority. (V)

**Interrupts-disabled (critical) sections in the SD path** are all short — a handful of
32/64-bit stores to publish the space-cache triple and snapshot `minFreeBytes`
(sd_card_manager.c:861-867, 913-919, 1605-1615). Each is O(a few stores) → sub-microsecond
interrupt latency, not a starvation source. (V) No `taskENTER_CRITICAL` in the SD path
wraps a filesystem/SPI operation.

**The encoder→SD hand-off is non-blocking.** The streaming encoder (pri 6) pushes bytes
into the SD circular buffer via `sd_card_manager_WriteToBuffer`, which is non-blocking
(sd_card_manager.c:92 "WriteToBuffer is now non-blocking"). The encoder never blocks
holding a lock that a higher task needs. (V)

---

## 3. Can an SD-related SCPI command block the USB task? (V)

Some SD SCPI commands *do* block the calling (USB, pri 7) task: `LISt?`, `GET`, `DELete`,
`FORmat` enqueue an operation to the SD task and then call
`sd_card_manager_WaitForCompletion()`, which waits on `opCompleteSemaphore`
(SCPIStorageSD.c:292, 631; sd_card_manager.c:1731-1751). During heavy SD write traffic
that completion is serialized behind ongoing writes, so *in principle* these commands
could see added latency.

**But these commands are guarded and rejected during an active SD session.** Each checks
`sd_card_manager_IsBusy()` up front and returns `SCPI_ERROR_EXECUTION_ERROR` immediately
if the SD manager is in any active mode (WRITE/READ/LIST/DELETE/FORMAT)
(SCPIStorageSD.c:124, 159, 209, 263, 598, 671; `IsBusy` at sd_card_manager.c:1838). So
while an SD **stream** is running, a `LISt?`/`GET`/`DELete`/`FORmat` does **not** block the
USB task — it fast-fails with an error the client reads via `SYST:LOG?`. (V)

**Generic SCPI queries** (`*IDN?`, `SYST:POW:STAT?`, `SYST:STR:STATS?`) touch **no** SD
lock and **no** `WaitForCompletion`. They are pure pri-7 work and complete on the USB
task's next ~1 ms poll. (V)

The blocking-on-`opCompleteSemaphore` path is therefore only reachable **when not
streaming** (an idle-time `LISt?`/`GET` of a large file/tree). That is expected behavior
(a directory walk or file read genuinely takes time) and is out of scope for the
"responsiveness during streaming" question. Its worst-case bound is the SCPI-side timeout
(`SCPI_SD_LIST_TIMEOUT_MS` / `SCPI_SD_DELETE_TIMEOUT_MS`), not the 30 s/60 s debug hang
detectors (`SD_DEBUG_MUTEX_TIMEOUT_MS`/`SD_DEBUG_TIMEOUT_MS`), which exist only to log a
`HANG DETECTED` line and are never hit on the normal path. (V)

---

## 4. Shared SCPI response buffer (V)

The shared 2048-byte SCPI response buffer (`SCPI_ResponseBuf_Take/Give`, mutex-guarded) is
taken by a small set of callbacks. On the SD side only `SCPI_StorageSDGetData`
(SCPIStorageSD.c:438) uses it, and — per §3 — `GET` is refused while streaming. So no
SD-path holder of the response-buffer mutex is live during an SD stream; a concurrent
`SYST:SYSInfo?`/`HELP` on the USB task cannot be blocked on it by the SD path. (V) (Cross-
interface note: USB pri-7 and WiFi pri-2 both run SCPI; if both are used they can contend
for this mutex, but that is unrelated to SD and unchanged by streaming.)

---

## 5. Where the reported symptom most plausibly comes from (I)

The LED/button-freeze symptom, **if reproducible**, cannot be the SD task (pri 5) starving
pri-7 (§1). The remaining candidates, in order of likelihood:

1. **Priority-9 capture-task CPU saturation at high sample rate (I — primary hypothesis).**
   At high stream rates the pri-9 deferred task + pri-9 EOS task can consume a large CPU
   fraction; because they sit above pri-7, sustained saturation would visibly delay
   `PowerAndUITask` (LEDs/button) and the USB SCPI poll. This scales with **rate**, not
   with the SD interface — you would see the same symptom on USB or WiFi at the same rate.
   *Closing experiment:* the §6 recipe measures SCPI latency vs rate across interfaces; if
   latency tracks rate identically on USB-only and SD, the cause is pri-9 CPU, not SD.
2. **A card-level write stall (I).** An SPI-mode-incompatible card (A2/SDXC) can make an
   individual block write take seconds. That stalls the **pri-5 SD task** (and the encoder
   waits on a full SD circular buffer), but still does **not** block pri-7 UI/SCPI — LEDs
   would keep blinking. So a card stall explains "SD data loss / stream stops", *not*
   "LEDs stop". The firmware already surfaces this (sd_card_manager.c:1744 advisory,
   `SdDroppedBytes`, the #589 quarantine path). (V for the surfacing; I for it not being
   the LED cause.)
3. **A pre-existing wedge unrelated to priorities (I).** The LED symptom overlaps the
   signature of the historical high-EOS-rate CDC-dead wedge (#525/#557, since fixed) and
   TLBL/stack-overflow wedges (crash-capture globals, #552/#581). If LEDs freeze *and*
   USB CDC dies together, that is a wedge, not scheduling — triage with the `mdb` crash-
   capture globals, not with SD priority tuning.

**Assessment: no responsiveness defect attributable to the SD path is evident in the
source.** The priority design already protects pri-7 SCPI/UI from the pri-5 SD writer, the
SD task yields every 1 ms, its critical sections are trivially short, the encoder→SD
hand-off is non-blocking, and the blocking SD SCPI commands are refused during streaming.

---

## 6. Measurement recipe (daqifi-python-test-suite)

Goal: quantify SCPI round-trip latency for cheap queries **during** an active SD stream,
across sample rates, and compare against USB-only streaming and an idle baseline. If the
SD hypothesis were true, SD-stream latency would exceed USB-stream latency at the same
rate; the structural analysis predicts they are **equal** and both grow only with rate.

Script: `test_170_scpi_sd_latency.py` (added in this ticket; built from `test_harness.py`
primitives — `ReliableSCPI`, `FastReader`). It runs entirely on the **free USB board** and
touches no other bench hardware.

**Per condition:**
1. Baseline (no streaming): issue N=30 `*IDN?` / `SYST:POW:STAT?` queries; record
   min/median/p95/max wall-clock round-trip.
2. Start an SD stream: `SYST:STOR:SD:ENAble 1` → `SYST:STOR:SD:FILE "lat170.csv"` →
   `SYST:STR:INT 2` → `SYST:STR:TEST:PATtern 3` → `SYST:STR:START <freq>`.
3. While streaming, issue the same N timed queries (spaced ~200 ms so each is a discrete
   RTT sample); record the same percentiles. Then `SYST:STR:STOP`.
4. Repeat for `freq ∈ {100, 1000, 5000}` Hz (per the ticket) and for interface **0 (USB-
   only)** as the control, so SD vs USB latency at each rate is directly comparable.

**Acceptance (ticket §Acceptance Criteria):** flag any simple-query median > 500 ms.
Report the SD-vs-USB delta per rate: a near-zero delta confirms the structural finding
(no SD-specific penalty); a large SD-only delta would falsify it and justify a code fix.

**Discipline notes baked into the script:**
- This test **deliberately** violates the CLAUDE.md "no SCPI during a benchmarked run"
  quiescence rule — because measuring in-stream SCPI latency *is* the objective. It
  therefore measures **latency, not throughput**; do not read its numbers as throughput.
- USB delivery is bursty; the script uses `ReliableSCPI.query` (drain-then-write-then-read
  with a deadline) so a single slow poll is captured as latency, not as a hang.
- LED/button responsiveness is a *visual/physical* check the harness cannot make. The
  script prints a prompt for the operator to confirm LEDs keep blinking and the button
  responds during the 5 kHz SD leg (ticket acceptance items 4–5). A future firmware hook
  (e.g. a `SYST:UI:HEARTBEAT?` counter incremented in `LED_Tasks`) would let this be
  automated — noted as a possible follow-up, **not** implemented here.

---

## 7. Recommendation

- **No firmware change in this ticket.** The SD write path does not, by construction,
  starve SCPI or UI. Ship this analysis + the measurement script and run the recipe on the
  bench to convert the §5 inferences (I) into empirical results (E).
- **If** §6 shows in-stream SCPI/UI latency growing with **rate** and equal across USB/SD
  interfaces → the lever is the **pri-9 capture-task CPU budget / streaming frequency cap**
  (already characterized), not the SD task. Open a scoped follow-up against the streaming
  subsystem, not the SD manager.
- **If** §6 shows an SD-only latency penalty absent on USB at the same rate → that would be
  a genuine surprise contradicting §1; capture the offending SCPI command and the SD state
  and file a targeted bug (likely an unexpected `IsBusy`/mutex interaction) with the trace.

No large refactor is warranted on the current evidence.
