# DAQiFi Nyquist Firmware v3.7.2

**Release date:** 2026-07-12
**Baseline:** v3.7.1

A SCPI input-hardening and robustness release: a systematic sweep of the SCPI
command surface for unchecked user input (channel indices, malformed
parameters), a connect-and-never-send TCP-console DoS guard, and WiFi
power-save. Notable process milestone — every code PR passed a **cross-vendor
adversarial pre-merge gate** (an independent GPT/Codex bug-hunt plus a Claude
refute-skeptic), which caught two real defects in already-merged code and a
second-order regression inside one of the fixes.

## Fixes

- **`CONF:ADC:CHAN` input validation (trio, #630 / #678 / #682)** — the
  two-argument `(channel,state)` setter is now robust across the whole input
  range: (#630) bounds-check the resolved index before the array deref (removes
  a wild OOB read on gap ids); (#678) validate the raw value **before** the
  `uint8_t` cast so a value ≥ 256 can no longer alias mod-256 onto a valid user
  channel (`256→0 … 271→15`) and silently mutate it; (#682) scope that guard to
  the truncation range only (so it doesn't usurp #630's gap-id handling) and push
  a classifiable `-222` on every non-settable channel — including monitoring ids
  248–255 — instead of the libscpi-default `-200`.
- **DIO single-channel index off-by-one (#653)** — the seven `DIO:PORt:*`
  single-channel commands rejected `id == Size` incorrectly (off-by-one), and
  `PWM:CHannel:FREQuency` was unbounded and reached a shared-timer divide-by-zero
  (device-resetting) in the callee. Fixed with a boundary helper + a zero-guard.
- **Malformed `StartStreamData` rate (#674)** — a non-numeric or unparseable
  rate (`SYST:STR:START abc`, `100Hz`, `#`) previously fell through and restarted
  streaming at the **stored** rate. It is now rejected with a SCPI error and does
  not start; the no-argument (stored-rate) and `freq==0` (disable) forms are
  unchanged.
- **TCP console idle-timeout TOCTOU (#677)** — the idle-console watchdog read the
  volatile deadline twice; a concurrent `SYST:COMM:LAN:IDLEtimeout 0` between the
  reads could close the idle console the operator just disabled the watchdog for.
  Now snapshotted once.
- **SPI-bus diagnostics ownership guard (#658)** — the invasive
  `SYST:DIAG:SPIBus:*` cross-CS / MISO-fight probes are now gated behind SPI4
  ownership (rejected while the SD manager holds the bus), closing the v3.7.1
  deferred diagnostic-misuse hazard.

## New features

- **TCP console idle-timeout (#663)** — `SYST:COMM:LAN:IDLEtimeout <seconds>`
  (0 = off) closes a `:9760` console client with no RX **or** TX for the deadline,
  defeating the connect-and-never-send hold of the single console slot. A
  streaming client is continuously TX-active and is never torn down.
- **Dynamic WINC power-save (#29)** — the WINC1500 enters power-save based on
  streaming state, trimming idle-link power without affecting an active stream.

## Docs / test infrastructure

- **CircularBuffer thread-safety contract (#123)** documented, plus **PC-hosted
  unit tests (#124)** for the buffer.
- **RTOS task-priority audit (#493)** — corrected the Task Priority Map drift in
  `CLAUDE.md` against the actual `xTaskCreate` / self-boost values.

## New SCPI commands

| Command | Description |
|---|---|
| `SYST:COMM:LAN:IDLEtimeout <sec>` / `?` | TCP console idle timeout (0 = off) |

## Compatibility

- **Error-class change:** a non-settable `CONF:ADC:CHAN` channel (gap or
  monitoring id) now rejects with `-222,"Data out of range"` where some paths
  previously returned the generic `-200,"Execution error"`. The command is still
  rejected with no state change — only the error code is more specific. Clients
  that key off the *rejection* (not the exact code) need no changes.
- No SCPI removals or renames.

## Validation

- **8 h at-cap USB endurance soak** (12 rounds, 249 at-cap cells, USB PB/CSV
  across the channel matrix): **zero data loss, zero wedges, zero heap leak**
  (`HeapMinEverFree` flat after the first-stream allocation; stacks healthy).
- **Cross-vendor adversarial pre-merge gate** (Codex GPT-5.x hunt + Claude opus
  refute-skeptic) on every code PR — caught #677 and #678 in already-merged code,
  and a monitoring-id error-class regression inside the #678 fix (→ #682), each
  fixed with a regression test before merge.
- **Hardware verification** of every behavioral change on bench NQ1 units:
  #678 (`256`/`248` rejected, victim channel intact), #674 (malformed rate
  rejected, no stream — `TotalSamplesStreamed=0`), #677 (idle-close + disabled-
  stays-open + race probe over WiFi), #630 (gap-id bounds), #682 (`248→-222`).
