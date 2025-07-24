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

/*! Funtion to read BQ24297 data by I2C communication
 * @param[in] reg Register to read
 * @param[in] txData Data to write
 */
static void BQ24297_Write_I2C(uint8_t reg, uint8_t txData);

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
    
    // Use the GPIO macros for BATT_MAN_OTG pin
    // Note: Hardware schematic shows RK5 as OTG control pin
    // Previous code incorrectly used RF5 which is the I2C SCL pin
    if (pWriteVariables->OTG_Val) {
        BATT_MAN_OTG_OutputEnable();  // Make sure it's an output
        BATT_MAN_OTG_Set();          // Set high for OTG enable
        LOG_D("BQ24297_InitHardware: Set OTG GPIO high");
    } else {
        BATT_MAN_OTG_OutputEnable();  // Make sure it's an output
        BATT_MAN_OTG_Clear();        // Set low for OTG disable
        LOG_D("BQ24297_InitHardware: Set OTG GPIO low");
    }
}

void BQ24297_Config_Settings(void) {
    // Temporary value to hold current register value
    uint8_t reg = 0;
    
    LOG_D("BQ24297_Config_Settings: Starting initialization (initComplete=%s)", 
          pData->initComplete ? "true" : "false");
    
    // Give BQ24297 and USB subsystem time to detect power sources
    // This delay is critical for proper USB VBUS detection on startup
    // 150ms provides a safety margin while being 3x faster than original 500ms
    vTaskDelay(150 / portTICK_PERIOD_MS);
    
    // Read the current status data
    BQ24297_UpdateStatus();
    while (pData->status.iinDet_Read) {
        BQ24297_UpdateStatus();
    }

    // At this point, the chip has evaluated the power source, so we should get the current limit
    // and save it when writing to register 0
    reg = BQ24297_Read_I2C(0x00);

    // Set input voltage limit to 3.88V: VINDPM = 0
    // REG00: 0b00000XXX
    BQ24297_Write_I2C(0x00, reg & 0b00000111);

    // OTG mode configuration - Required for battery operation on this board
    // 
    // OBSERVED BEHAVIOR:
    // - Device powers off when USB is disconnected if OTG is disabled
    // - Device remains powered when OTG is enabled during battery operation
    // 
    // THEORY (not confirmed):
    // The BQ24297's power path may not automatically connect battery to VSYS
    // when OTG is disabled. Enabling OTG creates a power path from battery
    // through the boost converter to VSYS, allowing the 3.3V regulator to
    // maintain power to the microcontroller.
    //
    // NOTE: This is unexpected behavior as the BQ24297 advertises automatic
    // power path management. Further investigation may reveal the true cause.
    
    // Check current power status to decide on OTG mode
    BQ24297_UpdateStatus();
    
    // Also check microcontroller's USB VBUS detection
    bool usbVbusDetected = UsbCdc_IsVbusDetected();
    
    // Set initial power mode based on detected power source
    // Use EITHER BQ24297's power detection OR microcontroller's USB detection
    bool hasExternalPower = (pData->status.pgStat && 
                            (pData->status.vBusStat == VBUS_USB || 
                             pData->status.vBusStat == VBUS_CHARGER)) ||
                           usbVbusDetected;
    
    LOG_D("BQ24297_Config_Settings: pgStat=%d, vBusStat=%d, usbVbus=%d -> hasExtPower=%d",
          pData->status.pgStat, pData->status.vBusStat, usbVbusDetected, hasExternalPower);
    
    // Configure REG01 based on power status
    // REG01 bits:
    // [7:6] = 01 (reset watchdog)
    // [5] = OTG (0=disable, 1=enable)
    // [4] = CHARGE ENABLE (0=disable, 1=enable)
    // [3:1] = 000 (SYS_MIN = 3.0V)
    // [0] = 1 (reserved)
    
    // Based on testing, OTG must be enabled for battery operation
    // THEORY: Without OTG, the power path from battery to system may be interrupted
    
    if (hasExternalPower) {
        // External power available - disable OTG, enable charging
        // REG01: 0b01010001 (OTG=0, CHG=1)
        BQ24297_Write_I2C(0x01, 0b01010001);
        LOG_D("BQ24297_Config_Settings: External power detected, configured for charging mode");
    } else {
        // Battery power only - enable OTG boost, disable charging
        // REG01: 0b01100001 (OTG=1, CHG=0)
        BQ24297_Write_I2C(0x01, 0b01100001);
        LOG_D("BQ24297_Config_Settings: No external power, enabled OTG boost mode");
    }
    
    // Debug: Read back REG01 to verify configuration
    reg = BQ24297_Read_I2C(0x01);
    LOG_D("BQ24297: REG01 after config = 0x%02X", reg);

    // Set fast charge to 2000mA - this should never need to be updated
    BQ24297_Write_I2C(0x02, 0b01100000);

    // Set charge voltage to 4.096V
    BQ24297_Write_I2C(0x04, 0b10010110);

    // Disable watchdog WATCHDOG = 0, set charge timer to 20hr
    // REG05: 0b10001110
    BQ24297_Write_I2C(0x05, 0b10001110);

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

    // If battery is present, enable charging
    BQ24297_ChargeEnable(pData->status.batPresent);

    // Final status check
    reg = BQ24297_Read_I2C(0x01);
    LOG_D("BQ24297_Config_Settings: Complete. REG01 = 0x%02X, OTG = %s, Charge = %s", 
          reg, 
          (reg & 0x20) ? "ON" : "OFF",
          (reg & 0x10) ? "ON" : "OFF");

    pData->initComplete = true;
    LOG_D("BQ24297_Config_Settings: Initialization complete, initComplete set to true");
}

void BQ24297_UpdateStatus(void) {
    uint8_t regData = 0;

    regData = BQ24297_Read_I2C(0x00);
    pData->status.hiZ = (bool) (regData & 0b10000000);
    pData->status.inLim = (uint8_t) (regData & 0b00000111);

    regData = BQ24297_Read_I2C(0x01);
    LOG_D("BQ24297_UpdateStatus: REG01 = 0x%02X", regData);
    pData->status.otg = (bool) (regData & 0b00100000);
    pData->status.chg = (bool) (regData & 0b00010000);

    regData = BQ24297_Read_I2C(0x02);
    pData->status.ichg = (uint8_t) (regData & 0b11111100) >> 2;

    // Make sure we are not still trying to determine the input source
    regData = BQ24297_Read_I2C(0x07);
    pData->status.iinDet_Read = (bool) (regData & 0b10000000);

    regData = BQ24297_Read_I2C(0x08);
    pData->status.vBusStat = (uint8_t) (regData & 0b11000000) >> 6;
    pData->status.chgStat = (uint8_t) (regData & 0b00110000) >> 4;
    pData->status.dpmStat = (bool) (regData & 0b00001000);
    pData->status.pgStat = (bool) (regData & 0b00000100);
    pData->status.thermStat = (bool) (regData & 0b00000010);
    pData->status.vsysStat = (bool) (regData & 0b00000001);

    // First read to REG09 resets faults
    regData = BQ24297_Read_I2C(0x09);

    // Second read to REG09 will send current status
    regData = BQ24297_Read_I2C(0x09);
    pData->status.watchdog_fault = (bool) (regData & 0b10000000);
    pData->status.otg_fault = (bool) (regData & 0b01000000);
    pData->status.chgFault = (uint8_t) (regData & 0b00110000) >> 4;
    pData->status.bat_fault = (bool) (regData & 0b00001000);
    pData->status.ntcFault = (uint8_t) (regData & 0b00000011);
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
        {
            // For some reason, this is required if running from the 
            // bootloader - otherwise I2C hangs
            //vTaskDelay(10 / portTICK_PERIOD_MS); 
            DRV_I2C_WriteReadTransfer(pData->I2C_Handle,
                    pConfigBQ24->I2C_Address,                           
                    I2CData,                                        
                    1,                                                  
                    &rxData,                                            
                    1);
        }

    }
    return (rxData);
}

static void BQ24297_Write_I2C(uint8_t reg, uint8_t txData) {
    uint8_t I2CData[2];

    // Build pData packet
    I2CData[0] = reg;
    I2CData[1] = txData;

    if (pData->I2C_Handle != DRV_HANDLE_INVALID) {
        // Write to selected register
        DRV_I2C_WriteTransfer(                       
                        pData->I2C_Handle,                                  
                        pConfigBQ24->I2C_Address,                           
                        I2CData,                                        
                        2);

    }

}

void BQ24297_EnableOTG(void) {
    // Set GPIO pin for OTG enable
    BATT_MAN_OTG_OutputEnable();  // Make sure it's an output
    BATT_MAN_OTG_Set();          // Set high for OTG enable
    LOG_D("BQ24297_EnableOTG: Set OTG GPIO high (RK5)");
    
    // Read current REG01 value
    uint8_t reg = BQ24297_Read_I2C(0x01);
    
    // Set OTG enable (bit 5) and clear charge enable (bit 4)
    // Preserve watchdog reset bit and SYS_MIN settings
    reg = (reg & 0b11001111) | 0b00100001;  // Clear bits 5:4, then set OTG
    
    BQ24297_Write_I2C(0x01, reg);
    LOG_D("BQ24297_EnableOTG: REG01 = 0x%02X", reg);
    
    // Update local status to reflect the change
    pData->status.otg = true;
    pData->status.chg = false;
}

void BQ24297_DisableOTG(bool enableCharging) {
    // Clear GPIO pin for OTG disable
    BATT_MAN_OTG_OutputEnable();  // Make sure it's an output
    BATT_MAN_OTG_Clear();         // Set low for OTG disable
    LOG_D("BQ24297_DisableOTG: Cleared OTG GPIO (RK5)");
    
    // Read current REG01 value
    uint8_t reg = BQ24297_Read_I2C(0x01);
    
    // Clear OTG enable (bit 5)
    reg = reg & 0b11011111;
    
    // Set charge enable if requested and battery is present
    if (enableCharging && pData->status.batPresent && pData->chargeAllowed) {
        reg = reg | 0b00010000;  // Set bit 4
    }
    
    BQ24297_Write_I2C(0x01, reg);
    LOG_D("BQ24297_DisableOTG: REG01 = 0x%02X, charging = %s", 
          reg, (reg & 0x10) ? "enabled" : "disabled");
    
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
    // Log current state before change
    uint8_t reg = BQ24297_Read_I2C(0x01);
    LOG_D("BQ24297_SetPowerMode: Current REG01=0x%02X (OTG=%d, CHG=%d)", 
          reg, (reg & 0x20) ? 1 : 0, (reg & 0x10) ? 1 : 0);
    
    if (externalPowerPresent) {
        // External power available - disable OTG and enable charging
        LOG_D("BQ24297_SetPowerMode: External power detected, switching to charge mode");
        BQ24297_DisableOTG(true);
        
        // Verify the change
        reg = BQ24297_Read_I2C(0x01);
        LOG_D("BQ24297_SetPowerMode: After switch REG01=0x%02X (OTG=%d, CHG=%d)", 
              reg, (reg & 0x20) ? 1 : 0, (reg & 0x10) ? 1 : 0);
    } else {
        // Battery power only - enable OTG 
        // Testing shows device powers off without OTG when on battery
        // Theory: OTG may be required to maintain power path from battery to system
        LOG_D("BQ24297_SetPowerMode: No external power, enabling OTG mode");
        BQ24297_EnableOTG();
        
        // Verify the change
        reg = BQ24297_Read_I2C(0x01);
        LOG_D("BQ24297_SetPowerMode: After switch REG01=0x%02X (OTG=%d, CHG=%d)", 
              reg, (reg & 0x20) ? 1 : 0, (reg & 0x10) ? 1 : 0);
    }
}