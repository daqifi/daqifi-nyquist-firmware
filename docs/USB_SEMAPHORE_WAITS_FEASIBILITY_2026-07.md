# USB CDC Semaphore-Based Waits — Feasibility Assessment (2026-07)

**Issue:** #185 — replace USB CDC polling loops with semaphore-based waiting.
**Status:** Assessment only. **No firmware behavior change is recommended in this pass.**
**Scope:** `firmware/src/services/UsbCdc/UsbCdc.c` / `.h`.

Evidence tags follow the project debugging-discipline convention: **V** = verified from
primary source (code/datasheet), **E** = empirical test we ran, **I** = inference/hypothesis,
**N** = our own prior notes (code comments, tickets — *not* external authority).

---

## 1. TL;DR / Recommendation

The USB CDC write path uses several `vTaskDelay()` polling loops to wait for a DMA
transfer's `WRITE_COMPLETE` event to clear `writeTransferHandle`. Replacing them with an
event-driven wait *could* cut multi-write SCPI command/response latency, but:

- The USB task timing is a repeat source of hard wedges (**#525** unbounded-wait wedge,
  **#347** SCPI-response hang). A wait primitive that can miss its wake = an indefinite hang.
- A **binary semaphore was already tried and reverted** — it hangs on multi-write responses
  (`SYST:INFo?`) because of a give-before-take race (N, #185 ticket body).
- Streaming throughput is **not** bottlenecked by these loops (V, `CLAUDE.md` — the streaming
  hot path uses the circular buffer + the PROCESS-loop `BeginWrite`, not `WaitForWrite`). The
  only thing being optimized is *SCPI command-response latency for multi-write responses*.

**Recommendation (cautious):**

1. **Do NOT use a binary semaphore.** It cannot safely represent a completion that may fire
   before the waiter blocks (this is exactly the reverted attempt).
2. **If** we optimize, use a **FreeRTOS direct-to-task notification** (`ulTaskNotifyTake` /
   `vTaskNotifyGiveFromISR`) **with a bounded timeout that re-checks `writeTransferHandle` as
   the source of truth** — a belt-and-suspenders design where a lost/spurious signal degrades
   to today's polling behavior, never to a hang. This costs **zero new static/heap RAM** (task
   notifications live in the TCB's built-in notification array).
3. **Never remove the timeout / `writeStalled` latch** — that latch is the #525 fix; an
   unbounded wait here previously starved the whole USB task (both endpoints) and hard-wedged
   the device (N, `WaitForWrite` comment lines 551-557).
4. **Verify the ISR-vs-task context of `WRITE_COMPLETE` first** (see §4) — the entire benefit
   depends on it, and the ticket's premise and the code comments currently disagree.
5. **Lowest-risk near-term option** if latency is a real complaint: reduce the poll granularity
   (`WaitForWrite`'s `vTaskDelay(5)` → `vTaskDelay(1)`), a one-line change bounded by the same
   timeout, no new primitive, no new race surface. This is *not* a semaphore but delivers most
   of the achievable win because the USB device task itself only advances on a 1 ms tick.

Any conversion is a **bench-validated change**, not a doc-only merge, and must soak against the
#525 host-stall repro and the `SYST:INFo?` multi-write response before it ships.

---

## 2. The polling loops (inventory)

All are in `UsbCdc.c`. "Signal" = the event that would replace the poll.

| # | Function | Loop | Bound | Signal | On streaming hot path? |
|---|----------|------|-------|--------|------------------------|
| 1 | `UsbCdc_WaitForWrite` (≈547) | `while (writeTransferHandle != INVALID) vTaskDelay(5)` | `USBCDC_WRITE_STALL_MS` = 1000 ms + `writeStalled` latch | `WRITE_COMPLETE` → `FinalizeWrite` clears handle | No — only via `UsbCdc_Flush` (SCPI response flush) |
| 2 | `UsbCdc_FlushWriteBuffer` (≈990) | nested `while (writeTransferHandle != INVALID) vTaskDelay(10)` inside a chunk loop | 500 ms shared deadline + `writeStalled` latch | same as #1 | No — SCPI-response / explicit flush |
| 3 | `UsbCdc_ResizeWriteBuffer` (≈1357) | `while (writeTransferHandle != INVALID) vTaskDelay(1)` | 1000 ms | same as #1 | No — runs once at `StartStreamData` (control plane) |
| 4 | `UsbCdc_WaitForRead` (≈654) | `while (readTransferHandle != INVALID) vTaskDelay(100)` | **UNBOUNDED (no timeout)** | `READ_COMPLETE` clears handle | **Dead code** — `UNUSED(UsbCdc_WaitForRead)` at line ≈1242 |

**Not in scope (fixed-time settle delays, not completion polls):** the `vTaskDelay(100)` VBUS
attach settle (≈314) and the `vTaskDelay(50)` VBUS-sag discharge re-check (≈339). These wait on
an analog RC filter, not on a firmware callback — there is no event to signal them, so a
semaphore is inapplicable. Leave them.

**Also not a busy-wait:** the `USB_CDC_STATE_PROCESS` state machine (≈1265) driven by
`app_USBDeviceTask`'s `while(1){ UsbCdc_ProcessState(); vTaskDelay(1); }` (V, `app_freertos.c`
250-253). This 1 ms cadence is the *fundamental latency floor* — even a perfect event-wait in
loop #1 cannot get a response out faster than the PROCESS loop re-drives `BeginWrite`.

---

## 3. The event chain (who would give the semaphore)

```
USB hardware endpoint completion
   → DRV_USBHS interrupt
      → USB_DEVICE_Tasks dispatch  (called from F_USB_DEVICE_Tasks, priority 1, vTaskDelay(1))
         → UsbCdc_CDCEventHandler(USB_DEVICE_CDC_EVENT_WRITE_COMPLETE)   [UsbCdc.c ≈230]
            → UsbCdc_FinalizeWrite()  → writeTransferHandle = INVALID    [UsbCdc.c ≈573]
```

The waiter is `app_USBDeviceTask` (priority 7, V `CLAUDE.md` task map). The clearer of the
handle is `FinalizeWrite`, reached from the CDC event handler. **The signalling primitive would
be given in `FinalizeWrite` and taken in the wait loop.**

---

## 4. The load-bearing unknown: ISR vs task context of `WRITE_COMPLETE`

This determines whether an event-driven wait is even worth doing, and which API to use.

- **Code says ISR context (N):** `FinalizeWrite` calls
  `Streaming_AddProfileSample_DmaPending_FromISR(...)` and its comment reads *"This runs in ISR
  context (WRITE_COMPLETE event)"* (UsbCdc.c ≈574-582); the `writeStalled` field doc says it is
  *"Cleared in UsbCdc_FinalizeWrite (WRITE_COMPLETE ISR)"* (UsbCdc.h ≈88). If true, the give
  must be `vTaskNotifyGiveFromISR` + `portYIELD_FROM_ISR`, and an event wait genuinely saves the
  full poll delay (a task can be unblocked directly from the completion ISR).

- **Ticket says task/polled context (N, #185 body):** *"Harmony's architecture uses polling for
  the USB device task — no ISR-to-task notification hook exists … `F_USB_DEVICE_Tasks` (1 ms
  poll) → dispatches CDC events."* If the event handler actually runs from `USB_DEVICE_Tasks`
  (task context, on `F_USB_DEVICE_Tasks`'s own 1 ms `vTaskDelay`), then there is **no true
  hardware event to ride** — the completion is only *discovered* on the next poll, and an
  event-driven wait can save at most ~1 ms vs simply reducing loop #1's delay to `vTaskDelay(1)`.

These two claims contradict each other and **both are N-class** (our own comments / ticket text,
not verified against the Harmony driver config). **Before any implementation, verify (V) whether
`DRV_USBHS` delivers CDC transfer callbacks from the USB ISR or from the `USB_DEVICE_Tasks`
poll** (inspect the driver's `interruptSource` / `USB_DEVICE_INIT` config in
`firmware/src/config/default/`, and/or set a probe/breakpoint in `FinalizeWrite` and read
`uxInterruptNesting`). The feasibility verdict flips on this:

- **ISR-driven → event wait is worthwhile** (task-notification design, §5).
- **Poll-driven → event wait buys ~nothing**; just reduce the poll delay (option in §1.5).

---

## 5. Proposed design (only if §4 resolves to ISR-driven)

**Primitive: direct-to-task notification, not a semaphore.**

Why not a binary semaphore: a binary semaphore holds a single token with no memory of "how many
completions happened." If `FinalizeWrite` gives before the waiter reaches its take, the token
can be consumed by the *wrong* iteration (a prior/next write), and the intended waiter blocks
forever. **This is precisely the reverted attempt** (N, #185: *"Binary semaphore race — when DMA
completes very fast … the Give is consumed by the wrong iteration. The next write's Take blocks
indefinitely."*).

A **task notification** avoids this because:

- The notification value is **latched in the waiter's TCB**: a give-before-take is *not lost* —
  the next `ulTaskNotifyTake` returns immediately (I — standard FreeRTOS semantics; confirm in
  our port).
- It is **addressed to a specific task**, so it cannot be "stolen" by an unrelated context.
- It uses the **built-in notification array** in the TCB — **zero new static or heap bytes**
  (important: firmware RAM headroom is ~500 B above the min-stack; a binary/counting semaphore
  would cost ~80 B heap, a task notification costs nothing).

**Sketch (illustrative, not final):**

```c
// waiter (UsbCdc_WaitForWrite), replacing the vTaskDelay(5) spin:
xTaskNotifyStateClear(NULL);                 // discard any stale notification from a prior cycle
while (client->writeTransferHandle != USB_DEVICE_CDC_TRANSFER_HANDLE_INVALID) {
    if (client->state != USB_CDC_STATE_PROCESS) return false;
    if (client->writeStalled) return false;                       // #525 latch preserved
    // Block up to a short slice, woken early by the completion give.
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(USBCDC_WRITE_STALL_MS));
    // Re-check the HANDLE (source of truth) — a lost/spurious wake just re-loops or times out.
    if ((xTaskGetTickCount() - start) > pdMS_TO_TICKS(USBCDC_WRITE_STALL_MS)) {
        client->writeStalled = true;                             // same timeout/latch path
        LOG_E_ONCE(...);
        return false;
    }
}

// giver (UsbCdc_FinalizeWrite), ISR context:
BaseType_t hpw = pdFALSE;
vTaskNotifyGiveFromISR(gUsbTaskHandle, &hpw);   // gUsbTaskHandle captured once at task start
portYIELD_FROM_ISR(hpw);
```

Key safety properties of this sketch:

1. **`writeTransferHandle` stays the source of truth.** The notification only *wakes* the waiter
   early; correctness is decided by re-reading the handle. A missed give → the timeout still
   fires → identical to today's behavior. **No new hang path** is introduced.
2. **The `writeStalled` / timeout branch is untouched** — the #525 host-stall fix survives.
3. **`xTaskNotifyStateClear` at entry** prevents a stale notification from a previous write from
   pre-satisfying the current wait (the counting-analog of the binary-semaphore race).
4. **`gUsbTaskHandle`** (one `TaskHandle_t`, 4 B) must be captured in `app_USBDeviceTask` after
   `xTaskCreate` (or via `xTaskGetCurrentTaskHandle()` cached at task start). It is 4 B of BSS —
   within budget.

**Caveat — notifications are a shared, single-slot channel per task.** `app_USBDeviceTask` must
not be using its default notification for anything else, or the write-complete give and that
other use will clobber each other. Audit `app_USBDeviceTask` for any existing
`ulTaskNotifyTake`/`xTaskNotify` use before adopting this (I — needs a grep + review; the
streaming deferred task already uses notifications, but that's a different task).

---

## 6. Deadlock / stall / regression risks

| Risk | Class | Mitigation |
|------|-------|------------|
| Signal never given (host stops draining IN endpoint) → indefinite hang. This is the **#525** failure mode: the original *unbounded* `WaitForWrite` starved the whole USB task, including the OUT/SCPI endpoint, and hard-wedged the device. | V (#525 history in `WaitForWrite` comments, `CLAUDE.md` #525 notes) | **Never** take with `portMAX_DELAY`. Keep the `USBCDC_WRITE_STALL_MS` timeout and the `writeStalled` latch. The notification is an *optimization on top of* the bounded poll, not a replacement for the bound. |
| Give-before-take race → waiter blocks forever (the **reverted binary-semaphore** bug). | E/N (#185: observed hang on `SYST:INFo?`) | Use task notification (latched) + `xTaskNotifyStateClear` at entry + handle-as-truth. Do not use a binary semaphore. |
| ISR-context give uses task API by mistake (`xSemaphoreGive` instead of `...FromISR`) → corruption/assert. Related class: the USB path is already assert-sensitive (`configASSERT` / `taskENTER_CRITICAL` from ISR wedge, N project memory). | I | If §4 confirms ISR context, use `vTaskNotifyGiveFromISR` + `portYIELD_FROM_ISR` only. If task context, no FromISR needed — but then the whole change is low-value. |
| Shared notification slot collision with another use of `app_USBDeviceTask`'s notification. | I | Audit before adopting (§5 caveat). If a collision exists, fall back to a **counting semaphore reset per cycle** (costs ~80 B heap — check RAM budget) or keep polling. |
| `FlushWriteBuffer` (loop #2) is a *multi-chunk* loop — each chunk arms a new DMA. Converting it means one give per chunk and a state-clear per chunk; more sequencing surface than loop #1. | I | Convert loop #1 first, soak it, then consider #2. Do not convert both at once. |
| Latency win is smaller than hoped because the PROCESS loop + `F_USB_DEVICE_Tasks` both run on 1 ms `vTaskDelay`. | I | Measure before/after (SCPI round-trip on a multi-write response). If the win is <1 ms, prefer the one-line poll-delay reduction and close the ticket. |
| #347-class: large SCPI response into a stream-shrunk USB buffer livelocks/wedges (`enumerated-but-CDC-dead`). Not caused by these loops, but the write path is the same neighborhood. | V (#572 comment in `UsbCdc_ScpiWrite`, `CLAUDE.md`) | Any change to the write-wait path must be re-tested against `CONF:CAP:JSON?` (~8 KB) while streaming, which is the #572 repro. |

---

## 7. Per-loop verdict

- **Loop #1 `WaitForWrite`** — *Convertible, medium risk, only if §4 = ISR-driven.* Primary
  candidate. Task-notification design in §5. Ship only after bench soak vs #525 repro +
  `SYST:INFo?`.
- **Loop #2 `FlushWriteBuffer`** — *Convertible, medium-high risk.* Multi-chunk; do only after
  #1 is proven. Lower priority.
- **Loop #3 `ResizeWriteBuffer`** — *Not worth converting.* Runs once per `StartStreamData`,
  control-plane, already bounded at 1000 ms; latency here is invisible to users. Leave as-is.
- **Loop #4 `WaitForRead`** — *Dead code with a latent hazard.* It is `UNUSED(...)` and never
  called, but it contains the only **unbounded** wait in the file. The clearly-safe action is
  **deletion** (remove the function + the `UNUSED` line), not conversion — that removes a
  foot-gun for any future caller. This is orthogonal to the semaphore work and can be done
  independently. (Left out of this doc-only pass; propose as a small separate cleanup PR.)

---

## 8. Conclusion

Semaphore-based waiting is **feasible in principle for loop #1** but is **gated on verifying the
`WRITE_COMPLETE` context (§4)** and must be implemented as a **timeout-bounded task notification
with the transfer handle as source of truth**, never a bare binary semaphore (already reverted)
and never an unbounded wait (already caused the #525 wedge). The RAM cost of the recommended
design is effectively zero.

Given (a) the modest, latency-only upside, (b) the repeated history of USB-timing wedges, and
(c) that streaming throughput is unaffected, the **pragmatic call is to keep this doc-only for
now**. If SCPI response latency becomes a real complaint, the **lowest-risk first step is the
one-line poll-delay reduction** in loop #1 (`vTaskDelay(5)` → `vTaskDelay(1)`), measured against
a multi-write response; escalate to the task-notification design only if that measurement shows
the poll granularity — not the 1 ms task cadence — is the dominant cost.

**No firmware change is made in this pass.**
