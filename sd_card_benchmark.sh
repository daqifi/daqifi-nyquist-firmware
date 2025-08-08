#!/bin/bash
#
# DAQiFi SD Card Benchmark Script
# 
# This script tests SD card write performance at various sizes using the
# SYST:STOR:SD:BENCH SCPI command. It handles device initialization,
# WiFi disabling, and SD card enabling.
#
# Usage: ./sd_card_benchmark.sh [card_description]
# Example: ./sd_card_benchmark.sh "SanDisk Ultra 32GB"

DEVICE="/dev/ttyACM0"
CARD_DESC="${1:-Unknown SD Card}"
SIZES=(1 5 10 25 50 100 250 500 750 1024)

# Color codes for output
GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "=========================================="
echo "DAQiFi SD Card Performance Benchmark"
echo "=========================================="
echo "Date: $(date)"
echo "Card: $CARD_DESC"
echo ""

# Check if device exists
if [ ! -e "$DEVICE" ]; then
    echo -e "${RED}Error: Device $DEVICE not found${NC}"
    echo "Please ensure the DAQiFi device is connected and attached to WSL"
    exit 1
fi

# Function to send SCPI command and check response
send_scpi() {
    local cmd="$1"
    local wait="${2:-1}"
    local timeout="${3:-2000}"
    echo -e "${YELLOW}Sending: $cmd${NC}"
    result=$(echo -e "$cmd\r" | picocom -b 115200 -q -x $timeout $DEVICE 2>&1)
    echo "$result" | tail -10
    sleep $wait
}

echo -e "${GREEN}Step 1: Checking device connection...${NC}"
send_scpi "*IDN?" 0.5

echo -e "\n${GREEN}Step 2: Powering up device...${NC}"
send_scpi "SYST:POW:STAT 2" 3

echo -e "\n${GREEN}Step 3: Disabling WiFi (required for SD card access)...${NC}"
send_scpi "SYST:COMM:LAN:ENABLED 0" 1
send_scpi "SYST:COMM:LAN:APPLY" 2

echo -e "\n${GREEN}Step 4: Enabling SD card...${NC}"
send_scpi "SYST:STOR:SD:ENABLE 1" 2

echo -e "\n${GREEN}Step 5: Enabling SD logging mode...${NC}"
send_scpi "SYST:STOR:SD:LOGGING 1" 1

echo -e "\n${GREEN}Step 6: Running benchmark tests...${NC}"
echo ""
echo "| Test Size | Time (ms) | Speed (bytes/sec) | Speed (KB/s) | Speed (MB/s) | Status      |"
echo "|-----------|-----------|-------------------|--------------|--------------|-------------|"

# Run benchmark tests
for size in "${SIZES[@]}"; do
    printf "| %-9s | " "${size} KB"
    
    # Run benchmark command with pattern 0 (zeros)
    # Timeout is set based on size (larger sizes need more time)
    timeout_ms=$((6000 + size * 10))
    result=$(echo -e "SYST:STOR:SD:BENCH $size,0\r" | picocom -b 115200 -q -x $timeout_ms $DEVICE 2>&1)
    
    # Check for various response patterns
    if echo "$result" | grep -q "Benchmark complete:"; then
        # Success - extract results
        complete_line=$(echo "$result" | grep "Benchmark complete:" | tail -1)
        bytes=$(echo "$complete_line" | sed -n 's/.*complete: \([0-9]\+\) bytes.*/\1/p')
        time_ms=$(echo "$complete_line" | sed -n 's/.*in \([0-9]\+\) ms.*/\1/p')
        speed_bps=$(echo "$complete_line" | sed -n 's/.*= \([0-9]\+\) bytes\/sec.*/\1/p')
        
        if [[ -n "$time_ms" && -n "$speed_bps" ]]; then
            speed_kbs=$(awk "BEGIN {printf \"%.1f\", $speed_bps / 1024}")
            speed_mbs=$(awk "BEGIN {printf \"%.2f\", $speed_bps / 1048576}")
            printf "%-9s | %-17s | %-12s | %-12s | " "$time_ms" "$speed_bps" "$speed_kbs" "$speed_mbs"
            echo -e "${GREEN}SUCCESS${NC}    |"
        else
            printf "%-9s | %-17s | %-12s | %-12s | " "ERROR" "-" "-" "-"
            echo -e "${RED}PARSE ERR${NC}  |"
        fi
    elif echo "$result" | grep -q "Write failed"; then
        # Write failure - check if it's a timeout
        if echo "$result" | grep -q "buffer timeout"; then
            printf "%-9s | %-17s | %-12s | %-12s | " "TIMEOUT" "-" "-" "-"
            echo -e "${RED}BUF TIMEOUT${NC}|"
        else
            printf "%-9s | %-17s | %-12s | %-12s | " "FAILED" "-" "-" "-"
            echo -e "${RED}WRITE FAIL${NC} |"
        fi
    else
        # Other failure
        printf "%-9s | %-17s | %-12s | %-12s | " "ERROR" "-" "-" "-"
        echo -e "${RED}FAILED${NC}     |"
    fi
    
    # Small delay between tests
    sleep 1
done

echo ""
echo "=========================================="
echo "Benchmark complete!"
echo "=========================================="

# Optional: Save results to file
if [ -n "$2" ]; then
    echo -e "\nSaving results to: $2"
    # Implementation for saving results could go here
fi