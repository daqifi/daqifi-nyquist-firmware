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
     * runtime via DioProbe_AssignToChannel / SYST:DIOP:ROUT. */
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
        /* Ad-hoc clear: release pin + slot fields, then restore the
         * channel's user-DIO state. The original implementation skipped
         * DIO_WriteStateSingle on the assumption that ad-hoc probes only
         * touched DIO_10..DIO_15 (no user runtime config). With
         * DioProbe_AssignToChannel an ad-hoc probe can be remapped onto
         * any DIO 0..15, including standard channels that DO carry
         * runtime config — so the restore is required to avoid leaving
         * a user pin in probe-output state after an OFF.
         *
         * Read prev_ch and publish mode=OFF INSIDE the CS so a concurrent
         * AssignToChannel from the other SCPI task can't see a half-torn-
         * down slot or double-release the same pin. */
        volatile DioProbeSlot_t* s = &gDioProbeSlots[probeId];
        uint8_t prev_ch;
        taskENTER_CRITICAL();
        prev_ch = s->channel;
        s->mode = DIO_PROBE_MODE_OFF;
        if (prev_ch <= DIO_PROBE_MAX_DIO_CHANNEL) {
            probe_release_pin(prev_ch);
            gDioProbeOwnedMask &= (uint16_t)~(1u << prev_ch);
        }
        s->channel = 0xFF;
        s->mask = 0;
        taskEXIT_CRITICAL();
        if (prev_ch <= DIO_PROBE_MAX_DIO_CHANNEL) {
            (void)DIO_WriteStateSingle(prev_ch);
        }
        recompute_any_active();
        return true;
    }

    /* Preflight: validate target channel BEFORE any destructive state
     * change so a remap that's going to fail (channel out of range for
     * BoardConfig, or PWM active on the channel) doesn't tear down the
     * slot's existing working assignment. Mirrors the checks inside
     * probe_configure_pin so that helper becomes the redundant belt. */
    tBoardConfig* cfg = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    if (channel >= cfg->DIOChannels.Size) return false;
    {
        tBoardRuntimeConfig* rt =
            BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_ALL_CONFIG);
        if (rt->DIOChannels.Data[channel].IsPwmActive) {
            LOG_E("DioProbe: channel %u rejected (PWM active)",
                  (unsigned)channel);
            return false;
        }
    }
    const DIOConfig* dio = &cfg->DIOChannels.Data[channel];

    volatile DioProbeSlot_t* slot = &gDioProbeSlots[probeId];
    uint8_t prev_ch;

    /* Atomic check-and-reserve. Two concurrent SCPI tasks (USB pri 7
     * and WiFi pri 2) can both call AssignToChannel; if the ownership
     * check on gDioProbeOwnedMask runs outside a CS, both can pass it
     * for the same channel before either sets the owned bit. Hold the
     * CS across check + prev_ch release + new-channel reservation so
     * the second arrival sees the first's bit set and rejects. */
    taskENTER_CRITICAL();
    prev_ch = slot->channel;
    /* Refuse a channel a peripheral (SPI/#665, future I2C/UART/...) has claimed
     * — the mirror of DIO_ClaimChannel refusing a probe-owned pin. Checked
     * inside the CS so it's atomic vs a concurrent DIO_ClaimChannel on the
     * other SCPI task. UNCONDITIONAL (unlike the probe-vs-probe check below,
     * which allows re-assigning your own slot's channel): a peripheral owner is
     * never the probe, so if one exists the probe must not take the pin even if
     * this slot's prev_ch already equals it — otherwise SYST:DIOProbe:MODE/ROUTe
     * onto an SPI-owned pin drives it (asserting CS outside a transfer,
     * corrupting the slave) and creates dual ownership. */
    DioChannelOwner_t periphOwner = DIO_GetChannelOwner(channel);
    if (periphOwner != DIO_OWNER_NONE) {
        taskEXIT_CRITICAL();
        LOG_E("DioProbe: channel %u owned by %s",
              (unsigned)channel, DIO_ChannelOwnerName(periphOwner));
        return false;
    }
    if (prev_ch != channel && (gDioProbeOwnedMask & (1u << channel)) != 0u) {
        taskEXIT_CRITICAL();
        LOG_E("DioProbe: channel %u already owned by another probe",
              (unsigned)channel);
        return false;
    }
    /* Release prev_ch's pin and clear slot fields if remapping. */
    if (prev_ch != 0xFF && prev_ch != channel &&
        prev_ch <= DIO_PROBE_MAX_DIO_CHANNEL) {
        slot->mode = DIO_PROBE_MODE_OFF;
        probe_release_pin(prev_ch);
        gDioProbeOwnedMask &= (uint16_t)~(1u << prev_ch);
        slot->channel = 0xFF;
        slot->mask = 0;
    }
    /* Reserve the new channel. Visible to other SCPI tasks immediately
     * via the owned mask; their AssignToChannel calls will reject. */
    gDioProbeOwnedMask |= (uint16_t)(1u << channel);
    taskEXIT_CRITICAL();

    /* Restore prev_ch to its user-DIO state outside CS (the DIO HAL
     * uses its own mutex for this). */
    if (prev_ch != 0xFF && prev_ch != channel &&
        prev_ch <= DIO_PROBE_MAX_DIO_CHANNEL) {
        (void)DIO_WriteStateSingle(prev_ch);
    }

    /* Configure the new pin. Preflight passed, so this should succeed,
     * but DIO_ProbeActivatePair can still report failure (defensive
     * check inside DIO HAL). On failure, roll back the reservation and
     * recompute the any-active gate so it can't remain stale-true if
     * we were the last live probe. */
    if (!probe_configure_pin(channel)) {
        taskENTER_CRITICAL();
        gDioProbeOwnedMask &= (uint16_t)~(1u << channel);
        taskEXIT_CRITICAL();
        recompute_any_active();
        return false;
    }

    /* Publication protocol: clear mode first (ISR readers bail on OFF),
     * then critical section: rewrite fields + publish mode + set
     * any_active. Mode publish MUST be inside the CS so a concurrent
     * recompute_any_active on the other SCPI task can't observe mode=OFF
     * and stomp any_active=false. The owned-mask bit was already set
     * above as the reservation. */
    slot->mode = DIO_PROBE_MODE_OFF;

    taskENTER_CRITICAL();
    slot->port    = dio->DataChannel;
    slot->mask    = 1u << dio->DataBitPos;
    slot->channel = channel;
    slot->mode    = (uint8_t)mode;
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

    uint8_t channel;

    /* Read channel + publish mode=OFF + release pin + clear slot fields
     * all under one CS so a concurrent AssignToChannel on the other SCPI
     * task can't observe a half-torn-down slot or race us into double-
     * releasing the same pin. probe_release_pin only does SFR writes,
     * safe in CS. */
    taskENTER_CRITICAL();
    channel = slot->channel;
    slot->mode = DIO_PROBE_MODE_OFF;
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
