/*******************************************************************************
 System Tasks File

  File Name:
    tasks.c

  Summary:
    This file contains source code necessary to maintain system's polled tasks.

  Description:
    This file contains source code necessary to maintain system's polled tasks.
    It implements the "SYS_Tasks" function that calls the individual "Tasks"
    functions for all polled MPLAB Harmony modules in the system.

  Remarks:
    This file requires access to the systemObjects global data structure that
    contains the object handles to all MPLAB Harmony module objects executing
    polled in the system.  These handles are passed into the individual module
    "Tasks" functions to identify the instance of the module to maintain.
 *******************************************************************************/

// DOM-IGNORE-BEGIN
/*******************************************************************************
* Copyright (C) 2018 Microchip Technology Inc. and its subsidiaries.
*
* Subject to your compliance with these terms, you may use Microchip software
* and any derivatives exclusively with Microchip products. It is your
* responsibility to comply with third party license terms applicable to your
* use of third party software (including open source software) that may
* accompany Microchip software.
*
* THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
* EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED
* WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A
* PARTICULAR PURPOSE.
*
* IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
* INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
* WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS
* BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO THE
* FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN
* ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
* THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
 *******************************************************************************/
// DOM-IGNORE-END

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************

#include "configuration.h"
#include "definitions.h"
#include "sys_tasks.h"


// *****************************************************************************
// *****************************************************************************
// Section: RTOS "Tasks" Routine
// *****************************************************************************
// *****************************************************************************

static void lDRV_SDSPI_0_Tasks(  void *pvParameters  )
{
    while(true)
    {
        DRV_SDSPI_Tasks(sysObj.drvSDSPI0);
        vTaskDelay(10U / portTICK_PERIOD_MS);
    }
}

static void F_USB_DEVICE_Tasks(  void *pvParameters  )
{
    while(true)
    {
                /* USB Device layer tasks routine */
        USB_DEVICE_Tasks(sysObj.usbDevObject0);
        vTaskDelay(1U / portTICK_PERIOD_MS);
    }
}

/* Handle for the APP_FREERTOS_Tasks. */
TaskHandle_t xAPP_FREERTOS_Tasks;

static void lAPP_FREERTOS_Tasks(  void *pvParameters  )
{   
    while(true)
    {
        APP_FREERTOS_Tasks();
    }
}

static void _WDRV_WINC_Tasks(void *pvParameters)
{
    while(1)
    {
        SYS_STATUS status;
       
        WDRV_WINC_Tasks(sysObj.drvWifiWinc);

        status = WDRV_WINC_Status(sysObj.drvWifiWinc);
      
        if ((SYS_STATUS_ERROR == status) || (SYS_STATUS_UNINITIALIZED == status))
        {
            vTaskDelay(50 / portTICK_PERIOD_MS);
        }
    }
}

static void F_DRV_USBHS_Tasks(  void *pvParameters  )
{
    while(true)
    {
                 /* USB FS Driver Task Routine */
        DRV_USBHS_Tasks(sysObj.drvUSBHSObject);
        vTaskDelay(10U / portTICK_PERIOD_MS);
    }
}


static void lSYS_FS_Tasks(  void *pvParameters  )
{
    while(true)
    {
        SYS_FS_Tasks();
        vTaskDelay(10U / portTICK_PERIOD_MS);
    }
}





// *****************************************************************************
// *****************************************************************************
// Section: System "Tasks" Routine
// *****************************************************************************
// *****************************************************************************

/*******************************************************************************
  Function:
    void SYS_Tasks ( void )

  Remarks:
    See prototype in system/common/sys_module.h.
*/
void SYS_Tasks ( void )
{
    /* Maintain system services */
    
    (void) xTaskCreate( lSYS_FS_Tasks,
        "SYS_FS_TASKS",
        SYS_FS_STACK_SIZE,
        (void*)NULL,
        SYS_FS_PRIORITY,
        (TaskHandle_t*)NULL
    );



    /* Maintain Device Drivers */
        (void) xTaskCreate( lDRV_SDSPI_0_Tasks,
        "DRV_SD_0_TASKS",
        DRV_SDSPI_STACK_SIZE_IDX0,
        (void*)NULL,
        DRV_SDSPI_PRIORITY_IDX0,
        (TaskHandle_t*)NULL
    );

    xTaskCreate( _WDRV_WINC_Tasks,
        "WDRV_WINC_Tasks",
        DRV_WIFI_WINC_RTOS_STACK_SIZE,
        (void*)NULL,
        DRV_WIFI_WINC_RTOS_TASK_PRIORITY,
        (TaskHandle_t*)NULL
    );




    /* Maintain Middleware & Other Libraries */
        /* Create OS Thread for USB_DEVICE_Tasks. */
    (void) xTaskCreate( F_USB_DEVICE_Tasks,
        "USB_DEVICE_TASKS",
        1024,
        (void*)NULL,
        6,
        (TaskHandle_t*)NULL
    );

    /* Create OS Thread for USB Driver Tasks. */
    (void) xTaskCreate( F_DRV_USBHS_Tasks,
        "DRV_USBHS_TASKS",
        1024,
        (void*)NULL,
        1,
        (TaskHandle_t*)NULL
    );



    /* Maintain the application's state machine. */
        /* Create OS Thread for APP_FREERTOS_Tasks. */
    (void) xTaskCreate((TaskFunction_t) lAPP_FREERTOS_Tasks,
                "APP_FREERTOS_Tasks",
                1500,
                NULL,
                1,
                &xAPP_FREERTOS_Tasks);




    /* Start RTOS Scheduler. */
    
     /**********************************************************************
     * Create all Threads for APP Tasks before starting FreeRTOS Scheduler *
     ***********************************************************************/
    vTaskStartScheduler(); /* This function never returns. */

}

/*******************************************************************************
 End of File
 */

