# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is the DAQiFi Nyquist firmware project - a multi-channel data acquisition device built on PIC32MZ microcontroller with FreeRTOS. The project consists of a USB bootloader and main application firmware.

## Build Instructions

### Prerequisites
- MPLAB X IDE (Microchip's development environment)
- XC32 Compiler for PIC32
- Harmony Configurator (MHC) for code generation

### Building Firmware Only
1. Open `firmware/daqifi.X` project in MPLAB X
2. In Project Properties, exclude all linker files from the build
3. Build and flash directly to the device

### Building Bootloader-Compatible Firmware
1. Open `firmware/daqifi.X` project in MPLAB X
2. In Project Properties, include `old_hv2_bootld.ld` linker script
3. Build the firmware
4. Flash the bootloader first: `bootloader/firmware/usb_bootloader.X`
5. Then flash the firmware using the Windows DAQiFi application or include it in bootloader Project Properties → Loading menu

### Building from Command Line (Windows with WSL/Linux)
1. Ensure MPLAB X is installed (typically in `C:\Program Files\Microchip\MPLABX`)
2. Clean build artifacts: `rm -rf firmware/daqifi.X/build/ dist/`
3. Build using MPLAB X's make utility:
   ```bash
   cd firmware/daqifi.X
   "/mnt/c/Program Files/Microchip/MPLABX/v6.25/gnuBins/GnuWin32/bin/make.exe" -f nbproject/Makefile-default.mk CONF=default build -j$(nproc)
   ```
   Note: `-j$(nproc)` uses all available CPU cores for faster builds
4. The output hex file will be in `dist/default/production/daqifi.X.production.hex` (approx 1MB)

Note: XC32 compiler is also available in Linux at `/opt/microchip/xc32/v4.60/bin/xc32-gcc`

### Programming with PICkit 4 from Command Line
1. Connect PICkit 4 to the device
2. Use ipecmd to program the hex file:
   ```bash
   cd firmware/daqifi.X
   "/mnt/c/Program Files/Microchip/MPLABX/v6.25/mplab_platform/mplab_ipe/ipecmd.exe" \
     -TPPK4 -P32MZ2048EFM144 -M -F"dist/default/production/daqifi.X.production.hex" -OL
   ```
3. The device will be erased and programmed automatically
4. Look for "Program Succeeded" message

Command options:
- `-TPPK4`: Use PICkit 4 as programmer
- `-P32MZ2048EFM144`: Target device
- `-M`: Program mode
- `-F`: Hex file to program
- `-OL`: Use loaded memories only

### Bootloader Entry
- Hold the user button for ~20 seconds until board resets
- Release button when LEDs light solid
- Hold button again until white LED blinks to enter bootloader mode

### Packaging Release Artifacts
For every prerelease and release, compress the production hex file into a zip labeled with the firmware revision:
```bash
zip -j "daqifi-nyquist-firmware-<VERSION>.zip" firmware/daqifi.X/dist/default/production/daqifi.X.production.hex
```
Example: `daqifi-nyquist-firmware-3.4.5b1.zip`. The version string comes from `FIRMWARE_REVISION` in `firmware/src/version.h`.

## Architecture Overview

### Component Hierarchy
```
Main Application (FreeRTOS Tasks)
├── USB Device Task (app_USBDeviceTask)
├── WiFi Task (app_WifiTask)
├── SD Card Task (app_SdCardTask)
├── Streaming Task
└── SCPI Command Processing
    
Hardware Abstraction Layer (HAL)
├── ADC (AD7173, AD7609, MC12bADC)
├── DAC (DAC7718)
├── DIO (Digital I/O)
├── Power Management (BQ24297)
└── UI (User Interface/LEDs)

Services Layer
├── Data Encoding (JSON, CSV, Protocol Buffers)
├── Communication (USB CDC, WiFi TCP Server, SCPI)
├── Storage (SD Card Manager)
└── Streaming Engine
```

### Key Data Structures

1. **Board Configuration** (`tBoardConfig` in `state/board/BoardConfig.h`)
   - Immutable hardware configuration
   - Defines available channels, modules, and capabilities
   - Three variants: NQ1, NQ2, NQ3

2. **Board Runtime Configuration** (`tBoardRuntimeConfig` in `state/runtime/BoardRuntimeConfig.h`)
   - Mutable runtime settings
   - Channel configurations, sampling rates, triggers

3. **Board Data** (`tBoardData` in `state/data/BoardData.h`)
   - Real-time acquisition data
   - Sample buffers and timestamps

### Communication Protocols

1. **SCPI Commands** - Standard Commands for Programmable Instruments
   - Entry point: `services/SCPI/SCPIInterface.c`
   - Modules: SCPIADC, SCPIDIO, SCPILAN, SCPIStorageSD
   - Used for device configuration and control
   - **SCPI Abbreviation Rule**: Commands can be abbreviated based on the capitalization in the full command. The abbreviated command must contain all letters that are in CAPS. For example:
     - `SYST:COMM:LAN:NETMode` can be abbreviated as `SYST:COMM:LAN:NETM`
     - `SYST:COMM:LAN:APPLy` can be abbreviated as `SYST:COMM:LAN:APPL`

#### SCPI Command Verification Protocol

**⚠️ CRITICAL: NEVER guess SCPI command syntax. ALWAYS verify first.**

**Mandatory Process Before Using ANY SCPI Command:**

1. **Search for the exact pattern** in `SCPIInterface.c`:
   ```bash
   grep -n "pattern.*<keyword>" firmware/src/services/SCPI/SCPIInterface.c
   ```

2. **Use the EXACT command** from the pattern definition - do not abbreviate unless testing abbreviations

3. **Verify parameter syntax** by reading the callback function implementation

**Common Mistakes to Avoid:**
- ❌ `SYST:STAR 5000` (guessed)
- ✅ `SYST:StartStreamData 5000` (verified from SCPIInterface.c:1521)

- ❌ `SYST:STOP` (guessed)
- ✅ `SYST:StopStreamData` (verified from SCPIInterface.c:1522)

- ❌ `SYST:STOR:SD:GET DAQiFi/file.csv` (unquoted path)
- ✅ `SYST:STOR:SD:GET "file.csv"` (quoted, verified from SCPIStorageSD.c)

**Verification Example:**
```bash
# Step 1: Search for streaming commands
grep -n "pattern.*Stream" firmware/src/services/SCPI/SCPIInterface.c
# Result: Line 1521: {.pattern = "SYSTem:StartStreamData", .callback = SCPI_StartStreaming,},

# Step 2: Use exact command
device._comm.send_command("SYST:StartStreamData 5000")  # Correct!
```

**If you use a SCPI command without verifying it first, STOP immediately and verify the syntax.**

#### SCPI Wiki Maintenance

**When adding, modifying, or removing SCPI commands, ALWAYS update the GitHub wiki SCPI reference page.**

The wiki lives in a separate repo:
```bash
git clone https://github.com/daqifi/daqifi-nyquist-firmware.wiki.git /tmp/daqifi-wiki
```

The SCPI command reference is in `01-SCPI-Interface.md`. Update the relevant tables:
- Command tables (add/remove/modify command rows)
- Response field tables (e.g., Streaming Statistics Response Fields)
- Register bit definition tables (OPER/QUES condition bits)
- Callback function names (must match actual firmware function names)

Commit and push wiki changes after updating.

2. **USB CDC** - Virtual COM port communication
   - Implementation: `services/UsbCdc/UsbCdc.c`
   - Handles command processing and data streaming

3. **WiFi** - Network communication
   - Manager: `services/wifi_services/wifi_manager.c`
   - TCP server for remote control
   - UDP announcements for device discovery

### Data Flow

1. **Acquisition Path**:
   - Hardware interrupts → HAL drivers → Sample buffers
   - ADC modules use DMA for high-speed acquisition
   - DIO uses interrupt-driven capture

2. **Streaming Path**:
   - Sample buffers → Encoder (JSON/CSV/ProtoBuf) → Output interface (USB/WiFi/SD)
   - Streaming engine manages buffer flow and encoding
   - Supports multiple simultaneous outputs

#### Voltage Output Precision

Systemwide configurable voltage precision via `StreamingRuntimeConfig.VoltagePrecision`. Applies to CSV streaming, JSON streaming, and SCPI voltage queries (`MEAS:VOLT:DC?`, `SOUR:VOLT:LEV?`).

**SCPI Commands:**
```bash
CONFigure:VOLTage:PRECision <0-10>   # Set precision
CONFigure:VOLTage:PRECision?         # Query current precision
CONFigure:VOLTage:SAVE               # Persist to NVM (survives reboot)
CONFigure:VOLTage:LOAD               # Load from NVM
```

| Value | Output | Example |
|-------|--------|---------|
| 0 | Integer millivolts | `1221` (fast path, uses `int_to_str`) |
| 4 | Volts, 4 decimal places | `1.2207` (NQ1 default, 12-bit ADC) |
| 6 | Volts, 6 decimal places | `1.220703` (NQ3 default, 18-bit AD7609) |
| 7 | Volts, 7 decimal places | `1.2207031` (NQ2 default, 24-bit AD7173) |

**Board-specific defaults** (`tBoardConfig.DefaultVoltagePrecision`): NQ1=4, NQ3=6, NQ2=7.

**NVM persistence**: Stored in `TopLevelSettings.voltagePrecision`. Saved via `CONF:DATA:SAVE`, loaded at boot from NVM. Falls back to board config default on first boot.

**Implementation:** `firmware/src/services/csv_encoder.c`, `firmware/src/services/JSON_Encoder.c`, `firmware/src/services/SCPI/SCPIInterface.h` (SCPI_ResultVoltage helper), `firmware/src/services/SCPI/SCPIADC.c`, `firmware/src/services/SCPI/SCPIDAC.c`

#### Streaming Statistics & Buffer Overrun Tracking

The streaming engine tracks data loss at every stage of the pipeline. Statistics are accumulated per streaming session (cleared on `StartStreamData`, preserved after `StopStreamData`).

**SCPI Commands:**
```bash
SYSTem:STReam:STATS?       # Query all statistics
SYSTem:STReam:CLEARSTATS   # Reset all counters
```

**Response fields from `SYSTem:STReam:STATS?`:**
| Field | Type | Description |
|-------|------|-------------|
| `TotalSamplesStreamed` | uint64 | Samples successfully queued from ISR |
| `TotalBytesStreamed` | uint64 | Total bytes encoded (offered to outputs) |
| `QueueDroppedSamples` | uint32 | Samples lost due to pool exhaustion or full sample queue (pool=700) |
| `UsbDroppedBytes` | uint32 | Data lost due to USB circular buffer full (16KB) |
| `WifiDroppedBytes` | uint32 | Data lost due to WiFi circular buffer full (14KB) |
| `SdDroppedBytes` | uint32 | Data lost due to SD write timeout/partial (8KB buf, 3 retries) |
| `EncoderFailures` | uint32 | Encoding attempts that returned 0 bytes with data available |
| `SampleLossPercent` | uint32 | `QueueDroppedSamples / (Total + Dropped) * 100` |
| `ByteLossPercent` | uint32 | `(USB + WiFi + SD dropped) / TotalBytesStreamed * 100` |
| `WindowLossPercent` | uint32 | Sliding-window sample loss % (0-100), updated every N samples |

**Thread safety:** `TotalSamplesStreamed` and `TotalBytesStreamed` are 64-bit counters (safe for week-long sessions) protected by `taskENTER_CRITICAL`/`taskEXIT_CRITICAL` on each increment and during snapshot reads. Drop counters remain 32-bit (atomic on PIC32MZ).

**Session-end logging:** When streaming stops, if any data was lost during the session, a `LOG_E` summary is automatically written with sample counts, per-buffer byte drops, and loss percentage. Retrieve via `SYST:LOG?`.

**Implementation:** `firmware/src/services/streaming.c` (StreamingStats struct), `firmware/src/services/SCPI/SCPIInterface.c` (SCPI callbacks)

#### SCPI Status Registers (OPER & QUES)

The firmware populates the IEEE 488.2 `STATus:OPERation` and `STATus:QUEStionable` condition registers to reflect real-time device state during streaming.

**SCPI Commands:**
```bash
STATus:OPERation:CONDition?      # Current OPER condition bits (non-destructive)
STATus:OPERation?                # OPER event register (latched, clears on read)
STATus:OPERation:ENABle <mask>   # Set OPER event enable mask
STATus:OPERation:ENABle?         # Query OPER event enable mask

STATus:QUEStionable:CONDition?   # Current QUES condition bits (non-destructive)
STATus:QUEStionable?             # QUES event register (latched, clears on read)
STATus:QUEStionable:ENABle <mask>  # Set QUES event enable mask
STATus:QUEStionable:ENABle?      # Query QUES event enable mask

STATus:PRESet                    # Reset enable masks to default
```

**OPERation Condition Register Bits:**
| Bit | Value | Name | Description |
|-----|-------|------|-------------|
| 4   | 16    | Measuring | Streaming/measuring is active |
| 10  | 1024  | SD Logging | SD card logging is active |

Set on `StartStreamData`, cleared on `StopStreamData`.

**QUEStionable Condition Register Bits:**
| Bit | Value | Name | Description |
|-----|-------|------|-------------|
| 4   | 16    | Data Loss | Windowed sample loss >= 5% |
| 8   | 256   | USB Overflow | USB circular buffer overflow detected |
| 9   | 512   | WiFi Overflow | WiFi circular buffer overflow detected |
| 10  | 1024  | SD Overflow | SD write failure detected |
| 11  | 2048  | Encoder Fail | Encoder returned 0 bytes with data available |

QUES bits are set in real-time during streaming and cleared when streaming stops. The QUES CONDition register reflects the *current* health; the QUES EVENt register latches transitions (clears on read).

**Windowed Data Flow Tracking:**
The `Data Loss` QUES bit (bit 4) uses a sliding double-buffer window to evaluate pipeline health independently of sample rate. Window size defaults to `clamp(frequency * 2, 20, 10000)` sample periods (~2 seconds of history). If the configured threshold percentage of samples in the window were dropped, the bit is set. The current window loss percentage is also reported as `WindowLossPercent` in `SYST:STR:STATS?`.

**Flow Window Configuration:**
```bash
SYSTem:STReam:LOSS:THREshold <pct>   # Set loss threshold 1-100% (default 5)
SYSTem:STReam:LOSS:THREshold?        # Query current threshold
SYSTem:STReam:LOSS:WINDow <samples>  # Set window size (0=auto, 20-10000=explicit)
SYSTem:STReam:LOSS:WINDow?           # Query current override (0=auto)
```

- **Threshold** takes effect immediately (next flow window check). Lower values (1-2%) for precision measurement; higher values (10-20%) for best-effort monitoring or noisy transports.
- **Window size** takes effect at next streaming start. Larger windows smooth out bursty loss; smaller windows detect faults faster. Auto mode (`0`) scales with frequency.
- Both settings survive across streaming sessions but reset to defaults on device reboot.

**Implementation:** `firmware/src/services/streaming.c` (gQuesBits, flow window), `firmware/src/services/SCPI/SCPIInterface.c` (OPER/QUES helpers, sync)

#### Streaming Frequency Capping

The firmware automatically caps the requested streaming frequency based on a three-constraint model validated against hardware benchmarks. The cap is applied silently — no SCPI error is returned, but a `LOG_I` message is written to the log buffer (retrievable via `SYST:LOG?`).

**Constraints:**
| Constraint | Limit | Formula |
|-----------|-------|---------|
| ISR ceiling | 11 kHz | Hard per-invocation overhead limit |
| Type 1 aggregate | 30 kHz total | `30000 / type1ChannelCount` |
| Per-tick budget | Scales with channels | `77000 / (6 + totalEnabledChannels)` |

**Effective limit:** `min(ISR_MAX, TYPE1_AGG / type1Count, BUDGET / (OVERHEAD + totalEnabled))`

**Example caps (all Type 1 channels):**
| Channels | Max Frequency |
|----------|--------------|
| 1 | 11,000 Hz |
| 5 | 6,000 Hz |
| 8 | 5,500 Hz |
| 16 | 3,500 Hz |

**Where capping is applied:**
- `SYSTem:StartStreamData <freq>` — caps frequency before starting the timer
- `CONFigure:ADC:CHANnel` — recalculates cap when channels are enabled/disabled during streaming

**Diagnosing capped frequency:** Check `SYST:LOG?` for `"Frequency capped: X Hz -> Y Hz"` messages. The actual streaming frequency is stored in the runtime config and can be verified by checking sample timestamps.

**Implementation:** `firmware/src/services/streaming.h` (`Streaming_ComputeMaxFreq`), `firmware/src/services/SCPI/SCPIInterface.c`, `firmware/src/services/SCPI/SCPIADC.c`

#### Test Pattern Streaming Mode

Test pattern mode replaces real ADC values with synthetic data for deterministic regression testing and benchmarking. The real ISR timing, ADC triggering, pool allocation, and full encoding pipeline are preserved — only the sample Value field is overridden.

**SCPI Commands:**
```bash
SYSTem:STReam:TESTpattern <pattern>   # Set pattern (0=off, 1-6)
SYSTem:STReam:TESTpattern?            # Query current pattern (0=disabled)
```

**Pattern Types:**

| Pattern | Name | Value Generated | Use Case |
|---------|------|----------------|----------|
| 0 | Off | Real ADC data | Normal operation |
| 1 | Counter | `(sampleCount + channelId) % (adcMax+1)` | Integrity verification (PC can predict exact values) |
| 2 | Midscale | `adcMax / 2` | Consistent encoding size across CSV/JSON/ProtoBuf |
| 3 | Fullscale | `adcMax` | Worst-case ProtoBuf variable-length encoding |
| 4 | Walking | `(sampleCount * (channelId+1)) % (adcMax+1)` | Multi-channel visual verification |
| 5 | Triangle | Ramps 0→adcMax→0, period=2*(adcMax+1) | Realistic waveform, staggered per channel |
| 6 | Sine | 256-sample sine wave, scaled to [0, adcMax] | Realistic signal testing, 45deg phase offset per channel |

- `adcMax` = max raw ADC code = Resolution - 1 (4095 for MC12bADC 12-bit, 262143 for AD7609 18-bit)
- Runtime-only (not persisted to NVM, resets on reboot)
- Sample counter resets at each `StartStreamData` for deterministic sessions
- Works with all encoding formats (CSV/JSON/ProtoBuf) and output interfaces (USB/WiFi/SD)

#### Streaming Throughput Benchmarking

Two benchmark tools for measuring streaming pipeline throughput:

**1. Benchmark Mode** (`SYST:STR:BENCHmark`): Bypasses the frequency cap so the timer ISR fires at any requested rate. Uses the real ISR→deferred task→encoder→output pipeline. Combine with test patterns for pure throughput measurement.

```bash
SYSTem:STReam:BENCHmark <0|1>     # 0=normal (freq cap active), 1=uncapped
SYSTem:STReam:BENCHmark?           # Query current mode
```

Usage:
```bash
SYST:STR:TESTpattern 2             # Midscale test data
SYST:STR:BENCHmark 1               # Uncap frequency
SYST:StartStreamData 11000         # Start at 11kHz (normally capped)
# ... wait ...
SYST:StopStreamData
SYST:STR:STATS?                    # Check throughput and drops
SYST:STR:BENCHmark 0               # Restore normal mode
```

When enabled, the deferred ISR task priority is lowered from 8 to 2 for fair scheduling with the encoder. Priority is saved and restored on disable.

**2. Self-Contained Throughput Test** (`SYST:STR:THRoughput`): Runs a complete benchmark internally — enables benchmark mode + test pattern, streams for the specified duration, stops, and returns all stats in one response.

```bash
SYSTem:STReam:THRoughput <freq>,<duration_sec>   # Run benchmark
```

Example: `SYST:STR:THR 5000,10` streams at 5kHz for 10 seconds.

**Note:** This command blocks the USB SCPI task for the duration. Use the Python test suite (`test_throughput_benchmark.py`) for reliable automated benchmarking.

**3. SD Write Benchmark** (pre-existing): Measures raw SD card write speed independent of streaming.

```bash
SYSTem:STORage:SD:BENCHmark <size_kb>,<pattern>  # Run (e.g., 1024,0)
SYSTem:STORage:SD:BENCHmark?                      # Query results: bytes,ms,bps
```

**Measured Throughput Ceilings (NQ1, test patterns):**

| Interface | Format | Channels | Max Zero-Loss Rate | KB/s |
|-----------|--------|----------|------------------:|-----:|
| USB | CSV | 1 | 15 kHz | 239 |
| USB | CSV | 16 | 3 kHz | 720 |
| USB | PB | 1 | 20 kHz | 51 |
| USB | PB | 8 | 11 kHz | 195 |
| USB | PB | 16 | 7 kHz | 211 |
| SD | CSV | 1 | 5 kHz | 85 |
| SD | CSV | 16 | 1 kHz | 266 |
| SD | PB | 1 | 11 kHz | 133 |
| SD | PB | 16 | 7 kHz | 177 |
| WiFi | PB | 16 | 1 kHz | 49 |
| WiFi | CSV | 16 | 1 kHz | 206 |
| SD | raw write | — | — | 665 |

**Benchmark variance:** USB throughput measurements show ~10-15% run-to-run variance (e.g., USB CSV 1ch@5kHz: 4739-5423 sps across 3 consecutive runs). Root cause unknown — likely USB CDC transfer scheduling, WSL USB passthrough jitter, or FreeRTOS task timing. Ceilings in the table above represent the highest zero-loss rate observed across multiple runs. When comparing benchmarks, run at least 3 times and use the average.


Update this table when new benchmark results are collected. Full benchmark history is version-controlled in `daqifi-python-test-suite/benchmarks/` (CSV files + CHANGELOG.md). Run `python test_throughput_benchmark.py <port> <duration>` and copy the output CSV to that directory.

**Implementation:** `firmware/src/services/streaming.c` (benchmark mode, gTestPattern, Streaming_GenerateTestValue), `firmware/src/services/SCPI/SCPIInterface.c` (SCPI callbacks)

### Board Variants

- **NQ1**: Basic variant configuration
- **NQ2**: Enhanced variant with additional features
- **NQ3**: Full-featured variant

Each variant has specific:
- Channel counts and types
- ADC/DAC configurations
- Power management settings
- UI configurations

#### Switching Between Board Variants

The firmware supports building for different board variants using MPLAB X configurations:

1. **In MPLAB X IDE**:
   - Right-click project → Set Configuration → Select "Nq1" or "Nq3"
   - Build the project

2. **From Command Line**:
   ```bash
   # Build NQ1 firmware
   "/mnt/c/Program Files/Microchip/MPLABX/v6.25/gnuBins/GnuWin32/bin/make.exe" \
     -f nbproject/Makefile-Nq1.mk CONF=Nq1 build -j4

   # Build NQ3 firmware
   "/mnt/c/Program Files/Microchip/MPLABX/v6.25/gnuBins/GnuWin32/bin/make.exe" \
     -f nbproject/Makefile-Nq3.mk CONF=Nq3 build -j4
   ```

3. **How It Works**:
   - Each configuration includes/excludes variant-specific files
   - `NqBoardConfig_Get()` returns the appropriate board configuration
   - `NqBoardRuntimeConfig_GetDefaults()` returns the appropriate runtime defaults
   - No code changes needed - just switch configuration and rebuild

4. **Variant-Specific Files**:
   - **NQ1**: `NQ1BoardConfig.c`, `NQ1RuntimeDefaults.c`
   - **NQ3**: `NQ3BoardConfig.c`, `NQ3RuntimeDefaults.c`

### MCU Reference: PIC32MZ2048EFM144

**Architecture**: MIPS32 M-Class (microAptiv), 200 MHz, 32-bit data bus
**Flash**: 2MB | **RAM**: 512KB | **L1 Cache**: 16KB I-cache + 4KB D-cache
**Package**: 144-pin LQFP
**Datasheet**: [DS60001320](https://www.microchip.com/en-us/product/PIC32MZ2048EFM144)
**Errata**: [DS80000663](https://ww1.microchip.com/downloads/aemDocuments/documents/MCU32/ProductDocuments/Errata/PIC32MZ-Embedded-Connectivity-with-Floating-Point-Unit-Family-Silicon-Errata-DS80000663.pdf) (Rev R, silicon A1/A3/B2)

#### Atomicity & Concurrency Rules

- **32-bit reads and writes are atomic** on the PIC32MZ bus. A simple `x = 0` or `return x` on a `uint32_t`/`uint16_t` does NOT need a critical section.
- **Read-modify-write** (`x |= bit`, `x &= ~bit`, `x++`, `x += n`) is NOT atomic — use `taskENTER_CRITICAL()`/`taskEXIT_CRITICAL()`.
- **64-bit operations** (`uint64_t` increment, struct copy) always need a critical section.
- **`volatile`** is needed when a variable is written by one task/ISR and read by another — it prevents the compiler from caching the value in a register. `volatile` alone does NOT make RMW atomic; you still need critical sections for `+=`, `|=`, etc.
- Do not add unnecessary critical sections around plain 32-bit stores/loads — it adds interrupt latency for no benefit.
- **ISR context vs task context**: `taskENTER_CRITICAL()`/`taskEXIT_CRITICAL()` is for **task context only**. Inside an ISR, use `taskENTER_CRITICAL_FROM_ISR()`/`taskEXIT_CRITICAL_FROM_ISR()`. In our firmware, hardware ISRs (streaming timer, ADC EOS) immediately defer to FreeRTOS tasks via `xTaskNotifyGive()`/`ulTaskNotifyTake()`, so all sample processing and critical section usage runs in task context — never directly in ISR handlers.
- **FPU in RTOS tasks**: Any task that uses floating-point (`float` or `double`) **must** call `portTASK_USES_FLOATING_POINT()` at the start of its task function, before any FP operations. This tells FreeRTOS to save/restore the 32×64-bit FPU registers on context switches. Without this call, FPU register state will be corrupted when switching between tasks. Currently registered: USB, WiFi, PowerAndUI, streaming, and deferred interrupt tasks.
- **`BoardRunTimeConfig_Get()` never returns NULL** — it indexes into a static array initialized at boot. Do not add NULL checks on its return value; no existing SCPI callback checks it, and adding guards creates inconsistency for zero safety benefit. Qodo/automated reviewers will repeatedly suggest this — ignore it.

#### Cache & DMA

- **L1 data cache is write-back** — DMA buffers MUST use `__attribute__((coherent))` or be placed in KSEG1 (uncached) to avoid stale data.
- **Cache line size**: 16 bytes — DMA buffers sharing a cache line with other data will cause corruption. Use `__attribute__((aligned(16)))`.
- **No hardware cache coherency** — software must invalidate/flush cache around DMA transfers. Harmony drivers handle this for SPI, USB, etc.
- **Coherent attribute** (`__attribute__((coherent, aligned(16)))`) maps buffers to uncached address space — simplest solution for DMA buffers.

#### Clock Tree (as configured)

| Clock | Source | Frequency | Peripherals |
|-------|--------|-----------|-------------|
| SYSCLK | SPLL (24MHz POSC × 50 / 3 / 2) | 200 MHz | CPU, REFCLK |
| PBCLK1 | SYSCLK/2 | 100 MHz | Watchdog |
| PBCLK2 | SYSCLK/2 | 100 MHz | I2C, UART, SPI (default) |
| PBCLK3 | SYSCLK/2 | 100 MHz | Timers, OC, IC |
| PBCLK5 | SYSCLK/2 | 100 MHz | Flash, Crypto, USB |
| REFCLK1 | SYSCLK (passthrough) | 200 MHz | SPI4 master clock (MCLKSEL=1) |

SPI4 BRG formula with REFCLK1: `SPI_CLK = 200MHz / (2 × (BRG + 1))`

#### Hardware FPU

The PIC32MZ2048**EF**M144 has a hardware 64-bit double-precision FPU (Coprocessor 1). All floating-point arithmetic (`double` multiply, divide, add, convert) executes in hardware — no software emulation.

- **Compiler**: targeting `PIC32MZ2048EFM144` implicitly sets `__mips_hard_float = 1`
- **FreeRTOS**: `configUSE_TASK_FPU_SUPPORT = 1` in `FreeRTOSConfig.h`; saves/restores 32×64-bit FPU registers on context switches for tasks that call `portTASK_USES_FLOATING_POINT()`
- **Registered tasks**: USB, WiFi, PowerAndUI, streaming, and deferred interrupt tasks use FPU
- **ADC voltage conversion**: `ADC_ConvertToVoltage()` uses native `mul.d`, `div.d`, `add.d` instructions

#### FreeRTOS Configuration

- **Tick rate**: 1000 Hz (1ms tick)
- **Preemptive** with time-slicing (equal-priority tasks round-robin)
- **Max priorities**: 10 (0=lowest, 9=highest)
- **Heap**: heap_4, 284KB (`configTOTAL_HEAP_SIZE`)
- **ISR stack**: 8192 bytes
- **Kernel interrupt priority**: 1 (lowest)
- **Max syscall interrupt priority**: 4 (ISRs at priority 5+ are never disabled by FreeRTOS)

#### Task Priority Map

| Priority | Task | Stack (words) | Peak Used | Notes |
|----------|------|--------------|-----------|-------|
| 8 | `_Streaming_Deferred_Interrupt_Task` | 512 | 214 | ISR deferral, sample collection, FPU |
| 8 | `MC12bADC_EosInterruptTask` | 160 | 80 | ADC end-of-scan deferred interrupt |
| 8 | `AD7609_DeferredInterruptTask` | 160 | 76 | AD7609 BSY pin handler |
| 7 | `app_PowerAndUITask` | 512 | 226 | UI + BQ24297 power, FPU |
| 7 | `app_USBDeviceTask` | 3072 | 1290 | SCPI callbacks use 512-byte locals |
| 2 | `streaming_Task` | 1392 | 692 | Encodes PB/CSV/JSON + outputs |
| 2 | `app_WifiTask` | 1024 | 360 | WiFi state machine + TCP |
| 2 | `app_SDCardTask` | 1024 | 468 | SD mount/write/read/list/delete |
| 2 | `lWDRV_WINC_Tasks` | 1024 | 290 | WINC1500 driver background |
| 2 | `fwUpdateTask` | 128 | 62 | WiFi FW update (dynamic) |
| 1 | `lAPP_FREERTOS_Tasks` | 1500 | 1156 | Boot init (77% used) |
| 1 | `F_USB_DEVICE_Tasks` | 144 | 72 | USB device stack |
| 1 | `F_DRV_USBHS_Tasks` | 144 | 72 | USB hardware driver |

Stack sizes profiled under stress: 16ch@5kHz PB/CSV/JSON + SD file ops + WiFi TCP + power cycles. Sized at 2-3x measured peak. Query at runtime: `SYST:MEM:STACk?`

**WARNING**: If recursive SD directory listing is enabled, `app_SDCardTask` needs 10KB+ (~550 bytes per nesting level).

**Scheduling implications**: The deferred ISR task (priority 8) preempts everything — it runs immediately when a streaming timer fires. The streaming encoder and all I/O tasks share priority 2, so they round-robin via time-slicing. USB device task starts at priority 2 then self-boosts to 7.

#### Known Silicon Errata (DS80000663R, verified against PDF pages 6-15)

All items verified against the actual errata document. Only issues affecting features we use are listed, with exact workaround text from Microchip.

| Issue | Module | Rev | Summary | Our Status |
|-------|--------|-----|---------|------------|
| #1 | Oscillator | All | **REFCLK cannot divide inputs >100 MHz.** | **Safe.** Errata workaround: "do not divide the SYSCLK and allow the destination peripheral (SPI) to divide it. Set RODIV and ROTRIM to 0." This is exactly our PR #219 approach (RODIV=0 passthrough). |
| #5 | Power-Saving | A1/A3 | Turning off REFCLK via PMD bits causes unpredictable behavior. | **Safe.** We never disable REFCLK via PMD. Not affected on B2/B3. |
| #6 | I2C | A1/A3 | Indeterminate I2C at >100 kHz and/or >500 bytes continuous. False collision detect, receive overflow, suspended transactions. All recoverable in software. | **Monitor.** I2C5 for BQ24297 at 100 kHz with mutex. Not affected on B2/B3 but good to know. |
| #8 | UART | All | **RX FIFO overflow → shift registers stop → UART loses sync.** Only recovery: toggle UART OFF/ON multiple times. | **Low risk.** Debug UART4 only. Workaround: ensure UART interrupt priority prevents RX overrun, or set URXISEL for earlier interrupt. |
| #9 | USB | All | USB won't function if USB PHY off in Sleep (USBSSEN=1). | **Safe.** Keep USBSSEN=0. |
| #16 | USB | All | No remote wake-up support (USBRIE in USBCRCON). | **N/A.** Inform host via USB descriptors. |
| #25/#26 | Crypto | All | Crypto DMA: no partial packets, no zero-length hash. | **N/A.** wolfSSL runs in software mode. |
| #27 | SPI | All | **SRMT bit falsely indicates TX complete** before last block shifts out. Does NOT affect Transmit Buffer Empty Interrupt (STXISEL=0). | **Safe.** Harmony SPI driver uses interrupts, not SRMT polling. |
| #37 | I2C | All | **SCL tLOW doesn't meet I2C spec at ≥400 kHz.** No workaround. | **Safe.** BQ24297 I2C at 100 kHz. Never use ≥400 kHz on this chip. |
| #38 | System Bus | B2/B3 | **Flash wait states at SYSCLK >184 MHz with ECC need 3 wait states** (not 2). <2% CPU impact with cache. | **Safe.** Verified: `PRECONbits.PFMWS = 3` and `ECCCON = 3` in `initialization.c:645-646`. |
| #39 | ADC | All | Excessive current through VREF- when external reference used and VREF- > AVss. | **Monitor.** Workaround: connect VREF- to AVss. Check NQ3 board schematic. |
| #40 | USB | All | FLUSH bit (USBIENCSRx<19>) doesn't flush TX FIFO properly. | **Safe.** Harmony USB driver: set FLUSH + clear TXPKTRDY simultaneously, repeat twice. |
| #42 | DMA | All | **DMA half-full interrupt can fire twice** when cleared at n/2 byte, re-triggers at (n/2)+1. | **Safe.** Workaround: clear CHDHIF along with CHBCIF. Harmony DMA driver handles this. |
| #44 | Timer2-9 | All | Timer match coinciding with sleep entry → interrupt may not fire. No workaround. | **Safe.** We never enter sleep during streaming. |
| #45 | Flash RTSP | All | Run-Time Self Programming of Configuration Words broken. No workaround. | **Safe.** NVM settings use regular flash pages, not config words. |

### Memory Considerations

#### Three Memory Regions

The firmware uses three distinct memory regions, each with different properties:

1. **Coherent Pool** (41KB static, DMA-safe, `firmware/src/Util/CoherentPool.c`)
   - Single `__attribute__((coherent, aligned(16)))` array in KSEG1 (uncached)
   - Bump allocator with named partitions, initialized at boot
   - Contains: SD circular buffer (32KB) + SD write buffer (8KB)
   - Query: `SYST:MEM:FREE?` → `CoherentPoolTotal`, `CoherentPoolFree`

2. **FreeRTOS Heap** (284KB, cached, `configTOTAL_HEAP_SIZE` in `FreeRTOSConfig.h`)
   - heap_4 (best-fit with coalescence), allocated from `.bss`
   - Contains: sample pool, circular buffers, task stacks, FreeRTOS internals
   - Query: `SYST:MEM:FREE?` → `HeapTotal`, `HeapFree`, `HeapMinEverFree`

3. **USB Coherent Struct** (~6KB static, `gRunTimeUsbSttings __attribute__((coherent))`)
   - USB CDC DMA buffers (read 512B + write 4KB) embedded in coherent struct
   - Must remain coherent for USB hardware driver DMA compatibility

#### Heap Allocation Map (349KB total, ~233KB used at boot)

| Consumer | Bytes | Source |
|----------|------:|--------|
| Sample pool (700 × 208) | 145,600 | `pvPortMalloc` in `AInSample.c` |
| Sample nextFree array (700 × 2) | 1,400 | `pvPortMalloc` in `AInSample.c` |
| Sample FreeRTOS queue | 2,880 | `xQueueCreate` in `AInSample.c` |
| USB circular buffer | 16,384 | `OSAL_Malloc` in `CircularBuffer.c` |
| WiFi circular buffer | 14,000 | `OSAL_Malloc` in `CircularBuffer.c` |
| Task stacks (14 tasks) | ~37,500 | `xTaskCreate` (profiled, see Task Priority Map) |
| DIO sample queue | ~3,200 | `xQueueCreate` in `DIOSample.c` |
| WiFi event queue | ~480 | `xQueueCreate` in `wifi_manager.c` |
| FreeRTOS TCBs, mutexes, kernel | ~5,000 | Kernel internals |
| WiFi driver (power-up) | ~18,000 | WINC1500 `OSAL_Malloc` at power-up |
| **Total used** | **~233,000** | |
| **Free after power-up** | **~115,000** | `xPortGetFreeHeapSize()` |

**Note**: `HeapMinEverFree` (high-water mark) should stay above 0. Monitor via `SYST:MEM:FREE?`. Task stack health via `SYST:MEM:STACk?`.

#### Dynamic Sample Pool

The sample pool is allocated from the FreeRTOS heap at boot and can be resized between streaming sessions via SCPI. The O(1) free-list allocation pattern is preserved — only the backing memory is heap-allocated instead of static.

- **Default size**: 500 samples (`DEFAULT_AIN_SAMPLE_COUNT` in `AInSample.h`)
- **Range**: 100–2000 samples (`MIN_AIN_SAMPLE_COUNT`–`MAX_AIN_SAMPLE_COUNT`)
- **Memory per sample**: ~208 bytes (`AInPublicSampleList_t`)
- **Resize**: `AInSampleList_Destroy()` frees old pool, `AInSampleList_Initialize(newSize)` allocates new
- **When resized**: At `Streaming_Start()` if `MemoryConfig.samplePoolCount` differs from current capacity
- **Cleared automatically** in `Streaming_Start()` — stale samples freed before new session

#### SCPI Dynamic Memory Configuration

All setters reject changes while streaming is active (`SCPI_ERROR_EXECUTION_ERROR`).
Settings take effect at next `StartStreamData`. Runtime-only — not NVM-persisted, reset on reboot.

```bash
SYSTem:MEMory:SD:BUFfer <bytes>       # Set SD circular buffer size
SYSTem:MEMory:SD:BUFfer?              # Query (default: 32768)
SYSTem:MEMory:WIFI:BUFfer <bytes>     # Set WiFi circular buffer size
SYSTem:MEMory:WIFI:BUFfer?            # Query (default: 14000)
SYSTem:MEMory:USB:BUFfer <bytes>      # Set USB circular buffer size
SYSTem:MEMory:USB:BUFfer?             # Query (default: 16384)
SYSTem:MEMory:SAMPle:POOL <count>     # Set sample pool depth (0=auto)
SYSTem:MEMory:SAMPle:POOL?            # Query (default: 500)
SYSTem:MEMory:FREE?                   # Full memory diagnostics
SYSTem:MEMory:AUTO                    # Auto-balance for enabled interfaces
```

**Setter Bounds:**

| Command | Min | Max | Constraint |
|---------|----:|----:|------------|
| `SD:BUFfer` | 4096 | 65536 | Must be multiple of 512 (sector alignment) |
| `WIFI:BUFfer` | 1400 | 65536 | Min = SOCKET_BUFFER_MAX_LENGTH |
| `USB:BUFfer` | 4096 | 65536 | Min = USBCDC_WBUFFER_SIZE |
| `SAMPle:POOL` | 0 or 100 | 2000 | 0 = auto (DEFAULT_AIN_SAMPLE_COUNT) |

**`SYST:MEM:FREE?` Response Fields:**

| Field | Description |
|-------|-------------|
| `HeapTotal` | Total FreeRTOS heap (284000) |
| `HeapFree` | Currently free heap bytes |
| `HeapUsed` | Currently used heap bytes |
| `HeapMinEverFree` | Lowest heap free since boot (high-water mark) |
| `CoherentPoolTotal` | Total coherent pool (41984) |
| `CoherentPoolFree` | Free coherent pool bytes |
| `SamplePoolCount` | Current sample pool depth |
| `SamplePoolBytes` | Sample pool data memory (count × 208) |
| `SampleNextFreeBytes` | Free-list array memory (count × 2) |
| `SampleQueueBytes` | FreeRTOS queue overhead estimate |

**`SYST:MEM:AUTO` Algorithm:**
1. Query enabled interfaces from `StreamingRuntimeConfig.ActiveInterface` and SD enable
2. Set default buffer sizes for enabled interfaces (SD:32768, WiFi:14000, USB:16384), 0 for disabled
3. Calculate remaining heap after buffers (with 20KB reserve)
4. Set sample pool to `remaining / sizeof(AInPublicSampleList_t)`, clamped to 100–2000

**Implementation:** `firmware/src/Util/CoherentPool.c` (pool), `firmware/src/state/data/AInSample.c` (dynamic pool), `firmware/src/services/SCPI/SCPIInterface.c` (SCPI callbacks), `firmware/src/state/runtime/StreamingRuntimeConfig.h` (MemoryConfig struct)

#### Memory Pressure Indicators
If heap allocation fails (`xPortGetFreeHeapSize()` returns low values):
1. **Query `SYST:MEM:FREE?`** — check `HeapMinEverFree` for high-water mark
2. **Reduce sample pool**: `SYST:MEM:SAMP:POOL 200` frees ~62KB heap
3. **Reduce circular buffers**: `SYST:MEM:USB:BUF 4096` or `SYST:MEM:WIFI:BUF 1400`
4. **Use `SYST:MEM:AUTO`**: auto-sizes based on active interfaces

#### Other Memory Constraints
- DMA buffers must be cache-aligned and in KSEG1 (coherent pool or coherent attribute)
- Bootloader reserves memory at 0x9D000000
- Application starts at 0x9D000480 (after bootloader)
- USB struct must remain `__attribute__((coherent))` — moving DMA buffers to pool causes boot failure

### Key Files for Understanding the System

1. **Application Entry**: `firmware/src/app_freertos.c` - Main application tasks
2. **Board Config**: `firmware/src/state/board/BoardConfig.c` - Hardware definitions
3. **Streaming Engine**: `firmware/src/services/streaming.c` - Data flow control
4. **SCPI Interface**: `firmware/src/services/SCPI/SCPIInterface.c` - Command processing
5. **HAL Drivers**: `firmware/src/HAL/ADC.c`, `DIO.c`, `DAC7718/DAC7718.c` - Hardware interfaces

### Development Considerations

- All hardware access must go through HAL layer
- SCPI commands follow IEEE 488.2 standard
- Use FreeRTOS primitives for synchronization
- Respect board variant differences in runtime checks
- Protocol buffer definitions in `services/DaqifiPB/DaqifiOutMessage.proto`

### DAC7718 Integration (NQ3 Board)

The NQ3 board variant includes a DAC7718 8-channel 12-bit DAC for analog output functionality:

#### Hardware Configuration
- **SPI Interface**: SPI2 at 10 MHz (configurable)
- **Control Pins**: 
  - CS (Chip Select): RK0 (GPIO_PIN_RK0)
  - CLR/RST (Reset): RJ13 (GPIO_PIN_RJ13) 
  - LDAC: Tied to 3.3V (no GPIO control)
- **Power Requirements**: Requires 10V rail (available in POWERED_UP state)
- **Channels**: 8 analog output channels (0-7)
- **Resolution**: 12-bit (4096 levels)

#### SCPI Commands for DAC Control
```bash
# Set DAC channel voltage
SOURce:VOLTage:LEVel 0,5.0        # Set channel 0 to 5.0V
SOURce:VOLTage:LEVel 5.0          # Set all channels to 5.0V

# Read DAC channel voltage  
SOURce:VOLTage:LEVel? 0           # Read channel 0 voltage
SOURce:VOLTage:LEVel?             # Read all channel voltages

# Channel enable/disable
ENAble:SOURce:DC 0,1              # Enable channel 0
ENAble:SOURce:DC? 0               # Get channel 0 enable status

# Configuration commands
CONFigure:DAC:RANGe 0,1           # Set channel 0 range
CONFigure:DAC:UPDATE              # Update all DAC outputs

# Calibration commands
CONFigure:DAC:chanCALM 0,1.0      # Set channel 0 calibration slope
CONFigure:DAC:chanCALB 0,0.0      # Set channel 0 calibration offset
CONFigure:DAC:SAVEcal             # Save user calibration values
CONFigure:DAC:LOADcal             # Load user calibration values
```

#### Integration Points
- **Board Configuration**: `state/board/AOutConfig.h` - Analog output configuration structure
- **SCPI Module**: `services/SCPI/SCPIDAC.c` - DAC command implementation  
- **HAL Driver**: `HAL/DAC7718/DAC7718.c` - Hardware abstraction layer
- **Runtime Config**: `state/runtime/AOutRuntimeConfig.h` - Runtime channel states

#### Power-Safe Initialization
The DAC7718 uses lazy initialization that:
1. Initializes global structures at startup (power-independent)
2. Defers hardware initialization until first DAC command when power is sufficient
3. Checks for POWERED_UP state (provides 10V rail) before hardware access
4. Gracefully handles power state transitions

### BQ24297 IINLIM Management

The firmware uses a timer-based state machine to manage input current limits (IINLIM) on the BQ24297 battery management IC, replacing the earlier DPDM-dependent approach.

#### State Machine
```
IDLE → WAIT_DPDM (1s min, 3s timeout) → WAIT_USB (5s) → SETTLED
```

- **OTG pin** driven LOW (output) so DPDM starts at conservative 100mA
- **WAIT_DPDM**: Waits for BQ24297 DPDM detection to complete (REG07 bit 7 clears)
- **WAIT_USB**: Sets 500mA, waits 5s checking `UsbCdc_IsConfigured()`
- **SETTLED**: USB host → 500mA; wall charger → 2000mA
- VBUS loss in any state resets to IDLE

#### SCPI Diagnostic Commands
```bash
SYST:POW:BQ:REGisters?     # Dump all BQ24297 registers (REG00-REG0A hex values)
SYST:POW:BQ:ILIM <0-7>     # Set IINLIM directly (0=100mA, 2=500mA, 6=2A, 7=3A)
SYST:POW:BQ:DPDM           # Force DPDM re-detection (WARNING: disrupts USB — see below)
SYST:POW:BQ:DIAGnostics?   # Comprehensive diagnostics dump (battery, registers,
                            # GPIO, IINLIM state machine, power state, VBUS level)
```

**Note:** `SYST:POW:BQ:DIAG?` calls `BQ24297_UpdateStatus()` which reads REG09 and clears latched fault flags as a side effect.

**WARNING:** `SYST:POW:BQ:DPDM` is diagnostic-only. It forces the BQ24297 to re-run D+/D- detection and prints the result (VBUS type from REG08), but does **not** update the status struct or reset the IINLIM state machine — no automatic current switching occurs. Use `SYST:POW:BQ:ILIM` to manually set IINLIM after forced DPDM. When connected via USB, DPDM temporarily resets IINLIM (potentially to 100mA), causing VBUS sag and USB disconnect. A physical cable replug is required to recover and restart the IINLIM state machine.

#### Implementation
- **State machine**: `HAL/BQ24297/BQ24297.c` — `BQ24297_ManageIINLIM()`
- **Caller**: `HAL/Power/PowerApi.c` — `Power_Tasks()` (~100ms interval)
- **USB tracking**: `services/UsbCdc/UsbCdc.c` — `UsbCdc_IsConfigured()`
- **SCPI commands**: `services/SCPI/SCPIInterface.c`
- **I2C mutex**: `BQ24297_Read_I2C()` and `BQ24297_Write_I2C()` are protected by a FreeRTOS mutex to synchronize access between PowerAndUITask and USBDeviceTask (both priority 7)

## Firmware Analysis Report

A comprehensive technical analysis of the firmware has been completed and is available in `FIRMWARE_ANALYSIS_REPORT.md`. This report provides:

### Critical Findings
- **Performance Bottleneck**: 700-byte buffer limitation constraining all communication channels
- **Safety Risk**: Shared SPI bus between WiFi and SD card without mutex protection
- **Architecture Issues**: Flat task priority structure causing scheduling inefficiencies
- **Memory Management**: Dynamic allocation in real-time streaming paths

### Key Recommendations
1. **IMMEDIATE**: Implement SPI bus mutex protection
2. **IMMEDIATE**: Replace shared buffer with per-channel buffers
3. **HIGH**: Restructure FreeRTOS task priorities for real-time performance
4. **HIGH**: Implement UDP streaming for high-throughput applications
5. **MEDIUM**: Optimize memory management and eliminate dynamic allocation from streaming

### Performance Projections
- **Current WiFi**: ~5-10 Mbps → **Optimized**: 50-80 Mbps
- **Current SD**: ~2-5 MB/s → **Optimized**: 15-25 MB/s
- **Current ADC**: ~1 kHz → **Optimized**: 10+ kHz sample rates

The report includes detailed technical analysis, code examples, and a prioritized roadmap for optimization. Reference this document before making performance-critical modifications to understand current limitations and optimization opportunities.

### SD Card File Splitting

**Feature:** Automatic file splitting prevents FAT32 4GB file size limit issues during long logging sessions.

**Configuration:**
```bash
# Set maximum file size (range: 1000 bytes to 4GB)
SYST:STOR:SD:MAXSize 2000000000    # 2GB limit
SYST:STOR:SD:MAXSize 0              # Use filesystem maximum (3.9GB default)
SYST:STOR:SD:MAXSize?               # Query current setting
```

**File Naming:**
- First file: Uses original name (e.g., `experiment.csv`)
- Split files: Sequential 4-digit numbering (e.g., `experiment-0001.csv`, `experiment-0002.csv`)
- Supports up to 9999 files per session (~39TB @ 3.9GB each)

**CSV Headers:**
- First file: Full CSV header (device info, column names with "ain" prefix)
- Split files: Data rows only (no headers) - simplifies merging

**Safety Features:**
- Default: 3.9GB limit (100MB below FAT32 maximum)
- Minimum: 1000 bytes (prevents rapid rotation)
- Circular buffer draining before rotation (zero data loss)
- Unconditional filesystem flush before file close

**Python Tools:**
- `download_sd_files.py` - Auto-detects and groups split files
- `analyze_split_files.py` - Validates integrity, merges parts

**Implementation:** `firmware/src/services/sd_card_services/sd_card_manager.c`

### SD Card Sector-Aligned Writes

The WRITE_TO_FILE state extracts data from the circular buffer in 512-byte sector-aligned chunks. This allows FatFS to use its fast path (`disk_write()` directly from the user buffer) instead of the per-sector `memcpy` + dirty-flag path. Measured improvement: ~55% throughput gain on SPI-mode SD cards (~500 KB/s vs ~320 KB/s with same benchmark method).

**Key design rules:**
- **Normal writes** (WRITE_TO_FILE loop): Wait for ≥512 bytes available, extract in sector multiples up to `WBUFFER_SIZE`
- **Drain paths** (rotation and UNMOUNT): Do NOT sector-align — must flush all remaining data including sub-sector tails
- **UNMOUNT_DISK**: Drains both pending writeBuffer and circular buffer before file close to prevent data loss on streaming stop

**Implementation:** `firmware/src/services/sd_card_services/sd_card_manager.c` (search for `SD_SECTOR_SIZE_BYTES`)

### Logging System

The firmware includes a compile-time configurable logging system with per-module log level control. Logs are stored in a circular buffer and can be retrieved via SCPI commands.

**Log Levels:**

| Level | Macro | Value | Use Case |
|-------|-------|-------|----------|
| NONE | `LOG_LEVEL_NONE` | 0 | Disable all logging for a module |
| ERROR | `LOG_LEVEL_ERROR` | 1 | Unexpected failures, hardware errors, critical issues |
| INFO | `LOG_LEVEL_INFO` | 2 | State changes, significant events (connection, mode changes) |
| DEBUG | `LOG_LEVEL_DEBUG` | 3 | Verbose diagnostics, data flow tracing, development |

**Production Default:** All modules default to `LOG_LEVEL_ERROR`. Only `LOG_E()` calls produce output; `LOG_I()` and `LOG_D()` compile to nothing (zero runtime cost).

**SCPI Commands:**
```bash
SYST:LOG?          # Retrieve all log messages (clears buffer after dump)
SYST:LOG:CLEAR     # Clear log buffer without reading
SYST:LOG:TEST      # Add test messages (for verification)
```

**Circular Buffer Behavior:**
- Capacity: 64 messages, 128 bytes each
- When full: Oldest message is dropped to make room for new
- Thread-safe: Protected by FreeRTOS mutex
- ISR-safe: Detects ISR context and skips buffering (prevents deadlock)

**Enabling Verbose Logging (Development Only):**

**IMPORTANT: Always reset all module log levels back to `LOG_LEVEL_ERROR` in `Logger.h` before release builds.** Debug/info logging adds overhead and fills the circular buffer, masking real errors in production.

To enable INFO or DEBUG logging for a specific module, change its level in `Logger.h`:
```c
// Change from:
#define LOG_LEVEL_WIFI      LOG_LEVEL_ERROR

// To (for debugging):
#define LOG_LEVEL_WIFI      LOG_LEVEL_DEBUG
```

Or define in the module's .c file before including Logger.h:
```c
#define LOG_LVL LOG_LEVEL_DEBUG  // Enable all logging for this file
#include "Util/Logger.h"
```

**Available Module Log Levels:**
- `LOG_LEVEL_POWER` - Power management (BQ24297, state transitions)
- `LOG_LEVEL_WIFI` - WiFi manager, TCP server, UDP discovery
- `LOG_LEVEL_BQ24297` - Battery charger IC
- `LOG_LEVEL_SD` - SD card operations
- `LOG_LEVEL_USB` - USB CDC interface
- `LOG_LEVEL_SCPI` - SCPI command processing
- `LOG_LEVEL_ADC` - ADC drivers (AD7609, MC12bADC)
- `LOG_LEVEL_DAC` - DAC7718 driver

**Real-Time UART Logging (Development Only):**

For live debugging without USB, enable ICSP UART output:
```c
// In project defines or Logger.h:
#define ENABLE_ICSP_REALTIME_LOG 1
```
This outputs logs via UART4 on ICSP pin 4 (RB0) at 921600 baud. Must be disabled before release.

**Implementation:** `firmware/src/Util/Logger.c`, `firmware/src/Util/Logger.h`

## Development Notes

### Building with Linux/WSL

The project can be built using Microchip tools in WSL/Linux:

#### Prerequisites in WSL
- MPLAB X IDE v6.25: `/opt/microchip/mplabx/v6.25/`
- XC32 Compiler v4.60: `/opt/microchip/xc32/v4.60/`
- Java Runtime (for IPE): `sudo apt install default-jre-headless`
- USB tools (for device access): `sudo apt install usbutils`

#### Building from Command Line
1. Navigate to project directory:
   ```bash
   cd /mnt/c/Users/User/Documents/GitHub/daqifi-nyquist-firmware/firmware/daqifi.X
   ```

2. Generate Linux-compatible makefiles:
   ```bash
   prjMakefilesGenerator -v .
   ```

3. Clean build directories (if previous build failed):
   ```bash
   rm -rf build dist
   ```

4. Build the project:
   ```bash
   make -f nbproject/Makefile-default.mk CONF=default build -j4
   ```

5. Output hex file location:
   ```
   dist/default/production/daqifi.X.production.hex (approx 1MB)
   ```

#### Programming with PICkit 4

##### From WSL (Verified Working Method)
```bash
"/mnt/c/Program Files/Microchip/MPLABX/v6.25/mplab_platform/mplab_ipe/ipecmd.exe" \
  -TPPK4 -P32MZ2048EFM144 -M -F"dist/default/production/daqifi.X.production.hex" -OL
```

##### From Windows PowerShell or Command Prompt
1. Navigate to project directory:
   ```cmd
   cd C:\Users\User\Documents\GitHub\daqifi-nyquist-firmware\firmware\daqifi.X
   ```

2. Program the device:
   ```cmd
   C:\"Program Files"\Microchip\MPLABX\v6.25\mplab_platform\mplab_ipe\ipecmd.exe -TPPK4 -P32MZ2048EFM144 -M -F"dist\default\production\daqifi.X.production.hex" -OL
   ```

**Important**: The `-OL` flag is required for successful programming

2. For Linux builds, the hex file will be at the WSL path:
   ```
   \\wsl$\Ubuntu\mnt\c\Users\User\Documents\GitHub\daqifi-nyquist-firmware\firmware\daqifi.X\dist\default\production\daqifi.X.production.hex
   ```

### Device Testing and SCPI Communication

#### Important Testing Notes

1. **Picocom Limitations — Use Python Libraries for Streaming Tests**

   Picocom is suitable ONLY for simple SCPI command/response testing (non-streaming). It has critical limitations that cause device crashes and test failures:

   **Problem 1: `-x` timeout is idle-based, not hard timeout.** When the device is streaming (PB or CSV), picocom receives continuous data and the "exit after N ms of inactivity" condition never triggers. The process hangs indefinitely.

   **Problem 2: Killing picocom during streaming corrupts WSL serial state.** After `kill -9 picocom`, the serial port often becomes unresponsive (opens but no data flows). Requires physical USB cable unplug/replug to recover.

   **Problem 3: Rapid picocom open/close can crash the device.** Each picocom session toggles DTR/RTS. Rapid cycling during streaming or buffer resize can cause USB CDC state corruption (Windows Code 43: "USB device descriptor failed"). Requires reprogramming to recover.

   **Recommended approach for streaming tests:**
   - Use `daqifi-python-core` library (`pip install -e /tmp/daqifi-python-core`)
   - `NyquistDevice` class handles binary PB framing, serial buffering, and clean disconnect
   - For SCPI commands not wrapped by python-core, use `device._comm.send_command("SYST:MEM:FREE?")`
   - See `daqifi-python-test-suite` for comprehensive test examples

   **Picocom safe usage (non-streaming only):**
   ```bash
   # Good: simple command/response
   (echo -e "*IDN?\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 | tail -5

   # Bad: streaming — picocom will hang
   (echo -e "SYST:StartStreamData 1000\r"; sleep 5; echo -e "SYST:StopStreamData\r") | picocom ...

   # Never use 2>&1 with picocom
   ```

   **Recovery if picocom hangs or device crashes:**
   ```bash
   pkill -9 picocom; fuser -k /dev/ttyACM0; rm -f /var/lock/LCK..ttyACM0
   # If port unresponsive: physically unplug/replug USB cable
   # If Windows shows Code 43: reprogram device via IPE
   ```

2. **Always use Python test suite for automated/streaming tests**

   Shell scripts with `dd | strings | grep` or picocom pipes are unreliable for:
   - Binary ProtoBuf data (corrupts text parsing)
   - Stats queries after streaming (stale data in serial buffer)
   - Multi-step test sequences (timing-dependent)
   - Throughput benchmarking (need precise timing)

   Use `daqifi-python-test-suite` and `daqifi-python-core`:
   ```bash
   cd /tmp
   git clone https://github.com/daqifi/daqifi-python-core.git
   git clone https://github.com/daqifi/daqifi-python-test-suite.git
   pip install --break-system-packages -e ./daqifi-python-core
   ```

   For new SCPI commands not yet in python-core, use `device._comm.send_command()`.
   Add new test scripts to the test suite when building new features.

3. **Power State Required**: Always power up the device before attempting WiFi or DAC operations
   ```bash
   # Check power state (0=STANDBY, 1=POWERED_UP, 2=POWERED_UP_EXT_DOWN)
   (echo -e "SYST:POW:STAT?\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 | tail -5
   
   # Power up device for full functionality (enables 10V rail needed for DAC7718)
   (echo -e "SYST:POW:STAT 1\r"; sleep 1) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 | tail-5
   ```

3. **Serial Buffer Timing**: When issuing multiple SCPI commands via picocom (especially `LISt?`, `LOG?`, or other commands that produce large output), wait long enough for the full response to be received before sending the next command. If the previous response hasn't fully drained from the serial buffer, the next command's picocom session will capture stale data mixed with the new response. For large responses (SD file listings, file downloads), use `sleep 5` or longer between commands. The `SYSTem:STORage:SD:GET` command is particularly sensitive — ensure the serial buffer is clean before issuing it.

4. **Use Exact SCPI Commands**: Don't guess command syntax. Check the actual command table in `SCPIInterface.c`
   ```bash
   # Correct commands (from SCPIInterface.c):
   SYST:COMM:LAN:SSIDSTR?    # Signal strength (not SSID:STR)
   SYST:COMM:LAN:NETTYPE?     # Network type (not MODE or NETWORK)
   SYST:COMM:LAN:ADDR?        # IP address (not ADDRESS or IP)
   ```

#### USB Device Access from WSL
1. **Initial device attachment** (from Windows PowerShell as admin):
   ```powershell
   usbipd list
   # Find DAQiFi device (e.g., "USB Serial Device (COM3)")
   usbipd attach --wsl --busid <BUSID>
   ```
   Note: May require `--force` if USBPcap filter is installed

2. **Reconnecting device from WSL side** (after PC sleep or device reprogramming):
   ```bash
   # List USB devices to find DAQiFi
   powershell.exe -Command "usbipd list"
   # Look for: 2-4    04d8:f794  USB Serial Device (COM3)
   
   # Attach from WSL
   powershell.exe -Command "usbipd attach --wsl --busid 2-4"
   
   # Wait for device to appear
   sleep 2 && ls -la /dev/ttyACM*
   ```

3. **Verify device in WSL**:
   ```bash
   lsusb | grep -i "04d8"
   # Should show: Microchip Technology, Inc. Nyquist
   
   ls -la /dev/ttyACM*
   # Should show: /dev/ttyACM0
   ```

#### Sending SCPI Commands
Use picocom for reliable serial communication:
```bash
# Query device identification
(echo -e "*IDN?\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -20

# Common SCPI commands:
# WiFi configuration
(echo -e "SYST:COMM:LAN:SSID?\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -20
(echo -e "SYST:COMM:LAN:SSID \"NetworkName\"\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -20

# Power management
(echo -e "SYST:POW:STAT?\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -20
(echo -e "SYST:POW:STAT 2\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -20
```

Power states:
- 0: STANDBY (MCU on if USB powered, off if battery powered)
- 1: POWERED_UP (Board fully powered, all modules active)
- 2: POWERED_UP_EXT_DOWN (Partial power, external power disabled - low battery mode)

#### Windows Network Access
Check WiFi networks from WSL using Windows netsh:
```bash
# List all visible WiFi networks
powershell.exe -Command "netsh wlan show networks"

# Check current WiFi interface status
powershell.exe -Command "netsh wlan show interfaces"
```

#### DAQiFi WiFi Access Point
When powered up, the device creates an open WiFi access point:
- Default SSID: "DAQiFi-XXXX" (where XXXX is the last 4 hex digits of the MAC address, e.g. "DAQiFi-95A7")
- Default Hostname: Same as SSID (used for DHCP Option 12 network discovery)
- Authentication: Open (no password)
- The device must be in POWERED_UP state for WiFi to operate
- The MAC suffix is automatically applied whenever the SSID or hostname equals the bare default "DAQiFi" (including after factory reset and APPLY). User-customized values are preserved without suffix.

### Automated Testing Notes

#### Quick Device Recovery Script
Create a script to quickly reconnect the device:
```bash
#!/bin/bash
# reconnect_device.sh
echo "Reconnecting DAQiFi device..."
powershell.exe -Command "usbipd attach --wsl --busid 2-4"
sleep 2
if [ -e /dev/ttyACM0 ]; then
    echo "Device connected at /dev/ttyACM0"
else
    echo "Device not found, checking USB..."
    lsusb | grep -i "04d8"
fi
```

#### WiFi Configuration Test Sequence
1. **Power off**: `SYST:POW:STAT 0`
2. **Configure SSID**: `SYST:COMM:LAN:SSID "TestSSID"`
3. **Apply settings**: `SYST:COMM:LAN:APPLY`
4. **Power on**: `SYST:POW:STAT 1`
5. **Check networks**: `cmd.exe /c "netsh wlan show networks"`
6. **Save settings**: `SYST:COMM:LAN:SAVE`

#### Key Testing Insights
- WiFi initialization takes ~2-3 seconds after power up
- APPLY command needed to activate runtime settings
- SAVE command needed to persist to NVM
- Device may load saved NVM settings on power up, not runtime settings
- Power states: 0=STANDBY, 1=POWERED_UP, 2=POWERED_UP_EXT_DOWN

### Shell Script Wrapper for Device Commands

To avoid permission prompts with complex piped commands, create temporary shell scripts:

**IMPORTANT**: Always use the filename `/tmp/temp.sh` for test scripts. This allows permissions to persist once approved in `.claude/settings.local.json`.

```bash
#!/bin/bash
# Always use: /tmp/temp.sh
send_cmd() {
    local cmd="$1"
    local delay="${2:-0.5}"
    (echo -e "${cmd}\r"; sleep $delay) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -20
}

# Use the function
send_cmd "SYST:POW:STAT 0" 1
send_cmd "*IDN?"
```

Then execute with:
```bash
chmod +x /tmp/temp.sh
dos2unix /tmp/temp.sh  # Fix line endings if needed
/tmp/temp.sh
```

This approach:
- Avoids complex inline piped commands that trigger prompts
- Makes tests repeatable and easier to debug
- Uses consistent filename `/tmp/temp.sh` for persistent permissions
- Add to `.claude/settings.local.json` permissions: `"Bash(/tmp/temp.sh:*)"`

### Git Configuration
- Ignore line ending changes when reviewing diffs (Windows/Linux compatibility)
- ALWAYS test changes on hardware before committing
- Use descriptive commit messages that explain the problem and solution

### Commit Message Format

This project uses **Conventional Commits** format for all commit messages:

```
<type>(<scope>): <subject>

<body>

<footer>
```

#### Types
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation only changes
- `style`: Changes that don't affect code meaning (white-space, formatting)
- `refactor`: Code change that neither fixes a bug nor adds a feature
- `perf`: Performance improvement
- `test`: Adding missing tests or correcting existing tests
- `build`: Changes to build system or dependencies
- `ci`: Changes to CI configuration files and scripts
- `chore`: Other changes that don't modify src or test files

#### Examples
```
fix(power): enable BQ24297 OTG mode for battery operation

The device was powering off when USB disconnected due to insufficient
voltage for the 3.3V regulator. OTG boost mode provides 5V from battery.

Fixes #23
```

```
feat(scpi): add battery diagnostics to SYST:INFO command

Added comprehensive battery status information including charge state,
voltage, NTC status, and power-up readiness.
```

#### Guidelines
- Use present tense ("add" not "added")
- Use imperative mood ("fix" not "fixes" or "fixed")
- First line should be 72 characters or less
- Reference issues and PRs in the footer
- Breaking changes should be noted with `BREAKING CHANGE:` in the footer

## Continuous Testing and Automation

### Setting Up for Unprompted Testing

1. **Permissions Configuration** (`.claude/settings.local.json`):
   ```json
   {
     "permissions": {
       "allow": [
         "Bash(*)"
       ],
       "deny": []
     }
   }
   ```
   - Use wildcard `Bash(*)` for unrestricted testing
   - Avoids interruption prompts during test execution
   - Can be more restrictive for production use

2. **Device Connection from WSL**:
   ```bash
   # List USB devices
   powershell.exe -Command "usbipd list"
   
   # Attach DAQiFi device (usually 2-4)
   powershell.exe -Command "usbipd attach --wsl --busid 2-4"
   
   # Verify connection
   ls -la /dev/ttyACM0
   ```

3. **Programming Device from WSL**:
   ```bash
   # Using Windows IPE (more reliable than WSL passthrough)
   cmd.exe /c "C:\"Program Files\"\\Microchip\\MPLABX\\v6.25\\mplab_platform\\mplab_ipe\\ipecmd.exe -P32MZ2048EFM144 -TPPK4 -F\"C:\\path\\to\\firmware.hex\" -M -OL"
   
   # IMPORTANT: After programming, always reattach the device to WSL
   powershell.exe -Command "usbipd attach --wsl --busid 2-4"
   ```

### Real-Time Testing Guidelines

1. **Avoid Echo Output to Windows**:
   - Don't echo test results - Claude should observe and report internally
   - Reduces prompting issues and improves test speed
   - Example: Instead of `echo "Test passed"`, observe results and summarize

2. **Efficient Test Commands**:
   ```bash
   # Good: Direct picocom command with minimal output
   (echo -e "*IDN?\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -5
   
   # Avoid: Multiple echo statements that trigger prompts
   ```

3. **State Management for Testing**:
   - Always ensure device is in known state before tests
   - Power cycle if needed: `SYST:POW:STAT 0` then `SYST:POW:STAT 1`
   - Clear error queue: `SYST:ERR?` until "No error"
   - Abort ongoing operations: `ABOR`

4. **Test Automation Scripts**:
   - Create self-contained test scripts that manage state
   - Include setup, test, and cleanup phases
   - Return clear pass/fail status
   - Log results to files for post-analysis

### Testing Best Practices

1. **Device State Verification**:
   ```bash
   # Check device ready
   *IDN?  # Should return DAQiFi info
   
   # Check power state
   SYST:POW:STAT?  # 0=STANDBY, 1=POWERED_UP, 2=POWERED_UP_EXT_DOWN
   
   # Check errors
   SYST:ERR?  # Should be "No error"
   ```

2. **Common Testing Patterns**:
   - Input filtering: Send dangerous characters, verify they're filtered
   - Command execution: Send valid commands, verify responses
   - Error handling: Send invalid commands, check error messages
   - State persistence: Change settings, power cycle, verify retained

3. **Automated Test Execution**:
   - Use bash scripts with proper error handling
   - Capture output for analysis without displaying
   - Report summary results only
   - Example structure:
     ```bash
     #!/bin/bash
     # Setup
     device_setup() { ... }
     
     # Tests
     test_feature() { 
         result=$(send_command "...")
         [[ "$result" == "expected" ]] && return 0 || return 1
     }
     
     # Run tests and report
     device_setup
     test_feature && echo "PASS" || echo "FAIL"
     ```

### DAQiFi Test & API Repositories

The DAQiFi GitHub organization (https://github.com/daqifi) hosts companion libraries and test suites useful for firmware validation and regression testing:

#### Python
- **[daqifi-python-core](https://github.com/daqifi/daqifi-python-core)** — Python API library for DAQiFi Nyquist devices
  - `daqifi/` package: NyquistDevice class, channels, streaming, discovery, SCPI internals
  - `examples/`: basic_adc, basic_dac, basic_dio, device_discovery, streaming (async/basic/callback/context)
  - Dependencies: pyserial>=3.5, Python 3.7+

- **[daqifi-python-test-suite](https://github.com/daqifi/daqifi-python-test-suite)** — Comprehensive firmware test suite
  - `comprehensive_test.py` — YAML-based full test framework
  - `test_sd_card.py` — Interactive SD card testing menu
  - `test_usb_transfer_integrity.py` — USB data corruption testing
  - `test_5khz_single_channel.py`, `test_high_speed_rates.py` — ADC performance tests
  - `download_sd_files.py`, `analyze_split_files.py` — SD file split handling/validation
  - `analyze_csv_integrity_v3.py` — CSV data integrity analysis

#### .NET
- **[daqifi-core](https://github.com/daqifi/daqifi-core)** — .NET library (net8.0/net9.0) for DAQiFi devices
  - Communication layer: SCPI, Protobuf message parsing/production, Serial & TCP transport
  - Device control: DaqifiDevice, DaqifiStreamingDevice
  - Test project: 16 xUnit test classes (communication, transport, device, integration)

- **[daqifi-desktop](https://github.com/daqifi/daqifi-desktop)** — Windows desktop application (WPF, net8.0)
  - Full device control UI with live graphing
  - 4 test projects with xUnit

#### Other
- **[daqifi-java-api](https://github.com/daqifi/daqifi-java-api)** — Java API
- **[daqifi-node](https://github.com/daqifi/daqifi-node)** — Node.js package and NodeRED plugin
- **[daqifi-labview](https://github.com/daqifi/daqifi-labview)** — LabVIEW interface
- **[daqifi_nyquist_arduino](https://github.com/daqifi/daqifi_nyquist_arduino)** — Arduino interface
- **[daqifi-core-example-app](https://github.com/daqifi/daqifi-core-example-app)** — .NET example app

#### Using Python Test Suite for PR Validation
```bash
# Clone if not already present
git clone https://github.com/daqifi/daqifi-python-core.git
git clone https://github.com/daqifi/daqifi-python-test-suite.git

# Install dependencies
pip install -e ./daqifi-python-core

# Run comprehensive tests
cd daqifi-python-test-suite
python comprehensive_test.py

# Run SD card tests specifically
python test_sd_card.py
```

### Known Issues and Workarounds

1. **USBPcap Filter**: Interferes with device passthrough
   - Use `--force` flag if needed
   - Or program from Windows side directly

2. **Line Ending Issues**: Windows creates CRLF in scripts
   - Fix with: `dos2unix script.sh` or use `sed -i 's/\r$//'`
   - Or create scripts directly in WSL

3. **Permission Prompts**: Even with wildcards, complex commands may prompt
   - Keep commands simple
   - Use script files instead of complex one-liners
   - Batch related commands together

4. **wolfSSL Must Stay at v5.4.0**: MCC Content Manager pushes wolfSSL v5.7.0 which **breaks the build** (confirmed Microchip bug — missing `types.h`, `WOLF_ENUM_DUMMY_LAST_ELEMENT` macro errors, `NO_BIG_INT` conflicts). See [Microchip forum thread](https://forum.microchip.com/s/topic/a5CV4000000249BMAQ/t397847). After any MCC session, revert wolfSSL changes:
   ```bash
   git checkout -- firmware/src/third_party/wolfssl/
   git clean -fd -- firmware/src/third_party/wolfssl/
   git checkout -- firmware/src/config/default/harmony-manifest-success.yml
   git checkout -- firmware/daqifi.X/nbproject/configurations.xml
   # Then regenerate Makefiles and remove references to deleted wolf files
   # (dilithium, kyber, xmss, lms, sphincs, sm2/3/4, hpke)
   ```
- pic32 tris convention is 1=input and 0=output
- when implementing new code/features remember that we have multiple configurations so we need to ensure we are keeping all the structs consistant as well as the handling functions, etc.
- always verify scpi command syntax - don't guess.
- we don't need to generate analysis docs or the like unless asked
- all errors should go through the error logging function and doesn't need to be sent out in the stream at all. if there is an
 error, the SCPI command should return the error through its own handling. then the user calls SYSTem:LOG? to know what the
error was.
- all errors should go through the error logging function and doesn't need to be sent out in the stream at all. if there is an
 error, the SCPI command should return the error through its own handling. then the user calls SYSTem:LOG? to know what the
error was.