/*! @file PowerApi.c
 *
 * This file implements the functions to manage power API
 */
#define LOG_LVL LOG_LEVEL_POWER

#include "HAL/Power/PowerApi.h"
#include "definitions.h"
#include "state/board/BoardConfig.h"
#include "state/data/BoardData.h"
#include "HAL/ADC.h"
#include "HAL/BQ24297/BQ24297.h"
#include "Util/Logger.h"
#include "../../services/wifi_services/wifi_manager.h"
#include "../../services/UsbCdc/UsbCdc.h"
#include "driver/usb/usbhs/src/plib_usbhs_header.h"
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
/* Battery Management Thresholds
 * - BATT_EXT_DOWN_TH (15%): Below this, external power rails disabled to conserve battery
 * - BATT_LOW_TH (5%): Critical level - device must shut down
 * - BATT_HYST (10%): Hysteresis prevents oscillation at threshold boundaries
 */
#define BATT_EXT_DOWN_TH 15.0  /* External power disabled below this */
#define BATT_LOW_TH 5.0        /* Critical shutdown threshold */
#define BATT_HYST 10.0         /* Must charge 10% above threshold to re-enable */ 

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
 * Function to update power status from GPIO and ADC (no I2C)
 */
static void Power_UpdateStatusFromGPIO(void);
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
    
    // Initialize auto external power control (enabled by default)
    pData->autoExtPowerEnabled = true;
    
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
    /* Initialize battery management if needed */
    if (pData->BQ24297Data.initComplete == false) {
        BQ24297_Config_Settings();
    }

    /* Update power state at 1 second intervals */
    static TickType_t lastUpdateTime = 0;
    TickType_t currentTime = xTaskGetTickCount();
    if ((currentTime - lastUpdateTime) >= pdMS_TO_TICKS(1000)) {
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
        
        // CRITICAL: Only drive HIGH or set as INPUT (HiZ) - never drive LOW
        // There's a pulldown resistor that will drop the line when in HiZ
        if (pWriteVariables->EN_3_3V_Val) {
            // Set as OUTPUT and drive high to enable 3.3V
            PWR_3_3V_EN_OutputEnable();
            PWR_3_3V_EN_Set();
        } else {
            // Set as INPUT (HiZ) to disable - pulldown will handle the rest
            // Never actively drive low to avoid conflicts
            PWR_3_3V_EN_InputEnable();
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
    
    /* Wait for battery management initialization with timeout
     * Prevents indefinite blocking if BQ24297 fails to initialize
     * Timeout: 5 seconds (50 x 100ms)
     */
    int initTimeout = 50;
    while (!pData->BQ24297Data.initComplete && initTimeout > 0) {
        vTaskDelay(pdMS_TO_TICKS(100));
        initTimeout--;
    }
    
    if (initTimeout == 0) {
        LOG_E("Power_Up: BQ24297 initialization timeout - proceeding anyway");
    }

    /* Power sequencing delay - allows voltage regulators to stabilize */
    vTaskDelay(pdMS_TO_TICKS(50));

    /* Enable 3.3V rail (critical - MCU power) */
    pWriteVariables->EN_3_3V_Val = true;
    Power_Write();
    
    /* 5V/10V External Power Rail Decision
     * Enable when:
     *   - User requested (enableExtPower=true) AND
     *   - Either external power present OR battery sufficient
     * Keep disabled when:
     *   - User wants conservation mode (enableExtPower=false) OR
     *   - Battery too low for safe operation
     */
    if (enableExtPower) {
        /* User wants external power - check if we should enable it
         * Use GPIO/ADC to get current status (no I2C) */
        Power_UpdateStatusFromGPIO();
        bool hasExternalPower = pData->BQ24297Data.status.pgStat;

        if (hasExternalPower) {
            /* External power present - always safe to enable */
            pWriteVariables->EN_5_10V_Val = true;
        } else if (pData->chargePct >= BATT_EXT_DOWN_TH) {
            /* Battery has sufficient charge */
            pWriteVariables->EN_5_10V_Val = true;
        } else {
            /* Battery too low - don't enable external power to avoid immediate re-transition */
            pWriteVariables->EN_5_10V_Val = false;
        }
    } else {
        /* User explicitly wants external power disabled (state 2) */
        pWriteVariables->EN_5_10V_Val = false;
    }
    Power_Write();
    vTaskDelay(pdMS_TO_TICKS(50));

    // 12V Enable (set low to turn on, set as input (or high if configured
    // as open collector) to turn off)
    pWriteVariables->EN_12V_Val = false;
    Power_Write();
    vTaskDelay(pdMS_TO_TICKS(50));

    // Vref Enable
    pWriteVariables->EN_Vref_Val = true;
    Power_Write();
    vTaskDelay(pdMS_TO_TICKS(50));

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
    
    /* Disable external power rails */
    pWriteVariables->EN_5_10V_Val = false;   /* 5V/10V off */
    pWriteVariables->EN_12V_Val = true;      /* 12V off (inverted logic) */
    pWriteVariables->EN_Vref_Val = false;    /* ADC reference off */
    
    /* 3.3V rail handling - special case
     * Setting to false triggers HiZ mode in Power_Write()
     * This allows pulldown resistor to control the rail
     * Also enables button-based power control
     */
    pWriteVariables->EN_3_3V_Val = false;
    Power_Write();  /* Executes HiZ transition */

    pData->powerState = STANDBY;
    pData->requestedPowerState = NO_CHANGE;
}

/*
 * Update power status from GPIO and ADC (no I2C)
 *
 * Replaces I2C polling with:
 *   - USB VBUS detection for external power (pgStat equivalent)
 *   - ADC battery voltage for vsysStat equivalent
 *   - BATT_MAN_STAT GPIO for charging status
 *
 * This eliminates I2C bus hang risk during normal operation.
 * The previous I2C polling (~70 reads/sec on USB power) caused intermittent
 * hangs in PowerAndUITask, leading to LED unresponsiveness (issue #172).
 *
 * Future: Issue #173 tracks adding interrupt-driven I2C for scenarios that
 * require BQ24297 register access (config changes, detailed diagnostics).
 */
static void Power_UpdateStatusFromGPIO(void) {
    /* Update battery voltage and charge percentage from ADC */
    Power_UpdateChgPct();

    /* Update external power status from hardware VBUS detection
     * Read directly from USB hardware register - more reliable than software events
     * which may not trigger correctly for wall chargers without USB data lines.
     *
     * Use USBHS_VBUS_BELOW_VBUSVALID threshold (~4.0V) instead of USBHS_VBUS_VALID
     * (~4.75V) to handle wall chargers with minor voltage sag. This aligns better
     * with BQ24297's pgStat threshold (3.88V) for external power detection. */
    USBHS_VBUS_LEVEL vbusLevel = PLIB_USBHS_VBUSLevelGet(USBHS_ID_0);
    bool hasExternalPower = (vbusLevel >= USBHS_VBUS_BELOW_VBUSVALID);
    pData->BQ24297Data.status.pgStat = hasExternalPower;

    /* Update vsysStat equivalent from ADC battery voltage
     * vsysStat=1 means battery < 3.0V (VSYSMIN threshold) */
    pData->BQ24297Data.status.vsysStat = (pData->battVoltage < 3.0f);

    /* Read charging status from BATT_MAN_STAT GPIO
     * BQ24297 STAT pin: LOW = charging, HIGH = not charging/complete
     * Map to chgStat: 0=not charging, 1=pre-charge, 2=fast charge, 3=done */
    bool isCharging = !BATT_MAN_STAT_Get();  /* Active low */
    if (isCharging) {
        pData->BQ24297Data.status.chgStat = 2;  /* Fast charging */
    } else if (hasExternalPower && pData->chargePct >= 95.0f) {
        pData->BQ24297Data.status.chgStat = 3;  /* Charge complete */
    } else {
        pData->BQ24297Data.status.chgStat = 0;  /* Not charging */
    }

    /* Update external power source type
     * Without I2C, we can't distinguish USB types, so assume USB 500mA when VBUS present */
    if (hasExternalPower) {
        pData->externalPowerSource = USB_500MA_EXT_POWER;
    } else {
        pData->externalPowerSource = NO_EXT_POWER;
    }
}

/*
 * Rate-limited status update helper
 * Uses GPIO and ADC instead of I2C to avoid bus hang risk
 *
 * @param updateIntervalMs Minimum time between updates (ms)
 * @param lastUpdateTime Pointer to timestamp of last update
 * @return true if update performed, false if rate-limited
 */
static bool Power_UpdateStatusIfNeeded(uint32_t updateIntervalMs, TickType_t* lastUpdateTime) {
    TickType_t currentTime = xTaskGetTickCount();
    if ((currentTime - *lastUpdateTime) >= pdMS_TO_TICKS(updateIntervalMs)) {
        *lastUpdateTime = currentTime;
        Power_UpdateStatusFromGPIO();
        return true;
    }
    return false;
}

/*
 * Check if system has sufficient power to operate
 * Returns true if:
 *   - Battery voltage > 3.0V (vsysStat = 0) OR
 *   - External power present (pgStat = 1)
 */
static bool Power_HasSufficientPower(void) {
    return (!pData->BQ24297Data.status.vsysStat || pData->BQ24297Data.status.pgStat);
}

/*
 * STANDBY State Handler
 * 
 * Responsibilities:
 *   - Process power-up requests from user
 *   - Execute power-down sequence when needed
 *   - Monitor battery and external power
 *   - Update status (faster on USB for button response)
 */
static void Power_HandleStandbyState(void) {
    /* Priority 1: Handle user power-up requests */
    if (pData->requestedPowerState == DO_POWER_UP ||
        pData->requestedPowerState == DO_POWER_UP_EXT_DOWN) {

        /* Refresh power status from GPIO/ADC before power-up decision */
        Power_UpdateStatusFromGPIO();

        if (Power_HasSufficientPower()) {
            // Power up with or without external power based on request
            bool enableExtPower = (pData->requestedPowerState == DO_POWER_UP);
            Power_Up(enableExtPower);
            pData->requestedPowerState = NO_CHANGE;
            return;
        } else {
            // Insufficient power for power-up request
            LOG_D("Power_UpdateState: Insufficient power - battery < 3.0V and no external power");
            pData->shutdownNotified = false;
            pData->requestedPowerState = NO_CHANGE;
        }
    }
    
    /* Execute power-down sequence (one-shot flag)
     * shutdownNotified is set by UI task after LED warning sequence completes
     * This ensures user is notified before shutdown */
    if (pData->shutdownNotified == true) {
        Power_Down();
        pData->shutdownNotified = false;  /* Clear flag to prevent repeated calls */
        
        /* Exit early on battery to conserve power */
        if (!pData->BQ24297Data.status.pgStat) {
            return;
        }
    }
    
    /* Status update with adaptive rate
     * - 100ms on USB: Better button/command responsiveness
     * - 1000ms on battery: Conserve power
     */
    static TickType_t lastStandbyUpdate = 0;
    uint32_t updateInterval = pData->BQ24297Data.status.pgStat ? 100 : 1000;
    if (Power_UpdateStatusIfNeeded(updateInterval, &lastStandbyUpdate)) {
        Power_Update_Settings();
    }
}

/*
 * POWERED_UP State Handler
 * 
 * All power rails enabled, monitoring for:
 *   - Battery drops below conservation threshold (15%)
 *   - User requests for state changes
 *   - Regular status updates
 */
static void Power_HandlePoweredUpState(void) {
    /* Clear redundant request (already in this state) */
    if (pData->requestedPowerState == DO_POWER_UP) {
        pData->requestedPowerState = NO_CHANGE;
    }
    
    // Rate limit status updates
    static TickType_t lastStatusUpdate = 0;
    if (Power_UpdateStatusIfNeeded(1000, &lastStatusUpdate)) {
        Power_Update_Settings();
    }
    
    bool hasExternalPower = pData->BQ24297Data.status.pgStat;
    
    /* User-requested transition to conservation mode */
    if (pData->requestedPowerState == DO_POWER_UP_EXT_DOWN) {
        pWriteVariables->EN_5_10V_Val = false;
        Power_Write();
        pData->powerState = POWERED_UP_EXT_DOWN;
        pData->requestedPowerState = NO_CHANGE;
    }
    /* Automatic transition when battery needs conservation */
    else if (!hasExternalPower) {
        /* Refresh power status from GPIO/ADC before threshold decision */
        Power_UpdateStatusFromGPIO();

        if (pData->chargePct < BATT_EXT_DOWN_TH) {
            LOG_D("Power_UpdateState: Battery at %.1f%%, transitioning to POWERED_UP_EXT_DOWN to conserve power", 
                  pData->chargePct);
            pWriteVariables->EN_5_10V_Val = false;
            Power_Write();
            pData->powerState = POWERED_UP_EXT_DOWN;
        }
    }
}

/*
 * POWERED_UP_EXT_DOWN State Handler (Conservation Mode)
 * 
 * Core system powered, external rails disabled to save battery
 * Monitoring for:
 *   - Battery recovery (charge > 25% with hysteresis)
 *   - External power connection
 *   - Critical battery level (<5%)
 */
static void Power_HandlePoweredUpExtDownState(void) {
    /* Rate limit status updates - uses GPIO/ADC, no I2C */
    static TickType_t lastExtDownUpdate = 0;
    Power_UpdateStatusIfNeeded(1000, &lastExtDownUpdate);

    bool hasExternalPower = pData->BQ24297Data.status.pgStat;

    /* Check for automatic recovery if auto external power control is enabled */
    if (pData->autoExtPowerEnabled) {
        /* Check recovery conditions (with hysteresis to prevent oscillation) */
        if (hasExternalPower || pData->chargePct >= (BATT_EXT_DOWN_TH + BATT_HYST)) {
            /* Auto-recovery - re-enable external power */
            LOG_D("Auto-recovery: Enabling external power - %s, battery at %.1f%%",
                  hasExternalPower ? "external power present" : "battery recovered", pData->chargePct);
            pWriteVariables->EN_5_10V_Val = true;
            Power_Write();
            pData->powerState = POWERED_UP;
            return;  /* Exit early after state change */
        }
    }

    /* Handle user power state change requests */
    if (pData->requestedPowerState == DO_POWER_UP) {
        /* Refresh power status from GPIO/ADC before decision */
        Power_UpdateStatusFromGPIO();
        hasExternalPower = pData->BQ24297Data.status.pgStat;

        /* Check recovery conditions (with hysteresis to prevent oscillation) */
        if (hasExternalPower || pData->chargePct >= (BATT_EXT_DOWN_TH + BATT_HYST)) {
            /* Manual recovery - re-enable external power */
            LOG_D("Manual recovery: Enabling external power - %s, battery at %.1f%%",
                  hasExternalPower ? "external power present" : "battery sufficient", pData->chargePct);
            pWriteVariables->EN_5_10V_Val = true;
            Power_Write();
            pData->powerState = POWERED_UP;
        } else {
            /* Cannot enable external power due to low battery */
            LOG_D("Power_HandlePoweredUpExtDownState: Cannot enable external power - battery at %.1f%%, needs %.1f%%",
                  pData->chargePct, BATT_EXT_DOWN_TH + BATT_HYST);
        }
        pData->requestedPowerState = NO_CHANGE;
    }
    /* Critical battery check - must shut down to prevent damage */
    else if (!hasExternalPower && pData->chargePct < BATT_LOW_TH) {
        LOG_D("Power_UpdateState: Battery critically low (%.1f%%), transitioning to STANDBY",
              pData->chargePct);

        /* Explicitly disable all external rails before shutdown */
        pWriteVariables->EN_5_10V_Val = false;
        pWriteVariables->EN_12V_Val = true;     /* Inverted logic - true = off */
        pWriteVariables->EN_Vref_Val = false;
        Power_Write();

        pData->shutdownNotified = false;
        pData->powerState = STANDBY;
    }
}

/*
 * Main Power State Machine
 * 
 * State transitions:
 *   STANDBY -> POWERED_UP: User request with sufficient power
 *   POWERED_UP -> POWERED_UP_EXT_DOWN: Battery < 15% or user request
 *   POWERED_UP_EXT_DOWN -> POWERED_UP: Battery > 25% or external power
 *   POWERED_UP_EXT_DOWN -> STANDBY: Battery < 5% (critical)
 *   Any state -> STANDBY: User power-down request
 */
static void Power_UpdateState(void) {
    static POWER_STATE lastLoggedState = -1;
    
    /* Monitor 3.3V rail state (debug/diagnostic) */
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
    
    /* Common handler: Power-down request (works in any state) */
    if (pData->requestedPowerState == DO_POWER_DOWN) {
        pData->powerState = STANDBY;
        pData->requestedPowerState = NO_CHANGE;
        /* Note: Actual power-down executed by STANDBY handler */
    }
    
    /* Delegate to state-specific handlers */
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
    /* Read battery voltage from ADC */
    size_t index = ADC_FindChannelIndex(ADC_CHANNEL_VBATT);
    const AInSample *pAnalogSample = BoardData_Get(
            BOARDDATA_AIN_LATEST,
            index);
    if (NULL != pAnalogSample) {
        float newVoltage = ADC_ConvertToVoltage(pAnalogSample);
        /* Validate reading (ignore noise near 0V) */
        if (newVoltage > 0.1) {
            pData->battVoltage = newVoltage;
        }
    }

    /* Convert voltage to percentage using linear approximation
     * Valid range: 3.17V (0%) to 3.868V (100%)
     * Formula: percentage = 142.92 * voltage - 452.93
     */
    if (pData->battVoltage < 3.17) {
        pData->chargePct = 0;
    } else if (pData->battVoltage > 3.868) {
        pData->chargePct = 100;
    } else {
        pData->chargePct = 142.92 * pData->battVoltage - 452.93;
    }
}

static void Power_Update_Settings(void) {
    /* Update power settings based on current status
     *
     * NOTE: External power source is now updated in Power_UpdateStatusFromGPIO()
     * I2C-based BQ24297 calls (AutoSetILim, SetPowerMode) are only done during
     * initial configuration in BQ24297_Config_Settings() to avoid I2C bus hangs.
     *
     * Charging is configured once at startup. The BQ24297 handles charging
     * autonomously based on its hardware configuration.
     */

    /* Nothing to do here anymore - status updates happen in Power_UpdateStatusFromGPIO()
     * and BQ24297 configuration happens once at init */
}


