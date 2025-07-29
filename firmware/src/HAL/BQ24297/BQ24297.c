/* 
 * @file   BQ24297.c
 * @brief This file manages the BQ24297 module
 */
#include "BQ24297.h"
#include "Util/Logger.h"
#include "services/UsbCdc/UsbCdc.h"
#include "services/UsbReconnection/UsbReconnection.h"
#include "peripheral/gpio/plib_gpio.h"

//! Pointer to the BQ24297 device configuration data structure
static tBQ24297Config *pConfigBQ24;
//! Pointer to the BQ24297 device "Write Variables" data structure
static tBQ24297WriteVars *pWriteVariables;
//! Pointer to the data structure where the BQ24297 data is
static tBQ24297Data *pData;

/*! Funtion to read BQ24297 data by I2C communication
 * @param[in] reg Register to read
 */
uint8_t BQ24297_Read_I2C(uint8_t reg);

/*! Funtion to write BQ24297 data by I2C communication
 * @param[in] reg Register to read
 * @param[in] txData Data to write
 * @return true if write succeeded, false on error
 */
bool BQ24297_Write_I2C(uint8_t reg, uint8_t txData);

void BQ24297_InitHardware(                                                  
                        tBQ24297Config *pConfigInit,                        
                        tBQ24297WriteVars *pWriteInit,                      
                        tBQ24297Data *pDataInit) {
    pConfigBQ24 = pConfigInit;
    pWriteVariables = pWriteInit;
    pData = pDataInit;
    // Battery management initialization (hardware interface)

    // ***Disable I2C calls*** as Harmony 2.06 doesn't have a working, 
    // interrupt-safe implementation
    // Open the I2C Driver for Master
    pData->I2C_Handle = DRV_I2C_Open(                                       
                        pConfigBQ24->I2C_Index,                             
                        DRV_IO_INTENT_READWRITE|DRV_IO_INTENT_BLOCKING);

    // Set I/O such that we can power up when needed
    LOG_D("BQ24297_InitHardware: Setting OTG GPIO - Port=%d, Bit=%d, Val=%d", 
          pConfigBQ24->OTG_Ch, pConfigBQ24->OTG_Bit, pWriteVariables->OTG_Val);
    
    /* CRITICAL POWER MANAGEMENT LESSONS LEARNED:
     * 
     * 1. GPIO INITIALIZATION:
     *    - The GPIO init (plib_gpio.c) sets LATK = 0x30U, which means RK5 starts HIGH
     *    - This enables OTG by default, ensuring battery power is available at startup
     *    - DO NOT clear OTG here - it will cause immediate power loss on battery
     * 
     * 2. HARDWARE REALITY vs THEORY:
     *    - THEORY: OTG is only needed to boost battery voltage to 5V for USB host
     *    - REALITY: Device loses power without OTG when running on battery
     *    - Root cause: OTG keeps BATFET enabled, providing battery-to-system path
     *    - The 3.3V buck/boost regulator can handle 3V+ input, voltage isn't the issue
     *    - Without OTG enabled, BATFET may disconnect battery from system
     * 
     * 3. WHY THE "BROKEN" CODE WORKED:
     *    - Commit d39a432d changed OTG control from RF5 to RK5
     *    - RF5 is the I2C SCL pin - toggling it corrupted I2C communication
     *    - When I2C was corrupted, BQ24297 init failed, leaving OTG in default state
     *    - Default state = OTG enabled = device stayed powered on battery
     * 
     * 4. TIMING IS CRITICAL:
     *    - USB disconnect event → OTG must be enabled IMMEDIATELY
     *    - Any delay (even 50-150ms) causes power loss
     *    - Cannot wait for Power_Tasks or other state machines
     * 
     * 5. BQ24297 LIMITATIONS:
     *    - Cannot detect USB power (pgStat) when OTG is enabled
     *    - This makes USB reconnection detection challenging
     *    - Disabling OTG to check for USB risks power loss
     * 
     * 6. MCU VBUS DETECTION IS UNRELIABLE:
     *    - Residual voltage on VBUS causes false detection after disconnect
     *    - Cannot be trusted for power management decisions
     *    - Use only for debug logging
     */
    
    // GPIO control for OTG pin (RK5)
    BATT_MAN_OTG_OutputEnable();  // Make sure it's an output
    
    // Start with OTG DISABLED (GPIO LOW) since we're likely powered by USB at startup
    // OTG pin is ACTIVE HIGH - HIGH = boost mode enabled, LOW = normal charging
    // We'll enable it only if we detect actual USB disconnect
    BATT_MAN_OTG_Clear();  // Set LOW = OTG disabled = allow charging
    LOG_D("BQ24297_InitHardware: OTG GPIO initialized LOW (OTG disabled)");
}

void BQ24297_Config_Settings(void) {
    // Temporary value to hold current register value
    uint8_t reg = 0;
    
    LOG_D("BQ24297_Config_Settings: Starting initialization");
    
    // CRITICAL: Log all registers at startup for debugging
    LOG_E("BQ24297_Config_Settings: Initial register dump:");
    for (int i = 0; i <= 0x0A; i++) {
        reg = BQ24297_Read_I2C(i);
        LOG_E("  REG%02X = 0x%02X", i, reg);
    }
    
    // Read initial status without changing OTG state
    // This preserves the power path if OTG is already enabled
    BQ24297_UpdateStatus();

    // At this point, the chip has evaluated the power source, so we should get the current limit
    // and save it when writing to register 0
    reg = BQ24297_Read_I2C(0x00);
    
    // CRITICAL: Always clear HIZ mode on initialization - it can cause power loss!
    if (reg & 0x80) {
        LOG_E("BQ24297_Config_Settings: HIZ mode detected (REG00=0x%02X), clearing!", reg);
    }

    // Set input voltage limit to 3.88V (minimum): VINDPM = 0
    // This gives maximum headroom before input voltage regulation kicks in
    // REG00: [7]=0 (CLEAR HIZ), [6:3]=0000 (3.88V min), [2:0]=keep current limit
    BQ24297_Write_I2C(0x00, reg & 0b00000111);  // This also clears HIZ bit
    
    // Verify HIZ was cleared
    reg = BQ24297_Read_I2C(0x00);
    if (reg & 0x80) {
        LOG_E("BQ24297_Config_Settings: WARNING - HIZ mode still active after clear attempt!");
    }

    // Power Mode Configuration
    // 
    // According to BQ24297 datasheet section 9.3.1.2.1:
    // - BATFET automatically connects battery to system when battery > depletion threshold
    // - OTG mode is only needed to boost battery voltage to 5V for USB host functionality
    // - BATFET can be manually disabled via REG07 bit 5 (we ensure this is cleared)
    // 
    // If device powers off on USB disconnect, check:
    // 1. BATFET is enabled (REG07 bit 5 = 0)
    // 2. No BATFET fault (REG09 bit 3)
    // 3. Battery voltage > depletion threshold (~2.5V)
    // 4. No overcurrent condition causing BATFET shutdown
    
    // Check current power status to decide on OTG mode
    BQ24297_UpdateStatus();
    
    // OTG GPIO starts disabled (LOW) from init
    // Check if we need to enable it because USB is NOT connected
    
    // Read actual hardware state
    bool otgGpioEnabled = BATT_MAN_OTG_Get(); // HIGH = enabled (active high)
    bool otgRegEnabled = pData->status.otg;
    
    // Check for external power
    // Note: pgStat might not work if OTG is enabled, so also check vBusStat
    bool hasExternalPower = pData->status.pgStat || 
                           (pData->status.vBusStat == 0x01) ||  // USB host
                           (pData->status.vBusStat == 0x02);     // Adapter
    
    // Log MCU VBUS for debug only
    bool usbVbusDebug = UsbCdc_IsVbusDetected();
    LOG_D("BQ24297_Config_Settings: pgStat=%d, vBusStat=%d, otgGpio=%d, otgReg=%d [MCU_VBUS_DEBUG=%d]",
          pData->status.pgStat, pData->status.vBusStat, otgGpioEnabled, otgRegEnabled, usbVbusDebug);
    
    // If we detect external power, we can safely disable OTG
    if (hasExternalPower || usbVbusDebug) {
        LOG_D("BQ24297_Config_Settings: External power detected - will disable OTG for charging");
        // We'll disable OTG below in the register configuration
    } else {
        LOG_D("BQ24297_Config_Settings: No external power - keeping OTG enabled");
    }
    
    // Configure REG01 based on power status
    // REG01 bits:
    // [7:6] = 01 (reset watchdog)
    // [5] = OTG (0=disable, 1=enable)
    // [4] = CHARGE ENABLE (0=disable, 1=enable)
    // [3:1] = 000 (SYS_MIN = 3.0V)
    // [0] = 1 (reserved)
    
    // Configure power mode based on external power detection
    // GPIO starts LOW (OTG disabled) from init
    // Note: CE pin is grounded (always LOW) for charging to work
    
    if (hasExternalPower || usbVbusDebug) {
        // External power detected - disable OTG and enable charging
        // Set GPIO LOW to disable OTG (active high logic)
        BATT_MAN_OTG_OutputEnable();
        BATT_MAN_OTG_Clear();  // Set LOW for OTG disable
        // Small delay to ensure GPIO settles
        vTaskDelay(2 / portTICK_PERIOD_MS);
        
        // REG01: 0b01010001 (watchdog reset, OTG=0, CHG=1, SYS_MIN=3.0V minimum)
        // CRITICAL: Using SYS_MIN=3.0V (000) instead of default 3.5V to prevent BATFET disable
        if (!BQ24297_Write_I2C(0x01, 0b01010001)) {
            LOG_E("BQ24297_Config_Settings: Failed to write charge mode!");
        }
        LOG_D("BQ24297_Config_Settings: External power - OTG disabled (GPIO=LOW), charging enabled");
    } else {
        // No external power - enable OTG
        // First set GPIO HIGH to enable OTG
        BATT_MAN_OTG_OutputEnable();
        BATT_MAN_OTG_Set();  // Set HIGH for OTG enable
        vTaskDelay(2 / portTICK_PERIOD_MS);
        
        // REG01: 0b01100001 (watchdog reset, OTG=1, CHG=0, SYS_MIN=3.0V)
        if (!BQ24297_Write_I2C(0x01, 0b01100001)) {
            LOG_E("BQ24297_Config_Settings: Failed to write OTG mode!");
        }
        LOG_D("BQ24297_Config_Settings: No external power - OTG ENABLED (GPIO=HIGH)");
    }
    
    // Debug: Read back REG01 to verify configuration
    reg = BQ24297_Read_I2C(0x01);
    LOG_D("BQ24297: REG01 after config = 0x%02X", reg);
    
    // Check for BATFET fault which would disconnect battery from system
    if (pData->status.bat_fault) {
        LOG_E("BQ24297: BATFET fault detected! Battery disconnected from system.");
        LOG_E("BQ24297: This can be caused by overcurrent/short. Requires USB reconnect to clear.");
    }

    // Set fast charge to 2000mA - this should never need to be updated
    BQ24297_Write_I2C(0x02, 0b01100000);
    
    // REG03: Pre-charge and termination current
    // Set minimum termination current (128mA) to avoid premature termination
    // [7:5]=000 (128mA precharge), [4]=0 (reserved), [3:0]=0001 (128mA termination)
    BQ24297_Write_I2C(0x03, 0b00000001);

    // REG04: Charge voltage and battery thresholds
    // [7:2]=100101 (4.096V), [1]=1 (3.0V battery low), [0]=0 (100mV recharge)
    // Set BATLOWV to 3.0V (higher threshold) to keep BATFET enabled longer
    BQ24297_Write_I2C(0x04, 0b10010110);

    // REG05: Disable watchdog and maximize charge timer
    // [7]=1 (enable termination), [6]=0 (disable timer), [5:4]=00 (disable watchdog)
    // [3]=0 (normal), [2:1]=11 (20hr timer), [0]=0 (reserved)
    // CRITICAL: Watchdog disabled to prevent unexpected BATFET disable
    BQ24297_Write_I2C(0x05, 0b10001110);
    
    // REG06: Boost voltage and thermal regulation
    // [7:4]=0111 (4.998V boost), [3:2]=00 (reserved), [1:0]=11 (120C thermal limit)
    // Keep default boost voltage but set highest thermal limit
    BQ24297_Write_I2C(0x06, 0b01110011);
    
    // CRITICAL: Ensure BATFET is enabled (REG07 bit 5 = 0)
    // Without BATFET, there's no path from battery to system!
    reg = BQ24297_Read_I2C(0x07);
    if (reg & 0b00100000) {  // Check if bit 5 is set (BATFET disabled)
        LOG_E("BQ24297: BATFET was disabled! Re-enabling...");
        reg = reg & 0b11011111;  // Clear bit 5 to enable BATFET
        BQ24297_Write_I2C(0x07, reg);
    }
    LOG_D("BQ24297: REG07 = 0x%02X (BATFET %s)", reg, (reg & 0x20) ? "DISABLED" : "enabled");

    // Read the current status pData
    BQ24297_UpdateStatus();

    // Evaluate current power source and set current limits
    BQ24297_AutoSetILim();

    /* Battery Detection Logic:
     * 
     * The NTC thermistor is physically located ON the battery pack itself.
     * Therefore, an NTC_FAULT_COLD indicates an open circuit, meaning the 
     * battery is physically disconnected from the board.
     * 
     * Battery is considered NOT present when BOTH conditions are true:
     * 1. ntcFault == NTC_FAULT_COLD (thermistor open = battery disconnected)
     * 2. vsysStat == true (battery voltage < 3.0V VSYSMIN threshold)
     * 
     * If the NTC shows COLD but voltage is >3.0V, we might have a faulty
     * thermistor but the battery is likely present and providing power.
     * 
     * Note: This detection is critical for power-up. The device requires
     * either battery >3.0V OR external power to stay powered after the
     * power button is released.
     */
    pData->status.batPresent = !((pData->status.ntcFault == NTC_FAULT_COLD) 
                        && (pData->status.vsysStat == true));

    // Charging is already enabled in REG01 configuration above
    // BQ24297 will handle battery presence detection internally

    // Final register dump for debugging
    LOG_E("BQ24297_Config_Settings: Final register configuration:");
    for (int i = 0; i <= 0x0A; i++) {
        reg = BQ24297_Read_I2C(i);
        LOG_E("  REG%02X = 0x%02X", i, reg);
    }
    
    // Final status check
    reg = BQ24297_Read_I2C(0x01);
    LOG_D("BQ24297_Config_Settings: Complete. REG01 = 0x%02X, OTG = %s, Charge = %s", 
          reg, 
          (reg & 0x20) ? "ON" : "OFF",
          (reg & 0x10) ? "ON" : "OFF");

    pData->initComplete = true;
    LOG_D("BQ24297_Config_Settings: Initialization complete, initComplete set to true");
    
    // Initialize USB reconnection detection now that BQ24297 is ready
    UsbReconnection_Init();
    LOG_D("BQ24297_Config_Settings: USB reconnection detection initialized");
}

/* 
 * Updates BQ24297 status by reading all registers
 * WARNING: This function reads REG09 which resets fault flags!
 * Use BQ24297_UpdateStatusSafe() if you don't want to reset faults
 */
void BQ24297_UpdateStatus(void) {
    uint8_t regData = 0;
    bool hasErrors = false;

    // REG00: Input Source Control
    regData = BQ24297_Read_I2C(0x00);
    if (regData != 0xFF) {
        pData->status.hiZ = (bool) (regData & 0b10000000);
        pData->status.inLim = (uint8_t) (regData & 0b00000111);
    } else {
        hasErrors = true;
    }

    // REG01: Power-On Configuration
    regData = BQ24297_Read_I2C(0x01);
    if (regData != 0xFF) {
        // Only log REG01 changes
        static uint8_t lastReg01 = 0xFF;
        if (regData != lastReg01) {
            LOG_D("BQ24297_UpdateStatus: REG01 = 0x%02X", regData);
            lastReg01 = regData;
        }
        pData->status.otg = (bool) (regData & 0b00100000);
        pData->status.chg = (bool) (regData & 0b00010000);
    } else {
        hasErrors = true;
    }

    // REG02: Charge Current Control
    regData = BQ24297_Read_I2C(0x02);
    if (regData != 0xFF) {
        pData->status.ichg = (uint8_t) (regData & 0b11111100) >> 2;
    } else {
        hasErrors = true;
    }

    // REG07: Misc Operation Control
    regData = BQ24297_Read_I2C(0x07);
    if (regData != 0xFF) {
        pData->status.iinDet_Read = (bool) (regData & 0b10000000);
    } else {
        hasErrors = true;
    }

    // REG08: System Status
    regData = BQ24297_Read_I2C(0x08);
    if (regData != 0xFF) {
        pData->status.vBusStat = (uint8_t) (regData & 0b11000000) >> 6;
        pData->status.chgStat = (uint8_t) (regData & 0b00110000) >> 4;
        pData->status.dpmStat = (bool) (regData & 0b00001000);
        pData->status.pgStat = (bool) (regData & 0b00000100);
        pData->status.thermStat = (bool) (regData & 0b00000010);
        pData->status.vsysStat = (bool) (regData & 0b00000001);
        
        // vsysStat = 1 means battery voltage < VSYSMIN (3.0V)
        // This is critical for power-up decisions
        // Only log status changes to reduce log spam
        static uint8_t lastReg08 = 0xFF;
        if (regData != lastReg08) {
            LOG_D("BQ24297: REG08=0x%02X vsysStat=%d (batt %s 3.0V) pgStat=%d vBusStat=%d", 
                  regData, pData->status.vsysStat, 
                  pData->status.vsysStat ? "<" : ">", 
                  pData->status.pgStat, pData->status.vBusStat);
                  
            // Extra logging when vsysStat indicates low battery
            if (pData->status.vsysStat) {
                LOG_E("BQ24297: WARNING - Battery voltage < 3.0V (vsysStat=1)! Device may power off!");
                LOG_E("BQ24297: This is why OTG boost is normally needed during USB disconnect");
            }
            
            lastReg08 = regData;
        }
    } else {
        hasErrors = true;
    }

    // REG09: Fault Status - Reading this resets fault flags!
    regData = BQ24297_Read_I2C(0x09);
    if (regData != 0xFF) {
        // First read resets faults, read again for current status
        regData = BQ24297_Read_I2C(0x09);
        if (regData != 0xFF) {
            pData->status.watchdog_fault = (bool) (regData & 0b10000000);
            pData->status.otg_fault = (bool) (regData & 0b01000000);
            pData->status.chgFault = (uint8_t) (regData & 0b00110000) >> 4;
            pData->status.bat_fault = (bool) (regData & 0b00001000);
            pData->status.ntcFault = (uint8_t) (regData & 0b00000011);
        }
    } else {
        hasErrors = true;
    }
    
    if (hasErrors) {
        LOG_E("BQ24297_UpdateStatus: I2C communication errors detected");
    }
    
    // Check for critical faults
    if (pData->status.bat_fault) {
        LOG_E("BQ24297: BATFET fault - battery disconnected from system!");
    }
}

/*
 * Updates BQ24297 status without resetting fault flags
 * Safe to call frequently as it doesn't read REG09
 */
void BQ24297_UpdateStatusSafe(void) {
    uint8_t regData = 0;
    bool hasErrors = false;

    // REG01: Power-On Configuration - Check OTG/CHG status
    regData = BQ24297_Read_I2C(0x01);
    if (regData != 0xFF) {
        pData->status.otg = (bool) (regData & 0b00100000);
        pData->status.chg = (bool) (regData & 0b00010000);
    } else {
        hasErrors = true;
    }

    // REG08: System Status - Check power status
    regData = BQ24297_Read_I2C(0x08);
    if (regData != 0xFF) {
        pData->status.vBusStat = (uint8_t) (regData & 0b11000000) >> 6;
        pData->status.chgStat = (uint8_t) (regData & 0b00110000) >> 4;
        pData->status.pgStat = (bool) (regData & 0b00000100);
    } else {
        hasErrors = true;
    }
    
    if (hasErrors) {
        LOG_E("BQ24297_UpdateStatusSafe: I2C communication errors detected");
    }
}

void BQ24297_ChargeEnable(bool chargeEnable) {
    // Temporary value to hold current register value
    uint8_t reg = 0;

    reg = BQ24297_Read_I2C(0x01);
    LOG_D("BQ24297_ChargeEnable: REG01 before = 0x%02X", reg);
    
    // IMPORTANT: OTG and Charge are mutually exclusive!
    // If OTG is enabled (bit 5), we cannot enable charging
    if (reg & 0x20) {
        LOG_D("BQ24297_ChargeEnable: OTG is enabled, cannot modify charge state");
        return;
    }
    
    if (pData->chargeAllowed && chargeEnable && pData->status.batPresent) {
        // Enable charging, preserve all bits except bit 4 (charge enable)
        reg = (reg & 0b11101111) | 0b00010000; // Clear bit 4, then set it
        BQ24297_Write_I2C(0x01, reg);
        LOG_D("BQ24297_ChargeEnable: Enabled charging, REG01 = 0x%02X", reg);
    } else {
        // Disable charging, preserve all bits except bit 4 (charge enable)
        reg = (reg & 0b11101111); // Just clear bit 4
        BQ24297_Write_I2C(0x01, reg);
        LOG_D("BQ24297_ChargeEnable: Disabled charging, REG01 = 0x%02X", reg);
    }
}

void BQ24297_ForceDPDM(void) {
    // Temporary value to hold current register value
    uint8_t reg = 0;
    // ----Untested implementation!----
    // Be sure that the USB lines are disconnected ie. USBCSR0bits.SOFTCONN = 0

    reg = BQ24297_Read_I2C(0x07);

    // Force DPDM detection
    // REG07: 0b1XXXXXXX
    BQ24297_Write_I2C(0x07, reg | 0b10000000);

    BQ24297_UpdateStatus();
    while (pData->status.iinDet_Read) {
        BQ24297_UpdateStatus();
    }
}

void BQ24297_AutoSetILim(void) {
    // Temporary value to hold current register value
    uint8_t reg0;

    reg0 = BQ24297_Read_I2C(0x00);
    
    // Log if HIZ mode is detected during AutoSetILim
    if (reg0 & 0x80) {
        LOG_E("BQ24297_AutoSetILim: WARNING - HIZ mode detected (REG00=0x%02X)! Will be cleared.", reg0);
    }

    //  Set system input current
    // CRITICAL: Never preserve HIZ bit (bit 7) when writing to REG00!
    // Use mask 0b01111000 instead of 0b11111000 to clear HIZ
    switch (pData->status.vBusStat) {
        case 0b00:
            // Unknown - assume it is a charger. Set IINLIM to 2000mA       
            //- maximum allowed on charger
            BQ24297_Write_I2C(0x00, 0b00000110 | (reg0 & 0b01111000));
            break;
        case 0b01:
            // NOTE: Originally set to 500mA for USB 2.0 compliance, but this
            // caused audible whining from the power supply. Using 2A instead.
            // Most modern USB ports can handle this, especially with enumeration.
            // Original 500mA setting:
            // BQ24297_Write_I2C(0x00, 0b00000010 | (reg0 & 0b01111000));
            BQ24297_Write_I2C(0x00, 0b00000110 | (reg0 & 0b01111000));
            break;
        case 0b10:
            // Unknown - assume it is a charger. Set IINLIM to 2000mA 
            //maximum allowed on charger
            BQ24297_Write_I2C(0x00, 0b00000110 | (reg0 & 0b01111000));
            break;
        default:
            break;
    }
}

uint8_t BQ24297_Read_I2C(uint8_t reg) {
    uint8_t I2CData[1];
    uint8_t rxData = 0;

    // Build data packet
    I2CData[0] = reg;

    if (pData->I2C_Handle != DRV_HANDLE_INVALID) {
        // Perform I2C transfer with error checking
        bool success = DRV_I2C_WriteReadTransfer(pData->I2C_Handle,
                pConfigBQ24->I2C_Address,                           
                I2CData,                                        
                1,                                                  
                &rxData,                                            
                1);
        
        if (!success) {
            LOG_E("BQ24297_Read_I2C: Failed to read register 0x%02X", reg);
            // Return 0xFF to indicate error (distinguishable from valid data)
            return 0xFF;
        }
    } else {
        LOG_E("BQ24297_Read_I2C: Invalid I2C handle");
        return 0xFF;
    }
    return rxData;
}

bool BQ24297_Write_I2C(uint8_t reg, uint8_t txData) {
    uint8_t I2CData[2];

    // Build data packet
    I2CData[0] = reg;
    I2CData[1] = txData;

    if (pData->I2C_Handle != DRV_HANDLE_INVALID) {
        // Write to selected register with error checking
        bool success = DRV_I2C_WriteTransfer(                       
                        pData->I2C_Handle,                                  
                        pConfigBQ24->I2C_Address,                           
                        I2CData,                                        
                        2);
        
        if (!success) {
            LOG_E("BQ24297_Write_I2C: Failed to write 0x%02X to register 0x%02X", txData, reg);
            return false;
        }
        return true;
    } else {
        LOG_E("BQ24297_Write_I2C: Invalid I2C handle");
        return false;
    }
}

void BQ24297_EnableOTG(void) {
    // CRITICAL LOGGING: Capture complete state at USB disconnect
    LOG_E("BQ24297_EnableOTG: USB DISCONNECT DETECTED - FULL REGISTER DUMP:");
    uint8_t regDump[11];
    for (int i = 0; i <= 0x0A; i++) {
        regDump[i] = BQ24297_Read_I2C(i);
        LOG_E("  REG%02X = 0x%02X", i, regDump[i]);
    }
    
    // Analyze critical registers
    LOG_E("BQ24297_EnableOTG: Critical status:");
    LOG_E("  HIZ Mode: %s (REG00 bit 7 = %d)", 
          (regDump[0] & 0x80) ? "ACTIVE!" : "Inactive", (regDump[0] >> 7) & 1);
    LOG_E("  Battery voltage: %s 3.0V (vsysStat=%d)", 
          (regDump[8] & 0x01) ? "BELOW" : "ABOVE", regDump[8] & 0x01);
    LOG_E("  BATFET: %s (REG07 bit 5 = %d)", 
          (regDump[7] & 0x20) ? "DISABLED!" : "Enabled", (regDump[7] >> 5) & 1);
    LOG_E("  Faults REG09 = 0x%02X: BAT_FAULT=%d, NTC=%d", 
          regDump[9], (regDump[9] >> 3) & 1, regDump[9] & 0x03);
    
    // CRITICAL: Clear HIZ mode if active - it blocks register writes!
    if (regDump[0] & 0x80) {
        LOG_E("BQ24297_EnableOTG: Clearing HIZ mode first!");
        uint8_t reg00 = regDump[0] & 0x7F;  // Clear bit 7 (HIZ)
        if (!BQ24297_Write_I2C(0x00, reg00)) {
            LOG_E("BQ24297_EnableOTG: Failed to clear HIZ mode!");
        } else {
            vTaskDelay(5 / portTICK_PERIOD_MS);  // Let it settle
            LOG_D("BQ24297_EnableOTG: HIZ mode cleared");
        }
    }
    
    // SAFETY CHECK: Cannot enable OTG if external power is present
    bool pgStat = (regDump[8] >> 2) & 0x01;
    bool dpmStat = (regDump[8] >> 3) & 0x01;  // DPM status (VINDPM or IINDPM)
    
    // Log DPM status for debugging
    if (dpmStat) {
        LOG_E("BQ24297_EnableOTG: DPM active (REG08 bit 3=1) - Input voltage/current regulation!");
    }
    
    if (pgStat) {
        LOG_D("BQ24297_EnableOTG: External power detected (pgStat=1), cannot enable OTG!");
        LOG_D("BQ24297_EnableOTG: This is likely a false disconnect - aborting OTG enable");
        return;
    }
    
    // Additional check: if VBUS_STAT shows USB host or adapter, don't enable OTG
    uint8_t vbusStat = (regDump[8] >> 6) & 0x03;
    if (vbusStat == 0x01 || vbusStat == 0x02) {
        LOG_D("BQ24297_EnableOTG: VBUS_STAT=%d indicates external power, aborting OTG enable", vbusStat);
        return;
    } else if (vbusStat == 0x03) {
        LOG_D("BQ24297_EnableOTG: VBUS_STAT=3 indicates OTG already active!");
        // Check if REG01 matches
        if ((regDump[1] & 0x20) != 0) {
            LOG_D("BQ24297_EnableOTG: OTG already enabled in REG01, updating status and returning");
            pData->status.otg = true;
            pData->status.chg = false;
            return;
        } else {
            LOG_E("BQ24297_EnableOTG: VBUS_STAT=OTG but REG01 OTG bit is clear - inconsistent state!");
        }
    }
    
    // Check if GPIO is already in correct state
    bool otgGpioEnabled = BATT_MAN_OTG_Get(); // HIGH = enabled (active high)
    
    if (otgGpioEnabled) {
        LOG_D("BQ24297_EnableOTG: OTG GPIO already HIGH (enabled), checking register");
    } else {
        LOG_E("BQ24297_EnableOTG: ENABLING OTG MODE FOR BATTERY OPERATION!");
        
        // Set GPIO pin for OTG enable FIRST (active high)
        BATT_MAN_OTG_OutputEnable();  // Make sure it's an output
        BATT_MAN_OTG_Set();          // Set HIGH for OTG enable
        LOG_D("BQ24297_EnableOTG: Set OTG GPIO HIGH (RK5) for enable");
    }
    
    // Read current REG01 value
    uint8_t reg = BQ24297_Read_I2C(0x01);
    if (reg == 0xFF) {
        LOG_E("BQ24297_EnableOTG: Failed to read REG01");
        // Still update status to reflect GPIO state
        pData->status.otg = true;
        pData->status.chg = false;
        return;
    }
    
    // Set OTG enable (bit 5) and clear charge enable (bit 4)
    // Also set watchdog reset (bit 6)
    // CRITICAL: Set SYS_MIN to 3.0V (minimum) to prevent BATFET disable
    // Preserve boost current limit from existing setting
    uint8_t boostLimit = reg & 0x01;  // Preserve bit 0 (boost current limit)
    // REG01: [7:6]=01 (watchdog reset), [5]=1 (OTG), [4]=0 (no charge), [3:1]=000 (3.0V), [0]=boost limit
    // IMPORTANT: CHG_CONFIG[5:4] must be 10 for OTG (not 11)!
    reg = 0b01100000 | boostLimit;  // Watchdog reset + OTG enable + SYS_MIN=3.0V + preserved boost limit
    LOG_D("BQ24297_EnableOTG: Setting REG01 = 0x%02X (OTG=1, CHG=0)", reg);
    
    // Write with retry logic
    bool writeSuccess = false;
    for (int retry = 0; retry < 3; retry++) {
        // Re-check pgStat before each attempt - it may have changed!
        uint8_t reg08 = BQ24297_Read_I2C(0x08);
        if ((reg08 >> 2) & 0x01) {
            LOG_E("BQ24297_EnableOTG: pgStat=1 detected during retry %d, aborting OTG enable", retry + 1);
            return;
        }
        
        if (BQ24297_Write_I2C(0x01, reg)) {
            LOG_D("BQ24297_EnableOTG: REG01 write = 0x%02X", reg);
            
            // Read back to verify
            vTaskDelay(5 / portTICK_PERIOD_MS);
            uint8_t readback = BQ24297_Read_I2C(0x01);
            
            if ((readback & 0x30) == 0x20) {  // Check OTG bit is set, charge bit is clear
                LOG_D("BQ24297_EnableOTG: REG01 verified = 0x%02X (OTG=1)", readback);
                writeSuccess = true;
                break;
            } else {
                LOG_E("BQ24297_EnableOTG: REG01 readback = 0x%02X, expected 0x%02X, retry %d", 
                      readback, reg, retry + 1);
                
                // If we're getting 0x01 or 0x11, it might mean power was detected
                if ((readback & 0xF0) == 0x00 || (readback & 0xF0) == 0x10) {
                    LOG_E("BQ24297_EnableOTG: Charge mode detected, likely due to power reconnection");
                    return;
                }
            }
        } else {
            LOG_E("BQ24297_EnableOTG: Failed to write REG01, retry %d", retry + 1);
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
    if (!writeSuccess) {
        LOG_E("BQ24297_EnableOTG: Failed to enable OTG after 3 retries!");
        
        // RECOVERY: If we can't enable OTG, at least ensure GPIO is in safe state
        // Set GPIO back to LOW and try to enable charging instead
        BATT_MAN_OTG_Clear();  // Set LOW to disable OTG
        vTaskDelay(5 / portTICK_PERIOD_MS);
        
        // Try to enable charging mode as fallback
        reg = 0b01010001;  // Watchdog reset + Charge enable + SYS_MIN=3.0V (minimum)
        if (BQ24297_Write_I2C(0x01, reg)) {
            LOG_E("BQ24297_EnableOTG: Recovery - enabled charging mode instead");
        } else {
            LOG_E("BQ24297_EnableOTG: Recovery failed - check I2C communication!");
        }
    }
    
    // CRITICAL: Check BATFET is enabled in REG07
    reg = BQ24297_Read_I2C(0x07);
    LOG_E("BQ24297_EnableOTG: REG07 = 0x%02X (BATFET %s)", reg, (reg & 0x20) ? "DISABLED!" : "enabled");
    if (reg & 0x20) {
        LOG_E("BQ24297_EnableOTG: WARNING - BATFET is disabled, no battery path!");
    }
    
    // Check REG08 status
    reg = BQ24297_Read_I2C(0x08);
    LOG_E("BQ24297_EnableOTG: REG08 = 0x%02X (pgStat=%d, vsysStat=%d)", 
          reg, (reg >> 2) & 1, reg & 1);
    
    // Add delay for OTG to stabilize
    vTaskDelay(50 / portTICK_PERIOD_MS);
    
    // Check status again
    reg = BQ24297_Read_I2C(0x08);
    LOG_E("BQ24297_EnableOTG: After delay - REG08 = 0x%02X", reg);
    
    // Update local status to reflect the change
    pData->status.otg = true;
    pData->status.chg = false;
}

void BQ24297_DisableOTG(bool enableCharging) {
    // CRITICAL: Clear HIZ mode FIRST before any other operations
    // HIZ mode can be triggered during OTG transitions and cause power loss
    uint8_t reg00 = BQ24297_Read_I2C(0x00);
    if (reg00 != 0xFF && (reg00 & 0x80)) {
        LOG_D("BQ24297_DisableOTG: Clearing HIZ mode before OTG disable");
        reg00 &= 0x7F;  // Clear bit 7 to exit HIZ mode
        BQ24297_Write_I2C(0x00, reg00);
        vTaskDelay(2 / portTICK_PERIOD_MS);
    }
    
    // Set GPIO pin LOW for OTG disable FIRST (active high logic)
    BATT_MAN_OTG_OutputEnable();  // Make sure it's an output
    BATT_MAN_OTG_Clear();         // Set LOW for OTG disable
    LOG_D("BQ24297_DisableOTG: Set OTG GPIO LOW (RK5) for disable");
    
    // Small delay to ensure GPIO settles
    vTaskDelay(2 / portTICK_PERIOD_MS);
    
    // Read current REG01 value
    uint8_t reg = BQ24297_Read_I2C(0x01);
    uint8_t original_reg = reg;
    if (reg == 0xFF) {
        LOG_E("BQ24297_DisableOTG: Failed to read REG01");
        // Still update status to reflect GPIO state
        pData->status.otg = false;
        return;
    }
    
    // Clear OTG enable (bit 5)
    reg = reg & 0b11011111;
    
    // Set watchdog reset (bit 6)
    reg = reg | 0b01000000;
    
    // Set charge enable if requested
    if (enableCharging) {
        reg = reg | 0b00010000;  // Set bit 4
        LOG_D("BQ24297_DisableOTG: Enabling charging");
    } else {
        reg = reg & 0b11101111;  // Clear bit 4
    }
    
    // CRITICAL: Keep SYS_MIN at 3.0V minimum (bits 3:1 = 000) and bit 0 set (reserved)
    // This prevents BATFET from being disabled due to low battery voltage
    reg = (reg & 0b11110001) | 0b00000001;
    
    if (!BQ24297_Write_I2C(0x01, reg)) {
        LOG_E("BQ24297_DisableOTG: Failed to write REG01");
        // GPIO is still set, so OTG should be disabled
    } else {
        LOG_D("BQ24297_DisableOTG: REG01 changed from 0x%02X to 0x%02X, charging = %s", 
              original_reg, reg, (reg & 0x10) ? "enabled" : "disabled");
    }
    
    // CRITICAL: Ensure HIZ mode is still cleared after REG01 write
    // Writing to REG01 can sometimes trigger HIZ mode
    vTaskDelay(5 / portTICK_PERIOD_MS);
    reg00 = BQ24297_Read_I2C(0x00);
    if (reg00 != 0xFF && (reg00 & 0x80)) {
        LOG_E("BQ24297_DisableOTG: HIZ mode triggered after REG01 write! Clearing again!");
        reg00 &= 0x7F;  // Clear bit 7 to exit HIZ mode
        BQ24297_Write_I2C(0x00, reg00);
    }
    
    // Update local status to reflect the change
    pData->status.otg = false;
    pData->status.chg = (reg & 0x10) ? true : false;
    
    // IMPORTANT: After disabling OTG, the BQ24297 will perform input detection
    // This can take 200-500ms and may cause temporary instability
    // Log this so we can track it
    LOG_D("BQ24297_DisableOTG: OTG disabled, input detection will begin");
    
    // Give the BQ24297 time to start input detection
    vTaskDelay(50 / portTICK_PERIOD_MS);
    
    // Check if DPM is active (indicating input detection in progress)
    uint8_t reg08 = BQ24297_Read_I2C(0x08);
    if ((reg08 & 0x08) != 0) {
        LOG_D("BQ24297_DisableOTG: DPM active (REG08=0x%02X), input detection in progress", reg08);
    }
    
    // Force a status update to refresh all fields
    BQ24297_UpdateStatus();
}

bool BQ24297_IsOTGEnabled(void) {
    uint8_t reg = BQ24297_Read_I2C(0x01);
    return (reg & 0b00100000) != 0;
}

bool BQ24297_IsChargingEnabled(void) {
    uint8_t reg = BQ24297_Read_I2C(0x01);
    return (reg & 0b00010000) != 0;
}

void BQ24297_SetPowerMode(bool externalPowerPresent) {
    if (externalPowerPresent) {
        // External power available - disable OTG and enable charging
        LOG_D("BQ24297_SetPowerMode: External power detected, switching to charge mode");
        BQ24297_DisableOTG(true);
    } else {
        // Battery power only - enable OTG 
        // Testing shows device powers off without OTG when on battery
        // Theory: OTG may be required to maintain power path from battery to system
        LOG_D("BQ24297_SetPowerMode: No external power, enabling OTG mode");
        BQ24297_EnableOTG();
    }
}