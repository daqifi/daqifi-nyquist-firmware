#include "BoardConfig.h"
#include "CommonBoardPinDefs.h"
#include "CommonBoardConfig.h"
#include "CommonMonitoringChannels.h"
#include "../../HAL/TimerApi/TimerApi.h"

// CSV column headers now in CommonBoardConfig.c


// The board configuration
// TODO: It would be handy if this was at a special place in memory so we could flash just the board config (vs recompiling the firmware w/ a different configuration)

// Common port definitions and PORTS_REMAP_OUTPUT_PIN enum now in CommonBoardPinDefs.h

const tBoardConfig NQ1BoardConfig = {
    .BoardVariant = 1,
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
        },
        .Size = 1
    },
    .AInChannels =
    {
        .Data =
        {
            // Internal ADC
            // Internal scale = (R1Ain+R2Ain)/(R2Ain) * ((R1+R2)/(R2)) 
            // where RAin is the resistor divider for the 16 RAin channels
            // and R is the resistor divider for the internal channels

            {
                .DaqifiAdcChannelId = 0,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {.AllowDifferential = false, .ChannelId = ADCHS_CH11, .ModuleId = ADCHS_MODULE7_MASK, .ChannelType = 2, .IsPublic = true, .InternalScale = 1}}
            },
            {
                .DaqifiAdcChannelId = 1,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {false, ADCHS_CH24, ADCHS_MODULE7_MASK, 2, true, 1}}
            },
            {
                .DaqifiAdcChannelId = 2,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {false, ADCHS_CH25, ADCHS_MODULE7_MASK, 2, true, 1}}
            },
            {
                .DaqifiAdcChannelId = 3,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {false, ADCHS_CH26, ADCHS_MODULE7_MASK, 2, true, 1}}
            },
            {
                .DaqifiAdcChannelId = 4,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {false, ADCHS_CH4, ADCHS_MODULE4_MASK, 1, true, 1}} // Type 1
            },
            {
                .DaqifiAdcChannelId = 5,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {false, ADCHS_CH39, ADCHS_MODULE7_MASK, 2, true, 1}}
            },
            {
                .DaqifiAdcChannelId = 6,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {false, ADCHS_CH38, ADCHS_MODULE7_MASK, 2, true, 1}}
            },
            {
                .DaqifiAdcChannelId = 7,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {false, ADCHS_CH27, ADCHS_MODULE7_MASK, 2, true, 1}}
            },
            {
                .DaqifiAdcChannelId = 8,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {true, ADCHS_CH0, ADCHS_MODULE0_MASK, 1, true, 1}} //Ch 0 using alternate pin AN45 - Type 1
            },
            {
                .DaqifiAdcChannelId = 9,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {false, ADCHS_CH5, ADCHS_MODULE7_MASK, 2, true, 1}}
            },
            {
                .DaqifiAdcChannelId = 10,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {true, ADCHS_CH1, ADCHS_MODULE1_MASK, 1, true, 1}} //Ch 1 using alternate pin AN46 - Type 1
            },
            {
                .DaqifiAdcChannelId = 11,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {false, ADCHS_CH6, ADCHS_MODULE7_MASK, 2, true, 1}}
            },
            {
                .DaqifiAdcChannelId = 12,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {true, ADCHS_CH2, ADCHS_MODULE2_MASK, 1, true, 1}} //Ch 2 using alternate pin AN47 - Type 1
            },
            {
                .DaqifiAdcChannelId = 13,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {false, ADCHS_CH7, ADCHS_MODULE7_MASK, 2, true, 1}}
            },
            {
                .DaqifiAdcChannelId = 14,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {true, ADCHS_CH3, ADCHS_MODULE3_MASK, 1, true, 1}} //Ch 3 using alternate pin AN48 - Type 1
            },
            {
                .DaqifiAdcChannelId = 15,
                .Type = AIn_MC12bADC,
                .Config =
                {.MC12b =
                    {false, ADCHS_CH8, ADCHS_MODULE7_MASK, 2, true, 1}}
            },

            // Internal monitoring channels (from CommonMonitoringChannels.h)
            COMMON_MONITORING_CHANNELS_BOARDCONFIG
        },
        .Size = 24
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
        .BQ24297Config.INT_Ch = BATT_MAN_INT_PORT,
        .BQ24297Config.INT_Bit = PORTS_BIT_POS_4,   // RA4
        .BQ24297Config.OTG_Ch = BATT_MAN_OTG_PORT,
        .BQ24297Config.OTG_Bit = PORTS_BIT_POS_5,   // RK5
        .BQ24297Config.STAT_Ch = BATT_MAN_STAT_PORT,
        .BQ24297Config.STAT_Bit = PORTS_BIT_POS_11, // RH11
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
        .LED1_Ind =
        {
            .patterns =
            {
                {0, 0, 0, 0, 0, 0, 0, 0}, // LEDs off
                {0, 0, 0, 0, 0, 0, 0, 0}, // Error state
                {0, 0, 1, 1, 0, 0, 1, 1}, // Bat exhausted
                {0, 0, 0, 0, 0, 0, 0, 0}, // Plugged in
                {1, 1, 1, 1, 1, 1, 1, 1}, // Plugged in, power on
                {0, 1, 1, 1, 1, 1, 1, 1}, // Plugged in, power on, charging
                {1, 1, 1, 1, 1, 1, 1, 1}, // Plugged in, power on, streaming
                {0, 1, 1, 1, 1, 1, 1, 1}, // Plugged in, power on, charging, streaming
                {1, 0, 0, 0, 0, 0, 0, 0}, // Power on
                {1, 0, 0, 0, 0, 0, 0, 0}, // Power on, streaming
                {1, 0, 1, 0, 0, 0, 0, 0}, // Power on, batt low
                {1, 0, 1, 0, 0, 0, 0, 0}, // Power on, streaming, batt low
            },
            .period =
            {2, 0, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2},
        },

        .LED2_Ind =
        {
            .patterns =
            {
                {0, 0, 0, 0, 0, 0, 0, 0}, // LEDs off
                {1, 0, 1, 0, 1, 0, 1, 0}, // Error state
                {1, 1, 0, 0, 1, 1, 0, 0}, // Bat exhausted
                {0, 0, 0, 0, 0, 0, 0, 0}, // Plugged in
                {0, 0, 0, 0, 0, 0, 0, 0}, // Plugged in, power on
                {0, 0, 0, 0, 0, 0, 0, 0}, // Plugged in, power on, charging
                {1, 0, 0, 0, 0, 0, 0, 0}, // Plugged in, power on, streaming
                {1, 0, 0, 0, 0, 0, 0, 0}, // Plugged in, power on, charging, streaming
                {0, 0, 0, 0, 0, 0, 0, 0}, // Power on
                {1, 0, 0, 0, 0, 0, 0, 0}, // Power on, streaming
                {0, 0, 0, 0, 0, 0, 0, 0}, // Power on, batt low
                {1, 0, 0, 0, 0, 0, 0, 0}, // Power on, streaming, batt low
            },
            .period =
            {2, 1, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2},
        },
    },
    .StreamingConfig =
    {
        .TimerIndex = TMR_INDEX_4,
        .TSTimerIndex = TMR_INDEX_6,
    },
    .csvChannelHeadersFirst = COMMON_CSV_CHANNEL_HEADERS_FIRST,
    .csvChannelHeadersSubsequent = COMMON_CSV_CHANNEL_HEADERS_SUBSEQUENT
};

/*! This function is used for getting a board configuration parameter
 * @return Pointer to Board Configuration structure
 */
const void *NqBoardConfig_Get(void) {
    return &NQ1BoardConfig;
}