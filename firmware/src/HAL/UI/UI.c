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

#define UI_TASK_CALLING_PRD 100 //  100ms
//!  Multiply by clock divider due to the slower clock frequency when "off"
//!  This multiplier value is the square of SYS_CLK_DIV_PWR_SAVE
#define UI_POWER_ON_TASK_CALLING_PRD (UI_TASK_CALLING_PRD * 4)      
 //!  ~1 second (+250 ms are required for power on sequence)                                                                   
#define BUTTON_POWER_ON_TH (1000/UI_POWER_ON_TASK_CALLING_PRD)  
#define BUTTON_POWER_OFF_TH (1000/UI_TASK_CALLING_PRD)  //  ~1 second

//! Pointer to the data structure where the UI configuration parameters
// will be stored
static tUIConfig *gpConfig;
//! Pointer to the data structure where the UI read variables will be stored
static tUIReadVars *gpReadVariables;
//! Pointer to the data structure where the UI Power Data will be stored
static tPowerData *gpPowerData;

void UI_Init(                                                               
                tUIConfig *pConfigInit,                                     
                tUIReadVars *pReadVarsInit,                                 
                tPowerData *pPowerDataInit )
{
    gpConfig = pConfigInit;
    gpReadVariables = pReadVarsInit;
    gpPowerData = pPowerDataInit;
}

void Button_Tasks( void )
{
    // Number of times the function has consecutively detected a button press
    static uint16_t buttonPressCount = 0;   
    // One shot variable to ensure only action is performed if the user 
    // holds the button for a long period of time
    static bool oneShot = false;    
    
    gpReadVariables->button = GPIO_PinRead(gpConfig->button_Pin);
    
    if(gpReadVariables->button)
    {
        buttonPressCount++;
        switch(gpPowerData->powerState){
            case MICRO_ON:
                if((buttonPressCount > BUTTON_POWER_ON_TH) && !oneShot)
                {
                    // Signal board to power up
                    gpPowerData->requestedPowerState = DO_POWER_UP;
                    oneShot = true;
                }
                break;
                
            case POWERED_UP:
            case POWERED_UP_EXT_DOWN:
                if((buttonPressCount > BUTTON_POWER_OFF_TH) && !oneShot)
                {
                    // User requested power down.  Allow board to turn 
                    // off without LED indication.
                    gpPowerData->powerDnAllowed = true;   
                    // Signal board to power off
                    gpPowerData->requestedPowerState = DO_POWER_DOWN;
                    oneShot = true;
                }
                break;
            default:
                break;
        }
    }
    else
    {
        buttonPressCount = 0;
        oneShot = false;
    }   
}

void LED_Tasks(bool streamingFlag)
{
    static uint16_t sequenceNum = 0;
    static int8_t currentPattern = 0;
    static int8_t repeatSeq = 0;
    static int8_t repeatSeqNum = 0;
    // If we've gotten in this function, the period is at least 1
    static uint32_t periodCount = 1;    
    static bool errorDisplayPending = false;
    static bool begunErrorDisplay = false;
    
    volatile bool pluggedIn = false;
    volatile bool poweredOn = false;
    volatile bool charging = false;
    volatile bool streaming = false;
    volatile bool battLow = false;
    volatile bool genError = false;
    // Lowest allowable period is 1
    uint32_t period = 1;                
    
    
    // Assign nicer variable names to make the code below more readable
    pluggedIn = gpPowerData->BQ24297Data.status.pgStat;
       
    poweredOn = (gpPowerData->powerState == POWERED_UP ||                    
                 gpPowerData->powerState == POWERED_UP_EXT_DOWN);

    charging = ((gpPowerData->BQ24297Data.status.chgStat == CHG_STAT_PRECHARGE)
            || (gpPowerData->BQ24297Data.status.chgStat == CHG_STAT_FASTCHARGE));

    streaming = streamingFlag;
    
    battLow = gpPowerData->BQ24297Data.status.vsysStat;

    genError = false;
    
    // Code below tests currentPattern to make sure another sequence is not
    // currently running (like a mutex).
    // 0 means no sequence is executing.  A number above 0 means that array 
    // number is executing.
    
    // If we are directed to power down, turn off LEDs
    if(gpPowerData->requestedPowerState == DO_POWER_DOWN){
        // Reset and take over any other sequence
        repeatSeq = 0;
        repeatSeqNum = 0;
        sequenceNum = 0;
        currentPattern = 0; 
        // TODO: This should remain false until we've signaled to the user we are going down for any reason
        // other than button power off
        gpPowerData->powerDnAllowed = true;    
    }
    // If we are directed to power up, turn on LED
    else if(gpPowerData->requestedPowerState == DO_POWER_UP){
        repeatSeq = 0;
        repeatSeqNum = 0;
        sequenceNum = 0;
        currentPattern = 4; // Take over any other sequence and turn on power LED
    }
    else if(pluggedIn && !poweredOn && charging && !streaming) // && !battLow)
    {   // Plugged in and charging - TODO NOTE: This state has been 
        // temporarily disabled to avoid confusion during debugging
        if(currentPattern == 0) currentPattern = 3;
    }
    else if(pluggedIn && poweredOn && !charging && !streaming) //  && !battLow)
    {   // Plugged in and powered on
        if(currentPattern == 0) currentPattern = 4;
    }
    else if(pluggedIn && poweredOn && charging && !streaming) //  && !battLow)
    {   // Plugged in, powered on, and charging
        if(currentPattern == 0) currentPattern = 5;
    }
    else if(pluggedIn && poweredOn && !charging && streaming) //  && !battLow)
    {   // Plugged in, powered on, and streaming
        if(currentPattern == 0) currentPattern = 6;
    }
    else if(pluggedIn && poweredOn && charging && streaming) //  && !battLow)
    {   // Plugged in, powered on, charging, and streaming
        if(currentPattern == 0) currentPattern = 7;
    }
    else if(!pluggedIn && poweredOn && !charging && !streaming) //  && !battLow)
    {   // Powered on
        if(currentPattern == 0) currentPattern = 8;
    }
    else if(!pluggedIn && poweredOn && !charging && streaming) //  && !battLow)
    {   // Powered on and streaming
        if(currentPattern == 0) currentPattern = 9;
    }
    else if(!pluggedIn && poweredOn && !charging && !streaming && battLow)
    {   // Powered on and batt low
        if(currentPattern == 0) currentPattern = 10;
    }
    else if(!pluggedIn && poweredOn && !charging && streaming && battLow)
    {   // Powered on, streaming, and batt low
        if(currentPattern == 0) currentPattern = 11;
    }
    else
    {
        currentPattern = 0;
    }
    
    // If we have an error, we want to allow the sequence to complete at 
    // least once, then repeat with the error at the end
    // Only generate an error if plugged in/powered on
    if(genError && !errorDisplayPending && (pluggedIn || poweredOn))    
    {
        repeatSeq++;
        errorDisplayPending = true;
        begunErrorDisplay = false;
    }
    // If we are at the beginning of the sequence and the beginning of the 
    // repeat, display error
    if(begunErrorDisplay || (repeatSeqNum == 0 && sequenceNum==0            
                        && errorDisplayPending))  
    {
        begunErrorDisplay = true;
        
        // Display the error sequence as this is the first repetition of this state
        if(gpConfig->LED1_Ind.period[1])
        {
            // If the error pattern specifies a rate, it is a pattern that
            // should overwrite the current pattern for LED 1
            GPIO_PinWrite(gpConfig->LED1_Pin,                                  
                        gpConfig->LED1_Ind.patterns[1][sequenceNum]);
        }
        else
        {
            // Otherwise keep displaying the current pattern for LED 1
            if(gpConfig->LED1_Ind.period[currentPattern])
            {
                GPIO_PinWrite(gpConfig->LED1_Pin,                                  
                        gpConfig->LED1_Ind.patterns[currentPattern][sequenceNum]);
            }
        }
        
        if(gpConfig->LED2_Ind.period[1])
        {
            // If the error pattern specifies a rate, it is a pattern that should overwrite the current pattern for LED 2
            GPIO_PinWrite(gpConfig->LED2_Pin,                                  
                        gpConfig->LED2_Ind.patterns[1][sequenceNum]);
        }
        else
        {
            // Otherwise keep displaying the current pattern for LED 2
            if(gpConfig->LED2_Ind.period[currentPattern])
            {
                GPIO_PinWrite(gpConfig->LED2_Pin,                                  
                        gpConfig->LED2_Ind.patterns[currentPattern][sequenceNum]);
            }
        }
        
    }
    else
    {
        // Otherwise display the states as normal
        if(gpConfig->LED1_Ind.period[currentPattern])
        {
            GPIO_PinWrite(gpConfig->LED1_Pin,                                      
                    gpConfig->LED1_Ind.patterns[currentPattern][sequenceNum]);
        }
        if(gpConfig->LED2_Ind.period[currentPattern])
        {
            GPIO_PinWrite(gpConfig->LED2_Pin,                                      
                    gpConfig->LED2_Ind.patterns[currentPattern][sequenceNum]);
        }
    }

    period = gpConfig->LED1_Ind.period[currentPattern];
    if(gpConfig->LED2_Ind.period[currentPattern] > period)
    {
        period = gpConfig->LED2_Ind.period[currentPattern];
    }

    // Update global LED values
    gpReadVariables->LED1 = GPIO_PinRead(gpConfig->LED1_Pin);
    gpReadVariables->LED2 = GPIO_PinRead(gpConfig->LED2_Pin);
    
    // If we've waited the defined period time, execute sequence otherwise
    //exit and wait longer
    if(periodCount == period){  
        periodCount = 1; // Reset period count to 1
        // If we are currently in a sequence, increase the sequence number
        // if we are beyond the maximum sequence number, reset the sequence
        // If sequence number has incremented past maximum, wrap back around
        if (sequenceNum == MAX_SEQ_NUM-1)  
        {
            sequenceNum = 0;
            repeatSeqNum++;     // Increase the repeatSeqNum value to show how many times we've displayed the sequence
            if (begunErrorDisplay)  // If we haven't displayed the error, don't reset the pending flags
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
        if(repeatSeqNum > repeatSeq+1){
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
