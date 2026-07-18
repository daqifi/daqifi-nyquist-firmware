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

    if (SCPI_ParamInt32(context, &channel, FALSE)) {
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

scpi_result_t SCPI_ADCChanEnableSet(scpi_t * context) {
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

    // #116: reject channel enable/disable while streaming is active. The sample
    // pool element stride is fixed at StartStreamData for the then-current channel
    // count (AInSampleList_InitializeExternal is only called at stream start, never
    // mid-stream), so changing the channel set live would desync the pool layout
    // from the ISR write width. The old behavior here silently re-capped the
    // frequency (LOG_I only) without re-partitioning the pool — unsound and
    // invisible to the client. Mirror SCPI_MemRejectIfStreaming: stop streaming,
    // reconfigure, restart. Rejecting BEFORE ADC_WriteChannelStateAll() leaves both
    // runtime config and ADC hardware untouched (no snapshot/rollback needed).
    //
    // Use IsEnabled || Running (not &&): the two flags are set/cleared in separate
    // steps at stream start/stop (StartStreaming arms IsEnabled, then
    // Streaming_UpdateState flips Running; stop clears them in turn). An && guard
    // would leave a transition window — IsEnabled set but Running not yet, or vice
    // versa — through which a concurrent SCPI session (USB pri 7 vs WiFi pri 2)
    // could slip a channel change after the pool/mapping was sized. Reject unless
    // streaming is FULLY idle (both flags clear).
    if (pRunTimeStreamConfig->IsEnabled || pRunTimeStreamConfig->Running) {
        LOG_E("Channel enable rejected: streaming is active (stop streaming first)");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }

    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }

    if (SCPI_ParamInt32(context, &param2, FALSE)) {
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

    // If streaming is globally disabled, channel states updated but no timer recalculation needed
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

    if (SCPI_ParamInt32(context, &param1, FALSE)) {
        // Single channel
        size_t index = ADC_FindChannelIndex((uint8_t) param1);
        // TODO: This function should be able to read which version of the board we are using and assign the ADC channels associated that version
        if (index > pBoardConfigAInChannels->Size) {
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

    if (SCPI_ParamInt32(context, &param2, FALSE)) {
        // Single channel
        size_t index = ADC_FindChannelIndex((uint8_t) param1);
        if (index > pBoardConfigAInChannels->Size) {
            return SCPI_RES_ERR;
        }

        pRuntimeAInChannels->Data[index].IsDifferential = (param2 == 0);
    } else {
        pAInLatestSize = BoardData_Get(
                BOARDDATA_AIN_LATEST_SIZE,
                0);

        size_t i = 0;
        for (i = 0; i<*pAInLatestSize; ++i) {
            pRuntimeAInChannels->Data[i].IsDifferential =
                    (param2 & (1 << i)) == 0;
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
    if (SCPI_ParamInt32(context, &param1, FALSE)) {
        // Single channel
        size_t index = ADC_FindChannelIndex((uint8_t) param1);
        if (index > pBoardConfigAInChannels->Size) {
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
        for (i = 0; i<*pAInLatestSize; ++i) {
            if (!pRuntimeAInChannels->Data[i].IsDifferential) {
                mask |= (1 << i);
            }
        }
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

    LOG_I("AD7609 module range set to ±%.1fV", rangeVoltage);

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
    if (index > pBoardConfigAInChannels->Size) {
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
    if (index > pBoardConfigAInChannels->Size) {
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
    if (index > pBoardConfigAInChannels->Size) {
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
    if (index > pBoardConfigAInChannels->Size) {
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

scpi_result_t SCPI_ADCUseCalSet(scpi_t * context) {
    int param1;
    DaqifiSettings tmpTopLevelSettings;
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_AIN_CHANNELS);
    StreamingRuntimeConfig * pRunTimeStreamConfig = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);

    // #158/#270: this command also switches the encoder output format
    // (value 2 = raw codes), so changing it mid-stream would alter the wire
    // format under an active session. Reject while streaming (mirrors the
    // CONF:ADC:CHANnel #116 guard); it also protects the mid-stream cal
    // coefficient reload for values 0/1.
    if (pRunTimeStreamConfig->IsEnabled || pRunTimeStreamConfig->Running) {
        LOG_E("Calibration-mode change rejected: streaming is active (stop streaming first)");
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }

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

scpi_result_t SCPI_ADCOnboardDiagSet(scpi_t * context) {
    int32_t val;
    if (!SCPI_ParamInt32(context, &val, TRUE)) return SCPI_RES_ERR;
    if (val < 0 || val > 1) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }
    StreamingRuntimeConfig *pStreamCfg = BoardRunTimeConfig_Get(
            BOARDRUNTIME_STREAMING_CONFIGURATION);
    // IsEnabled || Running (matches the #116 channel-enable gate): the two
    // flags transition in separate steps at start/stop, and a change slipped
    // through the window would desync the session scan list (#541 D-B builds
    // ADCCSS from OnboardDiagEnabled at stream start).
    if (pStreamCfg->IsEnabled || pStreamCfg->Running) {
        SCPI_ExecutionError(context, "CONF:ADC:OBDiag: cannot change while streaming");
        return SCPI_RES_ERR;
    }
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
scpi_result_t SCPI_ADCThresholdSet(scpi_t * context) {
    int32_t ch, mode, lo = 0, hi = 0;
    if (!SCPI_ParamInt32(context, &ch, TRUE))   return SCPI_RES_ERR;
    if (!SCPI_ParamInt32(context, &mode, TRUE)) return SCPI_RES_ERR;
    bool haveLo = SCPI_ParamInt32(context, &lo, FALSE);
    bool haveHi = SCPI_ParamInt32(context, &hi, FALSE);
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
    // Config change: reject while streaming (consistent with the CONF:ADC family;
    // the session scan is frozen at start, #116/#541).
    StreamingRuntimeConfig *pStreamCfg =
            BoardRunTimeConfig_Get(BOARDRUNTIME_STREAMING_CONFIGURATION);
    if (pStreamCfg->IsEnabled || pStreamCfg->Running) {
        SCPI_ExecutionError(context, "CONF:ADC:THRE: cannot change while streaming");
        return SCPI_RES_ERR;
    }
    const char* err = NULL;
    if (!AdcThreshold_Configure((uint8_t)ch, (AdcThresholdMode)mode,
                                (uint16_t)lo, (uint16_t)hi, &err)) {
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

// TEMP DEBUG (#670 bring-up): dump comparator + interrupt regs for unit 1.
scpi_result_t SCPI_ADCThresholdDbg(scpi_t * context) {
    SCPI_ResultUInt32Base(context, ADCCMPCON1, 16);  // ENDCMP/DCMPGIEN/DCMPED/AINID/IE*
    SCPI_ResultUInt32Base(context, ADCCMPEN1, 16);   // AN watch mask
    SCPI_ResultUInt32Base(context, ADCCMP1, 16);     // DCMPHI:DCMPLO
    SCPI_ResultUInt32Base(context, IFS1, 16);        // bit14 = ADCDC1IF
    SCPI_ResultUInt32Base(context, IEC1, 16);        // bit14 = ADCDC1IE
    SCPI_ResultUInt32Base(context, IPC11, 16);       // ADCDC1IP bits 20:18
    return SCPI_RES_OK;
}

// CONF:ADC:THREshold:CLEar [<ch>] -> clear latch+counter (no arg = all)
scpi_result_t SCPI_ADCThresholdClear(scpi_t * context) {
    int32_t ch;
    if (!SCPI_ParamInt32(context, &ch, FALSE)) {
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
static scpi_result_t SamcSetCommon(scpi_t *context, bool isDedicated) {
    int32_t val;
    if (!SCPI_ParamInt32(context, &val, TRUE)) return SCPI_RES_ERR;
    if (val < 0 || val > (int32_t)MC12B_SAMC_MAX) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }
    StreamingRuntimeConfig *pStreamCfg =
            BoardRunTimeConfig_Get(BOARDRUNTIME_STREAMING_CONFIGURATION);
    // IsEnabled || Running (matches #116 / OBDiag): SAMC feeds the live
    // scan-rate bound (#541 D-C reads ADCCON2.SAMC), so a mid-stream change
    // would invalidate the cap the session was admitted under.
    if (pStreamCfg->IsEnabled || pStreamCfg->Running) {
        SCPI_ExecutionError(context, "CONF:ADC:SAMC: cannot change while streaming");
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