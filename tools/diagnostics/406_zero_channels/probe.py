#!/usr/bin/env python3
"""Probe device state step-by-step and report what each command returns."""
import serial, sys, time
dev = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyACM0'
s = serial.Serial(dev, 115200, timeout=1.0)
time.sleep(0.5)
s.reset_input_buffer()

def q(cmd, wait=0.5):
    s.write((cmd + '\r').encode())
    time.sleep(wait)
    r = s.read_all().decode(errors='replace')
    return r.replace('\r', '\\r').replace('\n', '\\n')[-200:]

print('IDN: ', q('*IDN?'))
print('PWR: ', q('SYST:POW:STAT?'))
print('PWR1:', q('SYST:POW:STAT 1', wait=2.0))
print('PWR?:', q('SYST:POW:STAT?'))
print('en0: ', q('ENA:VOLT:DC 0,1'))
print('en?:0', q('ENA:VOLT:DC? 0'))
print('FOR2:', q('SYST:STR:FOR 2'))
print('INT0:', q('SYST:STR:INT 0'))
print('start:', q('SYST:StartStreamData 2', wait=3.5))
# read raw stream
time.sleep(0.5)
raw = s.read_all()
s.write(b'SYST:StopStreamData\r')
time.sleep(0.5)
raw += s.read_all()
print(f'raw bytes: {len(raw)}, sample: {raw[:200]!r}')
print('err: ', q('SYST:ERR?'))
s.close()
