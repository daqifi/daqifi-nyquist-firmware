#include "HAL/Power/PowerApi.h"
#include "system_definitions.h"
#include "state/board/BoardConfig.h"
#include "state/data/BoardData.h"
#include "HAL/ADC.h"
#include "HAL/Wifi/WifiApi.h"

#define BATT_EXH_TH 5.0
#define BATT_LOW_TH 10.0 // 10% or ~3.2V
#define BATT_LOW_HYST 10.0 // Battery must be charged at least this value higher than BATT_LOW_TH

void Power_Init(sPowerConfig config, sPowerData *data, sPowerWriteVars vars)
{
    // NOTE: This is called before the RTOS is running.  Don't call any RTOS functions here!
    BQ24297_InitHardware(config.BQ24297Config, vars.BQ24297WriteVars, &(data->BQ24297Data));
    
    PLIB_PORTS_PinWrite(PORTS_ID_0, config.EN_3_3V_Ch, config.EN_3_3V_Bit, vars.EN_3_3V_Val);
    PLIB_PORTS_PinWrite(PORTS_ID_0, config.EN_5_10V_Ch, config.EN_5_10V_Bit, vars.EN_5_10V_Val);
    PLIB_PORTS_PinWrite(PORTS_ID_0, config.EN_5V_ADC_Ch, config.EN_5V_ADC_Bit, vars.EN_5V_ADC_Val);
    PLIB_PORTS_PinWrite(PORTS_ID_0, config.EN_12V_Ch, config.EN_12V_Bit, vars.EN_12V_Val);
    PLIB_PORTS_PinWrite(PORTS_ID_0, config.EN_Vref_Ch, config.EN_Vref_Bit, vars.EN_Vref_Val);
}

void Power_Write(sPowerConfig config, sPowerWriteVars *vars)
{    
    // Current power state values

    bool EN_3_3V_Val_Current, EN_5_10V_Val_Current, EN_5V_ADC_Val_Current, EN_12V_Val_Current, EN_Vref_Val_Current;
    
    EN_3_3V_Val_Current = PLIB_PORTS_PinGetLatched(PORTS_ID_0, config.EN_3_3V_Ch, config.EN_3_3V_Bit);
    EN_5_10V_Val_Current = PLIB_PORTS_PinGetLatched(PORTS_ID_0, config.EN_5_10V_Ch, config.EN_5_10V_Bit);
    EN_5V_ADC_Val_Current = PLIB_PORTS_PinGetLatched(PORTS_ID_0, config.EN_5V_ADC_Ch, config.EN_5V_ADC_Bit);
    EN_12V_Val_Current = PLIB_PORTS_PinGetLatched(PORTS_ID_0, config.EN_12V_Ch, config.EN_12V_Bit);
    EN_Vref_Val_Current = PLIB_PORTS_PinGetLatched(PORTS_ID_0, config.EN_Vref_Ch, config.EN_Vref_Bit);
             
    // Check to see if we are changing the state of this power pin
    if(EN_3_3V_Val_Current != vars->EN_3_3V_Val)
    {
        PLIB_PORTS_PinWrite(PORTS_ID_0, config.EN_3_3V_Ch, config.EN_3_3V_Bit, vars->EN_3_3V_Val);
    }
    
    // Check to see if we are changing the state of these power pins
    if(EN_5_10V_Val_Current != vars->EN_5_10V_Val || EN_5V_ADC_Val_Current != vars->EN_5V_ADC_Val || EN_12V_Val_Current != vars->EN_12V_Val)
    {
        PLIB_PORTS_PinWrite(PORTS_ID_0, config.EN_5_10V_Ch, config.EN_5_10V_Bit, vars->EN_5_10V_Val);

        PLIB_PORTS_PinWrite(PORTS_ID_0, config.EN_5V_ADC_Ch, config.EN_5V_ADC_Bit, vars->EN_5V_ADC_Val);

        PLIB_PORTS_PinWrite(PORTS_ID_0, config.EN_12V_Ch, config.EN_12V_Bit, vars->EN_12V_Val);
    }
    
    // Check to see if we are changing the state of this power pin
    if(EN_Vref_Val_Current != vars->EN_Vref_Val)
    {
        PLIB_PORTS_PinWrite(PORTS_ID_0, config.EN_Vref_Ch, config.EN_Vref_Bit, vars->EN_Vref_Val);
    }

}

void Power_Up(sPowerConfig config, sPowerData *data, sPowerWriteVars *vars)
{
    //uint32_t achievedFrequencyHz=0;
    
    // If the battery management is not enabled, wait for it to become ready
    while(!data->BQ24297Data.initComplete) vTaskDelay(100 / portTICK_PERIOD_MS);   
    
    // Enable processor to run at full speed
    SYS_INT_Disable();
    
    PLIB_DEVCON_SystemUnlock(0);
    SLEWCONbits.SYSDIV = 0;
    PLIB_DEVCON_SystemLock(0);

    // We still have to call this after setting SYSDIV above for some reason
    SYS_CLK_SystemFrequencySet (SYS_CLK_SOURCE_PRIMARY_SYSPLL, 200000000, true);
    
    // Below code is commented out because it causes the processor to crash for some reason.  Perhaps future updates will fix this.
    // For now, above code works to divide the overall system and PB clocks using SYSDIV.
    
//    achievedFrequencyHz = SYS_CLK_SystemFrequencySet (SYS_CLK_SOURCE_PRIMARY_SYSPLL, 200000000, true);
//
//	if ( achievedFrequencyHz != 200000000 )
//	{
//		//Clock setting failed
//        while(1);
//	}
//    
//    //Example for MZ, multiple peripheral buses
//    achievedFrequencyHz = SYS_CLK_PeripheralFrequencySet ( CLK_BUS_PERIPHERAL_1, CLK_SOURCE_PERIPHERAL_SYSTEMCLK, 100000000, true);
//    if( achievedFrequencyHz != 100000000 )
//    {
//		//Clock setting failed
//        while(1); 
//    }
//    achievedFrequencyHz = SYS_CLK_PeripheralFrequencySet ( CLK_BUS_PERIPHERAL_2, CLK_SOURCE_PERIPHERAL_SYSTEMCLK, 100000000, true);
//    if( achievedFrequencyHz != 100000000 )
//    {
//		//Clock setting failed
//        while(1);
//    }
//    achievedFrequencyHz = SYS_CLK_PeripheralFrequencySet ( CLK_BUS_PERIPHERAL_3, CLK_SOURCE_PERIPHERAL_SYSTEMCLK, 100000000, true);
//    if( achievedFrequencyHz != 100000000 )
//    {
//		//Clock setting failed
//        while(1);
//    }
//    achievedFrequencyHz = SYS_CLK_PeripheralFrequencySet ( CLK_BUS_PERIPHERAL_4, CLK_SOURCE_PERIPHERAL_SYSTEMCLK, 100000000, true);
//    if( achievedFrequencyHz != 100000000 )
//    {
//		//Clock setting failed
//        while(1);
//    }
//    achievedFrequencyHz = SYS_CLK_PeripheralFrequencySet ( CLK_BUS_PERIPHERAL_5, CLK_SOURCE_PERIPHERAL_SYSTEMCLK, 100000000, true);
//    if( achievedFrequencyHz != 100000000 )
//    {
//		//Clock setting failed
//        while(1);
//    }
//    achievedFrequencyHz = SYS_CLK_PeripheralFrequencySet ( CLK_BUS_PERIPHERAL_6, CLK_SOURCE_PERIPHERAL_SYSTEMCLK, 100000000, true);
//    if( achievedFrequencyHz != 100000000 )
//    {
//		//Clock setting failed
//        while(1);
//    }
//    achievedFrequencyHz = SYS_CLK_PeripheralFrequencySet ( CLK_BUS_PERIPHERAL_7, CLK_SOURCE_PERIPHERAL_SYSTEMCLK, 200000000, true);
//    if( achievedFrequencyHz != 200000000 )
//    {
//		//Clock setting failed
//        while(1);
//    }
//    achievedFrequencyHz = SYS_CLK_PeripheralFrequencySet ( CLK_BUS_PERIPHERAL_8, CLK_SOURCE_PERIPHERAL_SYSTEMCLK, 100000000, true);
//    if( achievedFrequencyHz != 100000000 )
//    {
//		//Clock setting failed
//        while(1);
//    }

    SYS_DEVCON_PerformanceConfig(SYS_CLK_SystemFrequencyGet());
    
    SYS_INT_Enable();
    vTaskDelay(50 / portTICK_PERIOD_MS);   //Delay after turning up to full speed to allow steady-state before powering system
    
      // 3.3V Enable
    vars->EN_3_3V_Val = true;
    Power_Write(config, vars); 

    // 5V Enable
    if((data->BQ24297Data.status.batPresent) || (data->BQ24297Data.status.vBusStat == VBUS_CHARGER)) vars->EN_5_10V_Val = true;
    Power_Write(config, vars);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    
    // 5V ADC Enable
    vars->EN_5V_ADC_Val = true;
    Power_Write(config, vars); 
    vTaskDelay(50 / portTICK_PERIOD_MS);
    
    // 12V Enable (set low to turn on, set as input (or high if configured as open collector) to turn off)
    vars->EN_12V_Val = false;
    Power_Write(config, vars);
    vTaskDelay(50 / portTICK_PERIOD_MS);
    
    // Vref Enable
    vars->EN_Vref_Val = true;
    Power_Write(config, vars);  
    vTaskDelay(50 / portTICK_PERIOD_MS);
    
    data->powerState = POWERED_UP;

}

void Power_Down(sPowerConfig config, sPowerData *data, sPowerWriteVars *vars)
{
    // Turn off WiFi interface to save power
    WifiConnectionDown();
        
    // Divide system frequency by value below to save power
    PLIB_DEVCON_SystemUnlock(0);
    SLEWCONbits.SYSDIV = SYS_CLK_DIV_PWR_SAVE;
    PLIB_DEVCON_SystemLock(0);
    
    // We still have to call SYS_CLK_SystemFrequencySet after setting SYSDIV above for some reason
    // and the frequency must be 200MHz to avoid crashing (even though we are actually
    // dividing the full speed (200MHz by a divider, SYS_CLK_DIV_PWR_SAVE)
    SYS_CLK_SystemFrequencySet (SYS_CLK_SOURCE_PRIMARY_SYSPLL, 200000000, true);
    
    
    //SYS_CLK_SystemClockStatus
    
    // 3.3V Disable - if powered externally, board will stay on and go to low power state, else off completely
    vars->EN_3_3V_Val = false;
    // 5V Disable
    vars->EN_5_10V_Val = false;
    // 5V ADC Disable
    vars->EN_5V_ADC_Val = false;
    // 12V Disable (set low to turn on, set as input (or high if configured as open collector) to turn off)
    vars->EN_12V_Val = true;
    // Vref Disable
    vars->EN_Vref_Val = false;
    Power_Write(config, vars);

    
    data->powerState = MICRO_ON;    // Set back to default state
    data->requestedPowerState = NO_CHANGE;
    
    // Delay 1000ms for power to discharge
    vTaskDelay(1000 / portTICK_PERIOD_MS);
}


void Power_UpdateState(sPowerConfig config, sPowerData *data, sPowerWriteVars *vars)
{
    // Set power state immediately if POWER_DOWN is requested
    if(data->requestedPowerState == DO_POWER_DOWN) data->powerState = POWER_DOWN;
    
    switch(data->powerState){
        case POWER_DOWN:
            /* Not initialized or powered down */
            
            // Check to see if we've finished signaling the user of insufficient power if necessary
            if(data->powerDnAllowed == true) Power_Down(config, data, vars);
            break;
            
        case MICRO_ON:
        /* 3.3V rail enabled. Ready to check initial status 
         * NOTE: This is the default state if code is running!
         * There is no Vref at this time, so any read to ADC is invalid!
         */
            if(data->requestedPowerState == DO_POWER_UP)
            {
                // Code below is commented out when I2C is disabled
                if(!data->BQ24297Data.status.vsysStat || data->BQ24297Data.status.pgStat)    // If batt voltage is greater than VSYSMIN or power is good, we can power up
                {
                    Power_Up(config, data, vars);
                }
                // Code below is commented out when I2C is disabled
                else
                {
                    // Otherwise insufficient power.  Notify user and power down
                    data->powerDnAllowed = false; // This will turn true after the LED sequence completes
                    data->powerState = POWER_DOWN;
                }
            }
            break;
            
        case POWERED_UP:
        /* Board fully powered. Monitor for any changes/faults
         * ADC readings are now valid!
         */ 
            Power_UpdateChgPct(data);
            if(data->requestedPowerState == DO_POWER_UP_EXT_DOWN || (data->chargePct<BATT_LOW_TH && !data->BQ24297Data.status.pgStat))
            {
                // If battery is low on charge and we are not plugged in, disable external supplies
                vars->EN_5_10V_Val = false;
                Power_Write(config, vars);
                data->powerState = POWERED_UP_EXT_DOWN;
            }
            break;
            
        case POWERED_UP_EXT_DOWN:
        /* Board partially powered. Monitor for any changes */ 
            Power_UpdateChgPct(data);
            if(data->chargePct>(BATT_LOW_TH + BATT_LOW_HYST) || (data->BQ24297Data.status.inLim > 1))
            {
                if(data->requestedPowerState == DO_POWER_UP)
                {
                    // If battery is charged or we are plugged in, enable external supplies
                    vars->EN_5_10V_Val = true;
                    Power_Write(config, vars);
                    data->powerState = POWERED_UP;
                }
                    // Else, remain here because the user didn't want to be fully powered
            }else if(data->chargePct<BATT_EXH_TH)
            {
                // Code below is commented out when I2C is disabled
                // Insufficient power.  Notify user and power down.
                data->powerDnAllowed = false;   // This will turn true after the LED sequence completes
                data->powerState = POWER_DOWN;
            }

            break;
    }
 
}

void Power_UpdateChgPct(sPowerData *data)
{
    size_t index = ADC_FindChannelIndex( ADC_CHANNEL_VBATT );
    // TODO: Add data validation without blocking
//    while(!ADC_IsDataValid(&g_BoardData.AInLatest.Data[index]))
//    {
//        vTaskDelay(100 / portTICK_PERIOD_MS);
//    }
    data->battVoltage = ADC_ConvertToVoltage(&g_BoardData.AInLatest.Data[index]);

    // Function below is defined from 3.17-3.868.  Must coerce input value to within these bounds.
    if(data->battVoltage<3.17)
    {
        data->chargePct=0;
    }else if(data->battVoltage>3.868)
    {
        data->chargePct=100;
    }else{
        data->chargePct = 142.92*data->battVoltage - 452.93;
    }
}

void Power_Tasks(sPowerConfig PowerConfig, sPowerData *PowerData, sPowerWriteVars *powerWriteVars)
{
    // If we haven't initialized the battery management settings, do so now
    if (PowerData->BQ24297Data.initComplete == false)
    {
        BQ24297_InitSettings(PowerConfig.BQ24297Config, powerWriteVars->BQ24297WriteVars, &(PowerData->BQ24297Data));
    }

    // Update power settings based on BQ24297 interrupt change
    if (PowerData->BQ24297Data.intFlag)
    {
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
        BQ24297_UpdateStatus(PowerConfig.BQ24297Config, powerWriteVars->BQ24297WriteVars, &(PowerData->BQ24297Data));
        Power_Update_Settings(PowerConfig, PowerData, powerWriteVars);
        PowerData->BQ24297Data.intFlag = false; // Clear flag
    }
    
    // Call update state
    // TODO - perhaps put this in its own task to call only once per minute?
    // As is, it appears to cause LED blinking to be errant
    Power_UpdateState(PowerConfig, PowerData, powerWriteVars);
}

void Power_Update_Settings(sPowerConfig config, sPowerData *data, sPowerWriteVars *vars)
{
    // Change charging/other power settings based on current status
       
    // Check new power source and set parameters accordingly
    BQ24297_AutoSetILim(config.BQ24297Config, &vars->BQ24297WriteVars, &data->BQ24297Data);
    
    // Enable/disable charging
    BQ24297_ChargeEnable(config.BQ24297Config, &vars->BQ24297WriteVars, &data->BQ24297Data, data->BQ24297Data.status.batPresent);
}

void Power_USB_Sleep_Update(sPowerConfig config, sPowerData *data, bool sleep)
{
    data->USBSleep = sleep;
}