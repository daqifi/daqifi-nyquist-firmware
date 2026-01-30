# Testing Patterns

**Analysis Date:** 2026-01-10

## Test Framework

**Runner:**
- CUnit - For SCPI library unit tests
  - Location: `firmware/src/libraries/scpi/libscpi/test/`
  - Test files: `test_fifo.c`, `test_lexer_parser.c`, `test_parser.c`, `test_scpi_utils.c`

**Assertion Library:**
- CUnit built-in assertions
- Hardware testing: Manual verification via SCPI responses

**Primary Testing Method:**
- Hardware testing on live device via SCPI commands
- No comprehensive unit test framework for main firmware code

## Test File Organization

**Location:**
- SCPI library tests: `firmware/src/libraries/scpi/libscpi/test/`
- No unit tests for main firmware code (embedded hardware testing)

**Naming:**
- `test_*.c` for CUnit test files
- No co-located tests with source

**Structure:**
```
firmware/src/libraries/scpi/libscpi/test/
├── test_fifo.c
├── test_lexer_parser.c
├── test_parser.c
└── test_scpi_utils.c
```

## Test Structure

**CUnit Suite Organization:**
```c
static int init_suite(void) {
    return 0;
}

static int clean_suite(void) {
    return 0;
}

void test_feature(void) {
    // CUnit assertions
    CU_ASSERT_EQUAL(actual, expected);
}
```

**Patterns:**
- Suite init/cleanup functions
- Individual test functions
- CU_ASSERT macros for validation

## Hardware Testing

**Approach:**
- SCPI command verification on live device
- USB CDC serial communication at 115200 baud
- WiFi TCP server at port 9760

**Test Commands (via picocom/terminal):**
```bash
# Device identification
(echo -e "*IDN?\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0

# Power state verification
(echo -e "SYST:POW:STAT?\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0

# Error queue check
(echo -e "SYST:ERR?\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0
```

**Device States:**
- 0 = STANDBY
- 1 = POWERED_UP (full functionality)
- 2 = POWERED_UP_EXT_DOWN (low battery mode)

## Test Types

**Unit Tests (SCPI Library Only):**
- Scope: SCPI parser and utility functions
- Location: `firmware/src/libraries/scpi/libscpi/test/`
- Framework: CUnit
- Coverage: ~93% of SCPI library code

**Hardware Integration Tests:**
- Scope: Full system on physical hardware
- Method: SCPI command sequences
- Tools: picocom, Python scripts

**Manual Verification:**
- Power management transitions
- WiFi AP mode and connectivity
- SD card file operations
- ADC/DAC channel functionality

## Hardware Test Execution

**Setup:**
1. Connect device via USB
2. Attach to WSL if needed: `usbipd attach --wsl --busid <ID>`
3. Verify device: `ls -la /dev/ttyACM*`

**Test Automation Script Pattern:**
```bash
#!/bin/bash
# Always use: /tmp/temp.sh for consistent permissions

send_cmd() {
    local cmd="$1"
    local delay="${2:-0.5}"
    (echo -e "${cmd}\r"; sleep $delay) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -20
}

# Setup
send_cmd "SYST:POW:STAT 1" 1  # Power up
send_cmd "SYST:ERR?" 0.5      # Clear errors

# Test commands
send_cmd "*IDN?"
send_cmd "SYST:COMM:LAN:SSID?"

# Cleanup
send_cmd "SYST:POW:STAT 0" 1  # Power down
```

## Coverage

**Requirements:**
- No enforced coverage target for main firmware
- SCPI library: 93%+ coverage (from library documentation)

**Gaps:**
- Main firmware code: No unit test coverage
- HAL drivers: Hardware verification only
- Service layer: Integration testing only

## Test Tools & Utilities

**Python Tools:**
- `download_sd_files.py` - SD card file retrieval
- `analyze_split_files.py` - File integrity validation

**Serial Communication:**
- picocom - Linux serial terminal
- 115200 baud, 8N1 configuration

**USB Device Management:**
- usbipd - WSL USB passthrough
- PICkit 4 - Programming and debugging

## Testing Best Practices

**State Management:**
1. Ensure known device state before tests
2. Power cycle if needed: `SYST:POW:STAT 0` → `SYST:POW:STAT 1`
3. Clear error queue: `SYST:ERR?` until "No error"
4. Abort ongoing operations: `ABOR` command

**SCPI Command Verification:**
- Always verify command syntax in `SCPIInterface.c` before testing
- Use exact commands from pattern table
- Test valid cases, then edge cases

**Permissions:**
- Configure `.claude/settings.local.json` for automation:
  ```json
  {
    "permissions": {
      "allow": ["Bash(*)"]
    }
  }
  ```

## Known Limitations

**Hardware Dependencies:**
- Tests require physical hardware (PIC32MZ board)
- Cannot run unit tests in isolation without hardware stubs
- Board variant testing requires separate builds (NQ1, NQ3)

**Third-Party Tests:**
- SCPI library tests not integrated into main build
- Requires CUnit framework setup to execute

**No Mocking Framework:**
- Hardware abstraction requires real peripherals
- Service layer tests need full hardware context

---

*Testing analysis: 2026-01-10*
*Update when test patterns change*
