#define LOG_LVL LOG_LEVEL_DEBUG
#define LOG_MODULE LOG_MODULE_GENERAL
/*! @file Capabilities.c
 *
 * Derives capability summaries from the live tBoardConfig +
 * tBoardRuntimeConfig. See Capabilities.h for the API and issue
 * #327 for the framework vision.
 */

#include "Capabilities.h"

#include <string.h>

#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "services/streaming.h"

void Capabilities_GetAinSummary(CapabilitiesAinSummary* out) {
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));

    /* Per CLAUDE.md, BoardConfig_Get indexes a static array populated at
     * boot and never returns NULL. No guard. */
    const tBoardConfig* cfg = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);

    uint8_t resolution = 0;
    const AInArray* channels = &cfg->AInChannels;
    for (uint32_t i = 0; i < channels->Size; i++) {
        const AInChannel* ch = &channels->Data[i];

        bool isPublic = false;
        uint8_t channelType = 0;     /* 1 = Type 1, 2 = Type 2 (shared) */
        uint8_t channelBits = 0;
        if (ch->Type == AIn_MC12bADC) {
            isPublic = ch->Config.MC12b.IsPublic;
            channelType = (ch->Config.MC12b.ChannelType == 1) ? 1 : 2;
            channelBits = 12;
        } else if (ch->Type == AIn_AD7609) {
            isPublic = ch->Config.AD7609.IsPublic;
            channelType = 1;  /* AD7609 converts all 8 channels simultaneously */
            channelBits = 18;
            if (isPublic) out->hasAD7609 = true;
        }

        if (!isPublic) continue;

        if (channelType == 1) out->type1Count++;
        else                  out->type2Count++;
        out->publicChannelCount++;

        /* Primary resolution = whichever ADC type dominates the public
         * channel set. NQ1 all MC12b (12), NQ3 all AD7609 (18). NQ2
         * mixes — report the higher resolution when any AD7609
         * channel is public. */
        if (channelBits > resolution) resolution = channelBits;
    }
    out->primaryResolutionBits = resolution;
}

void Capabilities_GetAoutSummary(CapabilitiesAoutSummary* out) {
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));

    const tBoardConfig* cfg = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    out->publicChannelCount = (uint16_t)cfg->AOutChannels.Size;

    /* Pull resolution + voltage clamps from the first AOut module.
     * Current DAQiFi boards only ever have one AOut module
     * (DAC7718 on NQ3); NQ1 has AOutModules.Size == 0 which leaves
     * the defaults (0 channels, 0 resolution, 0..0 V). */
    if (cfg->AOutModules.Size > 0) {
        const AOutModule* mod = &cfg->AOutModules.Data[0];
        if (mod->Type == AOut_DAC7718) {
            /* Resolution is a counts value (e.g. 4096). Only power-of-two
             * counts map cleanly to a bit width; anything else means a
             * misconfigured board — report 0 bits so clients can flag
             * the device rather than silently rendering a wrong depth. */
            uint32_t counts = mod->Config.DAC7718.Resolution;
            uint8_t bits = 0;
            if (counts >= 2U && ((counts & (counts - 1U)) == 0U)) {
                while (counts > 1U) { counts >>= 1U; bits++; }
            }
            out->resolutionBits = bits;
            out->moduleMinVoltage = mod->Config.DAC7718.MinVoltage;
            out->moduleMaxVoltage = mod->Config.DAC7718.MaxVoltage;
        }
    }
}

void Capabilities_GetDioSummary(CapabilitiesDioSummary* out) {
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));

    const tBoardConfig* cfg = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    const DIOArray* dio = &cfg->DIOChannels;
    out->channelCount = (uint16_t)dio->Size;

    /* INVARIANT: DIO channel number == DIOChannels.Data[] index. The
     * DIOConfig struct has no explicit channel-ID field because the
     * entire firmware relies on this mapping (see DIOProbe, DIO HAL,
     * SCPI DIO callbacks). If a future board breaks this, add a
     * ChannelId field to DIOConfig first. */
    for (uint32_t i = 0; i < dio->Size; i++) {
        if (dio->Data[i].IsPwmCapable && i < 16) {
            out->pwmCapableMask |= (uint16_t)(1u << i);
        }
    }
}

void Capabilities_GetStreamingSummary(CapabilitiesStreamingSummary* out) {
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));

    uint16_t type1 = 0;
    uint16_t total = 0;
    Streaming_CountActiveChannels(&type1, &total, NULL);
    /* 0 public channels → no valid streaming rate. Reporting the
     * ISR ceiling in that state would advertise rates that
     * StartStreamData would reject. */
    /* Use ComputeMaxFreqForConfig (#524) so current_max_rate_hz matches the rate
     * StartStreamData actually applies — it now includes the per-interface,
     * per-format TRANSPORT cap, not just the channel-count/ADC terms. Without
     * this the capability query would advertise a higher rate than the device
     * delivers (clients pre-validate against current_max_rate_hz). */
    out->maxFreqHz = (total > 0) ? Streaming_ComputeMaxFreqForConfig() : 0;
    /* Qodo #595: advertise the variant-appropriate ISR ceiling. NQ1's basis
     * supports 22 kHz (2026-07-05 refit); NQ2/NQ3 remain characterized only
     * to the legacy 16 kHz and their legacy formula still enforces it -
     * exporting 22 kHz there would mislead client-side rate models. */
    const tBoardConfig* cfg = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    out->isrMaxHz      = (cfg != NULL && cfg->BoardVariant == 1u)
                             ? STREAMING_ISR_MAX_HZ
                             : STREAMING_ISR_MAX_HZ_LEGACY;
    out->type1AggMaxHz = STREAMING_TYPE1_AGG_MAX_HZ;
    out->tickBudget    = STREAMING_TICK_BUDGET;
    out->tickOverhead  = STREAMING_TICK_OVERHEAD;
}

void Capabilities_GetStorageSummary(CapabilitiesStorageSummary* out) {
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));

    const tBoardConfig* cfg = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    out->sdSupported      = cfg->CapabilitiesFlags.sdSupported;
    out->nvmSettingsSlots = cfg->CapabilitiesFlags.nvmSettingsSlots;
}

void Capabilities_GetPowerSummary(CapabilitiesPowerSummary* out) {
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));

    const tBoardConfig* cfg = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    out->batteryPresent         = cfg->CapabilitiesFlags.batteryPresent;
    out->externalPowerSupported = cfg->CapabilitiesFlags.externalPowerSupported;
    out->otgSupported           = cfg->CapabilitiesFlags.otgSupported;
}

void Capabilities_GetTransportsSummary(CapabilitiesTransportsSummary* out) {
    if (out == NULL) return;
    memset(out, 0, sizeof(*out));

    const tBoardConfig* cfg = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    out->usbSupported         = cfg->CapabilitiesFlags.usbSupported;
    out->wifiSupported        = cfg->CapabilitiesFlags.wifiSupported;
    out->ethernetSupported    = cfg->CapabilitiesFlags.ethernetSupported;
    out->serialDebugSupported = cfg->CapabilitiesFlags.serialDebugSupported;
}
