# ADC HAL — Direct-SFR-vs-PLIB Audit (2026-07)

Issue: [#333](https://github.com/daqifi/daqifi-nyquist-firmware/issues/333) —
*refactor(adc): audit ADC HAL for PLIB/bitfield use — replace raw SFR writes where safe*

**Audit only. No HAL code is changed by this document.** Conversions land in a
separate, hardware-verified PR. This is the reference list that PR works from.

Firmware audited at commit `475f9187` (branch `feat/589-sick-sd-tier123`).

## Method & scope

Files inspected (per ticket scope):

| File | Raw-SFR sites found |
|------|--------------------:|
| `firmware/src/HAL/ADC.c` | 0 (fully PLIB — `ADCHS_EOSCallbackRegister`, no register pokes) |
| `firmware/src/HAL/ADC/AD7609.c` | 0 (fully PLIB — `GPIO_*`, `DRV_SPI_*`, `CORETIMER_*`, `SYS_CACHE_*`) |
| `firmware/src/HAL/ADC/MC12bADC.c` | all sites below |
| `firmware/src/config/default/interrupts.c` | MCC-owned; hand-added ADC_DATAx body already uses PLIB (`ADCHS_ChannelResultIsReady`/`Get`) |
| `firmware/src/config/default/peripheral/adchs/plib_adchs.{c,h}` | MCC-owned, reference only (out of scope for edits) |

**Evidence tags** (per CLAUDE.md debugging discipline): **V** = verified from
primary source (device header / FRM / PLIB header), **E** = empirical (grep /
build), **I** = inference.

**PLIB surface available** (V — `plib_adchs.h` @ `475f9187`): `ADCHS_Initialize`,
`ADCHS_ModulesEnable`/`Disable`, `ADCHS_ChannelConversionStart`,
`ADCHS_Channel{ResultIsReady,ResultGet,ResultInterruptEnable/Disable,EarlyInterruptEnable/Disable}`,
`ADCHS_GlobalEdge/LevelConversionStart`, `ADCHS_GlobalLevelConversionStop`,
`ADCHS_{Callback,EOSCallback}Register`. **There is no PLIB API** for: factory
calibration copy, `ADCCSSx` scan-list (online update), `ADCTRGx` per-channel
trigger source, `ADCCON1.STRGSRC`, `ADCxTIME.SAMC` / `ADCCON2.SAMC`,
`ADCCON1.ON`, or the `ADCCON3.TRGSUSP/UPDRDY` online-update handshake. For those,
the CLAUDE.md order-of-preference bottoms out at **tier 2 (SFR bitfield
accessor)** or, where the register is a 32-bit blob/bitmap, **tier 3 (raw
register)** with a justifying comment.

**Bitfield accessors confirmed to exist in the device header** (V —
`p32mz2048efm144.h`, DFP 1.5.173): `ADCCON1bits.STRGSRC:5`,
`ADCCON2bits.ADCDIV:7`, `ADCCON2bits.SAMC:10`, `ADCCON3bits.CONCLKDIV:6`,
`ADCCON3bits.TRGSUSP`, `ADCCON3bits.UPDRDY`. This is what makes the "safe
convert" rows below actually available.

## Site table (MC12bADC.c)

Legend — **PLIB-able?**: `bitfield` = tier-2 accessor exists; `no` = neither PLIB
fn nor a meaningful bitfield (blob/bitmap register); `bitfield(used)` = accessor
already used for the *same* field elsewhere in this file. **Rec**: `CONVERT` =
safe tier-3→tier-2 cleanup; `KEEP` = justified raw, leave (add `// raw:` comment
if none present).

| # | Line | Current form | What it does | PLIB-able? | Rec | Risk |
|--:|-----:|--------------|--------------|------------|-----|------|
| 1 | 86–88 | `gSavedADCTRG[n] = ADCTRG1/2/3` | Save whole trigger regs (PLIB boot defaults) for restore | no (whole-reg save) | KEEP | none — reading a full register to snapshot it is the correct form |
| 2 | 89 | `(ADCCON1 >> ADCCON1_STRGSRC_SHIFT) & MASK` | Read STRGSRC to save scan-trigger default | `bitfield` | **CONVERT** → `ADCCON1bits.STRGSRC` | very low — read only; removes hand-maintained `SHIFT=16`/`MASK` constants (the #328 field-decode hazard) |
| 3 | 92–97 | `ADC0CFG = DEVADC0; …ADC7CFG = DEVADC7` | Copy factory calibration `DEVADCn` → `ADCnCFG` | no (32-bit blob copy, no PLIB API) | KEEP + verify | low — **check whether `ADCHS_Initialize()` already does this** (ticket hot-spot). If MCC init copies cal, this is dead/redundant; if not, it is required raw. Add `// raw: factory cal blob, no PLIB/bitfield form` either way |
| 4 | 114–119, 149–153 | `ADCANCONbits.ANENn = 1` | Enable analog-bias clock per module | tier-2 already; no PLIB wrapper | KEEP | none — already the recommended bitfield tier |
| 5 | 121–126 | `while(!ADCANCONbits.WKRDYn);` | Spin until module wake-ready | tier-2 already | KEEP | none (bitfield read; bounded by hardware wake, ~µs) |
| 6 | 336 | `if (ADCCSS1 == css1 && ADCCSS2 == css2)` | Skip redundant scan-list write | no (bitmap register) | KEEP | none — each bit is a channel; bitmap compare is correct |
| 7 | 342, 347, 352 | `ADCCON3bits.TRGSUSP = 1/0` | Suspend/resume triggers for online CSS update | tier-2 already | KEEP | none — FRM-documented handshake, already bitfield |
| 8 | 344 | `ADCCON3bits.UPDRDY == 0` | Poll update-ready during CSS update | tier-2 already | KEEP | none |
| 9 | 350–351, 356–357 | `ADCCSS1 = css1; ADCCSS2 = css2` | Write shared-scan channel-select bitmap | no (bitmap; no PLIB online-CSS API) | KEEP | none — justified raw, already commented (lines 286–290 cite FRM DS60001344E ADCCON3 §Update-Ready). Bitmap is the natural representation |
| 10 | 382 | `(ADCCON3 >> 24) & 0x3F` | Read CONCLKDIV for TAD calc | `bitfield` | **CONVERT** → `ADCCON3bits.CONCLKDIV` | very low — read only |
| 11 | 383 | `ADCCON2 & 0x7F` | Read ADCDIV for TAD calc | `bitfield` | **CONVERT** → `ADCCON2bits.ADCDIV` | very low — read only |
| 12 | 385 | `(ADCCON2 >> 16) & 0x3FF` | Read shared SAMC for TAD/busy calc | `bitfield(used)` | **CONVERT** → `ADCCON2bits.SAMC` | very low — the *same field* is already read via `ADCCON2bits.SAMC` at line 658; mixing raw shift with the accessor for one field is exactly the inconsistency #328 warns about |
| 13 | 578–580 | `ADCTRG1/2/3 = trg[n]` | Write per-channel trigger sources (assembled via field-precise `SetChannelTrigSrc` helper from saved PLIB defaults) | no (no PLIB `TriggerSourceSet`; per-field masking done in helper) | KEEP | low — the helper (lines 506–513) already does field-precise mask/overlay and preserves PLIB defaults; expanding to per-`TRGSRCn` bitfield writes adds no safety and loses the "overlay onto saved defaults" pattern. Well-commented |
| 14 | 585–588 | `con1 = ADCCON1; con1 &= ~(MASK<<SHIFT); con1 \|= scanSrc<<SHIFT; ADCCON1 = con1` | RMW STRGSRC (scan trigger source) | `bitfield` | **CONVERT** → `ADCCON1bits.STRGSRC = scanSrc` | low — **highest-value site.** Hand-rolled RMW with the manually-maintained `ADCCON1_STRGSRC_SHIFT=16` is the precise class of bug #328 hit (SAMC mis-decode). Config-time, single-writer (not streaming) → the non-atomic bitfield RMW is safe here |
| 15 | 596 | `(ADCTRG1 & MASK) == TMR5` | Query: is dedicated HW trigger on? | `bitfield` (`ADCTRG1bits.TRGSRC0`) | CONVERT (optional) | very low — read only; lower priority than the STRGSRC cluster |
| 16 | 600 | `(ADCCON1 >> SHIFT) & MASK == TMR5` | Query: is shared HW trigger on? | `bitfield` | **CONVERT** → `ADCCON1bits.STRGSRC` | very low — read only; pairs with #2/#14 (retire the shift/mask constants together) |
| 17 | 621, 623, 639, 646 | `ADCCON1bits.ON` (read + write) | Toggle ADC off/on around SAMC write | tier-2 already | KEEP | none — bitfield; #328/#329 exemplar |
| 18 | 628–635 | `ADCxTIMEbits.SAMC = s`, `ADCCON2bits.SAMC = …` | Set dedicated/shared acquisition time | tier-2 already | KEEP | none — the #329 fix that motivated this whole ticket; already ideal |
| 19 | 644–645 | `ADCCON2bits.BGVRRDY`, `ADCCON2bits.REFFLT` | Poll bandgap/reference ready after re-enable | tier-2 already | KEEP | none |

## Prioritized recommendations

### Safe conversions (do in the follow-up PR) — all tier-3 → tier-2, all in `MC12bADC.c`

These share one theme: **replace hand-maintained shift/mask on `ADCCON1.STRGSRC`,
`ADCCON2.ADCDIV/SAMC`, `ADCCON3.CONCLKDIV` with the device-header bitfield
accessors that already exist** (V). None is on the streaming hot path (all are
config-time or the cap-math helper), so there is no codegen/perf concern, and all
but one are read-only.

1. **Site #14 (line 585–588) — `ADCCON1bits.STRGSRC = scanSrc`.** Highest value:
   it is the only *write* in this group and the exact error-prone RMW pattern
   from #328. After conversion, delete the now-unused `ADCCON1_STRGSRC_SHIFT`
   uses at sites #2/#16/#600.
2. **Sites #10/#11/#12 (lines 382/383/385)** in `MC12b_HardwareScanMaxFreq` —
   `ADCCON3bits.CONCLKDIV`, `ADCCON2bits.ADCDIV`, `ADCCON2bits.SAMC`. Site #12
   also removes an in-file inconsistency (same field read both ways).
3. **Sites #2/#16 (lines 89/600), optional #15 (line 596)** — read-side STRGSRC
   / TRGSRC queries. Convert alongside #14 so the `_SHIFT`/`_MASK` constants can
   be retired.

Once #2/#14/#16 land, the file-local `ADCCON1_STRGSRC_SHIFT` constant (line 72)
has no remaining users and should be removed; `ADCTRG_FIELD_MASK` stays (still
used by the `ADCTRGx` helper, site #13).

### Leave as-is (justified raw / already ideal)

- **Bitmap registers — `ADCCSS1/2` (sites #6/#9).** No bitfield decomposition is
  meaningful (each bit = one channel); no PLIB online-CSS API exists. Already
  documented with an FRM citation.
- **Trigger-register writes — `ADCTRGx` (site #13).** The `SetChannelTrigSrc`
  helper already does field-precise masking and overlays onto saved PLIB
  defaults; no PLIB `TriggerSourceSet` exists. Converting adds no safety.
- **Factory-cal copy — `ADCnCFG = DEVADCn` (site #3).** 32-bit blob, no PLIB/
  bitfield form. **One action:** confirm whether `ADCHS_Initialize()` already
  performs this copy (ticket hot-spot). If yes → remove as redundant; if no →
  keep and add a `// raw: factory cal blob, no PLIB form` comment. (I — needs a
  read of the MCC-generated `plib_adchs.c` init body to settle; not done here to
  stay audit-only.)
- **All existing bitfield accessors (sites #4/#5/#7/#8/#17/#18/#19).** Already at
  tier 2, which is the recommended tier where no PLIB wrapper exists. `ADCxTIME`/
  `ADCCON2.SAMC` (#18) is the #329 exemplar this ticket cites.
- **`ADC.c`, `AD7609.c`, `interrupts.c` hand-added ADC code.** Already fully
  PLIB — nothing to convert.

## Non-goals honored

- No HAL code modified (conversions are a separate, hardware-verified PR per the
  ticket).
- No MCC-generated `plib_adchs.c` edits proposed.
- No perf-sensitive ISR/streaming raw writes recommended for change — the safe
  set is entirely config-time or the cap-math helper, none in the timer/EOS hot
  path.
- Errata-ordered sequences (the `ADCCON3.TRGSUSP → UPDRDY → ADCCSSx → resume`
  online-update handshake, sites #7–#9) are left intact.

## Summary

- **3 files clean** (`ADC.c`, `AD7609.c`, plus the hand-added `interrupts.c` ADC
  body) — 0 raw-SFR sites.
- **19 SFR sites in `MC12bADC.c`**: 13 KEEP (justified raw or already tier-2
  bitfield), **~5–6 safe CONVERT** (all `STRGSRC`/`ADCDIV`/`CONCLKDIV`/`SAMC`
  shift-mask → existing bitfield accessors; config-time, low risk), **1 verify**
  (factory-cal redundancy vs `ADCHS_Initialize`).
- Net: the HAL is already in good shape; the follow-up PR is a small, focused
  bitfield-accessor cleanup centered on `ADCCON1.STRGSRC`, not a rewrite.
</content>
