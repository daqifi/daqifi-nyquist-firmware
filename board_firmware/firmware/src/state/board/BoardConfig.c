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

tBoardConfig g_BoardConfig;

/*!
 * Initializes the g_BoardConfig structure for the current board
 * @param[in] pTopLevelSettings Board settings
 */
void InitBoardConfig(TopLevelSettings* pTopLevelSettings)
{
    // Initialize variable to known state
    memset(&g_BoardConfig, 0, sizeof(g_BoardConfig));
    
    switch (pTopLevelSettings->boardVariant)
    {
    case 3:
        memcpy(&g_BoardConfig, &g_NQ3BoardConfig, sizeof(tBoardConfig));
        break;
    case 2:
        memcpy(&g_BoardConfig, &g_NQ2BoardConfig, sizeof(tBoardConfig));
        break;
    case 1: // Everything else is an NQ1
    default:
        memcpy(&g_BoardConfig, &g_NQ1BoardConfig, sizeof(tBoardConfig));
        break;
    }
    
    // Set board version information from settings memory
    g_BoardConfig.BoardVariant = pTopLevelSettings->boardVariant;
    memcpy(                 &g_BoardConfig.boardFirmwareRev,                \
                            &pTopLevelSettings->boardFirmwareRev,           \
                            16);
    memcpy(                 &g_BoardConfig.boardHardwareRev,                \
                            &pTopLevelSettings->boardHardwareRev,           \
                            16);
    g_BoardConfig.boardSerialNumber = ((uint64_t)DEVSN1 << 32) | DEVSN0;
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
        case BOARDCONFIG_VARIANT:
            return &g_BoardConfig.BoardVariant;
        case BOARDCONFIG_HARDWARE_REVISION:
            return g_BoardConfig.boardHardwareRev;
        case BOARDCONFIG_FIRMWARE_REVISION:
            return g_BoardConfig.boardFirmwareRev;
        case BOARDCONFIG_SERIAL_NUMBER:
            return &g_BoardConfig.boardSerialNumber;
        case BOARDCONFIG_DIO_CHANNEL:
            if( index < g_BoardConfig.DIOChannels.Size ){
                return &g_BoardConfig.DIOChannels.Data[ index ];
            }
            return NULL;
        case BOARDCONFIG_AIN_MODULE:
            if( index < g_BoardConfig.AInModules.Size ){
                return &g_BoardConfig.AInModules.Data[ index ];
            }
            return NULL;
        case BOARDCONFIG_POWER_CONFIG:
            return &g_BoardConfig.PowerConfig;
        case BOARDCONFIG_UI_CONFIG:
            return &g_BoardConfig.UIConfig;
        case BOARDCONFIG_STREAMING_CONFIG:
            return &g_BoardConfig.StreamingConfig;
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
            g_BoardConfig.BoardVariant = *((uint8_t *)pSetValue );
            break;
        case BOARDCONFIG_HARDWARE_REVISION:
            memcpy(                                                         \
                            g_BoardConfig.boardHardwareRev,                 \
                            pSetValue,                                      \
                            BOARDCONFIG_HARDWARE_REVISION_SIZE );
            break;
        case BOARDCONFIG_FIRMWARE_REVISION:
            memcpy(                                                         \
                            g_BoardConfig.boardFirmwareRev,                 \
                            pSetValue,                                      \
                            BOARDCONFIG_FIRMWARE_REVISION_SIZE );
            break;
        case BOARDCONFIG_SERIAL_NUMBER:
            g_BoardConfig.boardSerialNumber = *( (uint64_t *)pSetValue);
            break;
        case BOARDCONFIG_DIO_CHANNEL:
            if( index < g_BoardConfig.DIOChannels.Size ){
                memcpy(                                                     \
                            &g_BoardConfig.DIOChannels.Data[ index ],       \
                            pSetValue,                                      \
                            sizeof( DIOConfig ) );
            }
            break;
        case BOARDCONFIG_AIN_MODULE:
            if( index < g_BoardConfig.AInModules.Size ){
                memcpy(                                                     \
                            &g_BoardConfig.AInModules.Data[ index ],        \
                            pSetValue,                                      \
                            sizeof( AInModule ) );
            }
            break;
        case BOARDCONFIG_POWER_CONFIG:
            memcpy(                                                         \
                            &g_BoardConfig.PowerConfig,                     \
                            pSetValue,                                      \
                            sizeof(tPowerConfig) );
            break;
        case BOARDCONFIG_UI_CONFIG:
            memcpy(                                                         \
                            &g_BoardConfig.UIConfig,                        \
                            pSetValue,                                      \
                            sizeof( tUIConfig ) );
            break;
        case BOARDCONFIG_STREAMING_CONFIG:
            memcpy(                                                         \
                            &g_BoardConfig.StreamingConfig,                 \
                            pSetValue,                                      \
                            sizeof( tStreamingConfig ) );
        case BOARDCONFIG_NUM_OF_ELEMENTS:
        default:
            break;
    }
}