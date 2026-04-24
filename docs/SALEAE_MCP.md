# Saleae Logic 2 — DAQiFi capture workflow

This document covers the DAQiFi-specific side of running the Saleae Logic 2
MCP: which probes are wired to which channels, what a typical capture
session looks like, and how to attach analyzers for the on-board peripherals
worth decoding (SD SPI, WINC SPI, BQ24297 I2C, debug UART).

For the generic MCP usage (capture lifecycle, the 1 GB buffer rule, tool
list), see `~/.claude/skills/saleae/SKILL.md`.

## Probe map — pipeline default

The streaming pipeline exposes 10 standard DIO probes (issue #301, PR #307)
mapped 1:1 to DIO_0..DIO_9. With a Saleae Logic 8, channels 0..7 cover the
high-priority probes; for full pipeline visibility use a Logic Pro 16 or
two captures with overlapping windows.

| Probe (DIO) | Mode | What it measures | Typical Tstd |
|---:|:---:|---|---:|
| 0 | TOGGLE | `Streaming_TimerHandler` ISR entry — true hardware timer rate | tens of ns |
| 1 | TOGGLE | `ADC_EOSInterruptCB` ISR entry — ADC end-of-scan ISR rate | tens of ns |
| 2 | TOGGLE | Deferred ISR task wake (`ulTaskNotifyTake` returns) | 100–200 ns |
| 3 | PULSE | Deferred task: pool alloc + channel loop + queue push | ~10 µs |
| 4 | PULSE | Deferred task: ADC trigger + DIO trigger | ~25 µs |
| 5 | TOGGLE | EOS task wake | tens of ns |
| 6 | PULSE | EOS task: MC12b read result loop (skip + read across 16 ch) | varies |
| 7 | TOGGLE | Encoder task (`streaming_Task`) wake | ~1 µs |
| 8 | PULSE | Encoder: encode call (CSV / PB / JSON) | varies |
| 9 | PULSE | Encoder: USB / WiFi / SD output write | varies |

Setup the full default in one shot:

```
SYST:DIOP:PIPEL TOGGLE
SYST:DIOP:MODE 3,PULSE
SYST:DIOP:MODE 4,PULSE
SYST:DIOP:MODE 6,PULSE
SYST:DIOP:MODE 8,PULSE
SYST:DIOP:MODE 9,PULSE
```

Or assign individually with `SYST:DIOP:MODE <id>,<mode>`. Release a probe
with `SYST:DIOP:CLE <id>` (or `SYST:DIOP:CLE:ALL`) — releasing returns the
DIO to whatever the user had configured.

Six ad-hoc probes (10..15) are available at compile time via
`DIO_PROBE_ENABLE_MASK` in `BoardConfig.h` plus the `DIO_PROBE_TOGGLE(n)` /
`DIO_PROBE_PULSE_START(n)` / `DIO_PROBE_PULSE_END(n)` macros.

## Standard capture session

```
1. Power up:        SYST:POW:STAT 1
2. Assign probes:   SYST:DIOP:PIPEL TOGGLE  (then PULSE the work probes)
3. mcp__saleae__get_devices                  → grab deviceId
4. mcp__saleae__start_capture(
       logicDeviceConfiguration={deviceId, captureMode={
           digitalChannels: [0..7], digitalSampleRate: 100000000}},
       captureConfiguration={timedCaptureMode: {durationSeconds: 10},
                             bufferSizeMegabytes: 1024})
5. Enable channels: ENA:VOLT:DC <chmask>,1
6. Start streaming: SYST:STR:TEST:PAT 3      (fullscale, optional)
                    SYST:StartStreamData <freq>
7. mcp__saleae__wait_capture(captureId)      → blocks ~10 s
8. Stop streaming:  SYST:StopStreamData
9. Verify counters: SYST:STR:STATS?
10. mcp__saleae__export_raw_data_csv(captureId, outputPath="/mnt/c/.../foo.csv")
11. Process in Python: edge deltas → fmean/Tstd/Nedges per probe
12. Append a session block to docs/PIPELINE_TIMING.md (don't overwrite)
```

100 MS/s × 8 ch × 10 s ≈ 1 GB raw — fits in the 1 GB buffer with no
truncation. Drop the rate to 50 MS/s if you need 20 s captures.

## Session log convention

`docs/PIPELINE_TIMING.md` is the **living record**. Every capture appends a
new session below the existing ones. Each session block must record:

- Firmware SHA (and branch if not main)
- Board variant (NQ1 / NQ3)
- Analyzer hardware (Logic 8 vs Pro 16, channel count)
- Streaming config (freq, channel count + types, encoding, interface)
- Firmware-side stats (samples / TimerISRCalls / drops / total bytes)
- TOGGLE probe stats: `fmean`, `Tstd`, `Nedges/ΔT`
- PULSE probe stats: above plus `Tpos mean / min / max` and `Tneg mean`
- Brief notes on what changed vs prior sessions

Don't overwrite — each session is a permanent point-in-time measurement.
This is what lets us prove e.g. "PR #335 idle-gate cuts 10 kHz CV from
9.44% to 0.5%" months later.

## Decoding peripherals — analyzer attachment

When you need to decode bus traffic instead of just measure pipeline
timing, attach a Logic 2 analyzer via `mcp__saleae__add_analyzer`. The
DAQiFi peripherals worth knowing about:

### SD card SPI

- **Pins**: SDI/SDO/CLK/CS on SPI3 (NQ1 board; check schematic for your
  variant).
- **Bus speed**: ~25 MHz typical after card-init handshake — sample at
  100 MS/s minimum to see edges cleanly.
- **Useful for**: chasing SD write throughput regressions (#312, #314),
  sector-aligned write verification, FAT32 directory-walk debugging.
- **Settings shape** (verify channel numbers from your wiring):
  ```json
  {"MISO": 0, "MOSI": 1, "Clock": 2, "Enable": 3,
   "Bits per Transfer": "8", "Significant Bit": "MSB",
   "Clock State": "CPOL=0", "Clock Phase": "CPHA=0", "Enable Line": "Active Low"}
  ```

### WINC1500 SPI

- **Pins**: SPI4 (NQ1). Driver in `firmware/src/config/default/driver/winc/`.
- **Bus speed**: 33 MHz max via REFCLK1 passthrough (PR #219).
- **Useful for**: WINC stack overflow forensics (#347-style),
  fresh-association timing, WINC_Tasks idle-gate validation (#335).
- The WINC frames its SPI traffic with NM_SPI_HDR — Logic 2's generic SPI
  analyzer shows raw bytes; structural decoding is a custom HLA exercise.

### BQ24297 I2C

- **Bus**: I2C5, 100 kHz (errata #6/#37 — never push past 100 kHz on
  this MCU).
- **Address**: 0x6B (8-bit: 0xD6 write / 0xD7 read).
- **Useful for**: IINLIM state-machine debugging, DPDM detection
  verification, REG07 charge-fault correlation.
- Saleae I2C analyzer settings: SDA + SCL channels, address-display
  "8-bit" or "7-bit" per preference.

### Debug UART (UART4, RB0)

- **Baud**: 921600.
- **Where it's enabled**: `ENABLE_ICSP_REALTIME_LOG 1` in `Logger.h`.
- **Useful for**: live LOG_E/LOG_I/LOG_D streams when USB CDC is wedged
  (e.g. during a freeze you'd otherwise need MDB CLI for). Wire a USB
  serial adapter to RB0 + GND on the ICSP header.
- Saleae Async Serial analyzer: 921600, 8N1, no parity.

## Cross-references

- `docs/PIPELINE_TIMING.md` — session log, probe map, jitter model
- `~/.claude/skills/saleae/SKILL.md` — generic MCP tool usage and gotchas
- `firmware/src/HAL/DioProbe.{c,h}` — probe framework source
- `firmware/src/services/SCPI/SCPIInterface.c` — `SYST:DIOP:*` command
  table (search `DIOProbe`)
- Wiki: SCPI Interface § DIO Debug Probe Commands
