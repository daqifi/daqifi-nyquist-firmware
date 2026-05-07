#!/bin/bash
# Build/flash production, then run a short 16-channel stream and dump LOG.
set -e
bash ~/.claude/skills/flash/flash.sh --build > /tmp/build.log 2>&1
if ! grep -q "Program Succeeded" /tmp/build.log; then
  echo "[BUILD FAIL]"; tail -10 /tmp/build.log; exit 2
fi
sleep 3
DAQIFI_DEV=$(bash /tmp/find_bench_device.sh 2>/dev/null)
[ -z "$DAQIFI_DEV" ] && { echo "[NO DEV]"; exit 3; }
echo "[device] $DAQIFI_DEV"

python3 -c "
import serial, time
s = serial.Serial('$DAQIFI_DEV', 115200, timeout=1.0); time.sleep(0.5); s.reset_input_buffer()
s.write(b'SYST:POW:STAT 1\r'); time.sleep(2.0); s.read_all()
s.write(b'SYST:LOG:CLEAR\r'); time.sleep(0.2); s.read_all()
for ch in range(16): s.write(f'ENA:VOLT:DC {ch},1\r'.encode()); time.sleep(0.05)
s.read_all()
s.write(b'SYST:STR:FOR 2\r'); time.sleep(0.2); s.write(b'SYST:STR:INT 0\r'); time.sleep(0.2); s.read_all()
s.write(b'SYST:StartStreamData 2\r'); time.sleep(4.0)
s.write(b'SYST:StopStreamData\r'); time.sleep(0.8)
raw = s.read_all().decode(errors='replace')
nz_report = '?'
for line in raw.splitlines():
    parts = line.strip().split(',')
    if len(parts) == 32:
        try:
            row = [float(p) for p in parts]
            nz = sum(1 for i in range(16) if abs(row[2*i+1]) > 50)
            nz_report = f'{nz}/16'
            break
        except ValueError: pass
print(f'NONZERO: {nz_report}')
time.sleep(0.3)
s.write(b'SYST:LOG?\r'); time.sleep(2.0)
log = s.read_all().decode(errors='replace')
for line in log.splitlines():
    if 'diag421' in line:
        print(line.strip())
s.close()
"
