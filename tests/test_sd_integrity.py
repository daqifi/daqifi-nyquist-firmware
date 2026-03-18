#!/usr/bin/env python3
"""
SD Card Write Integrity Test

Verifies that data written via the SD benchmark command matches expected
patterns byte-for-byte. Uses the SCPI benchmark command to write known
patterns, downloads the file, and compares against locally generated data.

Usage:
    python test_sd_integrity.py [--port /dev/ttyACM0] [--size 64] [--patterns 1,2]

Requirements:
    pip install -e ../daqifi-python-core
"""

import argparse
import time
import sys
from pathlib import Path

# Add daqifi-python-core to path
sys.path.insert(0, str(Path(__file__).parent.parent.parent / 'daqifi-python-core'))
sys.path.insert(0, str(Path(__file__).parent.parent / 'daqifi-python-core'))

from daqifi import NyquistDevice, PowerState


def generate_pattern(pattern_id, size):
    """Generate expected data pattern matching firmware implementation."""
    data = bytearray(size)
    if pattern_id == 0:
        # All zeros
        pass
    elif pattern_id == 1:
        # Sequential: each byte = (offset & 0xFF)
        for i in range(size):
            data[i] = i & 0xFF
    elif pattern_id == 2:
        # Pseudo-random: (offset * 1103515245 + 12345) & 0xFF
        for i in range(size):
            data[i] = ((i * 1103515245 + 12345) & 0xFF)
    return bytes(data)


def compare_data(expected, actual):
    """Compare expected vs actual data, return (match, first_mismatch_offset)."""
    min_len = min(len(expected), len(actual))
    for i in range(min_len):
        if expected[i] != actual[i]:
            return False, i
    if len(expected) != len(actual):
        return False, min_len
    return True, -1


def find_benchmark_file(device):
    """Find the most recent benchmark file on SD card."""
    files = device.sd_list_files()
    benchmark_files = [f for f in files if "benchmark_" in f and ".dat" in f]
    if not benchmark_files:
        return None
    # Strip directory prefix — sd_get_file expects filename only
    fname = benchmark_files[-1].rsplit("/", 1)[-1]
    return fname


def test_pattern(device, size_kb, pattern, pattern_name):
    """Test a single pattern: write, download, compare."""
    print(f"\n--- Testing pattern {pattern} ({pattern_name}), {size_kb}KB ---")

    # Run benchmark with pattern parameter (not supported by sd_benchmark() API)
    print(f"  Running benchmark: {size_kb}KB, pattern={pattern}...")
    response = device._comm.send_command(
        f"SYST:STOR:SD:BENCHmark {size_kb},{pattern}")
    if response:
        for line in response.strip().split('\n'):
            if "Benchmark complete" in line or "bytes/sec" in line:
                print(f"  Result: {line.strip()}")
            elif "Error" in line:
                print(f"  ERROR: {line.strip()}")
                return False

    time.sleep(1)

    # Find the benchmark file
    bench_file = find_benchmark_file(device)
    if not bench_file:
        print(f"  FAIL: Could not find benchmark file on SD card")
        return False

    print(f"  Found file: {bench_file}")

    # Download the file as raw binary (preserves all byte values)
    actual_data = device.sd_get_file_binary(bench_file, timeout=60)
    expected_size = size_kb * 1024

    if len(actual_data) == 0:
        print(f"  FAIL: Downloaded 0 bytes (expected {expected_size})")
        return False

    print(f"  Downloaded {len(actual_data)} bytes")

    # Generate expected pattern
    expected_data = generate_pattern(pattern, expected_size)

    # Compare
    if len(actual_data) != expected_size:
        print(f"  FAIL: Size mismatch - expected {expected_size}, got {len(actual_data)}")
        match, offset = compare_data(expected_data[:len(actual_data)], actual_data)
        if not match:
            print(f"  First data mismatch at byte offset {offset}")
            print(f"    Expected: 0x{expected_data[offset]:02X}")
            print(f"    Actual:   0x{actual_data[offset]:02X}")
        return False

    match, offset = compare_data(expected_data, actual_data)
    if match:
        print(f"  PASS: All {expected_size} bytes match")
        return True
    else:
        print(f"  FAIL: Data mismatch at byte offset {offset}")
        start = max(0, offset - 4)
        end = min(len(actual_data), offset + 8)
        print(f"    Expected [{start}:{end}]: {expected_data[start:end].hex()}")
        print(f"    Actual   [{start}:{end}]: {actual_data[start:end].hex()}")
        return False


def main():
    parser = argparse.ArgumentParser(description="SD Card Write Integrity Test")
    parser.add_argument("--port", default="/dev/ttyACM0",
                        help="Serial port (default: /dev/ttyACM0)")
    parser.add_argument("--size", type=int, default=64,
                        help="Test size in KB (default: 64)")
    parser.add_argument("--patterns", default="1,2",
                        help="Comma-separated pattern IDs to test (default: 1,2)")
    parser.add_argument("--cleanup", action="store_true",
                        help="Delete benchmark files after test")
    args = parser.parse_args()

    pattern_names = {0: "zeros", 1: "sequential", 2: "pseudo-random"}
    patterns = [int(p) for p in args.patterns.split(",")]

    print(f"SD Card Write Integrity Test")
    print(f"Port: {args.port}, Size: {args.size}KB, Patterns: {patterns}")
    print(f"{'=' * 60}")

    device = NyquistDevice.connect(args.port)
    print(f"Device: {device.variant.value}")

    if device.power_state != PowerState.POWERED_UP:
        print("Powering up...")
        device.power_up()
        time.sleep(2)
    print(f"Power state: {device.power_state}")

    # Enable SD card
    device.sd_enable(True)
    time.sleep(0.5)

    # Run tests
    results = {}
    for pattern in patterns:
        name = pattern_names.get(pattern, f"pattern-{pattern}")
        passed = test_pattern(device, args.size, pattern, name)
        results[name] = passed

    # Cleanup if requested
    if args.cleanup:
        print(f"\n--- Cleanup ---")
        files = device.sd_list_files()
        for f in files:
            if "benchmark_" in f and ".dat" in f:
                fname = f.rsplit("/", 1)[-1]
                print(f"  Deleting {fname}...")
                device.sd_delete_file(fname)

    # Summary
    print(f"\n{'=' * 60}")
    print("RESULTS:")
    all_pass = True
    for name, passed in results.items():
        status = "PASS" if passed else "FAIL"
        print(f"  {name}: {status}")
        if not passed:
            all_pass = False

    print(f"\nOverall: {'PASS' if all_pass else 'FAIL'}")

    device.disconnect()
    sys.exit(0 if all_pass else 1)


if __name__ == "__main__":
    main()
