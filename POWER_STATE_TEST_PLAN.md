# Power State Test Plan

## Test Environment Setup

1. Connect device via USB to PC
2. Attach device to WSL: `powershell.exe -Command "usbipd attach --wsl --busid 2-4"`
3. Verify device: `ls -la /dev/ttyACM0`
4. Have battery connected (if available)

## Test Commands Helper

Send SCPI command function:
```bash
send_scpi() {
    (echo -e "$1\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -5
}
```

## Test Plan

### 1. Initial State Verification

```bash
# Check device identity
send_scpi "*IDN?"

# Check initial power state (should be 0 for STANDBY)
send_scpi "SYST:POW:STAT?"

# Get detailed system info
send_scpi "SYST:INFO?"

# Check battery status
send_scpi "SYST:POW:SOURce?"
send_scpi "SYST:BAT:LEV?"

# Check OTG mode (should be OFF)
send_scpi "SYST:POW:OTG?"
```

### 2. Power State Transitions

#### Test 2.1: STANDBY to POWERED_UP
```bash
# Power up the device
send_scpi "SYST:POW:STAT 1"

# Verify state changed (should return 1)
send_scpi "SYST:POW:STAT?"

# Check system info to see detailed state
send_scpi "SYST:INFO?"

# Verify external supplies are enabled
send_scpi "SYST:FORC5V5POW:STAT?"
```

#### Test 2.2: POWERED_UP to STANDBY
```bash
# Power down the device
send_scpi "SYST:POW:STAT 0"

# Verify state changed (should return 0)
send_scpi "SYST:POW:STAT?"

# Check system info
send_scpi "SYST:INFO?"
```

#### Test 2.3: Invalid State Requests
```bash
# Try setting invalid state (should error)
send_scpi "SYST:POW:STAT 2"
send_scpi "SYST:POW:STAT 3"

# Verify state unchanged
send_scpi "SYST:POW:STAT?"
```

### 3. USB Disconnect/Reconnect Test

**CRITICAL TEST - Verifies the main fix**

1. Ensure device is powered up:
```bash
send_scpi "SYST:POW:STAT 1"
send_scpi "SYST:POW:STAT?"
```

2. **Physically disconnect USB cable**
   - Device should maintain power (3.3V rail stays enabled)
   - LEDs should remain lit

3. **Reconnect USB cable**

4. Re-attach to WSL and verify device still responsive:
```bash
powershell.exe -Command "usbipd attach --wsl --busid 2-4"
sleep 2
send_scpi "*IDN?"
send_scpi "SYST:POW:STAT?"
```

### 4. Manual OTG Control Test

```bash
# Check initial OTG state (should be 0/OFF)
send_scpi "SYST:POW:OTG?"

# Enable OTG boost mode
send_scpi "SYST:POW:OTG 1"

# Verify OTG enabled
send_scpi "SYST:POW:OTG?"

# Check system info for OTG status
send_scpi "SYST:INFO?"

# Disable OTG
send_scpi "SYST:POW:OTG 0"

# Verify OTG disabled
send_scpi "SYST:POW:OTG?"
```

### 5. Battery Operation Test (if battery connected)

1. Power up device:
```bash
send_scpi "SYST:POW:STAT 1"
```

2. Enable OTG for battery boost:
```bash
send_scpi "SYST:POW:OTG 1"
```

3. **Disconnect USB cable**
   - Device should continue running on battery power
   - OTG boost provides 5V from battery

4. **Reconnect USB and re-attach**:
```bash
powershell.exe -Command "usbipd attach --wsl --busid 2-4"
sleep 2
```

5. Verify device state and disable OTG:
```bash
send_scpi "SYST:POW:STAT?"
send_scpi "SYST:POW:OTG 0"
```

### 6. Error Queue Verification

After each test section, check for errors:
```bash
# Check error queue (should be "No error")
send_scpi "SYST:ERR?"

# If errors exist, keep reading until "No error"
send_scpi "SYST:ERR?"
```

### 7. Streaming Test

Verify data streaming still works after power state changes:
```bash
# Configure simple streaming
send_scpi "SAMP:FREQ 100"
send_scpi "SAMP:COUN 10"

# Start acquisition
send_scpi "INIT"

# Query state
send_scpi "STAT:OPER?"

# Abort if needed
send_scpi "ABOR"
```

### 8. Reset Test

```bash
# Send reset notification and reset device
send_scpi "*RST"

# Wait for device to reboot (5-10 seconds)
sleep 10

# Re-attach and verify device is back
powershell.exe -Command "usbipd attach --wsl --busid 2-4"
sleep 2
send_scpi "*IDN?"
```

## Expected Results

1. **Power State Queries**:
   - STANDBY: Returns 0
   - POWERED_UP or POWERED_UP_EXT_DOWN: Returns 1

2. **USB Disconnect**:
   - Device maintains power without OTG
   - 3.3V rail stays enabled
   - Device responsive after reconnect

3. **OTG Control**:
   - Manual enable/disable works
   - Not automatically enabled on USB disconnect

4. **System Info**:
   - Shows correct power state names (STANDBY, POWERED_UP, etc.)
   - Shows battery status if connected
   - Shows OTG status

## Automated Test Script

Save as `test_power_states.sh`:
```bash
#!/bin/bash

# Function to send SCPI command
send_scpi() {
    echo -e "\n>>> Sending: $1"
    (echo -e "$1\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -5
    sleep 1
}

echo "=== Power State Test Suite ==="

# Initial state
echo -e "\n--- Initial State ---"
send_scpi "*IDN?"
send_scpi "SYST:POW:STAT?"
send_scpi "SYST:POW:SOURce?"
send_scpi "SYST:POW:OTG?"

# Power transitions
echo -e "\n--- Power State Transitions ---"
send_scpi "SYST:POW:STAT 1"
sleep 2
send_scpi "SYST:POW:STAT?"
send_scpi "SYST:INFO?" | grep -E "Power state|Battery|OTG"

send_scpi "SYST:POW:STAT 0"
sleep 2
send_scpi "SYST:POW:STAT?"

# OTG control
echo -e "\n--- OTG Control ---"
send_scpi "SYST:POW:OTG 1"
send_scpi "SYST:POW:OTG?"
send_scpi "SYST:POW:OTG 0"
send_scpi "SYST:POW:OTG?"

# Error check
echo -e "\n--- Error Check ---"
send_scpi "SYST:ERR?"

echo -e "\n=== Test Complete ==="
echo "Now perform manual USB disconnect/reconnect test"
```

Make executable: `chmod +x test_power_states.sh`
Run: `./test_power_states.sh`