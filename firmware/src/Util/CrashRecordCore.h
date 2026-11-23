#pragma once
/*
 * CrashRecordCore.h -- the pure validate/decode half of the #1190 crash record.
 *
 * Split out of exceptions.c so a host test can compile it unchanged (no
 * FreeRTOS, no placement attributes). Everything here works on a plain array
 * of CRASH_RECORD_WORDS words: the reader copies the live block into a LOCAL
 * snapshot inside a short critical section and validates THAT copy, so a
 * validity check can never succeed on one state of the block and be followed by
 * a payload read of another (a SYSTem:LOG:CLEar on the other transport used to
 * land between the two and fabricate an all-zero "CRASH reason=0" record).
 */

#include "CrashRecord.h"

/* Rotate-xor fold of the payload words 0..9. `volatile` so the capture side can
 * fold the live block and the reader can fold its snapshot with one routine. */
static inline uint32_t CrashRecord_FoldWords( const volatile uint32_t * w )
{
    uint32_t f = 0U;
    uint32_t i;
    for ( i = 0U; i < CRASH_W_MAGIC; i++ )
    {
        f = ( ( f << 5 ) | ( f >> 27 ) ) ^ w[i];
    }
    return f;
}

/* True only when `w` (CRASH_RECORD_WORDS words) holds magic, the matching check
 * word and a known reason. A cleared (all zero), half-written or mixed block
 * fails. */
static inline bool CrashRecord_SnapshotValid( const uint32_t * w )
{
    uint32_t r;
    if ( w[CRASH_W_MAGIC] != CRASH_RECORD_MAGIC )
    {
        return false;
    }
    if ( w[CRASH_W_CHECK] != ~( CRASH_RECORD_MAGIC ^ CrashRecord_FoldWords( w ) ) )
    {
        return false;
    }
    r = w[CRASH_W_REASON];
    return ( r >= CRASH_REASON_OVERFLOW ) && ( r <= CRASH_REASON_MALLOC );
}

/* Decode a snapshot. Returns true only if the SNAPSHOT validates. */
static inline bool CrashRecord_SnapshotToView( const uint32_t * w, CrashRecordView * out )
{
    uint32_t i;
    if ( ( out == NULL ) || !CrashRecord_SnapshotValid( w ) )
    {
        return false;
    }
    out->reason  = w[CRASH_W_REASON];
    out->excCode = w[CRASH_W_EXCCODE];
    out->excAddr = w[CRASH_W_EXCADDR];
    out->tcb     = w[CRASH_W_TCB];
    for ( i = 0U; i < CRASH_TASK_NAME_MAX; i++ )
    {
        uint32_t nw = w[CRASH_W_NAME0 + ( i >> 2 )];
        char c = (char) ( ( nw >> ( 8U * ( i & 3U ) ) ) & 0xFFU );
        out->name[i] = ( ( c >= 0x20 ) && ( c <= 0x7E ) ) ? c : '\0';
    }
    out->name[CRASH_TASK_NAME_MAX] = '\0';
    return true;
}
