# Peripheral Busywork During Streaming — Audit (#331)

**Ticket:** #331 — *suppress WINC1500 / SD / other peripheral busywork during streaming*
**Date:** 2026-07
**Scope:** identify FreeRTOS/driver housekeeping that runs during an *active
stream* and is **not needed** for the active interface, determine whether it is
already gated (cite code), and flag residual avoidable work.

Evidence tags (per `CLAUDE.md` debugging discipline): **V** = verified from
firmware source (file:line), **E** = empirical from a test we ran, **I** =
inference/hypothesis.

---

## TL;DR

The headline target of #331 — the **~22 Hz / ~270 µs periodic preemption of the
streaming-timer notification path** during USB-only streaming with WiFi idle —
is **already fixed** and merged. It was root-caused (E, per the #330/#329 Saleae
captures cited in the ticket) to the `WDRV_WINC_Tasks` driver loop running as an
unbounded tight loop, and resolved by the **dynamic WINC idle-gate** in
`firmware/src/config/default/tasks.c` (**#335**, commit `2e8b853b`,
*"feat(streaming): dynamic WINC idle-gate — resolve 22 Hz jitter preemption
(#331)"*). A `SYST:WINC:GATE?` debug accessor (#55) exposes the live gate tiers.

The remaining candidates the ticket asked to rule in/out are either **already
gated** by later work (#541 dynamic scan CSS; #589/#605/#606 SD detect-poll
backoff; #173/#564 power-status GPIO/ADC path) or are **not on the sample path**
(priority below the encoder/deferred tasks) and therefore cannot reproduce the
#331 signature. No *clearly-safe, build-only* additional suppression was found;
every residual item is either an intentional responsiveness tradeoff or a change
that alters streaming-time peripheral behavior and so requires bench validation
(out of scope for this build-only ticket). Follow-ups are recommended below.

---

## Audit table

| Peripheral / task | Work during an active stream | Already gated by | Residual avoidable work? | Recommendation |
|---|---|---|---|---|
| **WINC1500 driver** — `lWDRV_WINC_Tasks` (pri 1) | `WDRV_WINC_Tasks()` SPI/state-machine poll each loop iteration; pre-#331 was an unbounded 0 ms tight loop = the 22 Hz / 270 µs preemption (E, #330 capture) | **#331/#335** `WincIdleGate_ComputeDelay()` `tasks.c:131`. When `SYS_STATUS_READY` **and** `Streaming_IsActiveOnNonWifiInterface()` **and** `!wifi_tcp_server_HasActiveClient()` → paces `vTaskDelay(50 ms)`; ERROR/UNINIT → 50 ms; else → 0 ms (V, `tasks.c:131-181`) | **Minor / intentional.** Tight 0 ms loop still runs when a TCP control client is connected (even idle), during BUSY/init phases, or when the stream *is* on WiFi. When paced, the 22 Hz signature is eliminated for the common USB/SD-stream + idle-AP case (I) | Keep. Deeper reduction (fully-idle AP) needs WINC chip power-down (#334) or event-queue-empty awareness in the driver; a naive per-loop `vTaskDelay(1)` is documented to assert+reboot in STA throughput (V, `tasks.c:113-118`). Do **not** add a fast-path delay to this loop. |
| **SD card-detect FSM** — `DRV_SDSPI` attach/detach poll | One `MediaCommandDetect` CMD exchange on the **shared SPI4 bus** per poll interval; stock cadence `DRV_SDSPI_POLLING_INTERVAL_MS_IDX0 = 1000` ms (V, `configuration.h:135`) | **#589 P1 / #605 / #606** backoff: after `DRV_SDSPI_DETECT_BACKOFF_AFTER_POLLS = 10` empty polls the interval stretches to `DRV_SDSPI_DETECT_BACKOFF_INTERVAL_MS = 5000` ms (V, `drv_sdspi.c:1712-1726`, `drv_sdspi_local.h:145-146`). `sd_card_manager_UpdateSettings()` calls `DRV_SDSPI_DetectPollKick()` to restore the fast cadence when SD activity is expected (V, `sd_card_manager.c:1706-1711`) | **Minor.** An **attached** card keeps the stock 1 Hz *removal*-detection poll even during USB-only streaming (SD not a stream target) — the counter is pinned to 0 while attached (V, `drv_sdspi.c:1755-1758`). 1 Hz ≠ the 22 Hz #331 signature and is one CMD exchange | Keep. Removal detection is a real requirement; 1 Hz is already low. Suspending the removal poll during non-SD streaming risks missing a hot-removal event → recommend only as a bench-validated follow-up, not a build-only change. |
| **SD manager task** — `app_SDCardTask` (pri 5) | 1 ms loop: `DRV_SDSPI_Tasks` + `sd_card_manager_ProcessState` (IDLE = `break`) + `SYS_FS_Tasks` (V, `app_freertos.c:398-424`, `sd_card_manager.c:1636-1638`) | Power-state gate + **SUSPENDED** state when WiFi owns SPI (`app_SDCard_IsWifiUsingSPI()`, V `app_freertos.c:352-362, 427`); #589 SPI-quarantine | **Minor.** During USB-only streaming with SD idle the task still spins at 1 ms, but at **pri 5 < encoder (6) < deferred (9)** it cannot preempt the sample path, so it is not a jitter source (I) | Keep. Low value; changing the loop cadence risks SD responsiveness for negligible CPU saving. |
| **ADC MODULE7 monitoring scan** (EOS ISR + monitoring channels) | Per-scan EOS interrupt + monitoring-channel reads add scan length and EOS-task wakeups | **#541** dynamic scan CSS: monitoring channels are in `ADCCSS1/2` **only when `OBDiag = 1`**; the session scan list is rebuilt at each stream start (V, per `CLAUDE.md` ADC Architecture §"Dynamic scan list (#541 D-B)"; `MC12b_ComputeScanList`) | **None for `OBDiag = 0`.** With OBDiag off, monitoring channels are dropped from the streaming scan entirely | Done. Honest physics: `OBDiag = 1` visibly lowers `CONF:CAP` because monitoring rides the same scan. |
| **USB device stack** — `F_USB_DEVICE_Tasks` (boosts self to pri 6), `F_DRV_USBHS_Tasks` (pri 1, 10 ms) | `USB_DEVICE_Tasks` each 1 ms; `DRV_USBHS_Tasks` each 10 ms (V, `tasks.c:79-91, 183-191`) | N/A — this **is** the USB CDC transport/control channel | Not busywork when USB is the transport. During WiFi/SD-only streaming it still services the CDC control channel; 100 Hz, wrong frequency for the 22 Hz signature (I) | Keep. Ruled out as the #331 source (frequency mismatch). |
| **Power / BQ24297** — `Power_Tasks()` (pri 7, ~100 ms) | IINLIM state machine each call; a single I2C read of REG08 (VSYS status, #564) at ~100 ms when VBUS present; otherwise GPIO/ADC only, **no I2C** (V, `PowerApi.c:178-197, 390-432`) | **#173 / #564**: the old ~70 reads/s I2C polling was replaced by GPIO/ADC status (V, `PowerApi.c:390-401`); I2C is now one REG08 read | **Minor.** ~10 Hz, and pri 7 > encoder (6) so a 100 ms I2C read *can* add a small stall — but 10 Hz ≠ 22 Hz and the read is required for low-battery (VSYS) detection | Keep. Already minimized to one register read; ruled out as the #331 source. |
| **FreeRTOS tick** | 1 kHz systick | Intrinsic | No | N/A — 1 kHz, not 22 Hz. |

---

## Why the #331 signature is specifically the WINC loop

The #330 capture (E, cited in #331) showed 44 outliers over 2 s at
**45.88 ms ± 0.57 ms spacing (CV 1.2 %)** — a fixed ~22 Hz scheduled
disturbance, not random contention. Two facts make `WDRV_WINC_Tasks` the
unique culprit (V + I):

1. It was the only always-on task running an **unbounded 0 ms loop** (every
   other housekeeping loop already had a `vTaskDelay`), so it monopolized CPU
   in bursts whenever the WINC driver's internal state machine had periodic
   work (its ~50 ms housekeeping cadence maps to the observed 22 Hz) (I).
2. Bisection attributed the signature to this task, and gating it to 50 ms in
   the safe case removed it — recorded in the `tasks.c:107-130` comment and the
   #335 PR (N, our own PR note; the underlying capture is E).

The gate is deliberately conservative because the WINC driver relies on
back-to-back calls during event-heavy phases: a blanket `vTaskDelay(1)` on the
fast path asserted and rebooted the firmware during STA-mode throughput testing
(V, `tasks.c:113-118`). Hence the three-tier policy that only paces when WiFi is
provably off the data path.

---

## Recommendations / follow-ups

1. **(no code change) Mark #331 as resolved-by-#335** for its primary target.
   This document is the audit deliverable; the fix shipped in #335/v-with the
   idle-gate and is observable via `SYST:WINC:GATE?` (#55).
2. **WINC fully-idle-AP power-down (#334, follow-up):** the only path to
   eliminating the residual 0 ms tight loop when an idle TCP client is connected
   is chip power-down or driver event-queue-empty awareness — both need bench
   validation and are explicitly out of scope for a build-only change.
3. **SD removal-poll suppression during non-SD streaming (follow-up, bench-gated):**
   could pause the 1 Hz attached-card removal poll while streaming to a non-SD
   interface, but only with a hardware test proving hot-removal is still detected
   at stream stop. Low priority (1 Hz, single CMD exchange).
4. **No build-only suppression implemented** here: every residual item is either
   an intentional responsiveness tradeoff or alters streaming-time peripheral
   behavior and therefore must be hardware-validated, which this ticket's
   build-only / no-bench constraint does not permit.

---

## Source references (all V unless noted)

- `firmware/src/config/default/tasks.c:107-181` — WINC idle-gate (#331/#335, #55)
- `firmware/src/config/default/WincIdleGate.h` — public gate API (#55)
- `firmware/src/services/streaming.c:2520-2532` — `Streaming_IsActiveOnNonWifiInterface()`
- `firmware/src/services/wifi_services/wifi_tcp_server.c:508` — `wifi_tcp_server_HasActiveClient()`
- `firmware/src/services/SCPI/SCPIInterface.c:4465` — `SYST:WINC:GATE?` debug accessor
- `firmware/src/config/default/driver/sdspi/src/drv_sdspi.c:1712-1758` — SD detect-poll backoff (#589/#605/#606)
- `firmware/src/config/default/driver/sdspi/src/drv_sdspi_local.h:145-146` — backoff constants
- `firmware/src/config/default/configuration.h:135` — stock 1000 ms SD poll interval
- `firmware/src/services/sd_card_services/sd_card_manager.c:1706-1711, 1636-1638` — `DetectPollKick`, IDLE state
- `firmware/src/app_freertos.c:349-456` — `app_SDCardTask` power/WiFi-SPI gating
- `firmware/src/HAL/Power/PowerApi.c:178-197, 390-432` — Power_Tasks GPIO/ADC path (#173/#564)
- ADC monitoring scan gating: `CLAUDE.md` §"ADC Architecture" / #541 dynamic scan CSS
