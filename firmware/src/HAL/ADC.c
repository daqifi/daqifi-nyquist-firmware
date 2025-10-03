/*! @file ADC.c 
 * 
 * This file implements the functions to manage the module ADC. 
 */

#include "ADC.h"

#include "ADC/AD7609.h"
//#include "ADC/AD7173.h"
#include "ADC/MC12bADC.h"
#include "DIO.h"
#include "Util/Logger.h"

#define UNUSED(x) (void)(x)

//! Pointer to the board configuration data structure to be set in initialization
static tBoardConfig *gpBoardConfig;
//! Pointer to the board runtime configuration data structure, to be set 
// !in initialization
static tBoardRuntimeConfig *gpBoardRuntimeConfig;
// Pointer to the BoardData data structure, to be set in initialization
static tBoardData* gpBoardData;
static TaskHandle_t gADCInterruptHandle;
/*!
 * Retrieves the index of a module
 * @param[in] pModule Pointer to the module to search for
 * @return The index of the module in the array
 */
static uint8_t ADC_FindModuleIndex(const AInModule* pModule);



/**
 * Updates the state of a single module
 * @param[in] moduleId ID of the module to use
 * @param[in] powerState Power state tp write in the module, @ref POWER_STATE
 */
static bool ADC_WriteModuleState(
        size_t moduleId,
        POWER_STATE powerState);

/*! This function initialice the hardware neccesary for ADC
 * @param[in] pBoardAInModule Pointer to analog input module configuration
 * @param[in] pModuleChannels Pointer to module channels
 */
static bool ADC_InitHardware(
        AInModule* pBoardAInModule,
        AInArray* pModuleChannels);

/*!
 * Extracts channel information for the specified module
 * @param moduleChannels [out] Static channel data
 * @param moduleChannelRuntime [out] Runtime channel data
 * @param moduleId The module to search for
 */
static void GetModuleChannelRuntimeData(
        AInArray* moduleChannels,
        AInRuntimeArray* moduleChannelRuntime,
        uint8_t moduleId);

/*!
 * Handles AD7609 data acquisition from deferred interrupt task
 * Called when BSY pin interrupt signals conversion complete
 */
void ADC_HandleAD7609Interrupt(void) {
    AInSample sample;
    AInSampleArray samples;
    uint32_t *valueTMR = (uint32_t*) BoardData_Get(BOARDDATA_STREAMING_TIMESTAMP, 0);

    samples.Size = 8;

    // Read all AD7609 channels (polls BSY internally for safety)
    if (AD7609_ReadSamples(&samples, &gpBoardConfig->AInChannels,
                          &gpBoardRuntimeConfig->AInChannels, *valueTMR)) {

        // Store each sample to BoardData at its array index
        for (size_t s = 0; s < samples.Size; s++) {
            // Find the array index for this channel
            uint8_t channelId = samples.Data[s].Channel;
            for (size_t i = 0; i < gpBoardConfig->AInChannels.Size; i++) {
                if (gpBoardConfig->AInChannels.Data[i].Type == AIn_AD7609 &&
                    gpBoardConfig->AInChannels.Data[i].DaqifiAdcChannelId == channelId) {

                    sample.Timestamp = samples.Data[s].Timestamp;
                    sample.Channel = channelId;
                    sample.Value = samples.Data[s].Value;
                    BoardData_Set(BOARDDATA_AIN_LATEST, i, &sample);
                    break;
                }
            }
        }
    }
}

// Internal PIC32 ADC End-of-Scan interrupt task (MC12bADC only)
void MC12bADC_EosInterruptTask(void) {
    const TickType_t xBlockTime = portMAX_DELAY;

    while (1) {
        ulTaskNotifyTake(pdFALSE, xBlockTime);
        AInSample sample;
        int i = 0;
        uint32_t adcval;
        uint32_t *valueTMR = (uint32_t*) BoardData_Get(BOARDDATA_STREAMING_TIMESTAMP, 0);

        // Read only the private internal ADC channels (MC12bADC)
        // Note: AD7609 now uses interrupt-based reading via its own deferred task
        for (i = 0; i < gpBoardConfig->AInChannels.Size; i++) {
            if (gpBoardConfig->AInChannels.Data[i].Type == AIn_MC12bADC &&
                gpBoardConfig->AInChannels.Data[i].Config.MC12b.IsPublic != 1
                    && gpBoardRuntimeConfig->AInChannels.Data[i].IsEnabled == 1) {
                if (!MC12b_ReadResult(gpBoardConfig->AInChannels.Data[i].Config.MC12b.ChannelId, &adcval)) {
                    continue;
                }
                sample.Timestamp = *valueTMR;
                sample.Channel = gpBoardConfig->AInChannels.Data[i].DaqifiAdcChannelId;
                sample.Value = adcval;
                BoardData_Set(
                        BOARDDATA_AIN_LATEST,
                        i,
                        &sample);
            }
        }
    }
}

// Legacy wrapper for backward compatibility
void ADC_EosInterruptTask(void) {
    MC12bADC_EosInterruptTask();
}

void ADC_Init(
        const tBoardConfig * pBoardConfigADCInit,
        const tBoardRuntimeConfig* pBoardRuntimeConfigADCInit,
        const tBoardData* pBoardDataADCInit) {
    gpBoardConfig = (tBoardConfig *) pBoardConfigADCInit;
    gpBoardRuntimeConfig =
            (tBoardRuntimeConfig *) pBoardRuntimeConfigADCInit;
    gpBoardData = (tBoardData *) pBoardDataADCInit;
    BaseType_t result = xTaskCreate((TaskFunction_t) MC12bADC_EosInterruptTask,
            "MC12bADC EOS",
            2048, NULL, 8, &gADCInterruptHandle); // Priority 8 for real-time ADC response
    if (result != pdPASS) {
        LOG_E("FATAL: Failed to create MC12bADC_EosInterruptTask (2048 bytes)\r\n");
    }

    // Register ADC EOS interrupt callback using Harmony's callback mechanism
    ADCHS_EOSCallbackRegister(ADC_EOSInterruptCB, 0);

    // Note: AD7609 initialization happens later when 10V rail is powered up
}

bool ADC_WriteChannelStateAll(void) {
    size_t i;
    bool result = true;
    AInArray moduleChannels;
    AInRuntimeArray moduleChannelRuntime;

    for (i = 0; i < gpBoardConfig->AInModules.Size; ++i) {
        // Get channels associated with the current module
        GetModuleChannelRuntimeData(
                &moduleChannels,
                &moduleChannelRuntime,
                i);

        // Delegate to the implementation
        switch (gpBoardConfig->AInModules.Data[i].Type) {
            case AIn_MC12bADC:
                result &= MC12b_WriteStateAll(
                        &moduleChannels,
                        &moduleChannelRuntime);
                break;
            case AIn_AD7609:
                result &= AD7609_WriteStateAll(&moduleChannels, &moduleChannelRuntime);
                break;
            default:
                // Not implemented yet
                break;
        }
    }

    return result;
}

// Generic ADC trigger dispatcher - delegates to specific ADC drivers
bool ADC_TriggerConversion_Generic(const AInModule* module, MC12b_adcType_t adcChannelType) {
    AInTaskState_t state;
    uint8_t moduleId = ADC_FindModuleIndex(module);
    POWER_STATE powerState = gpBoardData->PowerData.powerState;
    const AInModuleRuntimeConfig* moduleRuntime =
            &gpBoardRuntimeConfig->AInModules.Data[moduleId];

    bool isPowered = (powerState == POWERED_UP ||                    
                 powerState == POWERED_UP_EXT_DOWN);
    bool isEnabled = isPowered && moduleRuntime->IsEnabled;
    bool result = false;

    if (!isEnabled) {
        return false;
    }

    state = AINTASK_CONVSTART;
    BoardData_Set(
            BOARDDATA_AIN_MODULE,
            moduleId,
            &state);

    switch (module->Type) {
        case AIn_MC12bADC:
            result &= MC12b_TriggerConversion(&gpBoardRuntimeConfig->AInChannels, &gpBoardConfig->AInChannels, adcChannelType);
            break;
        case AIn_AD7609:
            result &= AD7609_TriggerConversion(&module->Config.AD7609);
            break;
        default:
            // Not implemented yet
            break;
    }

    state = AINTASK_BUSY;
    BoardData_Set(
            BOARDDATA_AIN_MODULE,
            moduleId,
            &state);

    return result;
}

// Legacy wrapper for backward compatibility
bool ADC_TriggerConversion(const AInModule* module, MC12b_adcType_t adcChannelType) {
    return ADC_TriggerConversion_Generic(module, adcChannelType);
}

// Specific trigger functions for clarity
bool MC12bADC_TriggerConversion_Deferred(const AInModule* module) {
    return ADC_TriggerConversion_Generic(module, MC12B_ADC_TYPE_ALL);
}

bool AD7609_TriggerConversion_Deferred(const AInModule* module) {
    return ADC_TriggerConversion_Generic(module, MC12B_ADC_TYPE_ALL); // adcChannelType not used for AD7609
}

const AInModule* ADC_FindModule(AInType moduleType) {
    size_t moduleIndex = 0;

    for (moduleIndex = 0;
            moduleIndex < gpBoardConfig->AInModules.Size;
            ++moduleIndex) {
        const AInModule* module =
                &gpBoardConfig->AInModules.Data[moduleIndex];
        if (module->Type == moduleType) {
            return module;
        }
    }

    return NULL;
}

void ADC_Tasks(void) {
    size_t moduleIndex = 0;
    POWER_STATE powerState = gpBoardData->PowerData.powerState;
    bool isPowered = (powerState == POWERED_UP ||                    
             powerState == POWERED_UP_EXT_DOWN);
    AInArray moduleChannels;
    bool canInit, initialized;
    AInModule* module = &gpBoardConfig->AInModules.Data[moduleIndex];
    const AInModuleRuntimeConfig* moduleRuntime =
            &gpBoardRuntimeConfig->AInModules.Data[moduleIndex];
    AInRuntimeArray moduleChannelRuntime;

    for (moduleIndex = 0;
            moduleIndex < gpBoardRuntimeConfig->AInModules.Size;
            ++moduleIndex) {
        // Update module pointer for current iteration
        module = &gpBoardConfig->AInModules.Data[moduleIndex];
        moduleRuntime = &gpBoardRuntimeConfig->AInModules.Data[moduleIndex];
        
        // Get channels associated with the current module
        GetModuleChannelRuntimeData(
                &moduleChannels,
                &moduleChannelRuntime,
                moduleIndex);

        // Check if the module is enabled - if not, skip it
        bool isEnabled = (module->Type == AIn_MC12bADC || module->Type == AIn_AD7609 || isPowered) &&
                moduleRuntime->IsEnabled;
        if (!isEnabled) {
            gpBoardData->AInState.Data[moduleIndex].AInTaskState =
                    AINTASK_DISABLED;
            continue;
        }

        if (gpBoardData->AInState.Data[moduleIndex].AInTaskState ==
                AINTASK_INITIALIZING) {
            // MC12bADC can init anytime, AD7609 requires isPowered (needs 10V rail)
            canInit = (module->Type == AIn_MC12bADC) || (module->Type == AIn_AD7609 && isPowered);
            initialized = false;

            if (canInit) {
                if (ADC_InitHardware(module, &moduleChannels)) {
                    if (module->Type == AIn_MC12bADC) {
                        MC12b_WriteStateAll(
                                &moduleChannels,
                                &moduleChannelRuntime);
                    }
                    ADC_WriteModuleState(moduleIndex, powerState);
                    gpBoardData->AInState.Data[moduleIndex].AInTaskState =
                            AINTASK_IDLE;
                    initialized = true;
                }
            }

            if (!initialized) {
                SYS_DEBUG_PRINT(
                        SYS_ERROR_FATAL,
                        "\nCannot Initialize ADC index=%d type=%d.\n",
                        moduleIndex,
                        module->Type);
            }
        }
    }
    if (!gpBoardRuntimeConfig->StreamingConfig.IsEnabled) {
        for (int i = 0; i < gpBoardRuntimeConfig->AInModules.Size; i++)
            ADC_TriggerConversion(&gpBoardConfig->AInModules.Data[i], MC12B_ADC_TYPE_ALL);
    }
}

size_t ADC_FindChannelIndex(uint8_t channelId) {
    size_t i = 0;
    for (i = 0; i < gpBoardConfig->AInChannels.Size; ++i) {
        if (gpBoardConfig->AInChannels.Data[i].DaqifiAdcChannelId == channelId) {
            return i;
        }
    }
    return (size_t) - 1;
}

bool ADC_IsDataValid(const AInSample* sample) {
    return (sample->Timestamp > 0);
}

double ADC_ConvertToVoltage(const AInSample* sample) {
    const AInRuntimeArray * pRunTimeAInChannels =
            &gpBoardRuntimeConfig->AInChannels;

    size_t channelIndex = ADC_FindChannelIndex(sample->Channel);
    const AInChannel* channelConfig =
            &gpBoardConfig->AInChannels.Data[channelIndex];
    const AInRuntimeConfig* pRuntimeConfig =
            &pRunTimeAInChannels->Data[channelIndex];



    switch (channelConfig->Type) {
        case AIn_MC12bADC:
            return MC12b_ConvertToVoltage(
                    &channelConfig->Config.MC12b,
                    pRuntimeConfig,
                    sample);
        case AIn_AD7609:
            return AD7609_ConvertToVoltage(pRuntimeConfig, sample);
        default:
            return 0.0;
    }
}

static uint8_t ADC_FindModuleIndex(const AInModule* pModule) {
    size_t moduleIndex = 0;
    for (moduleIndex = 0;
            moduleIndex < gpBoardConfig->AInModules.Size;
            ++moduleIndex) {
        if (pModule == &gpBoardConfig->AInModules.Data[moduleIndex]) {
            return moduleIndex;
        }
    }

    return (uint8_t) - 1;
}

bool ADC_ReadADCSampleFromISR(uint32_t value, uint8_t bufferIndex) {

    AInSample sample;
    bool status = false;
    int i = 0;
    sample.Value = value;
    uint32_t *valueTMR = (uint32_t*) BoardData_Get(BOARDDATA_STREAMING_TIMESTAMP, 0);
    for (i = 0; i < gpBoardConfig->AInChannels.Size; i++) {
        if (gpBoardConfig->AInChannels.Data[i].Config.MC12b.ChannelId == bufferIndex
                && gpBoardRuntimeConfig->AInChannels.Data[i].IsEnabled == 1) {

            sample.Timestamp = *valueTMR;
            sample.Channel =gpBoardConfig->AInChannels.Data[i].DaqifiAdcChannelId ;
            BoardData_Set(
                    BOARDDATA_AIN_LATEST,
                    i,
                    &sample);

            status = true;
            break;

        }
    }
    
    return status;
}

bool ADC_WriteModuleState(size_t moduleId, POWER_STATE powerState) {
    const AInModule* currentModule =
            &gpBoardConfig->AInModules.Data[moduleId];
    bool result = true;

    switch (currentModule->Type) {
        case AIn_MC12bADC:
            result &= MC12b_WriteModuleState();
            break;
        case AIn_AD7609:
            result &= AD7609_WriteModuleState(powerState == POWERED_UP || powerState == POWERED_UP_EXT_DOWN);
            break;
        default:
            // Not implemented yet
            break;
    }
    return result;
}

static bool ADC_InitHardware(
        AInModule* pBoardAInModule,
        AInArray* pModuleChannels) {
    bool result = false;

    switch (pBoardAInModule->Type) {
        case AIn_MC12bADC:
            result = MC12b_InitHardware(
                    &pBoardAInModule->Config.MC12b,
                    &gpBoardRuntimeConfig->AInModules.Data[pBoardAInModule->Type]);
            break;
        case AIn_AD7609:
            result = AD7609_InitHardware(&pBoardAInModule->Config.AD7609);
            break;
        default:
            // Not implemented yet
            break;
    }

    return result;
}

static void GetModuleChannelRuntimeData(
        AInArray* moduleChannels,
        AInRuntimeArray* moduleChannelRuntime,
        uint8_t moduleId) {
    moduleChannels->Size = 0;
    moduleChannelRuntime->Size = 0;
    size_t i;
    for (i = 0; i < gpBoardConfig->AInChannels.Size; ++i) {

        moduleChannels->Data[moduleChannels->Size] =
                gpBoardConfig->AInChannels.Data[i];
        moduleChannels->Size += 1;

        moduleChannelRuntime->Data[moduleChannelRuntime->Size] =
                gpBoardRuntimeConfig->AInChannels.Data[i];
        moduleChannelRuntime->Size += 1;
    }
}

void ADC_EOSInterruptCB(uintptr_t context) {
    (void)context; // Unused
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(gADCInterruptHandle, &xHigherPriorityTaskWoken);
    portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
}