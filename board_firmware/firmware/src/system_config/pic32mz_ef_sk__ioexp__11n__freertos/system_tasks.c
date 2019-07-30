/*******************************************************************************
 System Tasks File

  File Name:
    system_tasks.c

  Summary:
    This file contains source code necessary to maintain system's polled state
    machines.

  Description:
    This file contains source code necessary to maintain system's polled state
    machines.  It implements the "SYS_Tasks" function that calls the individual
    "Tasks" functions for all the MPLAB Harmony modules in the system.

  Remarks:
    This file requires access to the systemObjects global data structure that
    contains the object handles to all MPLAB Harmony module objects executing
    polled in the system.  These handles are passed into the individual module
    "Tasks" functions to identify the instance of the module to maintain.
 *******************************************************************************/

// DOM-IGNORE-BEGIN
/*******************************************************************************
Copyright (c) 2013-2015 released Microchip Technology Inc.  All rights reserved.

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

#include "system_config.h"
#include "system_definitions.h"
#include "app.h"

#include "../src/HAL/UI/UI.h"
#include "../src/HAL/Power/PowerApi.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "state/data/BoardData.h"
#include "state/board/BoardConfig.h"
#include "UsbCdc/UsbCdc.h"
#include "HAL/Wifi/WifiApi.h"

// *****************************************************************************
// *****************************************************************************
// Section: Local Prototypes
// *****************************************************************************
// *****************************************************************************


 
static void _SYS_Tasks ( void );
 
 
void _DRV_SDCARD_Tasks(void);

void _USB_Tasks(void);
void _TCPIP_Tasks(void);
void _NET_PRES_Tasks(void);
static void _APP_Tasks(void);
void _POWER_AND_UI_Tasks(void);


// *****************************************************************************
// *****************************************************************************
// Section: System "Tasks" Routine
// *****************************************************************************
// *****************************************************************************

static TaskHandle_t sysHandle;
static TaskHandle_t usbHandle;
static TaskHandle_t tcpipHandle;
static TaskHandle_t appHandle;
static TaskHandle_t netpHandle;
static TaskHandle_t powerUIHandle;
/*******************************************************************************
  Function:
    void SYS_Tasks ( void )

  Remarks:
    See prototype in system/common/sys_module.h.
*/

void SYS_Tasks ( void )
{
    /* Create OS Thread for Sys Tasks. */
    xTaskCreate((TaskFunction_t) _SYS_Tasks,
                "Sys Tasks",
                2048, NULL, 3, &sysHandle);


 
 
    /* Create task for gfx state machine*/
    /* Create OS Thread for DRV_SDCARD Tasks. */
    xTaskCreate((TaskFunction_t) _DRV_SDCARD_Tasks,
                "DRV_SDCARD Tasks",
                1024, NULL, 2, NULL);


 
    /* Create task for gfx state machine*/
    /* Create OS Thread for USB Tasks. */
    xTaskCreate((TaskFunction_t) _USB_Tasks,
                "USB Tasks",
                2048, NULL, 4, &usbHandle);


    /* Create task for TCPIP state machine*/
    /* Create OS Thread for TCPIP Tasks. */
    xTaskCreate((TaskFunction_t) _TCPIP_Tasks,
                "TCPIP Tasks",
                2048, NULL, 1, &tcpipHandle);

    /* Create OS Thread for Net Pres Tasks. */
    xTaskCreate((TaskFunction_t) _NET_PRES_Tasks,
                "Net Pres Tasks",
                1024, NULL, 2, &netpHandle);

    /* Create OS Thread for APP Tasks. */
    xTaskCreate((TaskFunction_t) _APP_Tasks,
                "APP Tasks",
                2048, NULL, 2, &appHandle);
    
    /* Create OS Thread for power Tasks. */
    xTaskCreate((TaskFunction_t) _POWER_AND_UI_Tasks,
                "POWER Tasks",
                1024, NULL, 9, &powerUIHandle);    
    
    /**************
     * Start RTOS * 
     **************/
    vTaskStartScheduler(); /* This function never returns. */
}


/*******************************************************************************
  Function:
    void _SYS_Tasks ( void )

  Summary:
    Maintains state machines of system modules.
*/
static void _SYS_Tasks ( void)
{
    volatile UBaseType_t uxHighWaterMark0 = 0;
    volatile UBaseType_t uxHighWaterMark1 = 0;
    volatile UBaseType_t uxHighWaterMark2 = 0;
    volatile UBaseType_t uxHighWaterMark3 = 0;
    volatile UBaseType_t uxHighWaterMark4 = 0;
    volatile UBaseType_t uxHighWaterMark5 = 0;
    
//    UNUSED(uxHighWaterMark0);
//    UNUSED(uxHighWaterMark1);
//    UNUSED(uxHighWaterMark2);
//    UNUSED(uxHighWaterMark3);
//    UNUSED(uxHighWaterMark4);
//    UNUSED(uxHighWaterMark5);
    
    while(1)
    {
        /* Maintain system services */
        SYS_RTCC_Tasks(sysObj.sysRtcc);
        SYS_DEVCON_Tasks(sysObj.sysDevcon);
        /* Maintain the file system state machine. */
        SYS_FS_Tasks();
        SYS_CONSOLE_Tasks(sysObj.sysConsole0);
        /* SYS_TMR Device layer tasks routine */ 
        SYS_TMR_Tasks(sysObj.sysTmr);

        /* Maintain Device Drivers */
 
 

        /* Maintain Middleware */
 
        uxHighWaterMark0 = uxTaskGetStackHighWaterMark(sysHandle);
        uxHighWaterMark1 = uxTaskGetStackHighWaterMark(usbHandle);
        uxHighWaterMark2 = uxTaskGetStackHighWaterMark(tcpipHandle);
        uxHighWaterMark3 = uxTaskGetStackHighWaterMark(netpHandle);
        uxHighWaterMark4 = uxTaskGetStackHighWaterMark(appHandle);
        uxHighWaterMark5 = uxTaskGetStackHighWaterMark(powerUIHandle);

        /* Task Delay */
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

 
 
void _DRV_SDCARD_Tasks(void)
{
    while(1)
    {
        // DRV_SDCARD_Tasks(sysObj.drvSDCard);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}

void _USB_Tasks(void)
{
    UsbCdc_Initialize();
    while(1)
    {
        /* USBHS Driver Task Routine */ 
        DRV_USBHS_Tasks(sysObj.drvUSBObject);
         
        
        /* USB Device layer tasks routine */ 
        USB_DEVICE_Tasks(sysObj.usbDevObject0);
        
        UsbCdc_ProcessState();
        
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
 }
void _TCPIP_Tasks(void)
{
    while(1)
    {
        /* Maintain the TCP/IP Stack*/
        TCPIP_STACK_Task(sysObj.tcpip);
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}
void _NET_PRES_Tasks(void)
{
    while(1)
    {
        /* Maintain the TCP/IP Stack*/
        NET_PRES_Tasks(sysObj.netPres);
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}

/*******************************************************************************
  Function:
    void _APP_Tasks ( void )

  Summary:
    Maintains state machine of APP.
*/

static void _APP_Tasks(void)
{
    portTASK_USES_FLOATING_POINT();
    WifiInit(&(g_BoardRuntimeConfig.wifiSettings.settings.wifi));
    while(1)
    {
        APP_Tasks();
        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}

/*******************************************************************************
  Function:
    void _POWER_Tasks ( void )

  Summary:
    Maintains state machine of Power API.
*/

void _POWER_AND_UI_Tasks(void)
{
    portTASK_USES_FLOATING_POINT();
    while(1)
    {
        Power_Tasks(g_BoardConfig.PowerConfig, &g_BoardData.PowerData, &g_BoardRuntimeConfig.PowerWriteVars);
        Button_Tasks(g_BoardConfig.UIConfig, &g_BoardData.UIReadVars, &g_BoardData.PowerData);
        if(g_BoardRuntimeConfig.UIWriteVars.enableLEDs) LED_Tasks(g_BoardConfig.UIConfig, &g_BoardData.PowerData, &g_BoardData.UIReadVars, g_BoardRuntimeConfig.StreamingConfig.IsEnabled);
        vTaskDelay(125 / portTICK_PERIOD_MS);
    }
}

/*******************************************************************************
 End of File
 */
