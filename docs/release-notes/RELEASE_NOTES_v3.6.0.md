# DAQiFi Nyquist Firmware v3.6.0

**Data-integrity release.** v3.5.0 and earlier silently stream *frozen* analog values in two common configurations; v3.6.0 fixes both, re-engineers the ADC read path on documented hardware semantics, and replaces optimistic rate caps with silicon-validated safe limits.

## Fixed

- **Silent frozen Type-1 (dedicated ADC) data above ~4.6 kHz** ([#539](https://github.com/daqifi/daqifi-nyquist-firmware/issues/539), [#541](https://github.com/daqifi/daqifi-nyquist-firmware/issues/541), [#543](https://github.com/daqifi/daqifi-nyquist-firmware/pull/543)). T1 results were read from a cache refreshed by the ADC end-of-scan interrupt, which stops firing when the shared scan is retriggered out-of-spec (documented-undefined, FRM DS60001344E §22.3.2) — above the cliff, prior firmware streams the last value forever at full rate with no error. T1 results are now read directly from the result registers, gated on the documented per-input `ARDY` flag — fresh every tick by construction, verified live to 18 kHz. New `T1ArdyMisses` field in `SYST:STR:STATS?` (expected 0).
- **Silent frozen Type-2 data with `CONF:ADC:OBDiag 0`** ([#537](https://github.com/daqifi/daqifi-nyquist-firmware/issues/537), [#538](https://github.com/daqifi/daqifi-nyquist-firmware/pull/538)/[#540](https://github.com/daqifi/daqifi-nyquist-firmware/pull/540)): the shared scan was gated on the monitoring flag, freezing user T2 channels when monitoring was off.
- **Stale final conversion leaking into the next streaming session** ([#533](https://github.com/daqifi/daqifi-nyquist-firmware/issues/533), [#535](https://github.com/daqifi/daqifi-nyquist-firmware/pull/535) — also in 3.5.x betas).
- **A stalled SD card can no longer throttle USB in multi-output streaming** ([#534](https://github.com/daqifi/daqifi-nyquist-firmware/issues/534), [#536](https://github.com/daqifi/daqifi-nyquist-firmware/pull/536)): SD writes in USB+SD mode are no-retry drop-and-count; hardware-verified (USB sustains its full rate while SD saturates).

## Changed

- **Dynamic ADC scan list** ([#541](https://github.com/daqifi/daqifi-nyquist-firmware/issues/541)): the shared-ADC scan now covers only the session's enabled T2 channels (plus monitoring when OBDiag=1) instead of always scanning all 19 inputs — scan time scales with your configuration. The nonfunctional internal temperature sensor (PIC32MZ erratum #18) is no longer scanned. Idle behavior (e.g. `MEAS:VOLT:DC?`) is unchanged.
- **Frequency caps are now silicon-validated hard limits** (still rejected with SCPI `-222` above the cap, per the 3.5.0 behavior). Three new scan-related bounds replace optimistic values that, with the old frozen-data bugs fixed, would otherwise allow rates the ADC interrupt machinery cannot survive (see *Known issues*): scan-busy time, an end-of-scan interrupt-rate ceiling (10,400 Hz), and an aggregate ADC-event-rate ceiling (60,000 events/s). Notable effective changes at default SAMC:
  - USB 1×T2 OBDiag=0: 15,000 → **10,400 Hz**
  - USB 11×T2 OBDiag=0 (Protobuf): 6,470 → **5,000 Hz**
  - 1×T1 with OBDiag=1: → **10,400 Hz** (monitoring rides the scan; OBDiag=1 now honestly lowers `CONF:CAP`)
  - T1-only with OBDiag=0 configs are unaffected (no scan armed)
  - WiFi caps refit to honest-scan endurance data ([#540](https://github.com/daqifi/daqifi-nyquist-firmware/pull/540)); all WiFi/SD caps re-validated with 120 s at-cap soaks (46/46 cells clean)
- **Mid-stream configuration changes are rejected** while streaming (`CONF:ADC:CHANnel`, `CONF:ADC:OBDiag`, `CONF:ADC:SAMC`) — stop, reconfigure, restart.

## Validation

Every cell of the USB (28/28) and WiFi (18/18) at-cap endurance matrices passed 120 s soaks with zero loss; a new real-ADC value-liveness regression test (all inputs driven at a known voltage) passes 9/9 on this build; Saleae captures confirm end-of-scan interrupt health at every new cap. Datasets: `daqifi-python-test-suite` `benchmarks/541_adc_read_path/`.

## Known issues

- USB CDC can wedge around stop/reconfigure under sustained high-rate streaming or extended idle during WiFi streaming (pre-existing, [#525](https://github.com/daqifi/daqifi-nyquist-firmware/issues/525)); `SYST:REBoot` recovers. Root-cause in progress ([#545](https://github.com/daqifi/daqifi-nyquist-firmware/issues/545)).
- `SYST:STR:BENCHmark 1/2` (NOCAP) bypasses the new caps by design and can drive the ADC scan into a zone that requires a hardware reset ([#544](https://github.com/daqifi/daqifi-nyquist-firmware/issues/544)) — bench use only.
