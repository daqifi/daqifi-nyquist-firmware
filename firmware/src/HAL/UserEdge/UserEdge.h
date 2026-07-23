/**
 * @file UserEdge.h
 * @brief Hardware edge events + pulse-count totalizers on the DIO terminal
 *        (#667, epic #664).
 *
 * Two independent capabilities on the user DIO-terminal input pins, so a digital
 * sensor's transitions can be counted and correlated with the analog stream:
 *
 *  1. EDGE EVENTS (external interrupts INT1..INT4). Each of the 4 INT units is
 *     PPS-routed to one DIO pin of its input group and fires on the selected
 *     edge. The ISR stamps the transition with the streaming timebase (TMR6, the
 *     same clock the ADC samples carry) into a shared drop-oldest FIFO of recent
 *     events and bumps a per-pin 64-bit edge counter. A per-unit storm guard mutes a pin that floods
 *     the ISR (>~32 edges/ms) so a fast line cannot starve the RTOS. Reachable
 *     pins (one active per group at a time — a shared-unit peripheral, so every
 *     mutating path guards active-pin == requested-pin):
 *       INT1 (group 4): DIO 0, 11        INT3 (group 1): DIO 5, 7, 15
 *       INT2 (group 3): DIO 3, 12        INT4 (group 2): DIO 2, 4, 6, 14
 *
 *  2. PULSE-COUNT TOTALIZERS (Timer8 / Timer9 in external-clock mode). Two 16-bit
 *     hardware counters, each extended to 48 bits by a rollover ISR, count every
 *     edge on their T8CK/T9CK pin IN HARDWARE — lossless at rates that would storm
 *     the INT path. T8CK and T9CK share their input groups with INT2/INT1
 *     respectively, so a pin is either an event source OR a totalizer (enforced by
 *     the DIO ownership registry, single owner DIO_OWNER_IC). Reachable pins:
 *       Timer8 (group 3): DIO 3, 12      Timer9 (group 4): DIO 0, 11
 *
 * Both families put the pin in peripheral-INPUT mode (buffer high-Z; the PIC reads
 * the +5V_D terminal through the 100K series resistor) and claim it via
 * DIO_OWNER_IC. Arming/disarming is REJECTED while streaming (a mid-stream PPS/
 * ownership change); once enabled they persist across a stream, which is the point
 * — that is when their timestamps line up with the samples. Reads (counts, next
 * event, totalizer) and totalizer CLEar are always allowed.
 *
 * Register semantics are device-header / FRM verified (DS60001108/DS60001105 +
 * the vendor plib_gpio CN reference for the interrupt idioms): single INTxR PPS
 * input select, INTCON.INTxEP edge polarity (1 = rising), TxCON.TCS=1 external
 * clock. TMR6 is owned + clocked by the streaming engine (TSTimerIndex =
 * TMR_INDEX_6) and only read here. Timer8/9 are otherwise unused and are held in
 * PMD (PMD4<T8MD,T9MD>) at boot — they MUST be PMD-ungated before their SFRs
 * respond (a PMD-gated module silently ignores writes and reads 0).
 *
 * Coexists with the #666 input-capture feature: both share DIO_OWNER_IC (a pin
 * cannot be a one-shot capture target and a persistent event source at once) but
 * use different hardware (IC1..9 + TMR2 there; INT1..4 + TMR8/9 + read-only TMR6
 * here), so they never contend for a timer. Within this file the event and
 * totalizer families ALSO share DIO_OWNER_IC, so each arm path cross-checks the
 * other (edge_EventActiveOnPin / edge_CounterActiveOnPin) because a same-owner
 * DIO claim succeeds idempotently and would not arbitrate them. NOTE for the epic
 * merge: #666 UserIC uses the same owner id, so the same cross-family idempotent-
 * claim hole exists against it — reconcile at merge (distinct owner ids per
 * IC-family feature, or a shared IC arbitration point).
 */
#ifndef USER_EDGE_H
#define USER_EDGE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Edge-event trigger mode for UserEdge_EventEnable. */
typedef enum {
    USER_EDGE_OFF     = 0,   /*!< disarm */
    USER_EDGE_RISING  = 1,   /*!< event on rising edges */
    USER_EDGE_FALLING = 2,   /*!< event on falling edges */
    USER_EDGE_BOTH    = 3,   /*!< event on both edges (best-effort — see .c) */
} UserEdgeMode_t;

/** Boot-time init: parks INT1..4 and Timer8/9 (disabled, IRQ off, priority set),
 *  clears the event FIFO and all counters. Safe to call pre-scheduler. */
void UserEdge_Initialize(void);

/**
 * Arm/disarm edge events on @p dio. @p mode is a UserEdgeMode_t (0 disarms).
 * Claims the pin (DIO_OWNER_IC), routes its INT unit's PPS input, and enables the
 * external-interrupt on the selected edge. Re-arming an already-armed pin updates
 * the mode and clears a latched storm.
 * @return false (reason in @p err) if @p dio is not INT-reachable, its INT unit is
 *   busy on another pin of the group, the pin is owned by another peripheral, or
 *   streaming is active.
 */
bool UserEdge_EventEnable(uint8_t dio, uint8_t mode, const char** err);

/** Current armed mode on @p dio (0 = off), for DIO:EVENt:ENAble?. */
/* 0 = off, 1/2/3 = armed & live (rising/falling/both), -1/-2/-3 = armed but
 * auto-muted by the edge-storm guard (magnitude = the mode); re-arm to resume. */
int8_t UserEdge_EventMode(uint8_t dio);

/** Accumulated edge count on @p dio since it was armed (0 if @p dio is not an
 *  active event pin), for DIO:EVENt:COUNt?. */
uint64_t UserEdge_EventCount(uint8_t dio);

/**
 * Pop the oldest event from the shared FIFO into @p dio / @p ts / @p edge
 * (edge: 1 = rising, 0 = falling; @p ts is the streaming timebase count). @p dropped
 * (if non-NULL) always receives the cumulative count of events lost to FIFO
 * drop-oldest overflow, so a client can detect gaps even when the pop succeeds.
 * @return true if an event was returned, false if the FIFO is empty.
 */
bool UserEdge_EventNext(uint8_t* dio, uint32_t* ts, uint8_t* edge, uint32_t* dropped);

/**
 * Arm/disarm the hardware pulse-count totalizer on @p dio.
 * @return false (reason in @p err) if @p dio is not totalizer-reachable, its timer
 *   is busy on the other group pin, the pin is owned, the timer failed to power up
 *   (PMD), or streaming is active.
 */
bool UserEdge_CounterEnable(uint8_t dio, bool on, const char** err);

/** True if a totalizer is currently armed on @p dio (DIO:COUNter:ENAble?). */
bool UserEdge_CounterEnabled(uint8_t dio);

/** Hardware totalizer count on @p dio (0 if not armed), for DIO:COUNter?. */
uint64_t UserEdge_CounterGet(uint8_t dio);

/** Reset the totalizer on @p dio to 0. @return false if @p dio is not an armed
 *  totalizer pin. Allowed while streaming (harmless hardware reset). */
bool UserEdge_CounterClear(uint8_t dio);

/* --- ISR bodies (called from the vector handlers in interrupts.c) --- */

/** External-interrupt ISR body for INT unit @p unit (0..3): stamp + enqueue the
 *  edge, bump the counter, apply the storm guard. No float, no blocking. */
void UserEdge_IsrEvent(uint8_t unit);

/** Timer8/Timer9 rollover ISR body for totalizer @p unit (0 = T8, 1 = T9):
 *  extend the 16-bit hardware count by one 65536-count epoch. */
void UserEdge_IsrCounterRollover(uint8_t unit);

#ifdef __cplusplus
}
#endif

#endif /* USER_EDGE_H */
