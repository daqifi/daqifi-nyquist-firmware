#!/bin/bash
# Comprehensive WiFi/LAN SCPI command test script

# Color codes for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to send SCPI command and capture response
send_scpi() {
    local cmd="$1"
    echo -e "${YELLOW}>>> $cmd${NC}"
    result=$(echo -e "$cmd\r" | timeout 2 picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -10 | grep -v "picocom\|Terminal\|Type\|CTRL\|Thanks")
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

echo -e "${GREEN}=== DAQiFi WiFi/LAN Command Test Suite ===${NC}\n"

# Power up the device first
echo -e "${GREEN}0. Power Up Device${NC}"
send_scpi "SYST:POW:STAT 1"
send_scpi "SYST:POW:STAT?"
sleep 2

# Test basic WiFi enable/disable
echo -e "${GREEN}1. Basic WiFi Enable/Disable${NC}"
send_scpi "SYSTem:COMMunicate:LAN:ENAbled?"
send_scpi "SYSTem:COMMunicate:LAN:ENAbled 0"
send_scpi "SYSTem:COMMunicate:LAN:ENAbled?"
send_scpi "SYSTem:COMMunicate:LAN:ENAbled 1"
send_scpi "SYSTem:COMMunicate:LAN:ENAbled?"

# Test network type
echo -e "${GREEN}2. Network Type${NC}"
send_scpi "SYSTem:COMMunicate:LAN:NETType?"
send_scpi "SYSTem:COMMunicate:LAN:NETType 4"  # Soft AP
send_scpi "SYSTem:COMMunicate:LAN:NETType?"
send_scpi "SYSTem:COMMunicate:LAN:NETType 1"  # Infrastructure
send_scpi "SYSTem:COMMunicate:LAN:NETType?"

# Test IPv6 (likely not implemented based on code)
echo -e "${GREEN}3. IPv6 Support (may not be implemented)${NC}"
send_scpi "SYSTem:COMMunicate:LAN:IPV6?"
send_scpi "SYSTem:COMMunicate:LAN:IPV6 0"
send_scpi "SYSTem:COMMunicate:LAN:IPV6?"

# Test IP configuration
echo -e "${GREEN}4. IP Configuration${NC}"
send_scpi "SYSTem:COMMunicate:LAN:ADDRess?"
send_scpi "SYSTem:COMMunicate:LAN:ADDRess \"192.168.1.100\""
send_scpi "SYSTem:COMMunicate:LAN:ADDRess?"

send_scpi "SYSTem:COMMunicate:LAN:MASK?"
send_scpi "SYSTem:COMMunicate:LAN:MASK \"255.255.255.0\""
send_scpi "SYSTem:COMMunicate:LAN:MASK?"

send_scpi "SYSTem:COMMunicate:LAN:GATEway?"
send_scpi "SYSTem:COMMunicate:LAN:GATEway \"192.168.1.1\""
send_scpi "SYSTem:COMMunicate:LAN:GATEway?"

# Test DNS configuration
echo -e "${GREEN}5. DNS Configuration${NC}"
send_scpi "SYSTem:COMMunicate:LAN:DNS1?"
send_scpi "SYSTem:COMMunicate:LAN:DNS1 \"8.8.8.8\""
send_scpi "SYSTem:COMMunicate:LAN:DNS1?"

send_scpi "SYSTem:COMMunicate:LAN:DNS2?"
send_scpi "SYSTem:COMMunicate:LAN:DNS2 \"8.8.4.4\""
send_scpi "SYSTem:COMMunicate:LAN:DNS2?"

# Test MAC address
echo -e "${GREEN}6. MAC Address${NC}"
send_scpi "SYSTem:COMMunicate:LAN:MAC?"
send_scpi "SYSTem:COMMunicate:LAN:MAC \"00:11:22:33:44:55\""
send_scpi "SYSTem:COMMunicate:LAN:MAC?"

# Test connection status
echo -e "${GREEN}7. Connection Status (may not be implemented)${NC}"
send_scpi "SYSTem:COMMunicate:LAN:CONnected?"

# Test hostname
echo -e "${GREEN}8. Hostname${NC}"
send_scpi "SYSTem:COMMunicate:LAN:HOST?"
send_scpi "SYSTem:COMMunicate:LAN:HOST \"DAQiFi-Test\""
send_scpi "SYSTem:COMMunicate:LAN:HOST?"

# Test SSID
echo -e "${GREEN}9. SSID Configuration${NC}"
send_scpi "SYSTem:COMMunicate:LAN:SSID?"
send_scpi "SYSTem:COMMunicate:LAN:SSID \"TestNetwork\""
send_scpi "SYSTem:COMMunicate:LAN:SSID?"

# Test SSID scanning
echo -e "${GREEN}10. SSID Scanning${NC}"
send_scpi "SYSTem:COMMunicate:LAN:AvSSIDScan"
sleep 3  # Give time for scan
send_scpi "SYSTem:COMMunicate:LAN:AvSSID?"

# Test security settings
echo -e "${GREEN}11. Security Settings${NC}"
send_scpi "SYSTem:COMMunicate:LAN:SECurity?"
send_scpi "SYSTem:COMMunicate:LAN:SECurity 0"  # Open
send_scpi "SYSTem:COMMunicate:LAN:SECurity?"
send_scpi "SYSTem:COMMunicate:LAN:SECurity 3"  # WPA
send_scpi "SYSTem:COMMunicate:LAN:SECurity?"

# Test password
echo -e "${GREEN}12. Password Settings${NC}"
send_scpi "SYSTem:COMMunicate:LAN:PASs \"TestPassword123\""
send_scpi "SYSTem:COMMunicate:LAN:PASSCHECK"

# Test display (likely not implemented)
echo -e "${GREEN}13. Display (may not be implemented)${NC}"
send_scpi "SYSTem:COMMunicate:LAN:DISPlay"

# Test settings management
echo -e "${GREEN}14. Settings Management${NC}"
send_scpi "SYSTem:COMMunicate:LAN:APPLY"
sleep 2
send_scpi "SYSTem:COMMunicate:LAN:SAVE"
send_scpi "SYSTem:COMMunicate:LAN:LOAD"

# Test factory reset
echo -e "${GREEN}15. Factory Reset${NC}"
send_scpi "SYSTem:COMMunicate:LAN:FACRESET"

# Test clear settings
echo -e "${GREEN}16. Clear Settings${NC}"
send_scpi "SYSTem:COMMunicate:LAN:CLEAR"

# Check error queue
echo -e "${GREEN}17. Error Queue Check${NC}"
send_scpi "SYST:ERR?"

echo -e "${GREEN}=== Test Complete ===${NC}"
echo -e "${YELLOW}Review the output above to identify which commands are working vs not implemented${NC}"