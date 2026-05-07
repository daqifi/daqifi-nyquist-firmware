#!/usr/bin/env python3
"""WiFi 16-channel stream test over TCP, repeat N times."""
import socket, sys, time, re

ip = sys.argv[1] if len(sys.argv) > 1 else '192.168.1.160'
port = 9760
N = int(sys.argv[2]) if len(sys.argv) > 2 else 10

def open_sock():
    sk = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sk.settimeout(5.0)
    sk.connect((ip, port))
    return sk

def send(sk, cmd, wait=0.3):
    sk.sendall((cmd + '\r').encode())
    time.sleep(wait)

def drain(sk, dur):
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

# Setup once
sk = open_sock()
send(sk, '*CLS', 0.2)
send(sk, 'SYST:POW:STAT 1', 2.0)
for ch in range(16):
    send(sk, f'ENA:VOLT:DC {ch},1', 0.05)
send(sk, 'SYST:STR:FOR 2', 0.2)   # CSV
send(sk, 'SYST:STR:INT 1', 0.3)   # WiFi
sk.close()
time.sleep(0.5)

results = []
for trial in range(1, N + 1):
    sk = open_sock()
    # drain any pending bytes
    try:
        sk.settimeout(0.2)
        sk.recv(65536)
    except socket.timeout:
        pass

    sk.sendall(b'SYST:StartStreamData 2\r')
    time.sleep(0.3)
    raw = drain(sk, 4.0)
    sk.sendall(b'SYST:StopStreamData\r')
    time.sleep(0.5)
    raw += drain(sk, 0.5)

    text = raw.decode(errors='replace')
    nz_seen = []
    for line in text.splitlines():
        parts = line.strip().split(',')
        if len(parts) == 32:
            try:
                row = [float(p) for p in parts]
                nz = sum(1 for i in range(16) if abs(row[2*i+1]) > 50)
                nz_seen.append(nz)
            except ValueError:
                pass
    if not nz_seen:
        v = f'NO_DATA bytes={len(raw)} sample={text[:100]!r}'
    else:
        all_16 = all(n == 16 for n in nz_seen[:5])
        v = f'{"FIXED" if all_16 else "broken"} min={min(nz_seen[:5])} max={max(nz_seen[:5])} rows={len(nz_seen)}'
    results.append(v)
    print(f'trial {trial:2d}: {v}')
    sk.close()
    time.sleep(0.6)

print(f'\nSummary: {sum(1 for r in results if r.startswith("FIXED"))}/{N} FIXED')
