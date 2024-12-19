/*! @file PowerApi.c 
 * 
 * This file implements the functions to manage power API
 */

#include "HAL/Power/PowerApi.h"
#include "definitions.h"
#include "state/board/BoardConfig.h"
#include "state/data/BoardData.h"
#include "HAL/ADC.h"
#include "../../services/wifi_services/wifi_manager.h"
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

static void WriteGpioPin(GPIO_PORT port, uint32_t mask, uint32_t value) {
    uint32_t pin = 1 << mask;
    if (value == 1)
        GPIO_PortSet(port, pin);
    else
        GPIO_PortClear(port, pin);

}

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

    WriteGpioPin(pConfig->EN_3_3V_Ch,
            pConfig->EN_3_3V_Bit,
            pWriteVariables->EN_3_3V_Val);

    WriteGpioPin(pConfig->EN_5_10V_Ch,
            pConfig->EN_5_10V_Bit,
            pWriteVariables->EN_5_10V_Val);
    WriteGpioPin(pConfig->EN_5V_ADC_Ch,
            pConfig->EN_5V_ADC_Bit,
            pWriteVariables->EN_5V_ADC_Val);
    WriteGpioPin(pConfig->EN_12V_Ch,
            pConfig->EN_12V_Bit,
            pWriteVariables->EN_12V_Val);
    WriteGpioPin(pConfig->EN_Vref_Ch,
            pConfig->EN_Vref_Bit,
            pWriteVariables->EN_Vref_Val);
}

void Power_Tasks(void) {
    // If we haven't initialized the battery management settings, do so now
    if (pData->BQ24297Data.initComplete == false) {
        BQ24297_Config_Settings();
    }

    // Update power settings based on BQ24297 interrupt change
    if (pData->BQ24297Data.intFlag) {
        /* The BQ24297 asserted an interrupt so we might need to update our power settings
         * INT on the BQ24297 can be asserted:
         * -Good input source detected
         * -Input removed or VBUS above VACOV threshold
         * -After successful input source qualification
         * -Any fault during boost operation, including VBUS over-voltage or over-current
         * -Once a charging cycle is complete/charging termination
         * -On temperature fault
         * -Safety timer timeout
         */
        vTaskDelay(100 / portTICK_PERIOD_MS);
        // Update battery management status - plugged in (USB, charger, etc), charging/discharging, etc.
        BQ24297_UpdateStatus();
        Power_Update_Settings();
        pData->BQ24297Data.intFlag = false; // Clear flag
    }

    // Call update state
    // TODO - perhaps put this in its own task to call only once per minute?
    // As is, it appears to cause LED blinking to be errant
    Power_UpdateState();
}

void Power_USB_Sleep_Update(bool sleep) {
    pData->USBSleep = sleep;
}

void Power_Write(void) {
    // Current power state values

    bool EN_3_3V_Val_Current;
    bool EN_5_10V_Val_Current;
    bool EN_5V_ADC_Val_Current;
    bool EN_12V_Val_Current;
    bool EN_Vref_Val_Current;

    EN_3_3V_Val_Current = ReadGpioPinStateLatched(pConfig->EN_3_3V_Ch, pConfig->EN_3_3V_Bit);
    EN_5_10V_Val_Current = ReadGpioPinStateLatched(pConfig->EN_5_10V_Ch, pConfig->EN_5_10V_Bit);
    EN_5V_ADC_Val_Current = ReadGpioPinStateLatched(pConfig->EN_5V_ADC_Ch, pConfig->EN_5V_ADC_Bit);
    EN_12V_Val_Current = ReadGpioPinStateLatched(pConfig->EN_12V_Ch, pConfig->EN_12V_Bit);
    EN_Vref_Val_Current = ReadGpioPinStateLatched(pConfig->EN_Vref_Ch, pConfig->EN_Vref_Bit);

    // Check to see if we are changing the state of this power pin
    if (EN_3_3V_Val_Current != pWriteVariables->EN_3_3V_Val) {
        WriteGpioPin(pConfig->EN_3_3V_Ch,
                pConfig->EN_3_3V_Bit,
                pWriteVariables->EN_3_3V_Val);
    }

    // Check to see if we are changing the state of these power pins
    if (EN_5_10V_Val_Current != pWriteVariables->EN_5_10V_Val ||
            EN_5V_ADC_Val_Current != pWriteVariables->EN_5V_ADC_Val ||
            EN_12V_Val_Current != pWriteVariables->EN_12V_Val) {
        WriteGpioPin(pConfig->EN_5_10V_Ch,
                pConfig->EN_5_10V_Bit,
                pWriteVariables->EN_5_10V_Val);

        WriteGpioPin(pConfig->EN_5V_ADC_Ch,
                pConfig->EN_5V_ADC_Bit,
                pWriteVariables->EN_5V_ADC_Val);

        WriteGpioPin(pConfig->EN_12V_Ch,
                pConfig->EN_12V_Bit,
                pWriteVariables->EN_12V_Val);
    }

    // Check to see if we are changing the state of this power pin
    if (EN_Vref_Val_Current != pWriteVariables->EN_Vref_Val) {
        WriteGpioPin(pConfig->EN_Vref_Ch,
                pConfig->EN_Vref_Bit,
                pWriteVariables->EN_Vref_Val);
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

    // 5V ADC Enable
    pWriteVariables->EN_5V_ADC_Val = true;
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
    // 5V ADC Disable
    pWriteVariables->EN_5V_ADC_Val = false;
    // 12V Disable (set low to turn on, set as input (or high if configured as open collector) to turn off)
    pWriteVariables->EN_12V_Val = true;
    // Vref Disable
    pWriteVariables->EN_Vref_Val = false;
    Power_Write();


    pData->powerState = MICRO_ON; // Set back to default state
    pData->requestedPowerState = NO_CHANGE; // Reset the requested power state after handling request

    // Delay 1000ms for power to discharge
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}

static void Power_UpdateState(void) {
    // Set power state immediately if DO_POWER_DOWN is requested
    if (pData->requestedPowerState == DO_POWER_DOWN) {
        pData->powerState = POWERED_DOWN;
    }

    switch (pData->powerState) {
        case POWERED_DOWN:
            /* Not initialized or powered down */

            // Check to see if we've finished signaling the user of insufficient power if necessary
            if (pData->powerDnAllowed == true) {
                Power_Down();
            }
            break;

        case MICRO_ON:
            /* 3.3V rail enabled. Ready to check initial status 
             * NOTE: This is the default state if code is running!
             * There is no Vref at this time, so any read to ADC is invalid!
             */
            if (pData->requestedPowerState == DO_POWER_UP) {

                if (!pData->BQ24297Data.status.vsysStat ||
                        pData->BQ24297Data.status.pgStat) // If batt voltage is greater than VSYSMIN or power is good, we can power up
                {
                    Power_Up();
                } else {
                    // Otherwise insufficient power.  Notify user and power down
                    pData->powerDnAllowed = false;  // This will turn true after the LED sequence completes
                    pData->powerState = POWERED_DOWN;
                }
                pData->requestedPowerState = NO_CHANGE;    // Reset the requested power state after handling request
            }
            break;

        case POWERED_UP:
            /* Board fully powered. Monitor for any changes/faults
             * ADC readings are now valid!
             */
            if (pData->requestedPowerState == DO_POWER_UP) pData->requestedPowerState = NO_CHANGE; // We are already powered so just reset the flag
            Power_UpdateChgPct();
            if (pData->requestedPowerState == DO_POWER_UP_EXT_DOWN ||
                    (pData->chargePct < BATT_LOW_TH &&
                    !pData->BQ24297Data.status.pgStat)) {
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
            } else if (pData->chargePct < BATT_EXH_TH) {
                // Code below is commented out when I2C is disabled
                // Insufficient power.  Notify user and power down.
                pData->powerDnAllowed = false; // This will turn true after the LED sequence completes
                pData->powerState = POWERED_DOWN;
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
        pData->battVoltage = ADC_ConvertToVoltage(pAnalogSample);
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

    // Check new power source and set parameters accordingly
    BQ24297_AutoSetILim();

    // Enable/disable charging
    BQ24297_ChargeEnable(pData->BQ24297Data.status.batPresent);
}


