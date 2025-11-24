#include "BoardConfig.h"
#include "CommonBoardPinDefs.h"
#include "CommonBoardConfig.h"
#include "CommonMonitoringChannels.h"
#include "../../HAL/TimerApi/TimerApi.h"
#include "../../HAL/DAC7718/DAC7718.h"

// CSV column headers now in CommonBoardConfig.c

// The board configuration
// TODO: It would be handy if this was at a special place in memory so we could flash just the board config (vs recompiling the firmware w/ a different configuration)

// Common port definitions now in CommonBoardPinDefs.h

// NQ3-specific pin definitions (peripheral modules use Pin API, not Port API)
#define DAC7718_CS_PIN             GPIO_PIN_RK0    // DAC CS on RK0
#define DAC7718_RST_PIN            GPIO_PIN_RJ13   // DAC CLR/RST on RJ13

// PORTS_REMAP_OUTPUT_PIN enum now in CommonBoardPinDefs.h
const tBoardConfig NQ3BoardConfig = {
    .BoardVariant = 3,
    // DIO channels - common config but can't extract #ifdef to macro
    .DIOChannels =
    {
        .Data = COMMON_DIO_CHANNELS_CONFIG_DATA,
#ifdef DIO_TIMING_TEST
        .Size = 15,
#else
        .Size = 16,
#endif
    },
    .AInModules =
    {
        .Data =
        {
            {
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    { .Resolution = 4096}},
                .Size = 16
            },
            {
                .Type = AIn_AD7609,
                .Config = {
                    .AD7609 = {
                        .SPI = {
                            .spiID = 6,  // SPI6 configured in MCC
                            .baud = 15000000,
                            .clock = 1,   
                            .busClk_id = 2,   
                            .clockPolarity = 1, 
                            .busWidth = 8,    
                            .inSamplePhase = 1,     
                            .outDataPhase = 0, 
                        },
                        .CS_Pin = GPIO_PIN_RH2,
                        .BSY_Pin = GPIO_PIN_RB3,
                        .RST_Pin = GPIO_PIN_RH3,
                        .STBY_Pin = GPIO_PIN_RK2,
                        .Range_Pin = GPIO_PIN_RK1,
                        .OS0_Pin = GPIO_PIN_RH7,
                        .OS1_Pin = GPIO_PIN_RK3,
                        .CONVST_Pin = GPIO_PIN_RB9,
                        .Range10V = true,  // Default ±10V range (runtime configurable via SCPI)
                        .OSMode = 0,
                        .Resolution = 262144,
                    }
                },
                .Size = 8
            },
        },
        .Size = 2
    },
    .AInChannels = {
        .Data = {
            // User-accessible AD7609 channels (0-7) - NQ3's main feature
            // Channel mapping: 1:1 (User channel N → Hardware channel N)
            {
                .DaqifiAdcChannelId = 0,
                .Type = AIn_AD7609,
                .Config = {.AD7609 = {.ChannelNumber = 0, .IsPublic = true}}
            },
            {
                .DaqifiAdcChannelId = 1,
                .Type = AIn_AD7609,
                .Config = {.AD7609 = {.ChannelNumber = 1, .IsPublic = true}}
            },
            {
                .DaqifiAdcChannelId = 2,
                .Type = AIn_AD7609,
                .Config = {.AD7609 = {.ChannelNumber = 2, .IsPublic = true}}
            },
            {
                .DaqifiAdcChannelId = 3,
                .Type = AIn_AD7609,
                .Config = {.AD7609 = {.ChannelNumber = 3, .IsPublic = true}}
            },
            {
                .DaqifiAdcChannelId = 4,
                .Type = AIn_AD7609,
                .Config = {.AD7609 = {.ChannelNumber = 4, .IsPublic = true}}
            },
            {
                .DaqifiAdcChannelId = 5,
                .Type = AIn_AD7609,
                .Config = {.AD7609 = {.ChannelNumber = 5, .IsPublic = true}}
            },
            {
                .DaqifiAdcChannelId = 6,
                .Type = AIn_AD7609,
                .Config = {.AD7609 = {.ChannelNumber = 6, .IsPublic = true}}
            },
            {
                .DaqifiAdcChannelId = 7,
                .Type = AIn_AD7609,
                .Config = {.AD7609 = {.ChannelNumber = 7, .IsPublic = true}}
            },

            // Internal monitoring channels (from CommonMonitoringChannels.h)
            COMMON_MONITORING_CHANNELS_BOARDCONFIG
        },
        .Size = 16
    },
    .AOutModules =
    {
        .Data =
        {
            {
                .Type = AOut_DAC7718,
                .Config = {
                    .DAC7718 = {
                        .SPI = {
                            .spiID = 2,          // SPI2 as specified
                            .baud = 10000000,    // 10 MHz SPI clock
                            .clock = 1,          
                            .busClk_id = 2,      
                            .clockPolarity = 0,  // CPOL = 0 for DAC7718
                            .busWidth = 8,       // 8-bit transfers
                            .inSamplePhase = 1,  // CPHA = 1 for DAC7718
                            .outDataPhase = 0,   
                        },
                        .CS_Pin = DAC7718_CS_PIN,     // CS on RK0
                        .RST_Pin = DAC7718_RST_PIN,   // CLR/RST on RJ13
                        .DAC_Range = 0,               // Default range setting
                        .Resolution = DAC7718_RESOLUTION,  // 12-bit DAC (4096 levels)
                        .MinVoltage = 0.0,            // Unipolar: 0V minimum
                        .MaxVoltage = 10.0,            // Maximum output voltage (software clamp)
                        .HardwareFullScale = 10.0,    // 10V full scale (4x gain configuration)
                    }
                },
                .Size = DAC7718_NUM_CHANNELS  // DAC7718 has 8 channels
            },
        },
        .Size = 1
    },
    .AOutChannels = {
        .Data = {
            // DAC7718 channel mapping: User 0-7 → DAC 3,2,1,0,7,6,5,4
            {
                .DaqifiDacChannelId = 0,
                .Type = AOut_DAC7718,
                .Config = {.DAC7718 = {.ChannelNumber = 3}}
            },
            {
                .DaqifiDacChannelId = 1,
                .Type = AOut_DAC7718,
                .Config = {.DAC7718 = {.ChannelNumber = 2}}
            },
            {
                .DaqifiDacChannelId = 2,
                .Type = AOut_DAC7718,
                .Config = {.DAC7718 = {.ChannelNumber = 1}}
            },
            {
                .DaqifiDacChannelId = 3,
                .Type = AOut_DAC7718,
                .Config = {.DAC7718 = {.ChannelNumber = 0}}
            },
            {
                .DaqifiDacChannelId = 4,
                .Type = AOut_DAC7718,
                .Config = {.DAC7718 = {.ChannelNumber = 7}}
            },
            {
                .DaqifiDacChannelId = 5,
                .Type = AOut_DAC7718,
                .Config = {.DAC7718 = {.ChannelNumber = 6}}
            },
            {
                .DaqifiDacChannelId = 6,
                .Type = AOut_DAC7718,
                .Config = {.DAC7718 = {.ChannelNumber = 5}}
            },
            {
                .DaqifiDacChannelId = 7,
                .Type = AOut_DAC7718,
                .Config = {.DAC7718 = {.ChannelNumber = 4}}
            },
        },
        .Size = 8
    },
    .PowerConfig = COMMON_POWER_CONFIG,
    .UIConfig = COMMON_UI_CONFIG,
    .StreamingConfig = COMMON_STREAMING_CONFIG,
    .csvChannelHeadersFirst = COMMON_CSV_CHANNEL_HEADERS_FIRST,
    .csvChannelHeadersSubsequent = COMMON_CSV_CHANNEL_HEADERS_SUBSEQUENT
};

/*! This function is used for getting a board configuration parameter
 * @return Pointer to Board Configuration structure
 */
const void *NqBoardConfig_Get( void )
{
    return &NQ3BoardConfig;
}