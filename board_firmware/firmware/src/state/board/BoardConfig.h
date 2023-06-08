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

#include "AInConfig.h"
#include "DIOConfig.h"
#include "StreamingConfig.h"
#include "HAL/Power/PowerApi.h"
#include "HAL/UI/UI.h"
#include "HAL/NVM/DaqifiSettings.h"
#include "HAL/BQ24297/BQ24297.h"


#ifdef __cplusplus
extern "C" {
#endif
     
//! Size in chars of the board hardware revision field
#define BOARDCONFIG_HARDWARE_REVISION_SIZE              16
//! Size in chars of the board firmware revision field
#define BOARDCONFIG_FIRMWARE_REVISION_SIZE              64

//! Enumeration with the board configuration parameters.
enum eBoardParameter{
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
typedef struct sBoardConfig
{
    //! The board variant we are configured to run
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
    //! Power Structure
    tPowerConfig PowerConfig;
    //! User Interface Structure
    tUIConfig UIConfig;
    //! Stream configuration structure
    tStreamingConfig StreamingConfig;
}tBoardConfig;

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
const void *BoardConfig_Get(                                                \
                        enum eBoardParameter parameter,                     \
                        uint8_t index );

/*! This function is used for setting a board configuration parameter
 * @param[in] parameter Parameter to be set
 * @param[in] index In case that the parameter is an array, an index can be 
 * specified here for setting a specific member of the array
 * @param[in] pSetValue Pointer to the configuration value to be set
 */
void BoardConfig_Set(                                                       \
                        enum eBoardParameter parameter,                     \
                        uint8_t index,                                      \
                        const void *pSetValue );

#ifdef __cplusplus
}
#endif

#endif /* __BOARDCONFIG_H_ */