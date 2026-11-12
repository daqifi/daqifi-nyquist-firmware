/* ==========================================================================
 * LogRing.h -- the SYSTem:LOG? ring's bookkeeping, as pure functions (#1248).
 *
 * WHY THIS FILE EXISTS
 *
 * LogMessageDump used to remove a record from the ring BEFORE writing it and
 * ignore how many bytes the transport took, so a short write lost the record
 * with nothing to show for it (#1248). The fix -- write first, remove only
 * after a FULL write, and only if the record is still the one that was
 * written -- is all index and counter arithmetic. Logger.c is not
 * host-includable (FreeRTOS, libscpi, xc.h), so that arithmetic lives here,
 * where tests/host/test_1248_log_ring.c compiles the REAL code rather than a
 * copy of it. Logger.c keeps the mutex, the I/O and the FreeRTOS calls.
 *
 * THE IDENTITY PROBLEM, and tailSeq. Between peeking a record and removing
 * it, the log mutex is RELEASED (the write can take ~1 s). In that window the
 * ring can move under the dumper:
 *   - a logger finds the ring full and overwrites the oldest entry, which
 *     advances tail -- and the oldest entry IS the record being written;
 *   - LogMessageClear resets the ring;
 *   - a second dumper (the other SCPI transport) pops it.
 * A tail INDEX cannot tell any of these apart from "nothing happened": after
 * an overwrite the same slot holds a different record. tailSeq can. It counts
 * every removal from the tail, however caused, so "tailSeq is what it was at
 * the peek" means exactly "the record peeked is still the oldest entry".
 *
 * Why a changed tailSeq means "the record is gone, continue from the new
 * tail" and never "skip one" or "resend one": the ring is FIFO, and every
 * removal takes the OLDEST entry first. The peeked record was the oldest at
 * the peek, so the FIRST removal after it -- whichever kind -- removed it.
 * Whatever is at the tail afterwards is newer than it, so this dumper has not
 * sent it (it only ever sends the tail, and pops what it sent). Pinned in
 * test_1248_log_ring.c.
 *
 * tailSeq is 32 bits and changes by one per removal. Wrapping back to the
 * peeked value inside one write would take 2^32 removals during a write that
 * SCPI_WriteWithRetry bounds at ~1 s.
 *
 * NOT thread-safe on their own: every function here must be called with the
 * log mutex held. That is the caller's contract, as for the old inline code.
 *
 * DEPENDENCIES: C standard headers only, so the host test includes this file
 * unchanged. The two sizes are the INCLUDER's: Logger.h defines them from
 * LOG_MAX_ENTRY_COUNT / LOG_MESSAGE_SIZE, which tools/lint/log_budget.py and
 * tests/host/Makefile read out of Logger.h, so they must stay there.
 * ========================================================================== */
#ifndef LOG_RING_H
#define LOG_RING_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifndef LOG_RING_CAPACITY
#error "define LOG_RING_CAPACITY before including LogRing.h"
#endif
#ifndef LOG_RING_MSG_SIZE
#error "define LOG_RING_MSG_SIZE before including LogRing.h"
#endif
#if (LOG_RING_CAPACITY) < 1 || (LOG_RING_CAPACITY) > 255
#error "LOG_RING_CAPACITY must fit the uint8_t head/tail/count"
#endif

#ifdef __cplusplus
extern "C" {
#endif

    typedef struct {
        char message[LOG_RING_MSG_SIZE];   /**< NUL-terminated record */
    } LogRingEntry;

    typedef struct {
        LogRingEntry entries[LOG_RING_CAPACITY];
        uint8_t head;      /**< index of the next push */
        uint8_t tail;      /**< index of the oldest record */
        uint8_t count;     /**< records held */
        /** Removals from the tail ever made: pops, overwrites, clears. */
        uint32_t tailSeq;
    } LogRing;

    /** Boot-time initialisation. */
    static inline void LogRing_Init(LogRing *r) {
        r->head = 0;
        r->tail = 0;
        r->count = 0;
        r->tailSeq = 0;
    }

    /**
     * Empty the ring. Bumps tailSeq even when the ring is empty, so a dumper
     * that peeked before the clear can never pop a record pushed after it.
     */
    static inline void LogRing_Clear(LogRing *r) {
        r->head = 0;
        r->tail = 0;
        r->count = 0;
        r->tailSeq++;
    }

    /**
     * Append a record; when full, drop the OLDEST (the ring's policy before
     * #1248, unchanged) and count that drop as a tail removal.
     * @param len strlen(msg); the caller guarantees len < LOG_RING_MSG_SIZE.
     */
    static inline void LogRing_Push(LogRing *r, const char *msg, size_t len) {
        memcpy(r->entries[r->head].message, msg, len);
        r->entries[r->head].message[len] = '\0';
        r->head = (uint8_t)((r->head + 1u) % (LOG_RING_CAPACITY));
        if (r->count < (LOG_RING_CAPACITY)) {
            r->count++;
        } else {
            r->tail = (uint8_t)((r->tail + 1u) % (LOG_RING_CAPACITY));
            r->tailSeq++;
        }
    }

    /**
     * Copy the oldest record WITHOUT removing it.
     * @param out      receives the record, always NUL-terminated
     * @param outSize  sizeof(out), at least 1
     * @param seq      receives the tailSeq that identifies this record
     * @return false if the ring is empty (out and seq untouched)
     */
    static inline bool LogRing_Peek(const LogRing *r, char *out,
                                    size_t outSize, uint32_t *seq) {
        if (r->count == 0 || outSize == 0) {
            return false;
        }
        strncpy(out, r->entries[r->tail].message, outSize - 1);
        out[outSize - 1] = '\0';
        *seq = r->tailSeq;
        return true;
    }

    /**
     * Remove the oldest record, but only if it is still the one peeked as
     * `seq`. Call ONLY after that record was written in full.
     * @return true if it was removed here; false if something else already
     *         removed it (an overwrite, a clear, or another dumper) -- in
     *         which case there is nothing to do: the record is gone, and the
     *         tail now holds the next record this dumper has not sent.
     */
    static inline bool LogRing_PopIfUnchanged(LogRing *r, uint32_t seq) {
        if (r->count == 0 || r->tailSeq != seq) {
            return false;
        }
        r->tail = (uint8_t)((r->tail + 1u) % (LOG_RING_CAPACITY));
        r->count--;
        r->tailSeq++;
        return true;
    }

    /* ----------------------------------------------------------------------
     * The dumping-task set (#1248).
     *
     * While a dump is reporting a truncation it holds an UNSENT record at the
     * tail. Anything the dumping task itself logs in that window -- the SCPI
     * error callback's "SCPI Error -200" echo, a transport flush complaint --
     * would, on a full ring, overwrite exactly that record: the dump's own
     * error report destroying what the report says was kept. So records the
     * dumping task logs during the dump are not buffered.
     *
     * Keys are opaque (FreeRTOS task handles in firmware). Two slots: there
     * are exactly two SCPI contexts (USB CDC and the WiFi TCP server), and
     * each runs one command at a time, so at most two dumps are in flight.
     * ---------------------------------------------------------------------- */
#define LOG_OWNER_SLOTS 2

    typedef struct {
        const void *owner[LOG_OWNER_SLOTS];
    } LogOwnerSet;

    /** @return false if every slot is taken (the caller is not added). */
    static inline bool LogOwnerSet_Add(LogOwnerSet *s, const void *key) {
        for (unsigned i = 0; i < LOG_OWNER_SLOTS; i++) {
            if (s->owner[i] == NULL) {
                s->owner[i] = key;
                return true;
            }
        }
        return false;
    }

    static inline void LogOwnerSet_Remove(LogOwnerSet *s, const void *key) {
        for (unsigned i = 0; i < LOG_OWNER_SLOTS; i++) {
            if (s->owner[i] == key) {
                s->owner[i] = NULL;
                return;
            }
        }
    }

    static inline bool LogOwnerSet_Contains(const LogOwnerSet *s,
                                            const void *key) {
        if (key == NULL) {
            return false;
        }
        for (unsigned i = 0; i < LOG_OWNER_SLOTS; i++) {
            if (s->owner[i] == key) {
                return true;
            }
        }
        return false;
    }

#ifdef __cplusplus
}
#endif

#endif /* LOG_RING_H */
