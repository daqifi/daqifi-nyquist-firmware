#include "BoardConfig.h"

// The board configuration
// TODO: It would be handy if this was at a special place in memory so we could flash just the board config (vs recompiling the firmware w/ a different configuration)
const tBoardConfig NQ3BoardConfig = {
    .BoardVariant = 3,
    .DIOChannels = {
        .Data = {
            {PORTS_ID_0, PORT_CHANNEL_D, PORTS_BIT_POS_1, PORTS_ID_0, PORT_CHANNEL_D, PORTS_BIT_POS_2, false},
            {PORTS_ID_0, PORT_CHANNEL_J, PORTS_BIT_POS_3, PORTS_ID_0, PORT_CHANNEL_J, PORTS_BIT_POS_2, true},
            {PORTS_ID_0, PORT_CHANNEL_D, PORTS_BIT_POS_3, PORTS_ID_0, PORT_CHANNEL_D, PORTS_BIT_POS_13, true},
            {PORTS_ID_0, PORT_CHANNEL_D, PORTS_BIT_POS_12, PORTS_ID_0, PORT_CHANNEL_J, PORTS_BIT_POS_0, false},
            {PORTS_ID_0, PORT_CHANNEL_F, PORTS_BIT_POS_0, PORTS_ID_0, PORT_CHANNEL_D, PORTS_BIT_POS_7, true},
            {PORTS_ID_0, PORT_CHANNEL_F, PORTS_BIT_POS_1, PORTS_ID_0, PORT_CHANNEL_K, PORTS_BIT_POS_7, false},
            {PORTS_ID_0, PORT_CHANNEL_G, PORTS_BIT_POS_0, PORTS_ID_0, PORT_CHANNEL_J, PORTS_BIT_POS_4, true},
            {PORTS_ID_0, PORT_CHANNEL_G, PORTS_BIT_POS_1, PORTS_ID_0, PORT_CHANNEL_J, PORTS_BIT_POS_5, false},
            {PORTS_ID_0, PORT_CHANNEL_J, PORTS_BIT_POS_6, PORTS_ID_0, PORT_CHANNEL_J, PORTS_BIT_POS_7, false},
            {PORTS_ID_0, PORT_CHANNEL_E, PORTS_BIT_POS_1, PORTS_ID_0, PORT_CHANNEL_E, PORTS_BIT_POS_0, true},
            {PORTS_ID_0, PORT_CHANNEL_E, PORTS_BIT_POS_4, PORTS_ID_0, PORT_CHANNEL_G, PORTS_BIT_POS_15, false},
            {PORTS_ID_0, PORT_CHANNEL_C, PORTS_BIT_POS_2, PORTS_ID_0, PORT_CHANNEL_J, PORTS_BIT_POS_10, true},
            {PORTS_ID_0, PORT_CHANNEL_E, PORTS_BIT_POS_3, PORTS_ID_0, PORT_CHANNEL_E, PORTS_BIT_POS_2, true},
            {PORTS_ID_0, PORT_CHANNEL_E, PORTS_BIT_POS_6, PORTS_ID_0, PORT_CHANNEL_E, PORTS_BIT_POS_7, false},
            {PORTS_ID_0, PORT_CHANNEL_E, PORTS_BIT_POS_5, PORTS_ID_0, PORT_CHANNEL_A, PORTS_BIT_POS_5, true},
            {PORTS_ID_0, PORT_CHANNEL_C, PORTS_BIT_POS_1, PORTS_ID_0, PORT_CHANNEL_J, PORTS_BIT_POS_12, false},  
        },
        .Size = 16,
    },
    .AInModules = {
        .Data = {
            {
                .Type = AIn_MC12bADC,
                .Config = {.MC12b = { .moduleId = DRV_ADC_ID_1, .Resolution = 4096 } },
                .Size = 16
            },
            {
                .Type = AIn_AD7609,
                .Config = {
                    .AD7609 = {
                        .SPI = {
                            .spiID = SPI_ID_6,
                            .baud = 15000000,
                            .clock = SPI_BAUD_RATE_PBCLK_CLOCK,   
                            .busClk_id = CLK_BUS_PERIPHERAL_2,   
                            .clockPolarity = SPI_CLOCK_POLARITY_IDLE_HIGH, 
                            .busWidth = SPI_COMMUNICATION_WIDTH_8BITS,    
                            .inSamplePhase = SPI_INPUT_SAMPLING_PHASE_IN_MIDDLE,     
                            .outDataPhase = SPI_OUTPUT_DATA_PHASE_ON_IDLE_TO_ACTIVE_CLOCK,
                        },
                        .DataModule = PORTS_ID_0, 
                        .CS_Ch = PORT_CHANNEL_H,    
                        .CS_Bit = PORTS_BIT_POS_2,    
                        .BSY_Ch = PORT_CHANNEL_B,    
                        .BSY_Bit = PORTS_BIT_POS_3,
                        .BSY_BitMask = 0b1000,        // Bit mask of port above
                        .RST_Ch = PORT_CHANNEL_H,
                        .RST_Bit = PORTS_BIT_POS_3,
                        .Range_Ch = PORT_CHANNEL_K,     
                        .Range_Bit = PORTS_BIT_POS_1,
                        .OS0_Ch = PORT_CHANNEL_H,  
                        .OS0_Bit = PORTS_BIT_POS_7,
                        .OS1_Ch = PORT_CHANNEL_K,    
                        .OS1_Bit = PORTS_BIT_POS_3,    
                        .CONVST_Ch = PORT_CHANNEL_B,
                        .CONVST_Bit = PORTS_BIT_POS_9,
                        .Range10V = true,
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
            // External ADC AD7609
            {
                .ChannelId = 16,
                .DataModule = 1,
                .Config = {.AD7609 = {0}}
            },
            {
                .ChannelId = 17,
                .DataModule = 1,
                .Config = {.AD7609 = {1}}
            },
            {
                .ChannelId = 18,
                .DataModule = 1,
                .Config = {.AD7609 = {2}}
            },
            {
                .ChannelId = 19,
                .DataModule = 1,
                .Config = {.AD7609 = {3}}
            },
            {
                .ChannelId = 20,
                .DataModule = 1,
                .Config = {.AD7609 = {4}}
            },
            {
                .ChannelId = 21,
                .DataModule = 1,
                .Config = {.AD7609 = {5}}
            },
            {
                .ChannelId = 22,
                .DataModule = 1,
                .Config = {.AD7609 = {6}}
            },
            {
                .ChannelId = 23,
                .DataModule = 1,
                .Config = {.AD7609 = {7}}
            },

            // ADC Channels for internal use
            // TODO: It may make sense to put these in the power settings.
            {
                .ChannelId = ADC_CHANNEL_3_3V,
                .DataModule = 0,
                .Config = {.MC12b = {false, 19, 2, false}} // +3.3V_Mon
            },
            {
                .ChannelId = ADC_CHANNEL_2_5VREF,
                .DataModule = 0,
                .Config = {.MC12b = {false, 31, 2, false}} // +2.5VRef_Mon
            },
            {
                .ChannelId = ADC_CHANNEL_VBATT,
                .DataModule = 0,
                .Config = {.MC12b = {false, 30, 2, false}} // Vbat_Mon
            },
            {
                .ChannelId = ADC_CHANNEL_5V,
                .DataModule = 0,
                .Config = {.MC12b = {false, 42, 2, false}} // +5V_ADC_Mon
            },
            {
                .ChannelId = ADC_CHANNEL_10V,
                .DataModule = 0,
                .Config = {.MC12b = {false, 32, 2, false}} // +10/+12V_Mon
            },
            {
                .ChannelId = ADC_CHANNEL_TEMP,
                .DataModule = 0,
                .Config = {.MC12b = {false, 44, 2, false}} // On board temperature sensor 5mV/degC 0->5V=-40degC
            },
            {
                .ChannelId = ADC_CHANNEL_5VREF,
                .DataModule = 0,
                .Config = {.MC12b = {false, 29, 2, false}} // On board +5V ref (only on Nq2)
            },
            {
                .ChannelId = ADC_CHANNEL_VSYS,
                .DataModule = 0,
                .Config = {.MC12b = {false, 10, 2, false}} // Board system power
            },
        },
        .Size = 16
    },
    .PowerConfig = {
        .EN_Vref_Ch = PORT_CHANNEL_J,
        .EN_Vref_Bit = PORTS_BIT_POS_15,
        .EN_3_3V_Ch = PORT_CHANNEL_H,
        .EN_3_3V_Bit = PORTS_BIT_POS_12,
        .EN_5_10V_Ch = PORT_CHANNEL_D,
        .EN_5_10V_Bit = PORTS_BIT_POS_0,
        .EN_5V_ADC_Ch = PORT_CHANNEL_B,
        .EN_5V_ADC_Bit = PORTS_BIT_POS_0,
        .EN_12V_Ch = PORT_CHANNEL_H,
        .EN_12V_Bit = PORTS_BIT_POS_15,
        .USB_Dp_Ch = PORT_CHANNEL_H, 
        .USB_Dp_Bit = PORTS_BIT_POS_9, 
        .USB_Dn_Ch = PORT_CHANNEL_H, 
        .USB_Dn_Bit = PORTS_BIT_POS_10,
    },
    .UIConfig = {
        .LED1_Mod = 0,
        .LED1_Ch = PORT_CHANNEL_C,      // White LED
        .LED1_Bit = PORTS_BIT_POS_3,
        .LED2_Mod = 0,
        .LED2_Ch = PORT_CHANNEL_B,       // Blue LED
        .LED2_Bit = PORTS_BIT_POS_14,
        .button_Mod = 0,
        .button_Ch = PORT_CHANNEL_J,    // The only button
        .button_Bit = PORTS_BIT_POS_14,
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
    .StreamingConfig = {
        .TimerIndex = DRV_TMR_INDEX_1,
        .TimerIntent = DRV_IO_INTENT_EXCLUSIVE,
        .TSTimerIndex = DRV_TMR_INDEX_2,
        .TSTimerIntent = DRV_IO_INTENT_EXCLUSIVE,
    }
};

/*! This function is used for getting a board version 1 configuration parameter
 * @return Pointer to Board Configuration structure
 */
const void *NQ3BoardConfig_Get( void )
{
    return &NQ3BoardConfig; 
}