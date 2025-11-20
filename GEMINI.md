# GEMINI.md

This file provides guidance to Gemini when working with code in this repository.

## Project Overview

This is the DAQiFi Nyquist firmware project - a multi-channel data acquisition device built on a PIC32MZ microcontroller with FreeRTOS. The project consists of a USB bootloader and main application firmware.

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

### Memory Considerations

#### Heap Configuration
- **Total FreeRTOS Heap**: 284KB (`configTOTAL_HEAP_SIZE` in `FreeRTOSConfig.h`)
- **Heap Implementation**: heap_4 (best-fit with coalescence)
- **Critical**: Heap must accommodate circular buffers, task stacks, and streaming sample queue

#### Major Heap Allocations

**Static Buffers** (compile-time allocation, not on heap):
- **SD Card Shared Buffer**: 64KB (`gSdSharedBuffer` in `sd_card_manager.c`)
  - Static coherent buffer for all SD operations (write, read, list)
  - DMA-safe, cache-line aligned
  - **Changed from heap to static allocation to reduce heap pressure**
  - Location: `firmware/src/services/sd_card_services/sd_card_manager.c`

**Circular Buffers** (heap-allocated at runtime):
- **WiFi TCP Write Buffer**: 5.6KB (`WIFI_CIRCULAR_BUFF_SIZE` = 1400 × 4)
  - Location: `firmware/src/services/wifi_services/wifi_tcp_server.c`
- **USB CDC Write Buffer**: 16KB (`USBCDC_CIRCULAR_BUFF_SIZE` = 4096 × 4)
  - Location: `firmware/src/services/UsbCdc/UsbCdc.c`
  - Increased from 8KB to improve throughput and reduce buffer-full conditions

**FreeRTOS Task Stacks** (~33KB total):
- `app_SdCardTask`: 5240 bytes
- `app_PowerAndUITask`: 4096 bytes
- `streaming_Task`: 4096 bytes
- `_Streaming_Deferred_Interrupt_Task`: 4096 bytes
- `app_USBDeviceTask`: 3072 bytes (note: malloc fails with 4096)
- `app_WifiTask`: 3000 bytes
- `lWDRV_WINC_Tasks`: 3000 bytes (WiFi driver)
- `MC12bADC_EosInterruptTask`: 2048 bytes
- `lAPP_FREERTOS_Tasks`: 1500 bytes
- `F_USB_DEVICE_Tasks`: 1024 bytes
- `F_DRV_USBHS_Tasks`: 1024 bytes
- `AD7609_DeferredInterruptTask`: 512 bytes

**Streaming Sample Queue**:
- **Queue Depth**: 20 samples (`MAX_AIN_SAMPLE_COUNT`)
- **Sample Size**: ~208 bytes (`AInPublicSampleList_t` = 16 channels × 12 bytes + 16 bools)
- **Max Queue Memory**: ~4KB (20 × 208 bytes)
- **Allocation**: Dynamic (`pvPortCalloc` in streaming interrupt task)
- **Critical**: Old samples must be freed before starting new streaming session
  - Cleared automatically in `Streaming_Start()` since 2025-01-XX

#### Heap Usage Summary
Typical allocations at steady state:
- Circular buffers: **21.6KB** (WiFi: 5.6KB, USB: 16KB)
- Task stacks: **~33KB**
- Streaming queue: **Up to 4KB** (dynamically allocated)
- **Total heap**: ~59KB used, **~225KB free** (under normal conditions)
- **Static buffers**: 64KB (SD card buffer - not counted against heap)

#### Memory Pressure Indicators
If heap allocation fails (`xPortGetFreeHeapSize()` returns low values):
1. **Check streaming sample queue**: `AInSampleList_Size()` - should be <20
2. **Reduce USB/WiFi buffers**: If SD logging is primary use case, can reduce USB/WiFi buffer sizes
3. **Monitor with debugger**: Set breakpoint in `pvPortMalloc` failure path

Note: SD buffer is now static (not heap), significantly improving heap availability.

#### Other Memory Constraints
- DMA buffers must be cache-aligned
- Bootloader reserves memory at 0x9D000000
- Application starts at 0x9D000480 (after bootloader)
- Sample buffers use coherent attribute for DMA safety

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

## Git Commit Message Format

This project uses the **Conventional Commits** format for all commit messages.

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

## Development and Testing Notes

### Building with Linux/WSL

The project can be built using Microchip tools in WSL/Linux.

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

3. Build the project:
   ```bash
   make -f nbproject/Makefile-default.mk CONF=default build -j4
   ```

### Programming with PICkit 4

**From WSL (Verified Working Method)**
```bash
"/mnt/c/Program Files/Microchip/MPLABX/v6.25/mplab_platform/mplab_ipe/ipecmd.exe" \
  -TPPK4 -P32MZ2048EFM144 -M -F"dist/default/production/daqifi.X.production.hex" -OL
```

**From Windows PowerShell or Command Prompt**
```cmd
cd C:\Users\User\Documents\GitHub\daqifi-nyquist-firmware\firmware\daqifi.X
C:\"Program Files"\Microchip\MPLABX\v6.25\mplab_platform\mplab_ipe\ipecmd.exe -TPPK4 -P32MZ2048EFM144 -M -F"dist\default\production\daqifi.X.production.hex" -OL
```

### Device Testing and SCPI Communication

#### Important Testing Notes
1. **Picocom Usage**: Do not use `2>&1` with picocom commands; it doesn't handle stderr redirection correctly.
2. **Power State**: Always power up the device (`SYST:POW:STAT 1`) before attempting WiFi or DAC operations.
3. **Use Exact SCPI Commands**: Verify commands in `SCPIInterface.c`.

#### USB Device Access from WSL
1. **Initial Device Attachment** (from Windows PowerShell as admin):
   ```powershell
   usbipd list
   # Find DAQiFi device (e.g., "USB Serial Device (COM3)")
   usbipd attach --wsl --busid <BUSID>
   ```

2. **Verify device in WSL**:
   ```bash
   lsusb | grep -i "04d8"
   ls -la /dev/ttyACM*
   ```

#### Sending SCPI Commands
Use `picocom` for reliable serial communication:
```bash
# Query device identification
(echo -e "*IDN?\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 | tail -20
```

#### Power states:
- 0: STANDBY
- 1: POWERED_UP
- 2: POWERED_UP_EXT_DOWN (Low battery mode)
