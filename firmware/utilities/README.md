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

The script performs a fully automated 4-step update. This is also the canonical manual procedure when running the steps yourself via PuTTY:

1. **Prepare Device** — Connect PuTTY (115200 8-N-1) to the COM port and issue:
   ```
   SYSTem:POWer:STATe 1                # Power up
   SYSTem:COMMunicate:LAN:FWUpdate     # Enter WINC firmware update mode
                                       # (engages USB CDC transparent mode via wifi_serial_bridge_interface)
   SYSTem:COMMunicate:LAN:APPLY        # Apply — SCPI parsing is now bypassed
   ```
   At this point USB CDC is forwarding raw bytes to the WINC SPI bus; no SCPI prompt will respond.

2. **Disconnect PuTTY** (free the COM port for the WINC tool), then run:
   ```
   .\winc_flash_tool.cmd /p COM3 /d WINC1500 /v 19.7.7 /x /e /i aio /w
   ```
   or with the full Harmony path:
   ```
   C:\Users\<username>\.mcc\harmony\content\wireless_wifi\v3.12.1\utilities\wifi\winc\winc_flash_tool.cmd /p COM3 /d WINC1500 /v 19.7.7 /x /e /i aio /w
   ```

3. **Reconnect PuTTY** and exit transparent mode:
   ```
   SYSTem:USB:TRANSparent:MODE 0       # Exit transparent mode (canonical, #311 round 3)
                                       # Legacy alias also works:
                                       #   SYSTem:USB:SetTransparentMode 0
                                       # Any valid SCPI keyword abbreviation of either is accepted
                                       # (e.g. SYST:USB:TRANS:MODE 0, syst:usb:trans:mode 0). The
                                       # raw detector in UsbCdc_FinalizeRead implements IEEE 488.2
                                       # §6.1.4.1 abbreviation rules so the keyword form you use
                                       # in normal SCPI also works here while parsing is bypassed.
   SYSTem:COMMunicate:LAN:ENabled 1    # Re-enable WiFi
   SYSTem:COMMunicate:LAN:APPLY        # Apply
   ```

4. **Verify** — confirm the new WINC firmware version:
   ```
   SYSTem:COMMunicate:LAN:GETChipInfo?
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
