#pragma once

#include "configuration.h"
#include "definitions.h"
//#include <>

#include "Util/ArrayWrapper.h"

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * Defines the available analog input types
     * This is ues to direct analog input channels to the correct driver
     */
    typedef enum e_AInType {
        /**
         * Channel is provided by the internal ADC
         */
        AIn_MC12bADC = 0,

        //        /**
        //         * Channel is provided by an AD7173 chip
        //         */
        //        AIn_AD7173,
                        
        /**
         * Channel is provided by an AD7609 chip
         */
        AIn_AD7609,
    } AInType;

    /**
     * Configuration for a MC12bADC module
     */
    typedef struct s_MC12bModuleConfig {
        //DRV_ADC_MODULE_ID moduleId;
        double Resolution; // Per-module resolution
    } MC12bModuleConfig;

    /**
     * SPI Configuration structure for AD7609
     */
    typedef struct s_AD7609_SPI_Config {
        uint32_t spiID;
        uint32_t baud;
        uint32_t clock;
        uint32_t busClk_id;
        uint32_t clockPolarity;
        uint32_t busWidth;
        uint32_t inSamplePhase;
        uint32_t outDataPhase;
    } AD7609_SPI_Config;

    /**
     * Configuration for an AD7609 module - Updated for Harmony 3
     */
    typedef struct s_AD7609ModuleConfig {
        AD7609_SPI_Config SPI;
        // GPIO pins using modern Harmony 3 GPIO_PIN format
        GPIO_PIN CS_Pin;
        GPIO_PIN BSY_Pin;
        GPIO_PIN RST_Pin;
        GPIO_PIN Range_Pin;
        GPIO_PIN OS0_Pin;
        GPIO_PIN OS1_Pin;
        GPIO_PIN CONVST_Pin;
        GPIO_PIN STBY_Pin;
        // Configuration settings
        bool Range10V;
        uint32_t OSMode;
        double Resolution;
    } AD7609ModuleConfig;


    /**
     * Declares Chip/Module level configuration for an Analog In provider
     */
    typedef struct s_AInModule {
        /**
         * Identifies the type of module stored in this config
         */
        AInType Type;

        /**
         * Contains the actual settings
         * Since C doesn't support polymorphism, this is stored as a Union. Make sure you check Type!
         */
        union u_AInModuleImpl {
            MC12bModuleConfig MC12b;
            //            AD7173ModuleConfig AD7173;
            AD7609ModuleConfig AD7609;
        } Config;

        /**
         * Identifies the number of elements/channels in the module
         */
        uint32_t Size;


    } AInModule;

    /**
     * Holds intrinsic channel information for a MC12b-backed channel
     */
    typedef struct s_MC12bChannelConfig {
        /**
         * Indicates that the channel allows differential measurement
         */
        bool AllowDifferential;

        /**
         * The ADC Channel index
         */
        ADCHS_CHANNEL_NUM ChannelId;

        /**
         * ADC Module to which the channel Belongs
         */

        ADCHS_MODULE_MASK ModuleId;

        /**
         * Indicates that this is a type 1 channel
         */
        uint8_t ChannelType;

        /**
         * Indicates whether this channel is publicly available (to the user) or private
         */
        bool IsPublic;

        /**
         * Internal scale to accommodate for a hardware voltage divider
         * Calculated by 1/((Range/Vref)*(R2/(R1+R2)))
         */
        double InternalScale;
    } MC12bChannelConfig;

    /**
     * Holds intrinsic channel information for an AD7609-backed channel
     */
    typedef struct s_AD7609ChannelConfig {
        /**
         * The channel number on the AD7609 chip (0-7)
         */
        uint8_t ChannelNumber;
    } AD7609ChannelConfig;

    /**
     * Defines the immutable parameters for a single Analog in channel
     */
    typedef struct s_AInChannel {
        /**
         * ADC channel ID in Daqifi Board
         */
        uint8_t DaqifiAdcChannelId;

        AInType Type;

        /**
         * Contains the actual settings
         * Since C doesn't support polymorphism, this is stored as a Union. Make sure you check AInModules[Channel].Type!
         */

        union u_AInChannelImpl {
            MC12bChannelConfig MC12b;
            //            AD7173ChannelConfig AD7173;
            AD7609ChannelConfig AD7609;
        } Config;

    } AInChannel;

    // Define a storage class for analog input modules
#define MAX_AIN_MOD 2
    ARRAYWRAPPERDEF(AInModArray, AInModule, MAX_AIN_MOD);

    // Define a storage class for analog input channels
#define MAX_AIN_CHANNEL 48
    ARRAYWRAPPERDEF(AInArray, AInChannel, MAX_AIN_CHANNEL);

#define MAX_AIN_PUBLIC_CHANNELS 16

#define ADC_CHANNEL_3_3V 248
#define ADC_CHANNEL_2_5VREF 249
#define ADC_CHANNEL_VBATT 250
#define ADC_CHANNEL_5V 251
#define ADC_CHANNEL_10V 252
#define ADC_CHANNEL_TEMP 253 
#define ADC_CHANNEL_VSYS 254
#define ADC_CHANNEL_5VREF 255   






#ifdef __cplusplus
}
#endif
