/* 
 * @file   BQ24297.c
 * @brief This file manages the BQ24297 module
 */
#include "BQ24297.h"
#include "Util/Logger.h"
#include "services/UsbCdc/UsbCdc.h"
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
static uint8_t BQ24297_Read_I2C(uint8_t reg);

/*! Funtion to write BQ24297 data by I2C communication
 * @param[in] reg Register to read
 * @param[in] txData Data to write
 * @return true if write succeeded, false on error
 */
static bool BQ24297_Write_I2C(uint8_t reg, uint8_t txData);

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
    
    // GPIO control for OTG pin (RK5)
    // CRITICAL: Keep OTG in its default state (HIGH) to maintain power continuity
    // The GPIO init sets RK5 high, which enables OTG and ensures battery power
    // We'll let the power management code decide when to switch modes
    BATT_MAN_OTG_OutputEnable();  // Make sure it's an output
    // Do NOT clear OTG here - preserve the GPIO init state
    LOG_D("BQ24297_InitHardware: OTG GPIO kept in default state (HIGH for power continuity)");
}

void BQ24297_Config_Settings(void) {
    // Temporary value to hold current register value
    uint8_t reg = 0;
    
    LOG_D("BQ24297_Config_Settings: Starting initialization");
    
    // Read initial status without changing OTG state
    // This preserves the power path if OTG is already enabled
    BQ24297_UpdateStatus();

    // At this point, the chip has evaluated the power source, so we should get the current limit
    // and save it when writing to register 0
    reg = BQ24297_Read_I2C(0x00);

    // Set input voltage limit to 3.88V: VINDPM = 0
    // REG00: 0b00000XXX
    BQ24297_Write_I2C(0x00, reg & 0b00000111);

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
    
    // If OTG is enabled at startup AND we detect external power, we need to carefully
    // transition to avoid power loss
    bool otgEnabledAtStartup = pData->status.otg;
    bool hasExternalPower = pData->status.pgStat;
    
    // Log MCU VBUS for debug only
    bool usbVbusDebug = UsbCdc_IsVbusDetected();
    LOG_D("BQ24297_Config_Settings: pgStat=%d, vBusStat=%d, otg=%d [MCU_VBUS_DEBUG=%d]",
          pData->status.pgStat, pData->status.vBusStat, otgEnabledAtStartup, usbVbusDebug);
    
    // Special case: If OTG is on but external power is detected, we need to
    // transition carefully to avoid power loss
    if (otgEnabledAtStartup && hasExternalPower) {
        LOG_D("BQ24297_Config_Settings: OTG enabled with external power - will switch to charge mode");
        // Keep OTG on for now, let Power_Update_Settings handle the transition
        // This ensures continuous power during the switch
        hasExternalPower = true;
    }
    
    // Configure REG01 based on power status
    // REG01 bits:
    // [7:6] = 01 (reset watchdog)
    // [5] = OTG (0=disable, 1=enable)
    // [4] = CHARGE ENABLE (0=disable, 1=enable)
    // [3:1] = 000 (SYS_MIN = 3.0V)
    // [0] = 1 (reserved)
    
    // CRITICAL INSIGHT: Device worked when I2C was corrupted because
    // BQ24297 initialization failed, leaving it in default/bootloader state.
    // Let's try minimal configuration - just enable charging, don't touch OTG
    
    // EXPERIMENT: Enable OTG to boost battery voltage for 3.3V regulator
    // The 3.3V regulator may need >4V input which battery alone can't provide
    if (!hasExternalPower) {
        // No external power - enable OTG boost
        // REG01: 0b01100001 (OTG=1, CHG=0, SYS_MIN=3.0V)
        BQ24297_Write_I2C(0x01, 0b01100001);
        BATT_MAN_OTG_Set();
        LOG_D("BQ24297_Config_Settings: No external power - OTG ENABLED for boost");
    } else {
        // External power - normal charging
        // REG01: 0b01010001 (OTG=0, CHG=1, SYS_MIN=3.0V)
        BQ24297_Write_I2C(0x01, 0b01010001);
        BATT_MAN_OTG_Clear();
        LOG_D("BQ24297_Config_Settings: External power - charging enabled");
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

    // Set charge voltage to 4.096V
    BQ24297_Write_I2C(0x04, 0b10010110);

    // Disable watchdog WATCHDOG = 0, set charge timer to 20hr
    // REG05: 0b10001110
    BQ24297_Write_I2C(0x05, 0b10001110);
    
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

    // Final status check
    reg = BQ24297_Read_I2C(0x01);
    LOG_D("BQ24297_Config_Settings: Complete. REG01 = 0x%02X, OTG = %s, Charge = %s", 
          reg, 
          (reg & 0x20) ? "ON" : "OFF",
          (reg & 0x10) ? "ON" : "OFF");

    pData->initComplete = true;
    LOG_D("BQ24297_Config_Settings: Initialization complete, initComplete set to true");
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

    //  Set system input current
    switch (pData->status.vBusStat) {
        case 0b00:
            // Unknown - assume it is a charger. Set IINLIM to 2000mA       
            //- maximum allowed on charger
            BQ24297_Write_I2C(0x00, 0b00000110 | (reg0 & 0b11111000));
            break;
        case 0b01:
            // NOTE: Originally set to 500mA for USB 2.0 compliance, but this
            // caused audible whining from the power supply. Using 2A instead.
            // Most modern USB ports can handle this, especially with enumeration.
            // Original 500mA setting:
            // BQ24297_Write_I2C(0x00, 0b00000010 | (reg0 & 0b11111000));
            BQ24297_Write_I2C(0x00, 0b00000110 | (reg0 & 0b11111000));
            break;
        case 0b10:
            // Unknown - assume it is a charger. Set IINLIM to 2000mA 
            //maximum allowed on charger
            BQ24297_Write_I2C(0x00, 0b00000110 | (reg0 & 0b11111000));
            break;
        default:
            break;
    }
}

static uint8_t BQ24297_Read_I2C(uint8_t reg) {
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

static bool BQ24297_Write_I2C(uint8_t reg, uint8_t txData) {
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
    LOG_E("BQ24297_EnableOTG: ENABLING OTG MODE FOR BATTERY OPERATION!");
    
    // Set GPIO pin for OTG enable FIRST
    BATT_MAN_OTG_OutputEnable();  // Make sure it's an output
    BATT_MAN_OTG_Set();          // Set high for OTG enable
    LOG_D("BQ24297_EnableOTG: Set OTG GPIO high (RK5)");
    
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
    // Preserve watchdog reset bit and SYS_MIN settings
    reg = (reg & 0b11001111) | 0b00100001;  // Clear bits 5:4, then set OTG
    
    if (!BQ24297_Write_I2C(0x01, reg)) {
        LOG_E("BQ24297_EnableOTG: Failed to write REG01");
        // GPIO is still set, so OTG might work anyway
    } else {
        LOG_D("BQ24297_EnableOTG: REG01 = 0x%02X", reg);
    }
    
    // Read back to verify
    vTaskDelay(10 / portTICK_PERIOD_MS);
    reg = BQ24297_Read_I2C(0x01);
    LOG_E("BQ24297_EnableOTG: REG01 readback = 0x%02X (OTG=%d)", reg, (reg >> 5) & 1);
    
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
    // Clear GPIO pin for OTG disable FIRST
    BATT_MAN_OTG_OutputEnable();  // Make sure it's an output
    BATT_MAN_OTG_Clear();         // Set low for OTG disable
    LOG_D("BQ24297_DisableOTG: Cleared OTG GPIO (RK5)");
    
    // Read current REG01 value
    uint8_t reg = BQ24297_Read_I2C(0x01);
    if (reg == 0xFF) {
        LOG_E("BQ24297_DisableOTG: Failed to read REG01");
        // Still update status to reflect GPIO state
        pData->status.otg = false;
        return;
    }
    
    // Clear OTG enable (bit 5)
    reg = reg & 0b11011111;
    
    // Set charge enable if requested and battery is present
    if (enableCharging && pData->status.batPresent && pData->chargeAllowed) {
        reg = reg | 0b00010000;  // Set bit 4
    }
    
    if (!BQ24297_Write_I2C(0x01, reg)) {
        LOG_E("BQ24297_DisableOTG: Failed to write REG01");
        // GPIO is still cleared, so OTG should be disabled
    } else {
        LOG_D("BQ24297_DisableOTG: REG01 = 0x%02X, charging = %s", 
              reg, (reg & 0x10) ? "enabled" : "disabled");
    }
    
    // Update local status to reflect the change
    pData->status.otg = false;
    pData->status.chg = (reg & 0x10) ? true : false;
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