#include "BoardRuntimeConfig.h"

// The board configuration
// TODO: It would be handly if this was at a special place in memory so we could flash just the board config (vs recompiling the firmware w/ a different configuration)
const tBoardRuntimeConfig g_NQ3BoardRuntimeConfig = {
    .DIOChannels = {
        .Data = {
            {.IsInput = true, .IsReadOnly = false, .Value = false},
            {true, false, false},
            {true, false, false},
            {true, false, false},
            {true, false, false},
            {true, false, false},
            {true, false, false},
            {true, false, false},
            {true, false, false},
            {true, false, false},
            {true, false, false},
            {true, false, false},
            {true, false, false},
            {true, false, false},
            {true, false, false},
            {true, false, false},
        },
        .Size = 16,
    },
    .AInModules = {
        .Data = {
            {.IsEnabled = true, .Range = 5.0},
            {true , 5.0}
        },
        .Size = 2,
    },
    .AInChannels = {
        .Data = {
            // Public External ADC AD7175
            {.IsEnabled = true, .IsDifferential = false, .Frequency = 0, .CalM = 1, .CalB = 0},
            {true, false, 0, 1, 0},
            {true, false, 0, 1, 0},
            {true, false, 0, 1, 0},
            {true, false, 0, 1, 0},
            {true, false, 0, 1, 0},
            {true, false, 0, 1, 0},
            {true, false, 0, 1, 0},
            {true, false, 0, 1, 0},
            {true, false, 0, 1, 0},
            {true, false, 0, 1, 0},
            {true, false, 0, 1, 0},
            {true, false, 0, 1, 0},
            {true, false, 0, 1, 0},
            {true, false, 0, 1, 0},
            {true, false, 0, 1, 0},
            
            // Private Internal ADC
            {.IsEnabled = true, .IsDifferential = false, .Frequency = 0, .CalM = 2, .CalB = 0},
            {true, false, 0, 2, 0},
            {true, false, 0, 2, 0},
            {true, false, 0, 4.329, 0},
            {true, false, 0, 4.329, 0},
            {true, false, 0, 1, 0},
            {true, false, 0, 4.329, 0},
            {true, false, 0, 2, 0},
        },
        .Size = 24
    },
    .PowerWriteVars = {
       .EN_Vref_Val = false,    // Vref rail off
       .EN_3_3V_Val = false,     // 3.3V rail off
       .EN_5_10V_Val = false,   // 5V rail off
       .EN_5V_ADC_Val = false,   // 5V ADC rail off
       .EN_12V_Val = true,      // 12V rail off (inverse logic)
    },
    .UIWriteVars = {
        .LED1 = false,
        .LED2 = false,
    },
    .StreamingConfig = {
        .IsEnabled = false,
        .TimerHandle = 0,
        .Running = false,
        .ClockDivider = 50000000,   // 1 sec default
        .StreamCountTrigger = 0,
        .StreamCount = 0,
        .Encoding = Streaming_ProtoBuffer,
        .TSTimerHandle = 0,
        .TSClockDivider = 0xFFFFFFFF,   // maximum
    },
    //.wifiSettings = {0},
    //.usbSettings = {0},
    //.serverData = {0}
};
