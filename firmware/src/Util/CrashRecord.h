#pragma once
/*
 * CrashRecord.h -- the #552 crash record, persisted across a reset (#1190).
 *
 * The capture sites (exceptions.c's general-exception handler and
 * freertos_hooks.c's stack-overflow and malloc-failed hooks) write the culprit
 * into a fixed-address, startup-untouched block; CrashCapture_Init() validates
 * it at boot instead of zeroing it; SYSTem:LOG? reports it and SYSTem:LOG:CLEar
 * discards it.
 *
 * Block layout, CRASH_RECORD_WORDS 32-bit words (whole 16-byte D-cache lines):
 *
 *   word  0       reason       CRASH_REASON_*
 *   word  1       ExcCode      CP0 Cause.ExcCode (0 for the two hook reasons)
 *   word  2       EPC          faulting instruction (0 for the two hook reasons)
 *   word  3       TCB          pxCurrentTCB / the overflowing task handle
 *   words 4..8    task name    20 bytes, NUL padded, packed little-endian
 *   word  9       image id    identity of the firmware image that wrote the record
 *                             (#1190 r3): the build's CRC32 fingerprint (#833,
 *                             SCPI_FirmwareImageCrc32), 0 if the capture ran before
 *                             main() published it. Folded into the check word.
 *   word 10       magic        CRASH_RECORD_MAGIC, written LAST
 *   word 11       check        ~(CRASH_RECORD_MAGIC ^ fold(words 0..9)), written
 *                              just before the magic
 *
 * A block is a record only when word 10 holds the magic AND word 11 holds the
 * matching check. Anything else -- never written, half written, random power-up
 * SRAM, or overwritten by something that ran before us (the bootloader's stack
 * grows down through this region of RAM) -- reads as NO record, never as a
 * crash. The validity pair sits in the TOP line on purpose: a downward-growing
 * stack reaches it first.
 *
 * Placement: one `persistent, coherent, address()` array, the construct
 * PowerApi.c (reboot handoff) and SCPIInterface.c (bootloader-entry flag)
 * already use. `persistent` keeps crt0 from clearing it (XC32 user's guide
 * DS50002799E 18.2.8); `coherent` makes the stores bypass the write-back
 * D-cache so they reach SRAM before the reset instead of sitting in a dirty
 * line (#1083). xc32-ld v4.60 needs the address 16-byte aligned and exits 5 with
 * no diagnostic otherwise. tools/lint/force_bootloader_cacheline.py owns the
 * attribute/size/alignment half that a _Static_assert cannot see.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define CRASH_REASON_OVERFLOW   1u  /* vApplicationStackOverflowHook */
#define CRASH_REASON_EXCEPTION  2u  /* _general_exception_handler */
#define CRASH_REASON_MALLOC     3u  /* vApplicationMallocFailedHook */

#define CRASH_TASK_NAME_MAX     20u

#define CRASH_W_REASON   0u
#define CRASH_W_EXCCODE  1u
#define CRASH_W_EXCADDR  2u
#define CRASH_W_TCB      3u
#define CRASH_W_NAME0    4u   /* words 4..8 (CRASH_TASK_NAME_MAX bytes) */
#define CRASH_W_IMAGE    9u   /* identity of the image that captured it */
#define CRASH_W_MAGIC    10u
#define CRASH_W_CHECK    11u
#define CRASH_RECORD_WORDS 12u

#define CRASH_RECORD_MAGIC 0xC4A5B007u

/* Placement. Three whole lines, 0x400 below the bootloader-entry flag line
 * (FORCE_BOOTLOADER_FLAG_ADDR is the top 16 bytes of RAM), NOT directly below
 * the reboot-handoff line as the other two blocks are chained: on a
 * bootloader-linked image the shipped USB bootloader runs first on every reset
 * and its stack starts at 0x8007FFE8 growing DOWN (PowerApi.c), so a block
 * directly under the handoff line is inside its first frames -- measured
 * 2026-11-22: the record did not survive a reset under the shipped bootloader
 * at 0x8007FFA0. A static walk of the shipped bootloader's direct call graph
 * from main() gives 336 bytes of frames (plus crt0's 24 and main's 24, ~360
 * bytes, i.e. down to 0x8007FE80); 0x400 puts the block's top at 0x8007FC20,
 * ~1 KB below the stack top. The application's own stack (crt0 _stack) is
 * placed by the linker BELOW the lowest absolute block, so it cannot reach the
 * record either. */
#define CRASH_RECORD_GAP   0x400
#define CRASH_RECORD_ADDR  (FORCE_BOOTLOADER_FLAG_ADDR - CRASH_RECORD_GAP)

/* Result of CrashRecord_Read. */
typedef struct {
    uint32_t reason;
    uint32_t excCode;
    uint32_t excAddr;
    uint32_t tcb;
    uint32_t imageId;   /* identity of the image that WROTE the record */
    bool     current;   /* imageId is non-zero and equals the running image's */
    char     name[CRASH_TASK_NAME_MAX + 1u];
} CrashRecordView;

/* ---- capture side: plain aligned 32-bit stores, no library calls ---------- */
void CrashRecord_Begin( void );                 /* invalidate, then caller fills */
void CrashRecord_SetWord( uint32_t idx, uint32_t v );
void CrashRecord_CopyName( const char * nm );   /* bounds-checked, packs words */
void CrashRecord_Seal( void );                  /* check word, then magic LAST */

/* ---- boot / readback side ------------------------------------------------- */
void CrashCapture_Init( void );                 /* validate; clear ONLY if invalid */
/* Set the RUNNING image's identity (its CRC32 fingerprint). main() calls it with 0
 * first (scrub a stale value) and with the fingerprint before the scheduler
 * starts; in between, and until then, the identity is 0 (unknown). */
void CrashRecord_SetImageId( uint32_t id );
bool CrashRecord_Read( CrashRecordView * out ); /* true only for a valid record */
void CrashRecord_Clear( void );                 /* explicit clear (SYSTem:LOG:CLEar) */
/* Worst-case line length incl. CRLF and NUL is 146 bytes; size the buffer with this. */
#define CRASH_LINE_MAX 160u
size_t CrashRecord_Format( char * buf, size_t n, const CrashRecordView * v );

#ifdef CRASH_RECORD_TEST_HOOK
/* Test builds only (make MP_EXTRA_CC_PRE=-DCRASH_RECORD_TEST_HOOK=1): force a
 * capture path for the #1190 companion test. Never compiled into a shipped
 * image. 1 = stack-overflow hook, 2 = general exception, 3 = malloc failure. */
void CrashRecord_TestFire( int32_t which );
/* Called by the real capture sites right after the record is sealed: software
 * reset, so the record can be read back with no debugger. */
void CrashRecord_TestReset( void );
#endif
