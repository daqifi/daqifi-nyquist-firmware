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
#include "HAL/ADC/AD7609.h"
#include "Util/Logger.h"


// *****************************************************************************
// *****************************************************************************
// Section: RTOS "Tasks" Routine
// *****************************************************************************
// *****************************************************************************
// WARNING: Task function disabled - DO NOT restore during MCC regeneration
// This function causes SPI mutex conflicts with app_SdCardTask
//static void lDRV_SDSPI_0_Tasks(  void *pvParameters  )
//{
//    while(true)
//    {
//        DRV_SDSPI_Tasks(sysObj.drvSDSPI0);
//        vTaskDelay(10U / portTICK_PERIOD_MS);
//    }
//}

static void F_USB_DEVICE_Tasks(  void *pvParameters  )
{
    while(true)
    {
        /* USB Device layer tasks routine */
        USB_DEVICE_Tasks(sysObj.usbDevObject0);
        vTaskDelay(1);  // Block to ensure other priority 7 tasks can run
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

static void lWDRV_WINC_Tasks(void *pvParameters)
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


// WARNING: Task function disabled - DO NOT restore during MCC regeneration  
// This function causes SPI mutex conflicts with app_SdCardTask
//static void lSYS_FS_Tasks(  void *pvParameters  )
//{
//    while(true)
//    {
//        SYS_FS_Tasks();
//        vTaskDelay(10U / portTICK_PERIOD_MS);
//    }
//}





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
    
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // WARNING: DO NOT RE-ENABLE - MCC will try to restore this during regeneration
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // IMPORTANT: SYS_FS_TASKS disabled to prevent SPI mutex conflicts
    // These tasks were accidentally restored by Harmony Configurator update but cause
    // cross-task FreeRTOS mutex violations when multiple tasks try to unlock SPI mutexes
    // that were locked by different tasks. Originally disabled in commit 86d25f91.
    // SD functionality is handled by app_SdCardTask instead.
    // REJECT MCC MERGE ATTEMPTS TO RE-ENABLE THIS TASK!
//    (void) xTaskCreate( lSYS_FS_Tasks,
//        "SYS_FS_TASKS",
//        SYS_FS_STACK_SIZE,
//        (void*)NULL,
//        SYS_FS_PRIORITY ,
//        (TaskHandle_t*)NULL
//    );



    /* Maintain Device Drivers */
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // WARNING: DO NOT RE-ENABLE - MCC will try to restore this during regeneration  
    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // IMPORTANT: DRV_SD_0_TASKS disabled to prevent SPI mutex conflicts  
    // This Harmony internal task competes with app_SdCardTask for SPI bus access,
    // causing FreeRTOS assert failures: configASSERT( pxTCB == pxCurrentTCB )
    // when different tasks try to unlock mutexes. Originally disabled in commit 86d25f91.
    // REJECT MCC MERGE ATTEMPTS TO RE-ENABLE THIS TASK!
//        (void) xTaskCreate( lDRV_SDSPI_0_Tasks,
//        "DRV_SD_0_TASKS",
//        DRV_SDSPI_STACK_SIZE_IDX0,
//        (void*)NULL,
//        DRV_SDSPI_PRIORITY_IDX0 ,
//        (TaskHandle_t*)NULL
//    );

    BaseType_t wifiResult = xTaskCreate( lWDRV_WINC_Tasks,
        "WDRV_WINC_Tasks",
        DRV_WIFI_WINC_RTOS_STACK_SIZE,
        (void*)NULL,
        DRV_WIFI_WINC_RTOS_TASK_PRIORITY,
        (TaskHandle_t*)NULL
    );
    if (wifiResult != pdPASS) {
        LOG_E("FATAL: Failed to create WDRV_WINC_Tasks (%d bytes)\r\n", DRV_WIFI_WINC_RTOS_STACK_SIZE);
    }

    /* Create AD7609 deferred interrupt task for BSY pin handling */
    volatile TaskHandle_t* pAD7609TaskHandle = (volatile TaskHandle_t*)AD7609_GetTaskHandle();
    BaseType_t ad7609Result = xTaskCreate(
        (TaskFunction_t) AD7609_DeferredInterruptTask,
        "AD7609 BSY",
        512,
        NULL,
        8,  // Priority 8 (max-1) for real-time response
        (TaskHandle_t*)pAD7609TaskHandle  // Cast away volatile for API compatibility
    );

    if (ad7609Result != pdPASS) {
        // Task creation failed - system will continue but AD7609 interrupts won't work
        LOG_E("Failed to create AD7609_DeferredInterruptTask (512 bytes)\r\n");
        *pAD7609TaskHandle = NULL;
    } else if (*pAD7609TaskHandle == NULL) {
        // Task handle wasn't populated - this should never happen but guard against it
        LOG_E("AD7609 task created but handle is NULL - interrupt notifications will fail\r\n");
    }




    /* Maintain Middleware & Other Libraries */
        /* Create OS Thread for USB_DEVICE_Tasks. */
    BaseType_t usbDeviceResult = xTaskCreate( F_USB_DEVICE_Tasks,
        "USB_DEVICE_TASKS",
        1024,
        (void*)NULL,
        7,
        (TaskHandle_t*)NULL
    );
    if (usbDeviceResult != pdPASS) {
        LOG_E("FATAL: Failed to create F_USB_DEVICE_Tasks (1024 bytes)\r\n");
    }

    /* Create OS Thread for USB Driver Tasks. */
    BaseType_t usbDriverResult = xTaskCreate( F_DRV_USBHS_Tasks,
        "DRV_USBHS_TASKS",
        1024,
        (void*)NULL,
        7,
        (TaskHandle_t*)NULL
    );
    if (usbDriverResult != pdPASS) {
        LOG_E("FATAL: Failed to create F_DRV_USBHS_Tasks (1024 bytes)\r\n");
    }



    /* Maintain the application's state machine. */

        /* Create OS Thread for APP_FREERTOS_Tasks. */
    BaseType_t appResult = xTaskCreate(
           (TaskFunction_t) lAPP_FREERTOS_Tasks,
                "APP_FREERTOS_Tasks",
                1500,
                NULL,
           1U ,
                &xAPP_FREERTOS_Tasks);
    if (appResult != pdPASS) {
        LOG_E("FATAL: Failed to create lAPP_FREERTOS_Tasks (1500 bytes)\r\n");
    }



    /* Start RTOS Scheduler. */
    
     /**********************************************************************
     * Create all Threads for APP Tasks before starting FreeRTOS Scheduler *
     ***********************************************************************/
    vTaskStartScheduler(); /* This function never returns. */

}

/*******************************************************************************
 End of File
 */

