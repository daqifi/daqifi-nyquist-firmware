/*! @file PowerApi.c 
 * 
 * This file implements the functions to manage power API
 */

#include "HAL/Power/PowerApi.h"
#include "system_definitions.h"
#include "state/board/BoardConfig.h"
#include "state/data/BoardData.h"
#include "HAL/ADC.h"
#include "HAL/Wifi/WifiApi.h"

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

/*! 
 * Funtion to write in power channel
 */
static void Power_Write( void );
/*!
 * Function to active power capabilities
 */
static void Power_Up( void );
/*!
 * Function to turn off power capabilities
 */
static void Power_Down( void );
/*!
 * Function to upate power state
 */
static void Power_UpdateState( void );
/*!
 * Function to update charge percentage
 */
static void Power_UpdateChgPct( void );
/*!
 * Function to update the configuration
 */
static void Power_Update_Settings( void );

void Power_Init(                                                            \
                        tPowerConfig *pInitConfig,                          \
                        tPowerData *pInitData,                              \
                        tPowerWriteVars *pInitVars)
{
    pConfig = pInitConfig;
    pData = pInitData;
    pWriteVariables = pInitVars;
    
    // NOTE: This is called before the RTOS is running.  
    // Don't call any RTOS functions here!
    BQ24297_InitHardware(                                                   \
                        &pConfig->BQ24297Config,                            \
                        &pWriteVariables->BQ24297WriteVars,                 \
                        &(pData->BQ24297Data));
       
    PLIB_PORTS_PinWrite(PORTS_ID_0,                                         \
                        pConfig->EN_3_3V_Ch,                                \
                        pConfig->EN_3_3V_Bit,                               \
                        pWriteVariables->EN_3_3V_Val);
    PLIB_PORTS_PinWrite(PORTS_ID_0,                                         \
                        pConfig->EN_5_10V_Ch,                               \
                        pConfig->EN_5_10V_Bit,                              \
                        pWriteVariables->EN_5_10V_Val);
    PLIB_PORTS_PinWrite(PORTS_ID_0,                                         \
                        pConfig->EN_5V_ADC_Ch,                              \
                        pConfig->EN_5V_ADC_Bit,                             \
                        pWriteVariables->EN_5V_ADC_Val);
    PLIB_PORTS_PinWrite(PORTS_ID_0,                                         \
                        pConfig->EN_12V_Ch,                                 \
                        pConfig->EN_12V_Bit,                                \
                        pWriteVariables->EN_12V_Val);
    PLIB_PORTS_PinWrite(PORTS_ID_0,                                         \
                        pConfig->EN_Vref_Ch,                                \
                        pConfig->EN_Vref_Bit,                               \
                        pWriteVariables->EN_Vref_Val);
}

void Power_Tasks( void )
{
    // If we haven't initialized the battery management settings, do so now
    if (pData->BQ24297Data.initComplete == false)
    {
        BQ24297_Config_Settings();
    }

    // Update power settings based on BQ24297 interrupt change
    if (pData->BQ24297Data.intFlag)
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
        BQ24297_UpdateStatus();
        Power_Update_Settings();
        pData->BQ24297Data.intFlag = false; // Clear flag
    }
    
    // Call update state
    // TODO - perhaps put this in its own task to call only once per minute?
    // As is, it appears to cause LED blinking to be errant
    Power_UpdateState();
}

void Power_USB_Sleep_Update( bool sleep )
{
    pData->USBSleep = sleep;
}

static void Power_Write( void )
{    
    // Current power state values

    bool EN_3_3V_Val_Current;
    bool EN_5_10V_Val_Current;
    bool EN_5V_ADC_Val_Current;
    bool EN_12V_Val_Current;
    bool EN_Vref_Val_Current;
    
    EN_3_3V_Val_Current = PLIB_PORTS_PinGetLatched( \
                                        PORTS_ID_0, \
                                        pConfig->EN_3_3V_Ch, \
                                        pConfig->EN_3_3V_Bit );
    EN_5_10V_Val_Current = PLIB_PORTS_PinGetLatched( \
                                        PORTS_ID_0, \
                                        pConfig->EN_5_10V_Ch, \
                                        pConfig->EN_5_10V_Bit );
    EN_5V_ADC_Val_Current = PLIB_PORTS_PinGetLatched( \
                                        PORTS_ID_0, \
                                        pConfig->EN_5V_ADC_Ch, \
                                        pConfig->EN_5V_ADC_Bit );
    EN_12V_Val_Current = PLIB_PORTS_PinGetLatched( \
                                        PORTS_ID_0, \
                                        pConfig->EN_12V_Ch, \
                                        pConfig->EN_12V_Bit );
    EN_Vref_Val_Current = PLIB_PORTS_PinGetLatched( \
                                        PORTS_ID_0, \
                                        pConfig->EN_Vref_Ch, \
                                        pConfig->EN_Vref_Bit );
             
    // Check to see if we are changing the state of this power pin
    if( EN_3_3V_Val_Current != pWriteVariables->EN_3_3V_Val ){
        PLIB_PORTS_PinWrite( \
                                        PORTS_ID_0, \
                                        pConfig->EN_3_3V_Ch, \
                                        pConfig->EN_3_3V_Bit, \
                                        pWriteVariables->EN_3_3V_Val );
    }
    
    // Check to see if we are changing the state of these power pins
    if( EN_5_10V_Val_Current != pWriteVariables->EN_5_10V_Val || \
        EN_5V_ADC_Val_Current != pWriteVariables->EN_5V_ADC_Val || \
        EN_12V_Val_Current != pWriteVariables->EN_12V_Val )
    {
        PLIB_PORTS_PinWrite( \
                                        PORTS_ID_0, \
                                        pConfig->EN_5_10V_Ch, \
                                        pConfig->EN_5_10V_Bit, \
                                        pWriteVariables->EN_5_10V_Val );

        PLIB_PORTS_PinWrite( \
                                        PORTS_ID_0, \
                                        pConfig->EN_5V_ADC_Ch, \
                                        pConfig->EN_5V_ADC_Bit, \
                                        pWriteVariables->EN_5V_ADC_Val );

        PLIB_PORTS_PinWrite( \
                                        PORTS_ID_0, \
                                        pConfig->EN_12V_Ch, \
                                        pConfig->EN_12V_Bit, \
                                        pWriteVariables->EN_12V_Val );
    }
    
    // Check to see if we are changing the state of this power pin
    if( EN_Vref_Val_Current != pWriteVariables->EN_Vref_Val ){
        PLIB_PORTS_PinWrite( \
                                        PORTS_ID_0, \
                                        pConfig->EN_Vref_Ch, \
                                        pConfig->EN_Vref_Bit, \
                                        pWriteVariables->EN_Vref_Val );
    }

}

static void Power_Up( void )
{
    // If the battery management is not enabled, wait for it to become ready
    while(!pData->BQ24297Data.initComplete)
    {
        vTaskDelay(100 / portTICK_PERIOD_MS);   
    }
    
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
    //Delay after turning up to full speed to allow steady-state
    //before powering system
    vTaskDelay( 50 / portTICK_PERIOD_MS );   
    
      // 3.3V Enable
    pWriteVariables->EN_3_3V_Val = true;
    Power_Write( ); 

    // 5V Enable
    if( ( pData->BQ24297Data.status.batPresent ) ||                         \
        ( pData->BQ24297Data.status.vBusStat == VBUS_CHARGER ) )
    {
        pWriteVariables->EN_5_10V_Val = true;
    }
    Power_Write( );
    vTaskDelay( 50 / portTICK_PERIOD_MS );
    
    // 5V ADC Enable
    pWriteVariables->EN_5V_ADC_Val = true;
    Power_Write( ); 
    vTaskDelay(50 / portTICK_PERIOD_MS);
    
    // 12V Enable (set low to turn on, set as input (or high if configured
    // as open collector) to turn off)
    pWriteVariables->EN_12V_Val = false;
    Power_Write( );
    vTaskDelay(50 / portTICK_PERIOD_MS);
    
    // Vref Enable
    pWriteVariables->EN_Vref_Val = true;
    Power_Write( );  
    vTaskDelay(50 / portTICK_PERIOD_MS);
    
    pData->powerState = POWERED_UP;

}

void Power_Down( void )
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
    SYS_CLK_SystemFrequencySet(SYS_CLK_SOURCE_PRIMARY_SYSPLL, 200000000, true);
    
    
    //SYS_CLK_SystemClockStatus
    
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
    Power_Write( );

    
    pData->powerState = MICRO_ON;    // Set back to default state
    pData->requestedPowerState = NO_CHANGE;
    
    // Delay 1000ms for power to discharge
    vTaskDelay( 1000 / portTICK_PERIOD_MS );
}


static void Power_UpdateState( void )
{
    // Set power state immediately if DO_POWER_DOWN is requested
    if( pData->requestedPowerState == DO_POWER_DOWN ){
        pData->powerState = POWERED_DOWN;
    }
    
    switch( pData->powerState ){
        case POWERED_DOWN:
            /* Not initialized or powered down */
            
            // Check to see if we've finished signaling the user of insufficient power if necessary
            if( pData->powerDnAllowed == true ){
                Power_Down( );
                // Reset the requested power state after handling request
                pData->requestedPowerState = NO_CHANGE;
            }
            break;
            
        case FRESH_BOOT:  
            
        case MICRO_ON:
        /* 3.3V rail enabled. Ready to check initial status 
         * NOTE: This is the default state if code is running!
         * There is no Vref at this time, so any read to ADC is invalid!
         */
            if( pData->requestedPowerState == DO_POWER_UP ){

                if( !pData->BQ24297Data.status.vsysStat || \
                    pData->BQ24297Data.status.pgStat )    // If batt voltage is greater than VSYSMIN or power is good, we can power up
                {
                    Power_Up( );
                    // Reset the requested power state after handling request
                    pData->requestedPowerState = NO_CHANGE;
                }

                else
                {
                    // Otherwise insufficient power.  Notify user and power down
                    pData->powerDnAllowed = false; // This will turn true after the LED sequence completes
                    pData->powerState = POWERED_DOWN;
                }
                
            }
            break;
            
        case POWERED_UP:
        /* Board fully powered. Monitor for any changes/faults
         * ADC readings are now valid!
         */ 
            Power_UpdateChgPct( );
            if( pData->requestedPowerState == DO_POWER_UP_EXT_DOWN || \
                ( pData->chargePct<BATT_LOW_TH && \
                  !pData->BQ24297Data.status.pgStat ) ){
                // If battery is low on charge and we are not plugged in, disable external supplies
                pWriteVariables->EN_5_10V_Val = false;
                Power_Write( );
                pData->powerState = POWERED_UP_EXT_DOWN;
                // Reset the requested power state after handling request
                pData->requestedPowerState = NO_CHANGE;
            }
            break;
            
        case POWERED_UP_EXT_DOWN:
        /* Board partially powered. Monitor for any changes */ 
            Power_UpdateChgPct( );
            if( pData->chargePct > ( BATT_LOW_TH + BATT_LOW_HYST ) || \
                ( pData->BQ24297Data.status.inLim > 1 ) ){
                if( pData->requestedPowerState == DO_POWER_UP){
                    // If battery is charged or we are plugged in, enable external supplies
                    pWriteVariables->EN_5_10V_Val = true;
                    Power_Write( );
                    pData->powerState = POWERED_UP;
                    // Reset the requested power state after handling request
                    pData->requestedPowerState = NO_CHANGE;
                }
            
            // Else, remain here because the user didn't want to be fully powered
            }else if( pData->chargePct < BATT_EXH_TH){
                // Code below is commented out when I2C is disabled
                // Insufficient power.  Notify user and power down.
                pData->powerDnAllowed = false;   // This will turn true after the LED sequence completes
                pData->powerState = POWERED_DOWN;
            }

            break;
    }
 
}

static void Power_UpdateChgPct( void )
{
    size_t index = ADC_FindChannelIndex( ADC_CHANNEL_VBATT );
    // TODO: Add data validation without blocking
//    while(!ADC_IsDataValid(&g_BoardData.AInLatest.Data[index]))
//    {
//        vTaskDelay(100 / portTICK_PERIOD_MS);
//    }
    const AInSample *pAnalogSample = BoardData_Get(                         \
                  BOARDDATA_AIN_LATEST,                                     \
                  index );
    if( NULL != pAnalogSample ){
        pData->battVoltage = ADC_ConvertToVoltage( pAnalogSample );
    }

    // Function below is defined from 3.17-3.868.  Must coerce input value to within these bounds.
    if( pData->battVoltage < 3.17 ){
        pData->chargePct=0;
    }else if( pData->battVoltage > 3.868 ){
        pData->chargePct=100;
    }else{
        pData->chargePct = 142.92 * pData->battVoltage - 452.93;
    }
}

static void Power_Update_Settings( void )
{
    // Change charging/other power settings based on current status
       
    // Check new power source and set parameters accordingly
    BQ24297_AutoSetILim( );
    
    // Enable/disable charging
    BQ24297_ChargeEnable( pData->BQ24297Data.status.batPresent);
}


