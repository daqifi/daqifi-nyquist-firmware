#!/usr/bin/env python3
"""Run a 16-channel DAQiFi stream test, count nonzero channels per row."""
import serial, sys, time

dev = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyACM0'
s = serial.Serial(dev, 115200, timeout=1.0)
time.sleep(0.5)
s.reset_input_buffer()
s.write(b'SYST:POW:STAT 1\r')
time.sleep(2.0)
s.read_all()
for ch in range(16):
    s.write(f'ENA:VOLT:DC {ch},1\r'.encode())
    time.sleep(0.05)
s.read_all()
s.write(b'SYST:STR:FOR 2\r'); time.sleep(0.2)
s.write(b'SYST:STR:INT 0\r'); time.sleep(0.2)
s.read_all()
s.write(b'SYST:StartStreamData 2\r'); time.sleep(4.0)
s.write(b'SYST:StopStreamData\r'); time.sleep(0.8)
raw = s.read_all().decode(errors='replace')

rows_seen = 0
nz_seen = []
for line in raw.splitlines():
    parts = line.strip().split(',')
    if len(parts) == 32:
        rows_seen += 1
        try:
            row = [float(p) for p in parts]
            nz = sum(1 for i in range(16) if abs(row[2*i+1]) > 50)
            nz_seen.append(nz)
        except ValueError:
            pass
print(f'rows_seen={rows_seen} nz_per_row[:5]={nz_seen[:5]}')
print(f'first 200 chars: {raw[:200]!r}')
s.close()
