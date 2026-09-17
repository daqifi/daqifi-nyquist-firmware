/* ==========================================================================
 * WaitLoop.h — the ONE spin-then-yield wait loop the polled HAL drivers share
 *
 * #913 gave three hand-rolled polled drivers the same bounded
 * spin-then-vTaskDelay(1) wait so a slow byte stops busy-spinning at the
 * dispatching SCPI task's priority and starving streaming/SD/WiFi. That left
 * the SAME loop written out three times — spi_WaitStat (UserSpi.c),
 * uart_WaitSta (UserUart.c) and i2c_WaitMif (UserI2c.c) — and a fourth copy in
 * the host test, which could only RE-IMPLEMENT the shape because none of those
 * .c files is includable on a host (Harmony PLIBs, FreeRTOS, the DIO ownership
 * registry). So the suite proved the shape's behaviour and nothing about
 * whether the firmware still had that shape: delete the fresh register read at
 * expiry, or move the deadline test after the yield, and every host test
 * stayed green. A textual shape checker was built to close that and reached
 * five generations before being deleted (see the block above $(WAITLOOP_BIN)
 * in tests/host/Makefile); closing the last hole needed a C parser, which
 * #1032 exists to get this repo out of.
 *
 * #1056 takes the other route, the one #889 took for AD7609_ConvertToVoltage:
 * split the logic into a header the host test includes FOR REAL. The loop now
 * exists once, here; tests/host/test_1056_wait_loop.c compiles the EXACT text
 * XC32 compiles into the firmware, so there is no copy to drift and no shape
 * to pin. The three drivers keep their own signatures, their own constants and
 * their own register reads, and differ only in what they inject.
 *
 * WHY CALLBACKS AND NOT A SHARED TIME BASE. The four waits do NOT share a
 * clock: spi, uart and dac7718 (#1108) measure a FreeRTOS TickType_t budget
 * from xTaskGetTickCount(), while i2c measures CP0 core-timer CYCLES from
 * _CP0_GET_COUNT() against I2C_OP_TIMEOUT_CP0. Baking either in would force a
 * behaviour change on the other, so the loop below owns no clock at all: each
 * driver supplies its own "has my budget run out" predicate, its own status
 * read, and its own yield.
 *
 * COST. The status predicate is called once per spin iteration — up to 8000
 * times — so an indirect call per iteration would lengthen the spin and
 * silently move the spin/yield cutover each driver's iteration bound is sized
 * around. It does not: every call site passes literal function addresses to a
 * static inline, so the optimiser inlines this loop, constant-propagates the
 * three pointers and inlines them too. Checked at -O1/-O2/-O3/-Os (gcc 13.3,
 * 2026-09-16): zero indirect calls, and at -O2 an instruction-for-instruction
 * match against the hand-written pre-#1056 loop. XC32 v4.60 is clang-based and
 * the firmware builds at -O2/-O3, where this chain (inline -> constant
 * propagation -> devirtualise) is routine; the drivers are written so it
 * applies — each keeps its predicates as file-local statics named directly at
 * the one call site, never through a variable.
 *
 * SCOPE: control flow only. No FreeRTOS type, no register access, no logging,
 * nothing a host cannot compile. Anything hardware-shaped belongs in the
 * driver's own callbacks.
 *
 * TWO SHAPES, NOT ONE (#1108/#1109). DAC7718 does not share the other three
 * drivers' retry shape: #1057 deliberately moved its fast spin OUTSIDE the
 * retry loop so a stuck transfer pays one register read per 1 ms tick instead
 * of re-running the whole spin on every wake -- see DAC7718.c's dac7718_WaitStat
 * for the fault-path-duty-cycle reasoning (the #913 starvation class). #1108
 * folded DAC7718 onto WaitLoop_SpinThenYield as though it were a fourth
 * instance of the RE-spinning shape; it is not, and doing so silently
 * reintroduced that duty-cycle regression (measured: +316 B against main,
 * where #1056's fold of the three re-spinning drivers was -35 B -- a size
 * regression from the retry loop no longer folding to the same code, not from
 * lost devirtualisation). WaitLoop_HoistedSpinThenYield below is the second
 * shape, spin run ONCE before the retry loop, kept side by side with the
 * re-spinning WaitLoop_SpinThenYield rather than a flag parameter, so each
 * driver's call site names the shape it actually wants and neither shape's
 * host test has to branch on which behaviour it is proving.
 * ========================================================================== */
#ifndef WAITLOOP_H
#define WAITLOOP_H

#include <stdbool.h>
#include <stdint.h>

/*!
 * A test the wait loop re-evaluates from scratch each time it is called —
 * either "is the hardware status I am waiting for met?" or "has my time
 * budget run out?".
 *
 * These are the same C type, so the compiler cannot catch the two being
 * swapped at a call site. That is why each driver's wrapper is a single call
 * naming two distinctly-named file-local statics rather than passing pointers
 * around.
 */
typedef bool (*WaitLoop_PredicateFn)(void *ctx);

/*! Give the rest of the system the CPU for one tick (vTaskDelay(1)). */
typedef void (*WaitLoop_YieldFn)(void *ctx);

/*!
 * True once @p elapsed ticks/cycles have reached or passed @p budget.
 * Pure arithmetic, not a register or clock read -- the caller does the
 * (now - start) subtraction itself, so this stays host-compilable and
 * hardware-agnostic no matter which clock domain @p elapsed came from.
 *
 * #1114: spi_WaitBudgetSpent and uart_WaitBudgetSpent's own inline
 * comparisons always used '>='; i2c_WaitBudgetSpent's used a strict '>'
 * instead, inherited verbatim through #1056's and #1108's WaitLoop.h folds
 * because neither refactor was meant to change a driver's behaviour.
 * Decided: '>=' everywhere. i2c_WaitBudgetSpent (UserI2c.c) now calls this
 * function instead of inlining the comparison, which is the part that
 * actually closes the decision -- spi_WaitStat/uart_WaitSta's inline copies
 * are untouched (already '>=', so migrating them is not this issue's
 * decision to make) and stay exactly what they were. The CP0 cycle this
 * moves the I2C timeout boundary by (~1.4 ppm of a 700,000-cycle budget) is
 * not observable on the bus -- see UserI2c.c's own comment.
 */
static inline bool WaitLoop_BudgetSpent(uint32_t elapsed, uint32_t budget)
{
    return elapsed >= budget;
}

/*!
 * Wait for @p statusMet, spinning briefly before yielding, and give up only
 * once @p budgetSpent AND a final fresh @p statusMet both say so.
 *
 * ORDERING IS THE WHOLE POINT (#913, tightened by an opus review of it). Per
 * pass the status is read up to @p spinCount times, then ONCE MORE, and only
 * THEN is the budget consulted — so an operation that completed while this
 * task was preempted is reported as success however late it is observed. At
 * expiry the status is read AGAIN, freshly, rather than reusing the read
 * above it: this task can be preempted in the gap between those two, and on
 * the WiFi SCPI path (app_WifiTask, priority 2) a gap longer than the whole
 * budget is reachable under streaming load. Returning a bare `false` there
 * reports a COMPLETED operation as a timeout — which on the i2c twin fires
 * i2c_BusRecover() on a healthy bus. Only a status still unmet at the moment
 * the budget is spent can produce false, which is what makes each driver's
 * budget immune to scheduling latency and sizeable against wire time alone.
 *
 * The tight spin covers the fast path with no context switch (a byte at the
 * drivers' ~100 kHz defaults completes in well under 100 us); @p spinCount is
 * each driver's own sizing of "how long is it worth spinning before a tick
 * sleep is the cheaper trade", and is documented at each call site.
 *
 * @param[in] statusMet    The hardware condition being waited for. Must read
 *                         the status afresh on every call — a cached value
 *                         defeats the fresh-read-at-expiry guarantee above.
 * @param[in] budgetSpent  True once this wait's time budget has run out. Owns
 *                         its own clock and its own comparison; the loop never
 *                         looks at a tick count.
 * @param[in] yield        Called only when the status is unmet and the budget
 *                         still has room. Must actually let lower-priority
 *                         work run, or the loop is a busy-wait with extra
 *                         steps.
 * @param[in] ctx          Passed through to all three, untouched.
 * @param[in] spinCount    Status reads per pass before yielding is considered.
 * @return true if the status was ever observed met (including on the fresh
 *         read taken at expiry); false only on a genuine timeout.
 */
static inline bool WaitLoop_SpinThenYield(WaitLoop_PredicateFn statusMet,
                                          WaitLoop_PredicateFn budgetSpent,
                                          WaitLoop_YieldFn     yield,
                                          void                *ctx,
                                          uint32_t             spinCount)
{
    for (;;) {
        for (uint32_t s = 0; s < spinCount; ++s) {
            if (statusMet(ctx)) { return true; }
        }
        if (statusMet(ctx)) { return true; }
        if (budgetSpent(ctx)) {
            /* FRESH read, not a reuse of the one above: see the ordering
             * paragraph in this function's doc comment. */
            return statusMet(ctx);
        }
        yield(ctx);
    }
}

/*!
 * Same contract, ordering guarantee and parameters as WaitLoop_SpinThenYield
 * (read its doc comment first -- this repeats only what differs), except the
 * fast spin is NOT re-paid on every wake: it runs @p spinCount times ONCE,
 * before the retry loop is ever entered, and every pass through the retry
 * loop after that reads @p statusMet exactly once instead of spinning again.
 *
 * WHY THIS EXISTS AS A SECOND FUNCTION RATHER THAN A FLAG. #1057 made this
 * shape deliberately for DAC7718: a legitimate SPI2 byte completes inside the
 * first spin (~1.14 us, far under one tick), so the hoist is invisible on the
 * fast path and only ever matters once the fast path has already failed --
 * i.e. it is a FAULT-PATH duty-cycle choice, not a completion-latency one.
 * Once the status is genuinely being waited on for real (SPI2 not
 * responding), a single register read per 1 ms tick detects a level-sensitive
 * status bit exactly as reliably as an @p spinCount-iteration re-spin would,
 * for a fraction of the CPU cost, paid at the dispatching SCPI task's
 * priority -- the same starvation class #913 exists to prevent. #1108 folded
 * DAC7718 onto the re-spinning WaitLoop_SpinThenYield as though it were a
 * fourth instance of the same shape; it silently re-introduced that
 * regression (see the file header). A bool parameter selected at the call
 * site would fold away identically under inlining + constant propagation, but
 * would also let a future caller flip it without reading either doc comment;
 * a separately-named function makes the two fault-path duty cycles a
 * deliberate choice at the call site instead of a drive-by argument, and
 * keeps WaitLoop_SpinThenYield's own three call sites (and their host test
 * coverage) untouched by this one's existence.
 *
 * Ordering and false-timeout-immunity are IDENTICAL to WaitLoop_SpinThenYield:
 * the status is always read before the budget is consulted, and the budget
 * branch takes a fresh read rather than reusing the retry loop's own check,
 * for the same preemption-gap reason. What is NOT identical is total CPU
 * spent once the fast path has missed: WaitLoop_SpinThenYield pays up to
 * @p spinCount + 1 status reads per yielded pass; this pays exactly 1.
 *
 * @return true if the status was ever observed met (including on the fresh
 *         read taken at expiry); false only on a genuine timeout.
 */
static inline bool WaitLoop_HoistedSpinThenYield(WaitLoop_PredicateFn statusMet,
                                                  WaitLoop_PredicateFn budgetSpent,
                                                  WaitLoop_YieldFn     yield,
                                                  void                *ctx,
                                                  uint32_t             spinCount)
{
    for (uint32_t s = 0; s < spinCount; ++s) {
        if (statusMet(ctx)) { return true; }
    }
    for (;;) {
        if (statusMet(ctx)) { return true; }
        if (budgetSpent(ctx)) {
            /* FRESH read, not a reuse of the one above: see the ordering
             * paragraph in WaitLoop_SpinThenYield's doc comment, which
             * applies identically here. */
            return statusMet(ctx);
        }
        yield(ctx);
    }
}

#endif /* WAITLOOP_H */
