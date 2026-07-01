/*******************************************************************************
  MPLAB Harmony Exceptions Source File

  File Name:
    exceptions.c

  Summary:
    This file contains a function which overrides the default _weak_ exception
    handler provided by the XC32 compiler.

  Description:
    This file redefines the default _weak_  exception handler with a more debug
    friendly one. If an unexpected exception occurs the code will stop in a
    while(1) loop.  The debugger can be halted and two variables exception_code
    and exception_address can be examined to determine the cause and address
    where the exception occurred.
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
#include "device.h"
#include "definitions.h"
#include <stdio.h>

// *****************************************************************************
// *****************************************************************************
// Section: Forward declaration of the handler functions
// *****************************************************************************
// *****************************************************************************
/* MISRAC 2012 deviation block start */
/* MISRA C-2012 Rule 21.2 deviated 8 times. Deviation record ID -  H3_MISRAC_2012_R_21_2_DR_4 */
void _general_exception_handler(void);
void _bootstrap_exception_handler(void);
void _cache_err_exception_handler (void);
void _simple_tlb_refill_exception_handler(void);

// *****************************************************************************
// *****************************************************************************
// Section: Global Data Definitions
// *****************************************************************************
// *****************************************************************************

/*******************************************************************************
  Exception Reason Data

  <editor-fold defaultstate="expanded" desc="Exception Reason Data">

  Remarks:
    These global static items are used instead of local variables in the
    _general_exception_handler function because the stack may not be available
    if an exception has occured.
*/

/* Exception codes */
#define EXCEP_IRQ       0U // interrupt
#define EXCEP_AdEL      4U // address error exception (load or ifetch)
#define EXCEP_AdES      5U // address error exception (store)
#define EXCEP_IBE       6U // bus error (ifetch)
#define EXCEP_DBE       7U // bus error (load/store)
#define EXCEP_Sys       8U // syscall
#define EXCEP_Bp        9U // breakpoint
#define EXCEP_RI        10U // reserved instruction
#define EXCEP_CpU       11U // coprocessor unusable
#define EXCEP_Overflow  12U // arithmetic overflow
#define EXCEP_Trap      13U // trap (possible divide by zero)
#define EXCEP_IS1       16U // implementation specfic 1
#define EXCEP_CEU       17U // CorExtend Unuseable
#define EXCEP_C2E       18U // coprocessor 2

/* Address of instruction that caused the exception. */
volatile static unsigned int exception_address;

/* Code identifying the cause of the exception (CP0 Cause register). */
volatile static uint32_t  exception_code;

/* ---- #552 crash capture (custom — not MCC-generated; preserve on regen) ----
 * A task stack overflow that corrupts pxTopOfStack faults inside the FreeRTOS
 * context-switch restore (port_asm.S) and vectors to _general_exception_handler,
 * BYPASSING vApplicationStackOverflowHook. Record the culprit into these fixed
 * globals (BSS — NOT any task's stack) so the offending task is diagnosable via
 * mdb (`print gCrash*`) or a Logger dump without a live repro. Peer capture in
 * vApplicationStackOverflowHook (freertos_hooks.c) covers the guard-caught case.
 * These read valid only after a crash sets them THIS boot (not zeroed across
 * MCLR in retained-RAM regions — see #409). */
#define CRASH_TASK_NAME_MAX 20

volatile uint32_t gCrashReason     = 0U; /* 0 none, 1 stack-overflow hook, 2 general exception, 3 malloc-fail */
volatile uint32_t gCrashExcCode    = 0U; /* CP0 ExcCode: 2=TLBL, 4=AdEL, 5=AdES, 7=DBE, ... */
volatile uint32_t gCrashExcAddr    = 0U; /* EPC of the faulting instruction */
volatile uint32_t gCrashTaskHandle = 0U; /* current TCB (pxCurrentTCB) at the fault */
volatile char     gCrashTaskName[CRASH_TASK_NAME_MAX] = { 0 };

/* FreeRTOS accessors used below (xTaskGetCurrentTaskHandle / pcTaskGetName) are
 * declared via definitions.h -> task.h, already in scope here. Both are always
 * compiled in this config (INCLUDE_xTaskGetCurrentTaskHandle == 1; pcTaskGetName
 * is unconditional). */

/* On-chip RAM bounds so a corrupt pointer can't cause a nested fault while we
 * copy a name. PIC32MZ2048EFM144 has 512 KB RAM at KSEG0 0x80000000 (KSEG1
 * uncached mirror at 0xA0000000). */
static bool CrashCapture_PtrInRam( uint32_t p )
{
    return ( ( p >= 0x80000000U && p < 0x80080000U ) ||
             ( p >= 0xA0000000U && p < 0xA0080000U ) );
}

/* Non-static: also used by vApplicationStackOverflowHook (freertos_hooks.c). */
void CrashCapture_CopyName( const char * nm )
{
    unsigned int i;
    if ( ( nm == NULL ) || !CrashCapture_PtrInRam( (uint32_t) nm ) )
    {
        return;
    }
    for ( i = 0U; i < ( CRASH_TASK_NAME_MAX - 1U ); i++ )
    {
        /* Validate EACH byte's address: a pointer near the RAM-top boundary
         * could otherwise read past mapped RAM and nested-fault mid-capture. */
        if ( !CrashCapture_PtrInRam( (uint32_t) &nm[i] ) ) { break; }
        char c = nm[i];
        if ( c == '\0' ) { break; }
        gCrashTaskName[i] = ( ( c >= 0x20 ) && ( c <= 0x7E ) ) ? c : '?';
    }
    gCrashTaskName[i] = '\0';
}

/* Zero the crash-capture globals at boot. They live in per-symbol .bss.<name>
 * sections that crt0 does NOT zero across MCLR / IPE flash (retained RAM, #409),
 * so without this an un-crashed boot could read stale metadata (e.g. a non-zero
 * gCrashReason). Call pre-scheduler alongside the other #409 retained-RAM
 * scrubs. See #552. */
void CrashCapture_Init( void )
{
    gCrashReason      = 0U;
    gCrashExcCode     = 0U;
    gCrashExcAddr     = 0U;
    gCrashTaskHandle  = 0U;
    gCrashTaskName[0] = '\0';
}


// </editor-fold>

/*******************************************************************************
  Function:
    void _general_exception_handler ( void )

  Description:
    A general exception is any non-interrupt exception which occurs during program
    execution outside of bootstrap code.

  Remarks:
    Refer to the XC32 User's Guide for additional information.
 */

void __attribute__((noreturn, weak)) _general_exception_handler ( void )
{
    /* Mask off the ExcCode Field from the Cause Register
    Refer to the MIPs Software User's manual */
    exception_code = ((_CP0_GET_CAUSE() & 0x0000007CU) >> 2U);
    exception_address = _CP0_GET_EPC();

    /* #552: record the culprit into fixed globals (not any task stack) so a
     * stack-overflow-escape TLBL — which never reaches the FreeRTOS overflow
     * hook — is diagnosable via mdb without a live repro. pxCurrentTCB is a
     * kernel global (not on a task stack), so it survives a task overflow; the
     * bounds check guards against a wild pointer causing a nested fault. */
    gCrashReason  = 2U;
    gCrashExcCode = exception_code;
    gCrashExcAddr = exception_address;
    gCrashTaskName[0] = '\0';   /* fresh per-crash; overwritten below only on a good capture,
                                 * so a failed/invalid-TCB read can't show a stale name */
    {
        TaskHandle_t tcb = xTaskGetCurrentTaskHandle();
        gCrashTaskHandle = (uint32_t) tcb;
        if ( ( tcb != NULL ) && CrashCapture_PtrInRam( (uint32_t) tcb ) )
        {
            CrashCapture_CopyName( pcTaskGetName( tcb ) );
        }
    }

    while (true)
    {
        #if defined(__DEBUG) || defined(__DEBUG_D) && defined(__XC32)
            __builtin_software_breakpoint();
        #endif
    }
}
/*******************************************************************************
  Function:
    void _bootstrap_exception_handler ( void )

  Description:
    A bootstrap exception is any exception which occurs while bootstrap code is
    running (STATUS.BEV bit is 1).

  Remarks:
    Refer to the XC32 User's Guide for additional information.
 */

void __attribute__((noreturn, weak)) _bootstrap_exception_handler(void)
{
    /* Mask off the ExcCode Field from the Cause Register
    Refer to the MIPs Software User's manual */
    exception_code = (_CP0_GET_CAUSE() & 0x0000007CU) >> 2U;
    exception_address = _CP0_GET_EPC();

    while (true)
    {
        #if defined(__DEBUG) || defined(__DEBUG_D) && defined(__XC32)
            __builtin_software_breakpoint();
        #endif
    }
}
/*******************************************************************************
  Function:
    void _cache_err_exception_handler ( void )

  Description:
    A cache-error exception occurs when an instruction or data reference detects
    a cache tag or data error. This exception is not maskable. To avoid
    disturbing the error in the cache array the exception vector is to an
    unmapped, uncached address. This exception is precise.

  Remarks:
    Refer to the XC32 User's Guide for additional information.
 */

void __attribute__((noreturn, weak)) _cache_err_exception_handler(void)
{
    /* Mask off the ExcCode Field from the Cause Register
    Refer to the MIPs Software User's manual */
    exception_code = (_CP0_GET_CAUSE() & 0x0000007CU) >> 2U;
    exception_address = _CP0_GET_EPC();

    while (true)
    {
        #if defined(__DEBUG) || defined(__DEBUG_D) && defined(__XC32)
            __builtin_software_breakpoint();
        #endif
    }
}

/*******************************************************************************
  Function:
    void _simple_tlb_refill_exception_handler ( void )

  Description:
    During an instruction fetch or data access, a TLB refill exception occurs
    when no TLB entry matches a reference to a mapped address space and the EXL
    bit is 0 in the Status register. Note that this is distinct from the case
    in which an entry matches, but has the valid bit off. In that case, a TLB
    Invalid exception occurs.

  Remarks:
    Refer to the XC32 User's Guide for additional information.
 */

void __attribute__((noreturn, weak)) _simple_tlb_refill_exception_handler(void)
{
    /* Mask off the ExcCode Field from the Cause Register
    Refer to the MIPs Software User's manual */
    exception_code = (_CP0_GET_CAUSE() & 0x0000007CU) >> 2U;
    exception_address = _CP0_GET_EPC();

    while (true)
    {
        #if defined(__DEBUG) || defined(__DEBUG_D) && defined(__XC32)
            __builtin_software_breakpoint();
        #endif
    }
}
/*******************************************************************************
 End of File
*/
