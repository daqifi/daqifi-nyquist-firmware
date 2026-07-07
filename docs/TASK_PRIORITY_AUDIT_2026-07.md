# RTOS Task Priority Audit — 2026-07 (#493)

Ground-truth audit of every FreeRTOS task created in `firmware/src`
(third-party excluded), compared against CLAUDE.md's "Task Priority
Map". Structural review only — **no functional code change**. Claims
tagged **V** (verified from source), **E** (empirical/measured), **I**
(inference) per the CLAUDE.md debugging-discipline rule.

Verified from firmware HEAD on branch `docs/493-priority-audit`
(worktree of `feat/589-sick-sd-tier123`), 2026-07-06. All file:line
references below are **V** — read directly from the current tree.

## 1. Ground-truth task table

Every `xTaskCreate` / `xTaskCreateStatic` in `firmware/src` (excluding
`third_party/`). Priority column shows the value passed to the create
call; where a task changes its own priority at runtime, both are shown.

| Pri (create → runtime) | Task string | Function | Stack (words) | FPU? | Source (file:line) |
|---|---|---|---:|:--:|---|
| 9 | `Stream Interrupt` | `_Streaming_Deferred_Interrupt_Task` | 512 | no | `services/streaming.c:2474` |
| 9 | `MC12bADC EOS` | `MC12bADC_EosInterruptTask` | 256 | no | `HAL/ADC.c:195` |
| 9 | `AD7609 BSY` | `AD7609_DeferredInterruptTask` | 1024 | no | `config/default/tasks.c:281` |
| 7 | `PowerAndUITask` | `app_PowerAndUITask` | 640 | **yes** | `app_freertos.c:745` |
| 2 → **6** | `USB_DEVICE_TASKS` | `F_USB_DEVICE_Tasks` | 144 | no | `config/default/tasks.c:305` (boost at `tasks.c:85`) |
| 6 | `Stream task` | `streaming_Task` | 1392 | **yes** | `services/streaming.c:2465` |
| 5 | `SDCardTask` | `app_SDCardTask` | 1024 | no | `app_freertos.c:781` |
| 5 | `Iperf2` | `Iperf2TaskMain` | 512 (static) | no | `services/wifi_services/iperf2/iperf2.c:974` |
| 2 | `USBDeviceTask` | `app_USBDeviceTask` | 3072 | **yes** | `app_freertos.c:756` |
| 2 | `WifiTask` | `app_WifiTask` | 1500 | **yes** | `app_freertos.c:767` |
| 2 | `DRV_USBHS_TASKS` | `F_DRV_USBHS_Tasks` | 144 | no | `config/default/tasks.c:316` |
| 2 | `fwUpdateTask` | `fwUpdateTask` | 1024 | no | `services/wifi_services/wifi_manager.c:2172` |
| 1 | `WDRV_WINC_Tasks` | `lWDRV_WINC_Tasks` | 1024 | no | `config/default/tasks.c:264` (prio macro `configuration.h:182` = 1) |
| 1 | `APP_FREERTOS_Tasks` | `lAPP_FREERTOS_Tasks` | 1500 | no | `config/default/tasks.c:332` |
| 1 | `logISR` | `LogIsrDrainTask` | 256 | no | `Util/Logger.c:618` (`LOG_ISR_TASK_PRIO` = 1, `Logger.c:589`) |

Notes:
- `F_USB_DEVICE_Tasks` is created at priority 2 and immediately raises
  itself to 6 with `vTaskPrioritySet(NULL, 6)` on its first loop
  iteration (`tasks.c:83-85`). A static grep of `xTaskCreate` sees pri
  2 and misses this. **V**
- `WDRV_WINC_Tasks` priority is supplied by the macro
  `DRV_WIFI_WINC_RTOS_TASK_PRIORITY`, which `configuration.h:182`
  currently defines as **1** (PR #492 dropped it 2→1). **V**
- `fwUpdateTask` and the WINC/USB Harmony tasks are dynamic/boot-time
  and may not all be resident simultaneously.

## 2. Discrepancies vs CLAUDE.md Task Priority Map

The `CONFIG` value read from source is authoritative. All rows below
are **V** (source vs doc text).

### 2a. Priority discrepancies

| Task | CLAUDE.md said | Reality (source) | Impact |
|---|---|---|---|
| `app_USBDeviceTask` | pri **7** | pri **2** | **Highest impact.** Off by 5 levels. Invalidates the "USB SCPI at 7 stays responsive above streaming (6)" mental model — USB SCPI is *below* the encoder. |
| `F_USB_DEVICE_Tasks` | pri **1** | created 2, **runtime 6** | Off by 5 at runtime. Doc missed the self-boost; the task actually preempts SD (5) and ties streaming (6). |
| `F_DRV_USBHS_Tasks` | pri **1** | pri **2** | Off by 1. |

`lWDRV_WINC_Tasks` pri 1 in the doc now matches source (PR #492) — no
longer a discrepancy.

### 2b. Stack-size discrepancies

| Task | CLAUDE.md said | Reality (source) | Note |
|---|---:|---:|---|
| `MC12bADC_EosInterruptTask` | 160 | **256** | Raised in #525 vsnprintf-margin follow-up. |
| `AD7609_DeferredInterruptTask` | 160 | **1024** | Raised for the BSY-stuck `LOG_E`/vsnprintf frame (#525 follow-up). |
| `app_PowerAndUITask` | 512 | **640** | Comment: 226 + 64 FPU peak, 2× → 640. |
| `fwUpdateTask` | 128 | **1024** | Code comment: "Keep original — FW flash path not fully profiled." |

### 2c. Tasks missing entirely from CLAUDE.md

| Task | Pri | Stack | Source |
|---|---:|---:|---|
| `Iperf2TaskMain` (`"Iperf2"`) | 5 | 512 static | `iperf2.c:974` |
| `LogIsrDrainTask` (`"logISR"`) | 1 | 256 | `Logger.c:618` |

All discrepancies in 2a/2b/2c are **fixed in this PR's CLAUDE.md edit**
(map values corrected + the two missing tasks added). No code changed.

## 3. FPU registration audit

Every `portTASK_USES_FLOATING_POINT()` call in `firmware/src`
(grep, **V**):

| Call site | Enclosing task function |
|---|---|
| `app_freertos.c:244` | `app_USBDeviceTask` |
| `app_freertos.c:258` | `app_WifiTask` |
| `app_freertos.c:462` | `app_PowerAndUITask` |
| `services/streaming.c:1980` | `streaming_Task` |

- CLAUDE.md's registered-FPU list is `app_USBDeviceTask`,
  `app_WifiTask`, `app_PowerAndUITask`, `streaming_Task` — **matches
  source exactly (4/4). V** No FPU discrepancy.
- CLAUDE.md's pure-integer list (`_Streaming_Deferred_Interrupt_Task`,
  `app_SDCardTask`, `lWDRV_WINC_Tasks`) — none of the three call
  `portTASK_USES_FLOATING_POINT()`. **Confirmed V.**
- The two previously-undocumented tasks (`Iperf2TaskMain`,
  `LogIsrDrainTask`) and the ADC deferred tasks
  (`MC12bADC_EosInterruptTask`, `AD7609_DeferredInterruptTask`) are all
  **correctly pure-integer** (no FPU call). **V.** Any future edit that
  adds a `double`/`float` read to these bodies would silently corrupt
  FPU state (see the #369 note in CLAUDE.md) — flagged as a latent trap,
  not a current bug.

## 4. Structural hazards (real only)

### H1 — CLAUDE.md's SCPI-responsiveness rationale is false. (V, load-bearing)

CLAUDE.md's "Scheduling implications" says the encoder "stays below USB
(7) so SCPI commands remain responsive during streaming." **Source:
`app_USBDeviceTask` is pri 2, not 7** (`app_freertos.c:756`). During
streaming, `streaming_Task` (6) and the self-boosted `F_USB_DEVICE_Tasks`
(6) both preempt the USB SCPI host (2). USB SCPI only runs when the
encoder blocks. This is a **documentation hazard** — prior priority
decisions justified by the false premise should be re-examined (the
issue #493 body raises this directly). No runtime bug is asserted here;
the risk is future design reasoning built on the wrong map. The fix is
the corrected map in this PR. **V.**

### H2 — Heavily contended pri-2 tier. (V mechanism / I on consequence)

Four tasks sit at pri 2: `app_USBDeviceTask`, `app_WifiTask`,
`F_DRV_USBHS_Tasks`, `fwUpdateTask` (when resident). Two of them are
SCPI hosts (USB + TCP). With 1 ms tick time-slicing, they round-robin
whenever ready. **V** that they share the level. **I** (not measured
here) that this is where SCPI latency-under-load lives; the deferred
`F_USB_DEVICE_Tasks` at pri 6 further starves the pri-2 USB app task
during heavy streaming. Experiment to close the gap: measure USB-SCPI
round-trip latency during a saturated stream (bench, main-loop job) —
this audit does not touch hardware.

### H3 — BQ24297 I2C mutex spans a real priority gap. (V)

CLAUDE.md's BQ24297 section states the I2C mutex synchronizes
`PowerAndUITask` and `USBDeviceTask` "(both priority 7)". Source:
`PowerAndUITask` = 7, `USBDeviceTask` = **2** (`app_freertos.c:745`
vs `:756`). So the mutex is actually shared across a **5-level
priority gap**, not between equals. FreeRTOS mutexes have priority
inheritance (`configUSE_MUTEXES=1`), so inversion is *bounded* — the
low-pri (2) USB task holding the I2C mutex transiently inherits 7 until
it releases. Not a live bug, but: (a) the doc's "both priority 7"
premise is wrong, and (b) any switch of that mutex to a non-inheriting
primitive (e.g. a binary semaphore) would expose unbounded inversion.
Flagged **V**; the wrong "both priority 7" text lives outside the
Task Priority Map, so per the #493 scope (map values only) it is
recorded here rather than edited.

### H4 — Runtime priority boost is invisible to static tooling. (V)

`F_USB_DEVICE_Tasks` self-boosts 2→6 at runtime. Any audit/tool that
reads only the `xTaskCreate` argument (as the pre-fix CLAUDE.md map
did) records the wrong effective priority. The corrected map now notes
the boost explicitly. **V.**

### Non-hazards checked and cleared

- **Task priority vs `configMAX_SYSCALL_INTERRUPT_PRIORITY`:** these
  are orthogonal axes. `configMAX_SYSCALL_INTERRUPT_PRIORITY = 4` is a
  hardware *interrupt* priority; task priorities are 0–9
  (`configMAX_PRIORITIES = 10`). No task priority can collide with an
  ISR priority — different numbering domains. All hardware ISRs defer
  to tasks via `xTaskNotifyGive` (CLAUDE.md, confirmed). No hazard. **V.**
- **SCPI shared-response-buffer mutex** (USB task 2 ↔ WiFi task 2):
  genuinely equal priority, so no inversion; worst case is round-robin
  serialization of two large-buffer SCPI callbacks. Bounded by the
  2048-byte static buffer + static mutex design. Low risk. **I.**
- **SPI4 mutex** (SD task 5 ↔ WINC task 1): 4-level gap, but priority
  inheritance bounds it; this is the intended #492 arrangement
  (streaming preempts WINC, OSAL semaphores yield). No hazard. **V.**

## 5. Summary

- 3 priority errors, 4 stack errors, 2 missing tasks in the CLAUDE.md
  map — **all corrected** in this PR (map only, values + 2 added rows,
  no format restructure).
- FPU registration is **correct** in both source and doc (4/4).
- Highest-impact finding: `app_USBDeviceTask` is pri **2**, not 7 —
  the SCPI-responsiveness rationale in CLAUDE.md was built on a false
  premise (H1). One out-of-map wrong statement (BQ "both priority 7",
  H3) is recorded here for a follow-up doc pass, left unedited to
  respect the #493 map-only scope.
- No functional code change; no build or bench required.
