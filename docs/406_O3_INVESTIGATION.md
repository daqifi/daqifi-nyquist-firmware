# #406 / #421 — Five Type-2 ADC channels stream zeros (resolved)

**Status:** fixed in `firmware/src/HAL/ADC/MC12bADC.c::MC12b_ConfigureHardwareTrigger`. See the commit titled `fix(adc): TRGSRC=3 (STRIG) for shared MODULE7 channels` for the patch.

**Datasheet sources:** `reference/PIC32MZ/Section22._12-bit_HS_SAR_ADC_FRM_DS60001344E.pdf`. Specific citations below.

**Repro tools:** `tools/diagnostics/406_zero_channels/` — kept in-tree for future re-use.

## Symptom

On NQ1, with 16 channels enabled and streaming as CSV at any rate, five channels reported zero values:

| User-channel | ADCHS hardware channel | ADCHS module |
|---|---|---|
| 0  | CH11 | MODULE7 (shared mux scan) |
| 9  | CH5  | MODULE7 |
| 11 | CH6  | MODULE7 |
| 13 | CH7  | MODULE7 |
| 15 | CH8  | MODULE7 |

Other shared-module channels (user-channels 1/2/3/5/6/7 → ADCHS CH24/25/26/39/38/27) worked correctly. All Type 1 dedicated-module channels (user 4/8/10/12/14 → ADCHS CH4/0/1/2/3) worked correctly.

The data-ready ISR for each broken hardware channel fired **exactly once per stream session** — confirmed by `gAdcIsrCount_5/6/7/8/11` (added in this PR for diagnostic purposes). The single fire was the implicit pulse during ADCANCON wakeup at the start of each stream. After that, the ADCHS scan-trigger reaching MODULE7 never produced a conversion for those five channels.

## Root cause

The PIC32MZ ADCHS divides analog inputs into three classes (FRM Section 22 page 22-32, ADCCSS register Notes):

- **Class 1 / Class 2** — hardware channels 0-11 (sometimes via alternate inputs to 12-31). Each has a per-channel `TRGSRC<4:0>` field in `ADCTRG1/2/3`. **The per-channel field is the actual trigger source for that channel.**
- **Class 3** — hardware channels 32-63 (and overflow inputs from 12-31). No per-channel trigger field — these always follow the module's `STRGSRC<4:0>` scan-trigger source.

For a Class 1/2 input to participate in MODULE7's shared mux scan, **two things** must be true:

1. Its bit is set in `ADCCSS1/2`.
2. Its per-channel `TRGSRC` is set to `0b011` (STRIG, value 3 in decimal) — defined in FRM Register 22-19 (page 22-38).

ADCCSS1 Note 2 (page 22-32, verbatim):

> "If a Class 1 or Class 2 input is included in the scan by setting the CSSx bit to '1' and by setting the TRGSRCx<4:0> bits to STRIG mode ('0b011'), the user application must ensure that no other triggers are generated for that input using the RQCNVRT bit in the ADCCON3 register or the hardware input or any digital filter."

The Microchip-generated `plib_adchs.c::ADCHS_Initialize()` boots `ADCTRG2 = 0x01010100` and `ADCTRG3 = 0x01000001` — that puts CH5/6/7/8/11 at TRGSRC=1, which is **GSWTRG** (Global Software Edge), not STRIG. With TRGSRC=GSWTRG, those channels convert only when `ADCCON3.GSWTRG=1` is pulsed via `ADCHS_GlobalEdgeConversionStart()`.

Pre-#282 (commit f114f44e, 2026-04-14), the streaming deferred task explicitly called `ADCHS_GlobalEdgeConversionStart()` each cycle, which by coincidence matched the PLIB's TRGSRC=GSWTRG and made the channels convert on every scan period. PR #282 introduced hardware-trigger sync — `STRGSRC=TMR5` on MODULE7 — and removed the manual GSWTRG call when both Type 1 and Type 2 used hardware triggers. From that commit forward, the per-channel TRGSRC=GSWTRG override on CH5/6/7/8/11 silently took priority over STRGSRC=TMR5 and waited for a software pulse that never came.

CH24+ never had this problem because they're Class 3 — no per-channel TRGSRC field exists for them, so they always followed STRGSRC.

## Fix

In `MC12b_ConfigureHardwareTrigger`, after the dedicated-channel TMR5 overlay loop, walk every shared-module channel with a per-channel TRGSRC slot (HW channels 0-11, ChannelType=2) and force `TRGSRC=3` (STRIG):

```c
for (size_t i = 0; i < pCfg->AInChannels.Size; i++) {
    const AInChannel* ch = &pCfg->AInChannels.Data[i];
    if (ch->Type != AIn_MC12bADC) continue;
    if (ch->Config.MC12b.ChannelType == 1) continue;  // Type 1 already TMR5
    uint8_t chId = ch->Config.MC12b.ChannelId;
    if (chId > 11) continue;                          // CH12+ no per-ch field
    SetChannelTrigSrc(trg, chId, ADC_TRGSRC_STRIG);   // value 3 per FRM 22-38
}
```

Reverted at stream-stop together with the rest of the trigger config so non-streaming ADC reads keep working with the MHC-configured trigger sources.

## Verification

Production -O3 build, NQ1 (serial `7E2898F46200E8A7`), 16-channel CSV stream, 4 s window, 10 trials each. `tools/diagnostics/406_zero_channels/` scripts.

| State | Pre-fix | Post-fix |
|---|---|---|
| USB stream, post-flash, no WiFi | 0/10 (or accidental 10/10 with TRGSRC=4 — see below) | 9/10 (1-row warmup transient on trial 1, rest perfect) |
| USB stream, WiFi STA associated to AP | 0/10 | **10/10** |
| WiFi TCP stream | 0/10 | **10/10** |

For the skew bounds and rate-impact discussion in the next section to be load-bearing, the fix needs to actually work at the rates listed. High-rate verification on the same hardware (16 enabled user channels + 3 monitoring = 19 CSS slots, 2 s capture per rate, chunked-read during stream to avoid buffer-tail-only reads, body rows after warmup transient):

| Rate    | Expected rows | Rows captured | Capture % | min nz | max nz | mean nz | Verdict   |
|--------:|--------------:|--------------:|----------:|-------:|-------:|--------:|-----------|
| 5 kHz   | 10,000        | 9,863         | 99%       | 16     | 16     | 16.00   | **FIXED** |
| 10 kHz  | 20,000        | 9,957         | 50%       | 16     | 16     | 16.00   | **FIXED** |
| 13 kHz  | 26,000        | 9,988         | 38%       | 16     | 16     | 16.00   | **FIXED** |

Run via `python3 tools/diagnostics/406_zero_channels/highrate_verify.py "$(bash tools/diagnostics/406_zero_channels/find_bench_device.sh)"` to resolve the correct CDC node automatically (the bench inventory in CLAUDE.md notes that hardcoding `/dev/ttyACM0` can target the wrong board when multiple DAQiFis are attached). 13 kHz is at or above the firmware-enforced 16-channel cap from `Streaming_ComputeMaxFreq`; pushing higher returns capped frequencies and the cap landing depends on enabled-channel mix (see CLAUDE.md "Streaming Frequency Capping"). The capture % drops above 5 kHz because the host-side serial transport saturates around ~5000 rows/s through USB CDC ACM on this bench setup; **what matters is that every body row that does land carries all 16 enabled channels**, confirming the scan continues to enroll every CSS-selected slot at rates well above the 1 kHz bench default. The Type 1 vs last-Type-2 spread bound (~40.3 µs at the MHC-default SAMC=100, see "Inter-type timing relationship" below) holds at all three rates.

Per-channel ISR counts in a 4 s session at 50 Hz post-fix:

```
CH5=216  CH6=216  CH7=216  CH8=216  CH11=216 | CH24=216  CH38=216
```

All scan-list channels fire equally — every TMR5 trigger drives one MODULE7 scan that visits every CSS-selected slot.

**Type 1 ↔ Type 2 skew bounds (calculated from datasheet timing, full derivation in "Inter-type timing relationship" below):**

| Quantity                               | Formula                       | NQ1 default config (19 CSS slots, SAMC=100, ADC_CLK=50 MHz) |
|----------------------------------------|-------------------------------|-------------------------------------------------------------|
| Slot conversion time                   | `(SAMC + 12) × T_AD`          | `112 × 20 ns = 2.24 µs`                                     |
| MODULE7 scan completion time           | `N_CSS × slot`                | `19 × 2.24 µs = 42.56 µs`                                   |
| Type 1 vs first Type 2 skew            | `~0`                          | coincident at ~2.24 µs post-trigger                         |
| **Type 1 vs last Type 2 skew (worst)** | `(N_CSS − 1) × slot`          | `18 × 2.24 µs = 40.32 µs`                                   |

The MHC-generated `plib_adchs.c::ADCHS_Initialize()` programs `ADC0TIME=0x3010064` … `ADC4TIME=0x3010064` (dedicated modules) and `ADCCON2=0x642001` (shared MODULE7), both encoding `SAMC=0x064=100` in their `SAMC<9:0>` field. SAMC is runtime-tunable via `SYST:CONF:ADC:SAMC` (see `MC12b_SetAcquisitionSamc` in `firmware/src/HAL/ADC/MC12bADC.c`); the bounds above are for the boot default. Lowering SAMC tightens slot conversion time toward the FRM's recommended-minimum sample-and-hold accuracy floor (see DS60001344E §22.10 ADC Sampling Requirements).

Bounds scale linearly with `N_CSS` (count of bits set in `ADCCSS1 ∪ ADCCSS2`); reduce CSS slots to tighten the spread. SAMC tradeoff covered in "Inter-type timing relationship" → "Sample-rate impact" below.

Pre-fix on the same firmware after STA association:

```
CH5=1    CH6=1    CH7=1    CH8=1    CH11=1   | CH24=33   CH38=33
```

Five-fire-once is the canonical "per-channel TRGSRC waiting for a trigger source that's not being driven" signature.

## Inter-type (Type 1 vs Type 2) timing relationship

The fix enrols Type 2 channels via `TRGSRC=STRIG`, which makes them follow MODULE7's scan trigger (`STRGSRC=TMR5` during streaming). This is the only way per the FRM to include a Class 1/Class 2 input in the shared-module scan — see ADCCSS1 Note 2 quote above. A few timing implications worth surfacing for downstream consumers:

- **Type 1 channels (dedicated MODULE0-4)** convert in parallel. They're all triggered simultaneously on the TMR5 edge and all complete in `(SAMC + 12) × T_AD` ≈ **2.24 µs** at the MHC-generated default `SAMC=100` and `ADC_CLK=50 MHz` (`T_AD = 20 ns`). Per-channel completion is identical to within hardware tolerance.
- **Type 2 channels (shared MODULE7 mux)** are visited serially per scan. Each slot takes the same `(SAMC + 12) × T_AD` ≈ **2.24 µs**, but completion time is offset by the channel's position in the CSS scan order. With N channels in `CSS1 ∪ CSS2`, the last Type 2 channel completes at approximately `N × 2.24 µs` after the trigger edge.
- **Worst-case Type 1 vs Type 2 skew** at the 16-channel NQ1 default (CSS1=`0xef0809e0`, CSS2=`0x16c1` → 13 + 6 = **19 scan slots**, including monitoring channels): `(19 − 1) × 2.24 µs ≈ 40.3 µs` between the simultaneous Type 1 completion and the last Type 2 sample. The first Type 2 channel and Type 1 channels are coincident (within hardware tolerance) at `~2.24 µs` post-edge.

This serial scanning behaviour is **inherent to MODULE7 mux operation** (FRM Section 22 "ADC Module Configuration") and is unchanged from pre-#282 code — the original `ADCHS_GlobalEdgeConversionStart()` path also serialised Type 2 channels through MODULE7. The `TRGSRC=STRIG` fix restores that pre-#282 behaviour by routing them through the documented scan-trigger path instead of via the side-channel they relied on before.

PR #282's "deterministic synchronization" claim refers to the **Type 1 ↔ Type 2 trigger alignment** (both fire on the same TMR5 edge), not to per-channel completion alignment. That property is preserved by this fix: the trigger edge is shared; only the completion-time spread of Type 2 channels depends on scan ordering, and that spread is bounded by `(N − 1) × (SAMC + 12) × T_AD` and known at config time from CSS1/CSS2 + the SAMC programmed in `ADCxTIME`/`ADCCON2`.

If a downstream user needs sub-µs Type 1 ↔ Type 2 alignment, the only path is interleaving Type 2 inputs onto dedicated modules (AN5-AN11 alternate inputs to MODULE0-4 via `ADCTRGMODE.SHxALT` — see FRM Section 22 page 22-3 Figure 22-1). That is out of scope for this fix; the bug being addressed is "channels report zero", not "channels are 4 µs apart from Type 1".

### Sample-rate impact

For the NQ1 default 16-channel + monitoring config (19 scan slots, ~42.6 µs scan time at the MHC-generated `SAMC=100`), the Type 2 spread becomes a progressively larger fraction of the streaming period as rate increases:

Two distinct timing numbers — keep them separate:

- **Scan completion time** = `N × (SAMC + 12) × T_AD` ≈ `19 × 2.24 µs` ≈ **42.6 µs** (the time from TMR5 edge until MODULE7's *last* slot's data-ready latch sets).
- **Type 1 vs last-Type-2 spread** = `(N − 1) × 2.24 µs` ≈ **40.3 µs** (the time between Type 1 channels' simultaneous completion at ~2.24 µs and the last Type 2 channel's completion at ~42.6 µs — i.e. one slot less than the scan time, because Type 1 and the first Type 2 slot finish coincidentally).

| Streaming rate | Period | Scan / period | Type1↔last-T2 spread | Notes |
|---|---|---|---|---|
| 1 kHz | 1000 µs | 4.3% | ~40.3 µs | Negligible |
| 2 kHz | 500 µs | 8.5% | ~40.3 µs | Negligible |
| 5 kHz | 200 µs | 21% | ~40.3 µs | Becoming significant |
| 10 kHz | 100 µs | 43% | ~40.3 µs | Significant fraction of period |
| 13 kHz | 77 µs | 55% | ~40.3 µs | Firmware-cap territory for 16ch |
| ≥23.5 kHz | ≤42.6 µs | ≥100% | (scan ≥ period) | Hardware impossible — firmware caps below this |

Spread is **fixed at ~40.3 µs** regardless of streaming rate at the boot SAMC=100 default (it's set by SAMC and CSS slot count, not by trigger period). What changes with rate is whether that fixed spread is a small fraction of the inter-sample interval (≤2 kHz) or a meaningful fraction (≥5 kHz). For applications that need tighter Type 2 alignment, drop SAMC via `SYST:CONF:ADC:SAMC` — at the FRM-recommended minimum SAMC=0, slot time falls to 240 ns and the 19-slot spread to ~4.3 µs (10× tighter), at the cost of S/H accuracy on high-impedance sources.

The firmware enforces upper bounds via `Streaming_ComputeMaxFreq` (CLAUDE.md "Streaming Frequency Capping" section) — practical caps land between 5-13 kHz depending on enabled-channel mix, all of which sit below the ~23.5 kHz hardware ceiling where the scan would exceed the trigger period. To push spread below ~4 µs at any rate, reduce CSS scan list size (disable monitoring channels via `OnboardDiagEnabled=0` or trim user channels), or reduce SAMC (impacts S/H accuracy — see FRM Section 22 page 22-117 ADC Sampling Requirements).

## Hypotheses ruled out (and why they looked plausible)

The investigation went through several wrong turns before finding the real bug. Listed here so future-you doesn't repeat them:

1. **`-O3` miscompile in ADC.c** — bisecting per-file optimization levels (`-O0`/`-O1`/`-O2`/`-O3`) on `ADC.c` produced 0/10 every level. Same for `interrupts.c` at -O1. The bug had nothing to do with `ADC.c`'s compilation.
2. **`-fipa-icf` folding identical handler bodies** — `xc32-nm` symbol dump shows `ADC_DATA5_Handler` … `ADC_DATA38_Handler` at distinct addresses spaced 76 bytes apart. ICF didn't fold them.
3. **Per-file -O1 on `MC12bADC.c`** — appeared to fix it 10/10 in one early test, broke 0/10 after `sta_setup.batch`. The "fix" was an artifact of testing immediately post-flash with NVM wiped, where the device defaults to AP mode with no traffic and the bug pattern is masked by some boot-state luck. Real fix is below the optimizer level.
4. **WiFi STA association is the trigger** — appeared true for several test cycles. Actually false: the bug is always present whenever any user-channel maps to ADCHS CH5/6/7/8/11; it just appeared correlated with WiFi state through a TMR1 side-channel artifact (next item).
5. **Audit-branch `TRGSRC=4` patch** — looked like the right shape (force a per-channel trigger source) but the wrong value. Per the FRM, value 4 is TMR1 trigger. With TRGSRC=4, CH5/6/7/8/11 would convert on every TMR1 (FreeRTOS tick, 1 kHz) edge — fast enough to look like a fix on USB streaming with no other CPU pressure. WiFi STA association adds enough WINC SPI traffic to disturb TMR1's regular cadence and the side-channel collapses, recreating the broken pattern. This is the source of the "WiFi association breaks it" red herring above.
6. **Reverting global -O3 → -O1** — confirmed the bug is independent of optimization level (broken 0/10 on both). Reinforced #1; not the level we should be fighting at.

## Open follow-ups

- **PR #420's `T * volatile` audit on shared static pointers in `ADC.c`** was made under the assumption the ch15 regression was a -O3 miscompile of pointer access. With the actual bug fixed, those `volatile` annotations may not be load-bearing. Worth re-evaluating in a separate audit. Issue #421 tracks the broader audit; this fix unblocks revisiting #420's specific cases.
- **CH15 specifically** (user-channel 15 → ADCHS CH8) — the original symptom that started this investigation chain. Is now fixed by this patch; verify against the original #354 reproduction once and close.
- **Throughput characterisation tables in CLAUDE.md** (Sessions 18-22) were measured at -O3 *without* this fix. Numbers should still be correct because dedicated channels and CH24+ scan channels were never affected, but it's worth a one-shot regression run on each row. Add as `STR:STATS?`-driven CI.
- **Datasheet PDFs** are local-only in `reference/PIC32MZ/` (45 files, ~41 MB). Not committed to git — copy them in if you need to read offline.
