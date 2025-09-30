#include "BoardConfig.h"
#include "../../config/default/peripheral/gpio/plib_gpio.h"

// The board configuration
// TODO: It would be handy if this was at a special place in memory so we could flash just the board config (vs recompiling the firmware w/ a different configuration)
#define DIO_0_PORT GPIO_PORT_D
#define DIO_1_PORT GPIO_PORT_J
#define DIO_2_PORT GPIO_PORT_D
#define DIO_3_PORT GPIO_PORT_D
#define DIO_4_PORT GPIO_PORT_F
#define DIO_5_PORT GPIO_PORT_F
#define DIO_6_PORT GPIO_PORT_G
#define DIO_7_PORT GPIO_PORT_G
#define DIO_8_PORT GPIO_PORT_J
#define DIO_9_PORT GPIO_PORT_E
#define DIO_10_PORT GPIO_PORT_E
#define DIO_11_PORT GPIO_PORT_C
#define DIO_12_PORT GPIO_PORT_E
#define DIO_13_PORT GPIO_PORT_E
#define DIO_14_PORT GPIO_PORT_E
#define DIO_15_PORT GPIO_PORT_C

#define DIO_EN_0_PORT GPIO_PORT_D
#define DIO_EN_1_PORT GPIO_PORT_J
#define DIO_EN_2_PORT GPIO_PORT_D
#define DIO_EN_3_PORT GPIO_PORT_J
#define DIO_EN_4_PORT GPIO_PORT_D
#define DIO_EN_5_PORT GPIO_PORT_K
#define DIO_EN_6_PORT GPIO_PORT_J
#define DIO_EN_7_PORT GPIO_PORT_J
#define DIO_EN_8_PORT GPIO_PORT_J
#define DIO_EN_9_PORT GPIO_PORT_E
#define DIO_EN_10_PORT GPIO_PORT_G
#define DIO_EN_11_PORT GPIO_PORT_J
#define DIO_EN_12_PORT GPIO_PORT_E
#define DIO_EN_13_PORT GPIO_PORT_E
#define DIO_EN_14_PORT GPIO_PORT_A
#define DIO_EN_15_PORT GPIO_PORT_J

#define PWR_3_3V_EN_PORT GPIO_PORT_H
#define PWR_VREF_EN_PORT GPIO_PORT_J
#define PWR_5V_EN_PORT GPIO_PORT_D
#define PWR_12V_EN_PORT GPIO_PORT_H
#define USB_DP_MON_PORT GPIO_PORT_H
#define USB_DN_MON_PORT GPIO_PORT_H
#define BATT_MAN_INT_PORT GPIO_PORT_A
#define BATT_MAN_OTG_PORT GPIO_PORT_K
#define BATT_MAN_STAT_PORT GPIO_PORT_H
#define LED_WHITE_PORT GPIO_PORT_C
#define LED_BLUE_PORT GPIO_PORT_B
#define BUTTON_PORT GPIO_PORT_J

// DAC7718 control pins
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

typedef enum {
    OUTPUT_PIN_RPA14 = 0,
    OUTPUT_PIN_RPA15 = 1,
    OUTPUT_PIN_RPB0 = 2,
    OUTPUT_PIN_RPB1 = 3,
    OUTPUT_PIN_RPB2 = 4,
    OUTPUT_PIN_RPB3 = 5,
    OUTPUT_PIN_RPB5 = 7,
    OUTPUT_PIN_RPB6 = 8,
    OUTPUT_PIN_RPB7 = 9,
    OUTPUT_PIN_RPB8 = 10,
    OUTPUT_PIN_RPB9 = 11,
    OUTPUT_PIN_RPB10 = 12,
    OUTPUT_PIN_RPB14 = 16,
    OUTPUT_PIN_RPB15 = 17,
    OUTPUT_PIN_RPC1 = 19,
    OUTPUT_PIN_RPC2 = 20,
    OUTPUT_PIN_RPC3 = 21,
    OUTPUT_PIN_RPC4 = 22,
    OUTPUT_PIN_RPC13 = 31,
    OUTPUT_PIN_RPC14 = 32,
    OUTPUT_PIN_RPD0 = 34,
    OUTPUT_PIN_RPD1 = 35,
    OUTPUT_PIN_RPD2 = 36,
    OUTPUT_PIN_RPD3 = 37,
    OUTPUT_PIN_RPD4 = 38,
    OUTPUT_PIN_RPD5 = 39,
    OUTPUT_PIN_RPD6 = 40,
    OUTPUT_PIN_RPD7 = 41,
    OUTPUT_PIN_RPD9 = 43,
    OUTPUT_PIN_RPD10 = 44,
    OUTPUT_PIN_RPD11 = 45,
    OUTPUT_PIN_RPD12 = 46,
    OUTPUT_PIN_RPD14 = 48,
    OUTPUT_PIN_RPD15 = 49,
    OUTPUT_PIN_RPE3 = 53,
    OUTPUT_PIN_RPE5 = 55,
    OUTPUT_PIN_RPE8 = 58,
    OUTPUT_PIN_RPE9 = 59,
    OUTPUT_PIN_RPF0 = 66,
    OUTPUT_PIN_RPF1 = 67,
    OUTPUT_PIN_RPF2 = 68,
    OUTPUT_PIN_RPF3 = 69,
    OUTPUT_PIN_RPF4 = 70,
    OUTPUT_PIN_RPF5 = 71,
    OUTPUT_PIN_RPF8 = 74,
    OUTPUT_PIN_RPF12 = 78,
    OUTPUT_PIN_RPF13 = 79,
    OUTPUT_PIN_RPG0 = 82,
    OUTPUT_PIN_RPG1 = 83,
    OUTPUT_PIN_RPG6 = 88,
    OUTPUT_PIN_RPG7 = 89,
    OUTPUT_PIN_RPG8 = 90,
    OUTPUT_PIN_RPG9 = 91

} PORTS_REMAP_OUTPUT_PIN;
const tBoardConfig NQ3BoardConfig = {
    .BoardVariant = 3,
    .DIOChannels =
    {
        .Data =
        {
            // Minimal DIO config - only first 4 channels to test
            { DIO_0_PORT, DIO_0_PIN & 15, DIO_EN_0_PORT, DIO_EN_0_PIN & 15, false, false, 0xFF},
            { DIO_1_PORT, DIO_1_PIN & 15, DIO_EN_1_PORT, DIO_EN_1_PIN & 15, true, false, 0xFF},
            { DIO_2_PORT, DIO_2_PIN & 15, DIO_EN_2_PORT, DIO_EN_2_PIN & 15, true, false, 0xFF},
            { DIO_3_PORT, DIO_3_PIN & 15, DIO_EN_3_PORT, DIO_EN_3_PIN & 15, false, false, 0xFF},
        },
        .Size = 4,
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
            // Channel mapping: User 0-7 → AD7609 4,5,6,7,0,1,2,3 (hardware layout)
            {
                .DaqifiAdcChannelId = 0,
                .Type = AIn_AD7609,
                .Config = {.AD7609 = {.ChannelNumber = 4}}
            },
            {
                .DaqifiAdcChannelId = 1,
                .Type = AIn_AD7609,
                .Config = {.AD7609 = {.ChannelNumber = 5}}
            },
            {
                .DaqifiAdcChannelId = 2,
                .Type = AIn_AD7609,
                .Config = {.AD7609 = {.ChannelNumber = 6}}
            },
            {
                .DaqifiAdcChannelId = 3,
                .Type = AIn_AD7609,
                .Config = {.AD7609 = {.ChannelNumber = 7}}
            },
            {
                .DaqifiAdcChannelId = 4,
                .Type = AIn_AD7609,
                .Config = {.AD7609 = {.ChannelNumber = 0}}
            },
            {
                .DaqifiAdcChannelId = 5,
                .Type = AIn_AD7609,
                .Config = {.AD7609 = {.ChannelNumber = 1}}
            },
            {
                .DaqifiAdcChannelId = 6,
                .Type = AIn_AD7609,
                .Config = {.AD7609 = {.ChannelNumber = 2}}
            },
            {
                .DaqifiAdcChannelId = 7,
                .Type = AIn_AD7609,
                .Config = {.AD7609 = {.ChannelNumber = 3}}
            },

            // Internal monitoring channels - exact copy from working NQ1 configuration
            {
                .DaqifiAdcChannelId = ADC_CHANNEL_3_3V,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {false, ADCHS_CH19, ADCHS_MODULE7_MASK, 2, false, 1}} // +3.3V_Mon
            },
            {
                .DaqifiAdcChannelId = ADC_CHANNEL_2_5VREF,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {false, ADCHS_CH31, ADCHS_MODULE7_MASK, 2, false, 1}} // +2.5VRef_Mon
            },
            {
                .DaqifiAdcChannelId = ADC_CHANNEL_VBATT,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {false, ADCHS_CH30, ADCHS_MODULE7_MASK, 2, false, 1}} // Vbat_Mon
            },
            {
                .DaqifiAdcChannelId = ADC_CHANNEL_5V,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {false, ADCHS_CH42, ADCHS_MODULE7_MASK, 2, false, 2.16666666667}} // +5V_Prot_Mon
            },
            {
                .DaqifiAdcChannelId = ADC_CHANNEL_10V,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {false, ADCHS_CH32, ADCHS_MODULE7_MASK, 2, false, 3.905000000000}} // +10_Prot_Mon
            },
            {
                .DaqifiAdcChannelId = ADC_CHANNEL_TEMP,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {false, ADCHS_CH44, ADCHS_MODULE7_MASK, 2, false, 1}} // On board temperature sensor 5mV/degC 0->5V=-40degC
            },
            {
                .DaqifiAdcChannelId = ADC_CHANNEL_5VREF,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {false, ADCHS_CH29, ADCHS_MODULE7_MASK, 2, false, 2.16666666667}} // On board +5V ref (only on Nq2)
            },
            {
                .DaqifiAdcChannelId = ADC_CHANNEL_VSYS,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {false, ADCHS_CH41, ADCHS_MODULE7_MASK, 2, false, 1.409090909091}} // Board system power
            },
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
                        .Resolution = 4096,           // 12-bit DAC (4096 levels)
                    }
                },
                .Size = 8  // DAC7718 has 8 channels
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
        .EN_Vref_Bit = PWR_VREF_EN_PIN & 15,
        .EN_3_3V_Ch = PWR_3_3V_EN_PORT,
        .EN_3_3V_Bit = PWR_3_3V_EN_PIN & 15,
        .EN_5_10V_Ch = PWR_5V_EN_PORT, 
        .EN_5_10V_Bit = PWR_5V_EN_PIN & 15,
        .EN_12V_Ch = PWR_12V_EN_PORT,
        .EN_12V_Bit = PWR_12V_EN_PIN  & 15,
        .USB_Dp_Ch = USB_DP_MON_PORT,
        .USB_Dp_Bit = USB_DP_MON_PIN & 15,
        .USB_Dn_Ch = USB_DN_MON_PORT,
        .USB_Dn_Bit = USB_DN_MON_PIN & 15,
        .BQ24297Config.INT_Ch = BATT_MAN_INT_PORT,
        .BQ24297Config.INT_Bit = BATT_MAN_INT_PIN & 15,
        .BQ24297Config.OTG_Ch = BATT_MAN_OTG_PORT,
        .BQ24297Config.OTG_Bit = BATT_MAN_OTG_PIN & 15,
        .BQ24297Config.STAT_Ch = BATT_MAN_STAT_PORT,
        .BQ24297Config.STAT_Bit = BATT_MAN_STAT_PIN & 15,
        .BQ24297Config.I2C_Index = DRV_I2C_INDEX_0,
        .BQ24297Config.I2C_Address = 0xD6>>1, // Microchip libraries use an 8 bit address with 0 appended to the end of the 7 bit I2C address
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
        .TimerIndex = 4,
        .TSTimerIndex = 6,
    }
};

/*! This function is used for getting a board configuration parameter
 * @return Pointer to Board Configuration structure based on BOARD_VARIANT
 */
const void *NqBoardConfig_Get( void )
{
    // For now, return NQ3 configuration since that's what we're testing
    // This function can be extended to check BOARD_VARIANT and return appropriate config
    return &NQ3BoardConfig; 
}