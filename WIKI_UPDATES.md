# Wiki Updates Required for SCPI Interface

## Power Commands Section Update

The current Power Commands section (lines 27-34) needs to be updated with the following changes:

### 1. Power State Command Clarification

**Current:**
```markdown
| SYSTem:POWer:STATe? | | | SCPI_GetPowerState |
| SYSTem:POWer:STATe | | | SCPI_SetPowerState |
```

**Updated:**
```markdown
| SYSTem:POWer:STATe? | Get power state (0=standby, 1=powered) | | SCPI_GetPowerState |
| SYSTem:POWer:STATe | Set power state (0=standby, 1=powered) | SYSTem:POWer:STATe 1 | SCPI_SetPowerState |
```

### 2. Add Missing OTG Commands

Add these commands after the existing power commands:

```markdown
| SYSTem:POWer:OTG? | Get OTG boost mode status (0=off, 1=on) | | SCPI_GetOTGMode |
| SYSTem:POWer:OTG | Set OTG boost mode (0=off, 1=on) | SYSTem:POWer:OTG 1 | SCPI_SetOTGMode |
| SYSTem:FORce5V5POWer:STATe | Enable/disable external 5V supply | | SCPI_Force5v5PowerStateSet |
```

### 3. Add New System Info Command

Add to System Commands section (after line 15):

```markdown
| SYSTem:INFo? | Get human-readable system information including power state, battery status, network config | | SCPI_SystemInfo |
```

### 4. Battery Status Command Clarification

**Current:**
```markdown
| SYSTem:BAT:STAT? | | | SCPI_BatteryStatusGet |
```

**Updated:**
```markdown
| SYSTem:BAT:STAT? | Get external power source type (0=none, 1=unknown, 2=1A charger, 3=2A charger, 4=USB 100mA, 5=USB 500mA) | | SCPI_BatteryStatusGet |
```

Note: This command is misnamed - it actually returns external power source type, not battery status.

## Complete Updated Power Commands Section

```markdown
## Power Commands
| SCPI Command | Description | Example | Firmware Callback |
| -- | -- | -- | -- |
| SYSTem:BAT:STAT? | Get external power source type (0=none, 1=unknown, 2=1A charger, 3=2A charger, 4=USB 100mA, 5=USB 500mA) | | SCPI_BatteryStatusGet |
| SYSTem:BAT:LEVel? | Get battery charge level percentage | | SCPI_BatteryLevelGet |
| SYSTem:POWer:STATe? | Get power state (0=standby, 1=powered) | | SCPI_GetPowerState |
| SYSTem:POWer:STATe | Set power state (0=standby, 1=powered) | SYSTem:POWer:STATe 1 | SCPI_SetPowerState |
| SYSTem:POWer:OTG? | Get OTG boost mode status (0=off, 1=on) | | SCPI_GetOTGMode |
| SYSTem:POWer:OTG | Set OTG boost mode (0=off, 1=on) | SYSTem:POWer:OTG 1 | SCPI_SetOTGMode |
| SYSTem:FORce5V5POWer:STATe | Enable/disable external 5V supply | | SCPI_Force5v5PowerStateSet |
```

## Additional Notes

1. The power state enum was simplified from 4 states to 3:
   - STANDBY = 0 (replaces both POWERED_DOWN and MICRO_ON)
   - POWERED_UP = 1
   - POWERED_UP_EXT_DOWN = 2

2. The SCPI interface now returns binary values for power state:
   - 0 = standby/off
   - 1 = any powered state (POWERED_UP or POWERED_UP_EXT_DOWN)

3. OTG mode is now manually controlled via SCPI commands instead of automatic transitions

4. The SYSTem:INFo? command provides detailed status including actual power state names

## System Commands Update

Add this command to the System Commands section:

```markdown
| SYSTem:USB:SetTransparentMode | Set USB transparent mode (0=SCPI mode, 1=transparent mode) | SYSTem:USB:SetTransparentMode 0 | SCPI_UsbSetTransparentMode |
```

This command allows switching between SCPI command mode and transparent pass-through mode for USB communication.