#pragma once

#include "BoardConfig.h"
#include "../../config/default/peripheral/adchs/plib_adchs.h"

/**
 * @file CommonMonitoringChannels.h
 * @brief Common internal monitoring ADC channels shared across all Nyquist board variants
 *
 * All variants use the same 8 internal MC12bADC channels for system health monitoring:
 * - 3.3V rail monitoring
 * - 2.5V reference monitoring
 * - Battery voltage monitoring
 * - 5V rail monitoring
 * - 10V rail monitoring
 * - Temperature sensor (disabled due to PIC32MZ silicon errata)
 * - 5V reference monitoring
 * - System voltage monitoring
 *
 * These channels are hardware-identical across NQ1, NQ2, and NQ3 variants.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Initializer array for the 8 common internal monitoring channels (MC12bADC).
 * Use this in the variant-specific BoardConfig after the user-facing channels.
 *
 * Channel mapping (indices 8-15 for NQ3, indices 16-23 for NQ1):
 * [0] = ADC_CHANNEL_3_3V    (ADCHS_CH19)
 * [1] = ADC_CHANNEL_2_5VREF (ADCHS_CH31)
 * [2] = ADC_CHANNEL_VBATT   (ADCHS_CH30)
 * [3] = ADC_CHANNEL_5V      (ADCHS_CH42)
 * [4] = ADC_CHANNEL_10V     (ADCHS_CH32)
 * [5] = ADC_CHANNEL_TEMP    (ADCHS_CH44) - Disabled due to silicon errata
 * [6] = ADC_CHANNEL_5VREF   (ADCHS_CH29)
 * [7] = ADC_CHANNEL_VSYS    (ADCHS_CH41)
 */
#define COMMON_MONITORING_CHANNELS_BOARDCONFIG \
    { \
        .DaqifiAdcChannelId = ADC_CHANNEL_3_3V, \
        .Type = AIn_MC12bADC, \
        .Config = {.MC12b = {false, ADCHS_CH19, ADCHS_MODULE7_MASK, 2, false, 1}} \
    }, \
    { \
        .DaqifiAdcChannelId = ADC_CHANNEL_2_5VREF, \
        .Type = AIn_MC12bADC, \
        .Config = {.MC12b = {false, ADCHS_CH31, ADCHS_MODULE7_MASK, 2, false, 1}} \
    }, \
    { \
        .DaqifiAdcChannelId = ADC_CHANNEL_VBATT, \
        .Type = AIn_MC12bADC, \
        .Config = {.MC12b = {false, ADCHS_CH30, ADCHS_MODULE7_MASK, 2, false, 1}} \
    }, \
    { \
        .DaqifiAdcChannelId = ADC_CHANNEL_5V, \
        .Type = AIn_MC12bADC, \
        .Config = {.MC12b = {false, ADCHS_CH42, ADCHS_MODULE7_MASK, 2, false, 2.16666666667}} \
    }, \
    { \
        .DaqifiAdcChannelId = ADC_CHANNEL_10V, \
        .Type = AIn_MC12bADC, \
        .Config = {.MC12b = {false, ADCHS_CH32, ADCHS_MODULE7_MASK, 2, false, 3.905000000000}} \
    }, \
    { \
        .DaqifiAdcChannelId = ADC_CHANNEL_TEMP, \
        .Type = AIn_MC12bADC, \
        .Config = {.MC12b = { \
            .AllowDifferential = false, \
            .ChannelId = ADCHS_CH44, \
            .ModuleId = ADCHS_MODULE7_MASK, \
            .ChannelType = 2, \
            .IsPublic = false, \
            .InternalScale = 1, \
            .IsTemperatureSensor = true, \
            .TempOffsetVoltage = 0.5, \
            .TempSensitivity = 0.005, \
            .TempReferenceC = -40.0 \
        }} \
    }, \
    { \
        .DaqifiAdcChannelId = ADC_CHANNEL_5VREF, \
        .Type = AIn_MC12bADC, \
        .Config = {.MC12b = {false, ADCHS_CH29, ADCHS_MODULE7_MASK, 2, false, 2.16666666667}} \
    }, \
    { \
        .DaqifiAdcChannelId = ADC_CHANNEL_VSYS, \
        .Type = AIn_MC12bADC, \
        .Config = {.MC12b = {false, ADCHS_CH41, ADCHS_MODULE7_MASK, 2, false, 1.409090909091}} \
    }

/**
 * Runtime defaults for the 8 common internal monitoring channels.
 * Use this in the variant-specific RuntimeDefaults after the user-facing channels.
 *
 * All channels enabled at 1Hz for continuous system health monitoring,
 * except TEMP which is disabled due to PIC32MZ silicon errata.
 */
#define COMMON_MONITORING_CHANNELS_RUNTIME \
    {true, false, 1, 1, 0},  /* ADC_CHANNEL_3_3V - 1Hz monitoring */ \
    {true, false, 1, 1, 0},  /* ADC_CHANNEL_2_5VREF - 1Hz monitoring */ \
    {true, false, 1, 1, 0},  /* ADC_CHANNEL_VBATT - 1Hz monitoring */ \
    {true, false, 1, 1, 0},  /* ADC_CHANNEL_5V - 1Hz monitoring */ \
    {true, false, 1, 1, 0},  /* ADC_CHANNEL_10V - 1Hz monitoring */ \
    {false, false, 0, 1, 0}, /* ADC_CHANNEL_TEMP - DISABLED: PIC32MZ silicon errata */ \
    {true, false, 1, 1, 0},  /* ADC_CHANNEL_5VREF - 1Hz monitoring */ \
    {true, false, 1, 1, 0}   /* ADC_CHANNEL_VSYS - 1Hz monitoring */

#ifdef __cplusplus
}
#endif
