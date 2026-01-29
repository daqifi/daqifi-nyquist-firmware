# Architecture

**Analysis Date:** 2026-01-10

## Pattern Overview

**Overall:** Layered + HAL + Task-Driven (FreeRTOS)

**Key Characteristics:**
- Multi-tier embedded architecture with clean separation of concerns
- FreeRTOS task-based concurrency model
- Hardware abstraction through dedicated HAL layer
- Three-layer state architecture (config, runtime, data)
- Deferred interrupt pattern for real-time acquisition

## Layers

**Entry Point Layer:**
- Purpose: Minimal bootstrap calling Harmony initialization
- Contains: `main()` function, `SYS_Initialize()`, `SYS_Tasks()` loop
- Location: `firmware/src/main.c`
- Depends on: Harmony framework
- Used by: System startup

**System/Harmony Layer:**
- Purpose: Hardware initialization and peripheral management
- Contains: Auto-generated Harmony code, peripheral drivers
- Location: `firmware/src/config/default/`
- Depends on: Hardware registers
- Used by: HAL and Application layers

**Application Task Layer:**
- Purpose: FreeRTOS task orchestration and high-level logic
- Contains: Task creation, initialization, main control loops
- Location: `firmware/src/app_freertos.c`
- Depends on: Service layer, FreeRTOS
- Used by: Entry point (scheduler start)

**Service Layer:**
- Purpose: High-level business logic and communication protocols
- Contains: SCPI commands, streaming engine, encoders, communication managers
- Location: `firmware/src/services/`
- Depends on: HAL, State Management, Utilities
- Used by: Application tasks

**Hardware Abstraction Layer (HAL):**
- Purpose: Device-specific driver abstractions
- Contains: ADC, DAC, DIO, Power, UI, Timer drivers
- Location: `firmware/src/HAL/`
- Depends on: Harmony PLIB, registers
- Used by: Service layer

**State Management Layer:**
- Purpose: Configuration and runtime data structures
- Contains: Board config, runtime config, sample data
- Location: `firmware/src/state/`
- Depends on: None (pure data)
- Used by: All layers

**Utility Layer:**
- Purpose: Shared helpers and data structures
- Contains: Circular buffers, linked lists, logger, formatters
- Location: `firmware/src/Util/`
- Depends on: FreeRTOS (for synchronization)
- Used by: All layers

## Data Flow

**Acquisition Path (Real-Time):**
1. Timer interrupt triggers ADC conversion
2. ADC EOC interrupt → HAL callback
3. ISR notifies deferred interrupt task
4. Deferred task allocates from object pool, copies samples
5. Samples pushed to queue
6. Streaming task encodes (CSV/JSON/ProtoBuf)
7. Encoded data written to active interface (USB/WiFi/SD)

**Command Processing Path:**
1. USB/WiFi receives character stream
2. SCPI parser accumulates command
3. Pattern matching in SCPIInterface.c
4. Callback handler invoked
5. Handler reads/writes BoardRuntimeConfig
6. HAL functions apply changes
7. Response returned to interface

**State Management:**
- BoardConfig: Hardware definition (immutable after init)
- BoardRuntimeConfig: User settings (mutable via SCPI)
- BoardData: Real-time samples (updated by ISR/tasks)

## Key Abstractions

**Board Configuration (tBoardConfig):**
- Purpose: Define hardware capabilities per variant
- Location: `firmware/src/state/board/BoardConfig.h`
- Variants: NQ1, NQ2, NQ3 (compile-time selection)
- Pattern: Variant-specific files + common shared code

**Board Runtime Configuration (tBoardRuntimeConfig):**
- Purpose: User-modifiable settings at runtime
- Location: `firmware/src/state/runtime/BoardRuntimeConfig.h`
- Pattern: Loaded from defaults, modified via SCPI, persisted to NVM

**Board Data (tBoardData):**
- Purpose: Real-time acquisition data
- Location: `firmware/src/state/data/BoardData.h`
- Pattern: Updated by ISRs, read by streaming engine

**Sample Object Pool:**
- Purpose: Pre-allocated sample buffers (no heap fragmentation)
- Location: `firmware/src/state/data/AInSample.c`
- Pattern: O(1) allocation/deallocation from static pool

**Streaming Engine:**
- Purpose: Data flow control and encoding
- Location: `firmware/src/services/streaming.c`
- Pattern: Two-task model (deferred ISR + streaming)

## Entry Points

**Firmware Startup:**
- Location: `firmware/src/main.c`
- Triggers: Power-on/reset
- Responsibilities: Call SYS_Initialize(), enter SYS_Tasks() loop

**FreeRTOS Tasks:**
- app_USBDeviceTask: `firmware/src/app_freertos.c` - USB CDC handling
- app_WifiTask: WiFi manager and TCP server
- app_SDCardTask: SD card file operations
- _Streaming_Deferred_Interrupt_Task: Sample collection
- streaming_Task: Data encoding and output

**SCPI Command Entry:**
- Location: `firmware/src/services/SCPI/SCPIInterface.c`
- Triggers: Command received via USB/WiFi
- Responsibilities: Parse, dispatch, execute, respond

**Bootloader:**
- Location: `bootloader/firmware/usb_bootloader.X/src/main.c`
- Triggers: Button hold at power-on
- Responsibilities: USB firmware update

## Error Handling

**Strategy:** Return codes + centralized error logging

**Patterns:**
- HAL functions return bool (true=success)
- Errors logged via Logger module: `firmware/src/Util/Logger.c`
- SCPI errors returned via command response
- Error queue: `SYST:LOG?` command to retrieve

## Cross-Cutting Concerns

**Logging:**
- Module: `firmware/src/Util/Logger.c`
- Levels: Error, Warning, Info, Debug
- Pattern: Circular buffer with drop-on-overflow

**Synchronization:**
- FreeRTOS semaphores and mutexes
- Lock providers: `firmware/src/Util/FreeRTOSLockProvider.c`
- Critical sections for ISR safety

**Memory Management:**
- Static allocation preferred
- Object pools for streaming samples
- Heap: 284KB total, ~225KB typically free

---

*Architecture analysis: 2026-01-10*
*Update when major patterns change*
