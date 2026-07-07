# HAL Boundary-Validation Audit (2026-07)

**Ticket:** #64 — "Optimize HAL operations: validate at boundary, trust internally."
**Scope:** `firmware/src/HAL/*` (user-authored HAL only; Harmony/PLIB excluded).
**Type:** Audit only — **no code changed**. Cleanup is a separate, hardware-validated PR.

## Method

Surveyed every user-authored HAL translation unit for argument re-validation
(NULL checks, index/range bounds, state/init checks) that a caller at the public
API boundary already guarantees — the CLAUDE.md pattern *validate once at the
boundary, trust internally* (canonical example: `BoardRunTimeConfig_Get()` never
returns NULL, so guarding it is pointless). For each site the audit records who
already guarantees the precondition and whether the check is safe to remove.
Checks that guard a genuinely-external, ISR-reachable, lazy-init, or
power-state-dependent entry point are explicitly **kept**.

Evidence tags: **V** = verified from firmware source (file:line); **I** =
inference from control flow; **E** = empirical (a test we ran — none here, this
is a static audit, no build/bench).

---

## Findings table

| # | Site | Redundant / ineffective check | Guaranteed by | Safe to remove? | Note |
|---|------|-------------------------------|---------------|-----------------|------|
| 1 | `HAL/ADC.c:489` (`ADC_ConvertToVoltage`) | `if (channelIndex >= AInChannels.Size) return 0.0;` immediately before calling `ADC_ConvertToVoltageByIndex(channelIndex, …)` | `ADC_ConvertToVoltageByIndex` re-checks the **identical** bound at `ADC.c:465` (V). `ADC_FindChannelIndex` returns either a valid `<Size` index or `(size_t)-1`, both handled by the callee. | **YES** | Pure double-check. Removing lines 489–491 changes nothing observable — the callee still returns `0.0` for `(size_t)-1`. Cleanest zero-risk cleanup. |
| 2 | `HAL/DAC7718/DAC7718.c:118` (`DAC7718_Init`) | `config = DAC7718_GetConfig(id); if (config == NULL) …` | **Nothing** — `DAC7718_GetConfig` returns `&m_DAC7718Config[id]` (V, `DAC7718.c:111`), which is **never NULL** for any `id`. The check is dead code and also does **not** validate `id` bounds. | **YES** (dead) — but see note | Ineffective defensive check. Boundary is `SCPIDAC.c` (single `dacInstanceId`, one instance). Prefer **converting** to `if (id >= MAX_DAC7718_CONFIG)` — the real intended guard, exactly as `DAC7718_UpdateLatch` already does at `DAC7718.c:310` — rather than deleting, to keep the OOB intent. |
| 3 | `HAL/DAC7718/DAC7718.c:185` (`DAC7718_ReadWriteReg`) | `config = DAC7718_GetConfig(id); if (config == NULL) …` | Same as #2 — `GetConfig` never returns NULL (V). | **YES** (dead) — but see note | Same recommendation as #2: replace the never-true NULL test with `id >= MAX_DAC7718_CONFIG`. `RW`/`Reg` range checks at `:175`/`:179` are legitimate arg validation of a public entry point — **keep those**. |
| 4 | `HAL/ADC/AD7609.c:523` (`AD7609_ConvertToVoltage`) | `if (pRuntimeModules != NULL && pRuntimeModules->Size > AIn_AD7609)` — the `!= NULL` half | `BoardRunTimeConfig_Get()` never returns NULL (V — CLAUDE.md standing rule; array-indexed static). | **YES** (the NULL half) | The `Size > AIn_AD7609` half is a real bounds guard — **keep**. Only the `!= NULL` conjunct is the documented anti-pattern. Very low value; folds into a broader BoardRunTimeConfig_Get NULL-check sweep, not DAC/ADC-specific. |
| 5 | `HAL/DIO.c:270` (`DIO_StreamingTrigger`) | `if (DioProbe_IsChannelOwned((uint8_t)i)) continue;` duplicated with the same check inside `DIO_WriteStateSingle` (`DIO.c:124`) | The loop-level skip and the callee guard test the same predicate. | **NO — keep** | **Intentional, documented** (comment at `DIO.c:268-269`): the loop-level check exists to avoid the call overhead on the streaming hot path; the internal guard is defense-in-depth for other callers. Both cheap. Leave as-is. |

---

## Sites reviewed and deliberately KEPT (not redundant)

| Site | Check | Why keep |
|------|-------|----------|
| `HAL/DIO.c:43-44` / `:60-61` (`DIO_ProbeActivatePair` / `…ReleasePair`) | `gpBoardConfig == NULL`, `channel >= DIOChannels.Size` | Reachable from the **ad-hoc debug-probe** path (`DioProbe.c`), an external/interactive entry point that is *not* funneled through SCPI channel validation. Genuine boundary. (V) |
| `HAL/ADC.c:465` (`ADC_ConvertToVoltageByIndex`) | `channelIndex >= AInChannels.Size` | **This is the boundary.** Called directly by `csv_encoder.c`, `JSON_Encoder.c`, `SCPIInterface.c` with caller-supplied indices. It is the single point that finding #1 relies on — must stay. (V) |
| `HAL/ADC/AD7609.c:148,177,199` (`AD7609_BSY_InterruptCallback`, `AD7609_DeferredInterruptTask`) | `pModuleConfigAD7609 == NULL`, `gAD7609_TaskHandle == NULL` | **ISR / async-task reachable** — can fire before init completes or after power-down. Cannot be guaranteed by any task-context caller. (V/I) |
| `HAL/ADC/AD7609.c:307,328,340,480` (`WriteModuleState`, `WriteStateAll`, `ReadSamples`, `TriggerConversion`) | `pModuleConfigAD7609 == NULL` (+ `spi_handle == DRV_HANDLE_INVALID` on ReadSamples) | NQ3 AD7609 is **lazy-init + power-rail-gated** (10 V / analog rail present only in POWERED_UP). These functions are dispatched from `ADC.c` in states where the module may legitimately be uninitialized. State guard, not arg-redundancy. (V/I) |
| `HAL/ADC.c:568,572` (`ADC_InitHardware`) | `moduleIndex == (uint8_t)-1`, `moduleIndex >= AInModules.Size` | Guards the result of `ADC_FindModuleIndex` (a lookup that *can* fail) before indexing the runtime array — real not-found handling, not a re-check of a validated arg. (V) |
| `HAL/ADC/MC12bADC.c:330-331` (`MC12b_ComputeScanList`) | `if (pCss1 != NULL) *pCss1 = …` | Optional-output-parameter pattern — caller may pass NULL to opt out of an output. Not redundant. (V) |
| `HAL/DAC7718/DAC7718.c:175,179` (`DAC7718_ReadWriteReg`) | `RW > 1U`, `Reg > DAC7718_MAX_REGISTER` | Legitimate range validation of a **public** HAL entry taking raw protocol fields. Keep. (V) |
| `HAL/BQ24297/BQ24297.c` mutex/`i2cMutex == NULL`, output-ptr `!= NULL` guards | various | Lazy mutex creation + optional-output-pointer patterns; not validated-arg redundancy. (V) |
| `HAL/DIO.c:24-40` (`WriteGpioPin` / `SetGpioDir`, static) | *(none)* | **Already the target pattern** — internal static helpers do zero validation and trust the caller. Reference exemplar for the ticket, no action. (V) |

---

## Prioritized safe-cleanup list (for the separate cleanup PR)

1. **`HAL/ADC.c:489-491`** — delete the redundant `channelIndex >= Size`
   re-check in `ADC_ConvertToVoltage`. Zero behavioral change (callee guards).
   **Risk: none.** Best first cut.
2. **`HAL/DAC7718/DAC7718.c:118-121` and `:185-189`** — the `config == NULL`
   tests are dead (`DAC7718_GetConfig` never returns NULL) *and* fail to check
   `id` bounds. Replace each with `if (id >= MAX_DAC7718_CONFIG) { LOG_E(...);
   return ...; }` (matching `DAC7718_UpdateLatch:310`) so the intended
   out-of-bounds guard becomes real, then call `GetConfig` on the now-known-good
   `id`. **Risk: low** (single instance, `MAX_DAC7718_CONFIG == 1`, one caller in
   `SCPIDAC.c`). NQ3-only path — validate on NQ3 hardware.
3. **(Optional, low value)** `HAL/ADC/AD7609.c:523` — drop only the `!= NULL`
   half of the `BoardRunTimeConfig_Get()` guard per the standing rule. Better
   folded into a firmware-wide `BoardRunTimeConfig_Get` NULL-check sweep than
   done piecemeal here.

**Explicitly out of scope / do not touch:** every KEPT site above, and in
particular all AD7609 `pModuleConfigAD7609 == NULL` state guards and the
`DIO_ProbeActivatePair`/`ReleasePair` boundary checks — removing those would
expose ISR-context, lazy-init, or debug-probe paths with no upstream guarantor.

## Notes for the implementer

- The DAC7718 cleanup (#2/#3) touches the **NQ3-only** DAC path; it must be
  built for the Nq3 configuration and, per CLAUDE.md, hardware-validated on an
  NQ3 board before merge. Findings #1 (ADC voltage convert) exercises on any
  variant via CSV/JSON streaming.
- Adjacent (separate-bug, **not** part of this validation audit, flagged for
  visibility only): `SCPIDIO.c` gates DIO channel index with `id > …->Size`
  (`SCPI_GPIOSingleDirectionSet`, `SCPI_GPIOSingleStateSet`) — an off-by-one
  that should be `>=`. Since the HAL layer trusts this caller (finding basis for
  DIO), the boundary check being loose is worth its own ticket.
