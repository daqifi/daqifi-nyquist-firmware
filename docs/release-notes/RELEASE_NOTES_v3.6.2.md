# v3.6.2 — battery-boot fix (#564) + NQ1 freeze-aware ADC caps & scan-stale counter (#557/#563)

Patch release over v3.6.1. The headline is a fix for a device that **would not power on from the button when running on battery alone** (no USB). The rest is an ADC streaming-cap correction for NQ1 plus a new data-integrity counter. Normal USB/WiFi/SD streaming is unaffected except where the NQ1 caps were previously wrong (see below).

## Highlights

### Fixed: device won't power on from the button on battery (#564)
On battery-only power, pressing the button to turn the device on could fail — the white LED would light for ~1 s and the device would immediately power back down. Root cause: `Power_UpdateStatusFromGPIO` **synthesized** the BQ24297's `vsysStat` low-battery flag from the PIC's VBATT ADC (`vsysStat = battVoltage < 3.0 V`). At cold boot the VBATT ADC reads a stale **0 V** before it has sampled, so the synthesized `vsysStat` latched to "battery critically low" → `Power_HasSufficientPower()` refused the power-up. USB masked it (the GPIO `pgStat` path satisfied the check), so it only bit on battery.

**Fix:** read the **real** `vsysStat` from the BQ24297 over I²C (REG08 bit 0) — the BMIC's authoritative measurement — instead of inferring it from an unsampled ADC. A healthy battery now correctly reads `vsysStat = 0` and powers up.

Hardening that came with it:
- **`chargePct` is now a signed tri-state** (`int16_t`): `-1` = UNKNOWN (no valid VBATT reading yet) vs a real `0..100`. A stale `0 V` ADC reading no longer masquerades as "0 %, dead battery" — it reads UNKNOWN, and the critical-battery decision falls back to the BQ's `vsysStat`. `battVoltageValid` is tracked per-reading (cleared when the reading is invalid/missing).
- The protobuf `batt_status` field clamps the `-1` sentinel to `0` for wire compatibility.

### Changed: NQ1 freeze-aware ADC streaming caps (#557/#563)
The enforced max streaming rate for **NQ1** now uses a freeze-aware **additive ADC model** fitted to drop-aware sweeps, replacing the old tick-budget / aggregate-event terms. The prior sweeps were drop-blind and counted frozen scanned data as clean, so the old caps were simultaneously **over-conservative on Type 1** channels and **inflated (unsafe) on Type 2** channels. The SAMC/divider-dependent hardware **scan-busy** bound (#539) is still applied as a `min()` so a non-default `CONF:ADC:SAMC` can't push the cap past the real scan-retrigger limit. NQ2/NQ3 are unchanged (no MODULE7 scan).

### Added: scan-stale drop counter (#557/#563)
A new `SYST:STR:STATS? ScanStaleDropped` field counts streaming ticks where the shared MODULE7 scan was armed but its end-of-scan hadn't fired by the next hardware trigger (scan-busy / frozen data, the #539 condition) — an edge-safe sequence counter, gated to the HW-trigger-every-tick path. It reads ~0 with the rate cap in place and turns otherwise-silent frozen data into a visible, accounted metric. It is reported separately (like `EosOverruns`), **not** folded into `SampleLossPercent` (the stale sample was still streamed, just with frozen data).

## All changes since v3.6.1
- **#564** — `fix(power)`: boot on battery — read `vsysStat` from the BQ, not the stale ADC; `chargePct` tri-state UNKNOWN; per-reading `battVoltageValid`.
- **#563/#557** — `feat(adc)`: NQ1 freeze-aware additive ADC cap + `ScanStaleDropped` counter; SAMC scan-busy bound preserved via `MC12b_HardwareScanMaxFreq`.
- **#565** — `chore(release)`: bump `FIRMWARE_REVISION` to 3.6.2.

## Validation (hardware, NQ1 `7E2898F46200E8A7`)
- **#564 on the combined release build:** `vsysStat` read live from the BQ (`REG08 VSYS=0`, healthy); `chargePct` reads `-1` UNKNOWN when the VBATT ADC is unsampled (not a false 0 %); device reaches POWERED_UP cleanly (no power-state-machine regression).
- **#564 field-confirmed on the shipped v3.6.2 build:** the device now powers on properly from the button on battery alone (no USB) — the original failing symptom, verified fixed.
- **#563 on the combined release build:** NQ1 additive cap computes `7453 Hz` for 2×T2; streaming runs clean; `ScanStaleDropped ≈ 0` (4 over ~900 k ticks); `EosOverruns=0`; the `TimerISRCalls == TotalSamples + QueueDropped` invariant (#265) holds exactly; error queue clean.

## Upgrade notes
- Standard in-app update (the desktop app selects the `.hex` asset) or manual flash.
- Every flash wipes NVM (WiFi / calibration / voltage-precision settings) — re-save after updating.
