#include "SCPIADC.h"

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
#include "state/runtime/BoardRuntimeConfig.h"
#include "HAL/ADC.h"
#include "../daqifi_settings.h"
#include "HAL/TimerApi/TimerApi.h"

scpi_result_t SCPI_ADCVoltageGet(scpi_t * context) {
    int channel;
    AInSample *pAInLatest;
    uint32_t *pAInLatestSize;
    AInArray * pBoardConfigAInChannels = BoardConfig_Get(
            BOARDCONFIG_AIN_CHANNELS,
            0);
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(
            BOARDRUNTIMECONFIG_AIN_CHANNELS);

    if (SCPI_ParamInt32(context, &channel, FALSE)) {
        // Get single
        volatile double val = 0;
        size_t index = ADC_FindChannelIndex((uint8_t) channel);
        if (index > pBoardConfigAInChannels->Size) {
            return SCPI_RES_ERR;
        }

        if (!pRuntimeAInChannels->Data[index].IsEnabled) {
            SCPI_ResultDouble(context, 0.0);
            return SCPI_RES_OK;
        }

        pAInLatest = BoardData_Get(
                BOARDDATA_AIN_LATEST,
                index);

        if (pAInLatest == NULL) {
            SCPI_ResultDouble(context, 0.0);
            return SCPI_RES_OK;
        }

        val = ADC_ConvertToVoltage(pAInLatest);
        SCPI_ResultDouble(context, val);
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
                SCPI_ResultDouble(context, 0.0);
            } else {
                SCPI_ResultDouble(context, pAInLatest->Value);
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

    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }

    if (SCPI_ParamInt32(context, &param2, FALSE)) {
        size_t channelIndex = ADC_FindChannelIndex((uint8_t) param1);
        AInRuntimeConfig* channelRuntimeConfig =
                &pRuntimeAInChannels->Data[channelIndex];
        AInChannel* channel = &pBoardConfigAInChannels->Data[channelIndex];
        const AInModule* module = ADC_FindModule(channel->Type);

        // Single channel
        if (channelIndex > pBoardConfigAInChannels->Size) {
            return SCPI_RES_ERR;
        }
        
        // Board variant-aware channel enable logic
        uint8_t boardVariant = pBoardConfig->BoardVariant;
        uint8_t channelId = (uint8_t) param1;
        
        switch (boardVariant) {
            case 1: // NQ1: User channels 0-15 (MC12bADC), monitoring channels always on
                if (channelId <= 15) {
                    if (module->Type == AIn_MC12bADC && channel->Config.MC12b.IsPublic) {
                        channelRuntimeConfig->IsEnabled = (param2 > 0);
                    } else {
                        return SCPI_RES_ERR; // Private or wrong type
                    }
                } else {
                    return SCPI_RES_ERR; // Monitoring channels not user-controllable
                }
                break;
                
            case 3: // NQ3: User channels 0-7 (AD7609), monitoring channels always on
                if (channelId <= 7) {
                    if (module->Type == AIn_AD7609) {
                        channelRuntimeConfig->IsEnabled = (param2 > 0);
                    } else {
                        return SCPI_RES_ERR; // Wrong type for NQ3 user channels
                    }
                } else {
                    return SCPI_RES_ERR; // Monitoring channels not user-controllable
                }
                break;
                
            default: // NQ2 or unknown variants
                // Legacy behavior for compatibility
                if (module->Type == AIn_MC12bADC) {
                    if (channel->Config.MC12b.IsPublic) {
                        channelRuntimeConfig->IsEnabled = (param2 > 0);
                    } else {
                        return SCPI_RES_ERR;
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
    bool hasActiveAD7609Channels __attribute__((unused)) = false;
    uint64_t freq = pRunTimeStreamConfig->Frequency;
    uint32_t clkFreq = TimerApi_FrequencyGet(pBoardConfig->StreamingConfig.TimerIndex);
    int i;
    
    // Count active channels and detect AD7609 usage
    for (i = 0; i < pBoardConfigAInChannels->Size; i++) {
        if (pRuntimeAInChannels->Data[i].IsEnabled == 1) {
            if (pBoardConfigAInChannels->Data[i].Type == AIn_AD7609) {
                hasActiveAD7609Channels = true;
            } else if (pBoardConfigAInChannels->Data[i].Type == AIn_MC12bADC && 
                       pBoardConfigAInChannels->Data[i].Config.MC12b.ChannelType == 1) {
                activeType1ChannelCount++;
            }
        }
    }
    
    // Note: Internal monitoring channels are configured with fixed 1Hz in NQ3 runtime config
    /**
     * The maximum aggregate trigger frequency for all active Type 1 ADC channels is 15,000 Hz.
     * For example, if two Type 1 channels are active, each can trigger at a maximum frequency of 7,500 Hz (15,000 / 2).
     * 
     * The maximum triggering frequency of non type 1 channel is 1000 hz, 
     * which is obtained by dividing Frequency with ChannelScanFreqDiv. 
     * Non-Type 1 channels are setup for channel scanning
     * 
     */
    if (activeType1ChannelCount > 0 && (freq * activeType1ChannelCount) > 15000) {
        freq = 15000 / activeType1ChannelCount;
    }
    
    // Prevent divide by zero exception
    if (freq == 0) {
        freq = 1000; // Default to 1kHz if frequency is 0
    }
    
    pRunTimeStreamConfig->ClockPeriod = clkFreq / freq;
    pRunTimeStreamConfig->Frequency = freq;
    pRunTimeStreamConfig->TSClockPeriod = 0xFFFFFFFF;
    if (freq > 1000) {
        pRunTimeStreamConfig->ChannelScanFreqDiv = freq / 1000;
    } else {
        pRunTimeStreamConfig->ChannelScanFreqDiv = 1;
    }

    if (ADC_WriteChannelStateAll()) {
        return SCPI_RES_OK;
    } else {
        return SCPI_RES_ERR;
    }
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
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }

    // Get runtime modules configuration
    AInModRuntimeArray* pRuntimeModules = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_AIN_MODULES);
    uint8_t moduleIndex = AIn_AD7609;  // Use module type as index

    // Validate runtime configuration and module index
    if (pRuntimeModules == NULL || moduleIndex >= pRuntimeModules->Size) {
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
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

    // Convert parameter to voltage range and store
    double rangeVoltage = (rangeParam == 1) ? 10.0 : 5.0;
    pRuntimeModules->Data[moduleIndex].Range = rangeVoltage;

    // Update hardware pin (Range_Pin: LOW=±10V, HIGH=±5V)
    bool range10V = (rangeParam == 1);
    GPIO_PinWrite(module->Config.AD7609.Range_Pin, !range10V);

    LOG_I("AD7609 module range set to ±%.1fV", rangeVoltage);

    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCChanRangeGet(scpi_t * context) {
    // Find the AD7609 module
    const AInModule* module = ADC_FindModule(AIn_AD7609);
    if (module == NULL) {
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
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

    if (!SCPI_ParamInt32(context, &param1, TRUE)) {
        return SCPI_RES_ERR;
    }

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

    if (daqifi_settings_LoadFromNvm(DaqifiSettings_TopLevelSettings, &tmpTopLevelSettings)) {
        SCPI_ResultInt32(context, tmpTopLevelSettings.settings.topLevelSettings.calVals);
        return SCPI_RES_OK;
    } else {
        return SCPI_RES_ERR;
    }
}