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

// #877: reject a channel argument that a (uint8_t) cast would TRUNCATE --
// i.e. one outside [0,255] -- BEFORE the cast, so it cannot alias mod-256
// onto a channel that exists: 256 -> 0, 257 -> 1, and -1 -> 255 (a
// monitoring id on NQ1). ADC_FindChannelIndex then RESOLVES that aliased
// channel, so the #630 resolved-index guard downstream passes and the
// command reads or writes the wrong channel while returning OK.
//
// The range is the truncation range ONLY -- the #682 narrowing. Values in
// [0,255] are not truncated and fall through to each caller's existing
// resolved-index guard, which keeps its own handling for the sparse id space.
// That space is sparse on BOTH variants: user ids 0..15 on NQ1 and 0..7 on
// NQ3, plus the same monitoring block 248..255 on each (ADC_CHANNEL_3_3V ..
// ADC_CHANNEL_5VREF, AInConfig.h:240-247, pulled in by
// COMMON_MONITORING_CHANNELS_BOARDCONFIG). Widening this test to the
// per-variant user maximum would usurp that handling, which is the regression
// #682 had to undo.
//
// Kept as one helper rather than a copy per call site so the invariant is
// greppable. Two sites carried this test inline before #877 and NEITHER was
// propagated: SCPI_ADCVoltageGet's predates #678 entirely, and #678 itself
// (1c5f5ef2) added exactly one, in SCPI_ADCChanEnableSet -- as
// `> maxUserChannel`, which #682 then had to narrow to `> 255`. Nine other
// call sites went on casting unchecked, which is what #877 is.
//
// SCPIDIO.c solves the same problem, but it does NOT need this shape, and the
// difference is the sparse id space. DIO ids are dense 0..Size-1, so a single
// `index >= Size` test on an UNSIGNED comparison bounds both ends at once: its
// getters compare an `int` against a `size_t` Size, which promotes the int, so
// -1 becomes 0xFFFFFFFF and is rejected by the same test that rejects 256
// (verified on the bench 2026-08-25 -- `PWM:CHannel:ENable? -1` answers no
// value and queues an error, while `? 0` answers 0). #671's
// DIO_SingleChannelIndexValid is that one test factored out, not a separate
// range check. Here the id space is sparse, so a resolved-index test cannot
// bound the ARGUMENT and this range test has to exist in its own right.
//
// The message is short on purpose: the log buffer is 128 B wide and a longer
// line loses its tail. That is what the PRE-#682 form of this guard did --
// its text ran past the width and hid the hint, regressing test_630 -- and
// narrowing the guard is what fixed it.
//
// WHAT THIS DOES NOT COVER, and cannot: a token libscpi lexes as a DECIMAL
// number is already TRUNCATED to an integer by the time it arrives, so
// `CONF:ADC:SINGleend -0.5,1` hands this function 0 -- in range, and it
// writes channel 0. Measured on the bench 2026-08-25 (`SYST:ERR?` reads
// `0,"No error"` and channel 0's state changes), while the integer `-1` on
// the same command is correctly refused with -222. That is a parser-level
// class affecting EVERY integer channel parameter in the firmware, not just
// the ones cast to uint8_t, and is tracked as #880 -- no range test placed
// here could see it.
static bool AdcChannelArgInRange(scpi_t * context, int channel, const char * cmd)
{
    if (channel >= 0 && channel <= 255) {
        return true;
    }
    LOG_E("%s: channel %d out of range (max 255)", cmd, channel);
    SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
    return false;
}

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
        // #877: same truncation test as before, now via the shared helper so
        // every site in this file is found by one grep. It also gains the
        // LOG_E the bare push did not emit (standing rule: the reason for a
        // SCPI error is retrievable through SYST:LOG?).
        if (!AdcChannelArgInRange(context, channel, "MEAS:VOLT:DC?")) {
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
        // #877: deliberately NOT AdcChannelArgInRange -- this is the one site
        // whose hint is worth its own text, because the two-arg and one-arg
        // (mask) forms are told apart by the argument COUNT, so a client that
        // meant a mask lands here. Same predicate, same -222.
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

    // BOARDCONFIG_ALL_CONFIG, not BOARDCONFIG_VARIANT: the latter returns
    // &boardConfig.BoardVariant -- a uint8_t*, widened to void* so nothing
    // diagnoses it -- and every `pBoardConfig->` below then reads at a member
    // offset from THAT byte's address. It resolves correctly today only
    // because BoardVariant happens to be the first member of tBoardConfig, so
    // the two addresses coincide; reordering the struct would silently
    // mis-resolve these loops. ALL_CONFIG returns &boardConfig, which is what
    // the declared type says -- the idiom already used earlier in this file.
    tBoardConfig * pBoardConfig = BoardConfig_Get(
            BOARDCONFIG_ALL_CONFIG,
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
        // #877: reject before the (uint8_t) narrowing -- see
        // AdcChannelArgInRange.
        if (!AdcChannelArgInRange(context, param1, "CONF:ADC:CHAN?")) {
            return SCPI_RES_ERR;
        }
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

static scpi_result_t ADCChanSingleEndSetClaimed(scpi_t * context);

/* #885: this setter had NO stream guard of any kind -- not even the
 * Running-only test #873 replaced. It writes AInRuntime.Data[i].IsDifferential
 * and then pushes the whole channel state to the ADC SFRs via
 * ADC_WriteChannelStateAll(), so a mid-session call re-wires the input
 * multiplexer under a running acquisition AND invalidates the description of
 * the stream that the client is already holding. That description is
 * `analog_in_port_rse` -- one bit per channel, built from IsDifferential in
 * NanoPB_Encoder.c -- and it does NOT ride the stream: the only session-start
 * metadata is the SD file header's six tags (fields_sd_metadata, streaming.c),
 * and rse appears only in the on-demand fields_all / fields_info reply
 * (SCPI_SysInfoGet, SCPIInterface.c). So a client that read the channel map
 * before START and then flipped a channel to differential at sample 10,000
 * gets samples whose meaning no longer matches the map it parsed, with nothing
 * in the stream marking where it changed.
 *
 * Two things an earlier revision of this comment got wrong, corrected here
 * rather than quietly dropped: there is no `is_differential` field anywhere in
 * the tree (grep finds only that comment), and `analog_in_int_scale_m` is read
 * from BOARD CONFIG (NanoPB_Encoder.c), not from runtime, so this setter
 * cannot move it and it is not part of the defect.
 *
 * Same shape as SCPI_ADCChanEnableSet above, and for the same two reasons: the
 * body has six returns so the release must be unconditional, and the claim is
 * the FIRST statement so the refusal does not depend on the arguments (#862's
 * ordering contract on SCPI_RejectCfgClaim, SCPIInterface.h). That ordering
 * matters more here than usual: the body's #874 optional-argument branch picks
 * the MASK form vs the single-channel form from the arguments, so a guard
 * placed after the parse would answer a different error for the two forms of
 * the same mid-stream call. */
scpi_result_t SCPI_ADCChanSingleEndSet(scpi_t * context) {
    StreamingCfgClaim claim = Streaming_BeginConfigChange();
    if (claim != STREAM_CFG_CLAIM_OK) {
        return SCPI_RejectCfgClaim(context,
                                   claim == STREAM_CFG_CLAIM_BUSY,
                                   "CONF:ADC:SINGleend");
    }
    scpi_result_t result = ADCChanSingleEndSetClaimed(context);
    Streaming_EndConfigChange();
    return result;
}

static scpi_result_t ADCChanSingleEndSetClaimed(scpi_t * context) {
    int param1, param2;
    AInArray * pBoardConfigAInChannels = BoardConfig_Get(
            BOARDCONFIG_AIN_CHANNELS,
            0);

    // BOARDCONFIG_ALL_CONFIG, not BOARDCONFIG_VARIANT: the latter returns
    // &boardConfig.BoardVariant -- a uint8_t*, widened to void* so nothing
    // diagnoses it -- and every `pBoardConfig->` below then reads at a member
    // offset from THAT byte's address. It resolves correctly today only
    // because BoardVariant happens to be the first member of tBoardConfig, so
    // the two addresses coincide; reordering the struct would silently
    // mis-resolve these loops. ALL_CONFIG returns &boardConfig, which is what
    // the declared type says -- the idiom already used earlier in this file.
    tBoardConfig * pBoardConfig = BoardConfig_Get(
            BOARDCONFIG_ALL_CONFIG,
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
        // #877: reject before the (uint8_t) narrowing -- see
        // AdcChannelArgInRange.
        if (!AdcChannelArgInRange(context, param1, "CONF:ADC:SINGleend")) {
            return SCPI_RES_ERR;
        }
        size_t index = ADC_FindChannelIndex((uint8_t) param1);
        if (index >= pBoardConfigAInChannels->Size) {
            return SCPI_RES_ERR;
        }

        pRuntimeAInChannels->Data[index].IsDifferential = (param2 == 0);
    } else {
        // Bounded to the USER channels, variant-aware, exactly as the mask
        // branch of SCPI_ADCChanEnableSet above does -- that command is the
        // authority on what a channel mask addresses, and these two must not
        // disagree.
        //
        // It used to run to BOARDDATA_AIN_LATEST_SIZE, which is
        // MAX_AIN_CHANNEL (48): more slots than the mask has bits, and more
        // than any board has user channels. Two defects came out of that
        // (#875 pre-merge audit): `1 << i` for i >= 32 is undefined in C --
        // on MIPS32 `sll` takes only the low five bits of the shift count, so
        // slots 32..47 aliased back onto bits 0..15 -- and the write reached
        // monitoring and nonexistent slots that no mask bit is supposed to
        // address. An intermediate revision bounded by
        // `AInModules.Data[0].Size`, which is what the sibling
        // SCPI_ADCChanEnableGet uses; that is 16 even on an NQ3, because
        // Data[0] there is the MC12b MONITORING module and the AD7609 user
        // channels are Data[1] (NQ3BoardConfig.c). The variant test below is
        // the bound that is actually right on both boards, and being <= 16 it
        // makes the shift safe by construction.
        uint8_t maxUserChannel = (pBoardConfig->BoardVariant == 3) ? 7 : 15;

        size_t i = 0;
        for (i = 0; i <= (size_t) maxUserChannel; ++i) {
            // Bit i addresses CHANNEL i, which is not necessarily runtime
            // slot i, so resolve the id the way every other path in this file
            // resolves it rather than indexing Data[i] directly.
            //
            // Be precise about what this does and does not fix. On BOTH
            // shipping variants the mapping is currently the IDENTITY, so
            // this is behaviour-neutral today, not a bug fix: NQ1's
            // AInChannels holds DaqifiAdcChannelId 0..15 at indices 0..15 in
            // order (the 8 monitoring entries follow at 16..23, ids 248..255
            // -- NQ1BoardConfig.c, AInConfig.h), and NQ3's holds ids 0..7 at
            // indices 0..7. An earlier revision of this comment claimed NQ1's
            // table was sparse with the public channels "at indices 8..15,
            // interleaved"; that is false, and so was the matching claim that
            // the old Data[i] form addressed a different channel. It did not.
            //
            // It is kept because the id->slot mapping is ADC_FindChannelIndex's
            // to define, not this loop's to assume, and because the sibling
            // paths (the two-argument form and the enable mask) already
            // resolve it -- a future variant whose table is not ordered would
            // break exactly the paths that open-code the identity. An id with
            // no table entry is skipped rather than failing the whole command,
            // matching the enable-mask branch.
            size_t channelIndex = ADC_FindChannelIndex((uint8_t) i);
            if (channelIndex >= (size_t) pBoardConfigAInChannels->Size) {
                continue;
            }
            // The mask is param1 -- the ONE argument this branch was given.
            // It read param2, which a failed optional parse leaves
            // uninitialized (ParamSignUInt32 does not write *value when it
            // fails), so the legal one-argument form
            // `CONF:ADC:SINGleend <mask>` wrote every channel's
            // IsDifferential from an indeterminate stack value. Mirrors the
            // mask branch of SCPI_ADCChanEnableSet above, which uses param1.
            pRuntimeAInChannels->Data[channelIndex].IsDifferential =
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
    AInArray * pBoardConfigAInChannels = BoardConfig_Get(
            BOARDCONFIG_AIN_CHANNELS,
            0);
    // BOARDCONFIG_ALL_CONFIG, not BOARDCONFIG_VARIANT: the latter returns
    // &boardConfig.BoardVariant -- a uint8_t*, widened to void* so nothing
    // diagnoses it -- and every `pBoardConfig->` below then reads at a member
    // offset from THAT byte's address. It resolves correctly today only
    // because BoardVariant happens to be the first member of tBoardConfig, so
    // the two addresses coincide; reordering the struct would silently
    // mis-resolve these loops. ALL_CONFIG returns &boardConfig, which is what
    // the declared type says -- the idiom already used earlier in this file.
    tBoardConfig * pBoardConfig = BoardConfig_Get(
            BOARDCONFIG_ALL_CONFIG,
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
        // #877: reject before the (uint8_t) narrowing -- see
        // AdcChannelArgInRange.
        if (!AdcChannelArgInRange(context, param1, "CONF:ADC:SINGleend?")) {
            return SCPI_RES_ERR;
        }
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

        // Same variant-aware user-channel bound as SCPI_ADCChanSingleEndSet's
        // mask branch -- see the comment there for why it is not the module
        // size. Reporting slots past the user channels set bits for channels
        // the board does not have (every unused slot is zero-initialised, so
        // IsDifferential reads false and the bit went UP), on top of the
        // undefined shift past bit 31.
        uint8_t maxUserChannel = (pBoardConfig->BoardVariant == 3) ? 7 : 15;
        for (i = 0; i <= (size_t) maxUserChannel; ++i) {
            // Channel i, not slot i -- see SCPI_ADCChanSingleEndSet's mask
            // branch. A channel with no table entry contributes no bit, which
            // is the honest answer for a channel the board does not have.
            size_t channelIndex = ADC_FindChannelIndex((uint8_t) i);
            if (channelIndex >= (size_t) pBoardConfigAInChannels->Size) {
                continue;
            }
            if (!pRuntimeAInChannels->Data[channelIndex].IsDifferential) {
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

static scpi_result_t ADCChanRangeSetClaimed(scpi_t * context);

/* #873: converted to the claim-in-wrapper shape the nine already-converted
 * setters use. Those nine are cap INPUTS -- they move the rate the session was
 * admitted under -- and this one is not: Range feeds the voltage CONVERSION,
 * not Streaming_ComputeMaxFreq. The exclusion it needs against an arm is the
 * same one, for a different reason, which is why it takes the same claim.
 *
 * It closes three separate defects at once, and the claim is what closes all
 * three:
 *
 *  1. It took NO claim, so SCPI_StartStreaming's arm-time observation of
 *     Streaming_ConfigChangeInProgress() could not see it (#847 class). A range
 *     change on one SCPI transport could pass its guard and then land on a
 *     session the other transport armed.
 *  2. Its guard read `Running` ALONE. The two flags move in separate steps at
 *     start/stop, so that test reads false for the whole interval between them
 *     -- it refused only the fully-established middle of a session and admitted
 *     both ends (#857/#844 class). Streaming_BeginConfigChange performs
 *     `IsEnabled || Running` inside one critical section instead.
 *  3. It parsed and range-checked BEFORE it guarded, so mid-stream
 *     `CONF:ADC:RANGe 5` answered -224 about the argument -- and an unparseable
 *     token -104 -- where every converted setter answers -200 about the stream
 *     (#862 class). Taking the
 *     claim as the first statement makes the refusal independent of the
 *     argument -- see the ordering contract on SCPI_RejectCfgClaim.
 *
 * The window this closes is milliseconds wide, not instruction-narrow: the body
 * holds an explicit vTaskDelay(2ms) for AD7609 analog settling BETWEEN the
 * hardware pin write and the runtime store, and that is a guaranteed yield
 * point. Holding the claim across it is correct and is the reason the exclusion
 * is a flag rather than a critical section (streaming.h, StreamingCfgClaim):
 * nothing blocks on the flag, so a START that lands inside those 2 ms is
 * refused with -200 instead of arming onto a range change that has written the
 * pin but not yet stored the scale. This is the first holder that yields, so it
 * is also the first for which a concurrent config setter on the other transport
 * can see STREAM_CFG_CLAIM_BUSY for a bounded couple of milliseconds; that is
 * the intended cost, and it is reported rather than silent.
 *
 * Range is read live per conversion by MC12b_ConvertToVoltage (MC12bADC.c:248-
 * 258) and its AD7609 counterpart, so the unguarded store rescaled a
 * running session's samples on a header already emitted -- and the pin moved
 * under a live acquisition. AD7609/NQ3-only in EFFECT (ADC_FindModule returns
 * NULL on NQ1, below), but the refusal semantics above are variant-independent.
 */
scpi_result_t SCPI_ADCChanRangeSet(scpi_t * context) {
    StreamingCfgClaim claim = Streaming_BeginConfigChange();
    if (claim != STREAM_CFG_CLAIM_OK) {
        return SCPI_RejectCfgClaim(context,
                                   claim == STREAM_CFG_CLAIM_BUSY,
                                   "CONF:ADC:RANGe");
    }
    scpi_result_t result = ADCChanRangeSetClaimed(context);
    Streaming_EndConfigChange();
    return result;
}

static scpi_result_t ADCChanRangeSetClaimed(scpi_t * context) {
    int32_t rangeParam;

    // Get range parameter (0=±5V, 1=±10V)
    //
    // #885: no second SCPI_ErrorPush here. The -109 that used to sit here
    // queued a SECOND and usually WRONG code on top of the one libscpi had
    // already pushed: `CONF:ADC:RANGe BANANA` reported -104 followed by a
    // spurious -109 "Missing parameter", and bare `CONF:ADC:RANGe` reported
    // -109 TWICE. A client draining the queue saw two errors for one command
    // and the last one it read was the wrong one. Returning the error
    // unmodified is what every peer setter in this file does
    // (ADCOnboardDiagSetClaimed, SamcSetCommonClaimed) and is the same fix PR
    // #882 made in SCPI_StartStreaming.
    //
    // REMOVING a push is only safe if the callee ALWAYS queues, so that claim
    // is established here rather than asserted. Walking libscpi from this call
    // site (parser.c), SCPI_ParamInt32 -> ParamSignUInt32 has exactly these
    // exits, and every REACHABLE one queues:
    //
    //   absent + mandatory      -109  SCPI_Parameter, missing-parameter arm
    //   bad separator           -103  SCPI_Parameter, comma arm
    //   unusable token type     -151  SCPI_Parameter, DEFAULT arm -- reached by
    //                                 e.g. `#H` with no digits, which the lexer
    //                                 types SCPI_TOKEN_UNKNOWN
    //   not a number at all     -104  ParamSignUInt32 else-arm
    //   number with a suffix    -138  ParamSignUInt32
    //   partial decimal token   -104  DaqifiIntTokenFullyConsumed (#880)
    //   value == NULL           -310  (we pass &rangeParam, so unreachable)
    //
    // The -151 row and the -103 code are BOTH corrections. An earlier revision
    // omitted the default arm entirely and numbered the separator error -115
    // (it is -103 -- error.h). Neither changes the conclusion, because both of
    // those exits QUEUE and queueing is the whole property being established --
    // but a proof presented as exhaustive has to BE exhaustive, or the next
    // reader trusts a list nobody checked (codex pre-merge audit re-walked it
    // and found both).
    //
    // TWO exits queue NOTHING, and both are unreachable BY CONSTRUCTION rather
    // than by luck -- which is the part a future libscpi bump could break:
    //
    //   * ParamSignToUInt32's HEXNUM/OCTNUM/BINNUM arms (:827-831) return
    //     FALSE when zero characters convert. scpiLex_NondecimalNumericData
    //     (lexer.c:597-627) only ASSIGNS those token types when it consumed at
    //     least one digit -- otherwise the token becomes SCPI_TOKEN_UNKNOWN --
    //     so a token of that type always has a digit at ptr[0] and the
    //     conversion always consumes it.
    //   * ParamSignToUInt32's `default:` arm (:842) returns FALSE silently.
    //     ParamSignUInt32 only calls it when SCPI_ParamIsNumber(param, FALSE)
    //     is true, and that function (:757-769) returns TRUE for exactly the
    //     four token types the switch enumerates.
    //
    // If a libscpi upgrade adds a numeric token type, or lets the lexer emit a
    // digitless HEXNUM, that `default:` becomes live -- and this command would
    // then report only libscpi's GENERIC -200 instead of the specific parameter
    // error, which is a real loss of diagnosability but is NOT a silent
    // failure. processCommand queues -200 for any callback that returns non-OK
    // with cmd_error unset, and it clears cmd_error on entry (parser.c). An
    // earlier revision of this comment said "NO error queued at all", which
    // contradicts both that code and this file's own correct account of the
    // same mechanism in SCPI_ADCChanCalmSet below. Re-check this list at that
    // point.
    if (!SCPI_ParamInt32(context, &rangeParam, TRUE)) {
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

    // The stream-state test that used to sit here is gone deliberately: the
    // wrapper's Streaming_BeginConfigChange() performs it (IsEnabled || Running,
    // atomically with taking the claim) and holds the exclusion across
    // everything below, including the vTaskDelay. A second test here would be
    // dead code AND would re-answer -200 from inside the claim, where the
    // wrapper has already decided the session state.

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

static scpi_result_t ADCChanCalmSetClaimed(scpi_t * context);

/* #885: CalM/CalB are read PER CONVERSION by MC12b_ConvertToVoltage
 * (HAL/ADC/MC12bADC.c), so a cal write that lands on a running session
 * rescales every sample from that instant on -- inside a stream whose header
 * was already emitted, and with no marker in the data saying where the scale
 * changed. That is the same consequence #873 describes for the AD7609 Range
 * pin, except this pair is reachable on the NQ1 this bench has.
 *
 * MC12b ONLY, and the AD7609 half is deliberately NOT claimed: an earlier
 * revision of this comment said "and by AD7609_ConvertToVoltage", which is
 * FALSE. That function's first statement is UNUSED(runtimeConfig)
 * (HAL/ADC/AD7609.c) -- it reads the module Range and nothing else, so on an
 * NQ3 these coefficients never reach the conversion and this guard is
 * defensive there rather than load-bearing. The wrong claim mattered because
 * it WAS the stated justification for refusing the command during an AD7609
 * stream (codex pre-merge audit). That the AD7609 path ignores user
 * calibration outright is a separate defect, filed on its own.
 *
 * Note what this does NOT change: CONF:ADC:SAVEcal / SAVEFcal stay unguarded.
 * They copy runtime cal INTO NVM and mutate nothing the conversion path reads,
 * and every other NVM-persisting setter in the tree (CONF:VOLTage:SAVE,
 * SYSTem:NAME, the LAN saves) is likewise unguarded -- guarding these two
 * would be a new and inconsistent restriction, not this fix.
 *
 * The claim is the first statement (#862 ordering contract, SCPIInterface.h),
 * so `CONF:ADC:chanCALM 300,1.0` mid-stream answers -200 like every other
 * converted setter rather than the -222 AdcChannelArgInRange would give.
 * (300, not 99: 99 is <= 255 so it PASSES AdcChannelArgInRange and then fails
 * the ADC_FindChannelIndex bound below, which returns SCPI_RES_ERR without
 * pushing anything ITSELF. libscpi then queues its generic -200 --
 * processCommand does `if (!context->cmd_error) SCPI_ErrorPush(...
 * EXECUTION_ERROR)` -- so the command is not literally silent, it is
 * UNSPECIFIC: -200 where TWO siblings push a specific -222 at that same
 * ADC_FindChannelIndex bound -- MEASure:VOLTage:DC? and the two-arg
 * CONF:ADC:CHANnel. Only CONF:ADC:CHANnel also LOG_Es the channel;
 * MEAS:VOLT:DC? pushes a BARE -222 (SCPI_ErrorPush passes a NULL info string)
 * with no log, so nothing there names the channel either. Pre-existing, shared
 * with chanCALB and both SINGleend paths, and NOT this change's subject --
 * filed as #888; the site to converge the other four onto is
 * CONF:ADC:CHANnel's logged form, not MEAS:VOLT:DC?'s bare push.
 *
 * Successive revisions of this paragraph have said "without pushing anything
 * at all", called it a silent-error path, called MEASure:VOLTage:DC? the one
 * site that gets it right, and said that site names the channel. All four were
 * wrong; the corrections came from the pre-merge audits, not from a reader
 * trusting the paragraph.) */
scpi_result_t SCPI_ADCChanCalmSet(scpi_t * context) {
    StreamingCfgClaim claim = Streaming_BeginConfigChange();
    if (claim != STREAM_CFG_CLAIM_OK) {
        return SCPI_RejectCfgClaim(context,
                                   claim == STREAM_CFG_CLAIM_BUSY,
                                   "CONF:ADC:chanCALM");
    }
    scpi_result_t result = ADCChanCalmSetClaimed(context);
    Streaming_EndConfigChange();
    return result;
}

static scpi_result_t ADCChanCalmSetClaimed(scpi_t * context) {
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

    // #877: reject before the (uint8_t) narrowing -- see
    // AdcChannelArgInRange.
    if (!AdcChannelArgInRange(context, param1, "CONF:ADC:chanCALM")) {
        return SCPI_RES_ERR;
    }
    size_t index = ADC_FindChannelIndex((uint8_t) param1);
    if (index >= pBoardConfigAInChannels->Size) {
        return SCPI_RES_ERR;
    }

    pRunTimeAInChannels->Data[index].CalM = param2;
    return SCPI_RES_OK;
}

static scpi_result_t ADCChanCalbSetClaimed(scpi_t * context);

/* #885: the offset half of the pair above -- see the comment on
 * SCPI_ADCChanCalmSet for why a live cal write is a stream-integrity defect
 * and why the SAVE commands are deliberately left alone. */
scpi_result_t SCPI_ADCChanCalbSet(scpi_t * context) {
    StreamingCfgClaim claim = Streaming_BeginConfigChange();
    if (claim != STREAM_CFG_CLAIM_OK) {
        return SCPI_RejectCfgClaim(context,
                                   claim == STREAM_CFG_CLAIM_BUSY,
                                   "CONF:ADC:chanCALB");
    }
    scpi_result_t result = ADCChanCalbSetClaimed(context);
    Streaming_EndConfigChange();
    return result;
}

static scpi_result_t ADCChanCalbSetClaimed(scpi_t * context) {
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

    // #877: reject before the (uint8_t) narrowing -- see
    // AdcChannelArgInRange.
    if (!AdcChannelArgInRange(context, param1, "CONF:ADC:chanCALB")) {
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

    // #877: reject before the (uint8_t) narrowing -- see
    // AdcChannelArgInRange.
    if (!AdcChannelArgInRange(context, param1, "CONF:ADC:chanCALM?")) {
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

    // #877: reject before the (uint8_t) narrowing -- see
    // AdcChannelArgInRange.
    if (!AdcChannelArgInRange(context, param1, "CONF:ADC:chanCALB?")) {
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

/* #885: LOADcal / LOADFcal overwrite EVERY channel's CalM and CalB from NVM,
 * and those two are read per conversion (see SCPI_ADCChanCalmSet above), so
 * this is the whole-board version of the same defect -- a mid-session load
 * rescales all sixteen channels at once. It is also the exact mutation that
 * CONF:ADC:USECal already performs UNDER the claim
 * (ADCUseCalSetClaimed -> daqifi_settings_LoadADCCalSettings, below): two
 * commands reaching one store, one of them guarded and one not.
 *
 * No `...Claimed` split here, unlike the setters above. That split exists to
 * make the release unconditional across a body with many returns; this body is
 * one call and has a single path out, so the wrapper would add indirection
 * without adding a guarantee. One claim, one release, one return.
 *
 * The two commands share this helper but each names ITSELF in the refusal --
 * they are distinct commands selecting distinct NVM banks, not two spellings
 * of one node the way CONF:ADC:SAMC:DEDicated/SHARed are. */
static scpi_result_t CalLoadCommon(scpi_t * context,
                                   DaqifiSettingsType type,
                                   const char * what) {
    StreamingCfgClaim claim = Streaming_BeginConfigChange();
    if (claim != STREAM_CFG_CLAIM_OK) {
        return SCPI_RejectCfgClaim(context,
                                   claim == STREAM_CFG_CLAIM_BUSY,
                                   what);
    }
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_AIN_CHANNELS);
    scpi_result_t result = daqifi_settings_LoadADCCalSettings(
            type, pRuntimeAInChannels) ? SCPI_RES_OK : SCPI_RES_ERR;
    Streaming_EndConfigChange();
    return result;
}

scpi_result_t SCPI_ADCCalLoad(scpi_t * context) {
    return CalLoadCommon(context, DaqifiSettings_UserAInCalParams,
                         "CONF:ADC:LOADcal");
}

scpi_result_t SCPI_ADCCalFLoad(scpi_t * context) {
    return CalLoadCommon(context, DaqifiSettings_FactAInCalParams,
                         "CONF:ADC:LOADFcal");
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