/*******************************************************************************
 System Tasks File

  File Name:
    freertos_hooks.c

  Summary:
    This file contains source code necessary for FreeRTOS hooks

  Description:

  Remarks:
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
#include "FreeRTOS.h"
#include "task.h"


void vApplicationIdleHook( void );
void vApplicationTickHook( void );
void vAssertCalled( const char * pcFile, unsigned long ulLine );

/* #552 crash capture — shared globals defined in exceptions.c. Written here so
 * the guard-caught (configCHECK_FOR_STACK_OVERFLOW == 2) overflow names its
 * culprit into fixed BSS (not the dying task's stack) for mdb / Logger dump. */
extern volatile uint32_t gCrashReason;
extern volatile uint32_t gCrashTaskHandle;
extern volatile char     gCrashTaskName[];

/*
*********************************************************************************************************
*                                          vApplicationStackOverflowHook()
*
* Description : Hook function called by FreeRTOS if a stack overflow happens.
*
* Argument(s) : none
*
* Return(s)   : none
*
* Caller(s)   : APP_StateReset()
*
* Note(s)     : none.
*********************************************************************************************************
*/
void vApplicationStackOverflowHook( TaskHandle_t xTask, char *pcTaskName )
{
   /* #552: record the culprit BEFORE disabling interrupts / spinning, into
    * fixed globals (not this task's overflowed stack) so it is diagnosable via
    * mdb (`print gCrashTaskName`) or a Logger dump without a live repro.
    * FreeRTOS hands us the name and handle directly here — no TCB deref needed,
    * and no vsnprintf/LOG_E (that printf frame is itself what overflows). */
   gCrashReason     = 1U;
   gCrashTaskHandle = (uint32_t) xTask;
   gCrashTaskName[0] = '\0';   /* fresh per-crash; a NULL name can't leave stale data */
   if ( pcTaskName != NULL )
   {
       unsigned int i;
       /* Task names are <= configMAX_TASK_NAME_LEN (16) incl. null; gCrashTaskName
        * is 20 bytes, so this never overruns. */
       for ( i = 0U; i < configMAX_TASK_NAME_LEN; i++ )
       {
           char c = pcTaskName[i];
           if ( c == '\0' ) { break; }
           /* Keep it printable — the name field may be garbage if the overflow
            * clobbered it (matches CrashCapture_CopyName in exceptions.c). */
           gCrashTaskName[i] = ( ( c >= 0x20 ) && ( c <= 0x7E ) ) ? c : '?';
       }
       gCrashTaskName[i] = '\0';
   }

   ( void ) pcTaskName;
   ( void ) xTask;

   /* Run time task stack overflow checking is performed if
   configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook  function is
   called if a task stack overflow is detected.  Note the system/interrupt
   stack is not checked. */
   taskDISABLE_INTERRUPTS();
   for( ;; )
   {
       /* Do Nothing */
   }
}

/*
*********************************************************************************************************
*                                     vApplicationMallocFailedHook()
*
* Description : vApplicationMallocFailedHook() will only be called if
*               configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h.
*               It is a hook function that will get called if a call to
*               pvPortMalloc() fails.  pvPortMalloc() is called internally by
*               the kernel whenever a task, queue, timer or semaphore is
*               created.  It is also called by various parts of the demo
*               application.  If heap_1.c or heap_2.c are used, then the size of
*               the heap available to pvPortMalloc() is defined by
*               configTOTAL_HEAP_SIZE in FreeRTOSConfig.h, and the
*               xPortGetFreeHeapSize() API function can be used to query the
*               size of free heap space that remains (although it does not
*               provide information on how the remaining heap might be
*               fragmented).
*
* Argument(s) : none
*
* Return(s)   : none
*
* Caller(s)   : APP_StateReset()
*
* Note(s)     : none.
*********************************************************************************************************
*/
void vApplicationMallocFailedHook( void )
{
   /* vApplicationMallocFailedHook() will only be called if
      configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h.  It is a hook
      function that will get called if a call to pvPortMalloc() fails.
      pvPortMalloc() is called internally by the kernel whenever a task, queue,
      timer or semaphore is created.  It is also called by various parts of the
      demo application.  If heap_1.c or heap_2.c are used, then the size of the
      heap available to pvPortMalloc() is defined by configTOTAL_HEAP_SIZE in
      FreeRTOSConfig.h, and the xPortGetFreeHeapSize() API function can be used
      to query the size of free heap space that remains (although it does not
      provide information on how the remaining heap might be fragmented). */

   taskDISABLE_INTERRUPTS();
   for( ;; )
   {
       /* Do Nothing */
   }
}
/*-----------------------------------------------------------*/

#if ( configSUPPORT_STATIC_ALLOCATION == 1 )

/* Required when configSUPPORT_STATIC_ALLOCATION == 1. FreeRTOS calls this to
 * obtain storage for the Idle task TCB + stack. */
void vApplicationGetIdleTaskMemory( StaticTask_t **ppxIdleTaskTCBBuffer,
                                    StackType_t **ppxIdleTaskStackBuffer,
                                    configSTACK_DEPTH_TYPE *puxIdleTaskStackSize )
{
    static StaticTask_t xIdleTaskTCB;
    static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE];
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *puxIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

#if ( configUSE_TIMERS == 1 )
/* Required only when configUSE_TIMERS == 1 (not currently — this firmware
 * has configUSE_TIMERS == 0). Provided for future-proofing: if software
 * timers are later enabled, this hook will already be in place. */
void vApplicationGetTimerTaskMemory( StaticTask_t **ppxTimerTaskTCBBuffer,
                                     StackType_t **ppxTimerTaskStackBuffer,
                                     configSTACK_DEPTH_TYPE *puxTimerTaskStackSize )
{
    static StaticTask_t xTimerTaskTCB;
    static StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];
    *ppxTimerTaskTCBBuffer = &xTimerTaskTCB;
    *ppxTimerTaskStackBuffer = uxTimerTaskStack;
    *puxTimerTaskStackSize = configTIMER_TASK_STACK_DEPTH;
}
#endif /* configUSE_TIMERS */

#endif /* configSUPPORT_STATIC_ALLOCATION */
/*-----------------------------------------------------------*/

void vApplicationIdleHook( void )
{
    /* vApplicationIdleHook() will only be called if configUSE_IDLE_HOOK is set
    to 1 in FreeRTOSConfig.h.  It will be called on each iteration of the idle
    task.  It is essential that code added to this hook function never attempts
    to block in any way (for example, call xQueueReceive() with a block time
    specified, or call vTaskDelay()).  If the application makes use of the
    vTaskDelete() API function  then it is also
    important that vApplicationIdleHook() is permitted to return to its calling
    function, because it is the responsibility of the idle task to clean up
    memory allocated by the kernel to any task that has since been deleted. */
    
}

/*-----------------------------------------------------------*/

/*-----------------------------------------------------------*/

void vApplicationTickHook( void )
{
    /* This function will be called by each tick interrupt if
    configUSE_TICK_HOOK is set to 1 in FreeRTOSConfig.h.  User code can be
    added here, but the tick hook is called from an interrupt context, so
    code must not attempt to block, and only the interrupt safe FreeRTOS API
    functions can be used (those that end in FromISR()). */
    
}

/*-----------------------------------------------------------*/

/*-----------------------------------------------------------*/

/* Error Handler */
void vAssertCalled( const char * pcFile, unsigned long ulLine )
{
   volatile unsigned long ul = 0;

   ( void ) pcFile;
   ( void ) ulLine;

   taskENTER_CRITICAL();
   {
      /* Set ul to a non-zero value using the debugger to step out of this
         function. */
      while( ul == 0U )
      {
         portNOP();
      }
   }
   taskEXIT_CRITICAL();
}
/*-----------------------------------------------------------*/



/*-----------------------------------------------------------*/

/*-----------------------------------------------------------*/
/*******************************************************************************
 End of File
 */
