#!/usr/bin/env python3
"""Stream over USB while WiFi is connected to Tesla but no TCP client. Run N times."""
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

send('SYST:POW:STAT 1', 2.0)
for ch in range(16):
    send(f'ENA:VOLT:DC {ch},1', 0.05)
send('SYST:STR:FOR 2', 0.2)
send('SYST:STR:INT 0', 0.2)  # USB

# verify wifi is connected
ssid = send('SYST:COMM:LAN:SSID?', 0.3).decode(errors='replace')
addr = send('SYST:COMM:LAN:ADDR?', 0.3).decode(errors='replace')
print(f'WiFi state: SSID={ssid.strip()[-30:]!r} ADDR={addr.strip()[-30:]!r}')

results = []
for trial in range(1, N + 1):
    s.reset_input_buffer()
    s.write(b'SYST:StartStreamData 2\r'); time.sleep(4.0)
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
        v = f'NO_DATA'
    else:
        all_16 = all(n == 16 for n in nz[:5])
        v = f'{"FIXED" if all_16 else "broken"} min={min(nz[:5])} max={max(nz[:5])} rows={len(nz)}'
    results.append(v)
    print(f'trial {trial:2d}: {v}')
    time.sleep(0.5)

print(f'\nSummary: {sum(1 for r in results if r.startswith("FIXED"))}/{N} FIXED')
s.close()
