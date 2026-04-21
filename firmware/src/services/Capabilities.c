#define LOG_LVL LOG_LEVEL_DEBUG
#define LOG_MODULE LOG_MODULE_GENERAL
/*! @file Capabilities.c
 *
 * Builds capability summaries from the live tBoardConfig. See
 * Capabilities.h for API and issue #327 for the full framework.
 */

#include "Capabilities.h"

#include <string.h>

#include "state/board/BoardConfig.h"

void Capabilities_GetAinSummary(CapabilitiesAinSummary* out_summary) {
    if (out_summary == NULL) return;
    memset(out_summary, 0, sizeof(*out_summary));

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
            if (isPublic) out_summary->hasAD7609 = true;
        }

        if (!isPublic) continue;

        /* Bitmask index uses the DaqifiAdcChannelId (user-facing channel
         * number), not the board-config array index. Keeps the mask
         * meaningful to a client that already knows channels by ID.
         * Counts always increment so the invariant
         *   type1Count + type2Count == publicChannelCount
         * holds even for boards with channel IDs >= 16 where the
         * bitmask can't represent them. */
        uint8_t channelId = ch->DaqifiAdcChannelId;
        if (channelType == 1) {
            if (channelId < 16) {
                out_summary->type1Bitmask |= (uint16_t)(1u << channelId);
            }
            out_summary->type1Count++;
        } else {
            if (channelId < 16) {
                out_summary->type2Bitmask |= (uint16_t)(1u << channelId);
            }
            out_summary->type2Count++;
        }
        out_summary->publicChannelCount++;

        /* Primary resolution = whichever ADC type the public channels
         * are predominantly using. NQ1 is all MC12b (12-bit), NQ3 is
         * all AD7609 (18-bit). NQ2 mixes — if any AD7609 channel is
         * public, report the higher resolution. */
        if (channelBits > resolution) resolution = channelBits;
    }
    out_summary->primaryResolutionBits = resolution;
}

void Capabilities_GetDioSummary(CapabilitiesDioSummary* out_summary) {
    if (out_summary == NULL) return;
    memset(out_summary, 0, sizeof(*out_summary));

    /* Per CLAUDE.md, BoardConfig_Get indexes a static array populated at
     * boot and never returns NULL. No guard. */
    const tBoardConfig* cfg = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);

    const DIOArray* dio = &cfg->DIOChannels;
    out_summary->channelCount = (uint16_t)dio->Size;
    for (uint32_t i = 0; i < dio->Size; i++) {
        if (dio->Data[i].IsPwmCapable && i < 16) {
            out_summary->pwmCapableMask |= (uint16_t)(1u << i);
        }
    }
}
