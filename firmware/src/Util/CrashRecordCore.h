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

#include <stdio.h>
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

/* Decode a snapshot. Returns true only if the SNAPSHOT validates (magic, check
 * word over words 0..9 INCLUDING the image id, known reason). `runningId` is the
 * running image's identity; the view is `current` only when the record's id is
 * non-zero and equal to it. A mismatch is still a record: it is returned, tagged
 * `current == false`, with the old identity kept in `imageId`. */
static inline bool CrashRecord_SnapshotToView( const uint32_t * w, uint32_t runningId,
                                               CrashRecordView * out )
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
    out->imageId = w[CRASH_W_IMAGE];
    out->current = ( out->imageId != 0U ) && ( out->imageId == runningId );
    for ( i = 0U; i < CRASH_TASK_NAME_MAX; i++ )
    {
        uint32_t nw = w[CRASH_W_NAME0 + ( i >> 2 )];
        char c = (char) ( ( nw >> ( 8U * ( i & 3U ) ) ) & 0xFFU );
        out->name[i] = ( ( c >= 0x20 ) && ( c <= 0x7E ) ) ? c : '\0';
    }
    out->name[CRASH_TASK_NAME_MAX] = '\0';
    return true;
}

/* One line, CRLF terminated, at most CRASH_LINE_MAX bytes. Grammar (#1190 r3):
 *
 *   CRASH reason=<n>(<why>) exccode=<n> <epc> tcb=0x<8 hex> task=<name> image=<img> id=<8 hex>
 *
 *   <epc> = epc=0x<8 hex>        image=current   (written by the running image)
 *         | epc(prev)=0x<8 hex>  image=previous  (written by another image, or by
 *                                one that had not yet published its identity)
 *
 * A previous-image EPC is an address in a binary that is no longer on the board,
 * so it is labelled and never offered as a current-map address. id= is the
 * identity (CRC32 fingerprint) of the image that WROTE the record, 00000000 when
 * that was not yet known. */
static inline size_t CrashRecord_FormatLine( char * buf, size_t n, const CrashRecordView * v )
{
    const char * why = ( v->reason == CRASH_REASON_OVERFLOW )  ? "stack-overflow-hook" :
                       ( v->reason == CRASH_REASON_EXCEPTION ) ? "general-exception"   :
                       ( v->reason == CRASH_REASON_MALLOC )    ? "malloc-failed"       : "unknown";
    int len = snprintf( buf, n,
        "CRASH reason=%u(%s) exccode=%u %s=0x%08X tcb=0x%08X task=%s image=%s id=%08X\r\n",
        (unsigned) v->reason, why, (unsigned) v->excCode,
        v->current ? "epc" : "epc(prev)", (unsigned) v->excAddr,
        (unsigned) v->tcb, v->name,
        v->current ? "current" : "previous", (unsigned) v->imageId );
    if ( ( len < 0 ) || ( (size_t) len >= n ) )
    {
        return ( n > 0U ) ? ( n - 1U ) : 0U;
    }
    return (size_t) len;
}
