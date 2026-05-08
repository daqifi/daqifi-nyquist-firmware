#!/usr/bin/env python3
import serial, sys, time
dev = sys.argv[1] if len(sys.argv) > 1 else '/dev/ttyACM0'
s = serial.Serial(dev, 115200, timeout=2.0)
time.sleep(0.5)
s.reset_input_buffer()
s.write(b'*IDN?\r')
time.sleep(0.5)
print(repr(s.read_all()))
s.close()
