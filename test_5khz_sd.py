#!/usr/bin/env python3
"""
Test 5kHz ADC logging to SD card on NQ1 board
Monitors for errors at all stages: ADC -> Queue -> SD write
"""

import serial
import time
import sys

# Configuration
SERIAL_PORT = 'COM3'  # Adjust to your COM port
BAUD_RATE = 115200
TIMEOUT = 2.0

class DAQiFiTester:
    def __init__(self, port, baud=115200):
        self.port = port
        self.baud = baud
        self.ser = None

    def connect(self):
        """Connect to DAQiFi device"""
        try:
            self.ser = serial.Serial(
                port=self.port,
                baudrate=self.baud,
                timeout=TIMEOUT,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE
            )
            time.sleep(0.5)  # Wait for connection to stabilize
            # Flush any stale data
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()
            print(f"[OK] Connected to {self.port}")
            return True
        except serial.SerialException as e:
            print(f"[ERROR] Failed to connect to {self.port}: {e}")
            return False

    def send_command(self, cmd, delay=0.5):
        """Send SCPI command and return response"""
        if not self.ser or not self.ser.is_open:
            print("[ERROR] Serial port not open")
            return None

        try:
            # Clear buffers
            self.ser.reset_input_buffer()

            # Send command
            self.ser.write(f"{cmd}\r\n".encode('utf-8'))
            self.ser.flush()

            # Wait for response
            time.sleep(delay)

            # Read response
            response = b''
            while self.ser.in_waiting > 0:
                response += self.ser.read(self.ser.in_waiting)
                time.sleep(0.1)

            # Decode and clean up
            response_str = response.decode('utf-8', errors='ignore').strip()
            return response_str
        except Exception as e:
            print(f"[ERROR] Error sending command '{cmd}': {e}")
            return None

    def query(self, cmd, delay=0.5):
        """Send query command and extract result"""
        response = self.send_command(cmd, delay)
        if response:
            # Extract result (last line usually has the answer)
            lines = [l.strip() for l in response.split('\n') if l.strip()]
            if lines:
                return lines[-1]
        return None

    def check_errors(self):
        """Check and clear error queue"""
        errors = []
        for _ in range(10):  # Max 10 errors to prevent infinite loop
            err = self.query("SYST:ERR?")
            if err and "No error" not in err:
                errors.append(err)
            else:
                break
        return errors

    def close(self):
        """Close serial connection"""
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("[OK] Connection closed")

def run_5khz_test(tester, channel=4, duration_sec=10):
    """
    Run 5kHz streaming test on a single Type 1 channel

    Type 1 channels on NQ1:
    - A4  (MODULE4) - recommended
    - A8  (MODULE0)
    - A10 (MODULE1)
    - A12 (MODULE2)
    - A14 (MODULE3)
    """
    print(f"\n{'='*60}")
    print(f"5kHz SD Logging Test - Channel {channel}")
    print(f"{'='*60}\n")

    # Step 1: Power up
    print("Step 1: Powering up device...")
    tester.send_command("SYST:POW:STAT 1", delay=2.0)
    time.sleep(1)

    # Check initial errors
    errors = tester.check_errors()
    if errors:
        print(f"[WARN] Initial errors: {errors}")

    # Step 2: Configure streaming
    print("\nStep 2: Configuring streaming...")
    print("  - Format: CSV (for easy verification)")
    tester.send_command("SYSTem:STReam:FORmat 2")  # 2=CSV

    print("  - Destination: SD card")
    tester.send_command("SYSTem:STReam:DEST 1")  # 1=SD

    # Step 3: Enable only the test channel
    print(f"\nStep 3: Enabling Channel {channel} only...")
    tester.send_command(f"ENAble:AIN:CHANnel {channel},1")

    # Verify
    enabled = tester.query(f"ENAble:AIN:CHANnel? {channel}")
    print(f"  Channel {channel} enabled: {enabled}")

    # Step 4: Start streaming
    print(f"\nStep 4: Starting 5kHz streaming...")
    expected_samples = duration_sec * 5000
    print(f"  Expected samples: ~{expected_samples:,} ({duration_sec}s × 5000Hz)")

    tester.send_command("SYST:StartStreamData 5000", delay=1.0)

    # Monitor during streaming
    print(f"\nStep 5: Streaming for {duration_sec} seconds...")
    for i in range(duration_sec):
        print(f"  [{i+1}/{duration_sec}] seconds elapsed...", end='\r')
        time.sleep(1)
    print(f"\n  [OK] Streaming duration complete")

    # Stop streaming
    print("\nStep 6: Stopping stream...")
    tester.send_command("SYST:StopStreamData", delay=1.0)

    # Check for errors
    print("\nStep 7: Checking for errors...")
    errors = tester.check_errors()
    if errors:
        print("  [ERROR] ERRORS DETECTED:")
        for err in errors:
            print(f"    - {err}")
        return False
    else:
        print("  [OK] No errors in queue")

    # Test a manual read to verify ADC still works
    print("\nStep 8: Verifying ADC still responsive...")
    voltage = tester.query(f"MEASure:VOLTage:DC? {channel}")
    print(f"  Channel {channel} voltage: {voltage}")

    print("\n" + "="*60)
    print("TEST COMPLETE")
    print("="*60)
    print("\nNext Steps:")
    print("1. Remove SD card and check CSV file")
    print(f"2. Verify ~{expected_samples:,} samples in file")
    print("3. Check for timestamp gaps or duplicate timestamps")
    print("4. Look for any error messages in the CSV")
    print("\nIf successful:")
    print(f"  - Test 2 channels at 5kHz (10kHz total)")
    print(f"  - Test 3 channels at 5kHz (15kHz total - at Type 1 limit)")

    return True

def main():
    print("DAQiFi NQ1 - 5kHz SD Logging Test")
    print("="*60)

    # Allow command line port override
    port = sys.argv[1] if len(sys.argv) > 1 else SERIAL_PORT

    tester = DAQiFiTester(port, BAUD_RATE)

    if not tester.connect():
        sys.exit(1)

    try:
        # Run test
        success = run_5khz_test(tester, channel=4, duration_sec=10)

        if success:
            print("\n[OK] Test completed successfully")
            sys.exit(0)
        else:
            print("\n[ERROR] Test failed - check errors above")
            sys.exit(1)

    except KeyboardInterrupt:
        print("\n\nTest interrupted by user")
        tester.send_command("SYST:StopStreamData")
        sys.exit(1)
    finally:
        tester.close()

if __name__ == "__main__":
    print("\nUsage: python test_5khz_sd.py [COM_PORT]")
    print(f"Default COM port: {SERIAL_PORT}\n")

    # Check if pyserial is installed
    try:
        import serial
    except ImportError:
        print("ERROR: pyserial not installed")
        print("Install with: pip install pyserial")
        sys.exit(1)

    main()
