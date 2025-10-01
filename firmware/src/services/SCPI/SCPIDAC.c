#include "SCPIDAC.h"

// General
#include <stdlib.h>
#include <string.h>

// Harmony
#include "configuration.h"
#include "definitions.h"

// Project
#include "Util/StringFormatters.h"
#include "Util/Logger.h"
#include "state/data/BoardData.h"
#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "state/runtime/AOutRuntimeConfig.h"
#include "HAL/DAC7718/DAC7718.h"
#include "HAL/Power/PowerApi.h"
#include "../daqifi_settings.h"

// Flag to track if DAC hardware has been initialized
static bool dacHardwareInitialized = false;

// Static DAC configuration to avoid stack usage
static tDAC7718Config dacConfig;

// Helper function to ensure DAC hardware is initialized when power is up
static bool DAC_EnsureHardwareInitialized(void) {
    if (dacHardwareInitialized) {
        return true; // Already initialized
    }
    
    // Check if power is up (10V rail needed for DAC7718)
    const tPowerData* pPowerState = BoardData_Get(BOARDDATA_POWER_DATA, 0);
    if (pPowerState == NULL) {
        LOG_E("DAC_EnsureHardwareInitialized: Cannot get power state data");
        return false;
    }
    
    // POWERED_UP (1) has 10V rail, POWERED_UP_EXT_DOWN (2) does not have 10V rail
    if (pPowerState->powerState != POWERED_UP) {
        return false;
    }
    
    // Get DAC configuration from board config and initialize hardware
    const tBoardConfig* pBoardConfig = BoardConfig_Get(BOARDCONFIG_ALL_CONFIG, 0);
    if (pBoardConfig == NULL || pBoardConfig->AOutModules.Size == 0) {
        return false;
    }
    
    // Test minimal operations to isolate TLB exception
    dacConfig.CS_Pin = GPIO_PIN_RK0;     // CS on RK0  
    dacConfig.RST_Pin = GPIO_PIN_RJ13;   // CLR/RST on RJ13
    
    // Test config creation + basic init (no hardware operations)
    uint8_t dacId = DAC7718_NewConfig(&dacConfig);
    DAC7718_Init(dacId, 0); // Hardware init with operations bypassed

    dacHardwareInitialized = true;
    return true;
}

// Helper function to find DAC channel index
static size_t DAC_FindChannelIndex(uint8_t channelId) {
    AOutArray* pBoardConfigAOutChannels = BoardConfig_Get(BOARDCONFIG_AOUT_CHANNELS, 0);
    
    if (pBoardConfigAOutChannels == NULL) {
        return SIZE_MAX; // Invalid index
    }
    
    for (size_t i = 0; i < pBoardConfigAOutChannels->Size; i++) {
        if (pBoardConfigAOutChannels->Data[i].DaqifiDacChannelId == channelId) {
            return i;
        }
    }
    return SIZE_MAX; // Channel not found
}

// Helper function to convert voltage to DAC counts
// Uses configuration values for voltage range and resolution
static uint32_t DAC_VoltageToCounts(double voltage, const DAC7718ModuleConfig* config) {
    // Unipolar conversion: 0V = 0 counts, MaxV = (Resolution-1) counts
    if (voltage < config->MinVoltage) voltage = config->MinVoltage;
    if (voltage > config->MaxVoltage) voltage = config->MaxVoltage;

    double voltageSpan = config->MaxVoltage - config->MinVoltage;
    double normalizedVoltage = (voltage - config->MinVoltage) / voltageSpan;
    uint32_t counts = (uint32_t)(normalizedVoltage * (config->Resolution - 1) + 0.5);

    return counts;
}

// Helper function to convert DAC counts to voltage
// Uses configuration values for voltage range and resolution
// Note: Currently unused but reserved for future DAC readback implementation
static double DAC_CountsToVoltage(uint32_t counts, const DAC7718ModuleConfig* config) __attribute__((unused));
static double DAC_CountsToVoltage(uint32_t counts, const DAC7718ModuleConfig* config) {
    // Unipolar conversion
    if (counts >= config->Resolution) counts = config->Resolution - 1;

    double voltageSpan = config->MaxVoltage - config->MinVoltage;
    double normalizedCounts = (double)counts / (double)(config->Resolution - 1);
    return config->MinVoltage + (normalizedCounts * voltageSpan);
}

scpi_result_t SCPI_DACVoltageSet(scpi_t * context) {
    int channel;
    double voltage;
    AOutArray* pBoardConfigAOutChannels = BoardConfig_Get(BOARDCONFIG_AOUT_CHANNELS, 0);
    AOutModArray* pBoardConfigAOutModules = BoardConfig_Get(BOARDCONFIG_AOUT_MODULE, 0);

    if (pBoardConfigAOutChannels == NULL || pBoardConfigAOutModules == NULL) {
        LOG_E("SCPI_DACVoltageSet: No DAC channels configured");
        return SCPI_RES_ERR;
    }

    // Ensure DAC hardware is initialized (lazy initialization when power is up)
    if (!DAC_EnsureHardwareInitialized()) {
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }

    // Get DAC7718 module configuration for voltage conversion
    const DAC7718ModuleConfig* dacConfig = &pBoardConfigAOutModules->Data[AOut_DAC7718].Config.DAC7718;

    if (SCPI_ParamInt32(context, &channel, FALSE) && SCPI_ParamDouble(context, &voltage, TRUE)) {
        // Set single channel
        size_t index = DAC_FindChannelIndex((uint8_t)channel);
        if (index >= pBoardConfigAOutChannels->Size) {
            return SCPI_RES_ERR;
        }

        // Convert voltage to DAC counts using configuration
        uint32_t counts = DAC_VoltageToCounts(voltage, dacConfig);

        // Get hardware channel number from board configuration
        uint8_t hwChannel = pBoardConfigAOutChannels->Data[index].Config.DAC7718.ChannelNumber;
        uint8_t dacRegister = 8 + hwChannel;  // DAC-0 = register 8
        DAC7718_ReadWriteReg(0, 0, dacRegister, counts);
        DAC7718_UpdateLatch(0);

    } else if (SCPI_ParamDouble(context, &voltage, TRUE)) {
        // Set all channels to the same voltage
        uint32_t counts = DAC_VoltageToCounts(voltage, dacConfig);

        for (size_t i = 0; i < pBoardConfigAOutChannels->Size; i++) {
            uint8_t hwChannel = pBoardConfigAOutChannels->Data[i].Config.DAC7718.ChannelNumber;
            uint8_t dacRegister = 8 + hwChannel;  // DAC-0 = register 8
            DAC7718_ReadWriteReg(0, 0, dacRegister, counts);
        }

        // Update all DAC latches
        DAC7718_UpdateLatch(0);
    } else {
        return SCPI_RES_ERR;
    }

    return SCPI_RES_OK;
}

scpi_result_t SCPI_DACVoltageGet(scpi_t * context) {
    int channel;
    AOutArray* pBoardConfigAOutChannels = BoardConfig_Get(BOARDCONFIG_AOUT_CHANNELS, 0);
    
    if (pBoardConfigAOutChannels == NULL) {
        LOG_E("SCPI_DACVoltageGet: No DAC channels configured");
        return SCPI_RES_ERR;
    }

    // Ensure DAC hardware is initialized (lazy initialization when power is up)
    if (!DAC_EnsureHardwareInitialized()) {
        LOG_D("SCPI_DACVoltageGet: DAC7718 hardware not ready - returning 0.0V");
        // Return 0.0V for all channels when DAC is not ready
        if (SCPI_ParamInt32(context, &channel, FALSE)) {
            SCPI_ResultDouble(context, 0.0);
        } else {
            AOutArray* pBoardConfigAOutChannels = BoardConfig_Get(BOARDCONFIG_AOUT_CHANNELS, 0);
            if (pBoardConfigAOutChannels != NULL) {
                for (size_t i = 0; i < pBoardConfigAOutChannels->Size; i++) {
                    SCPI_ResultDouble(context, 0.0);
                }
            }
        }
        return SCPI_RES_OK;
    }

    if (SCPI_ParamInt32(context, &channel, FALSE)) {
        // Get single channel
        size_t index = DAC_FindChannelIndex((uint8_t)channel);
        if (index >= pBoardConfigAOutChannels->Size) {
            LOG_E("SCPI_DACVoltageGet: Invalid channel %d", channel);
            return SCPI_RES_ERR;
        }

        // For now, return 0.0V since DAC read functionality needs proper initialization
        // TODO: Implement proper DAC readback once initialization is complete
        SCPI_ResultDouble(context, 0.0);
        LOG_D("SCPI_DACVoltageGet: Channel %d read (returning 0.0V - DAC read not fully implemented)", channel);
    } else {
        // Get all channels
        for (size_t i = 0; i < pBoardConfigAOutChannels->Size; i++) {
            // For now, return 0.0V for all channels
            SCPI_ResultDouble(context, 0.0);
        }
        LOG_D("SCPI_DACVoltageGet: All channels read (returning 0.0V - DAC read not fully implemented)");
    }

    return SCPI_RES_OK;
}

scpi_result_t SCPI_DACChanEnableSet(scpi_t * context) {
    int channel, enabled;

    if (!SCPI_ParamInt32(context, &channel, TRUE) || !SCPI_ParamInt32(context, &enabled, TRUE)) {
        return SCPI_RES_ERR;
    }

    // TODO: Implement channel enable/disable functionality
    // This would typically be stored in runtime configuration

    return SCPI_RES_OK;
}

scpi_result_t SCPI_DACChanEnableGet(scpi_t * context) {
    int channel;
    AOutArray* pBoardConfigAOutChannels = BoardConfig_Get(BOARDCONFIG_AOUT_CHANNELS, 0);
    
    if (pBoardConfigAOutChannels == NULL) {
        return SCPI_RES_ERR;
    }

    if (SCPI_ParamInt32(context, &channel, FALSE)) {
        // Get single channel enable status
        // TODO: Get from runtime configuration when implemented
        SCPI_ResultInt32(context, 1); // Default to enabled
    } else {
        // Get all channels enable status
        for (size_t i = 0; i < pBoardConfigAOutChannels->Size; i++) {
            SCPI_ResultInt32(context, 1); // Default to enabled
        }
    }

    return SCPI_RES_OK;
}

scpi_result_t SCPI_DACChanRangeSet(scpi_t * context) {
    int channel, range;

    if (!SCPI_ParamInt32(context, &channel, TRUE) || !SCPI_ParamInt32(context, &range, TRUE)) {
        return SCPI_RES_ERR;
    }

    // TODO: Implement range setting functionality

    return SCPI_RES_OK;
}

scpi_result_t SCPI_DACChanRangeGet(scpi_t * context) {
    int channel;
    AOutArray* pBoardConfigAOutChannels = BoardConfig_Get(BOARDCONFIG_AOUT_CHANNELS, 0);
    
    if (pBoardConfigAOutChannels == NULL) {
        return SCPI_RES_ERR;
    }

    if (SCPI_ParamInt32(context, &channel, FALSE)) {
        // Get single channel range
        SCPI_ResultInt32(context, 0); // Default range
    } else {
        // Get all channels range
        for (size_t i = 0; i < pBoardConfigAOutChannels->Size; i++) {
            SCPI_ResultInt32(context, 0); // Default range
        }
    }

    return SCPI_RES_OK;
}

scpi_result_t SCPI_DACChanCalmSet(scpi_t * context) {
    int channel;
    double calM;

    if (!SCPI_ParamInt32(context, &channel, TRUE) || !SCPI_ParamDouble(context, &calM, TRUE)) {
        return SCPI_RES_ERR;
    }

    // TODO: Implement calibration M setting

    return SCPI_RES_OK;
}

scpi_result_t SCPI_DACChanCalbSet(scpi_t * context) {
    int channel;
    double calB;

    if (!SCPI_ParamInt32(context, &channel, TRUE) || !SCPI_ParamDouble(context, &calB, TRUE)) {
        return SCPI_RES_ERR;
    }

    // TODO: Implement calibration B setting

    return SCPI_RES_OK;
}

scpi_result_t SCPI_DACChanCalmGet(scpi_t * context) {
    int channel;
    
    if (!SCPI_ParamInt32(context, &channel, TRUE)) {
        return SCPI_RES_ERR;
    }
    
    // TODO: Get calibration M from runtime config
    SCPI_ResultDouble(context, 1.0); // Default calibration
    
    return SCPI_RES_OK;
}

scpi_result_t SCPI_DACChanCalbGet(scpi_t * context) {
    int channel;
    
    if (!SCPI_ParamInt32(context, &channel, TRUE)) {
        return SCPI_RES_ERR;
    }
    
    // TODO: Get calibration B from runtime config
    SCPI_ResultDouble(context, 0.0); // Default calibration
    
    return SCPI_RES_OK;
}

scpi_result_t SCPI_DACCalSave(scpi_t * context) {
    // TODO: Implement calibration save to NVM
    return SCPI_RES_OK;
}

scpi_result_t SCPI_DACCalFSave(scpi_t * context) {
    // TODO: Implement factory calibration save to NVM
    return SCPI_RES_OK;
}

scpi_result_t SCPI_DACCalLoad(scpi_t * context) {
    // TODO: Implement calibration load from NVM
    return SCPI_RES_OK;
}

scpi_result_t SCPI_DACCalFLoad(scpi_t * context) {
    // TODO: Implement factory calibration load from NVM
    return SCPI_RES_OK;
}

scpi_result_t SCPI_DACUseCalSet(scpi_t * context) {
    int useCal;

    if (!SCPI_ParamInt32(context, &useCal, TRUE)) {
        return SCPI_RES_ERR;
    }

    // TODO: Implement calibration preference setting

    return SCPI_RES_OK;
}

scpi_result_t SCPI_DACUseCalGet(scpi_t * context) {
    // TODO: Get calibration preference from NVM
    SCPI_ResultInt32(context, 0); // Default to factory calibration
    return SCPI_RES_OK;
}

scpi_result_t SCPI_DACUpdate(scpi_t * context) {
    // Update all DAC latches to reflect current values
    DAC7718_UpdateLatch(0);
    return SCPI_RES_OK;
}