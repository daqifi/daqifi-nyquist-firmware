#ifndef _CIRCULAR_BUFFER_H    /* Guard against multiple inclusion */
#define _CIRCULAR_BUFFER_H


/* ************************************************************************** */
/* ************************************************************************** */
/* Section: Included Files                                                    */
/* ************************************************************************** */
/* ************************************************************************** */
#include <stdint.h>
#include <stdbool.h>
/* This section lists the other files that are included in this file.
 */

/* TODO:  Include other files here if needed. */


/* Provide C++ Compatibility */
#ifdef __cplusplus
extern "C" {
#endif


    /* ************************************************************************** */
    /* ************************************************************************** */
    /* Section: Constants                                                         */
    /* ************************************************************************** */
    /* ************************************************************************** */

    /*  A brief description of a section can be given directly below the section
        banner.
     */
/**
 * CircularBuffer - single-producer / single-consumer byte ring.
 *
 * THREAD-SAFETY CONTRACT (#123):
 *
 * 1. SPSC by design. Exactly ONE producer task may call AddBytes and exactly
 *    ONE consumer task may call ProcessBytes on a given instance. The split
 *    producedBytes/consumedBytes counters (#276) make that pairing safe
 *    WITHOUT a mutex; nothing in this module locks. Two producers (or two
 *    consumers) on one instance corrupt it - callers with multiple writers
 *    (e.g. SCPI responses from USB + WiFi tasks) must serialize externally
 *    (see the wCirbuf wMutex pattern in sd_card_manager.c).
 *
 * 2. Init/InitExternal/Deinit/Resize/Reset are NOT safe concurrent with any
 *    other call. Quiesce both sides first (streaming stopped) - the stream
 *    start/stop re-partition path is the canonical caller.
 *
 * 3. NumBytesAvailable/NumBytesFree are safe from either side (single 32-bit
 *    volatile reads; unsigned subtraction handles counter wraparound), but
 *    are momentary: a producer may see less free space and a consumer less
 *    available data than truly present - never more. Use accordingly
 *    (optimistic checks fine, exactness requires quiescence).
 *
 * 4. ISR context: no CircularBuffer call is ISR-safe. Both sides run in task
 *    context in this firmware (streaming task produces, transport tasks
 *    consume); keep it that way.
 *
 * CALLBACK CONTRACT (process_callback, used by ProcessBytes):
 *
 * - Called from the CONSUMER task's context, up to twice per ProcessBytes
 *    call (wrap-around split).
 * - Return >= 0: the number of bytes actually processed. Partial processing
 *    is honored - the ring advances by the returned count only (#126) and
 *    unprocessed bytes are re-offered on the next call. Returning more than
 *    offered is clamped.
 * - Return < 0: hard error; nothing is consumed and the value is surfaced
 *    through ProcessBytes' *error out-param.
 * - Must not block indefinitely: bounded waits (e.g. DMA-busy retry with
 *    vTaskDelay) are the norm; unbounded blocking stalls the whole consumer
 *    pipeline behind this buffer.
 * - Must not re-enter this module on the SAME instance (AddBytes to a
 *    DIFFERENT buffer is fine and common - e.g. transport fan-out).
 */
typedef struct s_CircularBuf
{
    uint8_t*    insertPtr;
    uint8_t*    removePtr;
    /* Split SPSC counters (#276): producer writes producedBytes,
     * consumer writes consumedBytes. Each has exactly one writer, so
     * the += RMW is safe regardless of task priority and preemption.
     * Available = producedBytes - consumedBytes (unsigned subtraction
     * handles modular wraparound correctly when both overflow). */
    volatile uint32_t    producedBytes;
    volatile uint32_t    consumedBytes;
    uint8_t*    buf_ptr;
    uint32_t    buf_size;
    int        (*process_callback)(uint8_t*, uint32_t);
    bool        _ownsMemory;  // true if buf_ptr was allocated by CircularBuf_Init
}CircularBuf_t;


void     CircularBuf_Init(CircularBuf_t*, int (*fp)(uint8_t*,uint32_t), uint32_t);
void     CircularBuf_InitExternal(CircularBuf_t*, int (*fp)(uint8_t*,uint32_t), uint8_t* buf, uint32_t size);
void     CircularBuf_Deinit(CircularBuf_t*);
bool     CircularBuf_Resize(CircularBuf_t*, uint32_t newSize);
uint32_t CircularBuf_AddBytes(CircularBuf_t*, uint8_t*, uint32_t);
uint32_t CircularBuf_NumBytesAvailable(CircularBuf_t*);
uint32_t CircularBuf_NumBytesFree(CircularBuf_t*);
uint32_t CircularBuf_ProcessBytes(CircularBuf_t*,uint8_t*, uint32_t,int*);
void CircularBuf_Reset(CircularBuf_t* cirbuf);
    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _EXAMPLE_FILE_NAME_H */

/* *****************************************************************************
 End of File
 */
