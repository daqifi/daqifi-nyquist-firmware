# PIC32MZ ADCHS Hardware Semantics — Documentation Verification (#541 Phase 0)

**Tracking issue:** #541 (re-engineering ticket) / #539 (bug report)
**Primary source:** PIC32 FRM Section 22, 12-bit High-Speed SAR ADC, **DS60001344E**
**Status:** D1–D4 resolved from documentation; **Phase 1 silicon confirmation
COMPLETE** (Saleae, below). D5/D6 pending.

---

## Phase 1 — On-silicon confirmation (E; Saleae Logic 8, 2026-06-11, main @ e9302a51)

1×T1 (ch4), OBDiag=1, USB PB, 4 s captures at 25 MS/s on DIO probes
0 (timer ISR), 1 (EOS ISR entry), 2 (deferred wake), 5 (EOS task wake):

| Stream rate | Timer ISR /s | EOS ISR /s | EOS task /s |
|---|---:|---:|---:|
| 4500 | 4499.6 | 4499.6 (1:1) | 4499.6 |
| 5000 | 4999.9 | **1521.7 (30%)** | 1521.7 |
| 5500 | 5499.4 | **0** | 0 |
| 9000 | 8999.3 | **0** | 0 |

- **T_scan measured = 216 µs** (timer-ISR → next EOS-ISR median at 4500 Hz;
  p10 215.0 / p90 216.6 µs) ⇒ in-spec scan ceiling **≈ 4630 Hz** for the
  current boot CSS list + shared SAMC. Matches the D4 model: degradation
  begins exactly when the tick period drops below T_scan (5000 Hz = 200 µs),
  and EOS is fully dead by 5500.
- **The EOS *interrupt itself* stops firing** (probe 1 = ISR entry = 0 edges
  in 4 s) — not task starvation, not read failure. Discriminates the #539
  candidate mechanisms conclusively.
- **The encFail cliff at 5500 (not 4630/5000) is a cache artifact**: at
  5000 the sporadic 1521/s EOS writes keep the one-deep LATEST slots
  *valid* (stale by a few ticks); the #535 validity gate only trips when
  EOS reaches zero. Value FRESHNESS already degrades from ~4630 Hz.
- **The earlier "partial recovery" at 7.5–11 kHz is session-phase
  bimodality**: this 9000 Hz session showed EOS fully dead, while the prior
  encFail run at 9000 measured 46% valid — consistent with
  undefined-behavior phase alignment locking per session start (also seen
  at 12222: 60% vs 0.1% across two starts).
- Open observation (non-blocking): probe 4 (deferred-task trigger-block
  pulse) showed 0 edges at 4500, 179/s at 5000, full tick rate at 5500+ —
  pulse-width vs 40 ns sampling resolution and/or trigger-path state worth a
  look during Phase 3, not load-bearing for the mechanism.

Raw captures: C:\temp\541_cap{4500,5000,5500,9000}/digital.csv.

### T_scan vs the documented timing model (SAMC sweep)

Three-point SAMC sweep (1×T1 OBDiag=1 @ 4000 Hz, timer→EOS median):

| SAMC (shared) | T_scan measured | Linear fit |
|---:|---:|---:|
| 10 | 48.3 µs | 48.7 µs |
| 50 | 124.1 µs | 123.3 µs |
| 100 | 216.2 µs | 216.6 µs |

**Empirical law: T_scan(SAMC) = 1.865 µs × SAMC + 30.1 µs** (this boot CSS
list; linear to <0.5%).

- **Structural match with the FRM model is exact**: per-input time =
  (SAMC+2)·TAD sample + conversion, and the fit's intercept/slope ratio
  gives **conversion + handoff ≈ 14 TAD** — the documented 13-TAD 12-bit
  conversion plus ~1 TAD (E confirming V).
- **Absolute-scale ambiguity: RESOLVED (V, DS60001320H).** The EF *device
  datasheet deviates from the FRM* on the ADC clock bit semantics — its own
  revision history flags it ("The bit value definitions for the ADCSEL<1:0>
  and CONCLKDIV<5:0> bits in the ADCCON3 register were updated", Appendix A
  vs Rev G). Per **Register 28-3**:
  - `ADCSEL=00` → **TCLK = PBCLK3 = 100 MHz** (not SYSCLK)
  - `CONCLKDIV`: **TQ = (N+1)·TCLK** (`000010 = 3·TCLK`!) — not the FRM's
    2N·TCLK. CONCLKDIV=4 ⇒ TQ = 5 × 10 ns = 50 ns
  - `ADCDIV` (Register 28-2): `0000001 = 2·TQ = TAD7` ⇒ **TAD7 = 100 ns**

  With **N = 19** (CSS bit set verified equal to the board map: 11 T2 user +
  8 monitoring, no extras — the STRIG-outside-CSS hypothesis is refuted):

  | Quantity | Datasheet prediction | Measured | Δ |
  |---|---:|---:|---:|
  | Slope (per SAMC count) | 19 × 100 ns = 1.900 µs | 1.865 µs | −1.8% |
  | T_scan @ SAMC=100 | 19×(115)×100 ns = 218.5 µs | 216.2 µs | −1.1% |
  | T_scan @ SAMC=10 | 19×(25)×100 ns = 47.5 µs | 48.3 µs | +1.7% |

  **Reconciled within 2%** — the residual is the conversion-constant
  (13 vs ~14 TAD) and slope noise. The earlier "98 ns / 23-slot" puzzle was
  entirely an FRM-vs-datasheet decode error: a live demonstration of why
  this audit verifies against the DEVICE datasheet, not just the FRM (the
  FRM's own page 22-2 note says it "may not apply to all PIC32 devices").

  **Stale firmware comments to fix in Phase 3 (N-class corrections):**
  `MC12bADC.c:376` ("With ADCDIV=1, ADC_clk=50 MHz so one clock = 20 ns" —
  actual TAD7 = 100 ns), the same claim in `SCPIADC.h`'s SAMC doc and
  CLAUDE.md's SAMC section, and issue #328's body (which decoded boot
  shared SAMC as 1; it is 100, SCPI-verified).
- **SAMC is a major design lever (E)**: at SAMC=10 the scan completes in
  48 µs ⇒ in-spec scan ceiling ≈ **20.7 kHz** (vs ≈4.6 kHz at the current
  SAMC=100), and EOS ran 1:1 at 4000 Hz. Phase-2 option: shorter shared
  SAMC (accuracy trade-off to characterize) lifts the in-spec scan rate
  above every transport cap — potentially complementing or simplifying
  candidates A/B.

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

## D5 — Class 1 (dedicated) trigger semantics — RESOLVED

**V (FRM §22.3, page 22-61):** "Each Class 1 input has a unique trigger …
and upon arrival of the trigger, ends sampling and starts conversion. Upon
completion of conversion, the ADC module reverts back to sampling mode.
**When a Class 1 input is enabled and is not being converted, it is always
sampled.**"

- A trigger arriving mid-conversion is not explicitly defined, but the
  exposure is structurally nil in our envelope: dedicated conversion ≈
  13 × TAD_ded = 13 × 100 ns ≈ **1.3 µs**, while the minimum tick period at
  the 16 kHz ISR ceiling is 62.5 µs — a Class-1 retrigger-during-conversion
  cannot occur during streaming.
- **Design-relevant corollary (V):** because Class 1 inputs sample
  continuously between conversions, **T1 settling time = the full
  inter-trigger gap (≥ 61 µs at max rate), not the SAMC window.** The
  ~10 kΩ source-impedance constraint therefore binds the *scanned*
  (T2/monitoring) inputs — whose sampling window is exactly
  (SAMC+2)·TAD7 — far more than T1.
- ADCCON1 STRGLVL = 0 (boot): scan trigger is **positive-edge sensitive**
  ("only a single scan trigger will be generated, which will complete the
  scan of all selected analog inputs") — consistent with the D4 model.

## D6 — Errata sweep (DS80000663R) — RESOLVED

All ADC-module errata, swept against our usage:

| # | Erratum | Revs | Our exposure |
|---|---|---|---|
| 11 | Multiple digital filters mis-capture when sources ready simultaneously | A1/A3 | None today (no filters); **constrains #332** (one filter at a time on early revs) |
| 12 | Level trigger won't burst in Debug mode | A1/A3 | None — edge triggers only (STRGLVL=0, TMR5 edge) |
| 13 | Differential mode DNL +3 at code 3072 | all | None — single-ended |
| 14 | VDD < 2.5 V: only one ADC core usable | all | None — 3.3 V rail |
| 15 | **Turbo mode not functional** | all | **Closes the Turbo-mode option for Phase 2** (combining two dedicated cores for 2× rate is dead silicon) |
| 18 | **Temperature sensor does not function** (workaround: none) | all | **Our monitoring scan includes AN44 (ADC_CHANNEL_TEMP)** — that slot reads a nonfunctional sensor. Follow-up: stop surfacing temp as a valid reading, and drop AN44 from the scan list when dynamic CSS lands (recovers ~11.5 µs of every scan) |
| 39 | VREF− current when external ref used | all | Known, tracked in CLAUDE.md errata table |

**Headline for #539/#541: no erratum touches scan retrigger, EOS, or
per-input ARDY** — the #539 failure is not a silicon bug; it is the
documented-undefined retrigger territory (D4), and candidate A's ARDY
mechanism has no errata against it.

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

---

## Worked example — registers → timing, verified against silicon

The complete arithmetic chain for the NQ1 boot configuration, every input
cited. Use this as the template for the Phase-2/3 computed scan bound and
for re-deriving timing after any clock or CSS change.

### Inputs (V)

| Quantity | Value | Source |
|---|---|---|
| PBCLK3 | 100 MHz (TCLK = 10 ns) | CLAUDE.md clock tree; DS60001320H Reg 28-3: `ADCSEL=00` → PBCLK3 |
| `ADCCON3` | `0x04002000` → CONCLKDIV = 4 | `plib_adchs.c:79` (boot; no runtime writes) |
| `ADCCON2` | `0x00642001` → ADCDIV = 1, SAMC = 100 | `plib_adchs.c:78`; SAMC SCPI-verified at runtime (`CONF:ADC:SAMC:SHARed?` → 100) |
| CONCLKDIV semantics | TQ = (N+1) × TCLK | DS60001320H Reg 28-3 (**EF deviation** — FRM DS60001344E says 2N; the datasheet revision history flags the change) |
| ADCDIV semantics | TAD7 = 2 × ADCDIV × TQ | DS60001320H Reg 28-2 (`0000001 = 2 * TQ = TAD7`) |
| Sample time | (SAMC + 2) × TAD7 | DS60001320H / FRM (SAMC `0000000000 = 2 TAD`) |
| 12-bit conversion | 13 TAD7 | FRM §22 conversion time; empirically 13–14 incl. scan handoff |
| N (scan length) | 19 inputs | `ADCCSS1=0xEF0809E0` (13 bits) + `ADCCSS2=0x16C1` (6 bits); bit-set == board map exactly (11 T2 public + 8 monitoring, `NQ1BoardConfig.c` + `CommonMonitoringChannels.h`) |

### Derivation

```
TCLK  = 1 / 100 MHz                    = 10 ns          (PBCLK3)
TQ    = (CONCLKDIV + 1) × TCLK = 5 × 10 ns = 50 ns
TAD7  = 2 × ADCDIV × TQ        = 2 × 50 ns = 100 ns

per-input  = (SAMC + 2) × TAD7  +  ~13 TAD7 conversion
           = (100 + 2 + 13) × 100 ns
           = 11.5 µs

T_scan     = N × per-input = 19 × 11.5 µs = 218.5 µs
f_scan_max = 1 / T_scan                  ≈ 4.58 kHz
```

### Verified against silicon (E — Saleae, probes 0/1, timer→EOS median)

| Quantity | Datasheet calc | Measured | Δ |
|---|---:|---:|---:|
| T_scan @ SAMC=100 | 218.5 µs | 216.2 µs | −1.1% |
| T_scan @ SAMC=50  | 123.5 µs | 124.1 µs | +0.5% |
| T_scan @ SAMC=10  | 47.5 µs  | 48.3 µs  | +1.7% |
| Slope (∂T_scan/∂SAMC) | 19 × TAD7 = 1.900 µs | 1.865 µs | −1.8% |
| Empirical law | — | T_scan = 1.865 µs × SAMC + 30.1 µs | — |
| EOS rate @ 4500 Hz stream | 1:1 with timer | 4499.6 /s vs 4499.6 /s | exact |
| EOS rate @ 5000 Hz (period 200 µs < T_scan) | degraded (out-of-spec) | 1521.7 /s (30%) | — |
| EOS rate @ 5500 / 9000 Hz | undefined (out-of-spec) | **0 /s** | — |

Residuals (≤2%) are the conversion constant (13 vs ~14 TAD7, i.e. ~1 TAD7
of scan handoff per input) plus slope-fit noise — all within the documented
model. **The published values and the silicon agree.**

### Generalized bound for firmware (Phase-2 requirement 3)

> **SUPERSEDED in Phase 3 — the per-input-only form below is NOT safe.**
> Phase-3 hardware validation (PR #543, 2026-06-12) showed it omits a
> per-scan **fixed** overhead and places admitted rates exactly at the
> boundary; see the corrected bound that follows.

The Phase-2 proposal was:

```
f_scan_max = 1 / ( N_active × (SAMC + 2 + 14) × TAD7 )        [v1 — fatal]
```

**Phase-3 finding (E, 2026-06-12):** streaming 1×T1 OBDiag=1 at this
formula's n=7 cap (12,315 Hz, monitoring-only scan) reproducibly **killed
the USB peripheral within ~1 s** — device off the bus until PICkit reset,
reproduced both WSL-side and Windows-native (rules out the usbipd
artifact). Sustained mid-scan retriggering of a small dynamic scan list is
catastrophically worse than the silent EOS death the 19-input boot scan
exhibited in #539. Threshold bisect (3 s legs, NOCAP): clean ≤ 11,500 Hz
(87.0 µs period), wedge at 11,750 Hz (85.1 µs) ⇒ T_busy(7) ∈ (85.1, 87.0] µs.

Jointly solving that window with the Phase-1 n=19 anchors (timer→EOS =
216 µs; 4,500 Hz = 222.2 µs period verified clean ⇒ T_busy(19) ∈
[216, 222.2] µs) gives a consistent two-term model (both windows satisfied
within 0.2 µs):

```
T_busy     = N_active × (SAMC + 2 + 14) × TAD7  +  ~5.5 µs (fixed, per scan)
```

(Corollary: the Phase-1 "4,500 Hz clean" data point sat ~0.1 µs from the
true boundary — luck, not margin.)

**Corrected bound as shipped (`MC12b_ScanMaxFreq`, PR #543):**

```
f_scan_max = 1 / ( 1.1 × ( N_active × (SAMC + 2 + 14) × TAD7 + 6 µs ) )
```

6 µs is the fixed term rounded conservatively; the 10% period margin keeps
every admitted rate off the boundary. A purely multiplicative derate cannot
substitute for the additive term — at n=1 it would still admit rates past
the true ~58 kHz boundary. At SAMC=100: n=1 → 51,652 Hz (never binds);
n=7 → 10,425; n=8 → 9,201; n=18 → 4,232. At-cap silicon verification:
n=7 @ 10,425 Hz ran 66,694 ticks with 66,694 samples and zero
drops/misses/EOS-overruns; Saleae shows EOS 1:1 with timer→EOS = 82.2 µs
(n=7) and 208.6 µs vs 208.8 µs model (n=18, 0.1%). Anchors recorded in
`daqifi-python-test-suite` `benchmarks/541_adc_read_path/SILICON_ANCHORS.md`.

Open hazard: NOCAP/benchmark modes bypass this cap by design and can still
drive a small scan list into the USB-fatal zone; the failure mechanism
(interrupt storm vs hard fault) is unconfirmed pending an mdb post-mortem.

---

## Static CSS finding (E+V, 2026-06-11) — the scan list never changes

`ADCCSS1/2` are written once by the Harmony boot init and **never modified at
runtime** (verified: no ADCCSS writes outside `plib_adchs.c`). Consequences:

1. **Scanning one T2 channel scans all 19 inputs.** Channel enable/disable
   only changes which results the firmware reads; the mux walks the full
   CSS list every scan trigger.
2. **OBDiag=0 does not shrink the scan** — monitoring inputs still convert
   every scan; only their reads are skipped in the EOS task.
3. This is why the #539 cliff is channel-count-independent (1/3/5×T1
   identical): T_scan is a constant 216 µs regardless of configuration.

## Phase-2 implementation requirements (established by Phase 0/1)

1. **Decouple T1 result reads from EOS** — per-input ARDY data-ready
   semantics (D3, documented): candidate A (deferred-task polling) or B
   (CH3 batch data-ready ISR). T1 must deliver a fresh sample per tick
   independent of the scan.
2. **Dynamic CSS**: rebuild the scan list at stream start from
   (enabled T2 channels) ∪ (monitoring if OBDiag=1), via the documented
   `ADCCON3.TRGSUSP` → `UPDRDY` SFR-update mechanism (Register 28-x /
   FRM p.22-15). Scan cost then scales with what is actually used
   (1×T2 OBDiag=0 ⇒ ~1 input ⇒ T_scan ≈ 11.5 µs ⇒ ~87 kHz in-spec ceiling).
3. **Computed scan-rate bound** folded into the cap equation:
   `f_scan_max = 1 / (N_active × (SAMC + 2 + 14) × 100 ns)` — every term
   runtime-known. The bound must gate the **hardware STRGSRC path**, not
   just the legacy software-divider branch (`ChannelScanFreqDiv` mechanism
   still exists but only governs the software trigger).
4. **SAMC stays at 100 pending analog characterization.** The signal path
   has ~10 kΩ series source impedance — the S&H cap charges through it, so
   shortening SAMC trades directly into AC amplitude error (gain error
   rising with frequency). #328 (runtime SAMC, merged) was motivated by
   exactly this trade. Any SAMC change in either direction requires
   measured amplitude-error data first; do NOT bank the "SAMC=10 ⇒ 20 kHz
   ceiling" lever until that characterization exists. (With dynamic CSS,
   most configs get their headroom from N_active anyway.)
5. **Comment hygiene** (Phase 3): fix the 50 MHz/20 ns TAD claims listed
   above; update CLAUDE.md's "ADC Architecture & ISR Design" section
   (still documents the pre-#292 topology).

### Phase-3 addendum 2 (E, 2026-06-12) — aggregate ADC-event-rate ceiling (D-C v4)

A third independent fatal limit, found by continuing the ceiling probes
after the EOS-rate fix: each enabled T2 USER channel fires a per-conversion
data-ready ISR on top of the per-scan EOS, and the combined event rate
`f × (nUserT2 + 1)` is USB-fatal around ~66–72k events/s. Anchors: 11×T2
OBDiag=0 wedged at 6,000 Hz on the plain ADMITTED path (72k events/s; its
cap was the 6,470 tick-budget value) and at 6,750 NOCAP (81k/s); clean at
5,500 × 60 s (66k/s); 60k/s is 120 s-endurance-proven (the 16-channel
at-cap cell). Neither prior bound catches it (EOS only 6 kHz; scan 10%
under busy). Fixed: `ADC_EVENT_RATE_MAX_PER_S = 60,000` clamp,
`MC12b_ScanMaxFreq(nActive, nUserT2)` (firmware `582ee9a3`); the new
5,000 cap validated (reject `-222` at 5,001 + 120 s admitted soak).
Monitoring channels fire no data-ready ISRs and do not count — which is
why the n=1/n=7 wedges needed the separate EOS-rate limit.

The cap's scan gate is therefore: `min(busy-bound, 10,400 EOS-rate,
60,000/(nUserT2+1))`. All three are silicon-anchored; the storm-vs-
starvation mechanism behind the two fatal limits is #545.
