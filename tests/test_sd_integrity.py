#!/usr/bin/env python3
"""
SD Card Write Integrity Test

Verifies that data written via the SD benchmark command matches expected
patterns byte-for-byte. Uses the SCPI benchmark command to write known
patterns, downloads the file, and compares against locally generated data.

Usage:
    python test_sd_integrity.py [--port /dev/ttyACM0] [--size 512] [--patterns 1,2]

Requirements:
    pip install pyserial
"""

import argparse
import serial
import time
import sys
import struct


def send_scpi(ser, cmd, delay=0.5, read_timeout=2.0):
    """Send SCPI command and return response lines."""
    ser.reset_input_buffer()
    ser.write(f"{cmd}\r".encode())
    time.sleep(delay)

    end_time = time.time() + read_timeout
    response = b""
    while time.time() < end_time:
        if ser.in_waiting:
            response += ser.read(ser.in_waiting)
            time.sleep(0.05)
        else:
            time.sleep(0.05)

    lines = response.decode("ascii", errors="replace").strip().splitlines()
    # Filter out echo of our command
    return [l for l in lines if l.strip() and l.strip() != cmd.strip()]


def wait_for_idle(ser, timeout=30):
    """Wait for SD card manager to become idle."""
    start = time.time()
    while time.time() - start < timeout:
        resp = send_scpi(ser, "SYST:STOR:SD:ENAble?", delay=0.3)
        time.sleep(0.5)
        # Just wait — the benchmark command blocks until complete
        if time.time() - start > 2:
            return True
    return True


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


def run_benchmark(ser, size_kb, pattern):
    """Run SD benchmark and return the result string."""
    print(f"  Running benchmark: {size_kb}KB, pattern={pattern}...")
    resp = send_scpi(ser, f"SYST:STOR:SD:BENCH {size_kb},{pattern}",
                     delay=1.0, read_timeout=30.0)
    for line in resp:
        if "Benchmark complete" in line or "bytes/sec" in line:
            print(f"  Result: {line.strip()}")
            return line.strip()
        if "Error" in line:
            print(f"  ERROR: {line.strip()}")
            return None
    print(f"  Response: {resp}")
    return resp[-1] if resp else None


def download_file(ser, filename, timeout=30):
    """Download file from SD card via SCPI GET command."""
    print(f"  Downloading '{filename}'...")
    ser.reset_input_buffer()
    ser.write(f'SYST:STOR:SD:GET "{filename}"\r'.encode())

    data = b""
    eof_marker = b"<EOF>"
    end_time = time.time() + timeout

    while time.time() < end_time:
        if ser.in_waiting:
            chunk = ser.read(ser.in_waiting)
            data += chunk
            if eof_marker in data:
                # Strip EOF marker and anything after
                idx = data.index(eof_marker)
                data = data[:idx]
                break
            time.sleep(0.05)
        else:
            time.sleep(0.1)
    else:
        print(f"  WARNING: Download timeout after {timeout}s, got {len(data)} bytes")

    print(f"  Downloaded {len(data)} bytes")
    return data


def compare_data(expected, actual):
    """Compare expected vs actual data, return (match, first_mismatch_offset)."""
    min_len = min(len(expected), len(actual))
    for i in range(min_len):
        if expected[i] != actual[i]:
            return False, i
    if len(expected) != len(actual):
        return False, min_len
    return True, -1


def list_files(ser, directory="DAQiFi"):
    """List files on SD card."""
    resp = send_scpi(ser, f'SYST:STOR:SD:LISt? "{directory}"',
                     delay=2.0, read_timeout=5.0)
    return resp


def find_benchmark_file(ser):
    """Find the most recent benchmark file on SD card."""
    files = list_files(ser)
    benchmark_files = []
    for line in files:
        if "benchmark_" in line and ".dat" in line:
            # Extract filename
            parts = line.strip().split()
            for p in parts:
                if "benchmark_" in p and ".dat" in p:
                    benchmark_files.append(p.strip())
    return benchmark_files[-1] if benchmark_files else None


def test_pattern(ser, size_kb, pattern, pattern_name):
    """Test a single pattern: write, download, compare."""
    print(f"\n--- Testing pattern {pattern} ({pattern_name}), {size_kb}KB ---")

    # Run benchmark
    result = run_benchmark(ser, size_kb, pattern)
    if result is None:
        print(f"  FAIL: Benchmark command failed")
        return False

    # Wait for file to be fully written
    time.sleep(1)

    # Find the benchmark file
    bench_file = find_benchmark_file(ser)
    if not bench_file:
        print(f"  FAIL: Could not find benchmark file on SD card")
        return False

    print(f"  Found file: {bench_file}")

    # Download the file
    actual_data = download_file(ser, bench_file)
    expected_size = size_kb * 1024

    if len(actual_data) == 0:
        print(f"  FAIL: Downloaded 0 bytes (expected {expected_size})")
        return False

    # Generate expected pattern
    expected_data = generate_pattern(pattern, expected_size)

    # Compare
    if len(actual_data) != expected_size:
        print(f"  FAIL: Size mismatch - expected {expected_size}, got {len(actual_data)}")
        # Still compare what we have
        match, offset = compare_data(expected_data[:len(actual_data)],
                                     actual_data)
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
        # Show context around mismatch
        start = max(0, offset - 4)
        end = min(len(actual_data), offset + 8)
        print(f"    Expected [{start}:{end}]: {expected_data[start:end].hex()}")
        print(f"    Actual   [{start}:{end}]: {actual_data[start:end].hex()}")
        return False


def cleanup_benchmark_files(ser):
    """Delete benchmark files from SD card."""
    files = list_files(ser)
    for line in files:
        if "benchmark_" in line and ".dat" in line:
            parts = line.strip().split()
            for p in parts:
                if "benchmark_" in p and ".dat" in p:
                    filename = p.strip()
                    print(f"  Deleting {filename}...")
                    send_scpi(ser, f'SYST:STOR:SD:DEL "{filename}"',
                              delay=1.0)


def main():
    parser = argparse.ArgumentParser(description="SD Card Write Integrity Test")
    parser.add_argument("--port", default="/dev/ttyACM0",
                        help="Serial port (default: /dev/ttyACM0)")
    parser.add_argument("--baud", type=int, default=115200,
                        help="Baud rate (default: 115200)")
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

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
        time.sleep(0.5)
    except serial.SerialException as e:
        print(f"ERROR: Cannot open {args.port}: {e}")
        sys.exit(1)

    try:
        # Verify device is responsive
        resp = send_scpi(ser, "*IDN?")
        if not resp:
            print("ERROR: No response from device")
            sys.exit(1)
        print(f"Device: {resp[0] if resp else 'unknown'}")

        # Check power state
        resp = send_scpi(ser, "SYST:POW:STAT?")
        power_state = resp[-1].strip() if resp else "?"
        print(f"Power state: {power_state}")
        if power_state == "0":
            print("WARNING: Device in STANDBY - powering up...")
            send_scpi(ser, "SYST:POW:STAT 1", delay=3.0)

        # Enable SD card
        send_scpi(ser, "SYST:STOR:SD:ENAble 1", delay=1.0)

        # Run tests
        results = {}
        for pattern in patterns:
            name = pattern_names.get(pattern, f"pattern-{pattern}")
            passed = test_pattern(ser, args.size, pattern, name)
            results[name] = passed

        # Cleanup if requested
        if args.cleanup:
            print(f"\n--- Cleanup ---")
            cleanup_benchmark_files(ser)

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
        sys.exit(0 if all_pass else 1)

    finally:
        ser.close()


if __name__ == "__main__":
    main()
