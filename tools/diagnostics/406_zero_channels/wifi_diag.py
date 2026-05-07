#!/usr/bin/env python3
"""Run one WiFi stream trial, then pull diag log over USB."""
import socket, serial, sys, time

ip = sys.argv[1] if len(sys.argv) > 1 else '192.168.1.160'
port = 9760
usb = sys.argv[2] if len(sys.argv) > 2 else '/dev/ttyACM0'

def send_tcp(sk, cmd, wait=0.3):
    sk.sendall((cmd + '\r').encode())
    time.sleep(wait)

def drain_tcp(sk, dur):
    sk.settimeout(0.5)
    end = time.time() + dur
    out = b''
    while time.time() < end:
        try:
            chunk = sk.recv(8192)
            if not chunk:
                break
            out += chunk
        except socket.timeout:
            pass
    return out

s = serial.Serial(usb, 115200, timeout=1.0)
time.sleep(0.5)
s.reset_input_buffer()
s.write(b'SYST:LOG:CLEAR\r'); time.sleep(0.3); s.read_all()
s.close()

# Run stream over WiFi
sk = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sk.settimeout(5.0)
sk.connect((ip, port))
send_tcp(sk, '*CLS', 0.2)
send_tcp(sk, 'SYST:POW:STAT 1', 1.5)
for ch in range(16):
    send_tcp(sk, f'ENA:VOLT:DC {ch},1', 0.05)
send_tcp(sk, 'SYST:STR:FOR 2', 0.2)
send_tcp(sk, 'SYST:STR:INT 1', 0.3)

sk.sendall(b'SYST:StartStreamData 2\r')
time.sleep(0.3)
raw = drain_tcp(sk, 4.0)
sk.sendall(b'SYST:StopStreamData\r')
time.sleep(0.8)
raw += drain_tcp(sk, 0.5)
sk.close()

# Count nz channels in WiFi-side data
text = raw.decode(errors='replace')
nz_seen = []
for line in text.splitlines():
    parts = line.strip().split(',')
    if len(parts) == 32:
        try:
            row = [float(p) for p in parts]
            nz_seen.append(sum(1 for i in range(16) if abs(row[2*i+1]) > 50))
        except ValueError:
            pass
print(f'WiFi rows={len(nz_seen)} nz={nz_seen[:5]}')

# Pull diag log over USB
time.sleep(0.5)
s = serial.Serial(usb, 115200, timeout=2.0)
time.sleep(0.3)
s.reset_input_buffer()
s.write(b'SYST:LOG?\r')
time.sleep(2.0)
log = s.read_all().decode(errors='replace')
s.close()
for line in log.splitlines():
    if 'diag421' in line or 'streaming' in line.lower():
        print('LOG:', line.strip())
