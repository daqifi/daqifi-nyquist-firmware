#pragma once

#include "configuration.h"
#include "definitions.h"
#include "Util/ArrayWrapper.h"

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * Defines the available analog output types
     * This is used to direct analog output channels to the correct driver
     */
    typedef enum e_AOutType {
        /**
         * Channel is provided by a DAC7718 chip
         */
        AOut_DAC7718 = 0,
    } AOutType;

    /**
     * SPI Configuration structure for DAC7718
     */
    typedef struct s_DAC7718_SPI_Config {
        uint32_t spiID;
        uint32_t baud;
        uint32_t clock;
        uint32_t busClk_id;
        uint32_t clockPolarity;
        uint32_t busWidth;
        uint32_t inSamplePhase;
        uint32_t outDataPhase;
    } DAC7718_SPI_Config;

    /**
     * Configuration for a DAC7718 module
     */
    typedef struct s_DAC7718ModuleConfig {
        DAC7718_SPI_Config SPI;
        // GPIO pins using modern Harmony 3 GPIO_PIN format
        GPIO_PIN CS_Pin;        // Chip Select pin
        GPIO_PIN RST_Pin;       // Reset/Clear pin
        // Note: LDAC is tied to 3.3V so no GPIO control needed
        // Configuration settings
        uint8_t DAC_Range;      // DAC output range setting
        double Resolution;      // DAC resolution (bits)
    } DAC7718ModuleConfig;

    /**
     * Declares Chip/Module level configuration for an Analog Out provider
     */
    typedef struct s_AOutModule {
        /**
         * Identifies the type of module stored in this config
         */
        AOutType Type;

        /**
         * Contains the actual settings
         * Since C doesn't support polymorphism, this is stored as a Union. Make sure you check Type!
         */
        union u_AOutModuleImpl {
            DAC7718ModuleConfig DAC7718;
        } Config;

        /**
         * Identifies the number of elements/channels in the module
         */
        uint32_t Size;

    } AOutModule;

    /**
     * Holds intrinsic channel information for a DAC7718-backed channel
     */
    typedef struct s_DAC7718ChannelConfig {
        /**
         * The channel number on the DAC7718 chip (0-7)
         */
        uint8_t ChannelNumber;
    } DAC7718ChannelConfig;

    /**
     * Defines the immutable parameters for a single Analog out channel
     */
    typedef struct s_AOutChannel {
        /**
         * DAC channel ID in DAQiFi Board
         */
        uint8_t DaqifiDacChannelId;

        AOutType Type;

        /**
         * Contains the actual settings
         * Since C doesn't support polymorphism, this is stored as a Union. Make sure you check AOutModules[Channel].Type!
         */
        union u_AOutChannelImpl {
            DAC7718ChannelConfig DAC7718;
        } Config;

    } AOutChannel;

    // Define a storage class for analog output modules
#define MAX_AOUT_MOD 1
    ARRAYWRAPPERDEF(AOutModArray, AOutModule, MAX_AOUT_MOD);

    // Define a storage class for analog output channels
#define MAX_AOUT_CHANNEL 8
    ARRAYWRAPPERDEF(AOutArray, AOutChannel, MAX_AOUT_CHANNEL);

#define MAX_AOUT_PUBLIC_CHANNELS 8

#ifdef __cplusplus
}
#endif