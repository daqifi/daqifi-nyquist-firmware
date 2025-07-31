# Status Display Discrepancies Found During Testing

## 1. Battery Voltage Reading Issue
**Problem**: Battery voltage/percentage calculation is incorrect
- **Displayed**: 3.70V (50%)
- **Actual**: 4.05V (should be ~75-80% for Li-ion)
- **Impact**: Users see incorrect battery status

## 2. USB Connection Status Issue  
**Problem**: USB status shows "Disconnected" when USB is connected
- **Displayed**: "USB: Disconnected"
- **Actual**: USB is connected and communicating via SCPI
- **Evidence**: We're sending commands over USB CDC
- **Impact**: Misleading connectivity status

## 3. External Power Status Issue
**Problem**: Inconsistent external power reporting
- **Displayed**: "Ext Power: None" 
- **Actual**: pgStat: 1 (indicates external power IS present)
- **Discrepancy**: Two different parts of the status report contradict each other
- **Impact**: Confusing power source information

## Root Cause Analysis

### Battery Voltage Issue
- Check ADC calibration for VBATT channel
- Review voltage-to-percentage conversion function
- Verify ADC reference voltage

### USB Status Issue  
- USB detection logic may be checking wrong source
- Could be reading stale or uninitialized data
- Need to verify USB state detection in BoardData

### External Power Issue
- Status display logic differs from BQ24297 register reading
- "Ext Power" field not properly synced with pgStat
- Display formatting issue in SYST:INFO command

## 4. SCPI Command Naming Issue
**Problem**: SYST:BAT:STAT? returns external power source, not battery status
- **Command**: SYST:BAT:STAT?
- **Returns**: 0 (NO_EXT_POWER) 
- **Should be named**: SYST:POW:SRC? or similar
- **Confusion**: Users expect battery status, get power source type

## 5. External Power Detection Inconsistency
**Problem**: Multiple sources disagree on external power presence
- **externalPowerSource**: NO_EXT_POWER (0)
- **pgStat**: 1 (power good, external power present)
- **Actual**: USB is connected and providing power
- **Root cause**: Power_Update_Settings() may not be detecting USB correctly

## Code Locations to Check
1. Battery voltage: `Power_UpdateChgPct()` in PowerApi.c - line ~450
2. USB status: USB detection logic in system status formatting
3. External power display: SCPI status formatting in SCPIInterface.c
4. SCPI naming: `SCPI_BatteryStatusGet()` in SCPIInterface.c - line 638
5. Power detection: `Power_Update_Settings()` in PowerApi.c - line ~485-512