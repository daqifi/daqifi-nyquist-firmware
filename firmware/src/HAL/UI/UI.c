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
#include <string.h>  /* memset (#409 retained-RAM defensive init) */
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

    /* Defensive zero of the UI read-vars struct (LED1/LED2/button bools).
     * Lives in retained RAM; without this, button state could be
     * misread immediately at boot before the first GPIO read. See #409. */
    memset(pReadVarsInit, 0, sizeof(*pReadVarsInit));
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
            case STANDBY:
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
                    gpPowerData->shutdownNotified = true;   
                    // Signal board to power off
                    gpPowerData->requestedPowerState = DO_POWER_DOWN;
                    // #454/#1084: with USB present, latch the per-VBUS-session
                    // auto-promote suppression, as SYSTem:POWer:STATe 0 does,
                    // so with AUTOOn on the STANDBY pass that runs this
                    // power-down does not promote the board straight back up.
                    //
                    // ⛔ SET ONLY, NEVER CLEAR. An earlier revision of this
                    // assigned pgStat directly:
                    //
                    //   autoPromotedThisVbusSession = BQ24297Data.status.pgStat;
                    //
                    // which also WRITES FALSE on a stale-false reading, and
                    // that clear cancels the very shutdown this fix exists to
                    // make stick (audit on #1092, blind leg, INTRODUCED):
                    //
                    //   boot on USB      -> auto-promote sets the latch true
                    //   unplug           -> still POWERED_UP, so nothing clears
                    //                       it, but cached pgStat goes false
                    //   USB back during  -> this site wrote FALSE from the
                    //   the ~1.1 s hold     stale reading, dropping a valid
                    //                       suppression
                    //   Power_Tasks      -> refreshes pgStat true, consumes
                    //                       DO_POWER_DOWN -> STANDBY
                    //   next pass        -> vbusPresent && !latch -> AUTO-PROMOTE
                    //
                    // i.e. the user held the button with USB connected and the
                    // board came back up: #1084's own symptom, reached through
                    // the cache instead of through the missing latch. A
                    // previous revision of this comment called that window
                    // acceptable ("that plug-in then counts as a new
                    // session"); it is not, because the cost is the shutdown.
                    //
                    // Clearing is NOT this path's job and never needed to be.
                    // Power_HandleStandbyState() owns the VBUS-session
                    // lifecycle and re-arms on any STANDBY pass without VBUS
                    // (PowerApi.c, "VBUS gone -- re-arm auto-promote for next
                    // plug-in"), reading pgStat in its own fresh context. So a
                    // genuine battery-only press leaves the latch alone here,
                    // that pass clears it, and a later plug-in still
                    // auto-promotes -- the round-1 battery-only gap stays
                    // closed without this site deciding anything.
                    //
                    // This also makes it the only writer of this latch that
                    // writes a CONSTANT, like every other one (PowerApi.c,
                    // SCPIInterface.c): a stale cache can now delay the
                    // suppression, never undo it.
                    /* ⛔ #1092 r3: SET UNCONDITIONALLY. This was gated on
                     * the cached pgStat, which left the r2 residual: a
                     * stale-FALSE reading at press time means the latch is not
                     * set, Power_Tasks then refreshes pgStat to true, and the
                     * next STANDBY pass auto-promotes the board back up --
                     * #1084's own symptom, reached through the cache rather
                     * than through a missing fix.
                     *
                     * r2 framed that as unfixable from here ("you cannot set a
                     * latch you do not know you need"). You can: set it
                     * ALWAYS, because the two errors are not symmetric.
                     * Setting it when it was not needed costs NOTHING --
                     * Power_HandleStandbyState owns this latch's lifecycle and
                     * re-arms it on any STANDBY pass without VBUS ("VBUS gone
                     * -- re-arm auto-promote for next plug-in"), so a
                     * battery-only press self-corrects on the very next pass
                     * and a later plug-in still auto-promotes. FAILING to set
                     * it costs the shutdown.
                     *
                     * So this stays the SET-ONLY, NEVER-CLEAR writer described
                     * above, and it now writes a constant unconditionally: a
                     * stale cache can no longer even delay the suppression,
                     * let alone undo it. */
                    gpPowerData->autoPromotedThisVbusSession = true;
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
        gpPowerData->shutdownNotified = true;    
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
