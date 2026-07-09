# DAQiFi Nyquist Firmware v3.7.1

**Release date:** 2026-07-09
**Baseline:** v3.7.0

A feature + hardening release on top of v3.7.0's 252 MHz work. 18 PRs, validated
by an 8 h freeze-aware at-cap endurance soak (245/247 cells clean, zero heap
leak, zero wedges) plus a per-fix adversarial pre-merge gate and hardware
verification of the behavioral changes.

## New features

- **SD file-integrity CRC** (#306) — `SYST:STOR:SD:CRC "file"` computes a
  zlib-compatible CRC32 on the SD task (bounded 8 KB/pass, never blocks SCPI or
  starves streaming); `SYST:STOR:SD:CRC?` returns `BUSY` / `0xXXXXXXXX,<bytes>`.
  Pairs with the v3.7.0 SD-over-WiFi transfer for end-to-end verification.

- **WINC deep power-down + RF-silent standby** (#334) —
  `SYST:COMM:LAN:POWer 0|1` fully powers the WINC1500 down (CHIP_EN low) for
  battery savings while USB/SD streaming keeps running. The power-state machine
  now also **holds the WINC in reset whenever the device is in STANDBY** — a
  standby unit is RF-silent (no SSID beacon, no STA reconnect) and restores WiFi
  on power-up if it was enabled.

- **User-definable device name** (#14) — `SYST:DEVice:NAME` persists a friendly
  name to NVM, emitted in the protobuf/JSON info messages (`friendly_device_name`).

- **On-demand diagnostic ADC** (#285) — `SYST:DIAGnostic:ADC?` returns the
  onboard monitoring rails (3.3 V, 2.5 VREF, VBATT, 5 V, 10 V, 5 VREF, VSYS) in
  a machine-parseable form, outside of streaming.

- **Raw ADC-code streaming** (#158/#270) — `CONF:ADC:USECal 2` streams integer
  ADC codes (no cal/voltage conversion), runtime-only (NVM cal choice preserved).

- **Wire-confirmed USB byte counter** (#511) — `UsbBytesSent` in
  `SYST:STR:STATS?` completes the true-delivery triad (SD / WiFi / USB) for
  PC-independent throughput verification.

- **AP SSID visibility control** (#45) — `SYST:COMM:LAN:HIDden 0|1` cloaks the
  soft-AP SSID from beacons.

- **BQ24297 fault monitoring** (#193) — the charger INT line (RA4) now drives
  interrupt-driven REG09 fault monitoring (NTC/charge/battery/watchdog), instead
  of read-once-at-boot.

- **Insert-mode SCPI console** (#48) — the microrl line editor now inserts
  mid-line instead of overwriting (USB-CDC and WiFi-TCP consoles).

## Fixes

- **WiFi disable now persists across a power cycle** (#656) — a user-disabled
  module no longer re-broadcasts its soft-AP on a `POW:STAT 0→1` cycle; the
  standby→powered re-init honors the runtime `isEnabled` instead of force-enabling.
- **SD filename path validation** (#612) — `SYST:STOR:SD:CRC` now rejects
  traversal/absolute/illegal paths like the other SD filename commands (#615
  centralized the validator across GET/LIST/DELETE/rename; #659 closed the CRC site).
- **Atomic SD enable** (#613) — a failed mount rolls the enable back instead of
  leaving the subsystem armed-but-broken.
- **USB write-submission TOCTOU** (#127) — a claim flag closes a window where two
  writers could both claim the USB DMA buffer.
- **DIO-only streaming emits again** (#593) — regression from v3.5.0 fixed.
- **Logging hygiene** (#605) — `app_freertos.c` / `AInSample.c` ship at an
  ERROR-only compile ceiling (LOG_E in, LOG_D/LOG_I stripped from these boot/hot
  paths); duplicate-define integration fix (#655).

## Caps / performance

- **SD CSV cap made soak-safe at high channel count** (#660) — the 8 h soak
  dropped SD bytes at the SD CSV 10 ch cap (byte-rate limit); the transport
  coefficient was lowered (`A` 42000→36000, 10 ch 1909→1636 Hz, ~14% margin),
  re-validated zero-loss. Single-channel and PB caps unchanged.
- **#586 PFMWS 5→4 evaluated and declined** — a PFMWS 4/5 A/B showed no
  measurable throughput gain (below the errata-#38 "<2% with cache" / bench-noise
  floor), so the conservative 5 wait-states are retained.

## New SCPI commands

| Command | Description |
|---|---|
| `SYST:STOR:SD:CRC "file"` / `SYST:STOR:SD:CRC?` | SD file CRC32 |
| `SYST:COMM:LAN:POWer 0\|1` / `?` | WINC deep power-down |
| `SYST:DIAGnostic:ADC?` | onboard monitoring rails |
| `SYST:COMM:LAN:HIDden 0\|1` / `?` | AP SSID cloak |
| `SYST:DEVice:NAME` (+ `:SAVE`) | user-definable device name |
| `CONF:ADC:USECal 2` | raw ADC-code streaming mode |

## Compatibility

- Behavioral change: a device in STANDBY now holds WiFi in reset (was: WINC
  left powered). Intentional (RF-silent standby).
- SD CSV enforced caps at high channel count moved **down** slightly (soak-safe);
  clients pre-validating against `CONF:CAP:JSON?` need no changes.
- No SCPI removals or renames.

## Known / deferred

- SPI cross-CS/MISO-fight diagnostics lack a bus-ownership guard (#658) —
  diagnostic-misuse hazard, deferred to v3.7.2.

## Validation

- 8 h freeze-aware at-cap endurance soak (USB/SD/USB+SD × PB/CSV): 245/247 cells
  clean, **zero heap leak** over the run, 0 wedges. The two initial SD CSV 10 ch
  drops drove the #660 cap nudge, re-validated clean at the new cap.
- Per-fix adversarial pre-merge gate (audit → refute) on every code PR.
- Hardware verification of #656 (disable persistence), #637 (standby-silence),
  #610 (CRC wedge + real-file CRC), and the #660 cap.
