#define LOG_LVL LOG_LEVEL_SCPI
#define LOG_MODULE LOG_MODULE_SCPI
#include "SCPIADC.h"
#include "SCPIInterface.h"

// General
#include <stdlib.h>
#include <string.h>

// Harmony
#include "configuration.h"
#include "definitions.h"

// Project
#include "Util/StringFormatters.h"
#include "Util/Logger.h"
#include "state/data/BoardData.h"
#include "state/board/BoardConfig.h"
#include "HAL/ADC/MC12bADC.h"
#include "HAL/ADC/AdcThreshold.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "HAL/ADC.h"
#include "../daqifi_settings.h"
#include "HAL/TimerApi/TimerApi.h"
#include "../streaming.h"

// FreeRTOS
#include "FreeRTOS.h"
#include "task.h"

scpi_result_t SCPI_ADCVoltageGet(scpi_t * context) {
    int channel;
    AInSample *pAInLatest;
    uint32_t *pAInLatestSize;
    AInArray * pBoardConfigAInChannels = BoardConfig_Get(
            BOARDCONFIG_AIN_CHANNELS,
            0);
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_AIN_CHANNELS);
    StreamingRuntimeConfig *pStreamCfg = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);
    uint8_t precision = (pStreamCfg != NULL) ? pStreamCfg->VoltagePrecision : 4;

    // #874: ABSENT selects the all-channel form; PRESENT-but-unparseable must
    // not -- see SCPI_OptionalParamInt32 (SCPIInterface.h) for the contract.
    // `MEAS:VOLT:DC? BANANA` used to answer with every channel's voltage,
    // which a client reading one value parses as its channel's.
    SCPI_OptionalParam chanOpt = SCPI_OptionalParamInt32(context, &channel);
    if (chanOpt == SCPI_OPT_BAD) {
        return SCPI_RES_ERR;
    }
    if (chanOpt == SCPI_OPT_PRESENT) {
        // Get single
        volatile double val = 0;
        if (channel < 0 || channel > 255) {
            SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
            return SCPI_RES_ERR;
        }
        uint8_t ch = (uint8_t)channel;
        size_t index = ADC_FindChannelIndex(ch);
        if (index >= pBoardConfigAInChannels->Size) {
            SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
            return SCPI_RES_ERR;
        }

        // Monitoring channels (ID >= 248) are not being refreshed when
        // OBDiag is disabled during active streaming — return an error so
        // the user knows the reading would be stale/meaningless. Gate on
        // Running (actual hardware state) rather than IsEnabled (user
        // intent) so the guard lifts the instant Streaming_Stop re-enables
        // the EOS interrupt, even if IsEnabled is still mid-reconfig.
        if (ch >= ADC_CHANNEL_3_3V &&
            pStreamCfg->Running && !pStreamCfg->OnboardDiagEnabled) {
            LOG_E("MEAS:VOLT:DC? ch%d: monitoring disabled (OBDiag=0 during streaming)", (int)ch);
            SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
            return SCPI_RES_ERR;
        }

        if (!pRuntimeAInChannels->Data[index].IsEnabled) {
            SCPI_ResultVoltage(context, 0.0, precision);
            return SCPI_RES_OK;
        }

        pAInLatest = BoardData_Get(
                BOARDDATA_AIN_LATEST,
                index);

        if (pAInLatest == NULL) {
            SCPI_ResultVoltage(context, 0.0, precision);
            return SCPI_RES_OK;
        }

        val = ADC_ConvertToVoltage(pAInLatest);
        SCPI_ResultVoltage(context, val, precision);
    } else {
        // Get all
        size_t i = 0;

        pAInLatestSize = BoardData_Get(
                BOARDDATA_AIN_LATEST_SIZE,
                0);

        for (i = 0; i<*pAInLatestSize; ++i) {
            pAInLatest = BoardData_Get(
                    BOARDDATA_AIN_LATEST,
                    i);

            if (!pRuntimeAInChannels->Data[i].IsEnabled ||
                    pAInLatest->Timestamp < 1) {
                SCPI_ResultVoltage(context, 0.0, precision);
            } else {
                double val = ADC_ConvertToVoltage(pAInLatest);
                SCPI_ResultVoltage(context, val, precision);
            }
        }
    }

    return SCPI_RES_OK;
}

static scpi_result_t ADCChanEnableSetClaimed(scpi_t * context);

/* #847: the claim is taken HERE and released on the single path out, so no
 * error return inside the body can leak it. The body is 259 lines with twelve
 * returns; wrapping it is what makes the release provably unconditional.
 *
 * The claim replaces the old inline stream-state guard, in the same position,
 * so the order in which this command reports its errors is unchanged. */
scpi_result_t SCPI_ADCChanEnableSet(scpi_t * context) {
    StreamingCfgClaim claim = Streaming_BeginConfigChange();
    if (claim != STREAM_CFG_CLAIM_OK) {
        return SCPI_RejectCfgClaim(context,
                                   claim == STREAM_CFG_CLAIM_BUSY,
                                   "CONF:ADC:CHANnel");
    }
    scpi_result_t result = ADCChanEnableSetClaimed(context);
    Streaming_EndConfigChange();
    return result;
}

static scpi_result_t ADCChanEnableSetClaimed(scpi_t * context) {
    int param1, param2;
    StreamingRuntimeConfig * pRunTimeStreamConfig = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);
    const tBoardConfig * pBoardConfig = BoardConfig_Get(
            BOARDCONFIG_ALL_CONFIG, 0);
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_AIN_CHANNELS);
    AInArray * pBoardConfigAInChannels = BoardConfig_Get(
            BOARDCONFIG_AIN_CHANNELS,
            0);

    // The stream-state rejection for this command is in the WRAPPER above, not
    // here: Streaming_BeginConfigChange() performs it and takes the claim in
    // one critical section (#847). The rationale it enforces is #116's, and is
    // kept here because it is about THIS body:
    //
    // #116: reject channel enable/disable while streaming is active. The sample
    // pool element stride is fixed at StartStreamData for the then-current channel
    // count (AInSampleList_InitializeExternal is only called at stream start, never
    // mid-stream), so changing the channel set live would desync the pool layout
    // from the ISR write width. The old behavior here silently re-capped the
    // frequency (LOG_I only) without re-partitioning the pool — unsound and
    // invisible to the client. The contract is: stop streaming, reconfigure,
    // restart -- the same one every SYST:MEM:* command states, and since #857
    // they reach it through this same claim. (This sentence used to say "mirror
    // SCPI_MemRejectIfStreaming", which #857 deleted; that guard was also the
    // one hand-rolling the && form this comment goes on to warn against, so it
    // was the wrong thing to point at even while it existed.)
    // Rejecting BEFORE ADC_WriteChannelStateAll() leaves both
    // runtime config and ADC hardware untouched (no snapshot/rollback needed).
    //
    // The claim tests IsEnabled || Running -- never && -- because the two flags
    // are set/cleared in separate steps at stream start/stop (StartStreaming
    // arms IsEnabled, then Streaming_UpdateState flips Running; stop clears
    // them in turn), so an && test would read false for the whole interval
    // between them and a concurrent SCPI session (USB pri 7 vs WiFi pri 2)
    // could slip a channel change through after the pool/mapping was sized.

    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }

    // #874: this is THE bug the issue was filed for. ABSENT selects the
    // one-argument MASK form; PRESENT-but-unparseable must not -- see
    // SCPI_OptionalParamInt32 (SCPIInterface.h). Before this guard,
    // `CONF:ADC:CHAN 0,BANANA` (and the same command's other spelling,
    // `ENAble:VOLTage:DC 0,BANANA`) queued -104 and then ran the mask form
    // with param1 as the mask -- mask 0 disables EVERY analog channel, and the
    // client was never told its channel configuration had been replaced.
    // Measured on hardware: mask 7 -> `CONF:ADC:CHAN 0,BANANA` -> mask 0.
    SCPI_OptionalParam stateOpt = SCPI_OptionalParamInt32(context, &param2);
    if (stateOpt == SCPI_OPT_BAD) {
        return SCPI_RES_ERR;
    }
    if (stateOpt == SCPI_OPT_PRESENT) {
        // Single-channel form: (channel, state). NOT a bitmask — the one-arg
        // form CONF:ADC:CHAN <mask> is the bitmask path (see #630).

        // #678: reject a channel value that would be TRUNCATED by the (uint8_t)
        // cast below — i.e. outside [0,255] — BEFORE the cast, so a value >= 256
        // cannot alias mod-256 onto a valid user channel (256->0, 257->1, ...
        // 271->15), which the old code silently enabled/disabled returning OK.
        // Scope the guard to the truncation range ONLY (> 255): values in
        // [0,255] are NOT truncated and fall through to the existing #630
        // resolved-index guard + variant switch, which reject non-settable ids
        // (16..255) with their own "not addressable" message. Narrowing to > 255
        // (was `> maxUserChannel`, #678 follow-up) keeps this guard from usurping
        // #630's gap-id handling — the broader form intercepted 16..255 and
        // replaced #630's message, and its longer text truncated past the 128 B
        // log-buffer width, hiding the hint (regressed test_630). Message kept
        // short so the hint survives untruncated.
        if (param1 < 0 || param1 > 255) {
            LOG_E("CONF:ADC:CHAN: channel %d out of range (max 255); use one-arg "
                  "<mask> to enable by bitmask", param1);
            SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
            return SCPI_RES_ERR;
        }

        size_t channelIndex = ADC_FindChannelIndex((uint8_t) param1);

        // #630: bounds-check BEFORE dereferencing. ADC_FindChannelIndex returns
        // (size_t)-1 for an id not present in the channel table (e.g.
        // CONF:ADC:CHAN 16,1 on NQ1, or 65535,1 → id 255), and the old code
        // read channel->Type at Data[(size_t)-1] — a wild OOB read — before the
        // range check caught it. Guard first, then it is safe to index.
        if (channelIndex >= (size_t) pBoardConfigAInChannels->Size) {
            // Note: do NOT report "valid 0..Size-1" — Size is the array entry
            // count (user + monitoring), not the settable channel-id range,
            // which is sparse (NQ1 user 0..15, monitoring 248..255; NQ3 0..7).
            // A numeric range here would be wrong per-variant (#630 review).
            LOG_E("CONF:ADC:CHAN: channel %d not addressable (not a settable "
                  "analog channel). The two-arg form is <channel>,<state>; use "
                  "the one-arg <mask> form to enable channels by bitmask.",
                  param1);
            // Push a specific error (not the libscpi-default generic -200) so
            // the failure is classifiable via SYST:ERR? too — consistent with
            // the DIO boundary rejects (#671) and the ADCVoltageGet path above.
            SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
            return SCPI_RES_ERR;
        }

        AInRuntimeConfig* channelRuntimeConfig =
                &pRuntimeAInChannels->Data[channelIndex];
        AInChannel* channel = &pBoardConfigAInChannels->Data[channelIndex];
        const AInModule* module = ADC_FindModule(channel->Type);

        // Board variant-aware channel enable logic
        uint8_t boardVariant = pBoardConfig->BoardVariant;
        uint8_t channelId = (uint8_t) param1;
        
        switch (boardVariant) {
            case 1: // NQ1: User channels 0-15 (MC12bADC), monitoring channels always on
                if (channelId <= 15) {
                    if (module->Type == AIn_MC12bADC && channel->Config.MC12b.IsPublic) {
                        channelRuntimeConfig->IsEnabled = (param2 > 0);
                    } else {
                        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
                        return SCPI_RES_ERR; // Private or wrong type
                    }
                } else {
                    // #682 gate: monitoring-channel ids (248..255) resolve to a valid
                    // table index — so the #630 guard above does NOT fire — but are
                    // not user-settable. Push the specific error so they reject with
                    // -222 like the gap-ids, not libscpi's default -200 (the narrowed
                    // #678 guard newly lets these reach this branch).
                    SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
                    return SCPI_RES_ERR; // Monitoring channels not user-controllable
                }
                break;
                
            case 3: // NQ3: User channels 0-7 (AD7609), monitoring channels always on
                if (channelId <= 7) {
                    if (module->Type == AIn_AD7609) {
                        channelRuntimeConfig->IsEnabled = (param2 > 0);
                    } else {
                        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
                        return SCPI_RES_ERR; // Wrong type for NQ3 user channels
                    }
                } else {
                    // #682 gate: monitoring ids reject with -222 (not default -200).
                    SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
                    return SCPI_RES_ERR; // Monitoring channels not user-controllable
                }
                break;
                
            default: // NQ2 or unknown variants
                // Legacy behavior for compatibility
                if (module->Type == AIn_MC12bADC) {
                    if (channel->Config.MC12b.IsPublic) {
                        channelRuntimeConfig->IsEnabled = (param2 > 0);
                    } else {
                        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
                        return SCPI_RES_ERR; // Private channel — not settable
                    }
                } else {
                    channelRuntimeConfig->IsEnabled = (param2 > 0);
                }
                break;
        }
    } else {
        // Channel mask - board variant-aware bulk enable
        uint8_t boardVariant = pBoardConfig->BoardVariant;
        uint8_t maxUserChannel = (boardVariant == 3) ? 7 : 15; // NQ3: 0-7, others: 0-15
        
        for (size_t index = 0; index <= maxUserChannel; ++index) {
            size_t channelIndex = ADC_FindChannelIndex((uint8_t) index);
            if (channelIndex < pBoardConfigAInChannels->Size) {
                AInRuntimeConfig* channelRuntimeConfig =
                        &pRuntimeAInChannels->Data[channelIndex];
                AInChannel* channel = &pBoardConfigAInChannels->Data[channelIndex];
                const AInModule* module = ADC_FindModule(channel->Type);
                bool value = (bool) ((param1 & (1 << index)) > 0);

                switch (boardVariant) {
                    case 1: // NQ1: MC12bADC user channels 0-15
                        if (module->Type == AIn_MC12bADC && channel->Config.MC12b.IsPublic) {
                            channelRuntimeConfig->IsEnabled = value;
                        }
                        break;
                        
                    case 3: // NQ3: AD7609 user channels 0-7
                        if (module->Type == AIn_AD7609) {
                            channelRuntimeConfig->IsEnabled = value;
                        }
                        break;
                        
                    default: // NQ2 or legacy
                        if (module->Type == AIn_MC12bADC) {
                            if (channel->Config.MC12b.IsPublic) {
                                channelRuntimeConfig->IsEnabled = value;
                            }
                        } else {
                            channelRuntimeConfig->IsEnabled = value;
                        }
                        break;
                }
            }
        }
        // Note: Monitoring channels (>maxUserChannel) are always enabled and not user-controllable
    }
    uint16_t activeType1ChannelCount = 0;
    uint16_t totalEnabledPublicChannels = 0;
    Streaming_CountActiveChannels(&activeType1ChannelCount,
                                  &totalEnabledPublicChannels,
                                  NULL);
    uint64_t freq = pRunTimeStreamConfig->Frequency;
    uint32_t clkFreq = TimerApi_FrequencyGet(pBoardConfig->StreamingConfig.TimerIndex);

    // Frequency capping (see streaming.h) — includes the WiFi wire-rate term
    // when ActiveInterface==WiFi on top of the ADC/ISR/tick constraints (#522).
    {
        uint32_t maxFreq = Streaming_ComputeMaxFreqForConfig();
        if (freq > maxFreq) {
            LOG_I("Frequency capped: %u Hz -> %u Hz (%u ch, %u type1)",
                  (unsigned)freq, (unsigned)maxFreq,
                  (unsigned)totalEnabledPublicChannels,
                  (unsigned)activeType1ChannelCount);
            freq = maxFreq;
        }
    }

    // CRITICAL: Always call ADC_WriteChannelStateAll() to enable/disable channel interrupts
    // This must happen even when streaming is off, so channels are ready when streaming starts
    if (!ADC_WriteChannelStateAll()) {
        return SCPI_RES_ERR;
    }

    // If streaming is globally disabled, channel states updated but no timer
    // recalculation needed.
    //
    // #847: this is now the branch taken on every normal path. The wrapper
    // holds the config-change claim for the whole of this function, and
    // SCPI_StartStreaming's arm-time critical section refuses to publish
    // IsEnabled while it is held -- so START can no longer flip it under us.
    //
    // That matters because of what the fall-through DID: before #847 it was
    // reachable ONLY through the race (the guard at entry rejects when
    // IsEnabled is already set), and there it overwrote ClockPeriod,
    // Frequency, TSClockPeriod and ChannelScanFreqDiv on the session START had
    // just armed -- a channel-enable command silently re-rating a live stream
    // to its own locally-capped freq. It is kept as a defensive path rather
    // than deleted because "unreachable" here rests on every arm site
    // observing the claim, which is an invariant across three call sites in
    // another file rather than something this function can enforce.
    if (!pRunTimeStreamConfig->IsEnabled) {
        return SCPI_RES_OK;
    }

    // Individual channel frequencies default to 0 - use 1kHz as reasonable default
    // This maintains Arghya's original design for individual channel frequency control
    if (freq == 0) {
        freq = 1000; // Default to 1kHz for individual channel sampling
    }

    // Guard against invalid timer configuration returning 0 clock frequency
    if (clkFreq == 0) {
        LOG_E("SCPI_ADCChanEnableSet: Invalid timer clock frequency (timer index %u)",
              pBoardConfig->StreamingConfig.TimerIndex);
        return SCPI_RES_ERR;
    }

    // PIC32MZ type-B timer counts 0..PR inclusive (PR+1 cycles per match).
    {
        uint32_t periodCycles = (clkFreq + freq - 1) / freq;
        if (periodCycles < 2) periodCycles = 2;
        pRunTimeStreamConfig->ClockPeriod = periodCycles - 1;
        Streaming_NoteRateConfigured();   /* #730 */
    }
    pRunTimeStreamConfig->Frequency = freq;
    pRunTimeStreamConfig->TSClockPeriod = 0xFFFFFFFF;
    // #107: Type-2 (muxed) channels scan every tick at the full rate (not the old
    // freq/1000 throttle that pinned them to 1 kHz). Mirrors SCPI_StartStreaming.
    pRunTimeStreamConfig->ChannelScanFreqDiv = 1;

    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCChanEnableGet(scpi_t * context) {
    int param1;
    AInArray * pBoardConfigAInChannels = BoardConfig_Get(
            BOARDCONFIG_AIN_CHANNELS,
            0);

    tBoardConfig * pBoardConfig = BoardConfig_Get(
            BOARDCONFIG_VARIANT,
            0);

    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_AIN_CHANNELS);

    // #874: a malformed channel argument must not silently answer with the
    // whole enable MASK instead -- see SCPI_OptionalParamInt32
    // (SCPIInterface.h). A client asking for one channel and parsing "7" as
    // that channel's state is the failure this prevents.
    SCPI_OptionalParam chanOpt = SCPI_OptionalParamInt32(context, &param1);
    if (chanOpt == SCPI_OPT_BAD) {
        return SCPI_RES_ERR;
    }
    if (chanOpt == SCPI_OPT_PRESENT) {
        // Single channel
        size_t index = ADC_FindChannelIndex((uint8_t) param1);
        // TODO: This function should be able to read which version of the board we are using and assign the ADC channels associated that version
        if (index >= pBoardConfigAInChannels->Size) {
            return SCPI_RES_ERR;
        }

        if (pRuntimeAInChannels->Data[index].IsEnabled) {
            SCPI_ResultInt32(context, 1);
        } else {
            SCPI_ResultInt32(context, 0);
        }
    } else {
        uint32_t mask = 0;
        size_t i = 0;
        // TODO: This function should be able to read which version of the board we are using and report the ADC channels associated that version
        for (i = 0; i < pBoardConfig->AInModules.Data[0].Size; ++i) {
            if (pRuntimeAInChannels->Data[i].IsEnabled) {
                mask |= (1 << i);
            }
        }
        SCPI_ResultInt32(context, mask);
    }

    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCChanSingleEndSet(scpi_t * context) {
    uint32_t *pAInLatestSize;
    int param1, param2;
    AInArray * pBoardConfigAInChannels = BoardConfig_Get(
            BOARDCONFIG_AIN_CHANNELS,
            0);

    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_AIN_CHANNELS);

    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }

    // #874: ABSENT selects the one-argument MASK form; PRESENT-but-unparseable
    // must not -- see SCPI_OptionalParamInt32 (SCPIInterface.h).
    // `CONF:ADC:SINGleend 0,BANANA` used to rewrite the single-ended /
    // differential state of EVERY channel -- and from the UNINITIALIZED param2
    // at that, which is the separate defect fixed below.
    SCPI_OptionalParam stateOpt = SCPI_OptionalParamInt32(context, &param2);
    if (stateOpt == SCPI_OPT_BAD) {
        return SCPI_RES_ERR;
    }
    if (stateOpt == SCPI_OPT_PRESENT) {
        // Single channel
        size_t index = ADC_FindChannelIndex((uint8_t) param1);
        if (index >= pBoardConfigAInChannels->Size) {
            return SCPI_RES_ERR;
        }

        pRuntimeAInChannels->Data[index].IsDifferential = (param2 == 0);
    } else {
        pAInLatestSize = BoardData_Get(
                BOARDDATA_AIN_LATEST_SIZE,
                0);

        // Bounded to the width of the mask. BOARDDATA_AIN_LATEST_SIZE is
        // MAX_AIN_CHANNEL (48) -- more slots than the mask has bits -- and
        // `1 << i` for i >= 32 is undefined in C. On MIPS32 `sll` uses only
        // the low five bits of the shift count, so slots 32..47 aliased back
        // onto bits 0..15 and could flip a real channel from a bit the caller
        // never set (#875 pre-merge audit).
        size_t maskable = (*pAInLatestSize < 32u) ? (size_t)*pAInLatestSize : 32u;

        size_t i = 0;
        for (i = 0; i < maskable; ++i) {
            // The mask is param1 -- the ONE argument this branch was given.
            // It read param2, which a failed optional parse leaves
            // uninitialized (ParamSignUInt32 does not write *value when it
            // fails), so the legal one-argument form
            // `CONF:ADC:SINGleend <mask>` wrote every channel's
            // IsDifferential from an indeterminate stack value. Mirrors the
            // mask branch of SCPI_ADCChanEnableSet above, which uses param1.
            pRuntimeAInChannels->Data[i].IsDifferential =
                    ((uint32_t)param1 & (1u << i)) == 0u;
        }
    }

    if (ADC_WriteChannelStateAll()) {
        return SCPI_RES_OK;
    } else {
        return SCPI_RES_ERR;
    }
}

scpi_result_t SCPI_ADCChanSingleEndGet(scpi_t * context) {
    int param1;
    uint32_t *pAInLatestSize;
    AInArray * pBoardConfigAInChannels = BoardConfig_Get(
            BOARDCONFIG_AIN_CHANNELS,
            0);
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_AIN_CHANNELS);
    // #874: a malformed channel argument must not silently answer with the
    // whole mask instead -- see SCPI_OptionalParamInt32 (SCPIInterface.h).
    SCPI_OptionalParam chanOpt = SCPI_OptionalParamInt32(context, &param1);
    if (chanOpt == SCPI_OPT_BAD) {
        return SCPI_RES_ERR;
    }
    if (chanOpt == SCPI_OPT_PRESENT) {
        // Single channel
        size_t index = ADC_FindChannelIndex((uint8_t) param1);
        if (index >= pBoardConfigAInChannels->Size) {
            return SCPI_RES_ERR;
        }

        if (pRuntimeAInChannels->Data[index].IsDifferential) {
            SCPI_ResultInt32(context, 0);
        } else {
            SCPI_ResultInt32(context, 1);
        }
    } else {
        uint32_t mask = 0;
        size_t i = 0;

        pAInLatestSize = BoardData_Get(
                BOARDDATA_AIN_LATEST_SIZE,
                0);
        // Bounded to the width of the mask, for the reason spelled out in
        // SCPI_ADCChanSingleEndSet above: 48 slots, 32 bits, and `1 << i`
        // beyond bit 31 is undefined (#875 pre-merge audit).
        size_t maskable = (*pAInLatestSize < 32u) ? (size_t)*pAInLatestSize : 32u;
        for (i = 0; i < maskable; ++i) {
            if (!pRuntimeAInChannels->Data[i].IsDifferential) {
                mask |= (1u << i);
            }
        }
        // The mask was computed and then discarded: the no-argument form of
        // this query answered NOTHING while returning OK, so a client waiting
        // on `CONF:ADC:SINGleend?` waited for its read timeout. Emit it, the
        // way the sibling getter SCPI_ADCChanEnableGet does.
        SCPI_ResultInt32(context, (int32_t) mask);
    }

    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCChanRangeSet(scpi_t * context) {
    int32_t rangeParam;

    // Get range parameter (0=±5V, 1=±10V)
    if (!SCPI_ParamInt32(context, &rangeParam, TRUE)) {
        SCPI_ErrorPush(context, SCPI_ERROR_MISSING_PARAMETER);
        return SCPI_RES_ERR;
    }

    // Validate range value
    if (rangeParam != 0 && rangeParam != 1) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }

    // Find the AD7609 module
    const AInModule* module = ADC_FindModule(AIn_AD7609);
    if (module == NULL) {
        SCPI_ExecutionError(context, "CONF:ADC:RANG: AD7609 module not found");
        return SCPI_RES_ERR;
    }

    // Get runtime modules configuration
    AInModRuntimeArray* pRuntimeModules = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_AIN_MODULES);
    uint8_t moduleIndex = AIn_AD7609;  // Use module type as index

    // Validate runtime configuration and module index
    if (pRuntimeModules == NULL || moduleIndex >= pRuntimeModules->Size) {
        SCPI_ExecutionError(context, "CONF:ADC:RANG: runtime module index invalid");
        return SCPI_RES_ERR;
    }

    // Prevent changing range while streaming is active
    // Changing range during streaming can corrupt ADC data
    StreamingRuntimeConfig* pStreamCfg = BoardRunTimeConfig_Get(BOARDRUNTIME_STREAMING_CONFIGURATION);
    if (pStreamCfg && pStreamCfg->Running) {
        LOG_E("SCPI_ADCChanRangeSet: Rejecting range change during active streaming");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }

    // Convert parameter to voltage range
    double rangeVoltage = (rangeParam == 1) ? 10.0 : 5.0;
    bool range10V = (rangeParam == 1);

    // Ensure Range_Pin is configured as output before writing
    GPIO_PinOutputEnable(module->Config.AD7609.Range_Pin);

    // Update hardware pin (Range_Pin: LOW=±10V, HIGH=±5V)
    GPIO_PinWrite(module->Config.AD7609.Range_Pin, !range10V);

    // Wait for AD7609 analog circuitry to settle after range change
    // Datasheet specifies settling time; conservative 2ms delay ensures stability
    vTaskDelay(pdMS_TO_TICKS(2));

    // Store range value after hardware has settled
    pRuntimeModules->Data[moduleIndex].Range = rangeVoltage;

    LOG_I("AD7609 module range set to +/-%.1fV", rangeVoltage);

    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCChanRangeGet(scpi_t * context) {
    // Find the AD7609 module
    const AInModule* module = ADC_FindModule(AIn_AD7609);
    if (module == NULL) {
        SCPI_ExecutionError(context, "CONF:ADC:RANG?: AD7609 module not found");
        return SCPI_RES_ERR;
    }

    // AD7609 is module index 1 in NQ3 board
    AInModRuntimeArray* pRuntimeModules = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_AIN_MODULES);
    uint8_t moduleIndex = AIn_AD7609;  // Use module type as index

    // Get range and convert to 0/1 format
    double rangeVoltage = pRuntimeModules->Data[moduleIndex].Range;
    int32_t rangeParam = (rangeVoltage >= 9.0) ? 1 : 0;  // >=9V means 10V range

    SCPI_ResultInt32(context, rangeParam);

    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCChanCalmSet(scpi_t * context) {
    int param1;
    double param2;
    AInArray * pBoardConfigAInChannels = BoardConfig_Get(
            BOARDCONFIG_AIN_CHANNELS,
            0);

    AInRuntimeArray * pRunTimeAInChannels = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_AIN_CHANNELS);


    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }

    if (!SCPI_ParamDouble(context, &param2, TRUE)) {
        return SCPI_RES_ERR;
    }

    size_t index = ADC_FindChannelIndex((uint8_t) param1);
    if (index >= pBoardConfigAInChannels->Size) {
        return SCPI_RES_ERR;
    }

    pRunTimeAInChannels->Data[index].CalM = param2;
    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCChanCalbSet(scpi_t * context) {
    int param1;
    double param2;
    AInArray * pBoardConfigAInChannels = BoardConfig_Get(
            BOARDCONFIG_AIN_CHANNELS,
            0);

    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_AIN_CHANNELS);
    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }

    if (!SCPI_ParamDouble(context, &param2, TRUE)) {
        return SCPI_RES_ERR;
    }

    size_t index = ADC_FindChannelIndex((uint8_t) param1);
    if (index >= pBoardConfigAInChannels->Size) {
        return SCPI_RES_ERR;
    }

    pRuntimeAInChannels->Data[index].CalB = param2;
    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCChanCalmGet(scpi_t * context) {
    int param1;
    AInArray * pBoardConfigAInChannels = BoardConfig_Get(
            BOARDCONFIG_AIN_CHANNELS,
            0);
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_AIN_CHANNELS);
    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }

    size_t index = ADC_FindChannelIndex((uint8_t) param1);
    if (index >= pBoardConfigAInChannels->Size) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultDouble(context, pRuntimeAInChannels->Data[index].CalM);
    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCChanCalbGet(scpi_t * context) {
    int param1;
    AInArray * pBoardConfigAInChannels = BoardConfig_Get(
            BOARDCONFIG_AIN_CHANNELS,
            0);
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_AIN_CHANNELS);
    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }

    size_t index = ADC_FindChannelIndex((uint8_t) param1);
    if (index >= pBoardConfigAInChannels->Size) {
        return SCPI_RES_ERR;
    }

    SCPI_ResultDouble(context, pRuntimeAInChannels->Data[index].CalB);
    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCCalSave(scpi_t * context) {
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_AIN_CHANNELS);
    if (daqifi_settings_SaveADCCalSettings(
            DaqifiSettings_UserAInCalParams,
            pRuntimeAInChannels)) {
        return SCPI_RES_OK;
    } else {
        return SCPI_RES_ERR;
    }
}

scpi_result_t SCPI_ADCCalFSave(scpi_t * context) {
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_AIN_CHANNELS);
    if (daqifi_settings_SaveADCCalSettings(
            DaqifiSettings_FactAInCalParams,
            pRuntimeAInChannels)) {
        return SCPI_RES_OK;
    } else {
        return SCPI_RES_ERR;
    }
}

scpi_result_t SCPI_ADCCalLoad(scpi_t * context) {
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_AIN_CHANNELS);
    if (daqifi_settings_LoadADCCalSettings(
            DaqifiSettings_UserAInCalParams,
            pRuntimeAInChannels)) {
        return SCPI_RES_OK;
    } else {
        return SCPI_RES_ERR;
    }
}

scpi_result_t SCPI_ADCCalFLoad(scpi_t * context) {
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_AIN_CHANNELS);
    if (daqifi_settings_LoadADCCalSettings(
            DaqifiSettings_FactAInCalParams,
            pRuntimeAInChannels)) {
        return SCPI_RES_OK;
    } else {
        return SCPI_RES_ERR;
    }
}

static scpi_result_t ADCUseCalSetClaimed(scpi_t * context);

/* #847: wrapped for the same reason as SCPI_ADCChanEnableSet -- the body has
 * nine returns and its "store" reaches an NVM save, which cannot run inside a
 * critical section. One claim, one release, no path that skips it. */
scpi_result_t SCPI_ADCUseCalSet(scpi_t * context) {
    StreamingCfgClaim claim = Streaming_BeginConfigChange();
    if (claim != STREAM_CFG_CLAIM_OK) {
        return SCPI_RejectCfgClaim(context,
                                   claim == STREAM_CFG_CLAIM_BUSY,
                                   "CONF:ADC:USECal");
    }
    scpi_result_t result = ADCUseCalSetClaimed(context);
    Streaming_EndConfigChange();
    return result;
}

static scpi_result_t ADCUseCalSetClaimed(scpi_t * context) {
    int param1;
    DaqifiSettings tmpTopLevelSettings;
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_AIN_CHANNELS);
    StreamingRuntimeConfig * pRunTimeStreamConfig = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);

    // #158/#270: this command also switches the encoder output format
    // (value 2 = raw codes), so changing it mid-stream would alter the wire
    // format under an active session. Rejecting while streaming (mirrors the
    // CONF:ADC:CHANnel #116 guard) also protects the mid-stream cal
    // coefficient reload for values 0/1. The test now lives in the wrapper's
    // Streaming_BeginConfigChange() (#847), which performs it and takes the
    // claim in one critical section.

    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }

    // #620: reject an out-of-range value BEFORE any state mutation. The paths
    // below clear RawOutputMode, assign calVals (a bool, so e.g. 7 -> 1) and
    // SaveToNvm before the switch's default rejected it — a command that
    // returns an error would otherwise persist a wrong calibration selection
    // (loaded as USER cal on the next reboot).
    if (param1 < 0 || param1 > 2) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }

    // #158/#270: value 2 = no calibration -> raw ADC-code output (CSV/JSON
    // emit the integer code, skipping cal + voltage conversion; PB is
    // already raw). Runtime-only streaming mode: it does NOT touch the NVM
    // calVals coefficient selection, so switching back to 0/1 or rebooting
    // restores the persisted factory/user cal choice.
    if (param1 == 2) {
        pRunTimeStreamConfig->RawOutputMode = true;
        return SCPI_RES_OK;
    }

    // Values 0/1 select the calibration coefficient set and emit calibrated
    // volts (leaving raw mode).
    pRunTimeStreamConfig->RawOutputMode = false;

    //  Load existing settings
    if (!daqifi_settings_LoadFromNvm(DaqifiSettings_TopLevelSettings, &tmpTopLevelSettings)) return SCPI_RES_ERR;

    //  Update calVals setting
    tmpTopLevelSettings.settings.topLevelSettings.calVals = param1;

    //  Store to NVM
    if (!daqifi_settings_SaveToNvm(&tmpTopLevelSettings)) return SCPI_RES_ERR;

    //  Update runtime values
    switch (param1) {
        case 0:
            if (!daqifi_settings_LoadADCCalSettings(
                    DaqifiSettings_FactAInCalParams,
                    pRuntimeAInChannels)) {
                return SCPI_RES_ERR;
            }
            break;
        case 1:
            if (!daqifi_settings_LoadADCCalSettings(
                    DaqifiSettings_UserAInCalParams,
                    pRuntimeAInChannels)) {
                return SCPI_RES_ERR;
            }
            break;
        default:
            return SCPI_RES_ERR;
            break;
    }
    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCUseCalGet(scpi_t * context) {
    DaqifiSettings tmpTopLevelSettings;
    StreamingRuntimeConfig * pRunTimeStreamConfig = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);

    // #158/#270: raw mode (runtime-only) reports as 2, overriding the
    // persisted 0/1 coefficient selection.
    if (pRunTimeStreamConfig != NULL && pRunTimeStreamConfig->RawOutputMode) {
        SCPI_ResultInt32(context, 2);
        return SCPI_RES_OK;
    }
    if (daqifi_settings_LoadFromNvm(DaqifiSettings_TopLevelSettings, &tmpTopLevelSettings)) {
        SCPI_ResultInt32(context, tmpTopLevelSettings.settings.topLevelSettings.calVals);
        return SCPI_RES_OK;
    } else {
        return SCPI_RES_ERR;
    }
}

static scpi_result_t ADCOnboardDiagSetClaimed(scpi_t * context);

// #847: the claim performs the IsEnabled || Running test (the two flags
// transition in separate steps at start/stop, and a change slipped through
// that window would desync the session scan list -- #541 D-B builds ADCCSS
// from OnboardDiagEnabled at stream start) AND excludes a concurrent arm
// until the body's store has landed.
//
// #862: taken HERE, before SCPI_ParamInt32 runs, so the refusal does not
// depend on the argument -- see the ordering contract on SCPI_RejectCfgClaim
// (SCPIInterface.h). This setter used to validate first and so answered -222
// mid-stream where CONF:ADC:CHANnel answered -200.
scpi_result_t SCPI_ADCOnboardDiagSet(scpi_t * context) {
    StreamingCfgClaim claim = Streaming_BeginConfigChange();
    if (claim != STREAM_CFG_CLAIM_OK) {
        return SCPI_RejectCfgClaim(context,
                                   claim == STREAM_CFG_CLAIM_BUSY,
                                   "CONF:ADC:OBDiag");
    }
    scpi_result_t result = ADCOnboardDiagSetClaimed(context);
    Streaming_EndConfigChange();
    return result;
}

static scpi_result_t ADCOnboardDiagSetClaimed(scpi_t * context) {
    int32_t val;
    if (!SCPI_ParamInt32(context, &val, TRUE)) return SCPI_RES_ERR;
    if (val < 0 || val > 1) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }
    StreamingRuntimeConfig *pStreamCfg = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);
    pStreamCfg->OnboardDiagEnabled = (val != 0);
    LOG_I("Onboard diagnostics during streaming: %s", val ? "enabled" : "disabled");
    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCOnboardDiagGet(scpi_t * context) {
    StreamingRuntimeConfig *pStreamCfg = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);
    SCPI_ResultInt32(context, pStreamCfg->OnboardDiagEnabled ? 1 : 0);
    return SCPI_RES_OK;
}

// --- #670: ADC hardware threshold alarms (ADCHS digital comparators) --------
// CONF:ADC:THREshold <ch>,<mode 0=off|1=below|2=above|3=inside|4=outside>,<lo>,<hi>
static scpi_result_t ADCThresholdSetClaimed(scpi_t * context);

// #862: the claim is taken HERE, before any parameter is read, so a mid-stream
// call is refused as streaming whatever its arguments -- see the ordering
// contract on SCPI_RejectCfgClaim (SCPIInterface.h). This is the setter with
// the most ways to escape the old ordering: SIX returns sat above where the
// claim used to be -- two parse failures, a libscpi param error, two range
// checks, and the "modes 1-4 require lo,hi" case. The last of those is the
// subtle one, because it DOES answer -200 -- via SCPI_ExecutionError with its
// own text -- so a client matching only the code would have called it
// compliant while the log said nothing about streaming.
//
// #847: via the claim rather than a bare read of the two flags, because
// AdcThreshold_Configure takes a FreeRTOS mutex with portMAX_DELAY -- it cannot
// share a critical section with the test, so the exclusion has to outlive one.
//
// The release moved out of the body with it, so it now also covers the
// `!configured` error return that it used to sit above.
scpi_result_t SCPI_ADCThresholdSet(scpi_t * context) {
    StreamingCfgClaim claim = Streaming_BeginConfigChange();
    if (claim != STREAM_CFG_CLAIM_OK) {
        return SCPI_RejectCfgClaim(context,
                                   claim == STREAM_CFG_CLAIM_BUSY,
                                   "CONF:ADC:THREshold");
    }
    scpi_result_t result = ADCThresholdSetClaimed(context);
    Streaming_EndConfigChange();
    return result;
}

static scpi_result_t ADCThresholdSetClaimed(scpi_t * context) {
    int32_t ch, mode, lo = 0, hi = 0;
    if (!SCPI_ParamInt32(context, &ch, TRUE))   return SCPI_RES_ERR;
    if (!SCPI_ParamInt32(context, &mode, TRUE)) return SCPI_RES_ERR;
    bool haveLo = SCPI_ParamInt32(context, &lo, FALSE);
    bool haveHi = SCPI_ParamInt32(context, &hi, FALSE);
    // A malformed optional lo/hi (e.g. "...,banana") pushes a data-type error but
    // still returns FALSE; treat that as a hard reject, not "absent", so a bad
    // token can't fall through to a mode-0 release or a partially-parsed arm.
    if (SCPI_ParamErrorOccurred(context)) {
        return SCPI_RES_ERR;
    }
    // Reject before the (uint8_t) narrowing so a value like 256 can't alias
    // onto a valid channel (#671 truncation-alias lesson).
    if (ch < 0 || ch > 255 || mode < 0 || mode > 4) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }
    if (mode != 0 && (!haveLo || !haveHi)) {
        SCPI_ExecutionError(context, "CONF:ADC:THRE: modes 1-4 require lo,hi");
        return SCPI_RES_ERR;
    }
    if ((haveLo && (lo < 0 || lo > (int32_t)ADC_THRESHOLD_MAX_CODE)) ||
        (haveHi && (hi < 0 || hi > (int32_t)ADC_THRESHOLD_MAX_CODE))) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }
    // The config change itself. Rejecting it while streaming (consistent with
    // the CONF:ADC family; the session scan is frozen at start, #116/#541) is
    // the WRAPPER's job now, not this body's -- see the comment on
    // SCPI_ADCThresholdSet above.
    const char* err = NULL;
    bool configured = AdcThreshold_Configure((uint8_t)ch, (AdcThresholdMode)mode,
                                             (uint16_t)lo, (uint16_t)hi, &err);
    if (!configured) {
        SCPI_ExecutionError(context, (err != NULL) ? err : "CONF:ADC:THRE: rejected");
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

// CONF:ADC:THREshold? <ch> -> mode,lo,hi,tripCount,latched (0s if none configured)
scpi_result_t SCPI_ADCThresholdGet(scpi_t * context) {
    int32_t ch;
    if (!SCPI_ParamInt32(context, &ch, TRUE)) return SCPI_RES_ERR;
    if (ch < 0 || ch > 255) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }
    AdcThresholdMode mode = ADC_THRESH_OFF;
    uint16_t lo = 0, hi = 0; uint32_t cnt = 0; bool latched = false;
    AdcThreshold_Query((uint8_t)ch, &mode, &lo, &hi, &cnt, &latched);
    SCPI_ResultUInt32(context, (uint32_t)mode);
    SCPI_ResultUInt32(context, (uint32_t)lo);
    SCPI_ResultUInt32(context, (uint32_t)hi);
    SCPI_ResultUInt32(context, cnt);
    SCPI_ResultUInt32(context, latched ? 1u : 0u);
    return SCPI_RES_OK;
}

// CONF:ADC:THREshold:CLEar [<ch>] -> clear latch+counter (no arg = all)
scpi_result_t SCPI_ADCThresholdClear(scpi_t * context) {
    int32_t ch;
    if (!SCPI_ParamInt32(context, &ch, FALSE)) {
        /* FALSE means EITHER "no argument" (clear all) OR "argument present but
         * malformed" (e.g. `CLE ch5`, which pushes a data-type error). Only the
         * absent case should clear all — a malformed token must not trigger a
         * destructive clear-all of every unit (incl. an alarm on another ch). */
        if (SCPI_ParamErrorOccurred(context)) {
            return SCPI_RES_ERR;
        }
        AdcThreshold_Clear(ADC_THRESHOLD_ALL_CH);
        return SCPI_RES_OK;
    }
    if (ch < 0 || ch > 255) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }
    AdcThreshold_Clear((uint8_t)ch);
    return SCPI_RES_OK;
}

// --- #328 phase 1: ADC acquisition-time runtime control ------------------
// Wrapper that rejects SAMC writes while streaming is active. Calls into
// MC12b_SetAcquisitionSamc for the register work.
//
// SAMC feeds the live scan-rate bound (#541 D-C reads ADCCON2.SAMC), so a
// mid-stream change would invalidate the cap the session was admitted under.
// The claim performs the IsEnabled || Running test (#116 / OBDiag form) and
// holds the exclusion across the write (#847) -- which it must, because
// MC12b_SetAcquisitionSamc spins up to 2,000,000 poll iterations on
// ADCCON2bits.BGVRRDY and so cannot share a critical section with the test.
//
// #862: the claim is the first statement, ahead of SCPI_ParamInt32, so
// `CONF:ADC:SAMC:DEDicated 99999` mid-stream answers -200 like every other
// converted setter instead of -222 -- see the ordering contract on
// SCPI_RejectCfgClaim (SCPIInterface.h). Both registered spellings route
// through here, so both are fixed by the one wrapper.
//
// The refusal names "CONF:ADC:SAMC" for either spelling, as it did before.
static scpi_result_t SamcSetCommonClaimed(scpi_t *context, bool isDedicated);

static scpi_result_t SamcSetCommon(scpi_t *context, bool isDedicated) {
    StreamingCfgClaim claim = Streaming_BeginConfigChange();
    if (claim != STREAM_CFG_CLAIM_OK) {
        return SCPI_RejectCfgClaim(context,
                                   claim == STREAM_CFG_CLAIM_BUSY,
                                   "CONF:ADC:SAMC");
    }
    scpi_result_t result = SamcSetCommonClaimed(context, isDedicated);
    Streaming_EndConfigChange();
    return result;
}

static scpi_result_t SamcSetCommonClaimed(scpi_t *context, bool isDedicated) {
    int32_t val;
    if (!SCPI_ParamInt32(context, &val, TRUE)) return SCPI_RES_ERR;
    if (val < 0 || val > (int32_t)MC12B_SAMC_MAX) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }
    bool ok = isDedicated ? MC12b_SetAcquisitionSamc(val, -1)
                          : MC12b_SetAcquisitionSamc(-1, val);
    if (!ok) {
        SCPI_ExecutionError(context, "CONF:ADC:SAMC: set failed");
        return SCPI_RES_ERR;
    }
    // Acquisition = (SAMC+2) x TAD, TAD = 100 ns at the boot clock config
    // (TCLK 10 ns x (CONCLKDIV+1)=5 x 2xADCDIV=2 — DS60001320H Reg 28-2/3;
    // the old "50 MHz / 20 ns" figure here was a 5x divider misdecode).
    LOG_I("ADC SAMC %s = %ld (%ld ns acquisition @ TAD=100 ns)",
          isDedicated ? "dedicated" : "shared",
          (long)val, (long)((val + 2) * 100));
    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCSamcDedicatedSet(scpi_t *context) {
    return SamcSetCommon(context, true);
}

scpi_result_t SCPI_ADCSamcSharedSet(scpi_t *context) {
    return SamcSetCommon(context, false);
}

scpi_result_t SCPI_ADCSamcDedicatedGet(scpi_t *context) {
    uint16_t samc = 0;
    MC12b_GetAcquisitionSamc(&samc, NULL);
    SCPI_ResultInt32(context, (int32_t)samc);
    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCSamcSharedGet(scpi_t *context) {
    uint16_t samc = 0;
    MC12b_GetAcquisitionSamc(NULL, &samc);
    SCPI_ResultInt32(context, (int32_t)samc);
    return SCPI_RES_OK;
}