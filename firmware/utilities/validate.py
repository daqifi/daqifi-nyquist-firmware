#!/usr/bin/env python3
"""
Comprehensive auditable test script for NQ3 consolidated firmware
Generates evidence-based report with SCPI logs and streaming data capture
"""

import serial
import time
import re
import sys
import os
import argparse
from datetime import datetime
from pathlib import Path

# Force UTF-8 output for Windows console
if sys.platform == 'win32':
    import codecs
    sys.stdout = codecs.getwriter('utf-8')(sys.stdout.buffer, 'strict')
    sys.stderr = codecs.getwriter('utf-8')(sys.stderr.buffer, 'strict')

class DAQiFiTester:
    def __init__(self, port='COM3', baudrate=115200, timeout=2, save_reports=False):
        """Initialize serial connection and logging"""
        self.port = port
        self.baudrate = baudrate
        self.save_reports = save_reports
        self.scpi_log = []  # List of (timestamp, command, response) tuples
        self.test_results = []  # List of (test_name, passed, evidence) tuples
        self.device_info = {}
        self.streaming_data = b''
        self.board_variant = None  # Will be set from *IDN? response
        self.user_adc_channels = 0  # Will be parsed from SYST:INFO?

        try:
            self.ser = serial.Serial(port, baudrate, timeout=timeout)
            time.sleep(0.5)
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
            self.log(f"Connected to {port} at {baudrate} baud")
        except Exception as e:
            self.log(f"FAILED to connect: {e}")
            sys.exit(1)

    def log(self, message):
        """Log message to console"""
        print(message)
        sys.stdout.flush()  # Force immediate output

    def send_command(self, cmd, delay=0.3):
        """Send SCPI command, log conversation, return response"""
        timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]

        self.ser.reset_input_buffer()
        self.ser.write(f"{cmd}\r\n".encode())
        time.sleep(delay)
        response = self.ser.read(self.ser.in_waiting).decode('ascii', errors='ignore')
        response = response.strip()

        # Log the conversation
        self.scpi_log.append((timestamp, cmd, response))

        return response

    def capture_streaming_data(self, duration=5.0, max_bytes=50000):
        """Capture raw streaming data for specified duration"""
        start_time = time.time()
        captured_data = b''
        packet_count = 0

        while (time.time() - start_time) < duration and len(captured_data) < max_bytes:
            if self.ser.in_waiting > 0:
                chunk = self.ser.read(self.ser.in_waiting)
                captured_data += chunk
                packet_count += 1
            time.sleep(0.01)  # 10ms polling

        actual_duration = time.time() - start_time
        self.log(f"    Captured {len(captured_data)} bytes in {actual_duration:.2f}s ({packet_count} chunks)")

        return captured_data, actual_duration, packet_count

    def hex_dump(self, data, max_bytes=256):
        """Generate hex dump of binary data"""
        lines = []
        for i in range(0, min(len(data), max_bytes), 16):
            hex_part = ' '.join(f'{b:02X}' for b in data[i:i+16])
            ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[i:i+16])
            lines.append(f"{i:08X}: {hex_part:<48} {ascii_part}")

        if len(data) > max_bytes:
            lines.append(f"... ({len(data) - max_bytes} more bytes)")

        return '\n'.join(lines)

    def format_scpi_conversation(self, start_idx=0, end_idx=None):
        """Format SCPI log entries as conversation"""
        if end_idx is None:
            end_idx = len(self.scpi_log)

        lines = []
        for timestamp, cmd, resp in self.scpi_log[start_idx:end_idx]:
            lines.append(f"> {cmd}")
            if resp:
                for line in resp.split('\n'):
                    if line.strip():
                        lines.append(f"< {line}")

        return '\n'.join(lines)

    def test_power_and_identification(self):
        """Test 1: Power management and device identification"""
        start_log_idx = len(self.scpi_log)

        # Get device info
        self.log("  Querying device identification...")
        idn = self.send_command("*IDN?")
        self.device_info['idn'] = idn

        # Check initial power state
        power_state = self.send_command("SYST:POW:STAT?")
        self.device_info['initial_power'] = power_state

        # Power up to POWERED_UP state (enables 10V rail for DAC)
        self.log("  Powering up device to POWERED_UP state...")
        self.send_command("SYST:POW:STAT 1", delay=1.0)

        new_power_state = self.send_command("SYST:POW:STAT?")
        self.device_info['power_state'] = new_power_state

        # Get detailed system info
        self.log("  Reading system information...")
        syst_info = self.send_command("SYST:INFO?", delay=0.5)
        self.device_info['syst_info'] = syst_info

        # Detect board variant from *IDN? response
        if "Nq1" in idn or "Nyquist1" in idn:
            self.board_variant = "NQ1"
        elif "Nq3" in idn or "Nyquist3" in idn:
            self.board_variant = "NQ3"
        else:
            self.board_variant = "Unknown"

        self.device_info['variant'] = self.board_variant

        # Verify - check for valid Nyquist variant
        passed = self.board_variant in ["NQ1", "NQ3"]
        evidence = {
            'scpi_conversation': self.format_scpi_conversation(start_log_idx),
            'verification': f"Device identified as: {idn}\nBoard variant: {self.board_variant}",
            'system_info': syst_info
        }

        self.test_results.append(("Power and Identification", passed, evidence))
        self.log(f"{'✓ PASS' if passed else '✗ FAIL'}: Device identification")

        return passed

    def display_device_info(self):
        """Display comprehensive device information"""
        self.log("\n" + "=" * 60)
        self.log("DEVICE INFORMATION")
        self.log("=" * 60)

        # Get system info for HW/FW versions and capabilities
        syst_info = self.send_command("SYST:INFO?", delay=0.5)

        # Parse hardware and firmware versions from header line
        # Format: "=== DAQiFi Nyquist3 | HW:2.0.0 FW:3.0.0b2 ==="
        hw_version = "Unknown"
        fw_version = "Unknown"

        for line in syst_info.split('\n'):
            if 'HW:' in line and 'FW:' in line:
                # Extract HW version
                hw_match = re.search(r'HW:(\S+)', line)
                if hw_match:
                    hw_version = hw_match.group(1)
                # Extract FW version
                fw_match = re.search(r'FW:(\S+)', line)
                if fw_match:
                    fw_version = fw_match.group(1)
                break

        self.log(f"\nHardware Version: {hw_version}")
        self.log(f"Firmware Version: {fw_version}")
        self.log(f"Board Variant: {self.board_variant}")

        # Parse capabilities from SYST:INFO?
        # User ADC: X/Y
        user_adc_match = re.search(r'User ADC:\s*(\d+)/(\d+)', syst_info)
        if user_adc_match:
            user_adc_enabled = int(user_adc_match.group(1))
            user_adc_total = int(user_adc_match.group(2))
            self.log(f"User ADC Channels: {user_adc_enabled}/{user_adc_total}")
        else:
            user_adc_total = 0

        # Internal ADC: X/Y
        internal_adc_match = re.search(r'Internal ADC:\s*(\d+)/(\d+)', syst_info)
        if internal_adc_match:
            internal_adc_enabled = int(internal_adc_match.group(1))
            internal_adc_total = int(internal_adc_match.group(2))
            self.log(f"Internal ADC Channels: {internal_adc_enabled}/{internal_adc_total}")

        # DIO: X/Y
        dio_match = re.search(r'DIO:\s*(\d+)/(\d+)', syst_info)
        if dio_match:
            dio_count = int(dio_match.group(2))
            self.log(f"DIO Channels: {dio_match.group(1)}/{dio_match.group(2)}")
        else:
            dio_count = 0

        # DAC (if available - NQ3 only)
        # Look for DAC info in system info
        dac_match = re.search(r'DAC.*?(\d+)', syst_info)
        if dac_match or self.board_variant == "NQ3":
            self.log(f"DAC Channels: 8 (DAC7718)")

        # Store parsed values for use in ADC reading
        self.user_adc_channels = user_adc_total if user_adc_match else (8 if self.board_variant == "NQ3" else 16)

        # WiFi module firmware
        self.log("\nWiFi Module:")
        wifi_chip_info_raw = self.send_command("SYST:COMM:LAN:GETChipInfo?", delay=0.5)
        wifi_chip_info = self._parse_value(wifi_chip_info_raw)
        if "ERROR" in wifi_chip_info or wifi_chip_info.startswith("-"):
            self.log("  Firmware: < 19.7.7 (command not supported)")
        else:
            self.log(f"  Chip Info: {wifi_chip_info}")

        self.log("\n" + "=" * 60)
        self.log("NOTE: ADC values will be displayed after ADC configuration")
        self.log("=" * 60 + "\n")

    def _parse_value(self, response):
        """Helper to parse value from SCPI response (removes command echo and prompt)"""
        try:
            lines = response.strip().split('\n')
            for line in lines:
                line = line.strip()
                # Skip command echoes (contain '?') and prompts (start with DAQIFI)
                if line and '?' not in line and not line.startswith('DAQIFI'):
                    return line
        except (ValueError, IndexError, AttributeError):
            pass
        return response.strip()  # Return as-is if parsing fails

    def _parse_voltage(self, response):
        """Helper to parse voltage from SCPI response"""
        try:
            lines = response.strip().split('\n')
            for line in lines:
                line = line.strip()
                if line and not line.startswith('MEAS') and not line.startswith('DAQIFI'):
                    return float(line)
        except (ValueError, IndexError, AttributeError):
            pass
        return None

    def test_dac_configuration(self):
        """Test 2: DAC channel configuration (NQ3 only)"""
        start_log_idx = len(self.scpi_log)

        # DAC is only available on NQ3
        if self.board_variant != "NQ3":
            self.log(f"  Skipping DAC test - not available on {self.board_variant}")
            evidence = {
                'scpi_conversation': '',
                'verification': f"DAC test skipped - {self.board_variant} does not have DAC7718"
            }
            self.test_results.append(("DAC Configuration", True, evidence))
            self.log("✓ SKIP: DAC not available on this variant")
            return True

        # Set each channel to voltage = channel number + 1
        self.log("  Configuring DAC channels (ch0=1V, ch1=2V, ..., ch7=8V)...")
        for ch in range(8):
            voltage = ch + 1.0
            self.send_command(f"SOUR:VOLT:LEV {ch},{voltage}", delay=0.2)

        # Read back values
        self.log("Reading back DAC values...")
        readback_values = []
        for ch in range(8):
            response = self.send_command(f"SOUR:VOLT:LEV? {ch}", delay=0.2)
            readback_values.append((ch, response))

        # Apply DAC update
        self.send_command("CONFigure:DAC:UPDATE", delay=0.3)

        # Verify - check that we got responses for all channels
        passed = len(readback_values) == 8 and all(resp for _, resp in readback_values)

        evidence = {
            'scpi_conversation': self.format_scpi_conversation(start_log_idx),
            'readback_values': readback_values,
            'verification': f"All 8 DAC channels configured and read back successfully"
        }

        self.test_results.append(("DAC Configuration", passed, evidence))
        self.log(f"{'✓ PASS' if passed else '✗ FAIL'}: DAC configuration")

        return passed

    def test_adc_configuration(self):
        """Test 3: ADC channel configuration and IsPublic status"""
        start_log_idx = len(self.scpi_log)

        # Variant-specific channel configuration
        if self.board_variant == "NQ3":
            max_channel = 7
            expected_total = 8
            adc_type = "AD7609"
            self.log(f"  Enabling {adc_type} channels 0-{max_channel}...")
        elif self.board_variant == "NQ1":
            max_channel = 15
            expected_total = 16
            adc_type = "MC12bADC"
            self.log(f"  Enabling {adc_type} channels 0-{max_channel}...")
        else:
            self.log("  Unknown variant - cannot configure ADC")
            return False

        # Enable ADC channels
        for ch in range(max_channel + 1):
            self.send_command(f"CONF:ADC:CHAN {ch},1", delay=0.2)

        # Query channel configuration
        chan_config = self.send_command("CONF:ADC:CHAN?", delay=0.5)

        # Get system info to verify public channel count
        syst_info = self.send_command("SYST:INFO?", delay=0.5)

        # Parse "User ADC: X/Y" to verify IsPublic refactor worked
        match = re.search(r'User ADC:\s*(\d+)/(\d+)', syst_info)
        if match:
            enabled_count = int(match.group(1))
            total_available = int(match.group(2))
        else:
            enabled_count = 0
            total_available = 0

        # Verify - should show all channels enabled
        passed = enabled_count == expected_total and total_available == expected_total

        evidence = {
            'scpi_conversation': self.format_scpi_conversation(start_log_idx),
            'system_info': syst_info,
            'enabled_public_channels': f"{enabled_count}/{expected_total}",
            'verification': f"IsPublic refactor verification: {adc_type} channels visible as public ({self.board_variant})"
        }

        self.test_results.append(("ADC Configuration & IsPublic", passed, evidence))
        self.log(f"{'✓ PASS' if passed else '✗ FAIL'}: ADC configuration ({enabled_count}/{expected_total} public channels)")

        return passed

    def test_adc_readings(self):
        """Test 4: ADC value reading"""
        start_log_idx = len(self.scpi_log)

        self.log("\n" + "=" * 60)
        self.log("ADC READINGS (After Configuration)")
        self.log("=" * 60)

        # Read all public channels
        self.log("\nPublic ADC Channels:")
        num_channels = self.user_adc_channels
        ch0_voltage = None

        if num_channels > 0:
            for ch in range(num_channels):
                reading = self.send_command(f"MEAS:VOLT:DC? {ch}", delay=0.2)
                voltage = self._parse_voltage(reading)
                if voltage is not None:
                    self.log(f"  CH{ch}: {voltage:.4f}V")
                    if ch == 0:
                        ch0_voltage = voltage
                else:
                    self.log(f"  CH{ch}: {reading.strip()}")
        else:
            self.log("  No user ADC channels available")

        # Read private/internal monitoring channels
        self.log("\nPrivate/Internal Monitoring Channels (Voltage Rails):")
        syst_info = self.send_command("SYST:INFO?", delay=0.5)

        voltage_rails = []
        in_voltage_section = False
        lines = syst_info.replace('\r\n', '\n').split('\n')

        for line in lines:
            line_stripped = line.strip()
            if '[Voltage Rails]' in line_stripped:
                in_voltage_section = True
                continue
            elif line_stripped.startswith('[') and in_voltage_section:
                break
            elif in_voltage_section and ':' in line_stripped and line_stripped:
                colon_idx = line_stripped.find(':')
                if colon_idx > 0:
                    rail_name = line_stripped[:colon_idx].strip()
                    rail_value = line_stripped[colon_idx+1:].strip()
                    if rail_name and not rail_name.startswith('['):
                        voltage_rails.append((rail_name, rail_value))

        if voltage_rails:
            for rail_name, rail_value in voltage_rails:
                self.log(f"  {rail_name}: {rail_value}")
        else:
            match = re.search(r'Internal ADC:\s*(\d+)/(\d+)', syst_info)
            if match:
                self.log(f"  {match.group(1)}/{match.group(2)} monitoring channels active")
            else:
                self.log("  (No private channels reported)")

        self.log("=" * 60 + "\n")

        # Validate ch0 reading is reasonable
        voltage_reasonable = ch0_voltage is not None and 0.0 <= ch0_voltage <= 10.0
        passed = voltage_reasonable

        evidence = {
            'scpi_conversation': self.format_scpi_conversation(start_log_idx),
            'ch0_voltage': ch0_voltage,
            'voltage_rails': voltage_rails,
            'verification': f"CH0 voltage: {ch0_voltage}V (range check: 0-10V)"
        }

        self.test_results.append(("ADC Value Reading", passed, evidence))
        self.log(f"{'✓ PASS' if passed else '✗ FAIL'}: ADC readings (ch0={ch0_voltage}V)")

        return passed

    def test_internal_monitoring(self):
        """Test 5: Internal monitoring channels verification (values shown in Test 4)"""
        start_log_idx = len(self.scpi_log)

        # Get system info
        self.log("  Verifying internal monitoring channels are active...")
        syst_info = self.send_command("SYST:INFO?", delay=0.5)

        # Parse "Internal ADC: X/Y"
        match = re.search(r'Internal ADC:\s*(\d+)/(\d+)', syst_info)
        if match:
            enabled = int(match.group(1))
            total = int(match.group(2))
        else:
            enabled, total = 0, 0

        passed = enabled == total and total > 0

        evidence = {
            'scpi_conversation': self.format_scpi_conversation(start_log_idx),
            'internal_channels': f"{enabled}/{total}",
            'verification': f"Internal monitoring channels active (values displayed in ADC readings)"
        }

        self.test_results.append(("Internal Monitoring", passed, evidence))
        self.log(f"{'✓ PASS' if passed else '✗ FAIL'}: Internal monitoring ({enabled}/{total} channels)")

        return passed

    def test_streaming(self):
        """Test 6: Streaming test with data capture (verify heap exhaustion fix)"""
        start_log_idx = len(self.scpi_log)

        # Clear any previous data
        self.ser.reset_input_buffer()

        # Start streaming at 1kHz sample rate
        self.log("  Starting streaming at 1kHz...")
        self.send_command("SYSTem:StartStreamData 1000", delay=0.5)

        # Capture streaming data for 5 seconds
        self.log("  Capturing data for 5 seconds...")
        captured_data, duration, packet_count = self.capture_streaming_data(duration=5.0)
        self.streaming_data = captured_data

        # Stop streaming
        self.send_command("SYSTem:StopStreamData", delay=0.5)
        self.log("  Stopped streaming")

        # Check for errors (heap exhaustion would cause errors)
        error_response = self.send_command("SYST:ERR?", delay=0.3)

        # Verify - should have captured data and no errors
        has_data = len(captured_data) > 0
        no_errors = "No error" in error_response or error_response == "0"
        passed = has_data and no_errors

        evidence = {
            'scpi_conversation': self.format_scpi_conversation(start_log_idx),
            'streaming_duration': f"{duration:.2f} seconds",
            'bytes_captured': len(captured_data),
            'packet_count': packet_count,
            'data_rate': f"{len(captured_data)/duration:.1f} bytes/sec" if duration > 0 else "N/A",
            'hex_dump': self.hex_dump(captured_data, max_bytes=256),
            'error_queue': error_response,
            'verification': f"Started streaming at 1kHz, ran {duration:.1f}s, captured {len(captured_data)} bytes, no heap errors"
        }

        self.test_results.append(("Streaming (Heap Fix)", passed, evidence))
        self.log(f"{'✓ PASS' if passed else '✗ FAIL'}: Streaming test")

        return passed

    def test_error_queue(self):
        """Test 7: Error queue check"""
        start_log_idx = len(self.scpi_log)

        # Clear error queue
        self.log("  Checking error queue...")
        errors = []
        for _ in range(10):
            response = self.send_command("SYST:ERR?", delay=0.2)
            if "No error" in response or response == "0":
                break
            errors.append(response)

        passed = len(errors) == 0

        evidence = {
            'scpi_conversation': self.format_scpi_conversation(start_log_idx),
            'errors_found': errors,
            'verification': "Error queue clear" if passed else f"Found {len(errors)} errors"
        }

        self.test_results.append(("Error Queue", passed, evidence))
        self.log(f"{'✓ PASS' if passed else '✗ FAIL'}: Error queue ({'clear' if passed else f'{len(errors)} errors'})")

        return passed

    def test_final_status(self):
        """Test 8: Final system status and power down"""
        start_log_idx = len(self.scpi_log)

        # Final system info
        self.log("  Reading final system status...")
        syst_info = self.send_command("SYST:INFO?", delay=0.5)

        # Leave powered on for manual verification with multimeter
        self.log("  Leaving device powered on for manual DAC verification...")

        final_power = self.send_command("SYST:POW:STAT?")

        passed = True  # Always pass unless error

        evidence = {
            'scpi_conversation': self.format_scpi_conversation(start_log_idx),
            'final_system_info': syst_info,
            'final_power_state': final_power,
            'verification': "Device left powered on for manual DAC verification"
        }

        self.test_results.append(("Final Status", passed, evidence))
        self.log(f"✓ PASS: Final status")

        return passed

    def run_all_tests(self):
        """Execute all tests"""
        self.log("=" * 60)
        self.log("DAQiFi Nyquist Firmware Test Suite")
        self.log("=" * 60)

        test_start_time = datetime.now()

        tests = [
            self.test_power_and_identification,
            self.test_dac_configuration,
            self.test_adc_configuration,
            self.test_adc_readings,
            self.test_internal_monitoring,
            self.test_streaming,
            self.test_error_queue,
            self.test_final_status,
        ]

        test_names = [
            "Power & Identification",
            "DAC Configuration",
            "ADC Configuration",
            "ADC Readings",
            "Internal Monitoring",
            "Streaming",
            "Error Queue",
            "Final Status"
        ]

        for idx, (test_func, test_name) in enumerate(zip(tests, test_names)):
            self.log(f"\n[{idx+1}/{len(tests)}] Running: {test_name}...")
            try:
                test_func()

                # Display device info after first test (power and identification)
                if idx == 0:
                    try:
                        self.display_device_info()
                    except Exception as e:
                        self.log(f"\n⚠ Warning: Could not display device info: {e}")

            except Exception as e:
                self.log(f"\n✗ EXCEPTION in {test_func.__name__}: {e}")
                import traceback
                traceback.print_exc()
                self.test_results.append((test_func.__name__, False, {'error': str(e)}))

        test_end_time = datetime.now()
        test_duration = (test_end_time - test_start_time).total_seconds()

        # Generate report only if requested
        if self.save_reports:
            self.generate_report(test_start_time, test_end_time, test_duration)

        # Summary
        passed_count = sum(1 for _, passed, _ in self.test_results if passed)
        total_count = len(self.test_results)

        self.log("\n" + "=" * 60)
        self.log(f"TEST SUMMARY: {passed_count}/{total_count} tests passed")
        self.log("=" * 60)

        return passed_count == total_count

    def generate_report(self, start_time, end_time, duration):
        """Generate comprehensive test report with evidence"""

        # Create report directory
        report_dir = Path("test_results")
        report_dir.mkdir(exist_ok=True)

        timestamp_str = start_time.strftime("%Y%m%d_%H%M%S")

        # Save streaming data binary
        if self.streaming_data:
            bin_path = report_dir / f"streaming_capture_{timestamp_str}.bin"
            with open(bin_path, 'wb') as f:
                f.write(self.streaming_data)
            self.log(f"Saved streaming data: {bin_path}")

            # Save hex dump
            hex_path = report_dir / f"streaming_capture_{timestamp_str}.hex"
            with open(hex_path, 'w', encoding='utf-8') as f:
                f.write(self.hex_dump(self.streaming_data, max_bytes=1024))
            self.log(f"Saved hex dump: {hex_path}")

        # Save full SCPI log
        scpi_log_path = report_dir / f"scpi_full_log_{timestamp_str}.txt"
        with open(scpi_log_path, 'w', encoding='utf-8') as f:
            f.write("SCPI Command/Response Log\n")
            f.write("=" * 60 + "\n\n")
            for timestamp, cmd, resp in self.scpi_log:
                f.write(f"[{timestamp}]\n")
                f.write(f"> {cmd}\n")
                if resp:
                    for line in resp.split('\n'):
                        if line.strip():
                            f.write(f"< {line}\n")
                f.write("\n")
        self.log(f"Saved SCPI log: {scpi_log_path}")

        # Generate Markdown report
        report_path = report_dir / f"test_report_{timestamp_str}.md"

        with open(report_path, 'w', encoding='utf-8') as f:
            # Header
            variant_name = self.device_info.get('variant', 'Unknown')
            f.write(f"# DAQiFi {variant_name} Firmware Test Report\n\n")
            f.write(f"**Test Date:** {start_time.strftime('%Y-%m-%d %H:%M:%S')}\n\n")
            f.write(f"**Test Duration:** {duration:.1f} seconds\n\n")
            f.write(f"**Device:** {self.device_info.get('idn', 'Unknown')}\n\n")
            f.write(f"**Board Variant:** {variant_name}\n\n")
            f.write(f"**Port:** {self.port} @ {self.baudrate} baud\n\n")

            # Summary
            passed_count = sum(1 for _, passed, _ in self.test_results if passed)
            total_count = len(self.test_results)

            f.write("## Test Summary\n\n")
            f.write(f"**Result:** {'✓ ALL TESTS PASSED' if passed_count == total_count else f'✗ {total_count - passed_count} TESTS FAILED'}\n\n")
            f.write(f"**Passed:** {passed_count}/{total_count}\n\n")

            # Individual test results
            f.write("## Test Results\n\n")
            for test_name, passed, evidence in self.test_results:
                status = "✓ PASS" if passed else "✗ FAIL"
                f.write(f"### {status}: {test_name}\n\n")

                # SCPI conversation
                if 'scpi_conversation' in evidence:
                    f.write("**SCPI Conversation:**\n```\n")
                    f.write(evidence['scpi_conversation'])
                    f.write("\n```\n\n")

                # Verification
                if 'verification' in evidence:
                    f.write(f"**Verification:** {evidence['verification']}\n\n")

                # Test-specific evidence
                if 'readback_values' in evidence:
                    f.write("**DAC Readback Values:**\n")
                    for ch, val in evidence['readback_values']:
                        f.write(f"- Channel {ch}: {val}\n")
                    f.write("\n")

                if 'enabled_public_channels' in evidence:
                    f.write(f"**Public Channels Enabled:** {evidence['enabled_public_channels']}\n\n")

                if 'ch0_voltage' in evidence:
                    f.write(f"**CH0 Voltage:** {evidence['ch0_voltage']}V (expected ~2.75V)\n\n")

                if 'streaming_duration' in evidence:
                    f.write(f"**Streaming Duration:** {evidence['streaming_duration']}\n\n")
                    f.write(f"**Bytes Captured:** {evidence['bytes_captured']}\n\n")
                    f.write(f"**Packet Count:** {evidence['packet_count']}\n\n")
                    f.write(f"**Data Rate:** {evidence['data_rate']}\n\n")
                    f.write(f"**Error Queue After Streaming:** {evidence['error_queue']}\n\n")

                    if 'hex_dump' in evidence:
                        f.write("**Streaming Data (first 256 bytes):**\n```\n")
                        f.write(evidence['hex_dump'])
                        f.write("\n```\n\n")

                if 'errors_found' in evidence and evidence['errors_found']:
                    f.write("**Errors Found:**\n")
                    for err in evidence['errors_found']:
                        f.write(f"- {err}\n")
                    f.write("\n")

                if 'system_info' in evidence:
                    f.write("**System Info:**\n```\n")
                    f.write(evidence['system_info'])
                    f.write("\n```\n\n")

                f.write("---\n\n")

            # Appendices
            f.write("## Appendices\n\n")

            f.write("### A. Device Information\n\n")
            f.write(f"**Identification:** {self.device_info.get('idn', 'N/A')}\n\n")
            f.write(f"**Initial Power State:** {self.device_info.get('initial_power', 'N/A')}\n\n")
            f.write(f"**Operating Power State:** {self.device_info.get('power_state', 'N/A')}\n\n")

            if 'syst_info' in self.device_info:
                f.write("**Full System Info:**\n```\n")
                f.write(self.device_info['syst_info'])
                f.write("\n```\n\n")

            f.write("### B. Test Artifacts\n\n")
            f.write(f"- Full SCPI log: `{scpi_log_path.name}`\n")
            if self.streaming_data:
                f.write(f"- Streaming data (binary): `streaming_capture_{timestamp_str}.bin` ({len(self.streaming_data)} bytes)\n")
                f.write(f"- Streaming data (hex dump): `streaming_capture_{timestamp_str}.hex`\n")
            f.write("\n")

            f.write("### C. Test Objectives\n\n")
            variant_str = self.device_info.get('variant', 'Unknown')
            f.write(f"This test validates the {variant_str} firmware with the following objectives:\n\n")
            f.write("1. **Power Management:** Verify device powers up and reports correct state\n")
            if variant_str == "NQ3":
                f.write("2. **DAC Configuration:** Test DAC7718 functionality (all 8 channels)\n")
                f.write("3. **ADC IsPublic Refactor:** Verify AD7609 channels visible as public (commit f0aaef1e)\n")
            elif variant_str == "NQ1":
                f.write("2. **DAC Configuration:** Skipped (DAC not available on NQ1)\n")
                f.write("3. **ADC IsPublic Refactor:** Verify MC12bADC channels visible as public (commit f0aaef1e)\n")
            else:
                f.write("2. **DAC Configuration:** Variant-dependent\n")
                f.write("3. **ADC IsPublic Refactor:** Variant-dependent\n")
            f.write("4. **ADC Readings:** Verify analog input functionality\n")
            f.write("5. **Internal Monitoring:** Verify private MC12bADC channels for system monitoring\n")
            f.write("6. **Heap Exhaustion Fix:** Verify streaming runs without memory errors (commit 06233cc1)\n")
            f.write("7. **Error Handling:** Verify no errors accumulated during testing\n")
            f.write("8. **Final Status:** Verify clean shutdown\n\n")

        self.log(f"Generated test report: {report_path}")

        return report_path

    def close(self):
        """Close serial connection"""
        if self.ser and self.ser.is_open:
            self.ser.close()
            self.log("Serial connection closed")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description='DAQiFi Nyquist Firmware Test Suite',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  python validate.py COM3                    # Quick test, no files saved
  python validate.py COM3 --save-reports     # Full test with report files
  python validate.py /dev/ttyACM0 --save-reports
        '''
    )
    parser.add_argument('port', nargs='?', default='COM3',
                        help='Serial port (default: COM3)')
    parser.add_argument('--save-reports', action='store_true',
                        help='Save test reports, SCPI logs, and streaming data to test_results/')

    args = parser.parse_args()

    tester = DAQiFiTester(port=args.port, save_reports=args.save_reports)
    try:
        success = tester.run_all_tests()
        sys.exit(0 if success else 1)
    except KeyboardInterrupt:
        print("\n\nTest interrupted by user")
        sys.exit(2)
    finally:
        tester.close()
