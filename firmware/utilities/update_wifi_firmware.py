#!/usr/bin/env python3
"""
WiFi Module Firmware Update Script for DAQiFi Nyquist

Automates the process of updating the WINC1500 WiFi module firmware to version 19.7.7.

Usage:
    python update_wifi_firmware.py COM3
    python update_wifi_firmware.py COM3 --firmware-version 19.7.7
    python update_wifi_firmware.py COM3 --tool-path "C:\path\to\winc_flash_tool.cmd"
"""

import serial
import time
import sys
import os
import subprocess
import argparse
from pathlib import Path

# Force UTF-8 output for Windows console (only if not already wrapped)
if sys.platform == 'win32':
    import codecs
    if hasattr(sys.stdout, 'buffer'):
        sys.stdout = codecs.getwriter('utf-8')(sys.stdout.buffer, 'strict')
        sys.stderr = codecs.getwriter('utf-8')(sys.stderr.buffer, 'strict')

class WiFiFirmwareUpdater:
    def __init__(self, port, firmware_version='19.7.7', tool_path=None):
        """Initialize WiFi firmware updater"""
        self.port = port
        self.firmware_version = firmware_version
        self.tool_path = tool_path or self.find_winc_tool()
        self.ser = None

    def find_winc_tool(self):
        """Try to locate winc_flash_tool.cmd"""
        # Common locations
        search_paths = [
            # MCC Harmony installation (standard location)
            Path.home() / ".mcc" / "harmony" / "content" / "wireless_wifi" / "v3.12.1" / "utilities" / "wifi" / "winc" / "winc_flash_tool.cmd",
            # Current directory
            Path.cwd() / "winc_flash_tool.cmd",
            # Script directory
            Path(__file__).parent / "winc_flash_tool.cmd",
            # User documents
            Path.home() / "Documents" / "winc_flash_tool.cmd",
        ]

        for path in search_paths:
            if path.exists():
                return str(path)

        return None

    def log(self, message):
        """Log message to console"""
        print(message)
        sys.stdout.flush()

    def connect(self):
        """Connect to serial port"""
        try:
            self.ser = serial.Serial(self.port, 115200, timeout=2)
            time.sleep(0.5)
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
            self.log(f"Connected to {self.port} at 115200 baud")
            return True
        except Exception as e:
            self.log(f"ERROR: Failed to connect: {e}")
            return False

    def disconnect(self):
        """Disconnect from serial port"""
        if self.ser and self.ser.is_open:
            self.ser.close()
            self.log(f"Disconnected from {self.port}")
            time.sleep(1)  # Give OS time to release port

    def send_command(self, cmd, delay=0.5):
        """Send SCPI command and return response"""
        if not self.ser or not self.ser.is_open:
            self.log("ERROR: Serial port not open")
            return ""

        self.ser.reset_input_buffer()
        self.ser.write(f"{cmd}\r\n".encode())
        time.sleep(delay)
        response = self.ser.read(self.ser.in_waiting).decode('ascii', errors='ignore')

        self.log(f"  > {cmd}")
        if response.strip():
            for line in response.strip().split('\n'):
                if line.strip():
                    self.log(f"  < {line.strip()}")

        return response.strip()

    def prepare_for_update(self):
        """Send SCPI commands to prepare device for firmware update"""
        self.log("\n" + "=" * 60)
        self.log("Step 1: Preparing device for WiFi firmware update")
        self.log("=" * 60)

        # Power up device
        self.log("\nPowering up device...")
        self.send_command("SYSTem:POWer:STATe 1", delay=1.0)

        # Enter firmware update mode
        self.log("\nEntering WiFi firmware update mode...")
        self.send_command("SYSTem:COMMunicate:LAN:FWUpdate", delay=0.5)

        # Apply settings
        self.log("\nApplying settings...")
        self.send_command("SYSTem:COMMunicate:LAN:APPLY", delay=1.0)

        self.log("\n✓ Device prepared for firmware update")

    def run_flash_tool(self):
        """Run winc_flash_tool.cmd to update firmware"""
        self.log("\n" + "=" * 60)
        self.log("Step 2: Flashing WiFi module firmware")
        self.log("=" * 60)

        if not self.tool_path:
            self.log("\nERROR: winc_flash_tool.cmd not found!")
            self.log("Please specify the path using --tool-path option")
            self.log("\nExample:")
            self.log(f'  python {sys.argv[0]} {self.port} --tool-path "C:\\path\\to\\winc_flash_tool.cmd"')
            return False

        if not os.path.exists(self.tool_path):
            self.log(f"\nERROR: Tool not found at: {self.tool_path}")
            return False

        # Get the directory containing the tool (we need to run from there)
        tool_dir = os.path.dirname(os.path.abspath(self.tool_path))
        tool_name = os.path.basename(self.tool_path)

        # Build command
        # Format: winc_flash_tool.cmd /p COM13 /d WINC1500 /v 19.7.7 /x /e /i aio /w
        cmd = [
            tool_name,  # Use relative name since we're changing directory
            '/p', self.port,
            '/d', 'WINC1500',
            '/v', self.firmware_version,
            '/x',  # Extended options
            '/e',  # Erase
            '/i', 'aio',  # Image type: all-in-one
            '/w'   # Write
        ]

        self.log(f"\nWorking directory: {tool_dir}")
        self.log(f"Running: {' '.join(cmd)}")
        self.log("\nThis may take several minutes...")

        try:
            # Run the flash tool from its own directory
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=300,  # 5 minute timeout
                cwd=tool_dir,  # Run from tool's directory so it finds the files
                shell=True  # Required for .cmd files on Windows
            )

            # Display output
            if result.stdout:
                self.log("\n--- Flash Tool Output ---")
                self.log(result.stdout)

            if result.stderr:
                self.log("\n--- Flash Tool Errors ---")
                self.log(result.stderr)

            if result.returncode == 0:
                self.log("\n✓ WiFi firmware flashed successfully")
                return True
            else:
                self.log(f"\n✗ Flash tool failed with exit code {result.returncode}")
                return False

        except subprocess.TimeoutExpired:
            self.log("\n✗ ERROR: Flash tool timed out after 5 minutes")
            return False
        except Exception as e:
            self.log(f"\n✗ ERROR running flash tool: {e}")
            return False

    def restore_normal_operation(self):
        """Send SCPI commands to restore normal WiFi operation"""
        self.log("\n" + "=" * 60)
        self.log("Step 3: Restoring normal WiFi operation")
        self.log("=" * 60)

        # Disable USB transparent mode
        self.log("\nDisabling USB transparent mode...")
        self.send_command("SYSTem:USB:SetTransparentMode 0", delay=0.5)

        # Enable WiFi
        self.log("\nEnabling WiFi...")
        self.send_command("SYSTem:COMMunicate:LAN:ENabled 1", delay=0.5)

        # Apply settings
        self.log("\nApplying settings...")
        self.send_command("SYSTem:COMMunicate:LAN:APPLY", delay=2.0)

        self.log("\n✓ Normal WiFi operation restored")

    def verify_update(self):
        """Verify the WiFi firmware version"""
        self.log("\n" + "=" * 60)
        self.log("Step 4: Verifying firmware update")
        self.log("=" * 60)

        # Wait for WiFi to initialize
        self.log("\nWaiting for WiFi module to initialize...")
        time.sleep(3.0)

        # Query WiFi chip info
        self.log("\nQuerying WiFi chip info...")
        chip_info = self.send_command("SYSTem:COMMunicate:LAN:GETChipInfo?", delay=0.5)

        # Parse response
        if self.firmware_version in chip_info:
            self.log(f"\n✓ Firmware update verified: {chip_info}")
            return True
        else:
            self.log(f"\n⚠ Warning: Could not verify firmware version")
            self.log(f"  Expected: {self.firmware_version}")
            self.log(f"  Response: {chip_info}")
            return False

    def check_current_version(self):
        """Check current WiFi firmware version and device info"""
        self.log("\n" + "=" * 60)
        self.log("Pre-Update Check: Current Device Status")
        self.log("=" * 60)

        # Get device hardware/firmware version
        self.log("\nQuerying device version...")
        idn = self.send_command("*IDN?", delay=0.3)

        # Parse IDN response - format: "Manufacturer,Model,Serial,FirmwareVersion"
        if idn:
            idn_clean = idn.replace('*IDN?', '').replace('DAQIFI>', '').strip()
            fields = idn_clean.split(',')
            if len(fields) >= 4:
                self.log(f"  Device: {fields[1].strip()}")
                self.log(f"  Firmware: {fields[3].strip()}")

        # Get system info for HW/FW versions
        self.log("\nQuerying system information...")
        syst_info = self.send_command("SYST:INFO?", delay=0.5)

        # Parse HW/FW versions from header
        import re
        hw_version = "Unknown"
        fw_version = "Unknown"

        for line in syst_info.split('\n'):
            if 'HW:' in line and 'FW:' in line:
                hw_match = re.search(r'HW:(\S+)', line)
                if hw_match:
                    hw_version = hw_match.group(1)
                fw_match = re.search(r'FW:(\S+)', line)
                if fw_match:
                    fw_version = fw_match.group(1)
                break

        self.log(f"  Hardware Version: {hw_version}")
        self.log(f"  Firmware Version: {fw_version}")

        # Check current WiFi firmware version
        self.log("\nQuerying WiFi module firmware...")
        chip_info = self.send_command("SYSTem:COMMunicate:LAN:GETChipInfo?", delay=0.5)

        # Parse response - remove command echo and prompt
        chip_info_clean = chip_info.replace('SYSTem:COMMunicate:LAN:GETChipInfo?', '').replace('DAQIFI>', '').strip()
        # Also handle newlines and get the actual response
        for line in chip_info_clean.split('\n'):
            line = line.strip()
            if line and not line.startswith('SYST') and not line.startswith('DAQIFI'):
                chip_info_clean = line
                break

        # Check if command is supported and parse version
        if "ERROR" in chip_info or chip_info_clean.startswith("-"):
            self.log(f"  WiFi Firmware: < 19.7.7 (GETChipInfo command not supported)")
            self.log("  ⚠ WiFi module needs update")
            return False  # Needs update
        else:
            self.log(f"  WiFi Chip Info: {chip_info_clean}")

            # Check if already at target version
            if self.firmware_version in chip_info_clean:
                self.log(f"\n✓ WiFi firmware is already at version {self.firmware_version}")
                self.log("  No update needed")
                return True  # Already up to date
            else:
                self.log(f"  ⚠ WiFi firmware needs update to {self.firmware_version}")
                return False  # Needs update

    def update(self):
        """Execute complete firmware update process"""
        self.log("=" * 60)
        self.log("DAQiFi WiFi Firmware Update Tool")
        self.log("=" * 60)
        self.log(f"\nPort: {self.port}")
        self.log(f"Target Firmware: {self.firmware_version}")
        self.log(f"Flash Tool: {self.tool_path or 'NOT FOUND'}")

        # Pre-check: Connect and check current version
        if not self.connect():
            return False

        try:
            already_updated = self.check_current_version()
            if already_updated:
                # Already at target version, no update needed
                self.disconnect()
                self.log("\n" + "=" * 60)
                self.log("✓ NO UPDATE REQUIRED - Already at target version")
                self.log("=" * 60)
                return True
        except Exception as e:
            self.log(f"\n⚠ Warning during version check: {e}")
            self.log("  Proceeding with update...")

        # Step 1: Prepare for update
        try:
            self.prepare_for_update()
        except Exception as e:
            self.log(f"\n✗ ERROR in Step 1: {e}")
            self.disconnect()
            return False

        # Step 2: Disconnect and flash
        self.disconnect()

        flash_success = self.run_flash_tool()

        # Wait before reconnecting (regardless of flash result)
        self.log("\nWaiting 5 seconds before reconnecting...")
        time.sleep(5)

        # Step 3: ALWAYS reconnect and restore normal mode (even if flash failed)
        if not self.connect():
            self.log("\n✗ ERROR: Could not reconnect to device")
            self.log("⚠ WARNING: Device may still be in firmware update mode!")
            return False

        try:
            # Always restore normal operation, even if flash failed
            self.restore_normal_operation()

            # Only verify if flash was successful
            if flash_success:
                verified = self.verify_update()
            else:
                self.log("\n✗ Firmware flash FAILED - but device restored to normal mode")
                verified = False

        except Exception as e:
            self.log(f"\n✗ ERROR in Step 3/4: {e}")
            # Try to at least disable transparent mode before exiting
            try:
                self.log("\nAttempting emergency restore to normal mode...")
                self.send_command("SYSTem:USB:SetTransparentMode 0", delay=0.5)
            except:
                pass
            self.disconnect()
            return False
        finally:
            self.disconnect()

        # Summary
        self.log("\n" + "=" * 60)
        if verified:
            self.log("✓ WiFi FIRMWARE UPDATE COMPLETE")
        else:
            self.log("✗ WiFi firmware update FAILED (device restored to normal mode)")
        self.log("=" * 60)

        return verified


def main():
    parser = argparse.ArgumentParser(
        description='Update WiFi module firmware on DAQiFi Nyquist',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  python update_wifi_firmware.py COM3
  python update_wifi_firmware.py COM3 --firmware-version 19.7.7
  python update_wifi_firmware.py COM3 --tool-path "C:\\tools\\winc_flash_tool.cmd"

Manual Process (for reference):
  1. Connect and send:
     SYST:POW:STAT 1
     SYST:COMM:LAN:FWUpdate
     SYST:COMM:LAN:APPLY

  2. Disconnect and run:
     winc_flash_tool.cmd /p COM3 /d WINC1500 /v 19.7.7 /x /e /i aio /w

  3. Reconnect and send:
     SYST:USB:SetTransparentMode 0
     SYST:COMM:LAN:ENabled 1
     SYST:COMM:LAN:APPLY
        '''
    )

    parser.add_argument('port', help='Serial port (e.g., COM3)')
    parser.add_argument('--firmware-version', default='19.7.7',
                        help='WiFi firmware version to flash (default: 19.7.7)')
    parser.add_argument('--tool-path',
                        help='Path to winc_flash_tool.cmd (auto-detected if not specified)')

    args = parser.parse_args()

    updater = WiFiFirmwareUpdater(args.port, args.firmware_version, args.tool_path)
    success = updater.update()

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
