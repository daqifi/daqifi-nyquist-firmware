/* ************************************************************************** */
/** UI.c

  @Company
    DAQifi

  @File Name
    UI.c

  @Summary
    This file contains user interface (LED and button) handling functions.
 */
/* ************************************************************************** */

#include "UI.h"
#include "state/runtime/StreamingRuntimeConfig.h"

#define UI_POWER_ON_TASK_CALLING_PRD (125 * 4)      //  Multiply by clock divider due to the slower clock frequency when "off"
                                                    //  This multiplier value is the square of SYS_CLK_DIV_PWR_SAVE

#define UI_TASK_CALLING_PRD 125 //  125ms
#define BUTTON_POWER_ON_TH (1000/UI_POWER_ON_TASK_CALLING_PRD)   //  2 seconds (~2 seconds are required for power on sequence)
#define BUTTON_POWER_OFF_TH (2000/UI_TASK_CALLING_PRD)  //  2 seconds (~1 second is required to power off)

//! Pointer to the data stucture where the UI configuration parameters
// will be stored
static sUIConfig *pConfig;
//! Pointer to the data structure where the UI read variables will be stored
static sUIReadVars *pReadVariables;
//! Pointer to the data structure where the UI Power Data will be stored
static sPowerData *pPowerData;

/*! Function for initializing UI layer, in which button and LED tasks 
 * are included
 * @param[in] pConfigInit Pointer to the configuration data structure
 * @param[in] pReadVarsInit Pointer to the Read Variables data structure
 * @param[in] pPowerDataInit Pointer to the Power Data data stucture
 */
void UI_Init( \
                sUIConfig *pConfigInit, \
                sUIReadVars *pReadVarsInit, \
                sPowerData *pPowerDataInit ){
    pConfig = pConfigInit;
    pReadVariables = pReadVarsInit;
    pPowerData = pPowerDataInit;
}

void Button_Tasks( void )
{
    static uint16_t buttonPressCount = 0;   // Number of times the function has consecutively detected a button press
    static bool oneShot = false;    // One shot variable to ensure only action is performed if the user holds the button for a long period of time
    
    pReadVariables->button = PLIB_PORTS_PinGet( \
                                        pConfig->button_Mod, \
                                        pConfig->button_Ch, \
                                        pConfig->button_Bit);
    
    if( pReadVariables->button )
    {
        buttonPressCount++;
        if( pPowerData->powerState == MICRO_ON )
        {
            if( ( buttonPressCount > BUTTON_POWER_ON_TH ) && !oneShot )
            {
                // Signal board to power up
                pPowerData->requestedPowerState = DO_POWER_UP;
                oneShot = true;
            }
        }
        
        if( ( pPowerData->powerState > MICRO_ON) && \
            ( buttonPressCount > BUTTON_POWER_OFF_TH ) && \
            !oneShot )
        {
            pPowerData->powerDnAllowed = true;   // User requested power down.  Allow board to turn off without LED indication.
            // Signal board to power off
            pPowerData->requestedPowerState = DO_POWER_DOWN;
            oneShot = true;
        }
         
    }
    else
    {
        buttonPressCount = 0;
        oneShot = false;
    }
     
}

void LED_Tasks( bool streamingFlag )
{
    static uint16_t sequenceNum = 0;
    static int8_t currentPattern = 0;
    static int8_t repeatSeq = 0;
    static int8_t repeatSeqNum = 0;
    static uint32_t periodCount = 1;    // If we've gotten in this function, the period is at least 1
    static bool errorDisplayPending = false;
    static bool begunErrorDisplay = false;
    
    volatile bool pluggedIn = false;
    volatile bool poweredOn = false;
    volatile bool charging = false;
    volatile bool streaming = false;
    volatile bool battLow = false;
    volatile bool genError = false;
    uint32_t period = 1;                // Lowest allowable period is 1
    
    
    // Assign nicer variable names to make the code below more readable
    pluggedIn = pPowerData->BQ24297Data.status.pgStat;
       
    poweredOn = ( pPowerData->powerState > 0 );

    charging = \
        ( ( pPowerData->BQ24297Data.status.chgStat == CHG_STAT_PRECHARGE ) || \
          ( pPowerData->BQ24297Data.status.chgStat == CHG_STAT_FASTCHARGE ) );

    streaming = streamingFlag;
    
    battLow = pPowerData->BQ24297Data.status.vsysStat;

    genError = false;
    
    // Code below tests currentPattern to make sure another sequence is not currently running (like a mutex).
    // 0 means no sequence is executing.  A number above 0 means that array number is executing.
    
    if( pPowerData->powerState == POWER_DOWN ){
        // Batt exhausted
        currentPattern = 2; // Take over any other sequence
        repeatSeq = 4;  // This sequence should repeat 4 times
        // If we've finished our repetitions, allow board to be powered down.
        if( repeatSeqNum == repeatSeq ){
            pPowerData->powerDnAllowed = true;
        }
    }
    else if( pluggedIn && !poweredOn && charging && !streaming ) // && !battLow)
    {   // Plugged in and charging - NOTE: This state has been temporarily disable to avoid confusion during debugging
        if( currentPattern == 0 ){
            currentPattern = 3;
        }
    }
    else if( pluggedIn && poweredOn && !charging && !streaming) //  && !battLow)
    {   // Plugged in and powered on
        if( currentPattern == 0 ){
            currentPattern = 4;
        }
    }
    else if(pluggedIn && poweredOn && charging && !streaming) //  && !battLow)
    {   // Plugged in, powered on, and charging
        if( currentPattern == 0 ){
            currentPattern = 5;
        }
    }
    else if(pluggedIn && poweredOn && !charging && streaming) //  && !battLow)
    {   // Plugged in, powered on, and streaming
        if( currentPattern == 0 ){
            currentPattern = 6;
        }
    }
    else if(pluggedIn && poweredOn && charging && streaming) //  && !battLow)
    {   // Plugged in, powered on, charging, and streaming
        if( currentPattern == 0 ){
            currentPattern = 7;
        }
    }
    else if(!pluggedIn && poweredOn && !charging && !streaming) //  && !battLow)
    {   // Powered on
        if( currentPattern == 0 ){
            currentPattern = 8;
        }
    }
    else if( !pluggedIn && poweredOn && !charging && streaming ) //  && !battLow)
    {   // Powered on and streaming
        if( currentPattern == 0 ){
            currentPattern = 9;
        }
    }
    else if( !pluggedIn && poweredOn && !charging && !streaming && battLow )
    {   // Powered on and batt low
        if( currentPattern == 0 ){
            currentPattern = 10;
        }
    }
    else if(!pluggedIn && poweredOn && !charging && streaming && battLow)
    {   // Powered on, streaming, and batt low
        if( currentPattern == 0 ){
            currentPattern = 11;
        }
    }
    else
    {
        currentPattern = 0;
    }
    
    // If we have an error, we want to allow the sequence to complete at least once, then repeat with the error at the end
    if( genError && !errorDisplayPending && ( pluggedIn || poweredOn ) )    // Only generate an error if plugged in/powered on
    {
        repeatSeq++;
        errorDisplayPending = true;
        begunErrorDisplay = false;
    }
    
    if( begunErrorDisplay || \
            (repeatSeqNum == 0 && sequenceNum==0 && errorDisplayPending ) )  // If we are at the beginning of the sequence and the beginning of the repeat, display error
    {
        begunErrorDisplay = true;
        
        // Display the error sequence as this is the first repetition of this state
        if( pConfig->LED1_Ind.period[ 1 ] )
        {
            // If the error pattern specifies a rate, it is a pattern that should overwrite the current pattern for LED 1
            PLIB_PORTS_PinWrite( \
                    pConfig->LED1_Mod, \
                    pConfig->LED1_Ch, \
                    pConfig->LED1_Bit, \
                    pConfig->LED1_Ind.patterns[ 1 ][ sequenceNum ] );
        }
        else
        {
            // Otherwise keep displaying the current pattern for LED 1
            if( pConfig->LED1_Ind.period[ currentPattern ] ){
                PLIB_PORTS_PinWrite( \
                    pConfig->LED1_Mod, \
                    pConfig->LED1_Ch, \
                    pConfig->LED1_Bit, \
                    pConfig->LED1_Ind.patterns[ currentPattern ][ sequenceNum ] );
            }
        }
        
        if( pConfig->LED2_Ind.period[ 1 ] ){
            // If the error pattern specifies a rate, it is a pattern that should overwrite the current pattern for LED 2
            PLIB_PORTS_PinWrite( \
                    pConfig->LED2_Mod, \
                    pConfig->LED2_Ch, \
                    pConfig->LED2_Bit, \
                    pConfig->LED2_Ind.patterns[ 1 ][ sequenceNum ] );
        }
        else{
            // Otherwise keep displaying the current pattern for LED 2
            if( pConfig->LED2_Ind.period[ currentPattern ] ){
                PLIB_PORTS_PinWrite( \
                    pConfig->LED2_Mod, \
                    pConfig->LED2_Ch, \
                    pConfig->LED2_Bit, \
                    pConfig->LED2_Ind.patterns[ currentPattern ][ sequenceNum ] );
            }
        }
        
    }
    else
    {
        // Otherwise display the states as normal
        if( pConfig->LED1_Ind.period[ currentPattern ] ){
            PLIB_PORTS_PinWrite( \
                    pConfig->LED1_Mod, \
                    pConfig->LED1_Ch, \
                    pConfig->LED1_Bit, \
                    pConfig->LED1_Ind.patterns[ currentPattern ][ sequenceNum ] );
        }
        if( pConfig->LED2_Ind.period[ currentPattern ] ){
            PLIB_PORTS_PinWrite( \
                    pConfig->LED2_Mod, \
                    pConfig->LED2_Ch, \
                    pConfig->LED2_Bit, \
                    pConfig->LED2_Ind.patterns[ currentPattern ][ sequenceNum ] );
        }
    }

    period = pConfig->LED1_Ind.period[ currentPattern ];
    if( pConfig->LED2_Ind.period[ currentPattern ] > period ){
        period = pConfig->LED2_Ind.period[ currentPattern ];
    }

    // Update global LED values
    pReadVariables->LED1 = PLIB_PORTS_PinGet( \
                                pConfig->LED1_Mod, \
                                pConfig->LED1_Ch, \
                                pConfig->LED1_Bit );
    pReadVariables->LED2 = PLIB_PORTS_PinGet( \
                                pConfig->LED2_Mod, \
                                pConfig->LED2_Ch, \
                                pConfig->LED2_Bit );
    
    if( periodCount == period ){   // If we've waited the defined period time, execute sequence otherwise exit and wait longer
        periodCount = 1; // Reset period count to 1
        // If we are currently in a sequence, increase the sequence number
        // if we are beyond the maximum sequence number, reset the sequence
        if( sequenceNum == MAX_SEQ_NUM - 1 )  // If sequence number has incremented past maximum, wrap back around
        {
            sequenceNum = 0;
            repeatSeqNum++;     // Increase the repeatSeqNum value to show how many times we've displayed the sequence
            if( begunErrorDisplay )  // If we haven't displayed the error, don't reset the pending flags
            {
                errorDisplayPending = false;     
                begunErrorDisplay = false;
            }
        }
        else
        {
            sequenceNum ++;     // Increase sequence number to advance to the next display values
        }

        // If we've reached the required number of repetitions, reset all values
        if( repeatSeqNum > repeatSeq + 1 ){
            repeatSeq = 0;
            repeatSeqNum = 0;
            sequenceNum = 0;
            currentPattern = 0;
        }
    }
    else
    {
        periodCount++;   // Increment period count if we are waiting
    }
}

/* *****************************************************************************
 End of File
 */
