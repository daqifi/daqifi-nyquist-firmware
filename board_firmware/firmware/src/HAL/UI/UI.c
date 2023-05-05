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
#define UI_POWER_ON_TASK_CALLING_PRD (UI_TASK_CALLING_PRD * 4)      //  Multiply by clock divider due to the slower clock frequency when "off"
                                                                    //  This multiplier value is the square of SYS_CLK_DIV_PWR_SAVE
#define BUTTON_POWER_ON_TH (1000/UI_POWER_ON_TASK_CALLING_PRD)   //  ~1 second (+250 ms are required for power on sequence)
#define BUTTON_POWER_OFF_TH (1000/UI_TASK_CALLING_PRD)  //  ~1 second


void Button_Tasks(tUIConfig config, tUIReadVars *UIReadVars, tPowerData *PowerData)
{
    static uint16_t buttonPressCount = 0;   // Number of times the function has consecutively detected a button press
    static bool oneShot = false;    // One shot variable to ensure only action is performed if the user holds the button for a long period of time
    
    UIReadVars->button = PLIB_PORTS_PinGet(config.button_Mod, config.button_Ch, config.button_Bit);
    
    if(UIReadVars->button)
    {
        buttonPressCount++;
        switch(PowerData->powerState){
            case MICRO_ON:
                if((buttonPressCount > BUTTON_POWER_ON_TH) && !oneShot)
                {
                    // Signal board to power up
                    PowerData->powerState = DO_POWER_UP;
                    oneShot = true;
                }
                break;
                
            case POWERED_UP:
            case POWERED_UP_EXT_DOWN:
                if((buttonPressCount > BUTTON_POWER_OFF_TH) && !oneShot)
                {
                    PowerData->powerDnAllowed = true;   // User requested power down.  Allow board to turn off without LED indication.
                    // Signal board to power off
                    PowerData->powerState = DO_POWER_DOWN;
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

void LED_Tasks(tUIConfig config, tPowerData *PowerData, tUIReadVars *UIReadVars, bool streamingFlag)
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
    pluggedIn = PowerData->BQ24297Data.status.pgStat;
       
    poweredOn = (PowerData->powerState == POWERED_UP || PowerData->powerState == POWERED_UP_EXT_DOWN);

    charging = ((PowerData->BQ24297Data.status.chgStat == CHG_STAT_PRECHARGE) || (PowerData->BQ24297Data.status.chgStat == CHG_STAT_FASTCHARGE));

    streaming = streamingFlag;
    
    battLow = PowerData->BQ24297Data.status.vsysStat;

    genError = false;
    
    // Code below tests currentPattern to make sure another sequence is not currently running (like a mutex).
    // 0 means no sequence is executing.  A number above 0 means that array number is executing.
    
    // If we are directed to power down, turn off LEDs
    if(PowerData->powerState == DO_POWER_DOWN){
        // Reset and take over any other sequence
        repeatSeq = 0;
        repeatSeqNum = 0;
        sequenceNum = 0;
        currentPattern = 0; 
    }
    // If we are directed to power down, turn on LED
    else if(PowerData->powerState == DO_POWER_UP){
        repeatSeq = 0;
        repeatSeqNum = 0;
        sequenceNum = 0;
        currentPattern = 4; // Take over any other sequence and turn on power LED
    }
    else if(pluggedIn && !poweredOn && charging && !streaming) // && !battLow)
    {   // Plugged in and charging - TODO NOTE: This state has been temporarily disabled to avoid confusion during debugging
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
    
    // If we have an error, we want to allow the sequence to complete at least once, then repeat with the error at the end
    if(genError && !errorDisplayPending && (pluggedIn || poweredOn))    // Only generate an error if plugged in/powered on
    {
        repeatSeq++;
        errorDisplayPending = true;
        begunErrorDisplay = false;
    }
    
    if(begunErrorDisplay || (repeatSeqNum == 0 && sequenceNum==0 && errorDisplayPending))  // If we are at the beginning of the sequence and the beginning of the repeat, display error
    {
        begunErrorDisplay = true;
        
        // Display the error sequence as this is the first repetition of this state
        if(config.LED1_Ind.period[1])
        {
            // If the error pattern specifies a rate, it is a pattern that should overwrite the current pattern for LED 1
            PLIB_PORTS_PinWrite(config.LED1_Mod, config.LED1_Ch, config.LED1_Bit, config.LED1_Ind.patterns[1][sequenceNum]);
        }
        else
        {
            // Otherwise keep displaying the current pattern for LED 1
            if(config.LED1_Ind.period[currentPattern]) PLIB_PORTS_PinWrite(config.LED1_Mod, config.LED1_Ch, config.LED1_Bit, config.LED1_Ind.patterns[currentPattern][sequenceNum]);
        }
        
        if(config.LED2_Ind.period[1])
        {
            // If the error pattern specifies a rate, it is a pattern that should overwrite the current pattern for LED 2
            PLIB_PORTS_PinWrite(config.LED2_Mod, config.LED2_Ch, config.LED2_Bit, config.LED2_Ind.patterns[1][sequenceNum]);
        }
        else
        {
            // Otherwise keep displaying the current pattern for LED 2
            if(config.LED2_Ind.period[currentPattern]) PLIB_PORTS_PinWrite(config.LED2_Mod, config.LED2_Ch, config.LED2_Bit, config.LED2_Ind.patterns[currentPattern][sequenceNum]);
        }
        
    }
    else
    {
        // Otherwise display the states as normal
        if(config.LED1_Ind.period[currentPattern]) PLIB_PORTS_PinWrite(config.LED1_Mod, config.LED1_Ch, config.LED1_Bit, config.LED1_Ind.patterns[currentPattern][sequenceNum]);
        if(config.LED2_Ind.period[currentPattern]) PLIB_PORTS_PinWrite(config.LED2_Mod, config.LED2_Ch, config.LED2_Bit, config.LED2_Ind.patterns[currentPattern][sequenceNum]);
    }

    period = config.LED1_Ind.period[currentPattern];
    if(config.LED2_Ind.period[currentPattern] > period) period = config.LED2_Ind.period[currentPattern];

    // Update global LED values
    UIReadVars->LED1 = PLIB_PORTS_PinGet(config.LED1_Mod, config.LED1_Ch, config.LED1_Bit);
    UIReadVars->LED2 = PLIB_PORTS_PinGet(config.LED2_Mod, config.LED2_Ch, config.LED2_Bit);
    
    if(periodCount == period){   // If we've waited the defined period time, execute sequence otherwise exit and wait longer
        periodCount = 1; // Reset period count to 1
        // If we are currently in a sequence, increase the sequence number
        // if we are beyond the maximum sequence number, reset the sequence
        if (sequenceNum == MAX_SEQ_NUM-1)  // If sequence number has incremented past maximum, wrap back around
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

/* *****************************************************************************
 End of File
 */
