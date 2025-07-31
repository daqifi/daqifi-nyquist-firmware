#!/bin/bash
# Test script to verify BAT:LEVel returns -1 in standby mode

send_scpi() {
    (echo -e "$1\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -5
}

echo "=== Testing BAT:LEVel in Standby Mode ==="

# First ensure device is in standby
echo "Setting device to STANDBY mode..."
send_scpi "SYST:POW:STAT 0"
sleep 1

# Check power state
echo -e "\nChecking power state (should be 0)..."
send_scpi "SYST:POW:STAT?"

# Check battery level (should be -1)
echo -e "\nChecking battery level in STANDBY (should be -1)..."
send_scpi "SYST:BAT:LEVel?"

# Now power up
echo -e "\nPowering up device..."
send_scpi "SYST:POW:STAT 1"
sleep 2

# Check power state
echo -e "\nChecking power state (should be 1)..."
send_scpi "SYST:POW:STAT?"

# Check battery level (should be 0-100)
echo -e "\nChecking battery level when POWERED (should be 0-100)..."
send_scpi "SYST:BAT:LEVel?"

echo -e "\n=== Test Complete ==="