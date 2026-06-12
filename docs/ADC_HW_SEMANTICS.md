# PIC32MZ ADCHS Hardware Semantics — Documentation Verification (#541 Phase 0)

**Tracking issue:** #541 (re-engineering ticket) / #539 (bug report)
**Primary source:** PIC32 FRM Section 22, 12-bit High-Speed SAR ADC, **DS60001344E**
**Status:** D1–D4 resolved from documentation (this file). D5/D6 + on-silicon
confirmation (Phase 1 Saleae/mdb) pending.

Every claim below is tagged per the debugging-discipline rules (CLAUDE.md):
**V** = verified against the cited document/page; **E** = empirical bench result;
**I** = inference.

---

## D1 — What sets EOSRDY / the EOS interrupt?

**V (DS60001344E, Register 22-2, page 22-12, bit 29):**

> **EOSRDY**: End of Scan Interrupt Status bit
> 1 = All analog inputs are considered for scanning through the scan trigger
> (all analog inputs specified in the ADCCSS1 and ADCCSS2 registers) **have
> completed scanning**
> 0 = Scanning has not completed

EOSRDY is documented to set **only on completion of the full CSS-specified
scan**. There is no documented per-module-completion behavior.

**Consequence:** issue #288's foundational observation — "the EOS interrupt
fires when ANY enabled module completes, not just MODULE7 scans" — is
**unsupported by the documentation**. It was an empirical observation (PR #289
investigation) that was never doc-checked, and the entire #292 design (T1
results read in the EOS deferred task) rides on it. Whatever #288 observed
(possibly an artifact of the pre-#282 software scan-trigger-per-tick, or
undefined-behavior territory per D4 below) is not a documented contract.

## D2 — What clears EOSRDY, and can the interrupt wedge?

**V (same register description, page 22-12):**

> This bit is cleared when ADCCON2<31:24> are read in software.

**V (firmware, `firmware/src/config/default/peripheral/adchs/plib_adchs.c:300-310`):** the
Harmony `ADC_EOS_InterruptHandler` performs `uint32_t status = ADCCON2;`
(a full 32-bit read covering <31:24>, clearing EOSRDY) **before** `IFS6CLR`,
then dispatches the callback. The clear therefore happens in ISR context with
correct source-then-flag ordering — a simple "flag never read → IRQ never
re-arms" wedge is **not** present in our code as written.

**I (open for Phase 1):** a race remains possible in the few-cycle window
between the ADCCON2 read and IFS6CLR (a new scan completion in that window
would be absorbed), but the window is far too small to explain the sharp
~5.2 kHz cliff. The cliff is explained by D4.

## D3 — Per-input data-ready (ARDY) semantics

**V (DS60001344E, Example 22-4 note, page 22-88):** "Reading the data clears
the ARDY bit." Per-input result reads (`ADCDATAx`) clear the corresponding
`ADCDSTATx` ready bit.

**V (§22.3.2, Figure 22-7, page 22-64):** during a scan, "when each conversion
is complete, the result is written to the ADC result buffer and an interrupt
is generated" — per-input data-ready interrupts fire input-by-input as the
scan progresses, independent of end-of-scan. This is the mechanism the current
T2 read path (per-channel `ADC_DATAx` ISRs) correctly relies on, and the
mechanism candidate fix A would use for T1.

## D4 — Scan trigger arriving during an active scan ← THE LOAD-BEARING ANSWER

**V (DS60001344E, §22.3.2 "Input Scan", page 22-64):**

> To ensure predicable results, **a scan should not be retriggered until
> sampling of all inputs has completed. Care should be taken in the system
> design to preclude retriggering a scan while a scan is in progress.**

Retriggering an in-progress scan is **explicitly out-of-spec**. The FRM
defines no ignore/queue/restart semantics — results are simply not
"predicable".

**Consequences:**

1. **The firmware violates this above ~1/T_scan.** Since #107 set
   `ChannelScanFreqDiv = 1`, the streaming timer (TMR5 STRGSRC) retriggers
   the scan every tick. With the full boot CSS list (~19 inputs ×
   (SAMC + conversion) ≈ ~190 µs ≈ 1/5.2 kHz), every stream above ~5.2 kHz
   operates the scan in undefined territory. The #539 evidence (EOS never
   fires; early-scan inputs keep converting; non-monotonic partial recovery
   at higher rates) is the empirical face of that undefined behavior on our
   silicon — consistent, but **not a contract**.
2. **#107's "the scan never overruns to ≥40 kHz (EosOverruns=0)" evidence is
   an instrument artifact**: `EosOverruns` counts EOS-task notification
   pile-up — it reads 0 both when the scan is keeping up **and when EOS never
   fires at all**. The #107 characterization did not distinguish these.
3. **T2 streaming above ~5.2 kHz is also out-of-spec**, even though early-scan
   inputs empirically keep producing data (take-5: encFail=0 at 5925 Hz on
   ch0). The current WiFi caps happen to keep T2 at/below the cliff; USB T2
   caps (up to ~13.8 kHz multi-channel) do not.

**Related defined behavior (contrast):** an *individual Class 2 trigger*
during a scan **is** defined — it pre-empts the scan sequence at the next
input boundary (§22.3.1/Figure 22-8, pages 22-63/64). Only *scan retrigger*
is undefined.

## D5 — Class 1 (dedicated) retrigger during conversion — PENDING

§22.3 (page 22-61) defines Class 1 trigger action as "ends sampling and starts
conversion", with the module reverting to sampling on completion. Behavior of
a trigger arriving mid-conversion not yet located in the FRM — to be resolved
before Phase 2 sign-off. (At streaming rates, tick period ≫ dedicated
conversion time, so exposure is low; still must be cited.)

## D6 — Errata sweep — PENDING

DS80000663 Rev R to be re-swept specifically for ADC scan/EOS/data-ready
items. (The CLAUDE.md errata table lists only #39 VREF for ADC.)

---

## Design constraints established by Phase 0 (input to Phase 2)

1. **T1 result reads must not depend on EOS** above the scan-rate bound —
   candidate A (per-input data-ready reads from the deferred task, D3-backed)
   or B (restore CH3 batch data-ready ISR) — both ride documented per-input
   ARDY semantics.
2. **The shared scan must never be retriggered while in progress** (D4).
   The scan trigger rate must be bounded to ≤ 1/T_scan_worst (a function of
   the CSS list length and shared SAMC — both runtime-known, so the bound is
   computable in firmware). Note the `ChannelScanFreqDiv` MECHANISM still
   exists in the code (the deferred task retains the divided-rate software
   scan-trigger branch); #107 removed only the POLICY by pinning the divisor
   to 1. Re-engaging it with a computed divisor is a candidate Phase-2
   implementation, provided the hardware-trigger path (STRGSRC=TMR5) is also
   gated — the divider only governs the software-trigger branch today.
3. **T2 full-rate-data-above-cliff is not achievable in-spec** with the
   current single-shared-scan architecture: above 1/T_scan, T2 channels
   cannot legally convert once per tick via the scan. Honest T2 caps must
   respect the scan bound, or T2-as-Class-2-individually-triggered
   (defined pre-emption semantics, §22.3.1) must be evaluated in Phase 2.
