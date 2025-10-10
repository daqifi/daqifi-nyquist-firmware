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

// Static DAC instance ID (returned from DAC7718_NewConfig)
static uint8_t dacInstanceId = 0xFF; // 0xFF = uninitialized

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
    
    // Initialize DAC7718 hardware configuration
    dacConfig.CS_Pin = GPIO_PIN_RK0;     // CS on RK0
    dacConfig.RST_Pin = GPIO_PIN_RJ13;   // CLR/RST on RJ13

    // Create DAC configuration and get instance ID
    dacInstanceId = DAC7718_NewConfig(&dacConfig);
    if (dacInstanceId == 0xFF) {
        LOG_E("DAC_EnsureHardwareInitialized: Failed to allocate DAC configuration");
        return false;
    }

    // Initialize DAC hardware with the instance ID
    DAC7718_Init(dacInstanceId, 0);

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
// MaxVoltage is a software limit for clamping, HardwareFullScale determines actual output
// Resolution from board config supports different DAC variants (12-bit=4096, 14-bit=16384, 16-bit=65536)
static uint32_t DAC_VoltageToCounts(double voltage, const AOutModule* module) {
    // Access config directly to avoid struct alignment issues
    const double minVoltage = module->Config.DAC7718.MinVoltage;
    const double maxVoltage = module->Config.DAC7718.MaxVoltage;
    const double hardwareFullScale = module->Config.DAC7718.HardwareFullScale;
    const uint16_t resolution = module->Config.DAC7718.Resolution;

    // Validate configuration - prevent division by zero and invalid resolution
    if (hardwareFullScale <= 0.0) {
        LOG_E("DAC_VoltageToCounts: Invalid hardwareFullScale (%.2f), returning 0", hardwareFullScale);
        return 0;
    }
    if (resolution == 0) {
        LOG_E("DAC_VoltageToCounts: Invalid resolution (0), returning 0");
        return 0;
    }

    // Clamp to software-configured limits
    if (voltage < minVoltage) voltage = minVoltage;
    if (voltage > maxVoltage) voltage = maxVoltage;

    // Convert using hardware full-scale voltage from config
    // This ensures correct output: 4V command -> 1638 counts -> 4V output
    double normalizedVoltage = (voltage - minVoltage) / hardwareFullScale;
    double rawCounts = normalizedVoltage * (double)(resolution - 1);

    // Clamp to valid DAC range [0, resolution-1] to prevent overflow
    // This guards against edge cases and ensures compatibility with different DAC resolutions
    const uint32_t maxCounts = (uint32_t)(resolution - 1);
    if (rawCounts < 0.0) {
        rawCounts = 0.0;
    }
    if (rawCounts > (double)maxCounts) {
        rawCounts = (double)maxCounts;
    }

    // Round to nearest integer
    uint32_t counts = (uint32_t)(rawCounts + 0.5);

    return counts;
}

scpi_result_t SCPI_DACVoltageSet(scpi_t * context) {
    int channel;
    double voltage;
    AOutArray* pBoardConfigAOutChannels = BoardConfig_Get(BOARDCONFIG_AOUT_CHANNELS, 0);
    AOutModule* pDACModule = BoardConfig_Get(BOARDCONFIG_AOUT_MODULE, 0);

    if (pBoardConfigAOutChannels == NULL || pDACModule == NULL) {
        LOG_E("SCPI_DACVoltageSet: No DAC channels configured");
        return SCPI_RES_ERR;
    }

    // Ensure DAC hardware is initialized (lazy initialization when power is up)
    if (!DAC_EnsureHardwareInitialized()) {
        SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
        return SCPI_RES_ERR;
    }

    // Try to parse first parameter as double (works for both int and double)
    if (!SCPI_ParamDouble(context, &voltage, TRUE)) {
        return SCPI_RES_ERR;
    }

    // Try to parse second parameter as double (voltage)
    double voltage2;
    if (SCPI_ParamDouble(context, &voltage2, FALSE)) {
        // Two parameters: first is channel (convert to int), second is voltage
        channel = (int)voltage;
        voltage = voltage2;

        size_t index = DAC_FindChannelIndex((uint8_t)channel);
        if (index >= pBoardConfigAOutChannels->Size) {
            return SCPI_RES_ERR;
        }

        // Convert voltage to DAC counts using configuration
        uint32_t counts = DAC_VoltageToCounts(voltage, pDACModule);

        // Get hardware channel number from board configuration
        uint8_t hwChannel = pBoardConfigAOutChannels->Data[index].Config.DAC7718.ChannelNumber;
        uint8_t dacRegister = 8 + hwChannel;  // DAC-0 = register 8
        DAC7718_ReadWriteReg(dacInstanceId, 0, dacRegister, counts);
        DAC7718_UpdateLatch(dacInstanceId);

        // Store commanded voltage in BoardData for readback
        AOutSample sample = {.Channel = (uint8_t)channel, .Voltage = voltage};
        BoardData_Set(BOARDDATA_AOUT_LATEST, index, &sample);

    } else {
        // One parameter: voltage for all channels
        uint32_t counts = DAC_VoltageToCounts(voltage, pDACModule);

        for (size_t i = 0; i < pBoardConfigAOutChannels->Size; i++) {
            uint8_t hwChannel = pBoardConfigAOutChannels->Data[i].Config.DAC7718.ChannelNumber;
            uint8_t dacRegister = 8 + hwChannel;  // DAC-0 = register 8
            DAC7718_ReadWriteReg(dacInstanceId, 0, dacRegister, counts);

            // Store commanded voltage in BoardData for readback
            uint8_t channelId = pBoardConfigAOutChannels->Data[i].DaqifiDacChannelId;
            AOutSample sample = {.Channel = channelId, .Voltage = voltage};
            BoardData_Set(BOARDDATA_AOUT_LATEST, i, &sample);
        }

        // Update all DAC latches
        DAC7718_UpdateLatch(dacInstanceId);
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

    // Note: DAC7718 does not support hardware readback
    // Return last commanded voltage from BoardData
    if (SCPI_ParamInt32(context, &channel, FALSE)) {
        // Get single channel
        size_t index = DAC_FindChannelIndex((uint8_t)channel);
        if (index >= pBoardConfigAOutChannels->Size) {
            LOG_E("SCPI_DACVoltageGet: Invalid channel %d", channel);
            return SCPI_RES_ERR;
        }

        // Read last commanded voltage from BoardData
        AOutSample* pSample = (AOutSample*)BoardData_Get(BOARDDATA_AOUT_LATEST, index);
        if (pSample != NULL) {
            SCPI_ResultDouble(context, pSample->Voltage);
        } else {
            SCPI_ResultDouble(context, 0.0);
        }
    } else {
        // Get all channels
        for (size_t i = 0; i < pBoardConfigAOutChannels->Size; i++) {
            AOutSample* pSample = (AOutSample*)BoardData_Get(BOARDDATA_AOUT_LATEST, i);
            if (pSample != NULL) {
                SCPI_ResultDouble(context, pSample->Voltage);
            } else {
                SCPI_ResultDouble(context, 0.0);
            }
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
    DAC7718_UpdateLatch(dacInstanceId);
    return SCPI_RES_OK;
}