# ADCHS Digital Filter (ADCFLTRn) Feasibility — ENOB Improvement Knob

**Ticket:** #332 (feasibility/research). **Date:** 2026-07. **Board focus:** NQ1
(PIC32MZ2048EFM144, on-chip MC12bADC / ADCHS). **Status of this document:**
research only — no filter is wired into the streaming path. That is deliberately
left as a characterized follow-up (see *Recommendation*).

Every load-bearing claim below is tagged per the project debugging-discipline
convention: **X** = external authority (datasheet/FRM/errata), **V** = verified
from our primary source (repo code, cite file:line), **I** = inference/hypothesis,
**E** = empirical from a test we ran. There is no **E** in this document — no
hardware was touched (bench boards were reserved for overnight jobs, and #332 is
a build-only/doc ticket).

Primary references:
- **X** PIC32MZ EF Family Reference Manual, Section 22 "12-bit High-Speed
  Successive Approximation Register (SAR) ADC" (DS60001344), *Digital Filter*
  subsection and Register 22-x `ADCFLTRy`.
- **X** PIC32MZ2048EFM144 device datasheet DS60001320H (ADCHS module).
- **V** Current ADC configuration: `firmware/src/HAL/ADC/MC12bADC.c`,
  `firmware/src/state/board/NQ1BoardConfig.c`.
- **N** ADC architecture record: repo `CLAUDE.md` "ADC Architecture & ISR Design",
  `docs/ADC_HW_SEMANTICS.md`.

---

## 1. What ADCFLTRn offers

**X** The ADCHS peripheral contains **six identical digital-filter units**,
`ADCFLTR1`–`ADCFLTR6`, each a 32-bit SFR. All read `0` at reset and none are
configured anywhere in the current tree (**V** `grep -rn ADCFLTR firmware/src`
returns nothing outside this document). Each unit exposes:

| Field | Meaning |
|---|---|
| `CHNLID<4:0>` | Which primary ADC channel (0–31) the filter taps |
| `DFMODE` | `1` = averaging (accumulate N, return sum/N at native width); `0` = oversampling (accumulate N, return the wider accumulated word) |
| `OSVR<2:0>` | Oversample ratio: 2×, 4×, 8×, 16×, 32×, 64×, 128×, 256× |
| `AFEN` | Filter enable |
| `DATARDY` | Filtered result available |
| `OVRFLOW` | Accumulator overflowed |
| `RDY_IE` | Interrupt when filtered result ready |
| `DATA<15:0>` | Filtered output (read from `ADCFLTRy` low half / associated data reg) |

**X** Each unit accumulates `N = 2^(OSVR+1)` consecutive conversions of its tapped
channel, then presents either the average (native 12-bit, noise-reduced) or the
oversampled sum (wider word). The classic oversampling-and-decimation result
applies: for uncorrelated (white) noise the effective resolution improves by
approximately **0.5·log₂(N)** bits (**X** — this is the standard ΣΔ/oversampling
relation, also the figure quoted in the ticket).

| OSVR | N | ENOB gain | Effective res. (from 12-bit) | Per-channel rate |
|-----:|--:|----------:|-----------------------------:|-----------------:|
| 2×   | 2   | +0.5 bit | 12.5 bit | 1/2 native |
| 4×   | 4   | +1.0 bit | 13.0 bit | 1/4 native |
| 8×   | 8   | +1.5 bit | 13.5 bit | 1/8 native |
| 16×  | 16  | +2.0 bit | 14.0 bit | 1/16 native |
| 32×  | 32  | +2.5 bit | 14.5 bit | 1/32 native |
| 64×  | 64  | +3.0 bit | 15.0 bit | 1/64 native |
| 128× | 128 | +3.5 bit | 15.5 bit | 1/128 native |
| 256× | 256 | +4.0 bit | 16.0 bit | 1/256 native |

**I** The ENOB numbers are the *theoretical ceiling* for white noise. Real gain is
capped by (a) whether the input noise is actually white and ≥ 1 LSB (oversampling
does nothing if the signal is quieter than the quantizer — you need dither), and
(b) correlated/systematic error (INL, reference noise, ground bounce) which
averaging does **not** remove. Treat the table as an upper bound to be confirmed
empirically (**E** work item in *Recommendation*).

**Contrast with SAMC (#328):** **N** SAMC only sets acquisition/sample time so the
native 12-bit conversion is *accurate* (S/H fully settled). It is not a noise
knob. ADCFLTRn is the first genuine resolution knob on the on-chip ADC — this is
the whole motivation for #332.

---

## 2. Which of our channels could actually use a filter

`CHNLID<4:0>` is **5 bits → addressable range 0–31** (**X** ticket + FRM). This
immediately excludes any of our inputs that live on ADCHS channel numbers > 31.

**NQ1 channel → ADCHS channel map** (**V** `NQ1BoardConfig.c:56–170`):

### Type 1 — dedicated SAR modules (simultaneous, one module each)

| Daqifi ch | ADCHS ch (`CHNLID`) | Module | Filter-tappable? |
|----------:|--------------------:|--------|:---------------:|
| 4  | CH4 | MODULE4 | ✅ (CHNLID=4) |
| 8  | CH0 | MODULE0 | ✅ (CHNLID=0) |
| 10 | CH1 | MODULE1 | ✅ (CHNLID=1) |
| 12 | CH2 | MODULE2 | ✅ (CHNLID=2) |
| 14 | CH3 | MODULE3 | ✅ (CHNLID=3) |

### Type 2 — shared MODULE7 mux scan

| Daqifi ch | ADCHS ch (`CHNLID`) | Filter-tappable? |
|----------:|--------------------:|:---------------:|
| 0  | CH11 | ✅ (11) |
| 9  | CH5  | ✅ (5) |
| 11 | CH6  | ✅ (6) |
| 13 | CH7  | ✅ (7) |
| 15 | CH8  | ✅ (8) |
| 1  | CH24 | ✅ (24) |
| 2  | CH25 | ✅ (25) |
| 3  | CH26 | ✅ (26) |
| 7  | CH27 | ✅ (27) |
| 5  | CH39 | ❌ **out of CHNLID range (>31)** |
| 6  | CH38 | ❌ **out of CHNLID range (>31)** |

**Finding:** 14 of 16 public NQ1 inputs are addressable by a filter unit;
**Daqifi ch5 (CH39) and ch6 (CH38) are physically unreachable** by ADCFLTRn
because their ADCHS channel numbers exceed the 5-bit `CHNLID`. Any client-facing
filter feature must report these two as unsupported. (**I** — derived from the
CHNLID width; worth a one-line bench confirmation that CH38/39 indeed can't be
selected, but the register field width leaves no room for it.)

**Hard resource limit:** only **6 filter units exist**. In an 8- or 16-channel
streaming config we cannot put every user channel on a filter — at most 6
channels can be oversampled simultaneously, each with its own OSVR.

---

## 3. Throughput / ENOB trade-off, and the streaming-timer interaction

The subtle part is *where the N conversions come from*. Our two channel classes
feed the filter very differently, and both collide with the existing trigger
architecture.

### 3a. Type 1 (dedicated module) — the clean case, but not free

**N** Today each Type-1 module converts **once per streaming-timer tick** (TMR5
match hardware-triggers all dedicated modules simultaneously, #282), and the
#541 deferred task reads `ADCDATAx` gated on the per-input `ARDY` flag.

**I** To feed a filter at OSVR=N on a Type-1 channel you need that module to
convert **N times per delivered sample**. Two ways, both with cost:

1. **Decimate a faster stream:** run the streaming timer N× faster and let the
   filter emit one result per N ticks. Cost: the ADC front-end and (worse) the
   whole ISR/deferral machinery run N× faster while the *output* rate is
   unchanged. At the rates where ENOB matters (16×–64×) this pushes the timer/EOS
   machinery straight into the regions #539/#557 documented as fragile. Not
   attractive.
2. **Free-run only the filtered module** on an independent auto/repeat trigger,
   decoupled from TMR5, and read its `ADCFLTR` result when `DATARDY` sets. Cost:
   that module is no longer simultaneously sampled with the other Type-1 channels
   (breaks the #282 "all Type-1 convert on the same edge" guarantee), and the
   filtered channel's timestamp no longer aligns with the streaming tick — it is
   a slower, phase-independent measurement multiplexed into the frame.

**I** Net: a filtered Type-1 channel is best modelled as a **separate,
lower-rate, higher-ENOB measurement** rather than a drop-in replacement for a
streamed channel. Its effective output rate is `f_convert / N`. Example: a
dedicated module converting at ~10 kHz with OSVR=16 delivers a 14-bit result at
~625 Hz.

### 3b. Type 2 (shared MODULE7 scan) — largely impractical

**N** Type-2 channels are visited **once per full MODULE7 scan** (#541 dynamic
CSS). The filter's N conversions of a Type-2 CHNLID therefore require **N full
scans**. At OSVR=16 the filtered channel updates every 16 scans, and the whole
#541 dynamic-scan-list / scan-busy cap (`MC12b_HardwareScanMaxFreq`,
**V** `MC12bADC.c:382+`) governs the achievable scan rate. This stacks two rate
divisions (scan length × oversample ratio) and interacts with the mid-stream
CSS-rebuild logic that is explicitly forbidden from changing during a session
(#116/#541).

**I** Oversampling a Type-2 channel is *possible* but the rate cost is severe and
the architectural coupling to the scan cap is high. Recommend **Type-1 channels
only** for a first implementation.

### 3c. Latency

**X** The first filtered result appears only after N conversions complete:
`t_first ≈ N × t_conv`, where `t_conv` = acquisition (SAMC+2 TAD) + ~14 TAD
conversion (**V** the timing terms used by `MC12b_HardwareScanMaxFreq`,
`MC12bADC.c:418–421`). At OSVR=256 this is a 256-conversion group delay before
the first sample — unusable for anything transient, fine for slow DC metrology.

### 3d. Read-path change

**I** A filtered channel's value comes from the `ADCFLTRy` data register with its
own `DATARDY`, **not** from `ADCDATAx`/`ARDY`. The #541 deferred-read path
(`MC12b_DrainType1Results`) would need a parallel branch for filtered inputs, and
the `validMask`/`ScanStaleDropped` freeze accounting would need to understand
"result not ready yet is expected at 1/N cadence" so it doesn't count normal
filter latency as a dropped sample.

---

## 4. Interaction with the frequency-cap model

**N** The NQ1 cap is the freeze-aware additive model `Streaming_AdcAdditiveCap_NQ1`
min()'d with the scan-busy bound (repo `CLAUDE.md` "Streaming Frequency Capping").
A filter changes the per-channel conversion cost and (for the decimate approach)
the effective ISR rate, so **any filtered channel would need its own cap term** —
the current model has no ADCFLTR input. This is a non-trivial characterization
task, not a constant tweak. Advertising a filtered channel through the #327
capability schema (which surfaces `current_max_rate_hz`) must wait until that
term is fitted, or the cap will be wrong for filtered sessions.

---

## 5. Recommendation

**Adopt ADCFLTRn as a future opt-in, per-channel, Type-1-only "high-resolution"
mode — not a default, and not in this ticket.** Justification:

1. **Real, unique benefit.** It is the only genuine ENOB knob on the on-chip ADC
   (SAMC is not). +2–3 bits (14–15-bit effective) on a DC/low-frequency channel
   is a meaningful product capability.
2. **But it is fundamentally a rate-for-resolution trade**, and the units are
   scarce (6) and range-limited (CH0–31 only → ch5/ch6 excluded). It cannot be a
   blanket "make everything better" default; it changes the timing contract of
   whatever channel it touches.
3. **Type-1 only for v1.** Type-2 oversampling stacks the scan-rate division on
   top of the oversample division and entangles with the #541 dynamic-scan cap —
   high cost, low payoff. Ship Type-1 first (clean per-module trigger story) and
   revisit Type-2 only if a customer needs it.
4. **Opt-in, explicit, off by default.** Because a filtered channel runs at
   `f/N` and is phase-decoupled from the streaming tick, silently enabling it
   would surprise every existing client. It must be a deliberate configuration
   the client chooses and the capability schema advertises.

### Suggested SCPI surface (design sketch, not built)

Per the project "keep the SCPI surface lean" rule, prefer **extending an existing
`CONFigure:ADC` node** over a new top-level tree. Illustrative:

```
CONFigure:ADC:FILTer <daqifiCh>,<mode>,<osvr>   # mode: AVERage|OVERsample; osvr: 2..256
CONFigure:ADC:FILTer? <daqifiCh>                # -> mode,osvr,unit,ready or OFF
```

- Reject non-Type-1 channels and ch5/ch6 with a SCPI error routed through the log
  (per the "error on config problems" standard rule).
- Reject changes while streaming (mirrors the existing SAMC/CHAN/OBDiag
  mid-stream rejection, #116/#541).
- Keep NQ1/NQ2/NQ3 struct-consistent: add the filter-slot config to the runtime
  ADC struct for all variants even though only MC12bADC (NQ1) implements it;
  NQ2/NQ3 (AD7609/AD7173) have their own on-chip oversampling handled per-chip
  (explicitly out of scope here, per the ticket).

### Follow-up work items (the actual next ticket)

1. **Minimal driver spike** — configure one filter unit on one Type-1 module
   (e.g. Daqifi ch8 = CH0/MODULE0), free-run it, read `ADCFLTR` on `DATARDY`,
   and confirm the accumulated/averaged value against a native `ADCDATA0` read.
   *(This is where the first **E** evidence gets produced.)*
2. **Empirical ENOB measurement** — apply a known DC input plus a small,
   ≥1-LSB AC/noise component (dither); compare sample std-dev at native 12-bit
   vs 16×/64× oversampled. Confirm the +2/+3-bit table above or find the real
   ceiling. Put the script in `daqifi-python-test-suite`.
3. **Cap-model term** — fit an ADCFLTR cost term into
   `Streaming_AdcAdditiveCap_NQ1` before advertising filtered rates via #327.
4. **Read-path integration** — parallel `ADCFLTR`/`DATARDY` branch in the #541
   deferred read, with freeze accounting that tolerates the 1/N cadence.

### RAM / build note

This document is doc-only: **no code, no new statics, no build impact.** Firmware
static-RAM headroom is tight (~500 B above the 8192-byte min-stack floor), so a
real ADCFLTR implementation must budget its per-slot runtime config carefully —
6 filter slots × a small struct is fine, but it should live in the existing
runtime ADC config, not a new standalone buffer.

---

## 6. Open questions to resolve at implementation time

- **X-to-E:** Confirm on silicon that `CHNLID` on a *shared* MODULE7 channel taps
  post-mux samples correctly (the FRM describes the filter tapping a "channel";
  whether that is the S/H input or the module output for scanned inputs should be
  bench-verified before betting the Type-2 story on it).
- Does the dedicated-module free-run trigger (§3a option 2) coexist with the
  boot/idle `MC12b_TriggerConversion` software-trigger path without contention?
- Averaging (`DFMODE=1`, native width, drop-in value) vs oversampling
  (`DFMODE=0`, wider word, needs a scale/format change through the encoder and
  `VoltagePrecision` path) — averaging is the lower-integration-cost choice for
  v1 and should be the default mode of the feature.
