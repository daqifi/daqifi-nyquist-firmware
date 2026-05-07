#!/bin/bash
# Pin one Makefile source file's compile at a given -O level. Run+test.
# Usage: bash /tmp/pin_file_olevel.sh <line_no> <-O level> [<obj path to delete>]
set -e
SCRIPT_DIR="$(cd -- "$(dirname -- "$0")" && pwd)"
LINE_NO="$1"
LEVEL="$2"
OBJPATH="${3:-}"
MAKEFILE=firmware/daqifi.X/nbproject/Makefile-default.mk

# Strip any prior -fno-* and replace -O[0-9] with chosen level on this line only
sed -i "${LINE_NO}s/ -fno-[a-z-]\+//g" "$MAKEFILE"
sed -i "${LINE_NO}s/-O[0-9]/${LEVEL}/" "$MAKEFILE"
echo "[line $LINE_NO] $(sed -n "${LINE_NO}p" "$MAKEFILE" | grep -oE -- '-O[0-9][^ ]*' | head -1) | $(sed -n "${LINE_NO}p" "$MAKEFILE" | grep -oE -- '\.\./src[^ ]+\.c' | head -1)"

[ -n "$OBJPATH" ] && rm -f "$OBJPATH"
bash ~/.claude/skills/flash/flash.sh --build > /tmp/build.log 2>&1
if ! grep -q "Program Succeeded" /tmp/build.log; then
  echo "[BUILD FAIL]"; tail -10 /tmp/build.log; exit 2
fi
sleep 3
DAQIFI_DEV=$(bash "${SCRIPT_DIR}/find_bench_device.sh" 2>/dev/null)
[ -z "$DAQIFI_DEV" ] && { echo "[NO DEV]"; exit 3; }

python3 -c "
import serial, time
s = serial.Serial('$DAQIFI_DEV', 115200, timeout=1.0); time.sleep(0.5); s.reset_input_buffer()
s.write(b'SYST:POW:STAT 1\r'); time.sleep(2.0); s.read_all()
for ch in range(16): s.write(f'ENA:VOLT:DC {ch},1\r'.encode()); time.sleep(0.05)
s.read_all()
s.write(b'SYST:STR:FOR 2\r'); time.sleep(0.2); s.write(b'SYST:STR:INT 0\r'); time.sleep(0.2); s.read_all()
s.write(b'SYST:StartStreamData 2\r'); time.sleep(4.0)
s.write(b'SYST:StopStreamData\r'); time.sleep(0.8)
raw = s.read_all().decode(errors='replace')
for line in raw.splitlines():
    parts = line.strip().split(',')
    if len(parts) == 32:
        try:
            row = [float(p) for p in parts]
            nz = sum(1 for i in range(16) if abs(row[2*i+1]) > 50)
            if nz > 0:
                if nz == 16: print('FIXED 16/16')
                else: print(f'broken {nz}/16')
                break
        except ValueError: pass
s.close()
"
