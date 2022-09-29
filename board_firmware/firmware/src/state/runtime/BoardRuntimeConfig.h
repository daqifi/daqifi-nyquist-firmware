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
    /**
     * Defines the intrinsic/immutable board configuration
     */
    typedef struct s_BoardRuntimeConfig
    {
        /**
         * Runtime settings for the digital IO channels
         * This is an array where index = channel and data = configuration
         */
        DIORuntimeArray DIOChannels;
        
        /**
         * The defined digital IO global enable
         * This is an bool which defines the global DIO enable (true, DIO is transmitted when streaming)
         */
        bool DIOGlobalEnable;
        
        /**
         * Runtime settings for the analog input modules
         * This is an array where index = module and data = configuration
         */
        AInModRuntimeArray AInModules;
        
        /**
         * Runtime settings for the analog input channels
         * This is an array where index = channel and data = configuration
         */
        AInRuntimeArray AInChannels;
                
        sPowerWriteVars PowerWriteVars;
        
        sUIWriteVars UIWriteVars;
        
        StreamingRuntimeConfig StreamingConfig;
        
        /**
         * The desired wifi settings
         */
        DaqifiSettings wifiSettings;
        
        /**
         * The desired USB settings
         */
        UsbCdcData usbSettings;
        
        /**
         * The TCP server state
         */
        TcpServerData serverData;
        
    } BoardRuntimeConfig;
    
    /**
     * The board configuration
     */
    extern BoardRuntimeConfig __attribute__((coherent)) g_BoardRuntimeConfig;

    /**
     * The Nyquist 1 board configuration
     */
    extern const BoardRuntimeConfig g_NQ1BoardRuntimeConfig;
    
    /**
     * The Nyquist 2 board configuration
     */
    extern const BoardRuntimeConfig g_NQ2BoardRuntimeConfig;
    
    /**
     * The Nyquist 3 board configuration
     */
    extern const BoardRuntimeConfig g_NQ3BoardRuntimeConfig;
    
    /**
     * Initializes the g_BoardRuntimeConfig structure for the current board
     */
    void InitBoardRuntimeConfig(int boardVariant);
    
#ifdef __cplusplus
}
#endif