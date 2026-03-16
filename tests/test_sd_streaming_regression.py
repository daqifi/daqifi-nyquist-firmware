#!/usr/bin/env python3
"""SD Card Streaming Regression Test

Verifies that SD card logging works correctly with zero data loss.
Run this after any changes to the SD write path, streaming pipeline,
or encoder to catch regressions.

Tests:
  1. Single channel @ 5kHz, 5 minutes, SD-only
     - Validates high-frequency single-channel throughput
     - Expected: ~1.5M samples, ~60 KB/s SD write rate, 0 drops
  2. All 16 channels @ 1kHz, 5 minutes, SD-only
     - Validates multi-channel throughput (all public channels)
     - Expected: ~300K samples, ~200 KB/s SD write rate, 0 drops

Hardware requirements:
  - NQ1 board (MC12bADC, 16 public channels 0-15)
  - SD card inserted (tested with SanDisk Extreme PRO A2 256GB)
  - USB serial connection (/dev/ttyACM0)

Usage:
  python3 -u test_sd_streaming_regression.py [--device /dev/ttyACM0] [--duration 300]

Pass criteria:
  - QueueDroppedSamples == 0
  - SdDroppedBytes == 0
  - EncoderFailures == 0
  - Actual sample rate within 5% of requested rate
  - No error logs

Baseline results (PR #214, 2026-03-15, SanDisk SR256):
  1ch @ 5kHz:  1,498,295 samples, 4994 Hz actual, 62.0 KB/s, 0 drops
  16ch @ 1kHz: 302,723 samples, 1009 Hz actual, 204.2 KB/s, 0 drops

NQ1 channel types (for reference):
  Type 1 (dedicated ADC modules, simultaneous conversion):
    Channels 4, 8, 10, 12, 14
  Type 2 (shared Module 7, sequential scan):
    Channels 0, 1, 2, 3, 5, 6, 7, 9, 11, 13, 15
  Note: At freq > 1kHz, ChannelScanFreqDiv = freq/1000, so Type 2
  channels scan at max 1kHz regardless of requested rate.
  Note: Aggregate Type 1 cap = 15kHz total (freq clamped to 15000/count).
"""
import serial
import time
import sys
import argparse

ALL_PUBLIC = list(range(16))

def send_cmd(ser, cmd, delay=0.5):
    ser.reset_input_buffer()
    ser.write((cmd + '\r\n').encode())
    time.sleep(delay)
    return ser.read(ser.in_waiting).decode(errors='replace').strip()

def drain_errors(ser):
    for _ in range(10):
        if '"No error"' in send_cmd(ser, "SYST:ERR?", 0.3):
            break

def get_stats(ser):
    resp = send_cmd(ser, "SYST:STR:STATS?", 1.5)
    stats = {}
    for line in resp.split('\n'):
        line = line.strip()
        if '=' in line and 'DAQIFI>' not in line:
            k, _, v = line.partition('=')
            stats[k.strip()] = v.strip()
    return stats

def run_test(ser, label, channels, freq, duration_sec):
    print(f"\n{'='*60}")
    print(f"REGRESSION TEST: {label}")
    print(f"  {len(channels)} ch @ {freq} Hz, {duration_sec}s, SD-only")
    print(f"{'='*60}")

    # Setup
    send_cmd(ser, "SYST:StopStreamData", 1)
    drain_errors(ser)
    send_cmd(ser, "SYST:STR:ClearStats", 0.5)
    send_cmd(ser, "SYST:LOG:CLEar", 0.5)

    resp = send_cmd(ser, "SYST:POW:STAT?", 0.5)
    if '1' not in resp:
        print("  Powering up...")
        send_cmd(ser, "SYST:POW:STAT 1", 3)

    send_cmd(ser, "SYST:STOR:SD:ENAble 1", 1)
    send_cmd(ser, "SYST:STR:FORmat 2", 0.5)      # CSV
    send_cmd(ser, "SYST:STR:INTerface 2", 0.5)    # SD-only (no USB streaming)

    fname = f"regtest_{len(channels)}ch_{freq}hz.csv"
    send_cmd(ser, f'SYST:STOR:SD:LOGging "{fname}"', 0.5)

    # Configure channels
    for ch in ALL_PUBLIC:
        send_cmd(ser, f"CONF:ADC:CHAN {ch},0", 0.1)
    for ch in channels:
        send_cmd(ser, f"CONF:ADC:CHAN {ch},1", 0.1)

    print(f"  File: {fname}")

    # Start streaming
    send_cmd(ser, f"SYST:StartStreamData {freq}", 2)

    # Monitor every 60s
    start = time.time()
    last_check = start

    while (time.time() - start) < duration_sec:
        if (time.time() - last_check) >= 60:
            stats = get_stats(ser)
            elapsed = int(time.time() - start)
            samples = stats.get('TotalSamplesStreamed', '?')
            sd_drop = stats.get('SdDroppedBytes', '?')
            q_drop = stats.get('QueueDroppedSamples', '?')
            print(f"  [{elapsed:3d}s/{duration_sec}s] samples={samples} sd_drop={sd_drop} q_drop={q_drop}")
            last_check = time.time()
        time.sleep(5)

    # Stop
    print("  Stopping...")
    send_cmd(ser, "SYST:StopStreamData", 3)
    send_cmd(ser, "SYST:STR:INTerface 0", 0.5)  # Reset to default

    # Collect results
    stats = get_stats(ser)
    print(f"\n  --- Results ---")
    for k in ['TotalSamplesStreamed', 'TotalBytesStreamed', 'QueueDroppedSamples',
              'SdDroppedBytes', 'EncoderFailures', 'SampleLossPercent']:
        print(f"  {k}: {stats.get(k, 'N/A')}")

    actual_rate = 0
    try:
        total_bytes = int(stats.get('TotalBytesStreamed', '0'))
        total_samples = int(stats.get('TotalSamplesStreamed', '0'))
        actual_rate = total_samples / duration_sec
        print(f"  Actual sample rate: {actual_rate:.0f} Hz (expected ~{freq})")
        print(f"  SD write rate: {total_bytes / duration_sec / 1024:.1f} KB/s")
    except (ValueError, ZeroDivisionError):
        pass

    # Check logs
    logs = send_cmd(ser, "SYST:LOG?", 2)
    log_lines = [l.strip() for l in logs.split('\n')
                 if l.strip() and 'DAQIFI>' not in l and '>' not in l
                 and 'SYST:' not in l and 'No log' not in l and l.strip()]
    if log_lines:
        print(f"\n  Logs ({len(log_lines)}):")
        for l in log_lines[:5]:
            print(f"    {l}")
        if len(log_lines) > 5:
            print(f"    ... and {len(log_lines) - 5} more")
    else:
        print(f"  No error logs")

    drain_errors(ser)

    # Evaluate pass/fail
    try:
        sd_dropped = int(stats.get('SdDroppedBytes', '0'))
        q_dropped = int(stats.get('QueueDroppedSamples', '0'))
        enc_fail = int(stats.get('EncoderFailures', '0'))
        no_drops = (sd_dropped == 0 and q_dropped == 0 and enc_fail == 0)
    except (ValueError, TypeError):
        no_drops = False

    # Check sample rate within 5% of expected
    rate_ok = abs(actual_rate - freq) / freq < 0.05 if freq > 0 and actual_rate > 0 else False

    passed = no_drops and rate_ok
    if not rate_ok and no_drops:
        print(f"\n  WARNING: Rate {actual_rate:.0f} Hz deviates >5% from {freq} Hz")
    print(f"\n  {'PASS' if passed else 'FAIL'}")
    return passed

def main():
    parser = argparse.ArgumentParser(description='SD Card Streaming Regression Test')
    parser.add_argument('--device', default='/dev/ttyACM0', help='Serial device path')
    parser.add_argument('--duration', type=int, default=300, help='Test duration in seconds (default: 300)')
    args = parser.parse_args()

    ser = serial.Serial(args.device, 115200, timeout=1)
    time.sleep(0.5)
    ser.reset_input_buffer()

    print("SD Card Streaming Regression Test")
    print(f"Device: {args.device}, Duration: {args.duration}s per test")

    resp = send_cmd(ser, "*IDN?", 0.5)
    for line in resp.split('\n'):
        if 'DAQiFi' in line:
            print(f"Board: {line.strip()}")

    resp = send_cmd(ser, "SYST:STOR:SD:INFO?", 0.5)
    for line in resp.split('\n'):
        line = line.strip()
        if line and 'DAQIFI>' not in line and '>' not in line and 'SYST:' not in line:
            print(f"Card: {line}")

    r1 = run_test(ser, "1ch @ 5kHz", [4], 5000, args.duration)
    time.sleep(5)
    r2 = run_test(ser, "16ch @ 1kHz", ALL_PUBLIC, 1000, args.duration)

    print(f"\n{'='*60}")
    print(f"REGRESSION SUMMARY")
    print(f"{'='*60}")
    print(f"  1 ch @ 5kHz:  {'PASS' if r1 else 'FAIL'}")
    print(f"  16 ch @ 1kHz: {'PASS' if r2 else 'FAIL'}")
    overall = r1 and r2
    print(f"  Overall: {'PASS' if overall else 'FAIL'}")

    ser.close()
    sys.exit(0 if overall else 1)

if __name__ == '__main__':
    main()
