/*******************************************************************************
  MPLAB Harmony Application Source File
  
  Company:
    Microchip Technology Inc.
  
  File Name:
    app.c

  Summary:
    This file contains the source code for the MPLAB Harmony application.

  Description:
    This file contains the source code for the MPLAB Harmony application.  It 
    implements the logic of the application's state machine and it may call 
    API routines of other MPLAB Harmony modules in the system, such as drivers,
    system services, and middleware.  However, it does not call any of the
    system interfaces (such as the "Initialize" and "Tasks" functions) of any of
    the modules in the system or make any assumptions about when those functions
    are called.  That is the responsibility of the configuration-specific system
    files.
 *******************************************************************************/

// DOM-IGNORE-BEGIN
/*******************************************************************************
Copyright (c) 2013-2014 released Microchip Technology Inc.  All rights reserved.

Microchip licenses to you the right to use, modify, copy and distribute
Software only when embedded on a Microchip microcontroller or digital signal
controller that is integrated into your product or third party product
(pursuant to the sublicense terms in the accompanying license agreement).

You should refer to the license agreement accompanying this Software for
additional information regarding your rights and obligations.

SOFTWARE AND DOCUMENTATION ARE PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY WARRANTY OF
MERCHANTABILITY, TITLE, NON-INFRINGEMENT AND FITNESS FOR A PARTICULAR PURPOSE.
IN NO EVENT SHALL MICROCHIP OR ITS LICENSORS BE LIABLE OR OBLIGATED UNDER
CONTRACT, NEGLIGENCE, STRICT LIABILITY, CONTRIBUTION, BREACH OF WARRANTY, OR
OTHER LEGAL EQUITABLE THEORY ANY DIRECT OR INDIRECT DAMAGES OR EXPENSES
INCLUDING BUT NOT LIMITED TO ANY INCIDENTAL, SPECIAL, INDIRECT, PUNITIVE OR
CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA, COST OF PROCUREMENT OF
SUBSTITUTE GOODS, TECHNOLOGY, SERVICES, OR ANY CLAIMS BY THIRD PARTIES
(INCLUDING BUT NOT LIMITED TO ANY DEFENSE THEREOF), OR OTHER SIMILAR COSTS.
 *******************************************************************************/
// DOM-IGNORE-END


// *****************************************************************************
// *****************************************************************************
// Section: Included Files 
// *****************************************************************************
// *****************************************************************************

#include "app.h"

#include "system/system.h"
#include "bootloader/src/bootloader.h"
#include "bootloader/src/nvm.h"
#include "peripheral/peripheral.h"
#include <sys/kmem.h> 
#include "system_definitions.h"

// *****************************************************************************
// *****************************************************************************
// Section: Global Data Definitions
// *****************************************************************************
// *****************************************************************************

// *****************************************************************************
/* Application Data

  Summary:
    Holds application data

  Description:
    This structure holds the application's data.

  Remarks:
    This structure should be initialized by the APP_Initialize function.
    
    Application strings and buffers are be defined outside this structure.
*/

#define UNUSED(x) (void)(x)

// These are defined as preprocessor macros in the app and bootloader
//#define FORCE_BOOTLOADER_FLAG_ADDR 0x8007FFE0 // Address must match what is defined in the project preprocessor definition (0x80000000 is the beginning 16 bytes of data mem)
//#define FORCE_BOOT_VALUE 0xF4CEB007    // magic force bootloader value

#define TIMER_PERIOD (uint32_t)500   //mS
#define BOOTLOADER_PRIME_WINDOW     (uint32_t)5000   //mSeconds duration where user may press button to enter bootloader
#define BOOTLOADER_FORCE_DURATION   (uint32_t)1000   //mSeconds required for user to press button to enter bootloader
#define BOOTLOADER_POWER_OFF_DURATION   (uint32_t)5000/TIMER_PERIOD   //mSeconds delay for user to press button to power off board  
#define BOOTLOADER_BLINK_DURATION   (uint32_t)500   //mSeconds

APP_DATA appData;
static SYS_TMR_HANDLE m_TimerCallback;

bool forceBootloader = false;
bool bootloaderConnected = false;
bool bootloaderDownloading = false;

extern BOOTLOADER_DATA bootloaderData __attribute__((coherent, aligned(16)));
volatile uint32_t force_bootloader_flag __attribute__((persistent, coherent, address(FORCE_BOOTLOADER_FLAG_ADDR)));
volatile uint32_t g_timerCount = 0;

// *****************************************************************************
// *****************************************************************************
// Section: Application Local Functions
// *****************************************************************************
// *****************************************************************************
void DelayMs(uint32_t msDelay);

// *****************************************************************************
// *****************************************************************************
// Section: Application Callback Functions
// *****************************************************************************
// *****************************************************************************
/******************************************************************************
  Function:
    static void APP_Bootloader_ForceEvent (void)
    
   Remarks:
    Sets a trigger to be passed to force bootloader callback.
    Run bootloader if switch_1 is pressed OR
    if memory location == '0xFFFFFFFF' otherwise jump to user 
    application.
*/ 
int APP_Bootloader_ForceEvent(void)
{
    if (force_bootloader_flag == FORCE_BOOTLOADER_FLAG_VALUE) forceBootloader = true;
    // Reset flag as we accomplished our task of entering the bootloader
    force_bootloader_flag = 0;

    /* Check the trigger memory location and return true/false. */
    if (*(uint32_t *)APP_RESET_ADDRESS == 0xFFFFFFFF) forceBootloader = true;
    
    DelayMs(2000);  // If device was previously connected immediately before booting, delay a couple seconds for the host to properly reenumerate the USB HID bootloader device
    
    return (forceBootloader);
}

/**
 * Called to set processor status prior to executing main application
 */
int APP_StartApp(void)
{
    // Jump to main program - but first, clean up some things.
    
    /* Disable Global Interrupts */
    PLIB_INT_Disable(INT_ID_0);
    
    // Turn off LEDs
    PLIB_PORTS_PinClear(0, LED_WHITE_PORT, LED_WHITE_PIN);
    PLIB_PORTS_PinClear(0, LED_BLUE_PORT, LED_BLUE_PIN);
    
    /* Disable Individual Interrupts */
    IEC0=0;
    IEC1=0;
    IEC2=0;
    IEC3=0;
    IEC4=0;
    IEC5=0;
    IEC6=0;
    
    IFS0=0;
    IFS1=0;
    IFS2=0;
    IFS3=0;
    IFS4=0;
    IFS5=0;
    IFS6=0;
    
    IPC0=0;
    IPC1=0;
    IPC2=0;
    IPC3=0;
    IPC4=0;
    IPC5=0;
    IPC6=0;
    
    /* Disable Timers */
    T1CONbits.ON=0;
    T2CONbits.ON=0;
    T3CONbits.ON=0;
    T4CONbits.ON=0;
    T5CONbits.ON=0;
    T6CONbits.ON=0;
    T7CONbits.ON=0;
    T8CONbits.ON=0;
    T9CONbits.ON=0;
    
    PLIB_DMA_Disable(DMA_ID_0);
    
    SYS_DEVCON_CacheFlush(); // Flush L1 data cache
    
    DATASTREAM_Close();
    
    USB_DEVICE_Deinitialize(sysObj.usbDevObject0);
    //while(!USB_DEVICE_Status(sysObj.usbDevObject0)!=0);
    SYS_TMR_Deinitialize(sysObj.sysTmr);
    //while(!SYS_TMR_Status (sysObj.sysTmr)==0);
    DRV_TMR_Deinitialize(sysObj.drvTmr0);
    //while(!DRV_TMR_Status (sysObj.drvTmr0)==0);
    
    SYS_INT_VectorPrioritySet(INT_VECTOR_T2, INT_PRIORITY_LEVEL1);
    SYS_INT_VectorSubprioritySet(INT_VECTOR_T2, INT_SUBPRIORITY_LEVEL0);
    SYS_INT_VectorPrioritySet(INT_VECTOR_USB1, INT_PRIORITY_LEVEL1);

    /* Set Sub-priority of USB interrupt source */
    SYS_INT_VectorSubprioritySet(INT_VECTOR_USB1, INT_SUBPRIORITY_LEVEL0);

    /* Set the priority of the USB DMA Interrupt */
    SYS_INT_VectorPrioritySet(INT_VECTOR_USB1_DMA, INT_PRIORITY_LEVEL1);

    /* Set Sub-priority of the USB DMA Interrupt */
    SYS_INT_VectorSubprioritySet(INT_VECTOR_USB1_DMA, INT_SUBPRIORITY_LEVEL0);
    
    Nop();
    Nop();
    Nop();
    Nop();
    
    return(1);
}

// CP0 runs at SYS_CLK_FREQ/2.  So, to change that to 1kHz (1/1ms), divide by 1000
#define ms_SCALE (uint32_t)(SYS_CLK_FREQ/2/1000)

void DelayMs(uint32_t msDelay)
{
    uint32_t startCntms = _CP0_GET_COUNT();
    uint32_t waitCntms = msDelay * ms_SCALE;
    while( _CP0_GET_COUNT() - startCntms < waitCntms );
}

/**
 * Timer callback. Toggles the status led
 * @param context Unused
 */
void APP_TimerCallback(uintptr_t context, uint32_t currTick)
{
    UNUSED(context);
    UNUSED(currTick);
    
    g_timerCount++;       // Increment global timer count
    
    PLIB_PORTS_PinToggle(0, LED_WHITE_PORT, LED_WHITE_PIN);
    
    Nop();
    Nop();
    Nop();
    Nop();
    
    if (bootloaderDownloading)    // If we are downloading the bootloader, flash LED2
    {
        PLIB_PORTS_PinWrite(0, LED_BLUE_PORT, LED_BLUE_PIN, PLIB_PORTS_PinGet(0, LED_WHITE_PORT, LED_WHITE_PIN));
    }else if(bootloaderConnected) // If we are connected to the bootloader app, light LED2
    {
        PLIB_PORTS_PinSet(0, LED_BLUE_PORT, LED_BLUE_PIN);
    } 
}

// *****************************************************************************
// *****************************************************************************
// Section: Application Initialization and State Machine Functions
// *****************************************************************************
// *****************************************************************************

/*******************************************************************************
  Function:
    void APP_Initialize ( void )

  Remarks:
    See prototype in app.h.
 */

void APP_Initialize ( void )
{
          
    RESET_REASON reasonType;
    
    /* Place the App state machine in its initial state. */
    appData.state = APP_STATE_INIT;

    // Register the bootloader callbacks
    BOOTLOADER_ForceBootloadRegister(APP_Bootloader_ForceEvent);
    BOOTLOADER_StartAppRegister(APP_StartApp);
    
    // User button
    PLIB_PORTS_PinDirectionInputSet(0, BUTTON_PORT, BUTTON_PIN);
    
    // Status LED1
    PLIB_PORTS_PinDirectionOutputSet(0, LED_WHITE_PORT, LED_WHITE_PIN);
    PLIB_PORTS_PinClear(0, LED_WHITE_PORT, LED_WHITE_PIN);
    
    // Status LED2
    PLIB_PORTS_PinDirectionOutputSet(0, LED_BLUE_PORT, LED_BLUE_PIN);
    PLIB_PORTS_PinClear(0, LED_BLUE_PORT, LED_BLUE_PIN);
    
    m_TimerCallback = SYS_TMR_HANDLE_INVALID;
       
    /* Delay to allow the power to stabilize */
    DelayMs(10);
        
    reasonType = SYS_RESET_ReasonGet();
    if(reasonType & RESET_REASON_MCLR)
    {
        // If the processor has been reset from the MCLR pin, turn on LEDs to indicate a reset has occurred 
        // and check to see if we should enter bootloader
        PLIB_PORTS_PinSet(0, LED_WHITE_PORT, LED_WHITE_PIN);
        PLIB_PORTS_PinSet(0, LED_BLUE_PORT, LED_BLUE_PIN);

        DelayMs(BOOTLOADER_PRIME_WINDOW);
        
        // If the user is still holding the button, stay in while and count loop cycles
        if(PLIB_PORTS_PinGet(0, BUTTON_PORT, BUTTON_PIN))
        {
            DelayMs(BOOTLOADER_FORCE_DURATION);
            // Once we've timed out the BOOTLOADER_FORCE_DURATION and the user is still holding the button
            while(PLIB_PORTS_PinGet(0, BUTTON_PORT, BUTTON_PIN))
            {   
                forceBootloader = true; 
                PLIB_PORTS_PinToggle(0, LED_WHITE_PORT, LED_WHITE_PIN);
                PLIB_PORTS_PinToggle(0, LED_BLUE_PORT, LED_BLUE_PIN);
                DelayMs(BOOTLOADER_BLINK_DURATION);
            }
        }
        
        // Clear the reset reason status flag
        SYS_RESET_ReasonClear(RESET_REASON_MCLR);
    }
    
}


/******************************************************************************
  Function:
    void APP_Tasks ( void )

  Remarks:
    See prototype in app.h.
 */

void APP_Tasks ( void )
{
    static uint32_t timerPowerTriggered = false;
    
    /* Check the application's current state. */
    switch ( appData.state )
    {
        /* Application's initial state. */
        case APP_STATE_INIT:
        {
            bool appInitialized = true;
            m_TimerCallback = SYS_TMR_CallbackPeriodic((uint32_t)500, 0, APP_TimerCallback);
        
            if (appInitialized)
            {
                appData.state = APP_STATE_SERVICE_TASKS;
            }
            break;
        }

        case APP_STATE_SERVICE_TASKS:
        {
            if(bootloaderData.currentState==BOOTLOADER_PROCESS_COMMAND || bootloaderData.currentState==BOOTLOADER_SEND_RESPONSE)
            {
                bootloaderConnected = true;
            }

            // This test was hopefully going to signal via LED when the bootloader was downloading.  However, this doesn't appear to be possible at this Harmony release.
            if(bootloaderData.DataTransferred)
            {
                bootloaderDownloading = true;
            }
            
            if(forceBootloader && PLIB_PORTS_PinGet(0, BUTTON_PORT, BUTTON_PIN)) // If we are in the bootloader and the user is holding the button
            {
                static uint32_t timerPowerStartCount = 0;

                if (!timerPowerTriggered) timerPowerStartCount = g_timerCount;   // Set the start count of the timer if not already started
                timerPowerTriggered = true;

                if((g_timerCount-timerPowerStartCount)>BOOTLOADER_POWER_OFF_DURATION)
                {
                    PLIB_PORTS_PinClear(0, PWR_3_3V_EN_PORT, PWR_3_3V_EN_PIN); // Disable the 3.3V power rail
                    PLIB_PORTS_PinClear(0, LED_WHITE_PORT, LED_WHITE_PIN);    // Turn off LED1
                    PLIB_PORTS_PinClear(0, LED_BLUE_PORT, LED_BLUE_PIN); // Turn off LED2

                    // Delay for about 1s to allow the 3.3V rail to power off (unless plugged in then we will timeout and hit the reset below)
                    _CP0_SET_COUNT(0);
                    while (_CP0_GET_COUNT() < SYS_CLK_FREQ / 2);

                    SYS_RESET_SoftwareReset();  // If the board is plugged in, just reset
                }
            }else
            {
                timerPowerTriggered = false;  // User let go of the button, so reset the trigger
            }
            break;
        }

        /* TODO: implement your application state machine.*/
        

        /* The default state should never be executed. */
        default:
        {
            /* TODO: Handle error in application's state machine. */
            break;
        }
    }
}

 

/*******************************************************************************
 End of File
 */
