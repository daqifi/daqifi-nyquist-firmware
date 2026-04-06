# DAQiFi Nyquist Firmware

Multi-channel data acquisition firmware for the DAQiFi Nyquist device family, built on PIC32MZ2048EFM144 with FreeRTOS.

## Features

- **16 analog input channels** (12-bit MC12bADC, optional 18-bit AD7609)
- **8 analog output channels** (12-bit DAC7718, NQ3 variant)
- **16 digital I/O channels** with interrupt-driven capture
- **USB CDC** streaming up to 1 MB/s
- **WiFi** streaming via WINC1500 (TCP, 2.4 GHz)
- **SD card** logging with FAT32, automatic file splitting
- **Protocol Buffers**, CSV, and JSON encoding formats
- **SCPI** command interface (IEEE 488.2 compliant)
- Auto-balanced DMA buffers across active interfaces
- Runtime-configurable streaming, logging, and memory

## Board Variants

| Variant | ADC | DAC | Description |
|---------|-----|-----|-------------|
| NQ1 | MC12bADC (12-bit) | — | Basic |
| NQ2 | AD7173 (24-bit) | — | Enhanced |
| NQ3 | AD7609 (18-bit) | DAC7718 (12-bit) | Full-featured |

## Prerequisites

- [MPLAB X IDE](https://www.microchip.com/mplab/mplab-x-ide) v6.25+
- [XC32 Compiler](https://www.microchip.com/xc32) v4.60
- [Harmony Configurator](https://www.microchip.com/mplab/mplab-harmony) (MHC) for code generation
- PICkit 4 programmer (or compatible)

## Building

### From MPLAB X IDE

1. Open `firmware/daqifi.X` project
2. Select configuration: **default** (NQ1), **Nq1**, or **Nq3**
3. Build (F11)

### From Command Line (WSL/Linux)

```bash
cd firmware/daqifi.X

# Build default (NQ1) configuration
"/mnt/c/Program Files/Microchip/MPLABX/v6.30/gnuBins/GnuWin32/bin/make.exe" \
  -f nbproject/Makefile-default.mk CONF=default build -j4

# Output: dist/default/production/daqifi.X.production.hex
```

### Building for Bootloader

Include `old_hv2_bootld.ld` linker script in Project Properties, then flash bootloader first (`bootloader/firmware/usb_bootloader.X`), followed by firmware via the Windows DAQiFi application.

## Programming

### With PICkit 4

```bash
"/mnt/c/Program Files/Microchip/MPLABX/v6.30/mplab_platform/mplab_ipe/ipecmd.exe" \
  -TPPK4 -P32MZ2048EFM144 -M \
  -F"dist/default/production/daqifi.X.production.hex" -OL
```

### Bootloader Entry

1. Hold the user button for ~20 seconds until board resets
2. Release button when LEDs light solid
3. Hold button again until white LED blinks to enter bootloader mode

## Streaming Throughput

Zero-leak ceilings measured with FastReader (1ms host polling), fullscale test pattern:

| Config | USB | SD | WiFi |
|--------|----:|----:|----:|
| PB 1ch | 13 kHz | 12 kHz | 5 kHz |
| PB 8ch | 9 kHz | 7 kHz | 3 kHz |
| PB 16ch | 5.5 kHz | 4.5 kHz | 3 kHz |
| CSV 1ch | 15 kHz | 11 kHz | 5 kHz |
| CSV 8ch | 7 kHz | 3.5 kHz | 1 kHz |
| CSV 16ch | 4 kHz / 1 MB/s | 2 kHz / 519 KB/s | 1 kHz / 262 KB/s |

Pipeline mode (skip ADC) achieves 2-3x higher throughput — see [benchmark results](https://github.com/daqifi/daqifi-python-test-suite/tree/main/benchmarks).

## Memory Architecture

| Region | Size | Purpose |
|--------|-----:|---------|
| Streaming Pool | 194 KB | USB/WiFi/SD circular buffers, encoder, sample pool |
| Coherent Pool | 124 KB | SD/USB/WiFi DMA write buffers (auto-balanced) |
| FreeRTOS Heap | 75 KB | Task stacks, queues, mutexes |

All DMA buffers are auto-balanced at each stream start based on active interfaces. No manual tuning required.

## SCPI Commands

Full command reference: [SCPI Interface Wiki](https://github.com/daqifi/daqifi-nyquist-firmware/wiki/01-SCPI-Interface)

Common commands:
```
*IDN?                           # Device identification
SYST:POW:STAT 1                 # Power up
SYST:StartStreamData 1000       # Start streaming at 1kHz
SYST:StopStreamData             # Stop streaming
SYST:STR:STATS?                 # Streaming statistics
SYST:MEM:FREE?                  # Memory diagnostics
SYST:LOG?                       # Retrieve log messages
```

## Testing

See [daqifi-python-test-suite](https://github.com/daqifi/daqifi-python-test-suite) for comprehensive test scripts and benchmarks.

**Critical**: USB CDC streaming requires the host to read at >= 1ms intervals. Use `FastReader` from `test_harness.py`. See [test suite README](https://github.com/daqifi/daqifi-python-test-suite#critical-usb-cdc-host-read-speed).

## Project Structure

```
firmware/
  daqifi.X/              # MPLAB X project
  src/
    HAL/                 # Hardware abstraction (ADC, DAC, DIO, Power, UI)
    services/            # Communication (USB CDC, WiFi, SD, SCPI, streaming)
    state/               # Board configuration, runtime config, data structures
    Util/                # Pools, buffers, logging, formatters
    config/default/      # Harmony-generated drivers and configuration
bootloader/              # USB bootloader project
```

## Related Repositories

| Repository | Description |
|------------|-------------|
| [daqifi-python-core](https://github.com/daqifi/daqifi-python-core) | Python API library |
| [daqifi-python-test-suite](https://github.com/daqifi/daqifi-python-test-suite) | Test scripts and benchmarks |
| [daqifi-core](https://github.com/daqifi/daqifi-core) | .NET library |
| [daqifi-desktop](https://github.com/daqifi/daqifi-desktop) | Windows desktop application |

## Documentation

- **[CLAUDE.md](CLAUDE.md)** — Comprehensive technical reference (architecture, memory, SCPI, benchmarks)
- **[Wiki](https://github.com/daqifi/daqifi-nyquist-firmware/wiki)** — SCPI command reference, LED patterns

