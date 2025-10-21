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

**Circular Buffers** (allocated at runtime via `CircularBuf_Init`):
- **SD Card Write Buffer**: 64KB (`SD_CARD_MANAGER_CIRCULAR_BUFFER_SIZE` in `sd_card_manager.c`)
  - ⚠️ **LARGEST ALLOCATION** - Reduced from 128KB to address heap pressure
  - Location: `firmware/src/services/sd_card_services/sd_card_manager.c`
  - **TODO**: Should be dynamically sized based on enabled features (see GitHub issue)
- **WiFi TCP Write Buffer**: 5.6KB (`WIFI_CIRCULAR_BUFF_SIZE` = 1400 × 4)
  - Location: `firmware/src/services/wifi_services/wifi_tcp_server.c`
- **USB CDC Write Buffer**: 2.8KB (`USBCDC_CIRCULAR_BUFF_SIZE` = 700 × 4)
  - Location: `firmware/src/services/UsbCdc/UsbCdc.c`

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
- Circular buffers: **72.4KB** (SD: 64KB, WiFi: 5.6KB, USB: 2.8KB)
- Task stacks: **~33KB**
- Streaming queue: **Up to 4KB** (dynamically allocated)
- **Total**: ~109KB used, ~175KB free (under normal conditions)

#### Memory Pressure Indicators
If heap allocation fails (`xPortGetFreeHeapSize()` returns low values):
1. **Check streaming sample queue**: `AInSampleList_Size()` - should be <20
2. **Further reduce SD buffer**: 64KB → 32KB saves additional 32KB if SD logging unused
3. **Monitor with debugger**: Set breakpoint in `pvPortMalloc` failure path

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
1. **Picocom Usage**: Never use `2>&1` with picocom commands - it doesn't handle stderr redirection properly
   ```bash
   # Good: 
   (echo -e "*IDN?\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 | tail -5
   # Bad:
   (echo -e "*IDN?\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -5
   ```

2. **Power State Required**: Always power up the device before attempting WiFi or DAC operations
   ```bash
   # Check power state (0=STANDBY, 1=POWERED_UP, 2=POWERED_UP_EXT_DOWN)
   (echo -e "SYST:POW:STAT?\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 | tail -5
   
   # Power up device for full functionality (enables 10V rail needed for DAC7718)
   (echo -e "SYST:POW:STAT 1\r"; sleep 1) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 | tail-5
   ```

3. **Use Exact SCPI Commands**: Don't guess command syntax. Check the actual command table in `SCPIInterface.c`
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
- Default SSID: "DAQiFi"
- Authentication: Open (no password)
- The device must be in POWERED_UP state for WiFi to operate

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
- pic32 tris convention is 1=input and 0=output
- when implementing new code/features remember that we have multiple configurations so we need to ensure we are keeping all the structs consistant as well as the handling functions, etc.
- always verify scpi command syntax - don't guess.