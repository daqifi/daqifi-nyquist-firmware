#include "BoardConfig.h"
#include "CommonBoardPinDefs.h"
#include "CommonMonitoringChannels.h"
#include "../../HAL/TimerApi/TimerApi.h"
#include "../../HAL/DAC7718/DAC7718.h"

// =============================================================================
// CSV Column Headers (pre-computed in program memory for fast header generation)
// =============================================================================
// Board-specific column names - NQ3 uses "ain" prefix for analog inputs
static const char* NQ3_CSV_CHANNEL_HEADERS_FIRST[] = {
    "ain0_ts,ain0_val",   "ain1_ts,ain1_val",   "ain2_ts,ain2_val",   "ain3_ts,ain3_val",
    "ain4_ts,ain4_val",   "ain5_ts,ain5_val",   "ain6_ts,ain6_val",   "ain7_ts,ain7_val",
    "ain8_ts,ain8_val",   "ain9_ts,ain9_val",   "ain10_ts,ain10_val", "ain11_ts,ain11_val",
    "ain12_ts,ain12_val", "ain13_ts,ain13_val", "ain14_ts,ain14_val", "ain15_ts,ain15_val"
};

static const char* NQ3_CSV_CHANNEL_HEADERS_SUBSEQUENT[] = {
    ",ain0_ts,ain0_val",   ",ain1_ts,ain1_val",   ",ain2_ts,ain2_val",   ",ain3_ts,ain3_val",
    ",ain4_ts,ain4_val",   ",ain5_ts,ain5_val",   ",ain6_ts,ain6_val",   ",ain7_ts,ain7_val",
    ",ain8_ts,ain8_val",   ",ain9_ts,ain9_val",   ",ain10_ts,ain10_val", ",ain11_ts,ain11_val",
    ",ain12_ts,ain12_val", ",ain13_ts,ain13_val", ",ain14_ts,ain14_val", ",ain15_ts,ain15_val"
};

// The board configuration
// TODO: It would be handy if this was at a special place in memory so we could flash just the board config (vs recompiling the firmware w/ a different configuration)

// Common port definitions now in CommonBoardPinDefs.h

// NQ3-specific DAC7718 control pins
#define DAC7718_CS_PORT GPIO_PORT_K
#define DAC7718_RST_PORT GPIO_PORT_J

// Pin definitions corresponding to plib_gpio.h GPIO_PIN_* constants
#define DIO_0_PIN                  GPIO_PIN_RD1
#define DIO_1_PIN                  GPIO_PIN_RJ3
#define DIO_2_PIN                  GPIO_PIN_RD3
#define DIO_3_PIN                  GPIO_PIN_RD12
#define DIO_4_PIN                  GPIO_PIN_RF0
#define DIO_5_PIN                  GPIO_PIN_RF1
#define DIO_6_PIN                  GPIO_PIN_RG0
#define DIO_7_PIN                  GPIO_PIN_RG1
#define DIO_8_PIN                  GPIO_PIN_RJ6
#define DIO_9_PIN                  GPIO_PIN_RE1
#define DIO_10_PIN                 GPIO_PIN_RE4
#define DIO_11_PIN                 GPIO_PIN_RC2
#define DIO_12_PIN                 GPIO_PIN_RE3
#define DIO_13_PIN                 GPIO_PIN_RE6
#define DIO_14_PIN                 GPIO_PIN_RE5
#define DIO_15_PIN                 GPIO_PIN_RC1

#define DIO_EN_0_PIN               GPIO_PIN_RD2
#define DIO_EN_1_PIN               GPIO_PIN_RJ2
#define DIO_EN_2_PIN               GPIO_PIN_RD13
#define DIO_EN_3_PIN               GPIO_PIN_RJ0
#define DIO_EN_4_PIN               GPIO_PIN_RD7
#define DIO_EN_5_PIN               GPIO_PIN_RK7
#define DIO_EN_6_PIN               GPIO_PIN_RJ4
#define DIO_EN_7_PIN               GPIO_PIN_RJ5
#define DIO_EN_8_PIN               GPIO_PIN_RJ7
#define DIO_EN_9_PIN               GPIO_PIN_RE0
#define DIO_EN_10_PIN              GPIO_PIN_RG15
#define DIO_EN_11_PIN              GPIO_PIN_RJ10
#define DIO_EN_12_PIN              GPIO_PIN_RE2
#define DIO_EN_13_PIN              GPIO_PIN_RE7
#define DIO_EN_14_PIN              GPIO_PIN_RA5
#define DIO_EN_15_PIN              GPIO_PIN_RJ12

#define PWR_3_3V_EN_PIN            GPIO_PIN_RH12
#define PWR_VREF_EN_PIN            GPIO_PIN_RJ15
#define PWR_5V_EN_PIN              GPIO_PIN_RD0
#define PWR_12V_EN_PIN             GPIO_PIN_RH15
#define USB_DP_MON_PIN             GPIO_PIN_RH9
#define USB_DN_MON_PIN             GPIO_PIN_RH10
#define BATT_MAN_INT_PIN           GPIO_PIN_RA4
//#define BATT_MAN_OTG_PIN           GPIO_PIN_RK5
#define BATT_MAN_STAT_PIN          GPIO_PIN_RH11
#define LED_WHITE_PIN              GPIO_PIN_RC3
#define LED_BLUE_PIN               GPIO_PIN_RB14
#define BUTTON_PIN                 GPIO_PIN_RJ14

// DAC7718 pin definitions
#define DAC7718_CS_PIN             GPIO_PIN_RK0    // CS on RK0
#define DAC7718_RST_PIN            GPIO_PIN_RJ13   // CLR/RST on RJ13

// PORTS_REMAP_OUTPUT_PIN enum now in CommonBoardPinDefs.h
const tBoardConfig NQ3BoardConfig = {
    .BoardVariant = 3,
    .DIOChannels =
    {
        .Data =
        {
#ifndef DIO_TIMING_TEST
            { DIO_0_PORT, PORTS_BIT_POS_1, DIO_EN_0_PORT, PORTS_BIT_POS_2, false, true, 1, OUTPUT_PIN_RPD1},
#endif
            { DIO_1_PORT, PORTS_BIT_POS_3, DIO_EN_1_PORT, PORTS_BIT_POS_2, true, false, 0xFF},
            { DIO_2_PORT, PORTS_BIT_POS_3, DIO_EN_2_PORT, PORTS_BIT_POS_13, true, false, 0xFF},
            { DIO_3_PORT, PORTS_BIT_POS_12, DIO_EN_3_PORT, PORTS_BIT_POS_0, false, true, 8, OUTPUT_PIN_RPD12},
            { DIO_4_PORT, PORTS_BIT_POS_0, DIO_EN_4_PORT, PORTS_BIT_POS_7, true, true, 4, OUTPUT_PIN_RPF0},
            { DIO_5_PORT, PORTS_BIT_POS_1, DIO_EN_5_PORT, PORTS_BIT_POS_7, false, true, 6, OUTPUT_PIN_RPF1},
            { DIO_6_PORT, PORTS_BIT_POS_0, DIO_EN_6_PORT, PORTS_BIT_POS_4, true, true, 7, OUTPUT_PIN_RPG0},
            { DIO_7_PORT, PORTS_BIT_POS_1, DIO_EN_7_PORT, PORTS_BIT_POS_5, false, true, 3, OUTPUT_PIN_RPG1},
            { DIO_8_PORT, PORTS_BIT_POS_6, DIO_EN_8_PORT, PORTS_BIT_POS_7, false, false, 0xFF},
            { DIO_9_PORT, PORTS_BIT_POS_1, DIO_EN_9_PORT, PORTS_BIT_POS_0, true, false, 0xFF},
            { DIO_10_PORT, PORTS_BIT_POS_4, DIO_EN_10_PORT, PORTS_BIT_POS_15, false, false, 0xFF},
            { DIO_11_PORT, PORTS_BIT_POS_2, DIO_EN_11_PORT, PORTS_BIT_POS_10, true, false, 0xFF},
            { DIO_12_PORT, PORTS_BIT_POS_3, DIO_EN_12_PORT, PORTS_BIT_POS_2, true, false, 0xFF},
            { DIO_13_PORT, PORTS_BIT_POS_6, DIO_EN_13_PORT, PORTS_BIT_POS_7, false, false, 0xFF},
            { DIO_14_PORT, PORTS_BIT_POS_5, DIO_EN_14_PORT, PORTS_BIT_POS_5, true, false, 0xFF},
            { DIO_15_PORT, PORTS_BIT_POS_1, DIO_EN_15_PORT, PORTS_BIT_POS_12, false, false, 0xFF},
        },
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
    .PowerConfig =
    {
        .EN_Vref_Ch = PWR_VREF_EN_PORT,
        .EN_Vref_Bit = PORTS_BIT_POS_15,       // RJ15
        .EN_3_3V_Ch = PWR_3_3V_EN_PORT,
        .EN_3_3V_Bit = PORTS_BIT_POS_12,       // RH12
        .EN_5_10V_Ch = PWR_5V_EN_PORT,
        .EN_5_10V_Bit = PORTS_BIT_POS_0,       // RD0
        .EN_12V_Ch = PWR_12V_EN_PORT,
        .EN_12V_Bit = PORTS_BIT_POS_15,        // RH15
        .USB_Dp_Ch = USB_DP_MON_PORT,
        .USB_Dp_Bit = PORTS_BIT_POS_9,         // RH9
        .USB_Dn_Ch = USB_DN_MON_PORT,
        .USB_Dn_Bit = PORTS_BIT_POS_10,        // RH10
        .BQ24297Config = {
            .INT_Ch = BATT_MAN_INT_PORT,
            .INT_Bit = PORTS_BIT_POS_4,   // RA4
            .OTG_Ch = BATT_MAN_OTG_PORT,
            .OTG_Bit = PORTS_BIT_POS_5,   // RK5
            .STAT_Ch = BATT_MAN_STAT_PORT,
            .STAT_Bit = PORTS_BIT_POS_11, // RH11
            .I2C_Index = DRV_I2C_INDEX_0,
            .I2C_Address = (0xD6U >> 1),  // 7-bit address format
        },
    },
    .UIConfig =
    {
       
        // White LED
        .LED1_Pin = LED_WHITE_PIN,
        
        // Blue LED
        .LED2_Pin = LED_BLUE_PIN,
       
        // The only button
        .button_Pin = BUTTON_PIN,
        .LED1_Ind = {
            .patterns = {
                {0,0,0,0,0,0,0,0},  // LEDs off
                {0,0,0,0,0,0,0,0},  // Error state
                {0,0,1,1,0,0,1,1},  // Bat exhausted
                {1,1,1,1,1,1,1,1},  // Plugged in
                {0,1,1,1,1,1,1,1},  // Plugged in, power on
                {0,1,0,1,1,1,1,1},  // Plugged in, power on, charging
                {0,1,1,1,1,1,1,1},  // Plugged in, power on, streaming
                {0,1,0,1,1,1,1,1},  // Plugged in, power on, charging, streaming
                {1,0,0,0,0,0,0,0},  // Power on
                {1,0,0,0,0,0,0,0},  // Power on, streaming
                {1,0,1,0,0,0,0,0},  // Power on, batt low
                {1,0,1,0,0,0,0,0},  // Power on, streaming, batt low
                },
            .period = {2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2},
        },
        
        .LED2_Ind = {
            .patterns = {
                {0,0,0,0,0,0,0,0},  // LEDs off
                {1,0,1,0,1,0,1,0},  // Error state
                {1,1,0,0,1,1,0,0},  // Bat exhausted
                {0,0,0,0,0,0,0,0},  // Plugged in
                {0,0,0,0,0,0,0,0},  // Plugged in, power on
                {0,0,0,0,0,0,0,0},  // Plugged in, power on, charging
                {1,0,0,0,0,0,0,0},  // Plugged in, power on, streaming
                {1,0,0,0,0,0,0,0},  // Plugged in, power on, charging, streaming
                {0,0,0,0,0,0,0,0},  // Power on
                {1,0,0,0,0,0,0,0},  // Power on, streaming
                {0,0,0,0,0,0,0,0},  // Power on, batt low
                {1,0,0,0,0,0,0,0},  // Power on, streaming, batt low
                },
            .period = {2, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2},
        },
    },
    .StreamingConfig =
    {
        .TimerIndex = TMR_INDEX_4,
        .TSTimerIndex = TMR_INDEX_6,
    },
    .csvChannelHeadersFirst = NQ3_CSV_CHANNEL_HEADERS_FIRST,
    .csvChannelHeadersSubsequent = NQ3_CSV_CHANNEL_HEADERS_SUBSEQUENT
};

/*! This function is used for getting a board configuration parameter
 * @return Pointer to Board Configuration structure
 */
const void *NqBoardConfig_Get( void )
{
    return &NQ3BoardConfig;
}