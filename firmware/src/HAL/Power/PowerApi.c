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
//! Battery threshold for turning off external power to conserve energy (15%)
#define BATT_EXT_DOWN_TH 15.0
//! Battery threshold for device shutdown - absolute minimum (5%)
#define BATT_LOW_TH 5.0
//! Hysteresis for battery thresholds - must charge this much above threshold to re-enable (2%)
#define BATT_HYST 2.0 

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
 * @param enableExtPower If true, enable 5V/10V external power. If false, keep external power disabled.
 */
static void Power_Up(bool enableExtPower);
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

static void Power_Up(bool enableExtPower) {
    
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
    

    // 5V Enable - based on request and power availability
    // Enable if: requested AND (external power present OR battery above conservation threshold)
    // Disable if: not requested (user wants POWERED_UP_EXT_DOWN state) OR battery too low
    if (enableExtPower) {
        // User wants external power - check if we should enable it
        bool hasExternalPower = pData->BQ24297Data.status.pgStat;
        
        if (hasExternalPower) {
            // External power present - always safe to enable
            pWriteVariables->EN_5_10V_Val = true;
        } else if (pData->BQ24297Data.status.batPresent) {
            // On battery only - check charge level before enabling
            Power_UpdateChgPct();  // Ensure we have current battery percentage
            if (pData->chargePct >= BATT_EXT_DOWN_TH) {
                // Battery has sufficient charge
                pWriteVariables->EN_5_10V_Val = true;
            } else {
                // Battery too low - don't enable external power to avoid immediate re-transition
                pWriteVariables->EN_5_10V_Val = false;
            }
        } else {
            // No power source available
            pWriteVariables->EN_5_10V_Val = false;
        }
    } else {
        // User explicitly wants external power disabled (state 2)
        pWriteVariables->EN_5_10V_Val = false;
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

    // Set power state based on actual hardware outcome
    // If we requested external power but couldn't enable it due to low battery,
    // we should be in POWERED_UP_EXT_DOWN state to reflect reality
    if (enableExtPower && pWriteVariables->EN_5_10V_Val) {
        // External power was requested AND successfully enabled
        pData->powerState = POWERED_UP;
    } else {
        // External power was either not requested OR couldn't be enabled
        pData->powerState = POWERED_UP_EXT_DOWN;
    }
    // Don't reset requestedPowerState here - let the state machine handle it
}

void Power_Down(void) {
    
    // 5V Disable
    pWriteVariables->EN_5_10V_Val = false;
    // 12V Disable (set low to turn on, set as input (or high if configured as open collector) to turn off)
    pWriteVariables->EN_12V_Val = true;
    // Vref Disable
    pWriteVariables->EN_Vref_Val = false;
    
    // Handle 3.3V based on power source
    if (pData->BQ24297Data.status.pgStat) {
        // On USB power - keep 3.3V enabled but clear the output
        // This allows the MCU to stay on and the button to work
        pWriteVariables->EN_3_3V_Val = false;
        Power_Write();
        // Set as INPUT to allow button control
        PWR_3_3V_EN_InputEnable();
    } else {
        // On battery only - fully disable 3.3V to power off
        pWriteVariables->EN_3_3V_Val = false;
        Power_Write();
        // Set as INPUT to allow complete power down
        PWR_3_3V_EN_InputEnable();
    }

    pData->powerState = STANDBY; // Set back to default state
    pData->requestedPowerState = NO_CHANGE; // Reset the requested power state after handling request

    // Delay for power to discharge - shorter on USB power for button responsiveness
    if (pData->BQ24297Data.status.pgStat) {
        // On USB power - short delay for button responsiveness
        vTaskDelay(100 / portTICK_PERIOD_MS);
    } else {
        // On battery - longer delay for power to fully discharge
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

/*!
 * Update battery and BQ24297 status with rate limiting
 * @param updateIntervalMs Update interval in milliseconds
 * @param lastUpdateTime Pointer to last update timestamp
 * @return true if update was performed, false if rate limited
 */
static bool Power_UpdateStatusIfNeeded(uint32_t updateIntervalMs, TickType_t* lastUpdateTime) {
    TickType_t currentTime = xTaskGetTickCount();
    if ((currentTime - *lastUpdateTime) >= (updateIntervalMs / portTICK_PERIOD_MS)) {
        *lastUpdateTime = currentTime;
        Power_UpdateChgPct();
        BQ24297_UpdateStatus();
        return true;
    }
    return false;
}

/*!
 * Check if system has sufficient power to operate
 * @return true if battery > 3.0V OR external power present
 */
static bool Power_HasSufficientPower(void) {
    return (!pData->BQ24297Data.status.vsysStat || pData->BQ24297Data.status.pgStat);
}

/*!
 * Handle STANDBY state logic
 */
static void Power_HandleStandbyState(void) {
    // Handle user power-up requests first (highest priority)
    if (pData->requestedPowerState == DO_POWER_UP || 
        pData->requestedPowerState == DO_POWER_UP_EXT_DOWN) {
        
        if (Power_HasSufficientPower()) {
            // Power up with or without external power based on request
            bool enableExtPower = (pData->requestedPowerState == DO_POWER_UP);
            Power_Up(enableExtPower);
            pData->requestedPowerState = NO_CHANGE;
            return;
        } else {
            // Insufficient power for power-up request
            LOG_D("Power_UpdateState: Insufficient power - battery < 3.0V and no external power");
            pData->powerDnAllowed = false;
            pData->requestedPowerState = NO_CHANGE;
        }
    }
    
    // Check if we should power down (only once, not continuously)
    if (pData->powerDnAllowed == true) {
        Power_Down();
        pData->powerDnAllowed = false;
        
        // If on battery, exit immediately after power down
        if (!pData->BQ24297Data.status.pgStat) {
            return;
        }
    }
    
    // Update status with rate limiting
    static TickType_t lastStandbyUpdate = 0;
    // Check more frequently on USB power for button responsiveness
    uint32_t updateInterval = pData->BQ24297Data.status.pgStat ? 100 : 1000;
    if (Power_UpdateStatusIfNeeded(updateInterval, &lastStandbyUpdate)) {
        Power_Update_Settings();
    }
}

/*!
 * Handle POWERED_UP state logic
 */
static void Power_HandlePoweredUpState(void) {
    // Clear redundant request
    if (pData->requestedPowerState == DO_POWER_UP) {
        pData->requestedPowerState = NO_CHANGE;
    }
    
    // Rate limit status updates
    static TickType_t lastStatusUpdate = 0;
    if (Power_UpdateStatusIfNeeded(1000, &lastStatusUpdate)) {
        Power_Update_Settings();
    }
    
    bool hasExternalPower = pData->BQ24297Data.status.pgStat;
    
    // Handle user-requested state changes
    if (pData->requestedPowerState == DO_POWER_UP_EXT_DOWN) {
        // User explicitly wants POWERED_UP_EXT_DOWN
        pWriteVariables->EN_5_10V_Val = false;
        Power_Write();
        pData->powerState = POWERED_UP_EXT_DOWN;
        pData->requestedPowerState = NO_CHANGE;
    }
    // Automatic transition only when on battery and battery needs conservation
    else if (!hasExternalPower && pData->chargePct < BATT_EXT_DOWN_TH) {
        LOG_D("Power_UpdateState: Battery at %.1f%%, transitioning to POWERED_UP_EXT_DOWN to conserve power", 
              pData->chargePct);
        pWriteVariables->EN_5_10V_Val = false;
        Power_Write();
        pData->powerState = POWERED_UP_EXT_DOWN;
    }
}

/*!
 * Handle POWERED_UP_EXT_DOWN state logic
 */
static void Power_HandlePoweredUpExtDownState(void) {
    // Rate limit status updates
    static TickType_t lastExtDownUpdate = 0;
    Power_UpdateStatusIfNeeded(1000, &lastExtDownUpdate);
    
    bool hasExternalPower = pData->BQ24297Data.status.pgStat;
    
    // Handle user power state change requests
    if (pData->requestedPowerState == DO_POWER_UP) {
        // Get fresh battery reading before decision
        Power_UpdateChgPct();
        
        // Check if we can enable external supplies (with hysteresis)
        if (hasExternalPower || pData->chargePct >= (BATT_EXT_DOWN_TH + BATT_HYST)) {
            pWriteVariables->EN_5_10V_Val = true;
            Power_Write();
            pData->powerState = POWERED_UP;
        } else {
            LOG_D("Power_UpdateState: Cannot enable external power - battery at %.1f%%, needs %.1f%%", 
                  pData->chargePct, BATT_EXT_DOWN_TH + BATT_HYST);
        }
        pData->requestedPowerState = NO_CHANGE;
    }
    // Automatic transition to STANDBY when battery critically low
    else if (!hasExternalPower && pData->chargePct < BATT_LOW_TH) {
        LOG_D("Power_UpdateState: Battery critically low (%.1f%%), transitioning to STANDBY", 
              pData->chargePct);
        pData->powerDnAllowed = false;
        pData->powerState = STANDBY;
    }
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
    
    // Handle power down request (common for all states)
    if (pData->requestedPowerState == DO_POWER_DOWN) {
        pData->powerState = STANDBY;
        pData->requestedPowerState = NO_CHANGE;
        // Note: actual power down happens in STANDBY state handler
    }

    
    // Handle state-specific logic
    switch (pData->powerState) {
        case STANDBY:
            Power_HandleStandbyState();
            break;
            
        case POWERED_UP:
            Power_HandlePoweredUpState();
            break;
            
        case POWERED_UP_EXT_DOWN:
            Power_HandlePoweredUpExtDownState();
            break;
            
        default:
            LOG_E("Power_UpdateState: Unknown power state %d", pData->powerState);
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


