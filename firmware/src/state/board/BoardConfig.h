/*! @file BoardConfig.h
 * @brief Interface of the Board Configuration module
 * 
 * j.longares@abluethinginthecloud.com
 * 
 * A Blue Thing In The Cloud S.L.U.
 *   === When Technology becomes art ===
 * www.abluethinginthecloud.com
 *     
 */

#ifndef __BOARDCONFIG_H__
#define __BOARDCONFIG_H__

//#define DIO_TIMING_TEST

/* Ad-hoc DIO probe enable mask. Each bit N enables DIO_PROBE_*(N)
 * macro expansion for probe ID N (maps to DIO channel N). Bits 10-15
 * are the ad-hoc probe range; bits 0-9 are reserved for standard
 * pipeline probes. Default 0 = all ad-hoc probe macros compile to
 * no-ops. See HAL/DioProbe.h. */
#define DIO_PROBE_ENABLE_MASK 0x0000u

#include "AInConfig.h"
#include "AOutConfig.h"
#include "DIOConfig.h"
#include "StreamingConfig.h"
#include "services/daqifi_settings.h"
#include "HAL/BQ24297/BQ24297.h"
#include "HAL/Power/PowerApi.h"
#include "HAL/UI/UI.h"

#ifdef __cplusplus
extern "C" {
#endif

    // Product name for all DAQiFi Nyquist devices
    #define DAQIFI_PRODUCT_NAME "Nyquist"

    //! Size in chars of the board hardware revision field
#define BOARDCONFIG_HARDWARE_REVISION_SIZE              16
    //! Size in chars of the board firmware revision field
#define BOARDCONFIG_FIRMWARE_REVISION_SIZE              64

    /**
     * Per-variant capability flags surfaced by the #327 rollup.
     * Each flag answers "does this board have the hardware / firmware
     * support for feature X?" — structural, not runtime state.
     * Populated in NQ1/NQ2/NQ3 BoardConfig.c; read by the Capabilities
     * module at rollup-query time.
     *
     * Add new fields here rather than hardcoding strings in
     * Capabilities.c so that a new variant (NQ4, third-party fork,
     * etc.) becomes fully declarative: a new BoardConfig.c entry
     * with the right flags automatically drives the capability JSON.
     */
    typedef struct sCapabilitiesFlags {
        bool sdSupported;               //!< SD SPI hardware fitted
        uint8_t nvmSettingsSlots;       //!< Number of NVM config slots
        bool batteryPresent;            //!< Battery connector + BMS
        bool externalPowerSupported;    //!< 5V external rail path
        bool otgSupported;              //!< BQ24297 OTG boost available
        bool usbSupported;              //!< USB CDC interface fitted
        bool wifiSupported;             //!< WiFi chipset fitted
        bool ethernetSupported;         //!< Ethernet PHY fitted
        bool serialDebugSupported;      //!< ICSP / debug UART pins broken out

        //! Streaming rate that is guaranteed zero-drop for 60s
        //! regardless of which other dials the client turns
        //! (all channels, heaviest encoder, all interfaces, DIO +
        //! OBDiag on). See issue #344 for the worst-case
        //! characterization test that populates this field. Clients
        //! may use it as a safe default without having to reason
        //! about transport / encoder / DIO interactions.
        //!
        //! Placeholder value pending actual measurement — set
        //! conservatively based on existing Session 20/21 SD CSV
        //! 16ch ceilings until characterization lands.
        uint32_t streamingConservativeEnvelopeHz;
    } tCapabilitiesFlags;

    //! Enumeration with the board configuration parameters.

    enum eBoardParameter {
        //! Board config
        BOARDCONFIG_ALL_CONFIG,
        //! Board variant element
        BOARDCONFIG_VARIANT,
        //! Hardware revision element
        BOARDCONFIG_HARDWARE_REVISION,
        //! Firmware revision element
        BOARDCONFIG_FIRMWARE_REVISION,
        //! Board Serial Number element
        BOARDCONFIG_SERIAL_NUMBER,
        //! DIO Channel element
        BOARDCONFIG_DIO_CHANNEL,
        //! AIN Module element
        BOARDCONFIG_AIN_MODULE,
        //! AIN channels
        BOARDCONFIG_AIN_CHANNELS,
        //! AOUT Module element
        BOARDCONFIG_AOUT_MODULE,
        //! AOUT channels
        BOARDCONFIG_AOUT_CHANNELS,
        //! Power Config element
        BOARDCONFIG_POWER_CONFIG,
        //! UI Config element
        BOARDCONFIG_UI_CONFIG,
        //! Streaming config
        BOARDCONFIG_STREAMING_CONFIG,
        //! Number of elements
        BOARDCONFIG_NUM_OF_ELEMENTS
    };

    /*! @struct sBoardConfig
     * @brief Data structure for the intrinsic/immutable board configuration
     * @typedef tBoardConfig
     * @brief Data type associated to the structure sBoardConfig
     */
    typedef struct sBoardConfig {
        //! The board variant we are configured to run (1=NQ1, 2=NQ2, 3=NQ3)
        uint8_t BoardVariant;
        //! The board hardware revision
        char boardHardwareRev[ BOARDCONFIG_HARDWARE_REVISION_SIZE ];
        //! The board firmware revision
        char boardFirmwareRev[ BOARDCONFIG_FIRMWARE_REVISION_SIZE ];
        //! The board serial number
        uint64_t boardSerialNumber;
        //! The defined digital IO channels. This is an array where index = channel
        // and data = configuration
        DIOArray DIOChannels;
        //! The defined analog input modules. This is an array where index = module 
        // and data = configuration
        AInModArray AInModules;
        //! The defined analog input channels. This is an array where 
        // index = channel and data = configuration
        AInArray AInChannels;
        //! The defined analog output modules. This is an array where index = module 
        // and data = configuration
        AOutModArray AOutModules;
        //! The defined analog output channels. This is an array where 
        // index = channel and data = configuration
        AOutArray AOutChannels;
        //! Power Structure
        tPowerConfig PowerConfig;
        //! User Interface Structure
        tUIConfig UIConfig;
        //! Stream configuration structure
        tStreamingConfig StreamingConfig;
        //! CSV column headers (pre-computed for fast header generation)
        // Format: csvChannelHeadersFirst[i] = "chN_ts,chN_val"
        //         csvChannelHeadersSubsequent[i] = ",chN_ts,chN_val"
        const char* const* csvChannelHeadersFirst;
        const char* const* csvChannelHeadersSubsequent;
        //! Default voltage precision for this board's ADC resolution.
        //! 0 = integer mV, 4 = 12-bit, 6 = 18-bit, 7 = 24-bit
        uint8_t DefaultVoltagePrecision;
        //! Per-variant capability flags consumed by the #327 rollup
        tCapabilitiesFlags CapabilitiesFlags;
    } tBoardConfig;

    /*!
     * Initializes the g_BoardConfig structure for the current board
     * @param[in] pTopLevelSettings Board settings
     */
    void InitBoardConfig(TopLevelSettings* pTopLevelSettings);

    /*! This function is used for getting a board configuration parameter
     * @param[in] parameter Parameter to be get
     * @param[in] index In case that the parameter is an array, an index can be 
     * specified here for getting a specific member of the array
     * @return Parameter which is part of the global Board Configuration structure
     */
    void *BoardConfig_Get(
            enum eBoardParameter parameter,
            uint8_t index);

    /*! This function is used for setting a board configuration parameter
     * @param[in] parameter Parameter to be set
     * @param[in] index In case that the parameter is an array, an index can be 
     * specified here for setting a specific member of the array
     * @param[in] pSetValue Pointer to the configuration value to be set
     */
    void BoardConfig_Set(
            enum eBoardParameter parameter,
            uint8_t index,
            const void *pSetValue);

#ifdef __cplusplus
}
#endif

#endif /* __BOARDCONFIG_H_ */