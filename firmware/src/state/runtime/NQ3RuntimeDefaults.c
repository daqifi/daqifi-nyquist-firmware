#include "BoardRuntimeConfig.h"

// The NQ3 board runtime configuration
const tBoardRuntimeConfig g_NQ3BoardRuntimeConfig = {
    .DIOGlobalEnable = false,
    .AInModules =
    {
        .Data =
        {
            {.IsEnabled = true, .Range = 5.0},  // MC12bADC module
            {.IsEnabled = true, .Range = 10.0}, // AD7609 module (±10V range)
        },
        .Size = 2,
    },
    .AInChannels =
    {
        .Data =
        {
            // AD7609 channels 0-7 (user-accessible)
            {.IsEnabled = false, .IsDifferential = false, .Frequency = 0, .CalM = 1, .CalB = 0},
            {false, false, 0, 1, 0},
            {false, false, 0, 1, 0},
            {false, false, 0, 1, 0},
            {false, false, 0, 1, 0},
            {false, false, 0, 1, 0},
            {false, false, 0, 1, 0},
            {false, false, 0, 1, 0},
            // Internal monitoring channels (MC12bADC) - always enabled at 1Hz for system health
            {true, false, 1, 1, 0}, // ADC_CHANNEL_3_3V - 1Hz monitoring
            {true, false, 1, 1, 0}, // ADC_CHANNEL_2_5VREF - 1Hz monitoring  
            {true, false, 1, 1, 0}, // ADC_CHANNEL_VBATT - 1Hz monitoring
            {true, false, 1, 1, 0}, // ADC_CHANNEL_5V - 1Hz monitoring
            {true, false, 1, 1, 0}, // ADC_CHANNEL_10V - 1Hz monitoring
            {true, false, 1, 1, 0}, // ADC_CHANNEL_TEMP - 1Hz monitoring
            {true, false, 1, 1, 0}, // ADC_CHANNEL_5VREF - 1Hz monitoring
            {true, false, 1, 1, 0}, // ADC_CHANNEL_VSYS - 1Hz monitoring
        },
        .Size = 16,
    },
    .PowerWriteVars = {
       .EN_3_3V_Val = true,     // 3.3V rail on
       .EN_5_10V_Val = false,   // 5V rail off initially
       .EN_12V_Val = true,      // 12V rail on (NQ3 supports 12V)
       .EN_Vref_Val = false,    // Vref rail off initially
       .BQ24297WriteVars.OTG_Val = true, // OTG mode for battery operation
    },
    .UIWriteVars = {
        .LED1 = false,
        .LED2 = false,
    },
    .StreamingConfig = {
        .IsEnabled = false,
        .Running = false,
        .ClockPeriod = 130,   // default 3k hz
        .Frequency = 1000,    // Default 1kHz (never 0 to avoid divide by zero)
        .ChannelScanFreqDiv = 3, // max channel scan frequency 1000 hz
        .Encoding = Streaming_ProtoBuffer,
        .TSClockPeriod = 0xFFFFFFFF,   // maximum
    },
};