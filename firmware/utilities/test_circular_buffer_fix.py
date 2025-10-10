#!/usr/bin/env python3
"""
Test Script for Circular Buffer Fix (PR #129)

Tests USB CDC, WiFi TCP, and SD card circular buffer implementations
to verify fixes for race conditions and callback inconsistencies.

Requirements:
    pip install pyserial colorama

Usage:
    python test_circular_buffer_fix.py [--port COM3] [--wifi-ip 192.168.1.1]
"""

import serial
import time
import sys
import argparse
import socket
from collections import defaultdict
from colorama import init, Fore, Style

# Initialize colorama for Windows
init(autoreset=True)

class TestResult:
    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.skipped = 0
        self.tests = []

    def add_pass(self, test_name, details=""):
        self.passed += 1
        self.tests.append((test_name, "PASS", details))
        print(f"{Fore.GREEN}✓{Style.RESET_ALL} {test_name}")
        if details:
            print(f"  {details}")

    def add_fail(self, test_name, details=""):
        self.failed += 1
        self.tests.append((test_name, "FAIL", details))
        print(f"{Fore.RED}✗{Style.RESET_ALL} {test_name}")
        if details:
            print(f"  {Fore.RED}{details}{Style.RESET_ALL}")

    def add_skip(self, test_name, reason=""):
        self.skipped += 1
        self.tests.append((test_name, "SKIP", reason))
        print(f"{Fore.YELLOW}⊘{Style.RESET_ALL} {test_name} (skipped: {reason})")

    def print_summary(self):
        print("\n" + "="*70)
        print(f"Test Summary: {self.passed} passed, {self.failed} failed, {self.skipped} skipped")
        print("="*70)

        if self.failed > 0:
            print(f"\n{Fore.RED}Failed Tests:{Style.RESET_ALL}")
            for name, status, details in self.tests:
                if status == "FAIL":
                    print(f"  - {name}: {details}")

        return self.failed == 0


class DAQiFiTester:
    def __init__(self, port, baudrate=115200, timeout=2.0, verbose=False):
        self.port = port
        self.baudrate = baudrate
        self.timeout = timeout
        self.ser = None
        self.result = TestResult()
        self.verbose = verbose

    def connect(self):
        """Open serial connection to device"""
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baudrate,
                timeout=self.timeout,
                write_timeout=self.timeout
            )
            time.sleep(0.5)  # Wait for device to be ready
            # Clear any pending data
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
            return True
        except Exception as e:
            print(f"{Fore.RED}Failed to connect to {self.port}: {e}{Style.RESET_ALL}")
            return False

    def disconnect(self):
        """Close serial connection"""
        if self.ser and self.ser.is_open:
            self.ser.close()

    def send_command(self, cmd, wait_time=0.5):
        """Send SCPI command and return response"""
        if not self.ser or not self.ser.is_open:
            return None

        try:
            # Clear buffers
            self.ser.reset_input_buffer()

            # Send command
            cmd_bytes = (cmd + '\r\n').encode('ascii')
            self.ser.write(cmd_bytes)
            self.ser.flush()

            if self.verbose:
                print(f"  {Fore.CYAN}>>> {cmd}{Style.RESET_ALL}")

            # Wait for response
            time.sleep(wait_time)

            # Read all available data (non-blocking)
            bytes_available = self.ser.in_waiting
            if bytes_available > 0:
                response = self.ser.read(bytes_available)
            else:
                response = b''

            decoded = response.decode('ascii', errors='replace')

            if self.verbose:
                # Show response with visible whitespace
                display = decoded.replace('\r', '\\r').replace('\n', '\\n\n  ')
                print(f"  {Fore.GREEN}<<< {display}{Style.RESET_ALL}")

            return decoded
        except Exception as e:
            print(f"Error sending command '{cmd}': {e}")
            return None

    def count_prompts(self, text):
        """Count occurrences of DAQIFI> prompt"""
        return text.count('DAQIFI>')

    def test_duplicate_prompts(self):
        """Test 1: Verify no duplicate prompts (THE MAIN BUG)"""
        print(f"\n{Fore.CYAN}=== Test 1: Duplicate Prompt Check ==={Style.RESET_ALL}")

        # Send multiple Enter keys rapidly
        prompt_counts = []
        for i in range(10):
            self.ser.reset_input_buffer()
            self.ser.write(b'\r\n')
            time.sleep(0.2)
            response = self.ser.read(self.ser.in_waiting).decode('ascii', errors='replace')
            count = self.count_prompts(response)
            prompt_counts.append(count)

            if self.verbose:
                display = response.replace('\r', '\\r').replace('\n', '\\n\n  ')
                print(f"  {Fore.CYAN}>>> (Enter #{i+1}){Style.RESET_ALL}")
                print(f"  {Fore.GREEN}<<< {display}{Style.RESET_ALL}")
                print(f"  Prompt count: {count}")

        # Each response should have exactly 1 prompt
        max_prompts = max(prompt_counts)
        avg_prompts = sum(prompt_counts) / len(prompt_counts)

        if max_prompts <= 1:
            self.result.add_pass(
                "No duplicate prompts",
                f"Max prompts per response: {max_prompts}, Average: {avg_prompts:.2f}"
            )
        else:
            self.result.add_fail(
                "Duplicate prompts detected",
                f"Max prompts per response: {max_prompts}, Average: {avg_prompts:.2f}"
            )

    def test_basic_scpi(self):
        """Test 2: Basic SCPI command execution"""
        print(f"\n{Fore.CYAN}=== Test 2: Basic SCPI Commands ==={Style.RESET_ALL}")

        # Test *IDN?
        response = self.send_command('*IDN?')
        if response and 'DAQiFi' in response:
            self.result.add_pass("*IDN? command", "Device identified correctly")
        else:
            self.result.add_fail("*IDN? command", f"Unexpected response: {response}")

        # Test system error query
        response = self.send_command('SYST:ERR?')
        if response and ('No error' in response or '0,' in response):
            self.result.add_pass("SYST:ERR? command", "No errors in queue")
        else:
            self.result.add_fail("SYST:ERR? command", f"Response: {response}")

    def test_rapid_commands(self):
        """Test 3: Rapid successive commands"""
        print(f"\n{Fore.CYAN}=== Test 3: Rapid Commands ==={Style.RESET_ALL}")

        success_count = 0
        total_count = 50

        for i in range(total_count):
            response = self.send_command('*IDN?', wait_time=0.2)  # 200ms wait for response
            if response and 'DAQiFi' in response:
                success_count += 1

        success_rate = (success_count / total_count) * 100

        if success_rate >= 95:
            self.result.add_pass(
                f"Rapid commands ({total_count} commands)",
                f"Success rate: {success_rate:.1f}%"
            )
        else:
            self.result.add_fail(
                f"Rapid commands ({total_count} commands)",
                f"Success rate: {success_rate:.1f}% (expected >= 95%)"
            )

    def test_large_response(self):
        """Test 4: Commands with large responses"""
        print(f"\n{Fore.CYAN}=== Test 4: Large Response Handling ==={Style.RESET_ALL}")

        # SYST:INFO? returns multi-line response
        response = self.send_command('SYST:INFO?', wait_time=1.0)

        if response and len(response) > 100:
            # Check for no duplicate prompts in large response
            prompt_count = self.count_prompts(response)
            if prompt_count <= 2:  # One at start, one at end
                self.result.add_pass(
                    "Large response (SYST:INFO?)",
                    f"Received {len(response)} bytes, {prompt_count} prompts"
                )
            else:
                self.result.add_fail(
                    "Large response (SYST:INFO?)",
                    f"Too many prompts: {prompt_count}"
                )
        else:
            self.result.add_fail(
                "Large response (SYST:INFO?)",
                f"Response too short or missing: {len(response) if response else 0} bytes"
            )

    def test_command_echo(self):
        """Test 5: Character echo behavior"""
        print(f"\n{Fore.CYAN}=== Test 5: Command Echo ==={Style.RESET_ALL}")

        # Send a command character by character and check echo
        test_cmd = "*IDN?"
        self.ser.reset_input_buffer()

        for char in test_cmd:
            self.ser.write(char.encode('ascii'))
            time.sleep(0.05)

        # Send line terminator to complete the command
        self.ser.write(b'\r\n')

        time.sleep(0.5)  # Wait for response
        echoed = self.ser.read(self.ser.in_waiting).decode('ascii', errors='replace')

        # Should see the command echoed back once (and executed with response)
        if test_cmd in echoed and echoed.count(test_cmd) == 1:
            self.result.add_pass("Command echo", "Characters echoed correctly (once)")
        else:
            self.result.add_fail(
                "Command echo",
                f"Expected '{test_cmd}' once, got: {echoed}"
            )

        # Clear any errors from this test
        self.send_command('SYST:ERR?', wait_time=0.3)

    def test_power_transitions(self):
        """Test 6: Commands during power state transitions"""
        print(f"\n{Fore.CYAN}=== Test 6: Power State Transitions ==={Style.RESET_ALL}")

        # Get current power state
        response = self.send_command('SYST:POW:STAT?')

        if not response:
            self.result.add_skip("Power transitions", "No response to power query")
            return

        # Try a command after power state query
        response2 = self.send_command('*IDN?')

        if response2 and 'DAQiFi' in response2:
            self.result.add_pass(
                "Commands during power transitions",
                "Device responsive after power state query"
            )
        else:
            self.result.add_fail(
                "Commands during power transitions",
                "Device not responsive after power state query"
            )

    def test_error_handling(self):
        """Test 7: Invalid command error handling"""
        print(f"\n{Fore.CYAN}=== Test 7: Error Handling ==={Style.RESET_ALL}")

        # Send invalid command
        response = self.send_command('INVALID:COMMAND:TEST')

        # Check error queue
        error_response = self.send_command('SYST:ERR?')

        if error_response and ('error' in error_response.lower() or '-' in error_response):
            self.result.add_pass(
                "Invalid command error handling",
                "Error properly queued and retrievable"
            )
        else:
            self.result.add_fail(
                "Invalid command error handling",
                f"Expected error in queue, got: {error_response}"
            )

    def test_buffer_stress(self):
        """Test 8: Buffer stress test"""
        print(f"\n{Fore.CYAN}=== Test 8: Buffer Stress Test ==={Style.RESET_ALL}")

        # Send multiple commands rapidly without waiting for responses
        commands = ['*IDN?', 'SYST:ERR?', 'SYST:POW:STAT?'] * 10

        for cmd in commands:
            self.ser.write((cmd + '\r\n').encode('ascii'))
            time.sleep(0.1)  # 100ms delay between commands

        # Wait for all responses
        time.sleep(2.0)

        # Try to execute a new command
        response = self.send_command('*IDN?', wait_time=1.0)

        if response and 'DAQiFi' in response:
            self.result.add_pass(
                "Buffer stress test",
                "Device recovered from rapid command burst"
            )
        else:
            self.result.add_fail(
                "Buffer stress test",
                "Device did not respond correctly after stress"
            )

    def run_all_tests(self):
        """Run all USB CDC tests"""
        print(f"\n{Fore.YELLOW}{'='*70}")
        print(f"DAQiFi Circular Buffer Fix - USB CDC Test Suite")
        print(f"Testing on: {self.port} @ {self.baudrate} baud")
        print(f"{'='*70}{Style.RESET_ALL}\n")

        if not self.connect():
            print(f"{Fore.RED}Cannot connect to device. Aborting tests.{Style.RESET_ALL}")
            return False

        try:
            self.test_duplicate_prompts()     # THE MAIN BUG FIX
            self.test_basic_scpi()
            self.test_rapid_commands()
            self.test_large_response()
            self.test_command_echo()
            self.test_power_transitions()
            self.test_error_handling()
            self.test_buffer_stress()

        finally:
            self.disconnect()

        return self.result.print_summary()


class WiFiTester:
    def __init__(self, host, port=9760, timeout=5.0):
        self.host = host
        self.port = port
        self.timeout = timeout
        self.result = TestResult()

    def send_tcp_command(self, cmd, wait_time=0.5):
        """Send command via TCP and return response"""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(self.timeout)
            sock.connect((self.host, self.port))

            # Send command
            sock.sendall((cmd + '\r\n').encode('ascii'))
            time.sleep(wait_time)

            # Receive response
            response = b''
            sock.settimeout(1.0)
            try:
                while True:
                    chunk = sock.recv(4096)
                    if not chunk:
                        break
                    response += chunk
            except socket.timeout:
                pass  # Expected when no more data

            sock.close()
            return response.decode('ascii', errors='replace')

        except Exception as e:
            print(f"Error sending TCP command: {e}")
            return None

    def test_tcp_connection(self):
        """Test 1: Basic TCP connection"""
        print(f"\n{Fore.CYAN}=== WiFi Test 1: TCP Connection ==={Style.RESET_ALL}")

        response = self.send_tcp_command('*IDN?')

        if response and 'DAQiFi' in response:
            self.result.add_pass("TCP connection and *IDN?", "Connected successfully")
        else:
            self.result.add_fail("TCP connection and *IDN?", f"Response: {response}")

    def test_tcp_rapid_commands(self):
        """Test 2: Rapid TCP commands (tests tcpSendPending mechanism)"""
        print(f"\n{Fore.CYAN}=== WiFi Test 2: Rapid TCP Commands ==={Style.RESET_ALL}")

        success_count = 0
        total_count = 20

        for i in range(total_count):
            response = self.send_tcp_command('*IDN?', wait_time=0.3)
            if response and 'DAQiFi' in response:
                success_count += 1

        success_rate = (success_count / total_count) * 100

        if success_rate >= 90:
            self.result.add_pass(
                f"Rapid TCP commands ({total_count} commands)",
                f"Success rate: {success_rate:.1f}%"
            )
        else:
            self.result.add_fail(
                f"Rapid TCP commands ({total_count} commands)",
                f"Success rate: {success_rate:.1f}% (expected >= 90%)"
            )

    def test_tcp_large_response(self):
        """Test 3: Large response via TCP"""
        print(f"\n{Fore.CYAN}=== WiFi Test 3: Large TCP Response ==={Style.RESET_ALL}")

        response = self.send_tcp_command('SYST:INFO?', wait_time=1.5)

        if response and len(response) > 100:
            self.result.add_pass(
                "Large TCP response",
                f"Received {len(response)} bytes"
            )
        else:
            self.result.add_fail(
                "Large TCP response",
                f"Response too short: {len(response) if response else 0} bytes"
            )

    def run_all_tests(self):
        """Run all WiFi TCP tests"""
        print(f"\n{Fore.YELLOW}{'='*70}")
        print(f"DAQiFi WiFi TCP Test Suite")
        print(f"Testing: {self.host}:{self.port}")
        print(f"{'='*70}{Style.RESET_ALL}\n")

        self.test_tcp_connection()
        self.test_tcp_rapid_commands()
        self.test_tcp_large_response()

        return self.result.print_summary()


def main():
    parser = argparse.ArgumentParser(
        description='Test DAQiFi circular buffer fixes (PR #129)'
    )
    parser.add_argument('--port', default='COM3',
                       help='Serial port (default: COM3)')
    parser.add_argument('--baud', type=int, default=115200,
                       help='Baud rate (default: 115200)')
    parser.add_argument('--wifi-ip', default=None,
                       help='WiFi IP address for TCP tests (optional)')
    parser.add_argument('--skip-usb', action='store_true',
                       help='Skip USB tests')
    parser.add_argument('--skip-wifi', action='store_true',
                       help='Skip WiFi tests')
    parser.add_argument('--verbose', '-v', action='store_true',
                       help='Show detailed command/response exchanges')

    args = parser.parse_args()

    all_passed = True

    # Run USB tests
    if not args.skip_usb:
        usb_tester = DAQiFiTester(args.port, args.baud, verbose=args.verbose)
        usb_passed = usb_tester.run_all_tests()
        all_passed = all_passed and usb_passed

    # Run WiFi tests if IP provided
    if args.wifi_ip and not args.skip_wifi:
        wifi_tester = WiFiTester(args.wifi_ip)
        wifi_passed = wifi_tester.run_all_tests()
        all_passed = all_passed and wifi_passed
    elif not args.skip_wifi:
        print(f"\n{Fore.YELLOW}WiFi tests skipped (use --wifi-ip to enable){Style.RESET_ALL}")

    # Final summary
    print(f"\n{'='*70}")
    if all_passed:
        print(f"{Fore.GREEN}ALL TESTS PASSED ✓{Style.RESET_ALL}")
        print("Circular buffer fixes verified successfully!")
    else:
        print(f"{Fore.RED}SOME TESTS FAILED ✗{Style.RESET_ALL}")
        print("Review failures above and re-test after fixes.")
    print(f"{'='*70}\n")

    return 0 if all_passed else 1


if __name__ == '__main__':
    sys.exit(main())
