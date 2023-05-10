#pragma once

#include "system_config.h"
#include "system_definitions.h"

#include "AInRuntimeConfig.h"
#include "DIORuntimeConfig.h"
#include "StreamingRuntimeConfig.h"
#include "TcpServerConfig.h"
#include "UsbRuntimeConfig.h"
#include "Util/ArrayWrapper.h"
#include "HAL/Power/PowerApi.h"
#include "HAL/UI/UI.h"
#include "HAL/NVM/DaqifiSettings.h"

#ifdef __cplusplus
extern "C" {
#endif
    
/*! @enum eBoardParameter
 * @brief Enumeration with the board configuration in run time parameters.
 */
enum eBoardRunTimeParameter{
    //! Board config 
    BOARDRUNTIMECONFIG_ALL_CONFIG, 
    //! Board digital input/output channels
    BOARDRUNTIMECONFIG_DIO_CHANNELS,
    //! Digital input/output global enable 
    BOARDRUNTIMECONFIG_DIO_GLOBAL_ENABLE,
    //! Runtime setting analog input modules
    BOARDRUNTIMECONFIG_AIN_MODULES, 
    //! Board analog input channels
    BOARDRUNTIMECONFIG_AIN_CHANNELS,
    //! Power write variables
    BOARDRUNTIME_POWER_WRITE_VARIABLES, 
    //! User interface write variables
    BOARDRUNTIME_UI_WRITE_VARIABLES, 
    //! Streaming configuration parameters
    BOARDRUNTIME_STREAMING_CONFIGURATION, 
    //! Wifi settings for runtime
    BOARDRUNTIME_WIFI_SETTINGS, 
    //! Desired USB settings in runtime 
    BOARDRUNTIME_USB_SETTINGS, 
    //! Runtime TCP server state
    BOARDRUNTIME_SERVER_DATA,
    //! Number of elements
    BOARDRUNTIMECONFIG_NUM_OF_ELEMENTS
};
    
/*! @struct sBoardRuntimeConfig
 * @brief Data structure for the intrinsic/immutable board configuration
 * @typedef tBoardRuntimeConfig
 * @brief Data type associated to the structure sBoardRuntimeConfig
 */
typedef struct sBoardRuntimeConfig
{
    //!Runtime settings for the digital IO channels
    //!This is an array where index = channel and data = configuration
    DIORuntimeArray DIOChannels;
    //!The defined digital IO global enable
    //!This is an bool which defines the global DIO enable 
    //!(true, DIO is transmitted when streaming)
    bool DIOGlobalEnable;
    //!Runtime settings for the analog input modules
    //!This is an array where index = module and data = configuration
    AInModRuntimeArray AInModules;
    //!Runtime settings for the analog input channels
    //!This is an array where index = channel and data = configuration
    AInRuntimeArray AInChannels;
    //! Power write variables
    tPowerWriteVars PowerWriteVars;
    //! User interface write variables
    tUIWriteVars UIWriteVars;
    //! Streaming configuration parameters
    StreamingRuntimeConfig StreamingConfig;
    //! The desired wifi settings
    DaqifiSettings wifiSettings;
    //!The desired USB settings
    UsbCdcData usbSettings;
    //!The TCP server state
    TcpServerData serverData;
} tBoardRuntimeConfig;

/*!
 * Initializes the boardRuntimeConfig structure for the current board
 * @param[in] boardVariant Board version, it could NQ1, NQ2 or NQ3. 
 */
void InitBoardRuntimeConfig(int boardVariant);

/*! This function is used for getting a board configuration in run time         
 * parameter
 * @param[in] parameter Parameter to be get
 * @return Parameter which is part of the global Board Configuration structure
 */
const void *BoardRunTimeConfig_Get( enum eBoardRunTimeParameter parameter ); 
    
/**
 * The Nyquist 1 board configuration
 */
extern const tBoardRuntimeConfig g_NQ1BoardRuntimeConfig;

/**
 * The Nyquist 2 board configuration
 */
extern const tBoardRuntimeConfig g_NQ2BoardRuntimeConfig;

/**
 * The Nyquist 3 board configuration
 */
extern const tBoardRuntimeConfig g_NQ3BoardRuntimeConfig;

/**
 * Initializes the g_BoardRuntimeConfig structure for the current board
 */
void InitBoardRuntimeConfig(int boardVariant);
    
#ifdef __cplusplus
}
#endif