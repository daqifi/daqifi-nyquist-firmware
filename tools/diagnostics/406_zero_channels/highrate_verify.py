#!/usr/bin/env python3
"""Verify TRGSRC=STRIG fix at multiple sample rates >= 5 kHz."""
import serial, sys, time
dev = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyACM0'

s = serial.Serial(dev, 115200, timeout=1.0); time.sleep(0.5); s.reset_input_buffer()
def send(cmd, wait=0.3):
    s.write((cmd + '\r').encode()); time.sleep(wait); return s.read_all()

send('SYST:POW:STAT 1', 2.0)
for ch in range(16): send(f'ENA:VOLT:DC {ch},1', 0.05)
send('SYST:STR:FOR 2', 0.2)
send('SYST:STR:INT 0', 0.2)

print(f"{'rate':>10} {'duration':>10} {'rows_seen':>10} {'min_nz':>8} {'max_nz':>8} {'mean_nz':>10} {'verdict':>10}")
for rate in [5000, 10000, 13000]:
    s.reset_input_buffer()
    s.write(f'SYST:StartStreamData {rate}\r'.encode()); time.sleep(2.0)
    s.write(b'SYST:StopStreamData\r'); time.sleep(0.8)
    raw = s.read_all().decode(errors='replace')
    nz = []
    for line in raw.splitlines():
        parts = line.strip().split(',')
        if len(parts) == 32:
            try:
                row = [float(p) for p in parts]
                nz.append(sum(1 for i in range(16) if abs(row[2*i+1]) > 50))
            except ValueError: pass
    if not nz:
        print(f'{rate:>10d} {"2 s":>10} {"0":>10} {"-":>8} {"-":>8} {"-":>10} {"NO_DATA":>10}')
        continue
    # Skip first row (warmup transient)
    body = nz[1:] if len(nz) > 1 else nz
    mn, mx = min(body), max(body)
    mean = sum(body) / len(body)
    verdict = "FIXED" if mn == 16 and mx == 16 else f"broken({mn}-{mx})"
    print(f'{rate:>10d} {"2 s":>10} {len(nz):>10d} {mn:>8d} {mx:>8d} {mean:>10.2f} {verdict:>10}')
    time.sleep(0.3)
s.close()
