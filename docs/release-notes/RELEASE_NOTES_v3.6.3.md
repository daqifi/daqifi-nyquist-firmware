# v3.6.3 — 252 MHz SYSCLK (#487) + WINC WiFi-module update path recovered (#587) + SD detection fix (#590)

Release over v3.6.2. Three headlines: the CPU now runs at its chip-rated **252 MHz** (up from 200 MHz); the **WiFi-module (WINC1500) firmware-update path over USB transparent mode works reliably again** — including validated updates to WINC firmware 19.7.11; and an **SD-card detection regression is fixed**, restoring robust concurrent SD + WiFi operation on the shared SPI bus. Existing streaming rate caps are unchanged and re-validated at 600 s endurance on this build.

## Highlights

### Changed: 252 MHz SYSCLK, 84 MHz peripheral buses (#487/#584)
The PIC32MZ now runs at its datasheet-rated 252 MHz (DS60001320H §39), with the peripheral buses moved from 100 MHz to a bus-spec-compliant 84 MHz. Flash wait states and ECC are configured per errata #38. The enforced streaming caps are **unchanged** — they were fitted at 200 MHz, so they remain safe (conservative) at 252 MHz; a re-characterization to harvest the added headroom is in progress. A single compile-time toggle (`clock_config.h DAQIFI_SYSCLK_252`) selects between the 252 MHz and legacy 200 MHz trees.

### Fixed: WINC1500 WiFi-module firmware updates over USB transparent mode (#587)
The `SYST:COMM:LAN:FWUpdate` → transparent-mode bridge path used by `winc_programmer`/`winc_flash_tool` had three independent host-side defects that produced "corrupted WINC" symptoms (bogus chip IDs like `0x00050000`, WiFi FW version `0.0.0`, NACKed writes) — **the WINC modules themselves were never corrupted**:
- The SD driver's attach-detect polling could leave the shared SPI bus locked in exclusive mode forever, silently rejecting every WINC transfer until reboot. The bus is now always handed over (release-on-suspend, executor watchdog, bounded WINC retry).
- The transparent-mode RX ring had no overflow protection and could silently wrap over unread flash payload — corrupting a WINC image mid-write behind a valid-looking ACK (the likely cause of historical real corruption). Replaced with USB-level backpressure: the host NAK-throttles, zero loss.
- Download-mode entry never reset the chip (a literal upstream `TODO`), so the PC tool's programmer firmware never ran. Entry now performs the canonical CHIP_EN/RESET_N hard reset.
Validated end-to-end: full erase/XO/write/verify of WINC 19.7.7 and an update to **19.7.11** (verify passed) through the DAQiFi bridge; 19.7.11 runs cleanly against this firmware's host driver. Note for tooling: use winc_programmer 2.0.2 **with its matched** `programmer_firmware.bin` from Harmony wireless_wifi `utilities/wifi/winc`.

### Fixed: SD card detection regression; robust concurrent SD + WiFi (#590, #567/#578)
#567's bus-completion watchdogs (which fixed the SD wedge class needing a power cycle) introduced a completion-correlation race: short SPI transfers complete from ISR context before the expected-handle bookkeeping executes, so valid completions were dropped and **SD cards stopped being detected** (bisected adjacent-commit; the card was fine). Fixed by inverting the guard — accept completions by default, ignore only explicitly watchdog-aborted transfers (4-slot ring, handle-reuse aware). With #587's bus hygiene this also removes the background WINC↔SD contention failures: SD detects, mounts and streams **with WiFi fully active**, and WiFi STA association is reliable (the historical "needs channel 1" AP workaround is obsolete — root cause was bus contention mid-handshake, not RF).

### Fixed: USB SCPI large-response drain (#572/#573)
Large SCPI responses over USB no longer stall the write path.

### Changed: SD-PB streaming cap derated for scan-armed configs (#574/#579)
NQ1 SD ProtoBuf caps corrected for scan-armed (Type 2 / OBDiag) configurations.

### Added: TCP listener-health observability (#560/#475/#583)
New counters expose WiFi TCP listener lifecycle health for diagnosing "association up but port dead" reports.

## All changes since v3.6.2
- **#584** — `perf(clock)`: SYSCLK 200→252 MHz, PBx buses → 84 MHz, single-define toggle, errata #38 wait states (#487).
- **#587** — `fix(wifi/sd)`: WINC flash-update bridge recovery + shared-SPI exclusive-lock hygiene.
- **#590** — `fix(sd)`: #567 completion-correlation race; SD detection restored; aborted-transfer ignore ring.
- **#578** — `fix(sd)`: hard-timeout SDSPI bus-completion waits (#567).
- **#579** — `fix(adc)`: derate SD-PB streaming cap for scan-armed configs (#574).
- **#573** — `fix(usb)`: drain USB write buffer during large SCPI responses (#572).
- **#583** — `feat(wifi)`: TCP listener-health observability counters (#560/#475).
- **#566** — `docs+tools(release)`: scripted bootloader-linked release process.
- **#571** — `fix(bootloader)`: bootloader 2.0 (separate bootloader image; not part of this application hex).
- **#591 (this)** — `chore(release)`: bump `FIRMWARE_REVISION` to 3.6.3.

## Validation (hardware, NQ1 `7E2898F46200E8A7`, WINC 19.7.11)
- **At-cap endurance, 600 s per cell, freeze-aware leak gates** (`ScanStaleDropped`/`T1ArdyMisses`/`fw_stats_ok`): **USB PB 14 configs, SD PB 14 configs (post-format card), WiFi PB 14 configs (STA over WPA2, DHCP)** — all pass at the enforced caps with zero loss. One known-marginal cell: USB PB 5T1+5T2 (10 ch) showed a single stale-scan tick over 600 s at its 5198 Hz cap (clean at 4525 Hz); cap adjustment tracked as a follow-up — not a regression vs v3.6.2, which shipped the same cap validated at 120 s.
- **WINC update path**: 19.7.7 full write + verify, then 19.7.11 update + verify, through the DAQiFi transparent-mode bridge; module boots and streams on 19.7.11.
- **Concurrency**: SD detect/mount/list with WiFi associated and active; zero bus-contention recoveries logged on this build.
- **SD hygiene note**: sustained SD streaming ceilings depend strongly on card state — a well-used unformatted card degraded progressively under load in testing while a freshly formatted card passed every cap. Recommend periodic reformat for long logging deployments.

## Upgrade notes
- Standard in-app update (the desktop app selects the `.hex` asset) or manual flash.
- Every flash wipes NVM (WiFi / calibration / voltage-precision settings) — re-save after updating.
- WiFi-module firmware **19.7.11** is validated with this release; the in-field module-update rollout is tracked in daqifi-desktop#661.
