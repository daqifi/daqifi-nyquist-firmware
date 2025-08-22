# Power State Machine Test Procedure
## PR #99 Validation Tests

### Test Environment Setup
- Device: DAQiFi Nyquist with battery installed
- USB connection for SCPI commands via /dev/ttyACM0
- Variable power supply or battery simulator (optional for precise testing)
- Serial monitor for logging

### Key Changes to Validate
1. **Battery threshold hysteresis** (15% conservation, 5% critical, 2% buffer)
2. **Shutdown notification** coordination (renamed from powerDnAllowed)
3. **BQ24297 initialization** with 5-second timeout
4. **Battery refresh** before critical shutdown
5. **Rail disabling** before standby transition
6. **Power state transitions** with proper sequencing

### Power State Values
- **0** = STANDBY (CPU on but standby, powers off if disconnected)
- **1** = POWERED_UP (fully powered)
- **2** = POWERED_UP_EXT_DOWN (partial power, low battery mode)

---

## Test Procedures

### Test 1: BQ24297 Initialization Timeout
**Purpose:** Verify 5-second timeout for BQ24297 charger IC initialization

**Steps:**
1. Power on device with USB connected
2. Monitor debug output during initialization
3. Verify BQ24297 initializes within 5 seconds
4. Check SYST:INFO output shows battery status

**Expected Results:**
- BQ24297 should initialize successfully
- No timeout errors in debug log
- Battery status visible in SYST:INFO

**SCPI Commands:**
```bash
# Check system info for battery status
SYST:INF?

# Check power state
SYST:POW:STAT?
```

---

### Test 2: Battery Conservation Threshold (15%)
**Purpose:** Verify device enters POWERED_UP_EXT_DOWN at 15% battery

**Steps:**
1. Disconnect USB power (device on battery)
2. Simulate or wait for battery to reach ~16%
3. Monitor power state transitions
4. Verify transition occurs at 15% (±2% hysteresis)
5. Verify external rails (5V, 12V, Vref) are disabled

**Expected Results:**
- State changes from POWERED_UP to POWERED_UP_EXT_DOWN at 15%
- External rails disabled
- Device remains operational on battery

**SCPI Commands:**
```bash
# Monitor battery percentage
SYST:BAT:LEV?

# Check power state
SYST:POW:STAT?

# Force state check
SYST:POW:STAT 1  # Try to go to POWERED_UP
# Should refuse if battery < 17%
```

---

### Test 3: Critical Battery Shutdown (5%)
**Purpose:** Verify device shuts down at 5% battery with proper notification

**Steps:**
1. Start with device in POWERED_UP_EXT_DOWN state
2. Simulate or wait for battery to reach ~6%
3. Monitor for LED warning sequence
4. Verify shutdown occurs at 5% after notification
5. Check debug logs for "Shutdown: Ready" status

**Expected Results:**
- LED warning sequence activates before shutdown
- shutdownNotified flag set by UI task
- Battery reading refreshed before critical check
- All rails disabled before STANDBY transition
- Device enters STANDBY at 5%

**Debug Monitoring:**
```bash
# Watch debug output for shutdown sequence
SYST:INF?
# Look for "Shutdown: Ready" in Power.ShutdownNotified field
```

---

### Test 4: Hysteresis Prevention (2% Buffer)
**Purpose:** Verify hysteresis prevents oscillation near thresholds

**Steps:**
1. Simulate battery at 16% (just above conservation)
2. Let battery drop to 15% → enters POWERED_UP_EXT_DOWN
3. Simulate small charge increase to 16%
4. Verify state doesn't immediately return to POWERED_UP
5. Increase to 17% or higher
6. Verify state returns to POWERED_UP

**Expected Results:**
- No state oscillation between 15-17%
- Requires 17% to re-enter POWERED_UP from conservation
- Requires 7% to exit critical shutdown state

---

### Test 5: External Power Detection
**Purpose:** Verify proper state transitions with USB power

**Steps:**
1. Start with battery < 15% (conservation mode)
2. Connect USB power
3. Verify immediate transition to POWERED_UP
4. Disconnect USB
5. Verify appropriate state based on battery level

**Expected Results:**
- External power overrides battery thresholds
- Immediate POWERED_UP when USB connected
- Returns to appropriate battery state when disconnected

**SCPI Commands:**
```bash
# With USB connected
SYST:POW:STAT?  # Should allow POWERED_UP

# Disconnect USB, check state
SYST:POW:STAT?  # Should reflect battery level
```

---

### Test 6: Rail Control Verification
**Purpose:** Verify all rails properly disabled before standby

**Steps:**
1. Monitor rail states during critical shutdown
2. Use debug output or hardware verification
3. Check EN_5_10V, EN_12V, EN_Vref signals

**Expected Results:**
- EN_5_10V_Val = false (disabled)
- EN_12V_Val = true (inverted logic - true = off)
- EN_Vref_Val = false (disabled)
- All rails off before STANDBY transition

---

### Test 7: Shutdown Notification Coordination
**Purpose:** Verify UI task properly sets shutdownNotified flag

**Steps:**
1. Trigger low battery condition (< 5%)
2. Monitor LED sequence
3. Check debug output for shutdownNotified flag
4. Verify shutdown only after notification complete

**Expected Results:**
- LED warning sequence completes
- shutdownNotified flag transitions false → true
- Power task waits for flag before STANDBY
- Clean shutdown sequence

**Debug Output Check:**
```bash
SYST:INF?
# Power.ShutdownNotified field should show:
# "Shutdown: Pending" → "Shutdown: Ready"
```

---

### Test 8: State Machine Command Validation
**Purpose:** Verify SCPI power commands respect battery levels

**Steps:**
1. Set battery to 10% (below conservation)
2. Try SYST:POW:STAT 1 (request POWERED_UP)
3. Verify command rejected with appropriate error
4. Set battery to 20%
5. Try SYST:POW:STAT 1 again
6. Verify command accepted

**Expected Results:**
- Commands rejected when battery insufficient
- Error messages indicate battery constraint
- Commands accepted when battery adequate

**SCPI Test Sequence:**
```bash
# At 10% battery
SYST:POW:STAT 1
SYST:ERR?  # Should show battery error

# At 20% battery  
SYST:POW:STAT 1
SYST:POW:STAT?  # Should be 1 (POWERED_UP)
```

---

### Test 9: Power Cycle Persistence
**Purpose:** Verify settings survive power cycles

**Steps:**
1. Configure device in POWERED_UP state
2. Note current settings
3. Remove all power (USB + battery)
4. Restore power
5. Verify device returns to appropriate state
6. Check settings retained

**Expected Results:**
- Device initializes to correct state based on battery
- Settings preserved through power cycle
- No corruption or unexpected behavior

---

### Test 10: Stress Test - Rapid State Changes
**Purpose:** Verify stability under rapid state transitions

**Steps:**
1. Connect/disconnect USB power rapidly (10 cycles)
2. Verify state machine remains stable
3. Check for memory leaks or crashes
4. Monitor error queue for issues

**Expected Results:**
- No crashes or hangs
- State transitions remain consistent
- Error queue remains clean
- Memory usage stable

---

## Automated Test Script

Create `/tmp/power_test.sh`:
```bash
#!/bin/bash

# Power State Machine Test Script
DEVICE=/dev/ttyACM0

send_cmd() {
    local cmd="$1"
    (echo -e "${cmd}\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 $DEVICE 2>&1 | tail -20
}

echo "=== Power State Machine Test Suite ==="
echo "Testing PR #99 changes..."
echo

echo "Test 1: Check current state and battery"
send_cmd "SYST:INF?" | grep -E "(PowerState|Battery|Shutdown)"
sleep 1

echo "Test 2: Verify power state"
send_cmd "SYST:POW:STAT?"
sleep 1

echo "Test 3: Check battery level"
send_cmd "SYST:BAT:LEV?"
sleep 1

echo "Test 4: Check error queue"
send_cmd "SYST:ERR?"
sleep 1

echo "Test 5: Attempt state change to POWERED_UP"
send_cmd "SYST:POW:STAT 1"
send_cmd "SYST:POW:STAT?"
send_cmd "SYST:ERR?"

echo "Test 6: Battery info details"
send_cmd "SYST:INF?" | grep -A5 "Battery"

echo "=== Test Complete ==="
```

---

## Success Criteria
✅ All state transitions occur at correct thresholds
✅ Hysteresis prevents oscillation
✅ Shutdown notification works correctly
✅ Rails properly disabled before standby
✅ Battery reading refreshed before critical decisions
✅ BQ24297 initializes within timeout
✅ External power detection works correctly
✅ SCPI commands respect battery constraints
✅ No crashes or unexpected behavior
✅ Debug output shows correct status messages

## Notes
- Use SYST:INF? for comprehensive status (not SYST:INFO)
- SYST:POW:STAT uses values 0, 1, 2 (not true/false)
- SYST:BAT:LEV? returns battery percentage as integer
- SYST:ERR? returns error queue status
- Monitor debug UART for detailed logs
- Test with actual battery discharge for most realistic results
- Document any deviations or issues found

## SCPI Command Reference for Power Testing
```
*IDN?                    - Device identification
SYST:INF?                - Comprehensive system info with all status
SYST:POW:STAT?           - Get power state (0=STANDBY, 1=POWERED_UP, 2=POWERED_UP_EXT_DOWN)
SYST:POW:STAT <n>        - Set power state (0, 1, or 2)
SYST:BAT:LEV?            - Get battery level percentage
SYST:POW:SOUR?           - Get external power source type
SYST:POW:OTG?            - Get OTG mode status
SYST:POW:OTG <n>         - Set OTG mode (0=off, 1=on)
SYST:ERR?                - Get next error from queue
SYST:ERR:COUN?           - Get error count
SYST:FORC5V5POW:STAT <n> - Force 5V rail control (debug only)
```