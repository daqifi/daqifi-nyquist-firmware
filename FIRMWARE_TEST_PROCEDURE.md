# DAQiFi Nyquist Firmware Test Procedure

## Overview
This document provides a comprehensive test procedure to validate the current uncommitted firmware changes before committing them to the repository.

## Test Environment Setup

### Required Equipment
- DAQiFi Nyquist device (NQ1, NQ2, or NQ3 variant)
- USB cable for device connection
- SD card (formatted as FAT32)
- WiFi access point (for WiFi testing)
- Test signal generator (optional, for ADC testing)
- Serial terminal software (e.g., PuTTY, Tera Term)

### Software Requirements
- MPLAB X IDE with XC32 compiler
- DAQiFi Windows application or SCPI terminal
- Python with pySerial (for automated testing)

## Pre-Test Checklist

1. **Build Verification**
   - [ ] Firmware builds without errors
   - [ ] No new compiler warnings introduced
   - [ ] Bootloader compatibility maintained

2. **Code Review**
   - [ ] Changes match intended modifications
   - [ ] No unintended file modifications
   - [ ] Code follows existing style conventions

## Test Procedures

### 1. Basic Functionality Test

#### 1.1 Power-On Test
1. Connect device via USB
2. Verify LED indicators:
   - [ ] Power LED illuminates
   - [ ] Status LED shows correct pattern
3. Check USB enumeration:
   - [ ] Device appears as COM port
   - [ ] VID/PID correct (check Device Manager)

#### 1.2 SCPI Communication Test
```
# Test basic SCPI commands
*IDN?                    # Should return device identification
*RST                     # Reset device
SYST:ERR?               # Check for errors (should return 0,"No error")
```

### 2. Configuration Changes Test

#### 2.1 Board Variant Test
```
# Verify board configuration
SYST:BVAR?              # Check board variant (NQ1/NQ2/NQ3)
SYST:BCFG?              # Verify board configuration matches variant
```

#### 2.2 Settings Persistence Test
1. Change a setting:
   ```
   LAN:SSID "TestNetwork"
   LAN:PASS "TestPassword"
   ```
2. Power cycle the device
3. Verify settings retained:
   ```
   LAN:SSID?               # Should return "TestNetwork"
   ```

### 3. Data Acquisition Test

#### 3.1 ADC Channel Test
```
# Enable ADC channels
AIN:CH1:ENAB 1
AIN:CH2:ENAB 1

# Configure sampling
SYST:SRAT 1000          # 1kHz sample rate

# Start streaming
SYST:STRM:ENAB 1

# Verify data reception (check for data on USB/WiFi/SD)
```

#### 3.2 Digital I/O Test
```
# Configure DIO
DIO:CH1:DIR OUT
DIO:CH1:VAL 1           # Set high
DIO:CH1:VAL?            # Verify readback

DIO:CH2:DIR IN
DIO:CH2:VAL?            # Read input state
```

### 4. Communication Interface Tests

#### 4.1 USB CDC Test
1. Enable USB streaming:
   ```
   SYST:STRM:DEST USB
   SYST:STRM:ENAB 1
   ```
2. Verify data flow:
   - [ ] Data received on COM port
   - [ ] No data corruption
   - [ ] Correct format (JSON/CSV/ProtoBuf)

#### 4.2 WiFi Test
1. Configure WiFi:
   ```
   LAN:ENAB 0              # Disable first
   LAN:SSID "YourSSID"
   LAN:PASS "YourPassword"
   LAN:ENAB 1              # Enable WiFi
   ```
2. Wait for connection (check LED)
3. Test TCP connection:
   - [ ] Connect to port 9760
   - [ ] Send SCPI commands
   - [ ] Receive responses

#### 4.3 SD Card Test
1. Insert formatted SD card
2. Enable SD logging:
   ```
   STOR:SD:ENAB 1
   STOR:SD:LOG "test.csv"
   ```
3. Stream data for 30 seconds
4. Verify file created and contains data

### 5. Streaming Performance Test

#### 5.1 Buffer Test
1. Enable all channels
2. Set maximum sample rate
3. Monitor for:
   - [ ] No buffer overruns
   - [ ] No data drops
   - [ ] Stable streaming

#### 5.2 Multi-Channel Test
1. Enable multiple communication channels:
   ```
   SYST:STRM:DEST USB,WIFI,SD
   ```
2. Verify data on all channels simultaneously

### 6. Error Handling Test

#### 6.1 Invalid Command Test
```
INVALID:COMMAND         # Should generate SCPI error
SYST:ERR?              # Check error queue
```

#### 6.2 Resource Conflict Test
1. Enable WiFi
2. Try to enable SD card:
   ```
   STOR:SD:ENAB 1         # Should fail (shared SPI)
   ```
3. Verify appropriate error message

### 7. Stress Testing

#### 7.1 Long Duration Test
1. Configure for continuous streaming
2. Run for minimum 1 hour
3. Monitor for:
   - [ ] Memory leaks (heap usage stable)
   - [ ] No system crashes
   - [ ] Consistent performance

#### 7.2 Rapid Command Test
Send 1000 SCPI queries rapidly:
```python
# Python test script
import serial
import time

ser = serial.Serial('COM3', 115200)
for i in range(1000):
    ser.write(b'*IDN?\n')
    response = ser.readline()
    if not response:
        print(f"Failed at iteration {i}")
        break
```

### 8. Regression Testing

#### 8.1 Previous Features Test
Verify all previously working features still function:
- [ ] ADC calibration commands
- [ ] Power management
- [ ] LED indicators
- [ ] Button interface

#### 8.2 Known Issues Check
Verify no regression on previously fixed issues:
- [ ] SPI bus conflicts handled
- [ ] Buffer overflow protection
- [ ] WiFi reconnection

## Test Results Documentation

### Pass/Fail Criteria
- All basic functionality tests must pass
- No system crashes during testing
- Performance meets or exceeds previous version
- No new SCPI errors introduced

### Test Log Template
```
Test Date: [DATE]
Firmware Version: [VERSION/COMMIT]
Board Variant: [NQ1/NQ2/NQ3]
Tester: [NAME]

Test Results:
1. Basic Functionality: [PASS/FAIL]
2. Configuration: [PASS/FAIL]
3. Data Acquisition: [PASS/FAIL]
4. Communication: [PASS/FAIL]
5. Streaming: [PASS/FAIL]
6. Error Handling: [PASS/FAIL]
7. Stress Test: [PASS/FAIL]
8. Regression: [PASS/FAIL]

Issues Found:
- [Description of any issues]

Notes:
- [Additional observations]
```

## Automated Test Script

Create a Python script for automated testing:

```python
#!/usr/bin/env python3
"""
DAQiFi Firmware Automated Test Script
"""
import serial
import time
import sys

class DaqifiTester:
    def __init__(self, port, baudrate=115200):
        self.ser = serial.Serial(port, baudrate, timeout=1)
        time.sleep(2)  # Wait for device to initialize
        
    def send_scpi(self, command):
        """Send SCPI command and return response"""
        self.ser.write(f"{command}\n".encode())
        return self.ser.readline().decode().strip()
    
    def test_basic_communication(self):
        """Test basic SCPI communication"""
        print("Testing basic communication...")
        idn = self.send_scpi("*IDN?")
        if "DAQiFi" in idn:
            print(f"✓ Device identified: {idn}")
            return True
        else:
            print(f"✗ Communication failed: {idn}")
            return False
    
    def test_error_handling(self):
        """Test error handling"""
        print("Testing error handling...")
        self.send_scpi("INVALID:CMD")
        error = self.send_scpi("SYST:ERR?")
        if "No error" not in error:
            print(f"✓ Error handling works: {error}")
            return True
        else:
            print("✗ Error not detected")
            return False
    
    def test_configuration(self):
        """Test configuration commands"""
        print("Testing configuration...")
        variant = self.send_scpi("SYST:BVAR?")
        print(f"  Board variant: {variant}")
        return True
    
    def run_all_tests(self):
        """Run all automated tests"""
        print("Starting automated firmware tests...")
        print("-" * 50)
        
        tests = [
            self.test_basic_communication,
            self.test_error_handling,
            self.test_configuration,
        ]
        
        passed = 0
        for test in tests:
            if test():
                passed += 1
            time.sleep(0.5)
        
        print("-" * 50)
        print(f"Tests passed: {passed}/{len(tests)}")
        return passed == len(tests)

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python test_firmware.py COM3")
        sys.exit(1)
    
    tester = DaqifiTester(sys.argv[1])
    success = tester.run_all_tests()
    sys.exit(0 if success else 1)
```

## Post-Test Actions

1. **If All Tests Pass:**
   - Document test results
   - Proceed with git commit
   - Update version number if applicable

2. **If Tests Fail:**
   - Document failures in detail
   - Debug and fix issues
   - Re-run failed tests
   - Perform full regression test after fixes

## Commit Message Template
```
feat/fix/chore: Brief description of changes

- Detailed change 1
- Detailed change 2
- Detailed change 3

Tested on: [Board variant]
Test status: All tests passed
Related issues: #XXX
```

## Notes
- Always test on actual hardware before committing
- If testing on multiple board variants, document results for each
- Keep test logs for future reference
- Update this procedure as new features are added