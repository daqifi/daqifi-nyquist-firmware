#!/bin/bash
# Quick power state test for DAQiFi device

# Color codes
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to send SCPI command
send_scpi() {
    echo -e "${YELLOW}>>> $1${NC}"
    result=$(echo -e "$1\r" | timeout 2 picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -10 | grep -v "picocom\|Terminal\|Type\|CTRL\|Thanks")
    echo "$result"
    echo
    sleep 0.5
}

# Check if device exists
if [ ! -e /dev/ttyACM0 ]; then
    echo -e "${RED}Error: Device not found at /dev/ttyACM0${NC}"
    echo "Try: powershell.exe -Command \"usbipd attach --wsl --busid 2-4\""
    exit 1
fi

echo -e "${GREEN}=== DAQiFi Power State Test ===${NC}\n"

# Test 1: Check initial state
echo -e "${GREEN}1. Initial State Check${NC}"
send_scpi "*IDN?"
send_scpi "SYST:POW:STAT?"

# Test 2: Power up
echo -e "${GREEN}2. Power Up Test${NC}"
send_scpi "SYST:POW:STAT 1"
send_scpi "SYST:POW:STAT?"

# Test 3: Check battery and OTG
echo -e "${GREEN}3. Battery and OTG Status${NC}"
send_scpi "SYST:BAT:STAT?"
send_scpi "SYST:POW:OTG?"

# Test 4: Power down
echo -e "${GREEN}4. Power Down Test${NC}"
send_scpi "SYST:POW:STAT 0"
send_scpi "SYST:POW:STAT?"

# Test 5: Error check
echo -e "${GREEN}5. Error Queue Check${NC}"
send_scpi "SYST:ERR?"

echo -e "${GREEN}=== Basic Tests Complete ===${NC}"
echo -e "${YELLOW}Now disconnect and reconnect USB cable to test power persistence${NC}"
echo -e "${YELLOW}Device should maintain power when USB is disconnected${NC}"