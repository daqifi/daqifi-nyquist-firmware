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

3. Clean build (optional):
   ```bash
   make -f nbproject/Makefile-default.mk CONF=default clean
   ```

4. Build the project:
   ```bash
   make -f nbproject/Makefile-default.mk CONF=default build -j4
   ```

5. Output hex file location:
   ```
   dist/default/production/daqifi.X.production.hex
   ```

#### Programming with PICkit 4
**Note**: USB passthrough of PICkit 4 to WSL may not work reliably due to USBPcap filter interference. Use Windows tools instead:

1. From Windows PowerShell or Command Prompt:
   ```cmd
   C:\"Program Files"\Microchip\MPLABX\v6.25\mplab_platform\mplab_ipe\ipecmd.exe -TPPK4 -P32MZ2048EFM144 -M -F"dist\default\production\daqifi.X.production.hex"
   ```

2. For Linux builds, the hex file will be at the WSL path:
   ```
   \\wsl$\Ubuntu\mnt\c\Users\User\Documents\GitHub\daqifi-nyquist-firmware\firmware\daqifi.X\dist\default\production\daqifi.X.production.hex
   ```

### Device Testing and SCPI Communication

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
- 0: POWERED_DOWN
- 1: MICRO_ON (USB powered, minimal functionality)
- 2: POWERED_UP (Full power, all modules active)
- 3: POWERED_UP_EXT_DOWN (Partial power)

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
- Power states: 0=off, 1=on for setting; 1=MICRO_ON, 2=POWERED_UP for reading

### Git Configuration
- Ignore line ending changes when reviewing diffs (Windows/Linux compatibility)

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
   SYST:POW:STAT?  # 1=MICRO_ON, 2=POWERED_UP
   
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