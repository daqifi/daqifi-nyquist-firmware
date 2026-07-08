# DAQiFi Nyquist Firmware v3.7.0

**Release date:** 2026-07-08
**Baseline:** v3.6.3

## Highlights

- **Higher streaming ceilings at 252 MHz.** The per-interface transport caps
  are re-fitted to the 252 MHz two-night endurance basis (#595): WiFi
  single-channel ProtoBuf +50–55% (5175 → 8000 Hz), USB 1×T1 +2% (15000 →
  15315 via the 22 kHz ISR ceiling), SD 1×T1 +9.5% (9000 → 9852), USB 5×T1
  +11%. Validated by a 42-cell at-cap campaign (600 s soaks, freeze-aware
  loss gates: 42/42 PASS, zero loss). NQ2/NQ3 ceilings are unchanged
  (variant-aware capabilities export).

- **SPI-mode SD card compatibility protection** (#589/#594). Some microSD
  cards drive their data-out line permanently (never tri-stating), which
  crushes the shared SPI4 bus and silently kills WiFi. The firmware now:
  - detects the jam (passive + active + cross-CS + MISO-fight probes),
  - attempts a documented release ritual, then quarantines the SD subsystem,
  - reports it: QUES bit 13 (8192, "SPI Bus Fault"), `SYST:DIAG:SPIBus?`
    family, and a definitive log advisory when a card is present on an
    unreachable-WiFi bus ("the card is likely bus-incompatible; remove it"),
  - clears quarantine via `SYST:STOR:SD:ENAble 1` after card removal/reseat.
  See the SD-Card-Compatibility wiki page for the electrical root cause.

- **WINC firmware-update rescue path hardening** (#592). The FW-update
  bridge now arms even when the WiFi module fails driver bring-up (dead or
  blank module firmware), with bounded tick-deadline waits that never sleep
  while holding the WiFi state-machine mutex. Recovery of a dead module via
  the PC-side updater is now reachable and verified on hardware.

- **SD detect-poll backoff** (#594). An empty card slot now costs the shared
  bus one detect exchange every 5 s (was every 1 s) after ten misses;
  enabling SD or any SD operation instantly restores the fast cadence.

- **Shared-bus health counters** (#597). `SYST:DIAG:SPIBus:STATs?` returns
  cumulative SPI transfer-rejection counters by class (stale handle /
  exclusive holder / lock fail / queue full).

- **SD file transfer over WiFi** (#598/#599). `SYST:STOR:SD:GET`/`LISt?`
  issued over the WiFi TCP console now return their data on that TCP
  connection (previously the bytes silently went to USB). A headless
  logger's data can be retrieved over the network. Validated with a
  53.4 MB byte-identical transfer. USB behavior unchanged.

- **ADC-side rate caps re-fitted at 252 MHz** (#596/#600). The NQ1
  additive cap model is now piecewise by scan class: pure-T1 +21-26%
  (single channel now 19.3 kHz PB), scan-armed OBDiag-off +31-33%;
  OBDiag-on unchanged (all cells 600 s at-cap re-validated). Basis:
  three-night fine-grain endurance grid, every raised cap 600 s
  soak-proven.

- **SD robustness pair** (#603/#605). Removing a card while mounted no
  longer wedges the SD manager (bounded unmount retries + card-absent
  short-circuit); the detect-poll backoff counter now counts real polls
  (Saleae-validated: 1 s x10 -> 5 s idle backoff -> instant fast-cadence
  restore on SD activity).

- **Pre-release hardening (#654).** An independent adversarial pre-merge
  audit over the full release delta caught three issues the endurance soak
  could not, all fixed before release:
  - *SD-over-WiFi connection isolation (#599).* An async `SD:GET`/`LISt?`
    reply is now bound to the TCP connection that requested it. Previously,
    if that client disconnected mid-transfer and a second client inherited
    the single console slot, the remaining file bytes could land in the new
    client's stream. Normal single-client transfers are unchanged.
  - *252 MHz PB cap raise scoped to NQ1 (#595).* The raised ProtoBuf
    transport caps were characterized on NQ1 only; they now apply to NQ1
    exclusively, and NQ2/NQ3 keep their pre-raise conservative caps until
    re-characterized (no silent over-cap on those variants).
  - *Build-config integrity.* The Nq1/Nq3 standalone build configurations
    were restored to the correct linker layout (internal; the shipped
    release hex is `default`-built and was always correct).

## New SCPI commands

| Command | Description |
|---|---|
| `SYST:DIAG:SPIBus?` | Bus jam probe (CLEAR/JAMMED/BUSY/INDETERMINATE[,QUARANTINED]) |
| `SYST:DIAG:SPIBus:CROSScs?` | Cross-chip-select response probe |
| `SYST:DIAG:SPIBus:MISOFight?` | MISO drive-fight count |
| `SYST:DIAG:SPIBus:STATs?` | SPI rejection counters by class |

## Fixes

- SD `SYST:STOR:SD:BENCHmark` no longer fails at exactly 32768 bytes on all
  cards (producer outpaced the SD task; paced with a bounded retry) (#594).
- Capabilities JSON exports the variant-appropriate `isrMaxHz` (NQ1 22 kHz,
  NQ2/NQ3 16 kHz) (#595).
- SPI bus probes report INDETERMINATE instead of echoing a stale verdict
  when the probe could not run (#594).
- SD-detect kick + card-presence queries added at the driver level
  (`DRV_SDSPI_IsCardAttached`, `DRV_SDSPI_DetectPollKick`) (#594).

## Compatibility

- Enforced caps only move up or stay; clients pre-validating against
  `current_max_rate_hz` (`CONF:CAP:JSON?`) need no changes.
- New QUES bit 13 (8192): clients masking QUES bits should be aware.
- No SCPI removals or renames.

## Validation

- 42-cell at-cap validation at the new caps: USB/SD/WiFi PB, 600 s/cell,
  freeze-aware gates — 42/42 PASS, zero loss (`atcap_20260705_120746.csv`).
- 8 h all-interface nightly regression on the release candidate (2026-07-05
  overnight, `nightly_regression.py`) — results referenced in the PR.
- Sick-card advisory + quarantine validated live against the incompatible
  card; bus-counter query validated on hardware.
- Post-fix cross-board re-validation (2026-07-08): the at-cap USB PB+CSV
  matrix run on two independent NQ1 units (COM3 + COM12) is byte-identical
  and 28/28 PASS zero-loss on both, confirming the #654 fixes did not
  regress streaming. Firmware-counter check: the device achieves its
  enforced cap (e.g. 8786 sps at an 8788 Hz cap, invariant holds, 0 drops).
