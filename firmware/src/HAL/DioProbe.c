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
     * enable mask. Each ad-hoc probe i maps to DIO channel i. */
#if (DIO_PROBE_ENABLE_MASK) != 0u
    for (uint8_t i = DIO_PROBE_ADHOC_FIRST; i < DIO_PROBE_SLOTS; ++i) {
        if (((DIO_PROBE_ENABLE_MASK) & (1u << i)) == 0u) continue;
        if (i > DIO_PROBE_MAX_DIO_CHANNEL) continue;

        uint8_t channel = i;
        if (!probe_configure_pin(channel)) {
            LOG_E("DioProbe: ad-hoc probe %u pin config failed", (unsigned)i);
            continue;
        }

        tBoardConfig* cfg = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
        const DIOConfig* dio = &cfg->DIOChannels.Data[channel];

        volatile DioProbeSlot_t* slot = &gDioProbeSlots[i];
        slot->port    = dio->DataChannel;
        slot->mask    = 1u << dio->DataBitPos;
        slot->channel = channel;
        slot->mode    = DIO_PROBE_MODE_TOGGLE;  /* publish last */

        gDioProbeOwnedMask |= (1u << channel);
    }
    recompute_any_active();
#endif
}

bool DioProbe_Assign(uint8_t probeId, DioProbeMode_t mode) {
    if (probeId >= DIO_PROBE_STANDARD_COUNT) return false;
    if (mode == DIO_PROBE_MODE_OFF) return DioProbe_Clear(probeId);

    uint8_t channel = probeId;  /* standard probes: probe N -> DIO N */

    if (!probe_configure_pin(channel)) {
        return false;
    }

    tBoardConfig* cfg = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    const DIOConfig* dio = &cfg->DIOChannels.Data[channel];

    /* Publication protocol:
     *   1. Clear mode to OFF (belt-and-suspenders — CS will block ISR
     *      readers anyway, but any read that slips before the CS sees
     *      OFF and bails)
     *   2. Single critical section: rewrite fields + masks + publish
     *      mode=NEW + set any_active. Mode publish MUST be inside the
     *      CS so recompute_any_active on the other SCPI task cannot
     *      observe mode=OFF and stomp any_active=false.
     *   3. Volatile fields (see DioProbeSlot_t) prevent the compiler
     *      from reordering writes out of the CS. */
    volatile DioProbeSlot_t* slot = &gDioProbeSlots[probeId];
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
