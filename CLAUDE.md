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

### Bootloader Entry
- Hold the user button for ~20 seconds until board resets
- Release button when LEDs light solid
- Hold button again until white LED blinks to enter bootloader mode

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

### Board Variants

- **NQ1**: Basic variant configuration
- **NQ2**: Enhanced variant with additional features
- **NQ3**: Full-featured variant

Each variant has specific:
- Channel counts and types
- ADC/DAC configurations
- Power management settings
- UI configurations

### Memory Considerations

- FreeRTOS heap configuration critical for stability
- DMA buffers must be cache-aligned
- Bootloader reserves memory at 0x9D000000
- Application starts at 0x9D000480 (after bootloader)

### Key Files for Understanding the System

1. **Application Entry**: `firmware/src/app_freertos.c` - Main application tasks
2. **Board Config**: `firmware/src/state/board/BoardConfig.c` - Hardware definitions
3. **Streaming Engine**: `firmware/src/services/streaming.c` - Data flow control
4. **SCPI Interface**: `firmware/src/services/SCPI/SCPIInterface.c` - Command processing
5. **HAL Drivers**: `firmware/src/HAL/ADC.c`, `DIO.c` - Hardware interfaces

### Development Considerations

- All hardware access must go through HAL layer
- SCPI commands follow IEEE 488.2 standard
- Use FreeRTOS primitives for synchronization
- Respect board variant differences in runtime checks
- Protocol buffer definitions in `services/DaqifiPB/DaqifiOutMessage.proto`

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

## Command Line Build Tools

Build scripts are available in the `scripts/` directory for automated compilation:

### Quick Build Test
```bash
# Test if code changes compile correctly
./scripts/build.sh test        # Linux/WSL
scripts\build.bat test         # Windows
```

### Build Commands
- `build` - Build the default configuration
- `clean` - Clean build artifacts  
- `test` - Build and report errors/warnings

### Prerequisites
- MPLAB X IDE (v6.00+)
- XC32 Compiler
- Make utility (Linux/WSL) or build scripts

See `scripts/README_BUILD_TOOLS.md` for detailed documentation on command line building, MPLAB X CLI tools, and automated testing capabilities.

## Development Workflow

### Proper Development Process
1. **Make code changes** - Implement the feature or fix
2. **Test build** - Run `./scripts/build.sh test` to verify compilation
3. **Review warnings/errors** - Address any build issues before proceeding
4. **Commit only after successful build** - Use conventional commit format
5. **Test on hardware** - Verify functionality with actual device
6. **Create PR after testing** - Include test results in PR description

### Line Ending Considerations
- Project uses Windows line endings (CRLF) enforced by `.gitattributes`
- WSL users should install `dos2unix` for script compatibility: `sudo apt-get install dos2unix`
- Shell scripts may need conversion: `dos2unix scripts/*.sh`
- Git will handle line endings automatically based on `.gitattributes`

### MPLAB X Command Line Interface
The project can be built without opening MPLAB X IDE:

```bash
# From firmware directory
cd firmware/daqifi.X
make -f nbproject/Makefile-default.mk SUBPROJECTS= .build-conf

# Check memory usage after build
cat dist/default/debug/memoryfile.xml
```

### Build Verification
Always verify builds before committing:
```bash
# Quick compile check - captures errors and warnings
./scripts/build.sh test

# Output includes:
# - Compilation status (success/fail)
# - Warning count
# - Memory usage statistics
# - Error summary if failed
```

### Common Build Issues
1. **Make not found in WSL**: Install with `sudo apt-get install build-essential`
2. **Permission denied**: Run `chmod +x scripts/*.sh`
3. **Line ending errors**: Convert with `dos2unix scripts/*.sh`
4. **Makefile not found**: Ensure you're in the correct directory or run from project root

### Continuous Integration Ready
The build scripts enable automated testing:
- Can be integrated into Git hooks for pre-commit validation
- Suitable for CI/CD pipelines (GitHub Actions, Jenkins, etc.)
- Exit codes indicate success (0) or failure (non-zero)