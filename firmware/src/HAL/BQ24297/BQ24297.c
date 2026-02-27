/* 
 * @file   BQ24297.c
 * @brief This file manages the BQ24297 module
 */
#define LOG_LVL LOG_LEVEL_BQ24297

#include "BQ24297.h"
#include "Util/Logger.h"
#include "services/UsbCdc/UsbCdc.h"
#include "peripheral/gpio/plib_gpio.h"
#include "semphr.h"

//! Pointer to the BQ24297 device configuration data structure
static tBQ24297Config *pConfigBQ24;
//! Pointer to the BQ24297 device "Write Variables" data structure
static tBQ24297WriteVars *pWriteVariables;
//! Pointer to the data structure where the BQ24297 data is
static tBQ24297Data *pData;
//! Mutex to synchronize I2C access between PowerAndUITask and USBDeviceTask
static SemaphoreHandle_t i2cMutex = NULL;

/*! Function to read BQ24297 data by I2C communication
 * @param[in] reg Register to read
 */
uint8_t BQ24297_Read_I2C(uint8_t reg);

/*! Function to write BQ24297 data by I2C communication
 * @param[in] reg Register to write
 * @param[in] txData Data to write
 * @return true if write succeeded, false on error
 */
bool BQ24297_Write_I2C(uint8_t reg, uint8_t txData);

/**
 * OR a raw REG09 value into accumulated (sticky) fault fields.
 * Called for both latched and current reads so no fault is ever lost.
 */
static void AccumulateFaults(uint8_t reg09) {
    if (reg09 & 0x80) pData->status.watchdog_faultAccum = true;
    if (reg09 & 0x40) pData->status.otg_faultAccum = true;
    uint8_t chgF = (reg09 >> 4) & 0x03;
    if (chgF > (uint8_t)pData->status.chgFaultAccum)
        pData->status.chgFaultAccum = (enum eChargeFault)chgF;
    if (reg09 & 0x08) pData->status.bat_faultAccum = true;
    uint8_t ntcF = reg09 & 0x03;
    if (ntcF != NTC_FAULT_NORMAL)
        pData->status.ntcFaultAccum = (enum eNTCFault)ntcF;
}

void BQ24297_InitHardware(                                                  
                        tBQ24297Config *pConfigInit,                        
                        tBQ24297WriteVars *pWriteInit,                      
                        tBQ24297Data *pDataInit) {
    pConfigBQ24 = pConfigInit;
    pWriteVariables = pWriteInit;
    pData = pDataInit;

    // Create I2C mutex for cross-task synchronization
    if (i2cMutex == NULL) {
        i2cMutex = xSemaphoreCreateMutex();
        if (i2cMutex == NULL) {
            LOG_E("BQ24297_InitHardware: failed to create I2C mutex");
            pData->initComplete = false;
            return;
        }
    }

    // Battery management initialization (hardware interface)

    // ***Disable I2C calls*** as Harmony 2.06 doesn't have a working, 
    // interrupt-safe implementation
    // Open the I2C Driver for Master
    pData->I2C_Handle = DRV_I2C_Open(                                       
                        pConfigBQ24->I2C_Index,                             
                        DRV_IO_INTENT_READWRITE|DRV_IO_INTENT_BLOCKING);

    // Drive OTG LOW — DPDM will set 100mA (conservative).
    // ManageIINLIM state machine overrides IINLIM via I2C after DPDM completes.
    BATT_MAN_OTG_OutputEnable();
    BATT_MAN_OTG_Clear();
}

bool BQ24297_ReadFaultReg(uint8_t *latched, uint8_t *current) {
    uint8_t latchedVal = BQ24297_Read_I2C(0x09);
    if (latchedVal == 0xFF) {
        return false;  // First read failed — don't touch any fields
    }

    // Accumulate faults from the latched read
    AccumulateFaults(latchedVal);
    if (latched != NULL) *latched = latchedVal;

    // Second read gets currently active faults
    uint8_t currentVal = BQ24297_Read_I2C(0x09);
    if (currentVal == 0xFF) {
        // Second read failed — use latched value for status fields
        LOG_E("BQ24297_ReadFaultReg: second REG09 read failed, using latched");
        currentVal = latchedVal;
    } else {
        AccumulateFaults(currentVal);
    }

    if (current != NULL) *current = currentVal;

    // Decode current value into status fields (existing behavior)
    pData->status.watchdog_fault = (bool)(currentVal & 0x80);
    pData->status.otg_fault = (bool)(currentVal & 0x40);
    pData->status.chgFault = (enum eChargeFault)((currentVal >> 4) & 0x03);
    pData->status.bat_fault = (bool)(currentVal & 0x08);
    pData->status.ntcFault = (enum eNTCFault)(currentVal & 0x03);

    return true;
}

void BQ24297_ClearAccumulatedFaults(void) {
    pData->status.watchdog_faultAccum = false;
    pData->status.otg_faultAccum = false;
    pData->status.chgFaultAccum = CHG_FAULT_NORMAL;
    pData->status.bat_faultAccum = false;
    pData->status.ntcFaultAccum = NTC_FAULT_NORMAL;
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
    
    // Drive OTG LOW — DPDM sets 100mA (conservative), ManageIINLIM overrides via I2C
    BATT_MAN_OTG_OutputEnable();
    BATT_MAN_OTG_Clear();

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

    // Read the current status
    BQ24297_UpdateStatus();

    // Start with conservative 500mA IINLIM
    // BQ24297_ManageIINLIM() state machine will adjust based on USB enumeration
    BQ24297_SetIINLIM(ILim_500);
    pData->iinlimState = IINLIM_STATE_IDLE;
    pData->iinlimTimestamp = 0;
    pData->iinlimLastVbus = false;

    // Update battery presence status (for reporting only)
    BQ24297_UpdateBatteryStatus();

    // Clear accumulated faults from init reads — start fresh
    BQ24297_ClearAccumulatedFaults();

    // Charging is always enabled via REG01 configuration above.
    // The BQ24297 has built-in hardware protection for:
    // - Battery temperature (NTC monitoring)
    // - Overvoltage/overcurrent
    // - Battery presence detection
    // Let the hardware handle charging decisions autonomously.

    // Mark initialization complete
    pData->initComplete = true;
    
    LOG_D("BQ24297_Config_Settings: Initialization complete");
}

/*
 * Updates BQ24297 status by reading all registers via I2C.
 * Called at boot, and on-demand by ForceDPDM, DisableOTG,
 * IsBatteryPresent, and UpdateBatteryStatus.
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

    // REG09: Fault Status — use centralized reader that accumulates faults
    if (!BQ24297_ReadFaultReg(NULL, NULL)) {
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

void BQ24297_ChargeEnable(bool chargeEnable) {
    // Temporary value to hold current register value
    uint8_t reg = 0;

    reg = BQ24297_Read_I2C(0x01);

    // OTG and Charge are mutually exclusive
    if (reg & 0x20) {
        return;  // OTG is enabled, cannot modify charge state
    }

    if (pData->chargeAllowed && chargeEnable && pData->status.batPresent) {
        // Enable charging, preserve all bits except bit 4 (charge enable)
        reg = (reg & 0b11101111) | 0b00010000; // Clear bit 4, then set it
        BQ24297_Write_I2C(0x01, reg);
    } else {
        // Disable charging, preserve all bits except bit 4 (charge enable)
        reg = (reg & 0b11101111); // Just clear bit 4
        BQ24297_Write_I2C(0x01, reg);
    }
}

void BQ24297_ForceDPDM(void) {
    uint8_t reg = 0;
    // WARNING: Forcing DPDM while connected via USB will disrupt USB comms
    // and may require physical cable replug. Only safe from wall charger or UART.

    reg = BQ24297_Read_I2C(0x07);
    if (reg == 0xFF) {
        LOG_E("BQ24297_ForceDPDM: REG07 read failed");
        return;
    }

    // Force DPDM detection — REG07 bit 7
    if (!BQ24297_Write_I2C(0x07, reg | 0x80)) {
        LOG_E("BQ24297_ForceDPDM: REG07 write failed");
        return;
    }

    const TickType_t start = xTaskGetTickCount();
    const TickType_t timeoutTicks = pdMS_TO_TICKS(2000);

    for (;;) {
        BQ24297_UpdateStatus();

        if (!pData->status.iinDet_Read) {
            break;
        }

        if ((xTaskGetTickCount() - start) > timeoutTicks) {
            LOG_E("BQ24297_ForceDPDM: timeout waiting for DPDM to complete");
            break;
        }

        vTaskDelay(pdMS_TO_TICKS(50));
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

    if (pData->I2C_Handle == DRV_HANDLE_INVALID) {
        LOG_E("BQ24297_Read_I2C: Invalid I2C handle");
        return 0xFF;
    }

    if (i2cMutex != NULL) {
        xSemaphoreTake(i2cMutex, portMAX_DELAY);
    }

    // Build data packet
    I2CData[0] = reg;

    // Perform I2C transfer with error checking
    bool success = DRV_I2C_WriteReadTransfer(pData->I2C_Handle,
            pConfigBQ24->I2C_Address,
            I2CData,
            1,
            &rxData,
            1);

    if (i2cMutex != NULL) {
        xSemaphoreGive(i2cMutex);
    }

    if (!success) {
        LOG_E("BQ24297_Read_I2C: Failed to read register 0x%02X", reg);
        return 0xFF;
    }

    return rxData;
}

bool BQ24297_Write_I2C(uint8_t reg, uint8_t txData) {
    uint8_t I2CData[2];

    if (pData->I2C_Handle == DRV_HANDLE_INVALID) {
        LOG_E("BQ24297_Write_I2C: Invalid I2C handle");
        return false;
    }

    if (i2cMutex != NULL) {
        xSemaphoreTake(i2cMutex, portMAX_DELAY);
    }

    // Build data packet
    I2CData[0] = reg;
    I2CData[1] = txData;

    // Write to selected register with error checking
    bool success = DRV_I2C_WriteTransfer(
                    pData->I2C_Handle,
                    pConfigBQ24->I2C_Address,
                    I2CData,
                    2);

    if (i2cMutex != NULL) {
        xSemaphoreGive(i2cMutex);
    }

    if (!success) {
        LOG_E("BQ24297_Write_I2C: Failed to write 0x%02X to register 0x%02X", txData, reg);
    }

    return success;
}

bool BQ24297_SetIINLIM(uint8_t iinlimCode) {
    // REG00 IINLIM is 3-bit field (bits 2:0), valid range 0-7 per datasheet Table 9-2
    if (iinlimCode > ILim_3000) return false;
    uint8_t reg = BQ24297_Read_I2C(0x00);
    if (reg == 0xFF) return false;
    // Clear HIZ (bit 7) and IINLIM (bits 2:0), preserve VINDPM (bits 6:3)
    reg = (reg & 0b01111000) | (iinlimCode & 0x07);
    bool success = BQ24297_Write_I2C(0x00, reg);
    if (!success) {
        LOG_E("BQ24297_SetIINLIM: Failed to write IINLIM code %u", iinlimCode);
    }
    return success;
}

void BQ24297_ManageIINLIM(bool vbusPresent) {
    TickType_t now = xTaskGetTickCount();

    switch (pData->iinlimState) {
        case IINLIM_STATE_IDLE:
            if (vbusPresent && !pData->iinlimLastVbus) {
                // Rising edge — VBUS just appeared
                LOG_D("IINLIM: VBUS appeared, entering WAIT_DPDM");
                pData->iinlimTimestamp = now;
                pData->iinlimState = IINLIM_STATE_WAIT_DPDM;
            }
            break;

        case IINLIM_STATE_WAIT_DPDM:
            if (!vbusPresent) {
                LOG_D("IINLIM: VBUS lost during WAIT_DPDM, returning to IDLE");
                pData->iinlimState = IINLIM_STATE_IDLE;
                break;
            }
            {
                TickType_t elapsed = now - pData->iinlimTimestamp;

                // Wait at least 1s after VBUS before acting on DPDM result
                // to ensure BQ24297 hardware has fully settled
                if (elapsed < pdMS_TO_TICKS(1000)) {
                    break;
                }

                // Check REG07 bit 7 for DPDM in progress
                uint8_t reg07 = BQ24297_Read_I2C(0x07);
                bool dpdmDone = (reg07 != 0xFF) && !(reg07 & 0x80);
                bool timeout = elapsed >= pdMS_TO_TICKS(3000);

                if (dpdmDone) {
                    // DPDM finished — safe to set IINLIM, hardware won't overwrite
                    if (BQ24297_SetIINLIM(ILim_500)) {
                        LOG_D("IINLIM: DPDM done after %lu ms, set 500mA, entering WAIT_USB",
                              (unsigned long)(elapsed * portTICK_PERIOD_MS));
                        pData->iinlimTimestamp = now;
                        pData->iinlimState = IINLIM_STATE_WAIT_USB;
                    } else {
                        LOG_E("IINLIM: Failed to set 500mA, retrying next cycle");
                    }
                } else if (timeout) {
                    // DPDM stuck after 3s — set IINLIM anyway
                    LOG_E("IINLIM: DPDM not complete after 3s (REG07=0x%02X), forcing 500mA",
                          reg07);
                    if (BQ24297_SetIINLIM(ILim_500)) {
                        pData->iinlimTimestamp = now;
                        pData->iinlimState = IINLIM_STATE_WAIT_USB;
                    } else {
                        LOG_E("IINLIM: Failed to set 500mA on timeout, retrying next cycle");
                    }
                }
            }
            break;

        case IINLIM_STATE_WAIT_USB:
            if (!vbusPresent) {
                BQ24297_SetIINLIM(ILim_500);
                LOG_D("IINLIM: VBUS lost during WAIT_USB, reset to 500mA");
                pData->iinlimState = IINLIM_STATE_IDLE;
                break;
            }
            if ((now - pData->iinlimTimestamp) >= pdMS_TO_TICKS(2000)) {
                if (UsbCdc_IsConfigured()) {
                    // USB host enumerated — keep 500mA (USB-spec safe)
                    LOG_D("IINLIM: USB configured, keeping 500mA");
                    pData->iinlimState = IINLIM_STATE_SETTLED;
                } else {
                    // No USB enumeration — wall charger, bump to 2000mA
                    if (BQ24297_SetIINLIM(ILim_2000)) {
                        LOG_D("IINLIM: No USB config, set 2000mA (wall charger)");
                        pData->iinlimState = IINLIM_STATE_SETTLED;
                    } else {
                        LOG_E("IINLIM: Failed to set 2000mA, retrying next cycle");
                    }
                }
            }
            break;

        case IINLIM_STATE_SETTLED:
            if (!vbusPresent) {
                BQ24297_SetIINLIM(ILim_500);
                LOG_D("IINLIM: VBUS lost, reset to 500mA");
                pData->iinlimState = IINLIM_STATE_IDLE;
            }
            break;

        default:
            pData->iinlimState = IINLIM_STATE_IDLE;
            break;
    }

    pData->iinlimLastVbus = vbusPresent;
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
    bool oldBatPresent = pData->status.batPresent;
    bool oldChargeAllowed = pData->chargeAllowed;

    pData->status.batPresent = BQ24297_IsBatteryPresent();
    pData->chargeAllowed = pData->status.batPresent &&
                           (pData->status.ntcFault != NTC_FAULT_HOT);

    // Enforce charge state via I2C when chargeAllowed changes
    if (pData->chargeAllowed != oldChargeAllowed) {
        BQ24297_ChargeEnable(pData->chargeAllowed);
        LOG_D("BQ24297: Charging %s",
              pData->chargeAllowed ? "enabled" : "disabled");
    }

    if (oldBatPresent != pData->status.batPresent) {
        LOG_D("BQ24297: Battery %s",
              pData->status.batPresent ? "detected" : "not detected");
    }
}

void BQ24297_SetPowerMode(bool externalPowerPresent) {
    static bool lastExternalPowerState = false;
    static bool initialized = false;

    // Only log when state actually changes
    if (!initialized || lastExternalPowerState != externalPowerPresent) {
        if (externalPowerPresent) {
            LOG_D("BQ24297_SetPowerMode: External power detected");
        } else {
            LOG_D("BQ24297_SetPowerMode: Battery power only");
        }
        lastExternalPowerState = externalPowerPresent;
        initialized = true;
    }

    // OTG mode is disabled by default in the BQ24297 and set to disabled at init.
    // Charging is always enabled and handled autonomously by the hardware.
    // No action needed here - just logging state changes above.
}
