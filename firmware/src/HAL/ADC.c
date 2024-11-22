/*! @file ADC.c 
 * 
 * This file implements the functions to manage the module ADC. 
 */

#include "ADC.h"

//#include "ADC/AD7609.h"
//#include "ADC/AD7173.h"
#include "ADC/MC12bADC.h"
#include "DIO.h"

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

void ADC_EosInterruptTask(void) {
    const TickType_t xBlockTime = portMAX_DELAY;

    while (1) {
        ulTaskNotifyTake(pdFALSE, xBlockTime);
        AInSample sample;
        int i = 0;
        uint32_t adcval;
        uint32_t *valueTMR = (uint32_t*) BoardData_Get(BOARDDATA_STREAMING_TIMESTAMP, 0);
        
        //Read only the private ADC's as the public ADC's has their own interrupts
        for (i = 0; i < gpBoardConfig->AInChannels.Size; i++) {
            if (gpBoardConfig->AInChannels.Data[i].Config.MC12b.IsPublic != 1
                    && gpBoardRuntimeConfig->AInChannels.Data[i].IsEnabled == 1) {
                if (!MC12b_ReadResult(gpBoardConfig->AInChannels.Data[i].Config.MC12b.ChannelId, &adcval)) {
                    continue;
                }
                sample.Timestamp = *valueTMR;
                sample.Channel = i;
                sample.Value = adcval;
                BoardData_Set(
                        BOARDDATA_AIN_LATEST,
                        i,
                        &sample);
            }
        }


    }
}

void ADC_Init(
        const tBoardConfig * pBoardConfigADCInit,
        const tBoardRuntimeConfig* pBoardRuntimeConfigADCInit,
        const tBoardData* pBoardDataADCInit) {
    gpBoardConfig = (tBoardConfig *) pBoardConfigADCInit;
    gpBoardRuntimeConfig =
            (tBoardRuntimeConfig *) pBoardRuntimeConfigADCInit;
    gpBoardData = (tBoardData *) pBoardDataADCInit;
    xTaskCreate((TaskFunction_t) ADC_EosInterruptTask,
            "ADC Interrupt",
            2048, NULL, 8, &gADCInterruptHandle);
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
            default:
                // Not implemented yet
                break;
        }
    }

    return result;
}

bool ADC_TriggerConversion(const AInModule* module, MC12b_adcType_t adcChannelType) {
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
            BOARDATA_AIN_MODULE,
            moduleId,
            &state);

    switch (module->Type) {
        case AIn_MC12bADC:
            result &= MC12b_TriggerConversion(&gpBoardRuntimeConfig->AInChannels, &gpBoardConfig->AInChannels, adcChannelType);
            break;
        default:
            // Not implemented yet
            break;
    }

    state = AINTASK_BUSY;
    BoardData_Set(
            BOARDATA_AIN_MODULE,
            moduleId,
            &state);

    return result;
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
        // Get channels associated with the current module

        GetModuleChannelRuntimeData(
                &moduleChannels,
                &moduleChannelRuntime,
                moduleIndex);

        // Check if the module is enabled - if not, skip it
        bool isEnabled = (module->Type == AIn_MC12bADC || isPowered) &&
                moduleRuntime->IsEnabled;
        if (!isEnabled) {
            gpBoardData->AInState.Data[moduleIndex].AInTaskState =
                    AINTASK_DISABLED;
            continue;
        }

        if (gpBoardData->AInState.Data[moduleIndex].AInTaskState ==
                AINTASK_INITIALIZING) {
            canInit = (module->Type == AIn_MC12bADC || isPowered);
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
            sample.Channel = i;
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

void ADC_EOSInterruptCB(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(gADCInterruptHandle, &xHigherPriorityTaskWoken);
    portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
}