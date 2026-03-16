#!/usr/bin/env python3
"""Verify test pattern data from DAQiFi SD card CSV files.

Downloads CSV files generated with test pattern mode and verifies that
every sample matches the expected synthetic value. This provides 100%
data integrity verification across the full pipeline:
  ISR → deferred task → pool → queue → encoder → SD write → file read

The script sets voltage precision=0 (integer millivolts) on the device
before streaming, so CSV values are deterministic integers that can be
computed from the pattern formula: mv = round(raw * range / resolution * 1000).

Usage:
  # Download from device and verify (requires pyserial):
  python3 verify_test_patterns.py --download --device /dev/ttyACM0 --pattern 1

  # Verify all patterns in one run:
  python3 verify_test_patterns.py --run-all --device /dev/ttyACM0

  # Verify an existing CSV file (precision=0 millivolt format):
  python3 verify_test_patterns.py testpat_p1.csv --pattern 1

Pattern Types:
  1 = Counter:   (sampleCount + channelId) % (adcMax+1)
  2 = Midscale:  adcMax / 2  (constant)
  3 = Fullscale: adcMax      (constant)
  4 = Walking:   (sampleCount * (channelId+1)) % (adcMax+1)
  5 = Triangle:  ramp 0→adcMax→0, period=2*(adcMax+1)
  6 = Sine:      256-sample sine, scaled to [0, adcMax]
"""
import argparse
import math
import sys
import time

# Sine wave period: must match firmware's SINE_PERIOD (256 samples per cycle)
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


def raw_to_millivolt(raw_value, adc_max, adc_range):
    """Convert raw ADC count to integer millivolts (matching firmware precision=0).

    Firmware formula: mv = round(raw * range * internalScale * CalM / resolution * 1000 + CalB*1000)
    With default calibration (CalM=1, CalB=0) and internalScale=1:
      mv = round(raw * range / resolution * 1000)

    Args:
        raw_value: Raw ADC count (0 to adc_max)
        adc_max: ADC resolution (e.g. 4096)
        adc_range: ADC voltage range in volts (e.g. 5.0 for NQ1)
    """
    voltage_mv = raw_value * adc_range / adc_max * 1000.0
    # Match firmware rounding: (int32_t)(mv + 0.5) for positive values
    return round(voltage_mv)


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


def detect_sample_offset(rows, channels, pattern, adc_max, adc_range):
    """Detect the sample counter offset for the first row in the CSV.

    The firmware's gTestPatternSampleCount starts at 0 when streaming begins,
    but the first sample written to the SD file may arrive after a pipeline
    delay. This function determines that offset so we can verify all
    subsequent samples correctly.

    For counter pattern (1): offset = round(actual_mv * adc_max / adc_range / 1000) - channelId
    For other patterns: try offsets 0..200 and pick the one that matches row 0.
    """
    if not rows or not channels:
        return 0

    ch = channels[0]
    val_col = f"ain{ch}_val"
    if val_col not in rows[0]:
        return 0

    actual_mv = rows[0][val_col]

    if pattern == 1:  # Counter: raw = (sampleCount + ch) % (adcMax+1)
        # Reverse the millivolt conversion to get raw
        raw_approx = actual_mv * adc_max / (adc_range * 1000.0)
        raw = round(raw_approx)
        offset = (raw - ch) % (adc_max + 1)
        return offset

    # For other patterns, brute-force search a reasonable offset range
    for offset in range(500):
        expected_raw = generate_expected_raw(pattern, ch, offset, adc_max)
        expected_mv = raw_to_millivolt(expected_raw, adc_max, adc_range)
        if abs(actual_mv - expected_mv) <= 1:
            # Verify with second row too (avoid false positives)
            if len(rows) > 1 and val_col in rows[1]:
                expected_raw2 = generate_expected_raw(pattern, ch, offset + 1, adc_max)
                expected_mv2 = raw_to_millivolt(expected_raw2, adc_max, adc_range)
                if abs(rows[1][val_col] - expected_mv2) <= 1:
                    return offset
            else:
                return offset

    print(f"  WARNING: Could not detect sample offset (first value: {actual_mv})")
    return 0


def verify_pattern(filepath, pattern, adc_max, adc_range=5.0,
                   tolerance_mv=1, verbose=False):
    """Verify that CSV data matches the expected test pattern.

    Expects precision=0 CSV format (integer millivolts). Auto-detects the
    pipeline sample offset, then computes expected millivolt values from the
    pattern formula and compares every sample.

    Args:
        filepath: Path to CSV file
        pattern: Pattern type (1-6)
        adc_max: ADC resolution (e.g., 4096)
        adc_range: ADC voltage range in volts (default 5.0 for NQ1)
        tolerance_mv: Allowed deviation in millivolts (default 1)
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
    print(f"  Pattern: {pattern}, ADC max: {adc_max}, range: {adc_range}V")

    # Auto-detect pipeline sample offset
    offset = detect_sample_offset(rows, channels, pattern, adc_max, adc_range)
    print(f"  Detected sample offset: {offset}")

    total_samples = 0
    mismatches = 0
    first_mismatches = []

    for sample_idx, row in enumerate(rows):
        sample_count = offset + sample_idx
        for ch in channels:
            val_col = f"ain{ch}_val"
            if val_col not in row:
                continue

            actual_mv = row[val_col]
            expected_raw = generate_expected_raw(pattern, ch, sample_count, adc_max)
            expected_mv = raw_to_millivolt(expected_raw, adc_max, adc_range)

            match = (abs(actual_mv - expected_mv) <= tolerance_mv)

            total_samples += 1
            if not match:
                mismatches += 1
                if len(first_mismatches) < 10:
                    first_mismatches.append(
                        f"  Row {sample_idx} (sample {sample_count}), ch{ch}: "
                        f"expected_raw={expected_raw}, expected_mv={expected_mv}, "
                        f"actual_mv={actual_mv}, diff={actual_mv - expected_mv}"
                    )

            if verbose and sample_idx < 20:
                status = "OK" if match else "MISMATCH"
                print(f"  [{sample_idx}] sample={sample_count}, ch{ch}: raw={expected_raw}, "
                      f"expected_mv={expected_mv}, actual_mv={actual_mv} [{status}]")

    details = ""
    if first_mismatches:
        details = "First mismatches:\n" + "\n".join(first_mismatches)

    return mismatches == 0, total_samples, mismatches, details


def download_and_verify(device, pattern, adc_max, adc_range=5.0,
                        duration=5, freq=1000, channel=4):
    """Run a test pattern session on device, download CSV, and verify.

    Sets voltage precision=0 (integer millivolts) for deterministic output,
    streams to SD card, downloads the file, and verifies every sample.
    """
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
    send_cmd("CONF:VOLT:PREC 0", 0.3)             # Integer millivolts
    send_cmd(f"SYST:STR:TESTpattern {pattern}")
    send_cmd("SYST:STOR:SD:ENAble 1", 1)
    send_cmd("SYST:STR:FORmat 2", 0.3)             # CSV
    send_cmd("SYST:STR:INTerface 2", 0.3)          # SD-only
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

    # Check for drops
    has_drops = False
    for line in stats_resp.split('\n'):
        if 'Dropped' in line and '=0' not in line:
            has_drops = True
        if 'LossPercent' in line and '=0' not in line:
            has_drops = True
    if has_drops:
        print("  WARNING: Data loss detected during streaming!")

    send_cmd("SYST:STR:TESTpattern 0")

    # Download file — read continuously to avoid serial buffer overflow
    print(f"  Downloading {fname}...")
    ser.reset_input_buffer()
    ser.write(f'SYST:STOR:SD:GET "{fname}"\r\n'.encode())

    data = b""
    timeout_end = time.time() + 120  # 2 min for large files
    while time.time() < timeout_end:
        waiting = ser.in_waiting
        if waiting > 0:
            data += ser.read(waiting)
            if b"__END_OF_FILE__" in data:
                break
        else:
            time.sleep(0.05)

    ser.close()
    print(f"  Downloaded {len(data)} bytes")

    # Parse downloaded data
    text = data.decode(errors='replace')
    lines = text.split('\n')
    csv_lines = []
    in_csv = False
    for line in lines:
        line = line.strip()
        # Strip DAQIFI> prompt prefix if present
        if line.startswith('DAQIFI>'):
            line = line[len('DAQIFI>'):].strip()
        if '# Device:' in line:
            in_csv = True
            # Extract just the metadata part after any prefix
            idx = line.index('# Device:')
            line = line[idx:]
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
        local_path, pattern, adc_max, adc_range, verbose=True)

    if passed:
        print(f"  PASS: {total} samples verified, 0 mismatches")
    else:
        print(f"  FAIL: {mismatches}/{total} mismatches")
        if details:
            print(details)

    return passed


def run_all_patterns(device, adc_max, adc_range=5.0, duration=3, freq=1000, channel=4):
    """Run and verify all 6 patterns sequentially."""
    results = {}
    for p in range(1, 7):
        name = {1: 'Counter', 2: 'Midscale', 3: 'Fullscale',
                4: 'Walking', 5: 'Triangle', 6: 'Sine'}[p]
        print(f"\n{'='*50}")
        print(f"Pattern {p}: {name}")
        print(f"{'='*50}")
        results[p] = download_and_verify(device, p, adc_max, adc_range,
                                         duration, freq, channel)
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
    parser.add_argument('--adc-range', type=float, default=5.0,
                        help='ADC voltage range in volts (default: 5.0 for NQ1)')
    parser.add_argument('--tolerance', type=int, default=1,
                        help='Allowed deviation in millivolts (default: 1)')
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
        ok = run_all_patterns(args.device, args.adc_max, args.adc_range,
                              args.duration, args.freq, args.channel)
        sys.exit(0 if ok else 1)

    if args.download:
        if args.pattern is None:
            parser.error("--pattern required with --download")
        ok = download_and_verify(args.device, args.pattern, args.adc_max,
                                 args.adc_range, args.duration, args.freq,
                                 args.channel)
        sys.exit(0 if ok else 1)

    if args.filepath is None:
        parser.error("filepath required (or use --download / --run-all)")

    if args.pattern is None:
        parser.error("--pattern required when verifying a file")

    passed, total, mismatches, details = verify_pattern(
        args.filepath, args.pattern, args.adc_max, args.adc_range,
        args.tolerance, args.verbose)

    if passed:
        print(f"\nPASS: {total} samples verified, 0 mismatches")
    else:
        print(f"\nFAIL: {mismatches}/{total} mismatches")
        if details:
            print(details)

    sys.exit(0 if passed else 1)


if __name__ == '__main__':
    main()
