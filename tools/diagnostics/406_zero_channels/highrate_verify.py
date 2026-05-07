#!/usr/bin/env python3
"""Verify TRGSRC=STRIG fix at multiple sample rates >= 5 kHz.

At high rates (5-13 kHz × 16 channels), pyserial's host-side buffer can't
hold a full streaming window. Reading once after StopStreamData would only
capture the tail. This script reads in chunks during the streaming window
to accumulate the full output.
"""
import serial, sys, time

dev = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyACM0'

s = serial.Serial(dev, 115200, timeout=0.05)
time.sleep(0.5)
s.reset_input_buffer()


def send(cmd, wait=0.3):
    s.write((cmd + '\r').encode())
    time.sleep(wait)
    return s.read_all()


def stream_chunked(rate_hz, duration_s):
    """Start, read continuously for duration_s, stop, drain. Returns full text."""
    s.reset_input_buffer()
    chunks = []
    s.write(f'SYST:StartStreamData {rate_hz}\r'.encode())
    end = time.time() + duration_s
    while time.time() < end:
        data = s.read(8192)
        if data:
            chunks.append(data)
        else:
            time.sleep(0.005)
    s.write(b'SYST:StopStreamData\r')
    # Drain trailing
    drain_end = time.time() + 0.8
    while time.time() < drain_end:
        data = s.read(8192)
        if data:
            chunks.append(data)
        else:
            time.sleep(0.01)
    return b''.join(chunks).decode(errors='replace')


send('SYST:POW:STAT 1', 2.0)
for ch in range(16):
    send(f'ENA:VOLT:DC {ch},1', 0.05)
send('SYST:STR:FOR 2', 0.2)
send('SYST:STR:INT 0', 0.2)

DURATION_S = 2.0
print(f"{'rate':>10} {'expect':>10} {'rows_seen':>10} {'min_nz':>8} {'max_nz':>8} {'mean_nz':>10} {'verdict':>10}")
for rate in [5000, 10000, 13000]:
    raw = stream_chunked(rate, DURATION_S)
    nz = []
    for line in raw.splitlines():
        parts = line.strip().split(',')
        if len(parts) == 32:
            try:
                row = [float(p) for p in parts]
                nz.append(sum(1 for i in range(16) if abs(row[2*i+1]) > 50))
            except ValueError:
                pass
    expected = int(rate * DURATION_S)
    if not nz:
        print(f'{rate:>10d} {expected:>10d} {"0":>10} {"-":>8} {"-":>8} {"-":>10} {"NO_DATA":>10}')
        continue
    body = nz[1:] if len(nz) > 1 else nz
    mn, mx = min(body), max(body)
    mean = sum(body) / len(body)
    verdict = "FIXED" if mn == 16 and mx == 16 else f"broken({mn}-{mx})"
    capture_ratio = len(nz) / expected if expected else 0
    print(f'{rate:>10d} {expected:>10d} {len(nz):>10d} {mn:>8d} {mx:>8d} {mean:>10.2f} {verdict:>10}  ({capture_ratio*100:.0f}% captured)')
    time.sleep(0.3)
s.close()
