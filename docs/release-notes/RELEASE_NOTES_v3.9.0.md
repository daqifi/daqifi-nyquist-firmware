# DAQiFi Nyquist Firmware v3.9.0

**Release date:** TBD
**Status:** DRAFT. Not tagged.
**Baseline:** v3.8.0. **143 commits** on `main` since `v3.8.0`: 89 fixes,
8 features, 3 refactors, and the rest docs, tests, CI and lint.

3.9.0 is a **hardening release**. It adds few features. Most of its weight is
in three places:

1. **SD logging integrity.**
   - A data-loss bug is fixed: a mid-session `SYST:STOR:SD:MAXSize` truncated
     the active log while reporting success (#915/#979).
   - A failed write now continues the session instead of truncating the log
     (#1031).
   - A new **opt-in** CRC-protected write mode, `SYST:STOR:SD:PROTect 1`
     (#1217), covers the case where the card captures a stray block from bus
     traffic and writes it over its target sector.
2. **In-app updates now keep settings and calibration on newly built units**
   (#909/#1141). Before this, every in-app update erased WiFi credentials and
   calibration. The bootloader change reaches **newly manufactured units
   only**. See below.
3. **Concurrency and stop-race fixes across SCPI, streaming and SD.** USB and
   TCP SCPI now have separate parse buffers and error queues (#1016). The
   stop-race guard is a single predicate used at every site (#1201). Torn
   64-bit calibration reads are closed (#1048, #1054, #1093).

---

## New features

- **`SYSTem:IDENtify[?]`** (#1220): flashes both LEDs so a specific board can
  be found on a bench or a shelf.
- **`SYSTem:COMMunicate:LAN:CONnected?`** (#951/#1044): reports whether the
  WiFi link is actually up.
- **`SYSTem:COMMunicate:LAN:NETType?` takes an optional argument** (#1186/#1193)
  and with it reports the *active* WiFi mode, not only the configured one.
  Without the argument the reply is unchanged.
- **`SYSTem:STReam:START?`** (#1007/#1014) reads back the stored streaming
  rate. STOP deliberately does not revert it.
- **`SYSTem:MEMory:FREE?` reports the streaming pool's total size** (#1082/#1089).
- **`CONF:CAP:JSON?` additions:**
  - each channel's intra-scan conversion offset (#267/#1112);
  - the individual rate-cap terms (#921/#928);
  - the JSON precision-4 cap terms (#920/#1088);
  - corrected so it publishes exactly what START and the DAC setters accept
    (#1202/#1208).
- **Deferred-task per-channel read-loop timing** (#251/#1091), exposed for
  characterisation.
- **`SYSTem:STORage:SD:FAILNext[?]`** (#981/#1013): a write-fault injection
  hook for testing. It is not for production use.
- **`SYSTem:STORage:SD:PROTect[?]`** (#1217/#1228). **Opt-in; the default is
  0.**
  - **At 1:** the card runs with CRC checking on and writes each block as its
    own CMD24. A block captured from bus traffic is refused instead of written,
    and an abandoned write is resynced.
  - **Cost:** about **two thirds of SD write throughput**, per the design note
    at `SCPIStorageSD.c:629`; not re-measured for this release. It also makes the
    WiFi control-session stall during SD logging (#1234) much worse. That is
    why it is not the default.
  - **When it can be changed:** only while SD is disabled and idle. It is a
    runtime setting and is not persisted.
  - **Follow-ups:** #1237 and #1240 extend resync and re-send to both modes.

## SD logging

- **#915/#979:** a mid-session `SYST:STOR:SD:MAXSize` no longer destroys the
  active log. Before the fix a probe session kept 0.46 of a control session's
  sample rate; after it, 1.00. Bench A/B, test-suite `test_979`.
- **#1031/#1224:** a failed write now continues the session. Before, it
  truncated the session's log.
- **#1217 series** (#1228, #1237, #1240): resync after an abandoned CMD25 write,
  re-send of a write in doubt in both PROTect modes, and the shared SPI4 chip
  select released on abort.
- **#914 Part A (#1106):** an `SD:GET` that cannot open its file now answers a
  distinguishable marker plus a SCPI error. Before, it returned an empty
  transfer.
- **#756 Part A (#1094):** the SD presence gate refreshes before it refuses.
- **Teardown and arm paths:**
  - **#963:** mode is cleared inside the claim on every arm refusal.
  - **#964/#968:** FORmat retracts format-pending when its arm is refused.
  - **#925/#929:** a leaked shared-SPI4 exclusive lock is unwound.
  - **#930/#1079:** the observed maximum SPI4 hold time is published.
- **`SD:BENCHmark`:**
  - **#936:** checks its arm's suspend refusal.
  - **#943:** has bounded stall and buffer waits.
  - **#953:** no longer blames the card for a suspended SD task.
  - **#958, partial:** its scratch filename no longer repeats every 65.5 s.
  - **#1111:** latches when its own arm was torn down.

## Streaming and encoders

- **#1072/#1243:** a WiFi stream whose TCP client has gone is stopped
  automatically.
- **#1177/#1242:** a protobuf session's first SD file now starts with its
  `sd_metadata`.

- **#164/#961:** the JSON encoder no longer silently destroys samples near
  buffer-full.
- **#996:** the CSV encoder evicts a row that cannot fit instead of stalling
  the stream.
- **#1200/#1204:** CSV eviction books one loss per evicted entry.
- **#1199/#1203:** a non-finite sample is never emitted or cast to an integer.
- **#970/#991:** consume-accounting no longer books samples on a zero-byte
  return.
- **#1021/#1028:** an undeliverable batch fails at once, not after a 10 s stall.
- **#931/#952:** an undersized sample-pool partition is no longer silently
  inflated.
- **#975:** the sample pool is invalidated on repartition instead of aliasing
  the old buffer.
- **#1158/#1209:** removes the **NQ3 1 Hz clamp** on AD7609 streams.
- **#1076:** JSON's DIO+ADC combination no longer emits two `"ts"` keys.
- **#1018/#982 (#1055):** the session loss summary labels grace-window loss
  correctly, and it now prints after the SD teardown's own report.
- **#977/#1103:** the session-start and config-change claims are interlocked.

## SCPI correctness and concurrency

- **#1016:** USB and TCP SCPI contexts each have **their own parse buffer and
  error queue**. Before, one transport's command or error could land in the
  other's.
- **Stop-race guard:**
  - **#1201/#1215:** one `StopRequestedSince()` predicate, used at every
    stop-race site.
  - **#969/#1020:** both pins are taken under one critical section.
  - **#1120/#1214:** threaded through the session-start runner.
  - **#965:** the WIFI:FINd? arm observes it.
- **Shared response buffer no longer held for seconds** by SysInfo (~90 s,
  #992), Help (~5–7 s, #1012), or the UART getters (#948/#960). SysLogLevel and
  SDInfo replies now go through the shared buffer (#1132), and Help through the
  bounded write (#1140).
- **#1156/#1211:** a double is rendered at the narrowest width that reads back
  exactly.
- **#1005/#1011:** `DIO:PORt:ENAble` can no longer corrupt the CSV column count
  mid-stream.
- **#885/#887:** five ADC setters no longer modify live conversion inputs
  without a guard.
- **Start-path refusals:** #942/#974, #1025 and #1123 (SD write-arm diagnosis
  on `SYST:STR:START`). #891/#892: the rate finder no longer arms a sweep with
  no ADC channels. #937: WIFI:FINd? reports a refusal before measurement as
  START_REFUSED.
- **Retired to NOT IMPLEMENTED:** the DAC calibration family (#919/#993) and
  DAC `USECal[?]` (#1002/#1022). Those commands now answer as not implemented.
- **Wire-visible narrowing (#907/#1036):** eight lowercase-start ADC/DAC
  patterns were respelled all-caps, for example
  `CONFigure:ADC:chanCALM` → `CONFigure:ADC:CHANCALM`.
  - **Unchanged:** the full old spelling still works, because matching is
    case-insensitive.
  - **Changed:** the degenerate header `CONF:ADC:` (nothing after the colon)
    used to dispatch silently to a calibration setter. It now answers `-113`.

## Calibration, ADC and DAC

- **#908/#1077:** boot **no longer writes identity values into the factory
  calibration slot** when the load fails. `CONF:CAP:JSON?` now reports
  `identity.cal.factory_present: false` / `source: "identity"`, so "never
  calibrated" can be told apart from "calibrated".
- **#889/#941:** AD7609 channel calibration is now applied in
  `AD7609_ConvertToVoltage`.
- **Torn 64-bit reads and writes closed:** #1048 (`CHANCALM?`/`CHANCALB?`),
  #1054/#1085 (the read side of CalM/CalB), and #904/#1054/#1093 (AIn Range).
- **#1157:** a non-finite CHANCALM/CHANCALB coefficient is rejected.
- **#1144/#1149:** the calibration doubles in `CONF:CAP:JSON?` are bounded, and
  non-finite values are written as `null`.
- **#1148/#1152:** `CONF:ADC:USECal` validates the reload before persisting the
  selector.
- **DAC7718:**
  - error paths propagate failure (#990);
  - hardware init also gates on the 5/10 V rail (#1129);
  - `AOUT_LATEST` is invalidated on reinit (#1147);
  - the busy-spin at SCPI priority now yields (#1057/#1068);
  - `SCPI_DACVoltageGet` no longer holds the DAC mutex across a transport
    write (#1063);
  - SCPI errors are no longer emitted while the DAC lock is held (#1155).

## Power, WiFi, USB, NVM, bootloader

- **#1226:** streaming is stopped and the SD log closed before power-down.
- **#1071/#1081:** `SYSTem:REboot` keeps the power state.
- **#1084/#1092:** a button power-off sets the AUTOOn latch.
- **WiFi:**
  - **#1060/#1078:** STA flags are cleared on the AP→STA APPLY branch.
  - **#956/#1024:** the TCP partial-send ring's TOCTOU and stale-reset races
    are closed.
  - **#934/#940:** the partial-send log wording is corrected.
- **#185/#1080:** USB CDC write waits are woken by a task notification.
- **#1205:** one recursive mutex for the NVM API, closing the #1153
  stale-load race.
- **#909/#1141 (bootloader):** the bootloader now erases only the lower flash
  panel, so an in-app update preserves the settings and calibration pages.
  - **Verified on silicon:** a hardware A/B on 2026-09-28, posted on #909.
  - ⚠ **Who gets it:** the bootloader cannot be updated in the field, so
    **only units built with the new bootloader** keep their settings across an
    in-app update. Already-fielded units keep the old erase-everything
    behaviour unless reflashed by PICkit or RMA.
  - The application-side half of #1141 does reach fielded units.
- **#1083/#1087:** `force_bootloader_flag` gets its own D-cache line.
- **#902/#1244:** the PWM frequency floor is enforced and advertised.

## Logging and tools

- Several `LOG_E` messages no longer lose their remedy to the logger's
  125-byte ceiling (#1029/#1038, #1116, #1135).
- **#1231/#1018 (#1233):** two log lines no longer describe a healthy device as
  faulty.
- **#1125/#1126:** `gLogOneShot` claims its bit with an atomic, ISR-safe
  test-and-set.
- **#1197/#1207:** the ISR-log queue is passed to its drain task directly.
- **`tools/hexcrc.py`** (#1127/#1130) reproduces `CONF:CAP:JSON?`'s
  `firmware_crc32` offline from a built hex, so a flashed image can be
  identified.
- **CI:** a `LOG_E` message-budget gate, and a SCPI pattern-shape gate (#907).

---

## Validation

E, 2026-11-10 and 2026-11-11, on `main` `7704cc23`, one NQ1 bench board.
The three later merges (#1242, #1243, #1244) are not covered by these runs.

- **SD go/no-go:** 4/4 PASS.
- **Full SD matrix (28 cells, 120 s each):** 27/28 PASS. The one FAIL was
  CSV, 11 channels, at the T2 cap (1565 Hz); see the repeat runs below.
- **CSV 11 ch at the T2 cap (1565 Hz), repeated:** 1 failure in 7 runs
  (120 s each, same card). The 6 passes all had `SdDroppedBytesSteady` = 0.
  The failure dropped 39,006 B (0.11%) through SD-ring back-pressure, with
  **0 samples lost**. The cap is unchanged.
- **The #1217 repair was exercised:** 19 resyncs across these runs, all
  repaired on the first try, 0 failed.
- **USB at-cap re-check:** 28/28 PASS, with caps identical to the 3.8.0-era
  soak.

## Known issues and caveats

> These must ship with the release.

- **Do not pull the SD card during a streaming session.** Stop the stream
  (and SD logging) before removing a card.
  - Since #1240, if the card is pulled during a multi-block write with
    `SD:PROTect 0`, the write stays in doubt and SD will not resume on
    reinsert. Reboot the device before reinserting a card that was pulled
    while logging.
  - This is deliberate: after a card is pulled, silence from the card is not
    proof that it is gone, so the firmware does not treat it as such.
- **Intermittent: SD logging of CSV at 11 channels at the T2 cap (1565 Hz).**
  1 failure in 7 runs of 120 s: that run dropped 39,006 B (0.11%) of the SD
  stream through SD-ring back-pressure, with 0 samples lost. The other 6 runs
  dropped nothing. The cap is unchanged. See "Validation".
- **Known, pre-existing, not fixed in 3.9.0: `SYSTem:STReam:THRoughput`
  over USB after repeated runs.** After several consecutive THRoughput runs
  since boot (the first miss came at about run 5 to 7), a reply can lose its
  result block, and host writes to the port then stall. The device logs
  nothing and does not restart. Reopening the port and sending
  `SYSTem:STReam:STOP` recovers it. It reproduces the same way on 3.8.0, so it
  is not new in this release. THRoughput is a diagnostic command; reboot
  between long diagnostic sequences. Tracked as #1245 (firmware) and test-suite
  #533; separate from #1238.
- **SD:PROTect is opt-in (default 0).** Without it, the stray-block
  overwrite #1217 describes can still happen. With it, SD write throughput
  drops by about two thirds (per the design note at `SCPIStorageSD.c:629`,
  not re-measured for this release), and the #1234 WiFi stall gets much
  worse. The setting is not persisted, so it must be re-applied after every
  boot.
- **The legacy MD5→CRC32 settings migration is untested.** Since #711 (3.8.0),
  the settings load path falls back to the legacy 16-byte MD5 checksum when
  the CRC32 does not match (`daqifi_settings.c`). No test exercises a device
  carrying MD5-checksummed settings through that path. With #1141, settings
  now survive updates on new units, so this path will be hit for real. Until
  it is tested, treat "settings survive an update from ≤3.7.x" as **unverified**.
- **test_822 at 11 channels: INCONCLUSIVE.** The SD file-splitting fix (#822,
  in 3.8.0) has no conclusive regression result at the 11-channel / ~342 KB/s
  operating point its root cause was found at.
- **#825's drain count is unverified.** The fix for "a failed write discards
  its chunk uncounted" (#825, in 3.8.0) closed without the fault-injection
  regression that would prove the count. The `SD:FAILNext` hook added in this
  release now makes that test possible.
- **#1234 (open): a WiFi TCP control session stalls, and is reset, while SD
  is logging.** This happens at either PROTect setting and is much worse with
  `SD:PROTect 1`. It is not fixed in 3.9.0.
- **Excluded: #1238**, an intermittent host-write stall after a high-rate
  USB+SD stream stops. Its cause has not been established. It is **not fixed
  in 3.9.0** and is not claimed to be.
