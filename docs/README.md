# DAQiFi Nyquist Documentation

This directory contains documentation for the DAQiFi Nyquist firmware project.

## Firmware reference (split out of `CLAUDE.md`, 2026-09-10)

`CLAUDE.md` is loaded into **every turn of every agent**; at 38k tokens it was
about 13% of all token spend on this project, paid again on every turn whether or
not the task touched the material. These files hold that reference material
**unchanged** — read the one that covers what you are working on, and update it
here rather than re-adding the text to `CLAUDE.md`.

| File | Covers |
|---|---|
| [STREAMING_AND_ADC.md](STREAMING_AND_ADC.md) | ADC read paths and ISR design, voltage precision, streaming statistics and loss accounting, OPER/QUES registers, frequency capping, test patterns, throughput benchmarking |
| [MCU_REFERENCE.md](MCU_REFERENCE.md) | Atomicity/concurrency rules, cache & DMA, clock tree, FPU, FreeRTOS config, task priority map, silicon errata |
| [MEMORY_ARCHITECTURE.md](MEMORY_ARCHITECTURE.md) | The four memory regions, RAM/heap budgets, dynamic sample pool, `SYSTem:MEMory:*` |
| [SD_SUBSYSTEM.md](SD_SUBSYSTEM.md) | SPI arbitration with WiFi, file splitting and rotation, sector-aligned writes |
| [SCPI_REFERENCE.md](SCPI_REFERENCE.md) | The `SYSTem:MEMory:*` claim-path gate, stream-control namespace aliases |
| [BUILD_AND_TOOLCHAIN.md](BUILD_AND_TOOLCHAIN.md) | Per-file optimization overrides, -O3 source patches, vendored-library patches, linker issue |
| [PERIPHERALS.md](PERIPHERALS.md) | DAC7718 (NQ3), BQ24297 IINLIM state machine |
| [LOGGING.md](LOGGING.md) | Compile-time ceilings vs runtime levels, ISR-safe logging, one-shot suppression |
| [RELEASE_PROCESS.md](RELEASE_PROCESS.md) | Bootloader-linked hex, the `.hex` asset the in-app updater requires, publishing |
| [BOARD_VARIANTS.md](BOARD_VARIANTS.md) | NQ1/NQ2/NQ3 differences, switching build configurations |

What stayed in `CLAUDE.md` is what a task needs regardless of area: build and
flash, the bench inventory and device-verification protocol, the SCPI command
verification protocol, the test policy, the debugging-evidence rules, and the
standing rules.

## Python API and Test Suite

The Python API and test suite have been moved to separate repositories:

### daqifi-python-core
**Python API for DAQiFi Nyquist devices**
- Private repository: https://github.com/daqifi/daqifi-python-core
- Complete Python API for controlling NQ1, NQ2, and NQ3 devices
- Project-agnostic library for general use
- Installation: `pip install git+https://github.com/daqifi/daqifi-python-core.git`

### daqifi-python-test-suite
**Comprehensive firmware test suite**
- Private repository: https://github.com/daqifi/daqifi-python-test-suite
- YAML-configured test suite using daqifi-python-core
- Tests USB CDC, WiFi, streaming (JSON/CSV/ProtoBuf), and more
- Installation: Clone repo and follow README instructions

## Firmware Documentation

For firmware-specific documentation, see the main repository files:
- `CLAUDE.md` - Development guide for working with the firmware
- `README.md` - Project overview

## Contributing

For Python API contributions, see the daqifi-python-core repository.
For test suite improvements, see the daqifi-python-test-suite repository.
For firmware contributions, follow the guidelines in CLAUDE.md.
