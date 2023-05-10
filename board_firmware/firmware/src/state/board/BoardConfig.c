/*! @file BoardConfig.c
 * @brief Implementation of the board configuration module
 * 
 * j.longares@abluethinginthecloud.com
 * 
 * A Blue Thing In The Cloud S.L.U.
 *   === When Technology becomes art ===
 * www.abluethinginthecloud.com
 *     
 */

#include "BoardConfig.h"

tBoardConfig pBoardConfig;

/*!
 * Initializes the doardConfig structure for the current board
 * @param[in] pTopLevelSettings Board settings
 */
void InitBoardConfig(TopLevelSettings* pTopLevelSettings)
{
    // Initialize variable to known state
    memset(&pBoardConfig, 0, sizeof(pBoardConfig));
    
    switch (pTopLevelSettings->boardVariant)
    {
    case 3:
        memcpy(&pBoardConfig, &g_NQ3BoardConfig, sizeof(tBoardConfig));
        break;
    case 2:
        memcpy(&pBoardConfig, &g_NQ2BoardConfig, sizeof(tBoardConfig));
        break;
    case 1: // Everything else is an NQ1
    default:
        memcpy(&pBoardConfig, &g_NQ1BoardConfig, sizeof(tBoardConfig));
        break;
    }
    
    // Set board version information from settings memory
    pBoardConfig.BoardVariant = pTopLevelSettings->boardVariant;
    memcpy(                 &pBoardConfig.boardFirmwareRev,                \
                            &pTopLevelSettings->boardFirmwareRev,          \
                            16);
    memcpy(                 &pBoardConfig.boardHardwareRev,                \
                            &pTopLevelSettings->boardHardwareRev,          \
                            16);
    pBoardConfig.boardSerialNumber = ((uint64_t)DEVSN1 << 32) | DEVSN0;
}

/*! This function is used for getting a board configuration parameter
 * @param[in] parameter Parameter to be get
 * @param[in] index In case that the parameter is an array, an index can be 
 * specified here for getting a specific member of the array
 * @return Parameter which is part of the global Board Configuration structure
 */
const void *BoardConfig_Get(                                                \
                            enum eBoardParameter parameter,                 \
                            uint8_t index )
{
    switch( parameter ){
        case BOARDCONFIG_ALL_CONFIG:
            return &pBoardConfig;
        case BOARDCONFIG_VARIANT:
            return &pBoardConfig.BoardVariant;
        case BOARDCONFIG_HARDWARE_REVISION:
            return pBoardConfig.boardHardwareRev;
        case BOARDCONFIG_FIRMWARE_REVISION:
            return pBoardConfig.boardFirmwareRev;
        case BOARDCONFIG_SERIAL_NUMBER:
            return &pBoardConfig.boardSerialNumber;
        case BOARDCONFIG_DIO_CHANNEL:
            if( index < pBoardConfig.DIOChannels.Size ){
                return &pBoardConfig.DIOChannels.Data[ index ];
            }
            return NULL;
        case BOARDCONFIG_AIN_MODULE:
            if( index < pBoardConfig.AInModules.Size ){
                return &pBoardConfig.AInModules.Data[ index ];
            }
            return NULL;
        case BOARDCONFIG_AIN_CHANNELS: 
            return &pBoardConfig.AInChannels; 
        case BOARDCONFIG_POWER_CONFIG:
            return &pBoardConfig.PowerConfig;
        case BOARDCONFIG_UI_CONFIG:
            return &pBoardConfig.UIConfig;
        case BOARDCONFIG_STREAMING_CONFIG:
            return &pBoardConfig.StreamingConfig;
        case BOARDCONFIG_NUM_OF_ELEMENTS:
        default:
            return NULL;
    }
}

/*! This function is used for setting a board configuration parameter
 * @param[in] parameter Parameter to be set
 * @param[in] index In case that the parameter is an array, an index can be 
 * specified here for setting a specific member of the array
 * @param[in] pSetValue Pointer to the configuration value to be set
 */
void BoardConfig_Set(                                                       \
                            enum eBoardParameter parameter,                 \
                            uint8_t index,                                  \
                            const void *pSetValue )
{
    if( pSetValue == NULL ){
        return;
    }
    switch( parameter ){
        case BOARDCONFIG_VARIANT:
            pBoardConfig.BoardVariant = *((uint8_t *)pSetValue );
            break;
        case BOARDCONFIG_HARDWARE_REVISION:
            memcpy(                                                         \
                            pBoardConfig.boardHardwareRev,                  \
                            pSetValue,                                      \
                            BOARDCONFIG_HARDWARE_REVISION_SIZE );
            break;
        case BOARDCONFIG_FIRMWARE_REVISION:
            memcpy(                                                         \
                            pBoardConfig.boardFirmwareRev,                  \
                            pSetValue,                                      \
                            BOARDCONFIG_FIRMWARE_REVISION_SIZE );
            break;
        case BOARDCONFIG_SERIAL_NUMBER:
            pBoardConfig.boardSerialNumber = *( (uint64_t *)pSetValue);
            break;
        case BOARDCONFIG_DIO_CHANNEL:
            if( index < pBoardConfig.DIOChannels.Size ){
                memcpy(                                                     \
                            &pBoardConfig.DIOChannels.Data[ index ],        \
                            pSetValue,                                      \
                            sizeof( DIOConfig ) );
            }
            break;
        case BOARDCONFIG_AIN_MODULE:
            if( index < pBoardConfig.AInModules.Size ){
                memcpy(                                                     \
                            &pBoardConfig.AInModules.Data[ index ],         \
                            pSetValue,                                      \
                            sizeof( AInModule ) );
            }
            break;
        case BOARDCONFIG_POWER_CONFIG:
            memcpy(                                                         \
                            &pBoardConfig.PowerConfig,                      \
                            pSetValue,                                      \
                            sizeof(tPowerConfig) );
            break;
        case BOARDCONFIG_UI_CONFIG:
            memcpy(                                                         \
                            &pBoardConfig.UIConfig,                         \
                            pSetValue,                                      \
                            sizeof( tUIConfig ) );
            break;
        case BOARDCONFIG_STREAMING_CONFIG:
            memcpy(                                                         \
                            &pBoardConfig.StreamingConfig,                  \
                            pSetValue,                                      \
                            sizeof( tStreamingConfig ) );
        case BOARDCONFIG_NUM_OF_ELEMENTS:
        default:
            break;
    }
}