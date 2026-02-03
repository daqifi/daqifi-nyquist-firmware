# Codebase Structure

**Analysis Date:** 2026-01-10

## Directory Layout

```
daqifi-nyquist-firmware/
├── firmware/                    # Main application firmware
│   ├── daqifi.X/               # MPLAB X project
│   │   ├── nbproject/          # Build configuration
│   │   └── dist/               # Build output
│   ├── src/                    # Source code
│   │   ├── config/default/     # Harmony-generated code
│   │   ├── HAL/                # Hardware abstraction layer
│   │   ├── services/           # High-level services
│   │   ├── state/              # State management
│   │   ├── libraries/          # Third-party libraries
│   │   ├── third_party/        # External code
│   │   └── Util/               # Utility functions
│   └── datasheets/             # Hardware documentation
├── bootloader/                  # USB bootloader
│   └── firmware/usb_bootloader.X/
└── .planning/                   # Project planning (GSD)
```

## Directory Purposes

**firmware/daqifi.X/**
- Purpose: MPLAB X project container
- Contains: Makefile, project configuration, linker scripts
- Key files: `nbproject/Makefile-default.mk`, `nbproject/configurations.xml`
- Subdirectories: `nbproject/` (config), `dist/` (build output)

**firmware/src/**
- Purpose: All source code
- Contains: Application code, HAL, services, state management
- Key files: `main.c`, `app_freertos.c`, `version.h`

**firmware/src/config/default/**
- Purpose: Harmony-generated configuration and drivers
- Contains: Peripheral drivers, system services, FreeRTOS config
- Key files: `FreeRTOSConfig.h`, `configuration.h`, `definitions.h`
- Note: Auto-generated - avoid manual edits

**firmware/src/HAL/**
- Purpose: Hardware abstraction for peripherals
- Contains: Device drivers for ADC, DAC, DIO, Power, UI
- Key files: `ADC.c`, `DIO.c`, `DAC7718/DAC7718.c`, `Power/PowerApi.c`
- Subdirectories: `ADC/`, `DAC7718/`, `BQ24297/`, `Power/`, `UI/`, `NVM/`, `TimerApi/`, `OcmpApi/`

**firmware/src/services/**
- Purpose: High-level application services
- Contains: SCPI commands, streaming, encoders, communication
- Key files: `streaming.c`, `daqifi_settings.c`
- Subdirectories: `SCPI/`, `UsbCdc/`, `wifi_services/`, `sd_card_services/`, `DaqifiPB/`

**firmware/src/state/**
- Purpose: State management (three-layer architecture)
- Contains: Board config, runtime config, sample data
- Subdirectories:
  - `board/` - Hardware definitions (NQ1/NQ2/NQ3 configs)
  - `runtime/` - Mutable settings and defaults
  - `data/` - Real-time sample data structures

**firmware/src/libraries/**
- Purpose: Third-party embedded libraries
- Contains: SCPI parser, nanopb, microrl
- Key files: `scpi/libscpi/`, `nanopb/pb.h`, `microrl/`

**firmware/src/third_party/**
- Purpose: External code with separate licensing
- Contains: FreeRTOS kernel, WolfSSL, zlib
- Subdirectories: `rtos/`, `wolfssl/`, `zlib/`

**firmware/src/Util/**
- Purpose: Shared utility functions and data structures
- Contains: Buffers, lists, logger, string formatters
- Key files: `CircularBuffer.c`, `Logger.c`, `LinkedList.c`

**bootloader/firmware/usb_bootloader.X/**
- Purpose: USB HID bootloader project
- Contains: Separate MPLAB X project for bootloader
- Key files: `src/main.c`, `src/app.c`

## Key File Locations

**Entry Points:**
- `firmware/src/main.c` - Firmware entry point
- `firmware/src/app_freertos.c` - FreeRTOS task definitions
- `bootloader/firmware/usb_bootloader.X/src/main.c` - Bootloader entry

**Configuration:**
- `firmware/src/config/default/FreeRTOSConfig.h` - RTOS settings
- `firmware/src/config/default/configuration.h` - Build macros
- `firmware/daqifi.X/nbproject/configurations.xml` - Project config
- `firmware/src/version.h` - Firmware version (3.4.2)

**Core Logic:**
- `firmware/src/services/streaming.c` - Data streaming engine
- `firmware/src/services/SCPI/SCPIInterface.c` - Command dispatcher
- `firmware/src/HAL/ADC.c` - ADC interface multiplexer
- `firmware/src/HAL/DIO.c` - Digital I/O handling

**State Management:**
- `firmware/src/state/board/BoardConfig.h` - Hardware schema
- `firmware/src/state/runtime/BoardRuntimeConfig.h` - Settings schema
- `firmware/src/state/data/BoardData.h` - Sample data schema

**Testing:**
- `firmware/src/libraries/scpi/libscpi/test/` - SCPI library tests
- Hardware testing via SCPI commands (no unit test framework for main code)

**Documentation:**
- `CLAUDE.md` - Development instructions for Claude
- `README.md` - Build instructions
- `FIRMWARE_ANALYSIS_REPORT.md` - Performance analysis (if present)

## Naming Conventions

**Files:**
- PascalCase for HAL modules: `AD7173.c`, `DAC7718.c`, `PowerApi.c`
- snake_case for services: `wifi_manager.c`, `sd_card_manager.c`
- PascalCase for utilities: `CircularBuffer.c`, `Logger.c`

**Directories:**
- lowercase for top-level: `firmware/`, `bootloader/`
- PascalCase for HAL subdirs: `ADC/`, `DAC7718/`, `Power/`
- snake_case for service subdirs: `wifi_services/`, `sd_card_services/`

**Special Patterns:**
- `NQ1*.c`, `NQ2*.c`, `NQ3*.c` - Board variant-specific files
- `Common*.c` - Shared code across variants
- `*Config.h` - Configuration structure definitions
- `*RuntimeConfig.h` - Mutable settings structures

## Where to Add New Code

**New Feature:**
- Primary code: `firmware/src/services/` or `firmware/src/HAL/`
- State changes: Add to relevant `state/` subdirectory
- Config if needed: Update `BoardConfig.h` or `BoardRuntimeConfig.h`

**New SCPI Command:**
- Definition: `firmware/src/services/SCPI/SCPIInterface.c` (pattern table)
- Handler: Existing SCPI module or new `SCPI*.c` file
- Update variant configs if hardware-specific

**New HAL Driver:**
- Implementation: `firmware/src/HAL/{DeviceName}/`
- Headers: Include public interface in `firmware/src/HAL/{DeviceName}.h`
- Integration: Add initialization call in `app_freertos.c`

**New Board Variant:**
- Config: `firmware/src/state/board/NQxBoardConfig.c`
- Defaults: `firmware/src/state/runtime/NQxRuntimeDefaults.c`
- Build: New Makefile configuration in `nbproject/`

**Utilities:**
- Shared helpers: `firmware/src/Util/`
- Type definitions: Relevant header in `state/` hierarchy

## Special Directories

**firmware/src/config/default/**
- Purpose: Harmony-generated code
- Source: MPLAB Harmony Configurator (MHC)
- Committed: Yes
- Warning: Avoid manual edits; regenerate via MHC

**firmware/daqifi.X/dist/**
- Purpose: Build output
- Source: Make build process
- Committed: No (in .gitignore)
- Contents: `.hex`, `.elf`, `.map` files

**firmware/daqifi.X/build/**
- Purpose: Intermediate build objects
- Source: Compiler output
- Committed: No (in .gitignore)

---

*Structure analysis: 2026-01-10*
*Update when directory structure changes*
