/* @file BoardRuntimeConfig.c
 * This file configure the board parameters in runtime 
 */

#include "BoardRuntimeConfig.h"

tBoardRuntimeConfig __attribute__((coherent)) pBoardRuntimeConfig;

/*!
 * Initializes the boardRuntimeConfig structure for the current board
 * @param[in] boardVariant Board version, it could NQ1, NQ2 or NQ3. 
 */
void InitBoardRuntimeConfig(int boardVariant)
{
    // Initialize variable to known state
    memset(&pBoardRuntimeConfig, 0, sizeof(pBoardRuntimeConfig));
    
    switch (boardVariant)
    {
    case 3:
        memcpy(         &pBoardRuntimeConfig,                               
                        &g_NQ3BoardRuntimeConfig,                           
                        sizeof(tBoardRuntimeConfig));
        break;
    case 2:
        memcpy(         &pBoardRuntimeConfig,                               
                        &g_NQ2BoardRuntimeConfig,                           
                        sizeof(tBoardRuntimeConfig));
        break;
    case 1: // Everything else is an NQ1
    default:
        memcpy(         &pBoardRuntimeConfig,                               
                        &g_NQ1BoardRuntimeConfig,                           
                        sizeof(tBoardRuntimeConfig));
        break;
    }
}

/*! This function is used for getting a board configuration in run time         
 * parameter
 * @param[in] parameter Parameter to be get
 * @param[in] index In case that the parameter is an array, an index can be 
 * specified here for getting a specific member of the array
 * @return Parameter which is part of the global Board Configuration structure
 */
void *BoardRunTimeConfig_Get(enum eBoardRunTimeParameter parameter)
{
    switch( parameter ){
        case BOARDRUNTIMECONFIG_ALL_CONFIG:
            return &pBoardRuntimeConfig;
        case BOARDRUNTIMECONFIG_DIO_CHANNELS:
            return &pBoardRuntimeConfig.DIOChannels;
        case BOARDRUNTIMECONFIG_DIO_GLOBAL_ENABLE:
            return &pBoardRuntimeConfig.DIOGlobalEnable;
        case BOARDRUNTIMECONFIG_AIN_MODULES:
            return &pBoardRuntimeConfig.AInModules;
        case BOARDRUNTIMECONFIG_AIN_CHANNELS:
            return &pBoardRuntimeConfig.AInChannels;
        case BOARDRUNTIME_POWER_WRITE_VARIABLES:
            return &pBoardRuntimeConfig.PowerWriteVars;
        case BOARDRUNTIME_UI_WRITE_VARIABLES: 
            return &pBoardRuntimeConfig.UIWriteVars; 
        case BOARDRUNTIME_STREAMING_CONFIGURATION:
            return &pBoardRuntimeConfig.StreamingConfig;
        case BOARDRUNTIME_WIFI_SETTINGS:
             return &pBoardRuntimeConfig.wifiSettings;
        case BOARDRUNTIME_USB_SETTINGS:
            return &pBoardRuntimeConfig.usbSettings; 
        case BOARDRUNTIME_SD_CARD_SETTINGS:
            return &pBoardRuntimeConfig.sdCardConfig;
        case BOARDRUNTIMECONFIG_NUM_OF_ELEMENTS:
        default:
            return NULL;
    } 
}
