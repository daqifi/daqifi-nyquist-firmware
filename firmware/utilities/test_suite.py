#!/usr/bin/env python3
"""
DAQiFi Nyquist Test Suite - Unified Testing Interface

Combines firmware validation, USB CDC tests, and WiFi firmware updates
into a single test suite with interactive menu and CLI options.

Usage:
    # Interactive menu
    python test_suite.py COM3

    # Run specific test groups
    python test_suite.py COM3 --firmware
    python test_suite.py COM3 --usb-cdc
    python test_suite.py COM3 --wifi-update
    python test_suite.py COM3 --all

    # Multiple test groups
    python test_suite.py COM3 --firmware --usb-cdc

Requirements:
    pip install pyserial colorama
"""

import sys
import argparse
from pathlib import Path

# Force UTF-8 output for Windows console (only if not already wrapped)
if sys.platform == 'win32':
    import codecs
    if hasattr(sys.stdout, 'buffer'):
        sys.stdout = codecs.getwriter('utf-8')(sys.stdout.buffer, 'strict')
        sys.stderr = codecs.getwriter('utf-8')(sys.stderr.buffer, 'strict')

# Import test modules
try:
    from validate import DAQiFiTester as FirmwareTester
    from test_circular_buffer_fix import DAQiFiTester as USBCDCTester
    from update_wifi_firmware import WiFiFirmwareUpdater
except ImportError as e:
    print(f"Error importing test modules: {e}")
    print("Make sure all test scripts are in the same directory.")
    sys.exit(1)


class TestSuite:
    """Unified test suite manager"""

    def __init__(self, port='COM3', quiet=False, save_reports=False):
        self.port = port
        self.quiet = quiet
        self.save_reports = save_reports
        self.results = {
            'firmware': None,
            'usb_cdc': None,
            'wifi_update': None
        }

    def print_banner(self):
        """Print test suite banner"""
        print("=" * 70)
        print("DAQiFi Nyquist Test Suite")
        print("=" * 70)
        print(f"Port: {self.port}")
        print()

    def show_menu(self):
        """Display interactive test menu"""
        print("\nAvailable Test Suites:")
        print("  1. Firmware Validation (comprehensive)")
        print("  2. USB CDC Tests (circular buffer race conditions)")
        print("  3. WiFi Firmware Update")
        print("  4. Run All Tests")
        print("  0. Exit")
        print()

        choice = input("Select test suite (0-4): ").strip()
        return choice

    def run_firmware_tests(self):
        """Run comprehensive firmware validation tests"""
        print("\n" + "=" * 70)
        print("Running Firmware Validation Tests")
        print("=" * 70 + "\n")

        try:
            tester = FirmwareTester(
                port=self.port,
                save_reports=self.save_reports,
                verbose=not self.quiet
            )
            success = tester.run_all_tests()
            self.results['firmware'] = success
            return success
        except KeyboardInterrupt:
            print("\n\nFirmware tests interrupted by user")
            return False
        except Exception as e:
            print(f"\nError running firmware tests: {e}")
            return False
        finally:
            try:
                tester.close()
            except:
                pass

    def run_usb_cdc_tests(self):
        """Run USB CDC circular buffer tests"""
        print("\n" + "=" * 70)
        print("Running USB CDC Tests")
        print("=" * 70 + "\n")

        try:
            tester = USBCDCTester(port=self.port, verbose=not self.quiet)

            if not tester.connect():
                print("Failed to connect to device")
                return False

            try:
                # Run USB tests only (skip WiFi tests)
                print("\n--- USB CDC Tests ---\n")
                tester.test_duplicate_prompts()
                tester.test_basic_scpi()
                tester.test_rapid_commands()
                tester.test_large_response()
                tester.test_command_echo()
                tester.test_power_transitions()
                tester.test_error_handling()
                tester.test_buffer_stress()

                # Print summary
                success = tester.result.print_summary()
                self.results['usb_cdc'] = success
                return success

            finally:
                tester.disconnect()

        except KeyboardInterrupt:
            print("\n\nUSB CDC tests interrupted by user")
            return False
        except Exception as e:
            print(f"\nError running USB CDC tests: {e}")
            import traceback
            traceback.print_exc()
            return False

    def run_wifi_update(self):
        """Run WiFi firmware update"""
        print("\n" + "=" * 70)
        print("Running WiFi Firmware Update")
        print("=" * 70 + "\n")

        try:
            # Ask user for confirmation
            print("This will update the WINC1500 WiFi module firmware to version 19.7.7")
            confirm = input("Continue? (y/N): ").strip().lower()

            if confirm != 'y':
                print("WiFi update cancelled")
                return True  # Not a failure, user cancelled

            # Look for winc_flash_tool.cmd
            updater = WiFiFirmwareUpdater(port=self.port)
            if not updater.tool_path:
                print("\nERROR: winc_flash_tool.cmd not found!")
                print("Please specify path with --wifi-tool option")
                return False

            success = updater.update()
            self.results['wifi_update'] = success
            return success

        except KeyboardInterrupt:
            print("\n\nWiFi update interrupted by user")
            return False
        except Exception as e:
            print(f"\nError running WiFi update: {e}")
            return False

    def run_all_tests(self):
        """Run all test suites"""
        print("\n" + "=" * 70)
        print("Running All Test Suites")
        print("=" * 70)

        results = []

        # Run firmware validation
        print("\n[1/2] Firmware Validation")
        results.append(self.run_firmware_tests())

        # Run USB CDC tests
        print("\n[2/2] USB CDC Tests")
        results.append(self.run_usb_cdc_tests())

        # WiFi update is optional, skip in "run all"

        return all(results)

    def print_final_summary(self):
        """Print final test results summary"""
        print("\n" + "=" * 70)
        print("FINAL TEST SUMMARY")
        print("=" * 70)

        for test_name, result in self.results.items():
            if result is None:
                status = "NOT RUN"
                color = ""
            elif result:
                status = "✓ PASSED"
                color = "\033[92m"  # Green
            else:
                status = "✗ FAILED"
                color = "\033[91m"  # Red

            reset = "\033[0m" if color else ""
            print(f"{color}{test_name.replace('_', ' ').title():.<40} {status}{reset}")

        print("=" * 70)

        # Overall result
        run_tests = [r for r in self.results.values() if r is not None]
        if not run_tests:
            print("\nNo tests were run")
            return False
        elif all(run_tests):
            print("\n✓ ALL TESTS PASSED")
            return True
        else:
            print("\n✗ SOME TESTS FAILED")
            return False


def interactive_mode(suite):
    """Run test suite in interactive menu mode"""
    suite.print_banner()

    while True:
        choice = suite.show_menu()

        if choice == '0':
            print("\nExiting test suite")
            break
        elif choice == '1':
            suite.run_firmware_tests()
        elif choice == '2':
            suite.run_usb_cdc_tests()
        elif choice == '3':
            suite.run_wifi_update()
        elif choice == '4':
            suite.run_all_tests()
            break  # Exit after running all
        else:
            print(f"\nInvalid choice: {choice}")
            continue

        # Ask if user wants to run more tests
        if choice in ['1', '2', '3']:
            cont = input("\nRun another test? (Y/n): ").strip().lower()
            if cont == 'n':
                break

    # Print final summary
    suite.print_final_summary()


def main():
    parser = argparse.ArgumentParser(
        description='DAQiFi Nyquist Unified Test Suite',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='''
Examples:
  # Interactive menu
  python test_suite.py COM3

  # Run specific test groups
  python test_suite.py COM3 --firmware
  python test_suite.py COM3 --usb-cdc
  python test_suite.py COM3 --wifi-update

  # Run all automated tests (excludes WiFi update)
  python test_suite.py COM3 --all

  # Multiple test groups
  python test_suite.py COM3 --firmware --usb-cdc

  # With reports and quiet mode
  python test_suite.py COM3 --all --save-reports --quiet
        '''
    )

    parser.add_argument('port', nargs='?', default='COM3',
                        help='Serial port (default: COM3)')

    # Test selection
    test_group = parser.add_argument_group('Test Selection')
    test_group.add_argument('--firmware', action='store_true',
                           help='Run firmware validation tests')
    test_group.add_argument('--usb-cdc', action='store_true',
                           help='Run USB CDC circular buffer tests')
    test_group.add_argument('--wifi-update', action='store_true',
                           help='Run WiFi firmware update')
    test_group.add_argument('--all', action='store_true',
                           help='Run all automated tests (firmware + USB CDC)')

    # Options
    options_group = parser.add_argument_group('Options')
    options_group.add_argument('--save-reports', action='store_true',
                              help='Save test reports to test_results/')
    options_group.add_argument('--quiet', '-q', action='store_true',
                              help='Minimal output (only show test results)')
    options_group.add_argument('--wifi-tool', type=str,
                              help='Path to winc_flash_tool.cmd (for WiFi update)')

    args = parser.parse_args()

    # Create test suite
    suite = TestSuite(
        port=args.port,
        quiet=args.quiet,
        save_reports=args.save_reports
    )

    # Determine mode: CLI or interactive
    cli_mode = args.firmware or args.usb_cdc or args.wifi_update or args.all

    if cli_mode:
        # CLI mode - run specified tests
        suite.print_banner()
        success = True

        if args.all:
            success = suite.run_all_tests()
        else:
            if args.firmware:
                success = suite.run_firmware_tests() and success

            if args.usb_cdc:
                success = suite.run_usb_cdc_tests() and success

            if args.wifi_update:
                success = suite.run_wifi_update() and success

        # Print summary
        suite.print_final_summary()

        sys.exit(0 if success else 1)
    else:
        # Interactive menu mode
        interactive_mode(suite)
        sys.exit(0)


if __name__ == "__main__":
    main()
