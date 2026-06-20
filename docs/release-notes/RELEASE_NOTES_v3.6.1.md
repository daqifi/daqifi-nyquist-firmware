# v3.6.1 — USB-CDC hard-wedge fix (#525) + tiny-stack hardening

Patch release over v3.6.0. The headline is a fix for a **hard device wedge** during high-rate USB streaming; the rest is correctness/robustness hardening and a documentation correction. No behavioral changes to normal streaming — the enforced rate caps are unchanged.

## Highlights

### Fixed: high-rate streaming USB-CDC hard wedge (#525)
Under sustained high-rate streaming (notably with a host that briefly stops reading), the device could go **CDC-dead — enumerated but unresponsive on USB and TCP, recoverable only by reflash/power-cycle**. Root-caused via `mdb` to a stack overflow: the ADC end-of-scan deferred task (`MC12bADC_EosInterruptTask`), whose stack was cut to 160 words by the v3.5/#230 memory optimization, called `vsnprintf` on a rare overrun branch — the float-printf frame overflowed into the adjacent heap task control block, corrupting the scheduler ready-list (TLBL exception). **Fix:** remove the `vsnprintf` from that task (the overrun is still counted in `SYST:STR:STATS? eosOverruns`) and restore stack margin.

A second, independent wedge path was also closed: an **unbounded USB-CDC write wait** that blocked the USB task forever when the host stopped draining the IN endpoint. It's now bounded with a stall latch — the connection stays responsive and excess stream data is dropped-and-counted instead of wedging.

> **Note:** this same defect was the true cause of the previously-documented "USB-fatal zone" at high ADC scan/event rates (formerly attributed to a silicon limit). With #525 fixed, that behavior is **gone** — verified on hardware up to 15 kHz (1×T2) / 96k events/s (11×T2). Exceeding a scan-rate cap is now graceful sample-loss, not a wedge.

## All changes since v3.6.0
- **#551** — `fix(#525)`: EOS-task `vsnprintf` stack-overflow wedge + USB-CDC host-stall robustness (the fix above).
- **#553** — `fix(adc)`: the same tiny-stack `vsnprintf` hazard in `AD7609_DeferredInterruptTask` (NQ2/NQ3) — stack restored + one-shot log. *(Runtime validation on AD7609 hardware tracked in #554.)*
- **#555** — `chore(logger)`: bump the ISR-log drain task stack for convention-compliant margin.
- **#549/#550** — `fix(streaming)`: keep the active USB ring ≥ one CDC write (`USBCDC_WBUFFER_SIZE`) on a partition over-commit, preventing USB underflow.
- **#548** — `fix(scpi)`: align the `CONF:CAP` capability JSON bounds with the actual SCPI setter bounds.
- **#547** — `chore(repo)`: remove tracked build artifacts, stale AI docs, and misplaced data (no firmware change).
- **#558** — `docs`: correct the CLAUDE.md "USB-fatal zone" framing (root-caused to #525, not silicon).

## Validation (hardware, NQ1)
- **At-cap real-ADC endurance soak, 30 configs (USB PB+CSV, 400 s each): 30/30 zero-loss.**
- USB-CDC host-stall (12 kHz, drain-stop-resume): survives + recovers, 10/10.
- `#541` value-liveness regression (`test_541`): PASS across T1 + scanned configs; rate caps enforce correctly.
- Burst ceilings unchanged vs documented; scan-rate caps confirmed to track the real data-integrity boundary (no change warranted — see #557).

## Upgrade notes
- Standard in-app update (the desktop app selects the `.hex` asset) or manual flash.
- Release hex is **bootloader-linked** (application at `0x9D000480`) — flashable in-app / via the bootloader on customer devices.
