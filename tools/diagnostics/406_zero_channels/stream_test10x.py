#!/usr/bin/env python3
"""Run 16-channel stream 10 times against an already-flashed device."""
import serial, sys, time

dev = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyACM0'
N = int(sys.argv[2]) if len(sys.argv) > 2 else 10

s = serial.Serial(dev, 115200, timeout=1.0)
time.sleep(0.5)
s.reset_input_buffer()

def send(cmd, wait=0.3):
    s.write((cmd + '\r').encode())
    time.sleep(wait)
    return s.read_all()

# One-time setup
send('SYST:POW:STAT 1', 2.0)
for ch in range(16):
    send(f'ENA:VOLT:DC {ch},1', 0.05)
send('SYST:STR:FOR 2', 0.2)
send('SYST:STR:INT 0', 0.2)

results = []
for trial in range(1, N + 1):
    s.reset_input_buffer()
    s.write(b'SYST:StartStreamData 2\r')
    time.sleep(4.0)
    s.write(b'SYST:StopStreamData\r')
    time.sleep(0.8)
    raw = s.read_all().decode(errors='replace')
    nz_seen = []
    rows_32 = 0
    for line in raw.splitlines():
        parts = line.strip().split(',')
        if len(parts) == 32:
            rows_32 += 1
            try:
                row = [float(p) for p in parts]
                nz = sum(1 for i in range(16) if abs(row[2*i+1]) > 50)
                nz_seen.append(nz)
            except ValueError:
                pass
    if not nz_seen:
        verdict = f'NO_DATA rows32={rows_32}'
    else:
        all_16 = all(n == 16 for n in nz_seen[:5])
        min_nz = min(nz_seen[:5])
        max_nz = max(nz_seen[:5])
        verdict = f'{"FIXED" if all_16 else "broken"} min={min_nz} max={max_nz} rows={len(nz_seen)}'
    results.append(verdict)
    print(f'trial {trial:2d}: {verdict}')
    time.sleep(0.5)

print(f'\nSummary: {sum(1 for r in results if r.startswith("FIXED"))}/{N} FIXED')
s.close()
