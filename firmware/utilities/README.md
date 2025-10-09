# Firmware Utilities

This directory contains automated testing, validation, and maintenance tools for the DAQiFi Nyquist firmware.

## Requirements

- Python 3.x
- pyserial library: `pip install pyserial`

---

## validate.py

Comprehensive automated test script for validating firmware functionality on NQ1 and NQ3 hardware.

### Usage

```bash
# Quick test (no report files saved)
python validate.py COM3

# Full test with report files saved
python validate.py COM3 --save-reports
```

### Features

- Auto-detects board variant (NQ1/NQ3)
- Tests power management, DAC, ADC (public/private), DIO, streaming
- Displays real-time device information (HW/FW versions, channel counts)
- Shows all ADC readings (public channels + internal monitoring/voltage rails)
- Optional report generation with SCPI logs and hex dumps

### Test Coverage

1. Power & Identification - Verifies device powers up and reports correct identity
2. DAC Configuration - Tests DAC7718 functionality (NQ3 only)
3. ADC Configuration - Verifies ADC channels are visible and configurable
4. ADC Readings - Displays all public and private channel values
5. Internal Monitoring - Verifies voltage rail monitoring is active
6. Streaming - Tests data streaming without heap exhaustion
7. Error Queue - Verifies no errors accumulated during testing
8. Final Status - Confirms device in stable state

### Exit Codes

- 0: All tests passed
- 1: One or more tests failed
- 2: Test interrupted by user

---

## update_wifi_firmware.py

Automated WiFi module firmware updater for the WINC1500 chip (firmware version 19.7.7).

### Usage

```bash
# Auto-detect winc_flash_tool.cmd location
python update_wifi_firmware.py COM3

# Specify custom firmware version
python update_wifi_firmware.py COM3 --firmware-version 19.7.7

# Specify custom tool path
python update_wifi_firmware.py COM3 --tool-path "C:\path\to\winc_flash_tool.cmd"
```

### Features

- Fully automated 4-step update process
- Auto-detects winc_flash_tool.cmd in MCC Harmony installation
- Verifies firmware version after update
- Real-time progress feedback

### Update Process

1. **Prepare Device** - Powers up and enters firmware update mode
   - `SYST:POW:STAT 1`
   - `SYST:COMM:LAN:FWUpdate`
   - `SYST:COMM:LAN:APPLY`

2. **Flash Firmware** - Runs winc_flash_tool.cmd
   - `/p COM3 /d WINC1500 /v 19.7.7 /x /e /i aio /w`

3. **Restore Operation** - Reconnects and restores WiFi
   - `SYST:USB:SetTransparentMode 0`
   - `SYST:COMM:LAN:ENabled 1`
   - `SYST:COMM:LAN:APPLY`

4. **Verify** - Checks firmware version via `SYST:COMM:LAN:GETChipInfo?`

### Default Tool Location

The script automatically searches for `winc_flash_tool.cmd` in:
- `C:\Users\<username>\.mcc\harmony\content\wireless_wifi\v3.12.1\utilities\wifi\winc\`
- Current directory
- Script directory
- User Documents folder

### Exit Codes

- 0: Firmware update successful and verified
- 1: Update failed or verification unsuccessful

---

## Notes

- Both scripts require exclusive access to the serial port (close other applications like PuTTY)
- The WiFi updater requires MPLAB Harmony to be installed at the default location (`C:\Users\<username>\.mcc\harmony\`)
- Device must be connected via USB and powered before running scripts
