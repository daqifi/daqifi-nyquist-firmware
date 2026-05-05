/*! @file DioProbe.h
 *
 * Lightweight DIO debug probe framework for pipeline timing analysis.
 *
 * When a DIO channel is assigned to a probe, the normal DIO write path
 * (DIO_WriteStateSingle, DIO_WriteStateAll, DIO_StreamingTrigger loop,
 * DIO_ReadSampleByMask, PWM functions, SCPI user-DIO commands) is
 * completely blocked for that channel. The probe owns both the data
 * pin TRIS/LAT and the enable pin. This isolation is PARAMOUNT — a
 * single spurious edge from another code path would make scope
 * measurements untrustworthy.
 *
 * Two probe classes (distinguished by who initiates them, NOT by which
 * DIO they drive — channel mapping is fully flexible at runtime):
 *
 * 1) STANDARD probes 0..9 — fired by `DioProbe_Toggle(N)` calls in
 *    the streaming/ADC pipeline. Default mapping is probe N -> DIO N
 *    via SCPI `SYSTem:DIOProbe:MODE <probe>,<OFF|TOGGLE|PULSE>`.
 *    Use `SYSTem:DIOProbe:ROUTe <probe>,<channel>,<mode>` to redirect
 *    any standard probe to any DIO (handy when the LA isn't wired
 *    to DIO_0..DIO_9).
 *
 * 2) AD-HOC probes 10..15 — compile-time instrumentation. Drop
 *    `DIO_PROBE_TOGGLE(n)` or `DIO_PROBE_PULSE_START(n)/END(n)` into
 *    any code path, set the corresponding bit in
 *    `DIO_PROBE_ENABLE_MASK`, recompile. Default mapping at boot is
 *    probe N -> DIO N. Override at runtime with `SYST:DIOP:ROUT` to
 *    route the probe to whichever DIO is wired to the LA.
 *
 * Hot-path cost when disabled: one load + branch-if-zero. Zero cost
 * for ad-hoc probes whose bit is not set in DIO_PROBE_ENABLE_MASK.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "../config/default/peripheral/gpio/plib_gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/*! Probe mode. OFF = inert; TOGGLE = single flip per call; PULSE = SET
 *  on _Toggle/_PulseStart call, CLEAR on _PulseEnd call. */
typedef enum {
    DIO_PROBE_MODE_OFF    = 0,
    DIO_PROBE_MODE_TOGGLE = 1,
    DIO_PROBE_MODE_PULSE  = 2
} DioProbeMode_t;

/*! Per-probe slot. Written by the SCPI task; read by many contexts
 *  including priority-1 ISRs. Fields are volatile so the compiler
 *  cannot reorder or cache accesses across the publish-mode-last /
 *  read-mode-first contract. */
typedef struct {
    volatile GPIO_PORT port;       //!< Data pin GPIO port
    volatile uint32_t  mask;       //!< 1u << data bit position
    volatile uint8_t   channel;    //!< DIO channel index (0..15); 0xFF = unassigned
    volatile uint8_t   mode;       //!< DioProbeMode_t
} DioProbeSlot_t;

#define DIO_PROBE_SLOTS            16  //!< probe IDs 0..15
#define DIO_PROBE_STANDARD_COUNT   10  //!< probes 0..9 SCPI-controlled
#define DIO_PROBE_ADHOC_FIRST      10  //!< probes 10..15 compile-time
#define DIO_PROBE_MAX_DIO_CHANNEL  15  //!< last valid DIO channel index

/*! Compile-time enable mask for ad-hoc probes. Default 0 = all ad-hoc
 *  probe calls expand to nothing. Developer sets bits in BoardConfig.h
 *  or on the compiler command line for probes they want live. */
#ifndef DIO_PROBE_ENABLE_MASK
#define DIO_PROBE_ENABLE_MASK 0x0000u
#endif

/* ---- globals (defined in DioProbe.c) ---- */

/*! Fast-path gate. When false, all probe calls short-circuit after
 *  one load. Written last when any slot is activated. */
extern volatile bool gDioProbeAnyActive;

/*! Slot table. Single-writer (SCPI task); readers do atomic struct
 *  copies on PIC32MZ's 32-bit bus. Volatile to match the per-field
 *  qualification in DioProbeSlot_t. */
extern volatile DioProbeSlot_t gDioProbeSlots[DIO_PROBE_SLOTS];

/*! Bitmask of DIO channels currently owned by a probe. Checked by
 *  DIO.c write paths to skip stomping the pin. */
extern volatile uint16_t gDioProbeOwnedMask;

/* ---- public API (SCPI task + boot) ---- */

/*! Zero all slots, masks, flags. Also activates any ad-hoc probes
 *  whose bit is set in DIO_PROBE_ENABLE_MASK. Call AFTER
 *  DIO_InitHardware so default DIO writes settle first. */
void DioProbe_Init(void);

/*! Assign a standard probe (0..9) to a mode using the default 1:1
 *  mapping (channel = probeId). Thin wrapper around
 *  DioProbe_AssignToChannel for backward compatibility.
 *  @return true on success */
bool DioProbe_Assign(uint8_t probeId, DioProbeMode_t mode);

/*! Assign ANY probe slot (0..15) to ANY DIO channel (0..15) at runtime.
 *  Decouples probe ID from physical pin so the caller can route a
 *  probe to whichever DIO is wired to the logic analyzer.
 *
 *  Releases the slot's previous channel (if any) before claiming the
 *  new one. Forces the new pin LOW, configures it as output with
 *  driver enabled. Rejects if PWM is active on the target channel.
 *
 *  Use cases:
 *    - SCPI `SYST:DIOP:ROUT <probeId>,<channel>,<mode>` for ad-hoc
 *      remapping during debug.
 *    - Re-routing standard probes 0..9 onto wired DIOs when the LA
 *      doesn't reach DIO_0..DIO_9.
 *    - Configuring ad-hoc probes 10..15 to drive arbitrary DIOs
 *      (overrides the default DIO_PROBE_ENABLE_MASK auto-init mapping).
 *  @return true on success */
bool DioProbe_AssignToChannel(uint8_t probeId, uint8_t channel,
                              DioProbeMode_t mode);

/*! Release a standard probe. Drives pin LOW, clears ownership,
 *  returns channel to normal DIO control. */
bool DioProbe_Clear(uint8_t probeId);

/*! Clear all standard probes (ad-hoc are not affected). */
void DioProbe_ClearAll(void);

/*! Set all 10 standard probes to the given mode in one call.
 *  Convenience for capturing the whole pipeline at once. */
void DioProbe_SetPipeline(DioProbeMode_t mode);

/*! Lookup: is this DIO channel currently owned by a probe?
 *  Used by DIO.c and SCPIDIO.c to enforce isolation. */
static inline bool DioProbe_IsChannelOwned(uint8_t channel) {
    if (channel > DIO_PROBE_MAX_DIO_CHANNEL) {
        return false;  /* shift by >=16 bits would be undefined behavior */
    }
    return (gDioProbeOwnedMask & (uint16_t)(1u << channel)) != 0;
}

/*! Remove owned bits from a DIO channel read mask. */
static inline uint32_t DioProbe_FilterReadMask(uint32_t mask) {
    return mask & ~(uint32_t)gDioProbeOwnedMask;
}

/*! Snapshot one slot (for LIST/ASS? SCPI). */
void DioProbe_GetSlot(uint8_t probeId, DioProbeSlot_t* out);

/* ---- hot path (called from pipeline) ---- */

/*! Fire a probe event. TOGGLE: flip pin. PULSE: drive pin HIGH.
 *  No-op if gDioProbeAnyActive is false or slot is OFF.
 *
 *  Tear-safe read order: load mode first, bail on OFF before reading
 *  port/mask. The writer publishes mode last (after fields are fully
 *  written) on assign, and clears mode first on clear — so reading
 *  mode before port/mask guarantees we only use fields that are
 *  committed when mode is ACTIVE. */
static inline void DioProbe_Toggle(uint8_t probeId) {
    if (!gDioProbeAnyActive) return;
    if (probeId >= DIO_PROBE_SLOTS) return;

    uint8_t mode = gDioProbeSlots[probeId].mode;
    if (mode == DIO_PROBE_MODE_OFF) return;

    GPIO_PORT port = gDioProbeSlots[probeId].port;
    uint32_t  mask = gDioProbeSlots[probeId].mask;

    if (mode == DIO_PROBE_MODE_TOGGLE) {
        GPIO_PortToggle(port, mask);
    } else { /* DIO_PROBE_MODE_PULSE */
        GPIO_PortSet(port, mask);
    }
}

/*! Close a PULSE. No-op if the slot is not in PULSE mode. */
static inline void DioProbe_PulseEnd(uint8_t probeId) {
    if (!gDioProbeAnyActive) return;
    if (probeId >= DIO_PROBE_SLOTS) return;

    if (gDioProbeSlots[probeId].mode != DIO_PROBE_MODE_PULSE) return;

    GPIO_PortClear(gDioProbeSlots[probeId].port, gDioProbeSlots[probeId].mask);
}

/*! Alias — reads better at call sites that start a pulse. */
static inline void DioProbe_PulseStart(uint8_t probeId) {
    DioProbe_Toggle(probeId);
}

/* ---- ad-hoc compile-time macros ----
 *
 * These compile to nothing when the probe's bit is not set in
 * DIO_PROBE_ENABLE_MASK. Use sparingly — the default is all-zero
 * so a forgotten ad-hoc probe doesn't leak into production. */

#define DIO_PROBE_TOGGLE(id) \
    do { \
        if ((DIO_PROBE_ENABLE_MASK) & (1u << (id))) { \
            DioProbe_Toggle((uint8_t)(id)); \
        } \
    } while (0)

#define DIO_PROBE_PULSE_START(id) DIO_PROBE_TOGGLE(id)

#define DIO_PROBE_PULSE_END(id) \
    do { \
        if ((DIO_PROBE_ENABLE_MASK) & (1u << (id))) { \
            DioProbe_PulseEnd((uint8_t)(id)); \
        } \
    } while (0)

#ifdef __cplusplus
}
#endif
