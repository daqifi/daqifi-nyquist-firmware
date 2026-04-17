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
 * Two probe classes:
 *
 * 1) STANDARD probes 0..9 — permanently wired to streaming pipeline
 *    stages. Mapped 1:1 to DIO_0..DIO_9. Assigned at runtime via
 *    SCPI SYST:DIOP:ASS <probe>,<mode> (caps-only) or long form
 *    SYSTem:DIOProbe:ASSign.
 *
 * 2) AD-HOC probes 10..15 — compile-time instrumentation. Drop
 *    `DIO_PROBE_TOGGLE(n)` or `DIO_PROBE_PULSE_START(n)/END(n)` into
 *    any code path, set the corresponding bit in
 *    `DIO_PROBE_ENABLE_MASK`, recompile. Mapped 1:1 to DIO_10..DIO_15.
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

/*! Per-probe slot. Written only by the SCPI task; read by many
 *  contexts. Publish order: fill port/mask/channel first, mode last. */
typedef struct {
    GPIO_PORT port;       //!< Data pin GPIO port
    uint32_t  mask;       //!< 1u << data bit position
    uint8_t   channel;    //!< DIO channel index (0..15); 0xFF = unassigned
    uint8_t   mode;       //!< DioProbeMode_t
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
 *  copies on PIC32MZ's 32-bit bus. */
extern DioProbeSlot_t gDioProbeSlots[DIO_PROBE_SLOTS];

/*! Bitmask of DIO channels currently owned by a probe. Checked by
 *  DIO.c write paths to skip stomping the pin. */
extern volatile uint16_t gDioProbeOwnedMask;

/* ---- public API (SCPI task + boot) ---- */

/*! Zero all slots, masks, flags. Also activates any ad-hoc probes
 *  whose bit is set in DIO_PROBE_ENABLE_MASK. Call AFTER
 *  DIO_InitHardware so default DIO writes settle first. */
void DioProbe_Init(void);

/*! Assign a standard probe (0..9) to a mode. Channel = probeId.
 *  Forces pin LOW then configures as output with driver enabled.
 *  Rejects if PWM is active on the target channel.
 *  @return true on success */
bool DioProbe_Assign(uint8_t probeId, DioProbeMode_t mode);

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
 *  No-op if gDioProbeAnyActive is false or slot is OFF. */
static inline void DioProbe_Toggle(uint8_t probeId) {
    if (!gDioProbeAnyActive) return;
    if (probeId >= DIO_PROBE_SLOTS) return;
    /* Struct copy is a few LW instructions; safe against concurrent
     * assign thanks to mode-published-last rule. */
    const DioProbeSlot_t s = gDioProbeSlots[probeId];
    if (s.mode == DIO_PROBE_MODE_TOGGLE) {
        GPIO_PortToggle(s.port, s.mask);
    } else if (s.mode == DIO_PROBE_MODE_PULSE) {
        GPIO_PortSet(s.port, s.mask);
    }
}

/*! Close a PULSE. No-op if the slot is not in PULSE mode. */
static inline void DioProbe_PulseEnd(uint8_t probeId) {
    if (!gDioProbeAnyActive) return;
    if (probeId >= DIO_PROBE_SLOTS) return;
    const DioProbeSlot_t s = gDioProbeSlots[probeId];
    if (s.mode == DIO_PROBE_MODE_PULSE) {
        GPIO_PortClear(s.port, s.mask);
    }
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
