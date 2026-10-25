/* ==========================================================================
 * ScpiBoundedWrite.h -- the bounded-write DECISION, as pure functions.
 *
 * WHY THIS FILE EXISTS (#1098)
 *
 * Several SCPI reply paths hold the single shared response buffer's mutex
 * (gScpiRespMutex, #347) across more than one transport write. On both
 * transports that write is SCPI_WriteWithRetry, which against a host that has
 * stopped reading spins SCPI_WRITE_MAX_RETRIES(200) x
 * SCPI_WRITE_RETRY_DELAY_MS(5) ~= 1 s before returning short -- so an N-write
 * reply can hold the mutex for ~N seconds while every other SCPI callback on
 * EITHER transport waits on it (portMAX_DELAY). The established remedy is two
 * guards (#947 SysInfoText_Write, #1004 ScpiHelpWrite, #1098 SysLogLevelWrite):
 *
 *   (1) short write -> latch. SCPI_WriteWithRetry has no resend path, so a
 *       short return means those bytes are already dropped and the remaining
 *       budget buys nothing.
 *   (2) cumulative deadline, checked BEFORE each write. Guard (1) never fires
 *       for a transport draining at exactly the trickle rate that lets every
 *       write finish just inside its own ~1 s budget, so guard (1) alone does
 *       not bound the hold at all.
 *
 * THE PROBLEM THIS SOLVES. Those guards used to live entirely inside a static
 * helper in SCPIInterface.c, which is NOT host-includable (libscpi + Harmony
 * PLIB + the USB HS and WINC drivers + FreeRTOS's MIPS port layer -- 42 direct
 * includes, established by SCPI999_BIN). So tests/host could only re-implement
 * the algorithm and assert against its own copy, and the Makefile could only
 * grep that the call sites NAME the helper. Both stayed green while the real
 * guards were broken: deleting the deadline check from the production helper
 * was measured to leave the ENTIRE host suite passing. A test that cannot fail
 * on the defect it exists to catch is a false guarantee, so the decision moved
 * here, where the real code is the tested code.
 *
 * WHAT IS AND IS NOT HERE. Only the pure decision: no I/O, no logging, no
 * FreeRTOS, no libscpi. The callers keep their own transport write, their own
 * LOG_E wording and their own budget constant -- which is why this does NOT
 * contradict #1004's "each site carries its own small helper rather than one
 * shared generic one". That rule is about the I/O-performing wrapper, whose
 * call-site coupling differs per site; the arithmetic underneath it is
 * identical everywhere and is the only part a host can test.
 *
 * DEPENDENCIES: three C standard headers, deliberately. Same property, and for
 * the same reason, as FixedPointFmt.h, AD7609Scale.h and JSON_StringEscape.h --
 * the test includes the real header directly, with no UUT copy and no stubs.
 *
 * TICK WIDTH. The deadline uses unsigned 32-bit subtraction so it stays correct
 * across the xTaskGetTickCount() wrap (~49.7 days at configTICK_RATE_HZ 1000).
 * That is exact only while TickType_t is 32 bits; tests/host/Makefile pins
 * configTICK_TYPE_WIDTH_IN_BITS and configTICK_RATE_HZ and refuses to build if
 * either drifts.
 * ========================================================================== */
#ifndef SCPI_BOUNDED_WRITE_H
#define SCPI_BOUNDED_WRITE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

    /** What a caller should do with the write it is about to attempt. */
    typedef enum {
        /** Already latched by an earlier failure -- emit nothing more. */
        SCPI_BOUNDED_WRITE_SKIP = 0,
        /** Hold budget exhausted -- latch and emit nothing more. */
        SCPI_BOUNDED_WRITE_EXPIRED,
        /** Within budget and not latched -- perform the write. */
        SCPI_BOUNDED_WRITE_PROCEED
    } ScpiBoundedWriteAction;

    /**
     * Guard (2), plus the latch short-circuit, as one pure decision.
     *
     * Ordering is load-bearing: the latch is tested BEFORE the deadline, so a
     * caller that has already given up does not start reporting a second,
     * different reason for the same abort.
     *
     * The deadline is `elapsed >= budget`, not `now >= start + budget`: the
     * former is correct across the tick wrap, the latter is not.
     *
     * @param ok          caller's latch; false means a previous write failed
     * @param nowTicks    current tick (xTaskGetTickCount() in firmware)
     * @param startTicks  tick sampled once, AFTER the buffer was taken, so the
     *                    budget bounds the HOLD and not the wait for it
     * @param budgetTicks total ticks the hold may span (pdMS_TO_TICKS(budget))
     * @return what the caller should do next
     */
    static inline ScpiBoundedWriteAction ScpiBoundedWrite_Decide(
            bool ok, uint32_t nowTicks, uint32_t startTicks,
            uint32_t budgetTicks) {
        if (!ok) {
            return SCPI_BOUNDED_WRITE_SKIP;
        }
        /* Unsigned subtraction: correct across the 32-bit tick wrap. */
        if ((uint32_t)(nowTicks - startTicks) >= budgetTicks) {
            return SCPI_BOUNDED_WRITE_EXPIRED;
        }
        return SCPI_BOUNDED_WRITE_PROCEED;
    }

    /**
     * Guard (1): did the transport take fewer bytes than offered?
     *
     * Trivial by construction, and extracted anyway so the production helper
     * and the host test cannot disagree about it -- a `>=` here, or comparing
     * against 0 instead of len, is exactly the silent regression #1098's own
     * guards failed to catch while it lived inline.
     *
     * @param written bytes the transport accepted
     * @param len     bytes offered
     * @return true if the write was incomplete and the caller must latch
     */
    static inline bool ScpiBoundedWrite_IsShort(size_t written, size_t len) {
        return written != len;
    }

#ifdef __cplusplus
}
#endif

#endif /* SCPI_BOUNDED_WRITE_H */
