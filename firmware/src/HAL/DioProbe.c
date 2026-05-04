#define LOG_LVL LOG_LEVEL_DEBUG
#define LOG_MODULE LOG_MODULE_GENERAL
/*! @file DioProbe.c
 *
 * Implementation of the DIO debug probe framework. See DioProbe.h for
 * full API and isolation contract.
 */

#include "DioProbe.h"
#include <string.h>
#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "Util/Logger.h"
#include "DIO.h"
#include "FreeRTOS.h"
#include "task.h"

/* ---- globals ---- */

volatile bool gDioProbeAnyActive = false;
volatile DioProbeSlot_t gDioProbeSlots[DIO_PROBE_SLOTS];
volatile uint16_t gDioProbeOwnedMask = 0;

/* ---- internal helpers ---- */

static bool probe_configure_pin(uint8_t channel) {
    tBoardConfig* cfg = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    tBoardRuntimeConfig* rt = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_ALL_CONFIG);
    if (channel >= cfg->DIOChannels.Size) return false;

    const DIORuntimeConfig* rtch = &rt->DIOChannels.Data[channel];

    /* Reject if PWM is currently active on this channel. PWM drives
     * the pin from OCMP at interrupt time — incompatible with probe. */
    if (rtch->IsPwmActive) {
        LOG_E("DioProbe: channel %u rejected (PWM active)", (unsigned)channel);
        return false;
    }

    /* Delegate data+enable pair handling to the DIO HAL. Keeps the
     * pair semantics (external driver chip needs both pins driven
     * together) in a single source of truth. */
    return DIO_ProbeActivatePair(channel);
}

static void probe_release_pin(uint8_t channel) {
    DIO_ProbeReleasePair(channel);
}

static void recompute_any_active(void) {
    /* Critical section: serializes this iteration with DioProbe_Assign
     * and DioProbe_Clear on the other SCPI task. Without it, Assign
     * on task B could publish mode=NEW right after our read of
     * gDioProbeSlots[i].mode returned OFF, and we would then stomp
     * Assign's gDioProbeAnyActive=true with a false. */
    bool any = false;
    taskENTER_CRITICAL();
    for (int i = 0; i < DIO_PROBE_SLOTS; ++i) {
        if (gDioProbeSlots[i].mode != DIO_PROBE_MODE_OFF) {
            any = true;
            break;
        }
    }
    gDioProbeAnyActive = any;
    taskEXIT_CRITICAL();
}

/* ---- public API ---- */

void DioProbe_Init(void) {
    memset((void*)gDioProbeSlots, 0, sizeof(gDioProbeSlots));
    for (int i = 0; i < DIO_PROBE_SLOTS; ++i) {
        gDioProbeSlots[i].channel = 0xFF;
    }
    gDioProbeOwnedMask = 0;
    gDioProbeAnyActive = false;

    /* Activate ad-hoc probes whose bit is set in the compile-time
     * enable mask. Default mapping is probe N -> DIO N; remap at
     * runtime via DioProbe_AssignToChannel / SYST:DIOP:MAP. */
#if (DIO_PROBE_ENABLE_MASK) != 0u
    for (uint8_t i = DIO_PROBE_ADHOC_FIRST; i < DIO_PROBE_SLOTS; ++i) {
        if (((DIO_PROBE_ENABLE_MASK) & (1u << i)) == 0u) continue;
        if (i > DIO_PROBE_MAX_DIO_CHANNEL) continue;
        if (!DioProbe_AssignToChannel(i, i, DIO_PROBE_MODE_TOGGLE)) {
            LOG_E("DioProbe: ad-hoc probe %u init failed", (unsigned)i);
        }
    }
#endif
}

bool DioProbe_Assign(uint8_t probeId, DioProbeMode_t mode) {
    if (probeId >= DIO_PROBE_STANDARD_COUNT) return false;
    if (mode == DIO_PROBE_MODE_OFF) return DioProbe_Clear(probeId);
    return DioProbe_AssignToChannel(probeId, probeId, mode);
}

bool DioProbe_AssignToChannel(uint8_t probeId, uint8_t channel,
                              DioProbeMode_t mode) {
    if (probeId >= DIO_PROBE_SLOTS) return false;
    if (channel > DIO_PROBE_MAX_DIO_CHANNEL) return false;
    if (mode == DIO_PROBE_MODE_OFF) {
        /* Standard probes go through the public Clear; ad-hoc slots
         * use the same teardown via the inline path below. */
        if (probeId < DIO_PROBE_STANDARD_COUNT) {
            return DioProbe_Clear(probeId);
        }
        /* Ad-hoc clear: release pin + slot fields. Mirrors DioProbe_Clear
         * but skips the DIO_WriteStateSingle restore (ad-hoc probes are
         * developer-instrumentation; user runtime config doesn't apply). */
        volatile DioProbeSlot_t* s = &gDioProbeSlots[probeId];
        uint8_t prev_ch = s->channel;
        s->mode = DIO_PROBE_MODE_OFF;
        taskENTER_CRITICAL();
        if (prev_ch <= DIO_PROBE_MAX_DIO_CHANNEL) {
            probe_release_pin(prev_ch);
            gDioProbeOwnedMask &= (uint16_t)~(1u << prev_ch);
        }
        s->channel = 0xFF;
        s->mask = 0;
        taskEXIT_CRITICAL();
        recompute_any_active();
        return true;
    }

    /* If this slot already owns a different channel, release the old
     * one first so the owned-mask invariant stays clean across remap. */
    volatile DioProbeSlot_t* slot = &gDioProbeSlots[probeId];
    uint8_t prev_ch = slot->channel;
    if (prev_ch != 0xFF && prev_ch != channel &&
        prev_ch <= DIO_PROBE_MAX_DIO_CHANNEL) {
        slot->mode = DIO_PROBE_MODE_OFF;
        taskENTER_CRITICAL();
        probe_release_pin(prev_ch);
        gDioProbeOwnedMask &= (uint16_t)~(1u << prev_ch);
        taskEXIT_CRITICAL();
        (void)DIO_WriteStateSingle(prev_ch);
    }

    /* Reject if the target channel is already owned by a DIFFERENT
     * probe slot (avoid two probes fighting over one pin). */
    if (prev_ch != channel && (gDioProbeOwnedMask & (1u << channel)) != 0u) {
        LOG_E("DioProbe: channel %u already owned by another probe",
              (unsigned)channel);
        return false;
    }

    if (!probe_configure_pin(channel)) {
        return false;
    }

    tBoardConfig* cfg = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    const DIOConfig* dio = &cfg->DIOChannels.Data[channel];

    /* Publication protocol: clear mode first (ISR readers bail on OFF),
     * then critical section: rewrite fields + publish mode + set
     * any_active. Mode publish MUST be inside the CS so a concurrent
     * recompute_any_active on the other SCPI task can't observe mode=OFF
     * and stomp any_active=false. */
    slot->mode = DIO_PROBE_MODE_OFF;

    taskENTER_CRITICAL();
    slot->port    = dio->DataChannel;
    slot->mask    = 1u << dio->DataBitPos;
    slot->channel = channel;
    slot->mode    = (uint8_t)mode;
    gDioProbeOwnedMask |= (uint16_t)(1u << channel);
    gDioProbeAnyActive = true;
    taskEXIT_CRITICAL();
    return true;
}

bool DioProbe_Clear(uint8_t probeId) {
    if (probeId >= DIO_PROBE_STANDARD_COUNT) return false;
    volatile DioProbeSlot_t* slot = &gDioProbeSlots[probeId];
    if (slot->mode == DIO_PROBE_MODE_OFF && slot->channel == 0xFF) {
        return true;  /* already clear */
    }

    uint8_t channel = slot->channel;

    /* Mode=OFF first (stops any further toggles from ISR context),
     * then wrap everything else in a single critical section: pin
     * release + owned mask clear + slot field reset all publish
     * together. Tightens the "owned" invariant — there's no window
     * where the mask says owned but the pin has already been reverted
     * to high-Z. probe_release_pin only does SFR writes, safe in CS. */
    slot->mode = DIO_PROBE_MODE_OFF;

    taskENTER_CRITICAL();
    if (channel <= DIO_PROBE_MAX_DIO_CHANNEL) {
        probe_release_pin(channel);
        gDioProbeOwnedMask &= (uint16_t)~(1u << channel);
    }
    slot->channel = 0xFF;
    slot->mask = 0;
    taskEXIT_CRITICAL();

    /* Ownership is cleared — re-apply the user's runtime-configured
     * DIO state so the channel returns to its pre-probe behavior
     * (direction, value, enable) without requiring an external SCPI
     * write from the host. */
    if (channel <= DIO_PROBE_MAX_DIO_CHANNEL) {
        (void)DIO_WriteStateSingle(channel);
    }

    recompute_any_active();
    return true;
}

void DioProbe_ClearAll(void) {
    for (uint8_t i = 0; i < DIO_PROBE_STANDARD_COUNT; ++i) {
        DioProbe_Clear(i);
    }
}

void DioProbe_SetPipeline(DioProbeMode_t mode) {
    for (uint8_t i = 0; i < DIO_PROBE_STANDARD_COUNT; ++i) {
        DioProbe_Assign(i, mode);
    }
}

void DioProbe_GetSlot(uint8_t probeId, DioProbeSlot_t* out) {
    if (out == NULL) return;
    if (probeId >= DIO_PROBE_SLOTS) {
        memset(out, 0, sizeof(*out));
        out->channel = 0xFF;
        return;
    }
    /* Critical section protects against a torn read if another SCPI
     * task (USB pri 7 vs WiFi pri 2) is concurrently modifying the
     * same slot via Assign/Clear. Copy fields explicitly rather than
     * using struct assignment from volatile — makes the volatile
     * loads explicit in generated code and portable across compilers. */
    DioProbeSlot_t tmp;
    taskENTER_CRITICAL();
    tmp.port    = gDioProbeSlots[probeId].port;
    tmp.mask    = gDioProbeSlots[probeId].mask;
    tmp.channel = gDioProbeSlots[probeId].channel;
    tmp.mode    = gDioProbeSlots[probeId].mode;
    taskEXIT_CRITICAL();
    *out = tmp;
}
