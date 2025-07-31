#!/bin/bash
# Test script to verify serial number format (no # prefix)

send_scpi() {
    (echo -e "$1\r"; sleep 0.5) | picocom -b 115200 -q -x 1000 /dev/ttyACM0 2>&1 | tail -5
}

echo "=== Testing Serial Number Format ==="

# Get serial number
echo -e "\nGetting device serial number (should be plain hex string)..."
send_scpi "SYSTem:SERialNUMber?"

echo -e "\n=== Test Complete ==="
echo "Expected format: 16-character hex string (e.g., 7E280C276200EA4B)"
echo "NOT: #H7E280C276200EA4B"