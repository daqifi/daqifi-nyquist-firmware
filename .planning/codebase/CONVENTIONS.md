# Coding Conventions

**Analysis Date:** 2026-01-10

## Naming Patterns

**Files:**
- PascalCase for HAL modules: `AD7173.c`, `PowerApi.c`, `CircularBuffer.c`
- snake_case for services: `wifi_manager.c`, `sd_card_manager.c`, `wifi_tcp_server.c`
- Variant prefixes: `NQ1BoardConfig.c`, `NQ2RuntimeDefaults.c`

**Functions:**
- Module-prefixed: `ModuleName_FunctionName()`
  - Examples: `AD7173_InitHardware()`, `DIO_ReadSampleByMask()`, `wifi_manager_Init()`
- SCPI handlers: `SCPI_CommandAction()` - e.g., `SCPI_ADCVoltageGet()`, `SCPI_ADCChanEnableSet()`
- Callbacks: `*CB()` suffix - e.g., `sd_card_manager_DataReadyCB()`
- Encoders: `format_Operation()` - e.g., `json_ResetEncoder()`, `csv_Encode()`

**Variables:**
- camelCase for locals: `channelConfig`, `deviceId`, `totalBytes`
- Pointer prefix `p`: `pModuleConfig`, `pStreamConfig`, `pBoardData`
- Member prefix `m_`: `m_ListPtr`, `m_Data` (in utility structures)
- UPPER_SNAKE_CASE for constants: `BUFFER_SIZE`, `MAX_CHANNELS`

**Types:**
- Struct pattern: `typedef struct sName { ... } tName;`
  - Examples: `typedef struct sBoardConfig { ... } tBoardConfig;`
  - Struct tag prefix: `s_` or `s` (e.g., `s_LinkedListNode`, `sBoardRuntimeConfig`)
  - Typedef prefix: `t` (e.g., `tBoardConfig`, `tBoardData`)
- Enum pattern: `typedef enum eName { ... } Name;`
  - Enum tag prefix: `e_` or `e` (e.g., `e_AInType`, `eILimit`)
- Array typedefs: `*Array` suffix (e.g., `DIORuntimeArray`, `AInModArray`)

## Code Style

**Formatting:**
- 4-space indentation (standard throughout)
- K&R brace style (opening brace on same line)
- Line length: Generally 80-100 characters
- Line continuation: Backslash with aligned spacing

**Linting:**
- No formal linter configured
- XC32 compiler warnings enabled
- Style enforced by convention

## Import Organization

**Order:**
1. Local project includes (quoted): `#include "SCPIInterface.h"`
2. System includes (angle brackets): `#include <stdlib.h>`
3. Framework/HAL includes: `#include "HAL/Power/PowerApi.h"`
4. Service includes: `#include "services/DaqifiPB/DaqifiOutMessage.pb.h"`

**Grouping:**
- No blank lines required between groups
- Related includes grouped together

**Path Style:**
- Forward slashes in include paths: `"HAL/ADC/AD7173.h"`
- Relative to `firmware/src/`

## Header File Organization

**Guards:**
- `#pragma once` preferred (modern style)
  - Examples: `firmware/src/HAL/DIO.h`, `firmware/src/Util/LinkedList.h`
- Alternative: `#ifndef _FILENAME_H` / `#define _FILENAME_H` / `#endif`

**C++ Compatibility:**
```c
#ifdef __cplusplus
extern "C" {
#endif

// declarations here

#ifdef __cplusplus
}
#endif
```

**Section Comments:**
- Microchip template style for generated code:
```c
/* ************************************************************************** */
/* Section: Constants                                                         */
/* ************************************************************************** */
```

## Error Handling

**Patterns:**
- Return bool for success/failure: `bool AD7173_InitHardware(...)`
- Return -1/0/positive for size functions
- Log errors via Logger module, not streaming output
- SCPI errors: Return through command response mechanism

**Error Types:**
- Silent failures: HAL operations that can't recover
- Logged errors: Retrievable via `SYST:LOG?` SCPI command
- Immediate response: SCPI command validation errors

## Logging

**Framework:**
- Custom Logger module: `firmware/src/Util/Logger.c`
- Macros: `LOG_E()`, `LOG_W()`, `LOG_I()`, `LOG_D()`
- Level control: `#define LOG_LVL LOG_LEVEL_*` at file top

**Patterns:**
- Format: `LOG_E("[%s:%d] Message", __FILE__, __LINE__)`
- Context: Include function name or module prefix
- Errors logged, not sent in stream (per CLAUDE.md guidelines)

## Comments

**When to Comment:**
- Explain why, not what
- Document workarounds and known issues
- Note TODO items for future work
- Describe complex algorithms

**Doxygen Style:**
```c
/*! @file ModuleName.h
 * @brief Interface of the Module
 */

/*!
 * @brief Function description
 * @param paramName Description
 * @return Description of return value
 */
```

**Inline Comments:**
- Double-slash for inline: `// comment`
- Member documentation: `//!` prefix for Doxygen

**TODO Pattern:**
- Format: `// TODO: description` or `// TODO(Owner): description`
- Reference issues if available: `// TODO: Fix race condition (issue #123)`

## Function Design

**Size:**
- Keep under 50 lines where practical
- Extract helpers for complex logic

**Parameters:**
- Pointer parameters: `p` prefix indicates pointer (e.g., `pConfig`)
- Output parameters: Document with `[out]` in Doxygen
- Max 4-5 parameters; use struct for more

**Return Values:**
- `bool` for success/failure
- `size_t` for byte counts
- Pointers for data access (NULL on error)
- Explicit return statements

## Module Design

**Exports:**
- Public API in header file
- Static functions for internal helpers
- Module-prefixed names prevent conflicts

**Initialization Pattern:**
```c
void Module_Init(config_t* pConfig);
bool Module_Operation(params);
void Module_Deinit(void);  // if needed
```

**State Access Pattern:**
```c
// Getter with parameter enum
tBoardConfig* BoardConfig_Get(enum eBoardParameter param, size_t index);

// Runtime config modification
void BoardRunTimeConfig_Set(enum eRuntimeParameter param, void* value);
```

## Macro Conventions

**Configuration Macros:**
- ALLCAPS_WITH_UNDERSCORES
- Examples: `USBDEVICETASK_PRIO`, `BUFFER_SIZE`, `MAX_CHANNELS`

**Utility Macros:**
- `UNUSED(x)` - Suppress unused parameter warnings
- `min(x,y)`, `max(x,y)` - Standard comparisons

**Conditional Compilation:**
- Board variant: Check `BOARD_VARIANT` or similar
- Debug: `#ifdef DEBUG` or `#if LOG_LVL >= LOG_LEVEL_DEBUG`

---

*Convention analysis: 2026-01-10*
*Update when patterns change*
