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

/* ---- globals ---- */

volatile bool gDioProbeAnyActive = false;
DioProbeSlot_t gDioProbeSlots[DIO_PROBE_SLOTS];
volatile uint16_t gDioProbeOwnedMask = 0;

/* ---- internal helpers ---- */

static bool probe_configure_pin(uint8_t channel) {
    tBoardConfig* cfg = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    tBoardRuntimeConfig* rt = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_ALL_CONFIG);
    if (cfg == NULL || rt == NULL) return false;
    if (channel >= cfg->DIOChannels.Size) return false;

    const DIOConfig* dio = &cfg->DIOChannels.Data[channel];
    const DIORuntimeConfig* rtch = &rt->DIOChannels.Data[channel];

    /* Reject if PWM is currently active on this channel. PWM drives
     * the pin from OCMP at interrupt time — incompatible with probe. */
    if (rtch->IsPwmActive) {
        LOG_E("DioProbe: channel %u rejected (PWM active)", (unsigned)channel);
        return false;
    }

    /* Drive LOW *before* setting direction to output. The pin starts
     * at a known level — scope triggers cleanly on first real edge. */
    uint32_t dataMask = 1u << dio->DataBitPos;
    uint32_t enMask   = 1u << dio->EnableBitPos;

    GPIO_PortClear(dio->DataChannel, dataMask);
    GPIO_PortOutputEnable(dio->DataChannel, dataMask);

    /* Activate driver. EnableInverted means active-low. */
    if (dio->EnableInverted) {
        GPIO_PortClear(dio->EnableChannel, enMask);
    } else {
        GPIO_PortSet(dio->EnableChannel, enMask);
    }
    GPIO_PortOutputEnable(dio->EnableChannel, enMask);
    return true;
}

static void probe_release_pin(uint8_t channel) {
    tBoardConfig* cfg = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    if (cfg == NULL) return;
    if (channel >= cfg->DIOChannels.Size) return;

    const DIOConfig* dio = &cfg->DIOChannels.Data[channel];
    uint32_t dataMask = 1u << dio->DataBitPos;

    /* Park pin LOW so the next stream sample reads a clean zero
     * until DIO_WriteStateSingle re-applies user config. */
    GPIO_PortClear(dio->DataChannel, dataMask);
}

static void recompute_any_active(void) {
    bool any = false;
    for (int i = 0; i < DIO_PROBE_SLOTS; ++i) {
        if (gDioProbeSlots[i].mode != DIO_PROBE_MODE_OFF) {
            any = true;
            break;
        }
    }
    gDioProbeAnyActive = any;
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

        DioProbeSlot_t* slot = &gDioProbeSlots[i];
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

    /* Write slot fields; mode LAST to publish. */
    DioProbeSlot_t* slot = &gDioProbeSlots[probeId];
    slot->port    = dio->DataChannel;
    slot->mask    = 1u << dio->DataBitPos;
    slot->channel = channel;
    slot->mode    = (uint8_t)mode;

    gDioProbeOwnedMask |= (1u << channel);
    gDioProbeAnyActive = true;  /* publish after all slot state valid */
    return true;
}

bool DioProbe_Clear(uint8_t probeId) {
    if (probeId >= DIO_PROBE_STANDARD_COUNT) return false;
    DioProbeSlot_t* slot = &gDioProbeSlots[probeId];
    if (slot->mode == DIO_PROBE_MODE_OFF && slot->channel == 0xFF) {
        return true;  /* already clear */
    }

    uint8_t channel = slot->channel;

    /* Stop toggles first, then drive the pin LOW, then clear ownership.
     * Readers seeing mode=OFF won't touch the pin even if they got a
     * torn copy of the slot. */
    slot->mode = DIO_PROBE_MODE_OFF;

    if (channel <= DIO_PROBE_MAX_DIO_CHANNEL) {
        probe_release_pin(channel);
        gDioProbeOwnedMask &= (uint16_t)~(1u << channel);
    }

    slot->channel = 0xFF;
    slot->mask = 0;
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
    *out = gDioProbeSlots[probeId];
}
