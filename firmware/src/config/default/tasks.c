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
#include "services/streaming.h"  // #331: Streaming_IsActiveOnNonWifiInterface
#include "services/wifi_services/wifi_tcp_server.h"  // #331: HasActiveClient
#include "config/default/WincIdleGate.h"                // #55: public idle-gate API


// *****************************************************************************
// *****************************************************************************
// Section: RTOS "Tasks" Routine
// *****************************************************************************
// *****************************************************************************
// WARNING: Task function disabled - DO NOT restore during MCC regeneration
// This function causes SPI mutex conflicts with app_SDCardTask
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
    // Boost priority after startup
    portYIELD();  // Let other tasks initialize first
    vTaskPrioritySet(NULL, 6);

    while(true)
    {
        /* USB Device layer tasks routine */
        USB_DEVICE_Tasks(sysObj.usbDevObject0);
        vTaskDelay(1);  // Block to ensure app_USBDeviceTask can run
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

// #331 WINC idle-gate — pace the driver's hot loop ONLY when we're
// certain WiFi isn't needed. Pre-#331 was an unbounded tight loop that
// preempted the streaming timer ISR every 45.9 ms for ~270 µs (22 Hz,
// CV 10.6% jitter at 10+ kHz). Bisection confirmed this task as the
// sole source of that signature. See #331 for data.
//
// CAUTION: Adding ANY delay to the fast path breaks USB CDC and WiFi
// responsiveness when WiFi is connecting / associated / transferring.
// The WINC driver's state machine relies on being called back-to-back
// during event-heavy phases. A 1 ms vTaskDelay between calls caused
// the firmware to assert + reboot into bootloader during STA mode
// throughput testing.
//
// Tier policy:
//   - ERROR / UNINITIALIZED → 50 ms (existing recovery behavior)
//   - READY + streaming on non-WiFi interface → 50 ms
//     (WiFi data path definitely not in use — safe to pace)
//   - Otherwise → 0 ms (preserve original unbounded loop behavior)
//
// The gate still eliminates the 22 Hz preemption for the common case
// (USB streaming with WiFi idle-AP), which was the #331 target.
// Further CPU reduction when WiFi is fully idle requires #334 (chip
// power-down) or a deeper driver rework that understands event-queue
// emptiness.
uint32_t WincIdleGate_ComputeDelay(SYS_STATUS status)
{
    if ((SYS_STATUS_ERROR == status) || (SYS_STATUS_UNINITIALIZED == status)) {
        return 50U;
    }
    if (SYS_STATUS_READY != status) {
        // BUSY / initialization phases rely on tight polling to advance
        // their internal state machines — don't pace.
        return 0U;
    }
    // READY. Pace only when:
    //   (a) streaming is using a non-WiFi interface (USB/SD/All), AND
    //   (b) no TCP client is connected to the control plane.
    // Either condition alone disqualifies: an active TCP client needs
    // tight polling even when streaming is on USB, otherwise SCPI
    // responses lag and the socket's receive queue can overflow.
    if (Streaming_IsActiveOnNonWifiInterface() &&
        !wifi_tcp_server_HasActiveClient()) {
        return 50U;
    }
    return 0U;
}

// Debug accessor for SCPI verification of #55: reports the live gate state
// so tests can observe the tier transitions across SYS_STATUS changes,
// streaming start/stop, and TCP client connect/disconnect.
void WincIdleGate_GetDebugState(int* out_status,
                                 bool* out_streaming_non_wifi,
                                 bool* out_tcp_client,
                                 uint32_t* out_delay_ms)
{
    SYS_STATUS s = WDRV_WINC_Status(sysObj.drvWifiWinc);
    if (out_status != NULL) *out_status = (int)s;
    if (out_streaming_non_wifi != NULL) *out_streaming_non_wifi = Streaming_IsActiveOnNonWifiInterface();
    if (out_tcp_client != NULL) *out_tcp_client = wifi_tcp_server_HasActiveClient();
    if (out_delay_ms != NULL) *out_delay_ms = WincIdleGate_ComputeDelay(s);
}

static void lWDRV_WINC_Tasks(void *pvParameters)
{
    while(1)
    {
        WDRV_WINC_Tasks(sysObj.drvWifiWinc);

        SYS_STATUS status = WDRV_WINC_Status(sysObj.drvWifiWinc);
        uint32_t delay_ms = WincIdleGate_ComputeDelay(status);
        if (delay_ms > 0U) {
            vTaskDelay(pdMS_TO_TICKS(delay_ms));
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
// This function causes SPI mutex conflicts with app_SDCardTask
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
    // SD functionality is handled by app_SDCardTask instead.
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
    // This Harmony internal task competes with app_SDCardTask for SPI bus access,
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

    // Stack sizes profiled under stress (issue #230). Harmony driver tasks kept
    // conservative — driver internals have unknown stack depth.
    BaseType_t wifiResult = xTaskCreate( lWDRV_WINC_Tasks,
        "WDRV_WINC_Tasks",
        1024,   // Profiled: 290 words peak. 3x+ margin for unknown driver depth. (was 3000)
        (void*)NULL,
        DRV_WIFI_WINC_RTOS_TASK_PRIORITY,
        (TaskHandle_t*)NULL
    );
    if (wifiResult != pdPASS) {
        LOG_E("FATAL: Failed to create WDRV_WINC_Tasks\r\n");
    }

    /* Create AD7609 deferred interrupt task for BSY pin handling */
    volatile TaskHandle_t* pAD7609TaskHandle = (volatile TaskHandle_t*)AD7609_GetTaskHandle();
    BaseType_t ad7609Result = xTaskCreate(
        (TaskFunction_t) AD7609_DeferredInterruptTask,
        "AD7609 BSY",
        160,   // Profiled: 76 words peak. 2x margin. (was 512)
        NULL,
        9,
        (TaskHandle_t*)pAD7609TaskHandle
    );

    if (ad7609Result != pdPASS) {
        LOG_E("Failed to create AD7609_DeferredInterruptTask\r\n");
        *pAD7609TaskHandle = NULL;
    } else if (*pAD7609TaskHandle == NULL) {
        LOG_E("AD7609 task created but handle is NULL\r\n");
    }

    /* Maintain Middleware & Other Libraries */
    BaseType_t usbDeviceResult = xTaskCreate( F_USB_DEVICE_Tasks,
        "USB_DEVICE_TASKS",
        144,   // Profiled: 72 words peak. 2x margin. (was 1024)
        (void*)NULL,
        2,
        (TaskHandle_t*)NULL
    );
    if (usbDeviceResult != pdPASS) {
        LOG_E("FATAL: Failed to create F_USB_DEVICE_Tasks\r\n");
    }

    BaseType_t usbDriverResult = xTaskCreate( F_DRV_USBHS_Tasks,
        "DRV_USBHS_TASKS",
        144,   // Profiled: 72 words peak. 2x margin. (was 1024)
        (void*)NULL,
        2,
        (TaskHandle_t*)NULL
    );
    if (usbDriverResult != pdPASS) {
        LOG_E("FATAL: Failed to create F_DRV_USBHS_Tasks\r\n");
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

