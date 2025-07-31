#!/bin/bash
# Test script for IMPLEMENTED WiFi commands only

# Color codes
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Function to send SCPI command
send_scpi() {
    echo -e "${YELLOW}>>> $1${NC}"
    (echo -e "$1\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -5 | grep -v "picocom\|Terminal"
    echo
}

echo -e "${GREEN}=== Testing IMPLEMENTED WiFi Commands ===${NC}\n"

# Power up first
echo -e "${GREEN}1. Power Up${NC}"
send_scpi "SYST:POW:STAT 1"
sleep 2

# Test WiFi enable
echo -e "${GREEN}2. WiFi Enable/Disable${NC}"
send_scpi "SYSTem:COMMunicate:LAN:ENAbled?"
send_scpi "SYSTem:COMMunicate:LAN:ENAbled 1"

# Test network type
echo -e "${GREEN}3. Network Type${NC}"
send_scpi "SYSTem:COMMunicate:LAN:NETType?"
send_scpi "SYSTem:COMMunicate:LAN:NETType 4"  # Soft AP

# Test IP configuration (GET only)
echo -e "${GREEN}4. IP Configuration (Read)${NC}"
send_scpi "SYSTem:COMMunicate:LAN:ADDRess?"
send_scpi "SYSTem:COMMunicate:LAN:MASK?"
send_scpi "SYSTem:COMMunicate:LAN:GATEway?"
send_scpi "SYSTem:COMMunicate:LAN:MAC?"

# Test hostname (GET only)
echo -e "${GREEN}5. Hostname (Read)${NC}"
send_scpi "SYSTem:COMMunicate:LAN:HOST?"

# Test SSID
echo -e "${GREEN}6. SSID Configuration${NC}"
send_scpi "SYSTem:COMMunicate:LAN:SSID?"
send_scpi "SYSTem:COMMunicate:LAN:SSID \"DAQiFi\""

# Test security
echo -e "${GREEN}7. Security Configuration${NC}"
send_scpi "SYSTem:COMMunicate:LAN:SECurity?"
send_scpi "SYSTem:COMMunicate:LAN:SECurity 0"  # Open

# Apply settings
echo -e "${GREEN}8. Apply Settings${NC}"
send_scpi "SYSTem:COMMunicate:LAN:APPLY"

# Save/Load
echo -e "${GREEN}9. Save/Load Settings${NC}"
send_scpi "SYSTem:COMMunicate:LAN:SAVE"
send_scpi "SYSTem:COMMunicate:LAN:LOAD"

echo -e "${GREEN}=== Test Complete ===${NC}"