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
#include "NQ1BoardConfig.h"
#include "BoardConfig.h"

static tBoardConfig boardConfig;

/*!
 * Initializes the doardConfig structure for the current board
 * @param[in] pTopLevelSettings Board settings
 */
void InitBoardConfig(TopLevelSettings* pTopLevelSettings) {
    // Initialize variable to known state
    memset(&boardConfig, 0, sizeof (boardConfig));

    switch (pTopLevelSettings->boardVariant) {
        case 3:
            //memcpy(&boardConfig, NQ3BoardConfig_Get(), sizeof(tBoardConfig));
            break;
        case 2:
            //memcpy(&boardConfig, NQ2BoardConfig_Get(), sizeof(tBoardConfig));
            break;
        case 1: // Everything else is an NQ1
        default:
            memcpy(&boardConfig, NQ1BoardConfig_Get(), sizeof (tBoardConfig));
            break;
    }

    // Set board version information from settings memory
    boardConfig.BoardVariant = pTopLevelSettings->boardVariant;
    memcpy(&boardConfig.boardFirmwareRev,
            &pTopLevelSettings->boardFirmwareRev,
            16);
    memcpy(&boardConfig.boardHardwareRev,
            &pTopLevelSettings->boardHardwareRev,
            16);
    boardConfig.boardSerialNumber = ((uint64_t) DEVSN1 << 32) | DEVSN0;
}

/*! This function is used for getting a board configuration parameter
 * @param[in] parameter Parameter to be get
 * @param[in] index In case that the parameter is an array, an index can be 
 * specified here for getting a specific member of the array
 * @return Parameter which is part of the global Board Configuration structure
 */
void *BoardConfig_Get(
        enum eBoardParameter parameter,
        uint8_t index) {
    switch (parameter) {
        case BOARDCONFIG_ALL_CONFIG:
            return &boardConfig;
        case BOARDCONFIG_VARIANT:
            return &boardConfig.BoardVariant;
        case BOARDCONFIG_HARDWARE_REVISION:
            return boardConfig.boardHardwareRev;
        case BOARDCONFIG_FIRMWARE_REVISION:
            return boardConfig.boardFirmwareRev;
        case BOARDCONFIG_SERIAL_NUMBER:
            return &boardConfig.boardSerialNumber;
        case BOARDCONFIG_DIO_CHANNEL:
            if (index < boardConfig.DIOChannels.Size) {
                return &boardConfig.DIOChannels.Data[ index ];
            }
            return NULL;
        case BOARDCONFIG_AIN_MODULE:
            if (index < boardConfig.AInModules.Size) {
                return &boardConfig.AInModules.Data[ index ];
            }
            return NULL;
        case BOARDCONFIG_AIN_CHANNELS:
            return &boardConfig.AInChannels;
            //        case BOARDCONFIG_POWER_CONFIG:
            //            return &boardConfig.PowerConfig;
            //        case BOARDCONFIG_UI_CONFIG:
            //            return &boardConfig.UIConfig;
        case BOARDCONFIG_STREAMING_CONFIG:
            return &boardConfig.StreamingConfig;
            //        case BOARDCONFIG_NUM_OF_ELEMENTS:
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
void BoardConfig_Set(
        enum eBoardParameter parameter,
        uint8_t index,
        const void *pSetValue) {
    if (pSetValue == NULL) {
        return;
    }
    switch (parameter) {
        case BOARDCONFIG_VARIANT:
            boardConfig.BoardVariant = *((uint8_t *) pSetValue);
            break;
        case BOARDCONFIG_HARDWARE_REVISION:
            memcpy(
                    boardConfig.boardHardwareRev,
                    pSetValue,
                    BOARDCONFIG_HARDWARE_REVISION_SIZE);
            break;
        case BOARDCONFIG_FIRMWARE_REVISION:
            memcpy(
                    boardConfig.boardFirmwareRev,
                    pSetValue,
                    BOARDCONFIG_FIRMWARE_REVISION_SIZE);
            break;
        case BOARDCONFIG_SERIAL_NUMBER:
            boardConfig.boardSerialNumber = *((uint64_t *) pSetValue);
            break;
        case BOARDCONFIG_DIO_CHANNEL:
            if (index < boardConfig.DIOChannels.Size) {
                memcpy(
                        &boardConfig.DIOChannels.Data[ index ],
                        pSetValue,
                        sizeof ( DIOConfig));
            }
            break;
        case BOARDCONFIG_AIN_MODULE:
            if (index < boardConfig.AInModules.Size) {
                memcpy(
                        &boardConfig.AInModules.Data[ index ],
                        pSetValue,
                        sizeof ( AInModule));
            }
            break;
            //        case BOARDCONFIG_POWER_CONFIG:
            //            memcpy(                                                         
            //                            &boardConfig.PowerConfig,                      
            //                            pSetValue,                                      
            //                            sizeof(tPowerConfig) );
            //            break;
            //        case BOARDCONFIG_UI_CONFIG:
            //            memcpy(                                                         
            //                            &boardConfig.UIConfig,                         
            //                            pSetValue,                                      
            //                            sizeof( tUIConfig ) );
            //            break;
        case BOARDCONFIG_STREAMING_CONFIG:
            memcpy(
                    &boardConfig.StreamingConfig,
                    pSetValue,
                    sizeof ( tStreamingConfig));
        case BOARDCONFIG_NUM_OF_ELEMENTS:
        default:
            break;
    }
}