#include "BoardConfig.h"
#include "CommonBoardPinDefs.h"
#include "CommonBoardConfig.h"
#include "CommonMonitoringChannels.h"
#include "SPIConfigEnums.h"
#include "../../HAL/TimerApi/TimerApi.h"
#include "../../HAL/DAC7718/DAC7718.h"

// CSV column headers now in CommonBoardConfig.c

// The board configuration
// TODO: It would be handy if this was at a special place in memory so we could flash just the board config (vs recompiling the firmware w/ a different configuration)

// Common port definitions now in CommonBoardPinDefs.h

// NQ2-specific pin definitions (peripheral modules use Pin API, not Port API)
#define DAC7718_CS_PIN             GPIO_PIN_RK0    // DAC CS on RK0
#define DAC7718_RST_PIN            GPIO_PIN_RJ13   // DAC CLR/RST on RJ13

// PORTS_REMAP_OUTPUT_PIN enum now in CommonBoardPinDefs.h
const tBoardConfig NQ2BoardConfig = {
    .BoardVariant = 2,
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
                .Type = AIn_AD7173,
                .Config = {
                    .AD7173 = {
                        .SPI = {
                            .spiID = HAL_SPI_ID_6,  // SPI6 configured in MCC
                            .baud = 15000000,
                            .clock = HAL_SPI_CLOCK_PBCLK,
                            .busClk_id = HAL_CLK_BUS_PERIPHERAL_2,
                            .clockPolarity = HAL_SPI_CLOCK_POLARITY_IDLE_HIGH,
                            .busWidth = HAL_SPI_WIDTH_8BITS,
                            .inSamplePhase = HAL_SPI_CLOCK_PHASE_LEADING_EDGE,
                            .outDataPhase = HAL_SPI_OUTPUT_PHASE_IDLE_TO_ACTIVE,
                        },
                        .CS_Pin = GPIO_PIN_RB3,
                        .ERR_Pin = GPIO_PIN_RB2,
                        .SDI_Pin = GPIO_PIN_RF2,
                        .Resolution = 16777216,  // 24-bit ADC
                    }
                },
                .Size = 16
            },
        },
        .Size = 2
    },
    .AInChannels = {
        .Data = {
            // User-accessible AD7173 channels (0-15) - NQ2's main feature
            {
                .DaqifiAdcChannelId = 0,
                .Type = AIn_AD7173,
                .Config = {.AD7173 = {.ChannelNumber = 0, .IsPublic = true}}
            },
            {
                .DaqifiAdcChannelId = 1,
                .Type = AIn_AD7173,
                .Config = {.AD7173 = {.ChannelNumber = 1, .IsPublic = true}}
            },
            {
                .DaqifiAdcChannelId = 2,
                .Type = AIn_AD7173,
                .Config = {.AD7173 = {.ChannelNumber = 2, .IsPublic = true}}
            },
            {
                .DaqifiAdcChannelId = 3,
                .Type = AIn_AD7173,
                .Config = {.AD7173 = {.ChannelNumber = 3, .IsPublic = true}}
            },
            {
                .DaqifiAdcChannelId = 4,
                .Type = AIn_AD7173,
                .Config = {.AD7173 = {.ChannelNumber = 4, .IsPublic = true}}
            },
            {
                .DaqifiAdcChannelId = 5,
                .Type = AIn_AD7173,
                .Config = {.AD7173 = {.ChannelNumber = 5, .IsPublic = true}}
            },
            {
                .DaqifiAdcChannelId = 6,
                .Type = AIn_AD7173,
                .Config = {.AD7173 = {.ChannelNumber = 6, .IsPublic = true}}
            },
            {
                .DaqifiAdcChannelId = 7,
                .Type = AIn_AD7173,
                .Config = {.AD7173 = {.ChannelNumber = 7, .IsPublic = true}}
            },
            {
                .DaqifiAdcChannelId = 8,
                .Type = AIn_AD7173,
                .Config = {.AD7173 = {.ChannelNumber = 8, .IsPublic = true}}
            },
            {
                .DaqifiAdcChannelId = 9,
                .Type = AIn_AD7173,
                .Config = {.AD7173 = {.ChannelNumber = 9, .IsPublic = true}}
            },
            {
                .DaqifiAdcChannelId = 10,
                .Type = AIn_AD7173,
                .Config = {.AD7173 = {.ChannelNumber = 10, .IsPublic = true}}
            },
            {
                .DaqifiAdcChannelId = 11,
                .Type = AIn_AD7173,
                .Config = {.AD7173 = {.ChannelNumber = 11, .IsPublic = true}}
            },
            {
                .DaqifiAdcChannelId = 12,
                .Type = AIn_AD7173,
                .Config = {.AD7173 = {.ChannelNumber = 12, .IsPublic = true}}
            },
            {
                .DaqifiAdcChannelId = 13,
                .Type = AIn_AD7173,
                .Config = {.AD7173 = {.ChannelNumber = 13, .IsPublic = true}}
            },
            {
                .DaqifiAdcChannelId = 14,
                .Type = AIn_AD7173,
                .Config = {.AD7173 = {.ChannelNumber = 14, .IsPublic = true}}
            },
            {
                .DaqifiAdcChannelId = 15,
                .Type = AIn_AD7173,
                .Config = {.AD7173 = {.ChannelNumber = 15, .IsPublic = true}}
            },

            // Internal monitoring channels (from CommonMonitoringChannels.h)
            COMMON_MONITORING_CHANNELS_BOARDCONFIG
        },
        .Size = 24  // 16 AD7173 + 8 monitoring
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
                            .spiID = HAL_SPI_ID_2,          // SPI2 as specified
                            .baud = 10000000,               // 10 MHz SPI clock
                            .clock = HAL_SPI_CLOCK_PBCLK,
                            .busClk_id = HAL_CLK_BUS_PERIPHERAL_2,
                            .clockPolarity = HAL_SPI_CLOCK_POLARITY_IDLE_LOW,  // CPOL = 0 for DAC7718
                            .busWidth = HAL_SPI_WIDTH_8BITS,                   // 8-bit transfers
                            .inSamplePhase = HAL_SPI_CLOCK_PHASE_LEADING_EDGE,  // CPHA = 1 for DAC7718
                            .outDataPhase = HAL_SPI_OUTPUT_PHASE_IDLE_TO_ACTIVE,
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
    return &NQ2BoardConfig;
}
