# Tickless Idle & PIC32MZ Wait-Mode Feasibility (#513)

**Date:** 2026-07  **Status:** Analysis / feasibility (doc-only — no code change)
**Scope:** Evaluate FreeRTOS `configUSE_TICKLESS_IDLE` + the MIPS `wait`
instruction (PIC32MZ Idle mode) as a CPU power-saving lever for the Nyquist
firmware, **without** touching SYSCLK / PLL / peripheral-clock dividers.

Every load-bearing claim below is tagged **V** (verified from primary
source), **E** (empirical test we ran), **I** (inference/hypothesis), **X**
(external authority), or **N** (our own prior notes) per the repo's debugging
discipline. **This document contains no E-class claims — nothing here was
measured on hardware.** It is a source-and-datasheet feasibility read only;
every performance/power figure is an **I** or **X** estimate that a bench
A/B must confirm before any code lands.

---

## 1. TL;DR recommendation

**Do not set `configUSE_TICKLESS_IDLE = 1` on this port as described in the
ticket — it would be a silent no-op (see §3).** If a power win is wanted, the
low-risk lever is a **guarded idle-hook `wait`** (Option A, §5) that halts the
CPU pipeline between interrupts while leaving the 1 kHz tick, all peripheral
clocks, USB, WINC-SPI, SD-SPI, and the streaming timer completely untouched.

Recommended posture:

1. **Prefer Option A (idle-hook `wait`), gated behind a build define**, and
   engage it **only when not streaming** (idle / low-power product states).
   This mirrors the firmware's existing errata posture ("we never enter a
   low-power wait during streaming", §6) and sidesteps every determinism and
   errata hazard below.
2. **Reject full tickless idle (Option B, §4)** for now: it requires
   hand-writing `vPortSuppressTicksAndSleep()` (the MIPS PIC32MZ port does not
   ship one), reprograms the tick timer, and buys little on a USB-tethered
   device that is already woken every 1 ms by the USB SOF and the RTOS tick.
3. **Never engage any wait during streaming** until a Saleae jitter A/B on the
   pri-9 capture path proves the wake latency is below existing ISR jitter.

Expected upside is modest and **unquantified in-firmware** — see §7. The
change is worth doing for genuinely-idle battery life, not for streaming.

---

## 2. Mechanism — what the two levers actually are

### 2.1 The `wait` instruction / PIC32MZ Idle mode

- **V** — `firmware/src/config/default/peripheral/power/plib_power.c:55,61`
  already defines `#define WAIT asm volatile("wait")` and
  `POWER_LowPowerModeEnter(LOW_POWER_IDLE_MODE)` which clears
  `OSCCONbits.SLPEN` (Idle, not Sleep) and then executes `WAIT`. This code is
  present but **not referenced anywhere in the application** (grep of
  `firmware/src` outside `peripheral/power` for `POWER_LowPowerModeEnter` /
  `LOW_POWER_IDLE_MODE` returns nothing).
- **X** — On MIPS M-class (PIC32MZ), `wait` with `SLPEN = 0` enters **Idle
  mode**: the CPU clock is gated off, **peripheral clocks keep running**, and
  any enabled interrupt wakes the core (DS60001320 "Power-Saving Modes"). With
  `SLPEN = 1` it enters **Sleep**, which gates peripheral clocks too — that
  would stop USB, the SPI baud generators, and the timers, so Sleep is **not
  viable** for a USB-tethered DAQ device (the ticket's "Out of scope" section
  agrees).
- **I** — Idle mode is the only wait variant compatible with live USB CDC /
  WiFi / streaming, because those subsystems must keep clocking. The idle-hook
  approach therefore must guarantee `SLPEN = 0` before every `wait`.

### 2.2 FreeRTOS tickless idle

- **X** — `configUSE_TICKLESS_IDLE = 1` makes `prvIdleTask` call
  `portSUPPRESS_TICKS_AND_SLEEP(xExpectedIdleTime)` once the projected idle
  span exceeds `configEXPECTED_IDLE_TIME_BEFORE_SLEEP` (default 2 ticks). A
  correct port implementation stops the periodic tick, reprograms the tick
  timer as a one-shot for the whole idle span, issues `wait`, then on wake
  calls `vTaskStepTick(n)` to fast-forward the tick count by the elapsed
  interval. This is the "sleep through N ticks, wake once" optimization.

---

## 3. Blocking finding: this port ships **no** `vPortSuppressTicksAndSleep()`

- **V** — The MIPS PIC32MZ port
  (`firmware/src/third_party/rtos/FreeRTOS/Source/portable/MPLAB/PIC32MZ/port.c`,
  369 lines) contains **no** `vPortSuppressTicksAndSleep` and **no**
  `portSUPPRESS_TICKS_AND_SLEEP` definition. Grep across the whole
  `Source/portable/` tree for either symbol returns nothing.
- **V** — Because the port never defines the macro,
  `FreeRTOS.h:2705-2707` supplies the fallback:
  ```c
  #ifndef portSUPPRESS_TICKS_AND_SLEEP
      #define portSUPPRESS_TICKS_AND_SLEEP( xExpectedIdleTime )
  #endif
  ```
  — an **empty** macro.

**Consequence (I, but a direct reading of the two V facts):** setting
`configUSE_TICKLESS_IDLE = 1` **as the ticket's step 1 describes, with nothing
else, compiles cleanly but saves zero power** — the idle task enters the
tickless code path and immediately expands `portSUPPRESS_TICKS_AND_SLEEP` to
nothing, then spins exactly as it does today. The ticket's step 2 ("the MIPS
PIC32 FreeRTOS port has a stock implementation; confirm it's compiled in") is
**incorrect for this port** — Amazon's upstream MIPS PIC32MZ port has never
shipped a tickless implementation; that assumption is the load-bearing error
in the ticket. Any real tickless idle requires us to **author** the port
function (Option B).

---

## 4. Option B — full tickless idle (hand-written port function)

To make tickless real we would have to write `vPortSuppressTicksAndSleep()`:

1. `eTaskConfirmSleepModeStatus()` gate (abort if a task became ready).
2. Reprogram Timer 1 (`PR1`) as a one-shot for the suppressed span. **V** —
   the tick runs on **Timer 1** (`port.c:233-249`
   `vApplicationSetupTickTimerInterrupt` writes `T1CON`/`PR1`; `port_asm.S:62`
   binds `_TIMER_1_VECTOR`). The reload value is
   `(configPERIPHERAL_CLOCK_HZ / prescale / configTICK_RATE_HZ) - 1`.
3. `wait` (Idle mode).
4. On wake, read elapsed T1 counts, `vTaskStepTick(elapsed)`, restore the
   periodic `PR1`, resume.

**Why B is not worth it now:**

- **I** — On a USB-enumerated device the host issues a Start-of-Frame every
  1 ms (**X**, USB 2.0 full-speed), and our RTOS tick is 1 ms. The idle task
  is therefore almost never idle for more than ~1 tick, so
  `xExpectedIdleTime` rarely exceeds `configEXPECTED_IDLE_TIME_BEFORE_SLEEP`
  (2) — tickless would seldom even engage. The one-shot-reprogram machinery
  earns its keep only when a device sleeps for *many* ticks, which this one
  does not while tethered.
- **I** — B's power benefit over A (§5) is only "skip the CPU wakeup on tick
  interrupts we don't need." Since USB SOF already wakes us every 1 ms, the
  incremental saving over "just `wait` in the idle hook and let the 1 ms tick
  wake you" is small.
- **Risk** — B adds tick-accounting code (T1 reprogram, `vTaskStepTick`
  arithmetic, wake-race handling) to the kernel's timing core. A bug there
  corrupts *all* `vTaskDelay`/timeout timing. High blast radius for a marginal
  gain.

**Verdict: defer B.** Revisit only if a future untethered / battery-only
product state (USB fully suspended, no streaming) makes multi-tick idle spans
common — that is the regime where B pays off.

---

## 5. Option A — guarded idle-hook `wait` (the recommended lever)

Set `configUSE_IDLE_HOOK = 1` (**V** — currently `0`,
`FreeRTOSConfig.h:326`) and implement the existing empty
`vApplicationIdleHook()`
(`firmware/src/config/default/freertos_hooks.c:179`) as, in effect:

```c
void vApplicationIdleHook(void) {
    /* Idle mode only: SLPEN must be 0 so peripheral clocks (USB, SPI4,
       TMR4/5 streaming timer, T1 tick) keep running. Wakes on any IRQ. */
    __asm__ __volatile__("wait");
}
```

- The 1 kHz tick keeps firing; there is **no** tick suppression and **no**
  timer reprogramming, so **none** of the §4 kernel-timing risk applies.
- Between interrupts the CPU pipeline is halted; it resumes on the next
  interrupt (tick, USB SOF, streaming TMR5, WINC, SD, etc.).
- **I** — This captures the bulk of the *idle* CPU-power saving (the CPU is
  the largest clocked consumer) at a fraction of B's complexity.

**Mandatory guards:**

1. **Build-time gate for debug builds.** **X** — a halted core interacts badly
   with PICkit-4 / MDB halt and single-step (the ticket flags this). Gate the
   `wait` behind e.g. `#if !defined(__DEBUG) && defined(ENABLE_IDLE_WAIT)` so
   MDB post-mortem debugging (our crash-capture workflow) is never blocked.
2. **Do not `wait` while streaming.** See §6 — engage only in idle/low-power
   product states. Simplest implementation: check a "streaming active" flag (or
   the `StreamingInterface`/OPER "Measuring" bit) in the hook and skip `wait`
   when set.

---

## 6. Risks — streaming, USB, WiFi, determinism, errata

### 6.1 Pri-9 capture determinism (I)

- The streaming path is: TMR5 match (hardware) → timer ISR
  `Streaming_Defer_Interrupt` → notify the pri-9 deferred task. From Idle mode,
  an interrupt restarts the CPU in a handful of cycles; at 252 MHz that
  wake latency is **< ~0.1 µs (I)** — well below the existing ISR
  entry/exit jitter already documented in `docs/PIPELINE_TIMING.md`.
- **However**, this is exactly the kind of sub-µs claim that must be
  **measured, not asserted** (repo rule: single-trial/estimate ≠ verified).
  Before enabling `wait` during streaming, a Saleae A/B on a DIO pipeline probe
  toggled at the streaming-timer ISR must show no jitter regression vs. a
  no-wait build on the same firmware in the same session.
- Because A (§5) recommends **not** waiting during streaming at all, this risk
  is avoided in the recommended configuration — the concern only becomes
  load-bearing if someone later wants wait-while-streaming for its own power
  win (which is near-zero anyway, since at high rate the idle task barely runs
  — the pipeline keeps higher-priority tasks busy, so `wait` seldom executes).

### 6.2 USB CDC (I)

- USB is a hardware peripheral whose clock keeps running in Idle mode; its
  interrupts wake the core. Enumeration and CDC transfers are interrupt-driven,
  so a `wait` in the idle hook cannot stall them beyond the wake latency.
- **X/N** — CLAUDE.md's USB-CDC guidance ("delivery is bursty; host must read
  fast") is host-side; it is orthogonal to core Idle mode. No expected
  interaction, but a throughput regression run (`test_overnight_characterization.py`)
  on an idle-hook build is the confirming test.

### 6.3 WiFi / SD SPI (V/I)

- **V** — SPI4 (shared SD + WINC) is clocked from PBCLK2 (`MCLKSEL = 0`,
  CLAUDE.md Clock Tree); PBCLK2 keeps running in Idle mode. Idle-hook `wait`
  changes **no** clock divider and **no** SPI baud, so it cannot perturb the
  WINC or SD timing that past WiFi work (#487, #540) was so sensitive to. This
  is the whole point of choosing tickless/wait over PLL scaling.

### 6.4 Silicon errata (X)

- **X** — DS80000663 erratum **#44**: a Timer2-9 match coinciding with **sleep
  entry** may miss its interrupt (no workaround). CLAUDE.md marks this "Safe —
  we never enter sleep during streaming." Idle-hook `wait` uses **Idle**, not
  Sleep, and the streaming timer is TMR4/5; the erratum is written for Sleep +
  Timer2-9. **I** — the erratum most likely does not apply to Idle mode, but
  the safe, zero-argument way to stay inside the firmware's existing proven
  posture is to **not wait during streaming** (Option A's guard). That keeps us
  bit-for-bit compatible with the "#44 Safe" rationale already in CLAUDE.md.
- **X** — erratum #9 (USB won't function if USB PHY off in Sleep, `USBSSEN=1`)
  reinforces that **Sleep is off the table**; Idle keeps the PHY clocked.

---

## 7. Expected savings — and why they are unquantified here

The ticket estimates ~30-50 % idle, ~10-20 % low-rate-streaming, negligible
high-rate. Those are **I/X-class estimates, not measured (E)**. Two structural
reasons the in-firmware win may land at the low end:

- **I** — On a tethered device the core is re-woken every 1 ms by the USB SOF
  and the RTOS tick regardless, so `wait` only saves the *fraction of each
  1 ms the idle task would otherwise spin*. That fraction is large when truly
  idle, small whenever any task has work.
- **I** — During streaming the idle task rarely runs (higher-priority
  encode/transport/capture tasks are busy), so `wait` seldom executes — the
  "10-20 % at low rate" figure assumes the streaming task spends most of each
  tick blocked on `ulTaskNotifyTake`, which holds only well below the transport
  ceiling.

**Any decision to ship must be gated on a bench power A/B** (board on a USB
current meter or bench supply with current readout): idle vs. 1 kHz vs. 5 kHz
vs. 16 kHz USB CSV, idle-hook build vs. main, same session. Without that E-class
number, the feature's value is unproven.

---

## 8. Relationship to Chris's abandoned PLL-scaling attempt (N)

- **N** — Project memory `project_pll_scaling_tried.md`: dynamic SYSCLK/PLL
  scaling was tried and abandoned because `configCPU_CLOCK_HZ` drift breaks
  every `vTaskDelay(N)` (delays are tick counts, not absolute time).
- **V — important nuance for this port:** the FreeRTOS tick here is **not**
  derived from `configCPU_CLOCK_HZ`. `FreeRTOSConfig.h:53-57` states the port
  "does not use `configCPU_CLOCK_HZ`"; the T1 compare value comes from
  `configPERIPHERAL_CLOCK_HZ` (PBCLK3) — `port.c:235`. So the PLL-scaling
  failure was really a *peripheral-clock*-drift problem (change SYSCLK → PBCLK3
  changes → T1 reload wrong → every delay wrong), plus the cascade into
  USB/SPI/WINC baud that CLAUDE.md documents.
- **I** — Tickless idle / idle-hook `wait` **touch no clock at all**, so they
  are immune to the exact failure mode that killed PLL scaling. That is the
  central argument in tickless idle's favor: same "spend less energy when
  idle" goal, none of the clock-drift blast radius. It does **not**, however,
  deliver PLL scaling's headline benefit (lower dynamic power *while actively
  computing*) — `wait` only helps when the core has nothing to do.

---

## 9. Concrete next steps (if pursued)

1. Implement **Option A** behind `ENABLE_IDLE_WAIT` (default off) +
   `#if !defined(__DEBUG)`; set `configUSE_IDLE_HOOK = 1`; guard the `wait` to
   skip while the OPER "Measuring" bit is set (not-streaming only).
2. Bench **power A/B** (idle + 1/5/16 kHz USB CSV), main vs. idle-hook build,
   same session — this is the E-class number the whole feature hinges on.
3. **Throughput regression** (`test_overnight_characterization.py`) — confirm
   no interface regresses on ceiling or KB/s.
4. **MDB / debug check** — confirm halt / single-step / crash-capture still
   work with the hook compiled in (should be moot given the `__DEBUG` gate).
5. Only if a future untethered product state makes multi-tick idle common,
   revisit **Option B** (hand-written `vPortSuppressTicksAndSleep`) with the
   T1-one-shot design in §4.

---

## Appendix — source anchors (V)

| Claim | File:line |
|---|---|
| Port ships no tickless function | `.../portable/MPLAB/PIC32MZ/port.c` (whole file; grep negative) |
| `portSUPPRESS_TICKS_AND_SLEEP` defaults to empty | `.../Source/include/FreeRTOS.h:2705-2707` |
| Tick = Timer 1, reload from `configPERIPHERAL_CLOCK_HZ` | `port.c:233-249`; `port_asm.S:62` |
| Tick does not use `configCPU_CLOCK_HZ` | `FreeRTOSConfig.h:53-57` |
| `configUSE_TICKLESS_IDLE = 0` today | `FreeRTOSConfig.h:95` |
| `configUSE_IDLE_HOOK = 0` today | `FreeRTOSConfig.h:326` |
| Empty idle hook exists | `freertos_hooks.c:179` |
| `wait` macro + Idle-mode (SLPEN=0) helper already present, unused | `peripheral/power/plib_power.c:55,61-95` |
| SPI4 on PBCLK2, MCLKSEL=0 | CLAUDE.md "Clock Tree" |
| Errata #44 (Timer match at sleep entry), #9 (USB PHY off in Sleep) | CLAUDE.md "Known Silicon Errata" / DS80000663 |
