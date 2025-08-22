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

    // Configure OTG GPIO pin (RK5) as output and set LOW
    // OTG pin is ACTIVE HIGH - HIGH = boost mode enabled, LOW = normal charging
    BATT_MAN_OTG_OutputEnable();
    BATT_MAN_OTG_Clear();  // Set LOW = OTG disabled = allow charging
    
    LOG_D("BQ24297_InitHardware: OTG GPIO initialized LOW (OTG disabled)");
}

void BQ24297_Config_Settings(void) {
    // Temporary value to hold current register value
    uint8_t reg = 0;
    
    LOG_D("BQ24297_Config_Settings: Starting initialization");
    
    // Read initial status to determine current power state
    BQ24297_UpdateStatus();

    // Read REG00 to preserve current limit settings
    reg = BQ24297_Read_I2C(0x00);
    
    // Clear HIZ mode if set - it can cause power loss
    if (reg & 0x80) {
        LOG_D("BQ24297_Config_Settings: Clearing HIZ mode");
    }

    // Set input voltage limit to 3.88V (minimum): VINDPM = 0
    // This gives maximum headroom before input voltage regulation kicks in
    // REG00: [7]=0 (CLEAR HIZ), [6:3]=0000 (3.88V min), [2:0]=keep current limit
    BQ24297_Write_I2C(0x00, reg & 0b00000111);  // This also clears HIZ bit

    // Configure REG01 for charging mode
    // REG01 bits:
    // [7:6] = 01 (reset watchdog)
    // [5] = 0 (OTG disable)
    // [4] = 1 (CHARGE ENABLE)
    // [3:1] = 000 (SYS_MIN = 3.0V)
    // [0] = 1 (reserved)
    
    // Set OTG GPIO LOW to disable boost mode
    BATT_MAN_OTG_OutputEnable();
    BATT_MAN_OTG_Clear();  // Set LOW for OTG disable
    
    // Configure for charge mode with SYS_MIN=3.0V to prevent BATFET disable
    BQ24297_Write_I2C(0x01, 0b01010001);  // Watchdog reset, OTG=0, CHG=1, SYS_MIN=3.0V

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
    
    // Ensure BATFET is enabled (REG07 bit 5 = 0)
    // Without BATFET, there's no path from battery to system
    reg = BQ24297_Read_I2C(0x07);
    if (reg & 0b00100000) {  // Check if bit 5 is set (BATFET disabled)
        reg = reg & 0b11011111;  // Clear bit 5 to enable BATFET
        BQ24297_Write_I2C(0x07, reg);
        LOG_D("BQ24297: BATFET re-enabled");
    }

    // Read the current status and set appropriate current limits
    BQ24297_UpdateStatus();
    BQ24297_AutoSetILim();

    // Update battery presence and charge allowed status
    BQ24297_UpdateBatteryStatus();
    
    // If charging is not allowed, disable it now
    if (!pData->chargeAllowed) {
        BQ24297_ChargeEnable(false);
        LOG_D("BQ24297_Config_Settings: Charging disabled - battery %s, thermistor %s", 
              pData->status.batPresent ? "present" : "not present",
              pData->status.ntcFault == NTC_FAULT_HOT ? "stuck low" : 
              pData->status.ntcFault == NTC_FAULT_COLD ? "open" : "normal");
    } else if (pData->chargeAllowed && pData->status.pgStat && !pData->status.otg) {
        // Charging is allowed, external power present, and not in OTG mode
        // Enable charging if not already enabled
        if (!BQ24297_IsChargingEnabled()) {
            LOG_D("BQ24297_Config_Settings: Enabling charging - battery present, external power available");
            BQ24297_ChargeEnable(true);
        }
    }

    // Mark initialization complete
    pData->initComplete = true;
    
    LOG_D("BQ24297_Config_Settings: Initialization complete");
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
        pData->status.chgEn = (bool) (regData & 0b00010000);
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
                LOG_D("BQ24297: Battery voltage < 3.0V (vsysStat=1)");
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
        pData->status.chgEn = (bool) (regData & 0b00010000);
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
    
    // OTG and Charge are mutually exclusive
    if (reg & 0x20) {
        return;  // OTG is enabled, cannot modify charge state
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
    // Read REG08 for safety checks
    uint8_t reg08 = BQ24297_Read_I2C(0x08);
    bool pgStat = (reg08 >> 2) & 0x01;
    uint8_t vbusStat = (reg08 >> 6) & 0x03;
    
    // Clear HIZ mode if active
    uint8_t reg00 = BQ24297_Read_I2C(0x00);
    if (reg00 & 0x80) {
        reg00 &= 0x7F;  // Clear bit 7 (HIZ)
        BQ24297_Write_I2C(0x00, reg00);
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }
    
    // Cannot enable OTG if external power is present
    if (pgStat) {
        return;
    }
    
    // Check if OTG is already active
    if (vbusStat == 0x03) {
        uint8_t reg01 = BQ24297_Read_I2C(0x01);
        if (reg01 & 0x20) {
            pData->status.otg = true;
            pData->status.chgEn = false;
            return;
        }
    }
    
    // Set OTG GPIO HIGH to enable boost mode
    BATT_MAN_OTG_OutputEnable();
    BATT_MAN_OTG_Set();
    
    // Read current REG01 value
    uint8_t reg01 = BQ24297_Read_I2C(0x01);
    if (reg01 == 0xFF) {
        return;
    }
    
    // Configure REG01 for OTG mode
    // Bit 5 = 1 (OTG enable), Bit 4 = 0 (charge disable), Bit 6 = 1 (watchdog reset)
    reg01 = (reg01 & 0x0F) | 0x60;
    
    // Write with retry logic
    bool success = false;
    for (int retry = 0; retry < 3; retry++) {
        if (BQ24297_Write_I2C(0x01, reg01)) {
            vTaskDelay(5 / portTICK_PERIOD_MS);
            
            // Verify write
            uint8_t readback = BQ24297_Read_I2C(0x01);
            if ((readback & 0x30) == 0x20) {  // Check OTG=1, CHG=0
                success = true;
                break;
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
    
    if (!success) {
        BATT_MAN_OTG_Clear();  // Clear GPIO on failure
    }
    
    // Update status
    pData->status.otg = success;
    pData->status.chgEn = false;
}

void BQ24297_DisableOTG(bool enableCharging) {
    // Clear HIZ mode if active
    uint8_t reg00 = BQ24297_Read_I2C(0x00);
    if (reg00 != 0xFF && (reg00 & 0x80)) {
        reg00 &= 0x7F;  // Clear bit 7
        BQ24297_Write_I2C(0x00, reg00);
        vTaskDelay(2 / portTICK_PERIOD_MS);
    }
    
    // Set GPIO LOW to disable OTG
    BATT_MAN_OTG_OutputEnable();
    BATT_MAN_OTG_Clear();
    vTaskDelay(2 / portTICK_PERIOD_MS);
    
    // Read and modify REG01
    uint8_t reg = BQ24297_Read_I2C(0x01);
    if (reg == 0xFF) {
        pData->status.otg = false;
        return;
    }
    
    // Clear OTG bit (bit 5), set watchdog reset (bit 6)
    // Keep SYS_MIN at 3.0V (bits 3:1 = 000)
    reg = (reg & 0b11010001) | 0b01000001;
    
    // Set charge enable if requested
    if (enableCharging) {
        reg |= 0b00010000;
    }
    
    // Write the new value
    if (!BQ24297_Write_I2C(0x01, reg)) {
        return;
    }
    
    // Check for HIZ mode after REG01 write
    vTaskDelay(5 / portTICK_PERIOD_MS);
    reg00 = BQ24297_Read_I2C(0x00);
    if (reg00 != 0xFF && (reg00 & 0x80)) {
        reg00 &= 0x7F;
        BQ24297_Write_I2C(0x00, reg00);
    }
    
    // Update local status
    pData->status.otg = false;
    pData->status.chgEn = (reg & 0x10) ? true : false;
    
    // Give the BQ24297 time to perform input detection
    vTaskDelay(50 / portTICK_PERIOD_MS);
    
    // Update status to reflect changes
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

bool BQ24297_IsBatteryPresent(void) {
    // Battery detection logic:
    // 1. If thermistor reads COLD (open circuit), battery is disconnected
    // 2. If thermistor reads HOT, it's stuck low (shorted) - battery may be present but charging unsafe
    // 3. Normal thermistor AND voltage > 3.0V indicates battery present
    
    // Update status to get latest NTC and voltage readings
    BQ24297_UpdateStatus();
    
    // NTC_FAULT_COLD (2) = thermistor open = no battery
    if (pData->status.ntcFault == NTC_FAULT_COLD) {
        return false;
    }
    
    // NTC_FAULT_HOT (1) = thermistor shorted/stuck low - unsafe to charge
    if (pData->status.ntcFault == NTC_FAULT_HOT) {
        // Battery might be present but thermistor is faulty
        // Check voltage to confirm battery presence
        return !pData->status.vsysStat;  // Battery present if voltage > 3.0V
    }
    
    // Normal thermistor reading - battery is present
    return true;
}

void BQ24297_UpdateBatteryStatus(void) {
    // Update battery presence status
    bool oldBatPresent = pData->status.batPresent;
    bool oldChargeAllowed = pData->chargeAllowed;
    pData->status.batPresent = BQ24297_IsBatteryPresent();
    
    // Update charge allowed flag based on battery and thermistor status
    if (pData->status.ntcFault == NTC_FAULT_HOT) {
        // Thermistor stuck low/shorted - disable charging for safety
        pData->chargeAllowed = false;
        if (oldBatPresent && !pData->chargeAllowed) {
            LOG_D("BQ24297: Thermistor fault detected (stuck low), charging disabled");
        }
    } else if (pData->status.batPresent) {
        // Battery present with normal thermistor - allow charging
        pData->chargeAllowed = true;
    } else {
        // No battery detected - disable charging
        pData->chargeAllowed = false;
    }
    
    // Update charging state based on current conditions
    if (!pData->chargeAllowed && BQ24297_IsChargingEnabled()) {
        // Charging not allowed but is enabled - disable it
        BQ24297_ChargeEnable(false);
    } else if (pData->chargeAllowed && !pData->status.otg && pData->status.pgStat) {
        // Charging allowed, not in OTG mode, and external power present
        // Enable charging if not already enabled
        if (!BQ24297_IsChargingEnabled()) {
            LOG_D("BQ24297: Battery present, external power available - enabling charging");
            BQ24297_ChargeEnable(true);
        }
    }
    
    // Log status changes
    if (oldBatPresent != pData->status.batPresent || oldChargeAllowed != pData->chargeAllowed) {
        LOG_D("BQ24297: Battery status changed - present=%d, chargeAllowed=%d, pgStat=%d, otg=%d",
              pData->status.batPresent, pData->chargeAllowed, 
              pData->status.pgStat, pData->status.otg);
    }
}

void BQ24297_SetPowerMode(bool externalPowerPresent) {
    static bool lastExternalPowerState = false;
    static bool initialized = false;
    
    // Only log when state actually changes
    if (!initialized || lastExternalPowerState != externalPowerPresent) {
        if (externalPowerPresent) {
            LOG_D("BQ24297_SetPowerMode: External power detected, switching to charge mode");
        } else {
            LOG_D("BQ24297_SetPowerMode: External power lost, battery power only");
        }
        lastExternalPowerState = externalPowerPresent;
        initialized = true;
    }
    
    if (externalPowerPresent) {
        // External power available - disable OTG and enable charging
        BQ24297_DisableOTG(true);
        
        // Update battery status which will enable charging if battery is present
        BQ24297_UpdateBatteryStatus();
    } else {
        // Battery power only - OTG mode is NOT automatically enabled
        // OTG can only be controlled manually via SCPI commands
        // Keep current OTG state - manual control only via SCPI
        
        // Still update battery status to ensure proper charge state
        BQ24297_UpdateBatteryStatus();
    }
}