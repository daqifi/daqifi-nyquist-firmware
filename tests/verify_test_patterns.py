#!/usr/bin/env python3
"""Verify test pattern data from DAQiFi SD card CSV files.

Downloads CSV files generated with test pattern mode and verifies that
every sample matches the expected synthetic value. This provides 100%
data integrity verification across the full pipeline:
  ISR → deferred task → pool → queue → encoder → SD write → file read

Usage:
  # Capture test pattern data, then verify:
  python3 verify_test_patterns.py testpat_counter.csv --pattern 1 --adc-max 4096
  python3 verify_test_patterns.py testpat_tri.csv --pattern 5 --adc-max 4096
  python3 verify_test_patterns.py testpat_sin.csv --pattern 6 --adc-max 4096

  # Download from device first (requires pyserial):
  python3 verify_test_patterns.py --download --device /dev/ttyACM0 --pattern 1

  # Verify all patterns in one run:
  python3 verify_test_patterns.py --run-all --device /dev/ttyACM0

Pattern Types:
  1 = Counter:   (sampleCount + channelId) % (adcMax+1)
  2 = Midscale:  adcMax / 2  (constant)
  3 = Fullscale: adcMax      (constant)
  4 = Walking:   (sampleCount * (channelId+1)) % (adcMax+1)
  5 = Triangle:  ramp 0→adcMax→0, period=2*(adcMax+1)
  6 = Sine:      256-sample sine, scaled to [0, adcMax]
"""
import argparse
import csv
import math
import os
import sys
import time

# Sine LUT must match firmware's gSineLUT exactly (256 entries, uint16_t)
SINE_PERIOD = 256


def generate_expected_raw(pattern, channel_id, sample_count, adc_max):
    """Generate expected raw ADC value for a given pattern.

    Must exactly match Streaming_GenerateTestValue() in streaming.c.
    """
    adc_range = adc_max + 1

    if pattern == 1:  # Counter
        return (sample_count + channel_id) % adc_range
    elif pattern == 2:  # Midscale
        return adc_max // 2
    elif pattern == 3:  # Fullscale
        return adc_max
    elif pattern == 4:  # Walking
        return (sample_count * (channel_id + 1)) % adc_range
    elif pattern == 5:  # Triangle
        period = 2 * adc_range
        pos = (sample_count + channel_id * (adc_range // 4)) % period
        return pos if pos < adc_range else (period - 1 - pos)
    elif pattern == 6:  # Sine (computed via sin(), matches firmware hardware FPU)
        phase = (sample_count + channel_id * 32) % SINE_PERIOD
        s = math.sin(phase * 2.0 * math.pi / SINE_PERIOD)
        return int((s + 1.0) * 0.5 * adc_max)
    else:
        raise ValueError(f"Unknown pattern: {pattern}")


def raw_to_voltage(raw_value, adc_max, vref, internal_scale):
    """Convert raw ADC count to voltage (matching firmware ADC_ConvertToVoltage)."""
    return (raw_value / adc_max) * vref * internal_scale


def voltage_to_raw(voltage, adc_max, vref, internal_scale):
    """Convert voltage back to raw ADC count."""
    if internal_scale == 0 or vref == 0:
        return 0
    return round(voltage / (vref * internal_scale) * adc_max)


def parse_csv_file(filepath):
    """Parse a DAQiFi CSV file, returning metadata and data rows.

    Returns:
        metadata: dict with device info
        channels: list of (channel_id, is_timestamp) tuples from header
        rows: list of dicts mapping column name to value
    """
    metadata = {}
    header = None
    rows = []

    with open(filepath, 'r') as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            if line.startswith('#'):
                # Parse metadata comments
                if ':' in line:
                    key, _, val = line[2:].partition(':')
                    metadata[key.strip()] = val.strip()
                continue
            if header is None:
                # First non-comment line is the CSV header
                header = line.split(',')
                continue
            # Data row
            values = line.split(',')
            if len(values) == len(header):
                row = {}
                for col, val in zip(header, values):
                    try:
                        row[col] = int(val)
                    except ValueError:
                        try:
                            row[col] = float(val)
                        except ValueError:
                            row[col] = val
                rows.append(row)

    # Parse channel IDs from header (format: ain4_ts, ain4_val)
    channels = []
    for col in (header or []):
        if col.endswith('_val'):
            ch_id = int(col.replace('ain', '').replace('_val', ''))
            channels.append(ch_id)

    return metadata, channels, header or [], rows


def verify_pattern(filepath, pattern, adc_max, vref=3.3, internal_scale=None,
                   tolerance=0, verbose=False):
    """Verify that CSV data matches the expected test pattern.

    Args:
        filepath: Path to CSV file
        pattern: Pattern type (1-6)
        adc_max: ADC resolution (e.g., 4096)
        vref: ADC reference voltage
        internal_scale: Per-channel scale factor (None = auto-detect)
        tolerance: Allowed deviation in raw ADC counts (0 for exact match)
        verbose: Print each sample comparison

    Returns:
        (passed, total_samples, mismatches, details)
    """
    metadata, channels, header, rows = parse_csv_file(filepath)

    if not rows:
        return False, 0, 0, "No data rows found"

    if not channels:
        return False, 0, 0, "No channel columns found in header"

    print(f"  File: {filepath}")
    print(f"  Metadata: {metadata}")
    print(f"  Channels: {channels}")
    print(f"  Rows: {len(rows)}")
    print(f"  Pattern: {pattern}, ADC max: {adc_max}")

    # For voltage-encoded CSV, we need to reverse the voltage conversion.
    # Try to auto-detect the scale by comparing first few samples.
    # If internal_scale is provided, use it directly.
    if internal_scale is None:
        # Try to detect: generate expected raw for sample 0, compare to actual
        # This works for patterns with known first values
        if pattern == 2:  # Midscale - constant, easy to detect
            expected_raw = adc_max // 2
            for ch in channels:
                val_col = f"ain{ch}_val"
                if val_col in rows[0]:
                    actual_voltage = rows[0][val_col]
                    if expected_raw > 0 and isinstance(actual_voltage, (int, float)):
                        detected_scale = actual_voltage / ((expected_raw / adc_max) * vref)
                        print(f"  Auto-detected internal_scale for ch{ch}: {detected_scale:.4f}")
                        internal_scale = detected_scale
                        break

    # If we still don't have scale, try raw value matching
    # (assumes precision=0 millivolt output or raw values)
    if internal_scale is None:
        print("  WARNING: Could not auto-detect internal_scale.")
        print("  Attempting raw integer comparison (precision=0 mode)")
        internal_scale = 0  # Flag for raw mode

    total_samples = 0
    mismatches = 0
    first_mismatches = []

    for sample_idx, row in enumerate(rows):
        for ch in channels:
            val_col = f"ain{ch}_val"
            if val_col not in row:
                continue

            actual = row[val_col]
            expected_raw = generate_expected_raw(pattern, ch, sample_idx, adc_max)

            if internal_scale == 0:
                # Raw/millivolt mode: compare integer values directly
                # The firmware outputs millivolts, need to figure out the conversion
                expected_val = expected_raw  # Direct comparison attempt
                match = (abs(actual - expected_val) <= tolerance)
            else:
                # Voltage mode: convert expected raw to voltage and compare
                expected_voltage = raw_to_voltage(expected_raw, adc_max, vref, internal_scale)
                # Compare with tolerance in voltage units
                voltage_tol = raw_to_voltage(tolerance, adc_max, vref, internal_scale) if tolerance > 0 else 0.001
                match = abs(actual - expected_voltage) <= voltage_tol

            total_samples += 1
            if not match:
                mismatches += 1
                if len(first_mismatches) < 5:
                    first_mismatches.append(
                        f"  Row {sample_idx}, ch{ch}: expected_raw={expected_raw}, "
                        f"actual={actual}"
                    )

            if verbose and sample_idx < 10:
                status = "OK" if match else "MISMATCH"
                print(f"  [{sample_idx}] ch{ch}: expected_raw={expected_raw}, "
                      f"actual={actual} [{status}]")

    details = ""
    if first_mismatches:
        details = "First mismatches:\n" + "\n".join(first_mismatches)

    return mismatches == 0, total_samples, mismatches, details


def download_and_verify(device, pattern, adc_max, duration=5, freq=1000, channel=4):
    """Run a test pattern session on device, download CSV, and verify."""
    try:
        import serial
    except ImportError:
        print("ERROR: pyserial required for device communication")
        print("  pip install pyserial")
        return False

    fname = f"testpat_p{pattern}.csv"

    ser = serial.Serial(device, 115200, timeout=1)
    time.sleep(0.5)
    ser.reset_input_buffer()

    def send_cmd(cmd, delay=0.5):
        ser.reset_input_buffer()
        ser.write((cmd + '\r\n').encode())
        time.sleep(delay)
        return ser.read(ser.in_waiting).decode(errors='replace').strip()

    print(f"\n--- Pattern {pattern}: Streaming {duration}s @ {freq}Hz, ch{channel} ---")

    # Setup
    send_cmd("SYST:StopStreamData", 1)
    send_cmd("SYST:POW:STAT 1", 3)
    send_cmd(f"SYST:STR:TESTpattern {pattern}")
    send_cmd("SYST:STOR:SD:ENAble 1", 1)
    send_cmd("SYST:STR:FORmat 2", 0.3)       # CSV
    send_cmd("SYST:STR:INTerface 2", 0.3)     # SD-only
    send_cmd(f'SYST:STOR:SD:LOGging "{fname}"', 0.5)

    # Disable all, enable target channel
    for ch in range(16):
        send_cmd(f"CONF:ADC:CHAN {ch},0", 0.1)
    send_cmd(f"CONF:ADC:CHAN {channel},1", 0.3)

    # Stream
    send_cmd(f"SYST:StartStreamData {freq}", duration + 1)
    send_cmd("SYST:StopStreamData", 2)

    # Stats
    stats_resp = send_cmd("SYST:STR:STATS?", 1.5)
    print(f"  Stats: {stats_resp}")

    send_cmd("SYST:STR:TESTpattern 0")

    # Download file
    print(f"  Downloading {fname}...")
    ser.reset_input_buffer()
    ser.write(f'SYST:STOR:SD:GET "{fname}"\r\n'.encode())
    time.sleep(2)

    # Read until __END_OF_FILE__
    data = b""
    timeout_end = time.time() + 30
    while time.time() < timeout_end:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            data += chunk
            if b"__END_OF_FILE__" in data:
                break
        time.sleep(0.1)

    ser.close()

    # Parse downloaded data
    text = data.decode(errors='replace')
    # Find CSV start (after DAQIFI> prompt)
    lines = text.split('\n')
    csv_lines = []
    in_csv = False
    for line in lines:
        line = line.strip()
        if line.startswith('# Device:'):
            in_csv = True
        if in_csv:
            if '__END_OF_FILE__' in line:
                break
            csv_lines.append(line)

    if not csv_lines:
        print("  ERROR: No CSV data received")
        return False

    # Write to temp file for verification
    local_path = f"/tmp/{fname}"
    with open(local_path, 'w') as f:
        f.write('\n'.join(csv_lines) + '\n')

    print(f"  Saved {len(csv_lines)} lines to {local_path}")

    # Verify
    passed, total, mismatches, details = verify_pattern(
        local_path, pattern, adc_max, verbose=True)

    if passed:
        print(f"  PASS: {total} samples verified, 0 mismatches")
    else:
        print(f"  FAIL: {mismatches}/{total} mismatches")
        if details:
            print(details)

    return passed


def run_all_patterns(device, adc_max, duration=3, freq=1000, channel=4):
    """Run and verify all 6 patterns sequentially."""
    results = {}
    for p in range(1, 7):
        name = {1: 'Counter', 2: 'Midscale', 3: 'Fullscale',
                4: 'Walking', 5: 'Triangle', 6: 'Sine'}[p]
        print(f"\n{'='*50}")
        print(f"Pattern {p}: {name}")
        print(f"{'='*50}")
        results[p] = download_and_verify(device, p, adc_max, duration, freq, channel)
        time.sleep(2)

    print(f"\n{'='*50}")
    print("SUMMARY")
    print(f"{'='*50}")
    all_pass = True
    for p in range(1, 7):
        name = {1: 'Counter', 2: 'Midscale', 3: 'Fullscale',
                4: 'Walking', 5: 'Triangle', 6: 'Sine'}[p]
        status = 'PASS' if results[p] else 'FAIL'
        print(f"  Pattern {p} ({name}): {status}")
        if not results[p]:
            all_pass = False

    print(f"\n  Overall: {'PASS' if all_pass else 'FAIL'}")
    return all_pass


def main():
    parser = argparse.ArgumentParser(
        description='Verify DAQiFi test pattern data from CSV files',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    parser.add_argument('filepath', nargs='?', help='CSV file to verify')
    parser.add_argument('--pattern', type=int, choices=[1,2,3,4,5,6],
                        help='Pattern type (1=counter, 2=midscale, 3=fullscale, '
                             '4=walking, 5=triangle, 6=sine)')
    parser.add_argument('--adc-max', type=int, default=4096,
                        help='ADC resolution (default: 4096 for NQ1 12-bit)')
    parser.add_argument('--vref', type=float, default=3.3,
                        help='ADC reference voltage (default: 3.3)')
    parser.add_argument('--scale', type=float, default=None,
                        help='Internal scale factor (auto-detect if omitted)')
    parser.add_argument('--tolerance', type=int, default=1,
                        help='Allowed deviation in raw ADC counts (default: 1)')
    parser.add_argument('--verbose', '-v', action='store_true',
                        help='Print each sample comparison')
    parser.add_argument('--download', action='store_true',
                        help='Download from device before verifying')
    parser.add_argument('--device', default='/dev/ttyACM0',
                        help='Serial device path (default: /dev/ttyACM0)')
    parser.add_argument('--run-all', action='store_true',
                        help='Run and verify all 6 patterns')
    parser.add_argument('--duration', type=int, default=5,
                        help='Streaming duration in seconds (default: 5)')
    parser.add_argument('--freq', type=int, default=1000,
                        help='Sample rate in Hz (default: 1000)')
    parser.add_argument('--channel', type=int, default=4,
                        help='ADC channel to use (default: 4)')
    args = parser.parse_args()

    if args.run_all:
        ok = run_all_patterns(args.device, args.adc_max, args.duration,
                              args.freq, args.channel)
        sys.exit(0 if ok else 1)

    if args.download:
        if args.pattern is None:
            parser.error("--pattern required with --download")
        ok = download_and_verify(args.device, args.pattern, args.adc_max,
                                 args.duration, args.freq, args.channel)
        sys.exit(0 if ok else 1)

    if args.filepath is None:
        parser.error("filepath required (or use --download / --run-all)")

    if args.pattern is None:
        parser.error("--pattern required when verifying a file")

    passed, total, mismatches, details = verify_pattern(
        args.filepath, args.pattern, args.adc_max, args.vref,
        args.scale, args.tolerance, args.verbose)

    if passed:
        print(f"\nPASS: {total} samples verified, 0 mismatches")
    else:
        print(f"\nFAIL: {mismatches}/{total} mismatches")
        if details:
            print(details)

    sys.exit(0 if passed else 1)


if __name__ == '__main__':
    main()
