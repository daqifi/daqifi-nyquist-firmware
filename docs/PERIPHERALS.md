# Peripheral integration

> Split out of `CLAUDE.md` on 2026-09-10. That file is loaded into **every turn of
> every agent**, and at 38k tokens it was ~13% of all token spend; this material is
> reference — needed when you work in this area, not on every task. **It is
> unchanged, not summarised.** Read it in full before changing anything it
> describes, and update it here rather than re-adding it to `CLAUDE.md`.

DAC7718 (NQ3) and the BQ24297 IINLIM state machine.

---

### DAC7718 Integration (NQ3 Board)

The NQ3 board variant includes a DAC7718 8-channel 12-bit DAC for analog output functionality:

### Hardware Configuration
- **SPI Interface**: SPI2 at 10 MHz (configurable)
- **Control Pins**: 
  - CS (Chip Select): RK0 (GPIO_PIN_RK0)
  - CLR/RST (Reset): RJ13 (GPIO_PIN_RJ13) 
  - LDAC: Tied to 3.3V (no GPIO control)
- **Power Requirements**: Requires 10V rail (available in POWERED_UP state)
- **Channels**: 8 analog output channels (0-7)
- **Resolution**: 12-bit (4096 levels)

### SCPI Commands for DAC Control
```bash
# Set DAC channel voltage
SOURce:VOLTage:LEVel 0,5.0        # Set channel 0 to 5.0V
SOURce:VOLTage:LEVel 5.0          # Set all channels to 5.0V

# Read DAC channel voltage  
SOURce:VOLTage:LEVel? 0           # Read channel 0 voltage
SOURce:VOLTage:LEVel?             # Read all channel voltages

# Channel enable/disable
ENAble:SOURce:DC 0,1              # Enable channel 0
ENAble:SOURce:DC? 0               # Get channel 0 enable status

# Configuration commands
CONFigure:DAC:RANGe 0,1           # Set channel 0 range
CONFigure:DAC:UPDATE              # Update all DAC outputs

# Calibration commands
CONFigure:DAC:chanCALM 0,1.0      # Set channel 0 calibration slope
CONFigure:DAC:chanCALB 0,0.0      # Set channel 0 calibration offset
CONFigure:DAC:SAVEcal             # Save user calibration values
CONFigure:DAC:LOADcal             # Load user calibration values
```

### Integration Points
- **Board Configuration**: `state/board/AOutConfig.h` - Analog output configuration structure
- **SCPI Module**: `services/SCPI/SCPIDAC.c` - DAC command implementation  
- **HAL Driver**: `HAL/DAC7718/DAC7718.c` - Hardware abstraction layer
- **Runtime Config**: `state/runtime/AOutRuntimeConfig.h` - Runtime channel states

### Power-Safe Initialization
The DAC7718 uses lazy initialization that:
1. Initializes global structures at startup (power-independent)
2. Defers hardware initialization until first DAC command when power is sufficient
3. Checks for POWERED_UP state (provides 10V rail) before hardware access
4. Gracefully handles power state transitions

### BQ24297 IINLIM Management

The firmware uses a timer-based state machine to manage input current limits (IINLIM) on the BQ24297 battery management IC, replacing the earlier DPDM-dependent approach.

### State Machine
```
IDLE → WAIT_DPDM (1s min, 3s timeout) → WAIT_USB (5s) → SETTLED
```

- **OTG pin** driven LOW (output) so DPDM starts at conservative 100mA
- **WAIT_DPDM**: Waits for BQ24297 DPDM detection to complete (REG07 bit 7 clears)
- **WAIT_USB**: Sets 500mA, waits 5s checking `UsbCdc_IsConfigured()`
- **SETTLED**: USB host → 500mA; wall charger → 2000mA
- VBUS loss in any state resets to IDLE

### SCPI Diagnostic Commands
```bash
SYST:POW:BQ:REGisters?     # Dump all BQ24297 registers (REG00-REG0A hex values)
SYST:POW:BQ:ILIM <0-7>     # Set IINLIM directly (0=100mA, 2=500mA, 6=2A, 7=3A)
SYST:POW:BQ:DPDM           # Force DPDM re-detection (WARNING: disrupts USB — see below)
SYST:POW:BQ:DIAGnostics?   # Comprehensive diagnostics dump (battery, registers,
                            # GPIO, IINLIM state machine, power state, VBUS level)
```

**Note:** `SYST:POW:BQ:DIAG?` calls `BQ24297_UpdateStatus()` which reads REG09 and clears latched fault flags as a side effect.

**WARNING:** `SYST:POW:BQ:DPDM` is diagnostic-only. It forces the BQ24297 to re-run D+/D- detection and prints the result (VBUS type from REG08), but does **not** update the status struct or reset the IINLIM state machine — no automatic current switching occurs. Use `SYST:POW:BQ:ILIM` to manually set IINLIM after forced DPDM. When connected via USB, DPDM temporarily resets IINLIM (potentially to 100mA), causing VBUS sag and USB disconnect. A physical cable replug is required to recover and restart the IINLIM state machine.

### Implementation
- **State machine**: `HAL/BQ24297/BQ24297.c` — `BQ24297_ManageIINLIM()`
- **Caller**: `HAL/Power/PowerApi.c` — `Power_Tasks()` (~100ms interval)
- **USB tracking**: `services/UsbCdc/UsbCdc.c` — `UsbCdc_IsConfigured()`
- **SCPI commands**: `services/SCPI/SCPIInterface.c`
- **I2C mutex**: `BQ24297_Read_I2C()` and `BQ24297_Write_I2C()` are protected by a FreeRTOS mutex to synchronize access between PowerAndUITask and USBDeviceTask (both priority 7)
