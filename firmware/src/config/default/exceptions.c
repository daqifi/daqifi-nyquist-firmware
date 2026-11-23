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

/* ---- #552 crash capture, persisted across reset by #1190 (custom -- not
 * MCC-generated; preserve on regen) ----
 * A task stack overflow that corrupts pxTopOfStack faults inside the FreeRTOS
 * context-switch restore (port_asm.S) and vectors to _general_exception_handler,
 * BYPASSING vApplicationStackOverflowHook. The culprit is recorded into the
 * fixed-address block below (NOT any task's stack) by that handler and by the
 * overflow and malloc-failed hooks in freertos_hooks.c, then read back after
 * the reset by SYSTem:LOG?. Layout, validity rule and placement rationale:
 * Util/CrashRecord.h.
 *
 * The block is `persistent, coherent, address()`: crt0 does not clear it
 * (XC32 DS50002799E 18.2.8) and the stores bypass the write-back D-cache so they
 * reach SRAM before the reset. It survives MCLR / software / watchdog reset; it
 * does NOT survive a power cycle (SRAM is then undefined, and the validity pair
 * is what makes that read as "no record"). */
#include "Util/CrashRecord.h"
#include "Util/CrashRecordCore.h"

static volatile uint32_t sCrashRecord[CRASH_RECORD_WORDS]
    __attribute__((persistent, coherent, address(CRASH_RECORD_ADDR)));

/* Identity of the RUNNING image, stored into every record by Seal() so a record
 * that outlives a firmware update (the bootloader never touches the block) is
 * recognisable as written by a different image (#1190 r3). Plain BSS, published
 * by CrashRecord_SetImageId() from main() once the CRC32 fingerprint is computed;
 * CrashCapture_Init() zeroes it first, because per-symbol .bss is not guaranteed
 * zeroed across a reset (#409) and a stale value from the previous boot must not
 * be stamped onto this image's record. 0 = not yet known. */
static volatile uint32_t sCrashImageId = 0U;

/* sizeof() cannot see persistent/coherent: tools/lint/force_bootloader_cacheline.py
 * owns that half. These two catch the size and alignment halves at compile time. */
_Static_assert((sizeof(sCrashRecord) % 16u) == 0u,
    "sCrashRecord must reserve whole 16-byte D-cache lines (#1190, #1083)");
_Static_assert(((CRASH_RECORD_ADDR) & 0xFu) == 0u,
    "CRASH_RECORD_ADDR must be 16-byte aligned to a D-cache line (#1190, #1083)");

/* On-chip RAM bounds so a corrupt pointer can't cause a nested fault while we
 * copy a name. PIC32MZ2048EFM144 has 512 KB RAM at KSEG0 0x80000000 (KSEG1
 * uncached mirror at 0xA0000000). */
static bool CrashCapture_PtrInRam( uint32_t p )
{
    return ( ( p >= 0x80000000U && p < 0x80080000U ) ||
             ( p >= 0xA0000000U && p < 0xA0080000U ) );
}

/* Everything down to the boot/readback marker runs while the system is already
 * faulting: plain aligned 32-bit stores through the volatile array, no library
 * calls, and leaf functions (no stack frame). */

void CrashRecord_Begin( void )
{
    uint32_t i;
    /* Invalidate FIRST: a crash that dies half way through its own capture must
     * read as "no record", not as the previous record with a torn payload. */
    sCrashRecord[CRASH_W_MAGIC] = 0U;
    sCrashRecord[CRASH_W_CHECK] = 0U;
    for ( i = 0U; i < CRASH_W_MAGIC; i++ )
    {
        sCrashRecord[i] = 0U;
    }
}

void CrashRecord_SetWord( uint32_t idx, uint32_t v )
{
    if ( idx < CRASH_W_MAGIC )
    {
        sCrashRecord[idx] = v;
    }
}

/* Task name -> words 4..8, 4 bytes per word little-endian, NUL padded. Every
 * source byte's address is range-checked (a pointer near the RAM top could
 * otherwise read past mapped RAM and nested-fault mid-capture) and
 * non-printables become '?'. A NULL / out-of-RAM name leaves the field empty. */
void CrashRecord_CopyName( const char * nm )
{
    uint32_t i;
    uint32_t acc = 0U;
    bool ended = ( nm == NULL ) || !CrashCapture_PtrInRam( (uint32_t) nm );

    for ( i = 0U; i < CRASH_TASK_NAME_MAX; i++ )
    {
        uint32_t c = 0U;
        /* The last byte is always the terminator. */
        if ( !ended && ( i < ( CRASH_TASK_NAME_MAX - 1U ) ) )
        {
            if ( !CrashCapture_PtrInRam( (uint32_t) &nm[i] ) )
            {
                ended = true;
            }
            else
            {
                char ch = nm[i];
                if ( ch == '\0' )
                {
                    ended = true;
                }
                else
                {
                    c = ( ( ch >= 0x20 ) && ( ch <= 0x7E ) ) ? (uint32_t) (uint8_t) ch : (uint32_t) '?';
                }
            }
        }
        acc |= c << ( 8U * ( i & 3U ) );
        if ( ( i & 3U ) == 3U )
        {
            sCrashRecord[CRASH_W_NAME0 + ( i >> 2 )] = acc;
            acc = 0U;
        }
    }
}

/* Validity LAST: payload (already stored), then the check word, then the magic.
 * `sync` drains the uncached write buffer so every store has reached SRAM
 * before the caller spins or resets. */
void CrashRecord_Seal( void )
{
    sCrashRecord[CRASH_W_IMAGE] = sCrashImageId;   /* inside the check's fold */
    __asm__ volatile ( "sync" : : : "memory" );
    sCrashRecord[CRASH_W_CHECK] = ~( CRASH_RECORD_MAGIC ^ CrashRecord_FoldWords( sCrashRecord ) );
    __asm__ volatile ( "sync" : : : "memory" );
    sCrashRecord[CRASH_W_MAGIC] = CRASH_RECORD_MAGIC;
    __asm__ volatile ( "sync" : : : "memory" );
}

/* ---- boot / readback side (task or pre-scheduler context, stack available) -- */

/* Copy the live block into `snap` (CRASH_RECORD_WORDS words). The caller owns
 * the exclusion: a critical section in task context, nothing pre-scheduler. */
static void CrashRecord_Copy( uint32_t * snap )
{
    uint32_t i;
    for ( i = 0U; i < CRASH_RECORD_WORDS; i++ )
    {
        snap[i] = sCrashRecord[i];
    }
}

/* Magic first, so an interrupted clear leaves "no record". No locking. */
static void CrashRecord_ClearUnlocked( void )
{
    uint32_t i;
    sCrashRecord[CRASH_W_MAGIC] = 0U;
    for ( i = 0U; i < CRASH_RECORD_WORDS; i++ )
    {
        sCrashRecord[i] = 0U;
    }
}

/* SYSTem:LOG:CLEar. Task context (the scheduler is running at the only call
 * site, SCPI_SysLogClear). The clear runs inside the SAME critical section the
 * reader's snapshot copy uses (CrashRecord_Read), so a reader sees either the
 * whole record or the whole clear, never a mix. */
void CrashRecord_Clear( void )
{
    taskENTER_CRITICAL();
    CrashRecord_ClearUnlocked();
    taskEXIT_CRITICAL();
}

/* Validate the block at boot and clear it ONLY when it is not a valid record.
 * A valid record survives until it is read and cleared (SYSTem:LOG:CLEar). The
 * block is NOT in retained-by-accident RAM: it is placed `persistent` on purpose,
 * so a boot that follows a crash finds the record and a cold boot (undefined
 * SRAM) finds garbage that fails validation and is zeroed here. Call
 * pre-scheduler (single-threaded, so no critical section is taken). See #552,
 * #1190. */
void CrashCapture_Init( void )
{
    uint32_t snap[CRASH_RECORD_WORDS];
    sCrashImageId = 0U;
    CrashRecord_Copy( snap );
    if ( !CrashRecord_SnapshotValid( snap ) )
    {
        CrashRecord_ClearUnlocked();
    }
}

void CrashRecord_SetImageId( uint32_t id )
{
    sCrashImageId = id;
}

/* Snapshot the block inside a short critical section (12 word loads), then
 * validate and decode the SNAPSHOT outside it. Returns true only if the copy
 * itself validates, so a concurrent SYSTem:LOG:CLEar (other transport) yields
 * "no record" rather than a zeroed or half-cleared one. Task context only.
 *
 * The capture WRITE path (CrashRecord_Begin/SetWord/CopyName/Seal, above)
 * cannot take this critical section: it runs while the system is faulting,
 * possibly from an exception with the kernel state undefined, and then resets.
 * It does not need to. It is plain aligned 32-bit stores with the validity pair
 * written last (check, then magic), after a Begin() that invalidated the magic
 * first, and no task runs again before the reset, so the only reader that can
 * ever observe it mid-write is a boot that finds it torn -- and a torn block
 * fails the magic/check test and reads as absent. */
bool CrashRecord_Read( CrashRecordView * out )
{
    uint32_t snap[CRASH_RECORD_WORDS];
    if ( out == NULL )
    {
        return false;
    }
    taskENTER_CRITICAL();
    CrashRecord_Copy( snap );
    taskEXIT_CRITICAL();
    return CrashRecord_SnapshotToView( snap, sCrashImageId, out );
}

/* Grammar and rationale: CrashRecord_FormatLine() in Util/CrashRecordCore.h
 * (the pure half, compiled unchanged by the host test). */
size_t CrashRecord_Format( char * buf, size_t n, const CrashRecordView * v )
{
    return CrashRecord_FormatLine( buf, n, v );
}

#ifdef CRASH_RECORD_TEST_HOOK
/* #1190 companion-test hook, compiled ONLY with -DCRASH_RECORD_TEST_HOOK=1 (a
 * test build; the shipped image has neither this function nor the reset below).
 * Each case drives the REAL capture site, not a copy of it. After the capture
 * the real sites call CrashRecord_TestReset() so a test needs no debugger to get
 * the reset the record has to survive. */
extern void vApplicationStackOverflowHook( TaskHandle_t xTask, char *pcTaskName );

void CrashRecord_TestReset( void )
{
    RCON_SoftwareReset();
    for ( ;; ) { }
}

void CrashRecord_TestFire( int32_t which )
{
    switch ( which )
    {
        case 1:
            vApplicationStackOverflowHook( xTaskGetCurrentTaskHandle(),
                                           pcTaskGetName( NULL ) );
            break;
        case 2:
            /* Unaligned word store to valid RAM (a literal `sw`, which the compiler
             * cannot turn into swl/swr): Address Error (store), ExcCode 5,
             * vectors to _general_exception_handler. */
            __asm__ volatile ( "sw $zero, 1(%0)" : : "r" ( 0x80000000U ) : "memory" );
            break;
        case 3:
            /* Larger than the heap: pvPortMalloc fails and calls
             * vApplicationMallocFailedHook. */
            ( void ) pvPortMalloc( 0x7FFFFFF0U );
            break;
        default:
            break;
    }
}
#endif /* CRASH_RECORD_TEST_HOOK */


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

    /* #552/#1190: record the culprit into the persistent crash record (not any
     * task stack) so a stack-overflow-escape TLBL -- which never reaches the
     * FreeRTOS overflow hook -- is readable after the reset without a debugger.
     * pxCurrentTCB is a kernel global (not on a task stack), so it survives a
     * task overflow; the bounds check guards against a wild pointer causing a
     * nested fault. Begin() invalidates the old record first, Seal() writes the
     * validity words last, so a capture cut short reads as "no record". */
    CrashRecord_Begin();
    CrashRecord_SetWord( CRASH_W_REASON,  CRASH_REASON_EXCEPTION );
    CrashRecord_SetWord( CRASH_W_EXCCODE, exception_code );
    CrashRecord_SetWord( CRASH_W_EXCADDR, exception_address );
    /* Capture whenever the scheduler has started (RUNNING or SUSPENDED — a
     * scheduler-suspended crash still has a valid pxCurrentTCB); only skip
     * pre-scheduler (NOT_STARTED), where pxCurrentTCB is invalid and the kernel
     * accessors could nested-fault. */
    if ( xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED )
    {
        TaskHandle_t tcb = xTaskGetCurrentTaskHandle();
        CrashRecord_SetWord( CRASH_W_TCB, (uint32_t) tcb );
        if ( ( tcb != NULL ) && CrashCapture_PtrInRam( (uint32_t) tcb ) )
        {
            CrashRecord_CopyName( pcTaskGetName( tcb ) );
        }
    }
    CrashRecord_Seal();
#ifdef CRASH_RECORD_TEST_HOOK
    CrashRecord_TestReset();
#endif

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
