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
#include "HAL/ADC.h"
#include "HAL/DIO.h"

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
void _ADC_Deferred_Interrupt_Task( void );
void _Streaming_Deferred_Interrupt_Task( void );

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
//! ADC Interrupt Task Handle
static TaskHandle_t ADCInterruptHandle;
//! Streaming Interrupt Task Handle
static TaskHandle_t streamingInterruptHandle;


static void Streaming_TriggerADC(AInModule* module)
{
    if (module->Type == AIn_MC12bADC)
    {
        
    }
    
    ADC_TriggerConversion(module);
}

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
//    xTaskCreate((TaskFunction_t) _DRV_SDCARD_Tasks,
//                "DRV_SDCARD Tasks",
//                1024, NULL, 2, NULL);


 
    /* Create task for gfx state machine*/
    /* Create OS Thread for USB Tasks. */
    xTaskCreate((TaskFunction_t) _USB_Tasks,
                "USB Tasks",
                2048, NULL, 4, &usbHandle);


    /* Create task for TCPIP state machine*/
    /* Create OS Thread for TCPIP Tasks. */
    xTaskCreate((TaskFunction_t) _TCPIP_Tasks,
                "TCPIP Tasks",
                4096, NULL, 9, &tcpipHandle);

    /* Create OS Thread for Net Pres Tasks. */
    xTaskCreate((TaskFunction_t) _NET_PRES_Tasks,
                "Net Pres Tasks",
                1024, NULL, 9, &netpHandle);

    /* Create OS Thread for APP Tasks. */
    xTaskCreate((TaskFunction_t) _APP_Tasks,
                "APP Tasks",
                2048, NULL, 9, &appHandle);
    
    /* Create OS Thread for power Tasks. */
    xTaskCreate((TaskFunction_t) _POWER_AND_UI_Tasks,
                "POWER Tasks",
                1024, NULL, 9, &powerUIHandle);   
    
    xTaskCreate((TaskFunction_t) _ADC_Deferred_Interrupt_Task,
                "ADC Interrupt",
                1024, NULL, 9, &ADCInterruptHandle);   
    
    xTaskCreate((TaskFunction_t) _Streaming_Deferred_Interrupt_Task,
                "Stream Interrupt",
                2048, NULL, 4, &streamingInterruptHandle);   
      
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
    
    (void)uxHighWaterMark0;
    (void)uxHighWaterMark1;
    (void)uxHighWaterMark2;
    (void)uxHighWaterMark3;
    (void)uxHighWaterMark4;
    (void)uxHighWaterMark5;
    
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
        Power_Tasks( );
        Button_Tasks(g_BoardConfig.UIConfig, &g_BoardData.UIReadVars, &g_BoardData.PowerData);
        LED_Tasks(g_BoardConfig.UIConfig, &g_BoardData.PowerData, &g_BoardData.UIReadVars, g_BoardRuntimeConfig.StreamingConfig.IsEnabled);
        vTaskDelay(125 / portTICK_PERIOD_MS);
    }
}

/*! Task for ADC deferred interrupt*/
void _ADC_Deferred_Interrupt_Task( void ){
    const TickType_t xBlockTime = portMAX_DELAY;
    
    while( 1 ){
        ulTaskNotifyTake( pdFALSE,
                                            xBlockTime );
        const AInModule* module = ADC_FindModule( AIn_MC12bADC );
        ADC_ConversionComplete(module);
    }
}

/*! Function to be called from the ISR for deferring the ADC interrupt */
void _ADC_Defer_Interrupt( void ){
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR( ADCInterruptHandle, &xHigherPriorityTaskWoken );
    portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
}

void _Streaming_Deferred_Interrupt_Task( void ){
    
    uint8_t i=0;
    const TickType_t xBlockTime = portMAX_DELAY;

    while( 1 ){
        ulTaskNotifyTake( pdFALSE, xBlockTime );
        
        for (i=0; i < g_BoardRuntimeConfig.AInModules.Size; ++i)
        {
            // Only trigger conversions if the previous conversion is complete
            if (g_BoardData.AInState.Data[i].AInTaskState == AINTASK_IDLE &&
                g_BoardRuntimeConfig.StreamingConfig.StreamCount == g_BoardRuntimeConfig.StreamingConfig.StreamCountTrigger) // TODO: Replace with ADCPrescale[i]
            {
                Streaming_TriggerADC(&g_BoardConfig.AInModules.Data[i]);
            }

        }

        if (g_BoardRuntimeConfig.StreamingConfig.StreamCount == g_BoardRuntimeConfig.StreamingConfig.StreamCountTrigger) // TODO: Replace with DIOPrescale
        {
            DIO_Tasks( &g_BoardData.DIOLatest, &g_BoardData.DIOSamples );
        }

        g_BoardRuntimeConfig.StreamingConfig.StreamCount = (g_BoardRuntimeConfig.StreamingConfig.StreamCount + 1) % g_BoardRuntimeConfig.StreamingConfig.MaxStreamCount;
    }
}

void Streaming_Defer_Interrupt( void ){
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR( streamingInterruptHandle, &xHigherPriorityTaskWoken );
    portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
    
}
/*******************************************************************************
 End of File
 */
