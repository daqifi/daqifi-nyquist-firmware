/*! @file ADC.c 
 * 
 * This file implements the functions to manage the module ADC. 
 */

#include "ADC.h"

#include "ADC/AD7609.h"
#include "ADC/AD7173.h"
#include "ADC/MC12bADC.h"
#include "DIO.h"

#define UNUSED(x) (void)(x)

//! Pointer to the board configuration data structure to be set in initialization
static tBoardConfig *pBoardConfigADC;
//! Pointer to the board runtime configuration data structure, to be set 
// !in initialization
static tBoardRuntimeConfig *pBoardRuntimeConfigADC;
// Pointer to the BoardData data structure, to be set in initialization
static tBoardData* pBoardDataADC;

/*!
 * Retrieves the index of a module
 * @param[in] pModule Pointer to the module to search for
 * @return The index of the module in the array
 */
static uint8_t ADC_FindModuleIndex( const AInModule* pModule );

/*!
* Populates the sample array using data in the board config
* NOTE: This is intended to be non-blocking. Samples that are not ready will be skipped.
* @param[in] pSamples Pointer to the array to populate
* @param[in] module The module to retrieve samples from
* @param moduleRuntime[in] Runtime data for the given module
*/
static bool ADC_ReadSamples(                                                \
                            AInSampleArray* pSamples,                       \
                            const AInModule* module,                        \
                            AInModuleRuntimeConfig* moduleRuntime );

/**
* Updates the state of a single module
 * @param[in] moduleId ID of the module to use
 * @param[in] powerState Power state tp write in the module, @ref POWER_STATE
*/
static bool ADC_WriteModuleState(                                           \
                            size_t moduleId,                                \
                            POWER_STATE powerState);

/*! This function initialice the hardware neccesary for ADC
 * @param[in] pBoardAInModule Pointer to analog input module configuration
 * @param[in] pModuleChannels Pointer to module channels
 */
static bool ADC_InitHardware(                                               \
                            const AInModule* pBoardAInModule,               \
                            const AInArray* pModuleChannels );

/*!
 * Extracts channel information for the specified module
 * @param moduleChannels [out] Static channel data
 * @param moduleChannelRuntime [out] Runtime channel data
 * @param moduleId The module to search for
 */
static void GetModuleChannelRuntimeData(                                    \
                            AInArray* moduleChannels,                       \
                            AInRuntimeArray* moduleChannelRuntime,          \
                            uint8_t moduleId );

void ADC_Init(                                                              \
                    const tBoardConfig * pBoardConfigADCInit,               \
                    const tBoardRuntimeConfig* pBoardRuntimeConfigADCInit,  \
                    const tBoardData* pBoardDataADCInit)
{
    pBoardConfigADC = (tBoardConfig *)pBoardConfigADCInit;
    pBoardRuntimeConfigADC =                                                \
                        (tBoardRuntimeConfig *)pBoardRuntimeConfigADCInit;
    pBoardDataADC = (tBoardData *)pBoardDataADCInit;
}

bool ADC_WriteChannelStateAll( void )
{
    size_t i;
    bool result = true;
    AInArray moduleChannels;
    AInRuntimeArray moduleChannelRuntime;
    
    for (i=0; i<pBoardConfigADC->AInModules.Size; ++i)
    {
        // Get channels associated with the current module
        GetModuleChannelRuntimeData(                                        \
                        &moduleChannels,                                    \
                        &moduleChannelRuntime,                              \
                        i);
        
        // Delegate to the implementation
        switch(pBoardConfigADC->AInModules.Data[i].Type)
        {
        case AIn_MC12bADC:
            result &= MC12b_WriteStateAll(                                  \
                        &moduleChannels,                                    \
                        &moduleChannelRuntime);
            
            break;
        case AIn_AD7609:
            result &= AD7609_WriteStateAll(                                 \
                        &moduleChannels,                                    \
                        &moduleChannelRuntime);
            
            break;
        case AIn_AD7173:
            result &= AD7173_WriteStateAll(                                 \
                        &moduleChannels,                                    \
                        &moduleChannelRuntime);
            
            break;
        default:
            // Not implemented yet
            break;
        }
    }
    
    return result;
}

bool ADC_TriggerConversion(const AInModule* module)
{
    AInTaskState_t state; 
    uint32_t valueTMR = 0;   
    
    uint8_t moduleId = ADC_FindModuleIndex(module);
    
    POWER_STATE powerState = pBoardDataADC->PowerData.powerState;
    const AInModuleRuntimeConfig* moduleRuntime =                           \
                            &pBoardRuntimeConfigADC->AInModules.Data[moduleId];
    bool isPowered = (powerState > MICRO_ON);
    bool isEnabled = isPowered && moduleRuntime->IsEnabled;
    bool result = false;
    
    #if(DAQIFI_DIO_DEBUG == 1)
    {
        //TODO: DAQiFi For diagnostic purposes, setup DIO pin 2
        DIOChannels * pRunTimeDIOChannels = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIMECONFIG_DIO_CHANNELS);
        pRunTimeDIOChannels->Data[2].IsInput = false;
        pRunTimeDIOChannels->Data[2].IsReadOnly = false;
        pRunTimeDIOChannels->Data[2].Value = !pRunTimeDIOChannels->Data[2].Value;
        // Toggle DIO pin for diagnostic use
        DIO_WriteStateSingle(                                               \
                        &g_BoardConfig.DIOChannels.Data[2],                 \
                        &pRunTimeDIOChannels->Data[2]);
    }
    #endif

    if (!isEnabled)
    {
        return false;
    }
    
    state = AINTASK_CONVSTART; 
    BoardData_Set(                                                          \
                            BOARDATA_AIN_MODULE,                            \
                            moduleId,                                       \
                            &state );
    
    // Set streaming trigger timestamp
    valueTMR = DRV_TMR_CounterValueGet(                                     \
                        pBoardRuntimeConfigADC->StreamingConfig.TSTimerHandle);
    BoardData_Set(                                                          \
                        BOARDDATA_STREAMING_TIMESTAMP,                      \
                        0,                                                  \
                        &valueTMR ); 
    switch(module->Type)
    {
    case AIn_MC12bADC:
        result &= MC12b_TriggerConversion();
        break;
    case AIn_AD7609:
        result &= AD7609_TriggerConversion(&module->Config.AD7609);
        break;
    case AIn_AD7173:
        result &= AD7173_TriggerConversion(&module->Config.AD7173);
        break;
    default:
        // Not implemented yet
        break;
    }
    
    state = AINTASK_BUSY;
    BoardData_Set(                                                          \
                            BOARDATA_AIN_MODULE,                            \
                            moduleId,                                       \
                            &state );
    
    return result;
}

const AInModule* ADC_FindModule( AInType moduleType)
{
    size_t moduleIndex = 0;
    
    for (               moduleIndex = 0;                                    \
                        moduleIndex < pBoardConfigADC->AInModules.Size;     \
                        ++moduleIndex)
    {
        const AInModule* module =                                           \
                        &pBoardConfigADC->AInModules.Data[moduleIndex];
        if (module->Type == moduleType)
        {
            return module;
        }
    }
    
    return NULL;
}



void ADC_ConversionComplete(const AInModule* module)
{
    AInSampleArray samples;
    samples.Size = 0;
    int i=0;
    bool result = false;
    AInTaskState_t state;  
    uint8_t moduleId = 0; 
    
    AInModRuntimeArray * pRunTimeAInModules =                               \
                        &pBoardRuntimeConfigADC->AInModules;
    AInRuntimeArray * pRunTimeAInChannels =                                 \
                        &pBoardRuntimeConfigADC->AInChannels;
    
    StreamingRuntimeConfig * pRuntimeStreamConfig =                         \
                        &pBoardRuntimeConfigADC->StreamingConfig;
    
    AInSampleList *pAInSample = &pBoardDataADC->AInSamples;
    
    #if(DAQIFI_DIO_DEBUG == 1)
    {       
        DIORuntimeArray * pRunTimeDIOChannels = BoardConfig_Get(            \
                            BOARDRUNTIMECONFIG_DIO_CHANNELS,                \
                            0 );
        //TODO: DAQiFi For diagnostic purposes, setup DIO pin 0
        pRunTimeDIOChannels->Data[0].IsInput = false;
        pRunTimeDIOChannels->Data[0].IsReadOnly = false;
        pRunTimeDIOChannels->Data[0].Value =                                
                        \!pRunTimeDIOChannels->Data[0].Value;
    }
    #endif

    moduleId = ADC_FindModuleIndex( module );
    
    state = AINTASK_CONVCOMPLETE; 
    BoardData_Set(                                                          \
                            BOARDATA_AIN_MODULE,                            \
                            moduleId,                                       \
                            &state );
       
    // Read samples
    ADC_ReadSamples(&samples, module, &pRunTimeAInModules->Data[moduleId]);
    
    // Copy samples to the data list
    for (i=0; i<samples.Size; ++i)
    {
        size_t channelIndex = ADC_FindChannelIndex(samples.Data[i].Channel);
        AInChannel* channel = &pBoardConfigADC->AInChannels.Data[channelIndex];
        if (pRuntimeStreamConfig->IsEnabled &&                              \
            pRunTimeAInChannels->Data[channelIndex].IsEnabled)
        {   
            // If the current module is the internal ADC then check to see
            //if the channel is public
            if (moduleId == AIn_MC12bADC) 
            {
                if (channel->Config.MC12b.IsPublic)
                {
                    // If public, allow the value to be sent to streaming
                    AInSampleList_PushBack(pAInSample, &samples.Data[i]);  
                }
            }
            else
            {
                // If not the internal ADC, send to streaming
                result = AInSampleList_PushBack(pAInSample, &samples.Data[i]);  
                #if(DAQIFI_DIO_DEBUG == 1)
                {
                    DIORuntimeArray * pRunTimeDIOChannels = BoardConfig_Get(            \
                        BOARDRUNTIMECONFIG_DIO_CHANNELS,                    \
                        0 );
                    // Toggle DIO pin for diagnostic use
                    if (result) DIO_WriteStateSingle(                       \
                        &pRunTimeDIOChannels->Data[0],                      \
                        &pRunTimeDIOChannels->Data[0]);
                }
                #endif      
            }
        }
        BoardData_Set(                                                      \
                            BOARDDATA_AIN_LATEST,                           \
                            channelIndex,                                   \
                            &samples.Data[i] );
        
    }
    
    state = AINTASK_IDLE; 
    BoardData_Set(                                                          \
                            BOARDATA_AIN_MODULE,                            \
                            moduleId,                                   \
                            &state );
}

void ADC_Tasks( void )
{
    size_t moduleIndex = 0;
    POWER_STATE powerState = pBoardDataADC->PowerData.powerState;
    bool isPowered = (powerState > MICRO_ON);        
    AInArray moduleChannels;
    bool canInit, initialized;
    const AInModule* module =                                               \
                        &pBoardConfigADC->AInModules.Data[moduleIndex];
    const AInModuleRuntimeConfig* moduleRuntime =                           \
                        &pBoardRuntimeConfigADC->AInModules.Data[moduleIndex];
    AInRuntimeArray moduleChannelRuntime;
    
    for (           moduleIndex = 0;                                        \
                    moduleIndex < pBoardRuntimeConfigADC->AInModules.Size;  \
                    ++moduleIndex)
    {
        // Get channels associated with the current module

        GetModuleChannelRuntimeData(                                        \
                        &moduleChannels,                                    \
                        &moduleChannelRuntime,                              \
                        moduleIndex);
        
        // Check if the module is enabled - if not, skip it
        bool isEnabled = (module->Type == AIn_MC12bADC || isPowered) &&     \
                        moduleRuntime->IsEnabled;
        if(!isEnabled)
        {
            pBoardDataADC->AInState.Data[moduleIndex].AInTaskState =        \
                        AINTASK_DISABLED;
            continue;
        }
               
        if (pBoardDataADC->AInState.Data[moduleIndex].AInTaskState ==       \
                        AINTASK_INITIALIZING)
        {
            canInit = (module->Type == AIn_MC12bADC || isPowered);
            initialized = false;
            if (canInit)
            {
                if (ADC_InitHardware(module, &moduleChannels))
                {
                    if(module->Type == AIn_MC12bADC)
                    {
                        MC12b_WriteStateAll(                                \
                        &moduleChannels,                                    \
                        &moduleChannelRuntime);
                    }
                    if(module->Type == AIn_AD7173)
                    {
                        AD7173_WriteStateAll(                               \
                        &moduleChannels,
                        &moduleChannelRuntime);
                    }
                    ADC_WriteModuleState(moduleIndex, powerState);
                    
                    pBoardDataADC->AInState.Data[moduleIndex].AInTaskState = \
                        AINTASK_IDLE;
                    initialized = true;
                }
            }
            
            if (!initialized)
            {
                SYS_DEBUG_PRINT(                                            \
                        SYS_ERROR_FATAL,                                    \
                        "\nCannot Initialize ADC index=%d type=%d.\n",      \
                        moduleIndex,                                        \
                        module->Type);
            }
        }
    }
}

size_t ADC_FindChannelIndex(uint8_t channelId)
{
    size_t i=0;
    for (i=0; i<pBoardConfigADC->AInChannels.Size; ++i)
    {
        if (pBoardConfigADC->AInChannels.Data[i].ChannelId == channelId)
        {
            return i;
        }
    }
    return (size_t)-1;
}

bool ADC_IsDataValid(const AInSample* sample)
{
    return (sample->Timestamp > 0);
}

double ADC_ConvertToVoltage(const AInSample* sample)
{
    const AInRuntimeArray * pRunTimeAInChannels =                           \
                        &pBoardRuntimeConfigADC->AInChannels;
    
    size_t channelIndex = ADC_FindChannelIndex(sample->Channel);
    const AInChannel* channelConfig =                                       \
                        &pBoardConfigADC->AInChannels.Data[channelIndex];
    const AInRuntimeConfig* pRuntimeConfig =                                 \
                        &pRunTimeAInChannels->Data[channelIndex];
    const AInModule* moduleConfig =                                         \
                  &pBoardConfigADC->AInModules.Data[channelConfig->DataModule];

    switch(moduleConfig->Type)
    {
    case AIn_MC12bADC: 
        return MC12b_ConvertToVoltage(                                      \
                        &channelConfig->Config.MC12b,                       \
                        pRuntimeConfig,                                     \
                        sample);
    case AIn_AD7173:
        return AD7173_ConvertToVoltage(                                     \
                        pRuntimeConfig,                                     \
                        sample);
    case AIn_AD7609:
        return AD7609_ConvertToVoltage(                                     \
                        pRuntimeConfig,                                     \
                        sample);
    default:
        return 0.0;
    }
}

static uint8_t ADC_FindModuleIndex(const AInModule* pModule)
{
    size_t moduleIndex = 0;
    for (               moduleIndex = 0;                                    \
                        moduleIndex < pBoardConfigADC->AInModules.Size;     \
                        ++moduleIndex)
    {
        if (pModule == &pBoardConfigADC->AInModules.Data[moduleIndex])
        {
            return moduleIndex;
        }
    }
    
    return (uint8_t)-1;
}

static bool ADC_ReadSamples(                                                \
                            AInSampleArray* pSamples,                       \
                            const AInModule* module,                        \
                            AInModuleRuntimeConfig* moduleRuntime )
{
    uint8_t moduleId = ADC_FindModuleIndex( module );
    
    // Get channels associated with the current module
    AInArray moduleChannels;
    AInRuntimeArray moduleChannelRuntime;
    GetModuleChannelRuntimeData(                                            \
                            &moduleChannels,                                \
                            &moduleChannelRuntime,                          \
                            moduleId);
    
    bool result = true;
    
    switch(module->Type)
    {
    case AIn_MC12bADC:
        result &= MC12b_ReadSamples(                                        \
                        pSamples,                                           \
                        &moduleChannels,                                    \
                        &moduleChannelRuntime,                              \
                        pBoardDataADC->StreamTrigStamp);
        break;
    case AIn_AD7609:
        result &= AD7609_ReadSamples(                                       \
                        pSamples,                                           \
                        &moduleChannels,                                    \
                        &moduleChannelRuntime,                              \
                        pBoardDataADC->StreamTrigStamp);
        break;
    case AIn_AD7173:
        result &= AD7173_ReadSamples(                                       \
                        pSamples,                                           \
                        &moduleChannels,                                    \
                        &moduleChannelRuntime,                              \
                        pBoardDataADC->StreamTrigStamp);
        break;
    default:
        // Not implemented yet
        break;
    }
    
    return result;
}

bool ADC_WriteModuleState( size_t moduleId, POWER_STATE powerState)
{
    const AInModule* currentModule =                                        \
                        &pBoardConfigADC->AInModules.Data[moduleId];
    bool result = true;
    bool isPowered = (powerState> MICRO_ON);
    
    switch(currentModule->Type)
    {
    case AIn_MC12bADC:
        result &= MC12b_WriteModuleState();
        break;
    case AIn_AD7609:
        result &= AD7609_WriteModuleState(isPowered);
        break;
    case AIn_AD7173:
        result &= AD7173_WriteModuleState(isPowered);
        break;
    default:
        // Not implemented yet
        break;
    } 
    return result;
}

static bool ADC_InitHardware(                                               \
                            const AInModule* pBoardAInModule,               \
                            const AInArray* pModuleChannels )
{
    bool result = false;
     
    switch(pBoardAInModule->Type)
    {
    case AIn_MC12bADC:
        result = MC12b_InitHardware(                                        \
                &pBoardAInModule->Config.MC12b,                             \
                &pBoardRuntimeConfigADC->AInModules.Data[pBoardAInModule->Type]);
        break;
    case AIn_AD7609:
        result = AD7609_InitHardware( &pBoardAInModule->Config.AD7609 );
        break;
    case AIn_AD7173:
        result = AD7173_InitHardware(                                       \
                                    &pBoardAInModule->Config.AD7173,        \
                                    pModuleChannels );
        break;
    default:
        // Not implemented yet
        break;
    }
    
    return result;
}

static void GetModuleChannelRuntimeData(                                    \
                        AInArray* moduleChannels,                           \
                        AInRuntimeArray* moduleChannelRuntime,              \
                        uint8_t moduleId)
{
    moduleChannels->Size = 0;
    moduleChannelRuntime->Size = 0;
    size_t i;
    for (i=0; i<pBoardConfigADC->AInChannels.Size; ++i)
    {
        if (pBoardConfigADC->AInChannels.Data[i].DataModule != moduleId)
        {
            continue;
        }
        
        moduleChannels->Data[moduleChannels->Size] =                        \
                        pBoardConfigADC->AInChannels.Data[i];
        moduleChannels->Size += 1;
        
        moduleChannelRuntime->Data[moduleChannelRuntime->Size] =            \
                        pBoardRuntimeConfigADC->AInChannels.Data[i];
        moduleChannelRuntime->Size += 1;
    }
}