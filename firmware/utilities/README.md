# Firmware Utilities

This directory contains maintenance utilities for the DAQiFi Nyquist firmware.

## Testing and Validation

**For comprehensive firmware testing**, use the dedicated test suite repository:
- **Repository**: https://github.com/daqifi/daqifi-python-test-suite
- **Location**: `C:\Users\User\Documents\GitHub\daqifi-python-test-suite`
- **Features**: Automated validation, USB CDC tests, YAML-configured test scenarios

---

## update_wifi_firmware.py

Automated WiFi module firmware updater for the WINC1500 chip.

### Purpose

Updates the WINC1500 WiFi module to firmware version 19.7.7. This utility is needed when:
- First-time WiFi module initialization
- Recovering from corrupted WiFi firmware
- Upgrading to newer WiFi firmware versions

### Requirements

```bash
pip install pyserial
```

### Usage

```bash
# Auto-detect winc_flash_tool.cmd location
python update_wifi_firmware.py COM3

# Specify custom firmware version
python update_wifi_firmware.py COM3 --firmware-version 19.7.7

# Specify custom tool path
python update_wifi_firmware.py COM3 --tool-path "C:\path\to\winc_flash_tool.cmd"
```

### Update Process

The script performs a fully automated 4-step update:

1. **Prepare Device** - Powers up and enters firmware update mode
   ```
   SYST:POW:STAT 1              # Power up device
   SYST:COMM:LAN:FWUpdate       # Enter update mode
   SYST:COMM:LAN:APPLY          # Apply settings
   ```

2. **Flash Firmware** - Runs winc_flash_tool.cmd
   ```
   winc_flash_tool.cmd /p COM3 /d WINC1500 /v 19.7.7 /x /e /i aio /w
   ```

3. **Restore Operation** - Reconnects and restores WiFi
   ```
   SYST:USB:SetTransparentMode 0  # Exit transparent mode
   SYST:COMM:LAN:ENabled 1        # Re-enable WiFi
   SYST:COMM:LAN:APPLY            # Apply settings
   ```

4. **Verify** - Confirms firmware version
   ```
   SYST:COMM:LAN:GETChipInfo?  # Check WiFi chip info
   ```

### Default Tool Location

The script automatically searches for `winc_flash_tool.cmd` in:
- `C:\Users\<username>\.mcc\harmony\content\wireless_wifi\v3.12.1\utilities\wifi\winc\`
- Current directory
- Script directory
- User Documents folder

If not found, specify with `--tool-path` option.

### Harmony Installation

The WiFi flash tool is included with MPLAB Harmony. If you don't have it:

1. **Install MPLAB Harmony** via MPLAB X IDE:
   - Tools → Plugins → Available Plugins → MPLAB Harmony Configurator
   - Or download from: https://microchipdeveloper.com/harmony3:mhc-overview

2. **Download wireless_wifi package**:
   - Harmony Content Manager will download to: `C:\Users\<username>\.mcc\harmony\content\wireless_wifi\`

### Exit Codes

- `0`: Firmware update successful and verified
- `1`: Update failed or verification unsuccessful

### Troubleshooting

**Serial Port Access Error**:
- Close any applications using the COM port (PuTTY, TeraTerm, etc.)
- Ensure device is connected and powered

**winc_flash_tool.cmd Not Found**:
- Install MPLAB Harmony via MPLAB X IDE
- Manually specify path with `--tool-path` option

**Update Fails During Flash**:
- Ensure device is in POWERED_UP state before starting
- Check USB connection is stable
- Try manual update via MPLAB X → Tools → WINC Firmware Update

---

## Notes

- Device must be connected via USB and powered before running utilities
- Utilities require exclusive access to the serial port
- For automated firmware validation and testing, use the dedicated test suite repository
