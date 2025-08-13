/*! @file PowerApi.c 
 * 
 * This file implements the functions to manage power API
 */

#include "HAL/Power/PowerApi.h"
#include "definitions.h"
#include "state/board/BoardConfig.h"
#include "state/data/BoardData.h"
#include "HAL/ADC.h"
#include "HAL/BQ24297/BQ24297.h"
#include "Util/Logger.h"
#include "../../services/wifi_services/wifi_manager.h"
#include "../../services/UsbCdc/UsbCdc.h"
#include <xc.h>
//typedef enum
//{
//    /* Source of clock is internal fast RC */
//    SYS_CLK_SOURCE_FRC,
//
//    /* Source of clock is internal fast RC multiplied by system PLL */
//    SYS_CLK_SOURCE_FRC_SYSPLL,
//
//    /* Source of clock is primary oscillator */
//    SYS_CLK_SOURCE_PRIMARY,
//
//    /* Source of clock is primary oscillator multiplied by
//    the System PLL value and divided by the divisor configured by software */
//    SYS_CLK_SOURCE_PRIMARY_SYSPLL,
//
//    /* Source of clock is secondary oscillator */
//    SYS_CLK_SOURCE_SECONDARY,
//
//    /* Source of clock is internal low power RC */
//    SYS_CLK_SOURCE_LPRC,
//
//    /* Source of clock is internal fast RC divided by the divisor
//    configured in software */
//    SYS_CLK_SOURCE_FRC_BY_16,
//
//    /* Source of clock is internal fast RC divided by the divisor
//    configured in software */
//    SYS_CLK_SOURCE_FRC_BY_DIV,
//
//    /* Source of clock is backup fast RC */
//    SYS_CLK_SOURCE_BKP_FRC,
//
//    /* Source of clock is USB PLL output configured in software */
//    SYS_CLK_SOURCE_UPLL,
//
//    /* Source of clock is none*/
//    SYS_CLK_SOURCE_NONE,
//
//} CLK_SOURCES_SYSTEM;
#define SYS_CLK_CONFIG_FREQ_ERROR_LIMIT     10
#define SYS_CLK_CONFIG_PRIMARY_XTAL         24000000ul
#define BATT_EXH_TH 5.0
//! 10% or ~3.2V
#define BATT_LOW_TH 10.0
//! Battery must be charged at least this value higher than BATT_LOW_TH
#define BATT_LOW_HYST 10.0 

//! Pointer to a data structure for storing the configuration data
static tPowerConfig *pConfig;
//! Pointer to a data structure with all the data fields
static tPowerData *pData;
//! Pointer to a data structure with all the write variable data fields
static tPowerWriteVars *pWriteVariables;

///*! 
// * Funtion to write in power channel
// */
//static void Power_Write( void );
/*!
 * Function to active power capabilities
 */
static void Power_Up(void);
/*!
 * Function to turn off power capabilities
 */
static void Power_Down(void);
/*!
 * Function to upate power state
 */
static void Power_UpdateState(void);
/*!
 * Function to update charge percentage
 */
static void Power_UpdateChgPct(void);
/*!
 * Function to update the configuration
 */
static void Power_Update_Settings(void);

// Removed generic GPIO functions - using Harmony macros instead

bool ReadGpioPinStateLatched(GPIO_PORT port, uint32_t mask) {
    return GPIO_PortLatchRead(port)& (1 << mask);
}

void Power_Init(
        tPowerConfig *pInitConfig,
        tPowerData *pInitData,
        tPowerWriteVars *pInitVars) {
    pConfig = pInitConfig;
    pData = pInitData;
    pWriteVariables = pInitVars;

    // NOTE: This is called before the RTOS is running.  
    // Don't call any RTOS functions here!
    BQ24297_InitHardware(
            &pConfig->BQ24297Config,
            &pWriteVariables->BQ24297WriteVars,
            &(pData->BQ24297Data));

    // CRITICAL: Force 3.3V rail ON regardless of initial config
    // The microcontroller is running, so 3.3V must already be on
    // This ensures it stays on during USB disconnect
    PWR_3_3V_EN_OutputEnable();  // Set RH12 as output
    PWR_3_3V_EN_Set();          // Drive 3.3V_EN high
    pWriteVariables->EN_3_3V_Val = true;
    
    // Initialize other power pins to their default states
    // Note: These would need their own macros if they exist
    // For now, keeping the basic initialization
}

void Power_Tasks(void) {
    // If we haven't initialized the battery management settings, do so now
    if (pData->BQ24297Data.initComplete == false) {
        BQ24297_Config_Settings();
    }
    
    // CRITICAL SAFETY CHECKS: Ensure HIZ mode is cleared and BATFET is enabled
    
    // Check REG00 for HIZ mode - if set (1), it blocks charging and can cause power loss
    uint8_t reg00 = BQ24297_Read_I2C(0x00);
    if (reg00 != 0xFF && (reg00 & 0x80)) {
        LOG_D("Power_Tasks: HIZ mode detected - clearing");
        reg00 &= 0x7F;  // Clear bit 7 to exit HIZ mode
        BQ24297_Write_I2C(0x00, reg00);
        
        // Force a power settings update after clearing HIZ
        BQ24297_UpdateStatus();
        Power_Update_Settings();
    }
    
    // Check REG07 bit 5 - if set (1), BATFET is disabled which will cause power loss
    uint8_t reg07 = BQ24297_Read_I2C(0x07);
    if (reg07 != 0xFF && (reg07 & 0x20)) {
        LOG_D("Power_Tasks: BATFET disabled - re-enabling");
        reg07 &= 0xDF;  // Clear bit 5 to enable BATFET
        BQ24297_Write_I2C(0x07, reg07);
    }

    // Rate limit Power_UpdateState to 1 second intervals
    // This provides adequate safety monitoring while reducing unnecessary LED activity
    // Hardware resettable fuses provide protection against shorts/overcurrent
    static TickType_t lastUpdateTime = 0;
    TickType_t currentTime = xTaskGetTickCount();
    if ((currentTime - lastUpdateTime) >= (1000 / portTICK_PERIOD_MS)) {
        lastUpdateTime = currentTime;
        Power_UpdateState();
    }
}

void Power_USB_Sleep_Update(bool sleep) {
    pData->USBSleep = sleep;
}

void Power_Write(void) {
    // Current power state values

    bool EN_3_3V_Val_Current;
    bool EN_5_10V_Val_Current;
    bool EN_12V_Val_Current;
    bool EN_Vref_Val_Current;

    EN_3_3V_Val_Current = ReadGpioPinStateLatched(pConfig->EN_3_3V_Ch, pConfig->EN_3_3V_Bit);
    EN_5_10V_Val_Current = ReadGpioPinStateLatched(pConfig->EN_5_10V_Ch, pConfig->EN_5_10V_Bit);
    EN_12V_Val_Current = ReadGpioPinStateLatched(pConfig->EN_12V_Ch, pConfig->EN_12V_Bit);
    EN_Vref_Val_Current = ReadGpioPinStateLatched(pConfig->EN_Vref_Ch, pConfig->EN_Vref_Bit);

    // Check to see if we are changing the state of this power pin
    if (EN_3_3V_Val_Current != pWriteVariables->EN_3_3V_Val) {
        
        // CRITICAL: Set pin direction and value using Harmony macros
        if (pWriteVariables->EN_3_3V_Val) {
            // Set as OUTPUT and drive high
            PWR_3_3V_EN_OutputEnable();
            PWR_3_3V_EN_Set();
        } else {
            // Clear the pin (will set as INPUT later in Power_Down())
            PWR_3_3V_EN_Clear();
        }
        
    }

    // Check to see if we are changing the state of these power pins
    if (EN_5_10V_Val_Current != pWriteVariables->EN_5_10V_Val) {
        // PWR_5V_EN uses Port D, bit 0
        if (pWriteVariables->EN_5_10V_Val) {
            PWR_5V_EN_OutputEnable();
            PWR_5V_EN_Set();
        } else {
            PWR_5V_EN_Clear();
        }
    }
    
    if (EN_12V_Val_Current != pWriteVariables->EN_12V_Val) {
        // PWR_12V_EN uses Port H, bit 15
        if (pWriteVariables->EN_12V_Val) {
            PWR_12V_EN_OutputEnable();
            PWR_12V_EN_Set();
        } else {
            PWR_12V_EN_Clear();
        }
    }

    // Check to see if we are changing the state of this power pin
    if (EN_Vref_Val_Current != pWriteVariables->EN_Vref_Val) {
        // PWR_VREF_EN uses Port J, bit 15
        if (pWriteVariables->EN_Vref_Val) {
            PWR_VREF_EN_OutputEnable();
            PWR_VREF_EN_Set();
        } else {
            PWR_VREF_EN_Clear();
        }
    }

}

static void Power_Up(void) {
    
    // If the battery management is not enabled, wait for it to become ready
    while (!pData->BQ24297Data.initComplete) {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }

    //Delay after turning up to full speed to allow steady-state
    //before powering system
    vTaskDelay(50 / portTICK_PERIOD_MS);

    // 3.3V Enable
    pWriteVariables->EN_3_3V_Val = true;
    Power_Write();
    

    // 5V Enable
    if ((pData->BQ24297Data.status.batPresent) ||
            (pData->BQ24297Data.status.vBusStat == VBUS_CHARGER)) {
        pWriteVariables->EN_5_10V_Val = true;
    }
    Power_Write();
    vTaskDelay(50 / portTICK_PERIOD_MS);

    // 12V Enable (set low to turn on, set as input (or high if configured
    // as open collector) to turn off)
    pWriteVariables->EN_12V_Val = false;
    Power_Write();
    vTaskDelay(50 / portTICK_PERIOD_MS);

    // Vref Enable
    pWriteVariables->EN_Vref_Val = true;
    Power_Write();
    vTaskDelay(50 / portTICK_PERIOD_MS);

    pData->powerState = POWERED_UP;
    pData->requestedPowerState = NO_CHANGE; // Reset the requested power state after handling request
}

void Power_Down(void) {
    
    // 3.3V Disable - if powered externally, board will stay on and go to low power state, else off completely
    pWriteVariables->EN_3_3V_Val = false;
    // 5V Disable
    pWriteVariables->EN_5_10V_Val = false;
    // 12V Disable (set low to turn on, set as input (or high if configured as open collector) to turn off)
    pWriteVariables->EN_12V_Val = true;
    // Vref Disable
    pWriteVariables->EN_Vref_Val = false;
    Power_Write();
    
    // CRITICAL: Set 3.3V_EN pin as INPUT for power down
    PWR_3_3V_EN_InputEnable();

    pData->powerState = STANDBY; // Set back to default state
    pData->requestedPowerState = NO_CHANGE; // Reset the requested power state after handling request

    // Delay 1000ms for power to discharge
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

static void Power_UpdateState(void) {
    static POWER_STATE lastLoggedState = -1;
    
    // Monitor 3.3V_EN GPIO state changes
    static bool last3v3State = false;
    static bool first3v3Check = true;
    bool current3v3State = ReadGpioPinStateLatched(pConfig->EN_3_3V_Ch, pConfig->EN_3_3V_Bit);
    
    if (first3v3Check || (current3v3State != last3v3State)) {
        
        last3v3State = current3v3State;
        first3v3Check = false;
    }
    
    // Log state changes
    if (pData->powerState != lastLoggedState) {
        LOG_D("Power_UpdateState: State changed from %d to %d", lastLoggedState, pData->powerState);
        lastLoggedState = pData->powerState;
    }
    
    // Set power state immediately if DO_POWER_DOWN is requested
    if (pData->requestedPowerState == DO_POWER_DOWN) {
        pData->powerState = STANDBY;
    }

    switch (pData->powerState) {
        case STANDBY:
        {
            /* Standby/Off state
             * - On USB power: MCU stays on (3.3V enabled) 
             * - On battery: MCU powers off (3.3V disabled)
             * This is the default state if code is running on USB power
             */
            
            // Check to see if we've finished signaling the user of insufficient power if necessary
            if (pData->powerDnAllowed == true) {
                Power_Down();
                break;  // Exit early if powering down
            }

            // Update BQ24297 status and external power source detection
            // Rate limit to reduce log spam during STANDBY state
            static TickType_t lastStandbyUpdate = 0;
            TickType_t currentTime = xTaskGetTickCount();
            if ((currentTime - lastStandbyUpdate) >= (1000 / portTICK_PERIOD_MS)) {
                lastStandbyUpdate = currentTime;
                BQ24297_UpdateStatus();
                Power_Update_Settings();
            }
            
            if (pData->requestedPowerState == DO_POWER_UP || 
                pData->requestedPowerState == DO_POWER_UP_EXT_DOWN) {

                // Power up if battery > 3.0V OR external power present
                      
                if (!pData->BQ24297Data.status.vsysStat ||
                        pData->BQ24297Data.status.pgStat)
                {
                    Power_Up();
                    // If requested state is DO_POWER_UP_EXT_DOWN, we'll transition to that state
                    // once we're in POWERED_UP state (handled in POWERED_UP case)
                } else {
                    // Otherwise insufficient power.  Notify user and power down
                    LOG_D("Power_UpdateState: Insufficient power - battery < 3.0V and no external power");
                    pData->powerDnAllowed = false;  // This will turn true after the LED sequence completes
                    // Already in STANDBY state, no need to set it again
                    pData->requestedPowerState = NO_CHANGE;    // Reset the requested power state
                }
                // Don't reset requestedPowerState if DO_POWER_UP_EXT_DOWN - let POWERED_UP state handle it
                if (pData->requestedPowerState == DO_POWER_UP) {
                    pData->requestedPowerState = NO_CHANGE;
                }
            }
        }
            break;

        case POWERED_UP:
            /* Board fully powered. Monitor for any changes/faults
             * ADC readings are now valid!
             */
            // Check if we need to immediately transition to POWERED_UP_EXT_DOWN
            if (pData->requestedPowerState == DO_POWER_UP_EXT_DOWN) {
                // Immediately transition to POWERED_UP_EXT_DOWN state
                pWriteVariables->EN_5_10V_Val = false;
                Power_Write();
                pData->powerState = POWERED_UP_EXT_DOWN;
                pData->requestedPowerState = NO_CHANGE;
                break;  // Exit early to avoid the rest of POWERED_UP processing
            }
            
            if (pData->requestedPowerState == DO_POWER_UP) pData->requestedPowerState = NO_CHANGE; // We are already powered so just reset the flag
            
            // Rate limit status updates to reduce log spam
            static TickType_t lastStatusUpdate = 0;
            TickType_t currentTime = xTaskGetTickCount();
            if ((currentTime - lastStatusUpdate) >= (1000 / portTICK_PERIOD_MS)) { // Update every 1 second
                lastStatusUpdate = currentTime;
                Power_UpdateChgPct();
                BQ24297_UpdateStatus();
                Power_Update_Settings();  // Check for power source changes
            }
            
            // Only trust battery percentage if we have a valid voltage reading
            bool validBatteryReading = (pData->battVoltage > 2.5);  // Li-ion can't be < 2.5V
            
            // Use BQ24297 power detection only - MCU VBUS detection is unreliable
            bool hasExternalPower = pData->BQ24297Data.status.pgStat;
            
            if (pData->requestedPowerState == DO_POWER_UP_EXT_DOWN ||
                    (validBatteryReading && pData->chargePct < BATT_LOW_TH &&
                    !hasExternalPower)) {
                // If battery is low on charge and we are not plugged in, disable external supplies
                pWriteVariables->EN_5_10V_Val = false;
                Power_Write();
                pData->powerState = POWERED_UP_EXT_DOWN;
                // Reset the requested power state after handling request
                pData->requestedPowerState = NO_CHANGE;
            }
            break;

        case POWERED_UP_EXT_DOWN:
            /* Board partially powered. Monitor for any changes */
            Power_UpdateChgPct();
            
            // Only trust battery percentage if we have a valid voltage reading
            validBatteryReading = (pData->battVoltage > 2.5);  // Li-ion can't be < 2.5V
            if (pData->chargePct > (BATT_LOW_TH + BATT_LOW_HYST) ||
                    (pData->BQ24297Data.status.inLim > 1)) {
                if (pData->requestedPowerState == DO_POWER_UP) {
                    // If battery is charged or we are plugged in, enable external supplies
                    pWriteVariables->EN_5_10V_Val = true;
                    Power_Write();
                    pData->powerState = POWERED_UP;
                    // Reset the requested power state after handling request
                    pData->requestedPowerState = NO_CHANGE;
                }

                // Else, remain here because the user didn't want to be fully powered
            } else if (validBatteryReading && pData->chargePct < BATT_EXH_TH) {
                // Only shut down if we have a valid battery reading AND it's truly exhausted
                // Code below is commented out when I2C is disabled
                // Insufficient power.  Notify user and power down.
                LOG_D("Power_UpdateState: Battery exhausted (%.1f%% < %.1f%%), transitioning to STANDBY", 
                      pData->chargePct, BATT_EXH_TH);
                pData->powerDnAllowed = false; // This will turn true after the LED sequence completes
                pData->powerState = STANDBY;
            }

            break;
    }

}

static void Power_UpdateChgPct(void) {
    size_t index = ADC_FindChannelIndex(ADC_CHANNEL_VBATT);
    const AInSample *pAnalogSample = BoardData_Get(
            BOARDDATA_AIN_LATEST,
            index);
    if (NULL != pAnalogSample) {
        float newVoltage = ADC_ConvertToVoltage(pAnalogSample);
        // Only update if we have a valid reading (not 0V)
        if (newVoltage > 0.1) {
            pData->battVoltage = newVoltage;
        }
    }

    // Function below is defined from 3.17-3.868.  Must coerce input value to within these bounds.
    if (pData->battVoltage < 3.17) {
        pData->chargePct = 0;
    } else if (pData->battVoltage > 3.868) {
        pData->chargePct = 100;
    } else {
        pData->chargePct = 142.92 * pData->battVoltage - 452.93;
    }
}

static void Power_Update_Settings(void) {
    // Change charging/other power settings based on current status

    // Check if input detection is in progress (DPM_STAT = 1)
    if (pData->BQ24297Data.status.dpmStat) {
        // Don't make power state changes while input detection is active
        return;
    }

    

    // Update external power source based on BQ24297 detection
    if (pData->BQ24297Data.status.pgStat) {
        // External power is present - determine type from vBusStat
        switch (pData->BQ24297Data.status.vBusStat) {
            case 0b01: // USB host
                // Check actual current limit to distinguish USB types
                if (pData->BQ24297Data.status.inLim <= ILim_150) {
                    pData->externalPowerSource = USB_100MA_EXT_POWER;
                } else {
                    pData->externalPowerSource = USB_500MA_EXT_POWER;
                }
                break;
            case 0b10: // Adapter/charger
                pData->externalPowerSource = CHARGER_2A_EXT_POWER;
                break;
            case 0b11: // OTG
                // OTG mode active - but if pgStat=1, external power is actually present
                // This happens during transition from OTG to charge mode
                pData->externalPowerSource = USB_500MA_EXT_POWER;
                break;
            case 0b00: // Unknown but power good
            default:
                // Default to USB when power is good but type unknown
                pData->externalPowerSource = USB_500MA_EXT_POWER;
                break;
        }
    } else {
        // No external power
        pData->externalPowerSource = NO_EXT_POWER;
    }

    // Check new power source and set parameters accordingly
    BQ24297_AutoSetILim();
    
    // Update BQ24297 power mode based on external power availability
    // This will handle charging enable/disable appropriately
    BQ24297_SetPowerMode(pData->BQ24297Data.status.pgStat);

    // Automatic OTG mode transitions are disabled
    // OTG can only be controlled manually via SCPI commands
    // This prevents unexpected power state changes during USB disconnect/reconnect
}


