#pragma once

#include "configuration.h"
#include "definitions.h"
//#include <>

#include "Util/ArrayWrapper.h"

#ifdef __cplusplus
extern "C" {
#endif

    //    typedef struct sSPIConfig
    //    {
    //        SPI_MODULE_ID spiID;
    //        uint32_t baud;
    //        SPI_BAUD_RATE_CLOCK clock;                                   //Selects the type of clock is used by the Baud Rate Generator.
    //        CLK_BUSES_PERIPHERAL busClk_id;
    //
    //        SPI_CLOCK_POLARITY clockPolarity;                    //Enables clock polarity.
    //        SPI_COMMUNICATION_WIDTH busWidth;                   //Selects the data width for the SPI communication.
    //
    //        SPI_INPUT_SAMPLING_PHASE inSamplePhase;        //Selects the SPI data input sample phase.
    //
    //        SPI_OUTPUT_DATA_PHASE outDataPhase; //Selects serial output data change.
    //    } SPIConfig;

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
        //                
        //        /**
        //         * Channel is provided by an AD7609 chip
        //         */
        //        AIn_AD7609,
    } AInType;

    /**
     * Configuration for a MC12bADC module
     */
    typedef struct s_MC12bModuleConfig {
        //DRV_ADC_MODULE_ID moduleId;
        double Resolution; // Per-module resolution
    } MC12bModuleConfig;

    //    /**
    //     * Configuration for a AD7173 module
    //     */
    //    typedef struct s_AD7173ModuleConfig
    //    {
    //        /**
    //         * The module SPI config
    //         */
    //        SPIConfig SPI;
    //        
    //        /**
    //         * The port module to use for the data line (probably always PORTS_ID_0)
    //         */
    //        PORTS_MODULE_ID DataModule;
    //        
    //        PORTS_CHANNEL CS_Ch;
    //        PORTS_BIT_POS CS_Bit;
    //        
    //        PORTS_CHANNEL SPI_SDI_Ch;
    //        PORTS_BIT_POS SPI_SDI_Bit;
    //        uint32_t SPI_SDI_BitMask;
    //        
    //        PORTS_CHANNEL ERR_Ch;
    //        PORTS_BIT_POS ERR_Bit;
    //            
    //        uint32_t Resolution;
    //    } AD7173ModuleConfig;

    //    /**
    //     * Configuration for a AD7609 module
    //     * NOTE: For the moment this is identical to AD7173Config 
    //     */
    //    typedef struct s_AD7609ModuleConfig
    //    {
    //        /**
    //         * The module SPI config
    //         */
    //        SPIConfig SPI;
    //        
    //        /**
    //         * The port module to use for the data line (probably always PORTS_ID_0)
    //         */
    //        PORTS_MODULE_ID DataModule;
    //        
    //        PORTS_CHANNEL CS_Ch;
    //        PORTS_BIT_POS CS_Bit;
    //        
    //        PORTS_CHANNEL BSY_Ch;
    //        PORTS_BIT_POS BSY_Bit;
    //        uint32_t BSY_BitMask;
    //        
    //        PORTS_CHANNEL RST_Ch;
    //        PORTS_BIT_POS RST_Bit;
    //        
    //        PORTS_CHANNEL Range_Ch;
    //        PORTS_BIT_POS Range_Bit;
    //        
    //        PORTS_CHANNEL OS0_Ch;
    //        PORTS_BIT_POS OS0_Bit;
    //        
    //        PORTS_CHANNEL OS1_Ch;
    //        PORTS_BIT_POS OS1_Bit;
    //        
    //        PORTS_CHANNEL CONVST_Ch;
    //        PORTS_BIT_POS CONVST_Bit;
    //        
    //        /**
    //         * The value of the range pin. 0 = 5V, 1 = 10V
    //         * TODO: Should this be a runtime variable
    //         */
    //        bool Range10V;
    //        
    //        /**
    //         * The Oversampling mode.
    //         * 00: No Oversample
    //         * 01: OS Ratio 2
    //         * 10: OS Ratio 4
    //         * 11: OS Ratio 8
    //         * TODO: According to http://www.analog.com/media/en/technical-documentation/data-sheets/AD7609.pdf 
    //         *       there is another OS pin which provides access to OS Ratios 16, 32 and 64). Verify that this is not implemented.
    //         */
    //        uint8_t OSMode;
    //        
    //        uint32_t Resolution;
    //        
    //    } AD7609ModuleConfig;

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
            //            AD7609ModuleConfig AD7609;
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

    //    /**
    //     * Holds intrinsic channel information for an AD7173-backed channel
    //     */
    //    typedef struct s_AD7173ChannelConfig
    //    {
    //        // The Channel on the Chip (0-15)
    //        uint8_t ChannelIndex;
    //    } AD7173ChannelConfig;

    //    /**
    //     * Holds intrinsic channel information for an AD7609-backed channel
    //     */
    //    typedef struct s_AD7609ChannelConfig
    //    {
    //        // The Channel on the Chip (0-7)
    //        uint8_t ChannelIndex;
    //    } AD7609ChannelConfig;
    //    

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
            //            AD7609ChannelConfig AD7609;
        } Config;

    } AInChannel;

    // Define a storage class for analog input modules
#define MAX_AIN_MOD 1
    ARRAYWRAPPERDEF(AInModArray, AInModule, MAX_AIN_MOD);

    // Define a storage class for analog input channels
#define MAX_AIN_CHANNEL 48
    ARRAYWRAPPERDEF(AInArray, AInChannel, MAX_AIN_CHANNEL);

#define MAX_AIN_PUBLIC_CHANNELS 16

#define ADC_CHANNEL_VSYS 248
#define ADC_CHANNEL_5VREF 249
#define ADC_CHANNEL_3_3V 250
#define ADC_CHANNEL_2_5VREF 251
#define ADC_CHANNEL_VBATT 252
#define ADC_CHANNEL_5V 253
#define ADC_CHANNEL_10V 254
#define ADC_CHANNEL_TEMP 255

#ifdef __cplusplus
}
#endif
