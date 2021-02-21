#include "ADC.h"

#include "ADC/AD7609.h"
#include "ADC/AD7173.h"
#include "ADC/MC12bADC.h"
#include "DIO.h"

#define UNUSED(x) (void)(x)

//! Pointer to the board configuration data structure to be set in initialization
static BoardConfig *pBoardConfig;
//! Pointer to the board runtime configuration data structure, to be set in initialization
static BoardRuntimeConfig *pBoardRuntimeConfig;
// Pointer to the BoardData data structure, to be set in initialization
static BoardData* pBoardData;

/**
 * Extracts channel information for the specified module
 * @param moduleChannels [out] Static channel data
 * @param moduleId The module to search for
 * @param boardConfig The board to extract information from
 */
//Function unused
//static void GetModuleChannelData(AInArray* moduleChannels, uint8_t moduleId, const BoardConfig* boardConfig)
//{
//    moduleChannels->Size = 0;
//    size_t i;
//    for (i=0; i<boardConfig->AInChannels.Size; ++i)
//    {
//        if (boardConfig->AInChannels.Data[i].DataModule != moduleId)
//        {
//            continue;
//        }
//        
//        moduleChannels->Data[moduleChannels->Size] = boardConfig->AInChannels.Data[i];
//        moduleChannels->Size += 1;
//    }
//}

//static bool ADC_IsDataValid(const AInSample* sample);

static uint8_t ADC_FindModuleIndex( const AInModule* module );

static bool ADC_ReadSamples( \
                            AInSampleArray* samples, \
                            const AInModule* module, \
                            AInModuleRuntimeConfig* moduleRuntime );

static bool ADC_WriteModuleState( \
                            size_t moduleId, \
                            POWER_STATE powerState);

static bool ADC_InitHardware( \
                            const AInModule* boardConfig, \
                            const AInArray* moduleChannels );

static void GetModuleChannelRuntimeData( \
                            AInArray* moduleChannels, \
                            AInRuntimeArray* moduleChannelRuntime, \
                            uint8_t moduleId );


void ADC_Init( \
                    const BoardConfig *pInitBoardConfig, \
                    const BoardRuntimeConfig *pInitBoardRuntimeConfig, \
                    const BoardData *pInitBoardData ){
    
    pBoardConfig = (BoardConfig *)pInitBoardConfig;
    pBoardRuntimeConfig = (BoardRuntimeConfig *)pInitBoardRuntimeConfig;
    pBoardData = (BoardData *)pInitBoardData;
}

bool ADC_WriteChannelStateAll( void )
{
    size_t i;
    bool result = true;
    for (i=0; i< pBoardConfig->AInModules.Size; ++i)
    {
        // Get channels associated with the current module
        AInArray moduleChannels;
        AInRuntimeArray moduleChannelRuntime;
        GetModuleChannelRuntimeData( &moduleChannels, &moduleChannelRuntime, i );
        
        // Delegate to the implementation
        switch( pBoardConfig->AInModules.Data[i].Type)
        {
        case AIn_MC12bADC:
            result &= MC12b_WriteStateAll(&pBoardConfig->AInModules.Data[i].Config.MC12b,
                &pBoardRuntimeConfig->AInModules.Data[i],
                &moduleChannels,
                &moduleChannelRuntime);
            
            break;
        case AIn_AD7609:
            result &= AD7609_WriteStateAll(&pBoardConfig->AInModules.Data[i].Config.AD7609,
                &pBoardRuntimeConfig->AInModules.Data[i],
                &moduleChannels,
                &moduleChannelRuntime);
            
            break;
        case AIn_AD7173:
            result &= AD7173_WriteStateAll(&pBoardConfig->AInModules.Data[i].Config.AD7173,
                &pBoardRuntimeConfig->AInModules.Data[i],
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

//bool ADC_WriteChannelStateSingle(const BoardConfig* boardConfig, BoardRuntimeConfig* runtimeConfig, size_t channelId)
//{
//    const AInChannel* channel = &boardConfig->AInChannels.Data[channelId];
//    AInRuntimeConfig* channelRuntime = &runtimeConfig->AInChannels.Data[channelId];
//    
//    const AInModule* module = &boardConfig->AInModules.Data[channel->DataModule];
//    AInModuleRuntimeConfig* moduleRuntime = &runtimeConfig->AInModules.Data[channel->DataModule];
//    
//    bool result = true;
//    switch(module->Type)
//    {
//    case AIn_MC12bADC:
//        result &= MC12b_WriteStateSingle(&module->Config.MC12b,
//            moduleRuntime,
//            &channel->Config.MC12b,
//            channelRuntime);
//        break;
//    case AIn_AD7609:
//        result &= AD7609_WriteStateSingle(&module->Config.AD7609,
//            moduleRuntime,
//            &channel->Config.AD7609,
//            channelRuntime);
//        break;
//    case AIn_AD7173:
//        result &= AD7173_WriteStateSingle(&module->Config.AD7173,
//            moduleRuntime,
//            &channel->Config.AD7173,
//            channelRuntime);
//        break;
//    default:
//        // Not implemented yet
//        break;
//    }
//    
//    return result;
//}

bool ADC_TriggerConversion( const AInModule* module )
{
    #if(DAQIFI_DIO_DEBUG == 1)
    {
        //TODO: DAQiFi For diagnostic purposes, setup DIO pin 2
        g_BoardRuntimeConfig.DIOChannels.Data[2].IsInput = false;
        g_BoardRuntimeConfig.DIOChannels.Data[2].IsReadOnly = false;
        g_BoardRuntimeConfig.DIOChannels.Data[2].Value = !g_BoardRuntimeConfig.DIOChannels.Data[2].Value;
        // Toggle DIO pin for diagnostic use
        DIO_WriteStateSingle(&g_BoardConfig.DIOChannels.Data[2], &g_BoardRuntimeConfig.DIOChannels.Data[2]);
    }
    #endif
    
    uint8_t moduleId = ADC_FindModuleIndex( module );
    
    POWER_STATE powerState = g_BoardData.PowerData.powerState;
    const AInModuleRuntimeConfig* moduleRuntime = &g_BoardRuntimeConfig.AInModules.Data[moduleId];
    bool isPowered = (powerState > MICRO_ON);
    bool isEnabled = isPowered && moduleRuntime->IsEnabled;
    if (!isEnabled)
    {
        return false;
    }
    
    g_BoardData.AInState.Data[moduleId].AInTaskState = AINTASK_CONVSTART;
    g_BoardData.StreamTrigStamp = DRV_TMR_CounterValueGet(g_BoardRuntimeConfig.StreamingConfig.TSTimerHandle);  // Set streaming trigger timestamp
    
    bool result = false;
    
    switch(module->Type)
    {
    case AIn_MC12bADC:
        result &= MC12b_TriggerConversion(&module->Config.MC12b);
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
    
    g_BoardData.AInState.Data[moduleId].AInTaskState = AINTASK_BUSY;
    
    return result;
}

const AInModule* ADC_FindModule( AInType moduleType )
{
    size_t moduleIndex;
    
    for( \
            moduleIndex = 0; \
            moduleIndex < pBoardConfig->AInModules.Size; \
            ++moduleIndex ){
        
        const AInModule* module = &pBoardConfig->AInModules.Data[ moduleIndex ];
        if( module->Type == moduleType ){
            return module;
        }
    }
    
    return NULL;
}

void ADC_ConversionComplete( const AInModule* module )
{
    
    AInSampleArray samples;
    samples.Size = 0;
    int i=0;
    
    #if(DAQIFI_DIO_DEBUG == 1)
    {       
        //TODO: DAQiFi For diagnostic purposes, setup DIO pin 0
        g_BoardRuntimeConfig.DIOChannels.Data[0].IsInput = false;
        g_BoardRuntimeConfig.DIOChannels.Data[0].IsReadOnly = false;
        g_BoardRuntimeConfig.DIOChannels.Data[0].Value = !g_BoardRuntimeConfig.DIOChannels.Data[0].Value;
    }
    #endif

    
    uint8_t moduleId = ADC_FindModuleIndex( module);
    
    g_BoardData.AInState.Data[moduleId].AInTaskState = AINTASK_CONVCOMPLETE;
       
    // Read samples
    ADC_ReadSamples(&samples, module, &g_BoardRuntimeConfig.AInModules.Data[moduleId]);
   
    if( samples.Size > MAX_AIN_CHANNEL ){
        samples.Size = MAX_AIN_CHANNEL;
    }
    // Copy samples to the data list
    for (i=0; i<samples.Size; i++)
    {
        size_t channelIndex = ADC_FindChannelIndex( samples.Data[i].Channel);
        if(channelIndex == (size_t)-1 ){
            break;
        }
        AInChannel* channel = &g_BoardConfig.AInChannels.Data[channelIndex];
        if (g_BoardRuntimeConfig.StreamingConfig.IsEnabled && g_BoardRuntimeConfig.AInChannels.Data[channelIndex].IsEnabled)
        {
            if (moduleId == AIn_MC12bADC) // If the current module is the internal ADC then check to see if the channel is public
            {
                if (channel->Config.MC12b.IsPublic) AInSampleList_PushBack(&g_BoardData.AInSamples, &samples.Data[i]);  // If public, allow the value to be sent to streaming
            }
            else
            {
                AInSampleList_PushBack(&g_BoardData.AInSamples, &samples.Data[i]);  // If not the internal ADC, send to streaming
                #if(DAQIFI_DIO_DEBUG == 1)
                {
                    // Toggle DIO pin for diagnostic use
                    if (result) DIO_WriteStateSingle(&g_BoardConfig.DIOChannels.Data[0], &g_BoardRuntimeConfig.DIOChannels.Data[0]);
                }
                #endif      
            }
        }
        
        g_BoardData.AInLatest.Data[channelIndex] = samples.Data[i];
    }
    
    g_BoardData.AInState.Data[moduleId].AInTaskState = AINTASK_IDLE;
}

void ADC_Tasks( void )
{
    size_t moduleIndex = 0;
    POWER_STATE powerState = pBoardData->PowerData.powerState;
    bool isPowered = ( powerState > MICRO_ON );
    
    for( \
            moduleIndex = 0; \
            moduleIndex < pBoardRuntimeConfig->AInModules.Size; \
            ++moduleIndex ){
        // Get channels associated with the current module
        AInArray moduleChannels;
        const AInModule* module = &pBoardConfig->AInModules.Data[moduleIndex];
        const AInModuleRuntimeConfig* moduleRuntime = &pBoardRuntimeConfig->AInModules.Data[moduleIndex];
        AInRuntimeArray moduleChannelRuntime;
        GetModuleChannelRuntimeData( &moduleChannels, &moduleChannelRuntime, moduleIndex );
        
        // Check if the module is enabled - if not, skip it
        bool isEnabled = (module->Type == AIn_MC12bADC || isPowered) && moduleRuntime->IsEnabled;
        if(!isEnabled)
        {
            pBoardData->AInState.Data[moduleIndex].AInTaskState = AINTASK_DISABLED;
            continue;
        }
               
        if (pBoardData->AInState.Data[moduleIndex].AInTaskState == AINTASK_INITIALIZING)
        {
            bool canInit = (module->Type == AIn_MC12bADC || isPowered);
            bool initialized = false;
            if (canInit)
            {
                if (ADC_InitHardware(module, &moduleChannels))
                {
                    if(module->Type == AIn_MC12bADC)
                    {
                        MC12b_WriteStateAll(&pBoardConfig->AInModules.Data[moduleIndex].Config.MC12b,
                            &pBoardRuntimeConfig->AInModules.Data[moduleIndex],
                            &moduleChannels,
                            &moduleChannelRuntime);
                    }
                    if(module->Type == AIn_AD7173)
                    {
                        AD7173_WriteStateAll(&pBoardConfig->AInModules.Data[moduleIndex].Config.AD7173,
                            &pBoardRuntimeConfig->AInModules.Data[moduleIndex],
                            &moduleChannels,
                            &moduleChannelRuntime);
                    }
                    ADC_WriteModuleState( moduleIndex, powerState );
                    pBoardData->AInState.Data[moduleIndex].AInTaskState = AINTASK_IDLE;
                    initialized = true;
                }
            }
            
            if (!initialized)
            {
                SYS_DEBUG_PRINT(SYS_ERROR_FATAL, "\nCannot Initialize ADC index=%d type=%d.\n", moduleIndex, module->Type);
            }
        }
         
    }
}

size_t ADC_FindChannelIndex( uint8_t channelId )
{
    size_t i=0;
    for (i=0; i< pBoardConfig->AInChannels.Size; ++i){
        if( pBoardConfig->AInChannels.Data[i].ChannelId == channelId ){
            return i;
        }
    }

    return (size_t)-1;
}

double ADC_ConvertToVoltage(const AInSample* sample)
{
    size_t channelIndex = ADC_FindChannelIndex( sample->Channel );
    const AInChannel* channelConfig = &g_BoardConfig.AInChannels.Data[channelIndex];
    const AInRuntimeConfig* runtimeConfig = &g_BoardRuntimeConfig.AInChannels.Data[channelIndex];
    const AInModule* moduleConfig = &g_BoardConfig.AInModules.Data[channelConfig->DataModule];
    const AInModuleRuntimeConfig* moduleRuntimeConfig = &g_BoardRuntimeConfig.AInModules.Data[channelConfig->DataModule];

    
    switch(moduleConfig->Type)
    {
    case AIn_MC12bADC:
        return MC12b_ConvertToVoltage(&channelConfig->Config.MC12b,
            runtimeConfig,
            &moduleConfig->Config.MC12b,
            moduleRuntimeConfig,
            sample);
    case AIn_AD7173:
        return AD7173_ConvertToVoltage(&channelConfig->Config.AD7173,
            runtimeConfig,
            &moduleConfig->Config.AD7173,
            moduleRuntimeConfig,
            sample);
    case AIn_AD7609:
        return AD7609_ConvertToVoltage(&channelConfig->Config.AD7609,
            runtimeConfig,
            &moduleConfig->Config.AD7609,
            moduleRuntimeConfig,
            sample);
    default:
        return 0.0;
    }
}

/*static bool ADC_IsDataValid(const AInSample* sample){
    return (sample->Timestamp > 0);
}*/

static uint8_t ADC_FindModuleIndex( const AInModule* module ){
    size_t moduleIndex = 0;
    for( moduleIndex = 0; moduleIndex < pBoardConfig->AInModules.Size; ++moduleIndex ){
        
        if( module == &pBoardConfig->AInModules.Data[ moduleIndex ] ){
            return moduleIndex;
        }
    }
    
    return (uint8_t)-1;
}

static bool ADC_ReadSamples( \
                            AInSampleArray* samples, \
                            const AInModule* module, \
                            AInModuleRuntimeConfig* moduleRuntime )
{
    uint8_t moduleId = ADC_FindModuleIndex( module );
    
    // Get channels associated with the current module
    AInArray moduleChannels;
    AInRuntimeArray moduleChannelRuntime;
    GetModuleChannelRuntimeData( \
                            &moduleChannels, \
                            &moduleChannelRuntime, \
                            moduleId );
    
    bool result = true;
    
    switch(module->Type)
    {
    case AIn_MC12bADC:
        result &= MC12b_ReadSamples( \
                                    samples, \
                                    &module->Config.MC12b, \
                                    moduleRuntime, \
                                    &moduleChannels, \
                                    &moduleChannelRuntime, \
                                    pBoardData->StreamTrigStamp );
        break;
    case AIn_AD7609:
        result &= AD7609_ReadSamples( \
                                    samples, \
                                    &module->Config.AD7609, \
                                    moduleRuntime, \
                                    &moduleChannels, \
                                    &moduleChannelRuntime, \
                                    pBoardData->StreamTrigStamp );
        break;
    case AIn_AD7173:
        result &= AD7173_ReadSamples( \
                                    samples, \
                                    &module->Config.AD7173, \
                                    moduleRuntime, \
                                    &moduleChannels, \
                                    &moduleChannelRuntime, \
                                    pBoardData->StreamTrigStamp );
        break;
    default:
        // Not implemented yet
        break;
    }
    
    return result;
}

static bool ADC_WriteModuleState( size_t moduleId, POWER_STATE powerState )
{
    const AInModule* currentModule = &pBoardConfig->AInModules.Data[ moduleId ];
    AInModuleRuntimeConfig* currentModuleRuntime = \
                                &pBoardRuntimeConfig->AInModules.Data[moduleId];
    
    bool result = true;
    bool isPowered = (powerState> MICRO_ON);
    switch(currentModule->Type)
    {
    case AIn_MC12bADC:
        result &= MC12b_WriteModuleState( \
                        &currentModule->Config.MC12b, \
                        currentModuleRuntime );
        break;
    case AIn_AD7609:
        result &= AD7609_WriteModuleState( \
                        &currentModule->Config.AD7609, \
                        currentModuleRuntime, \
                        isPowered );
        break;
    case AIn_AD7173:
        result &= AD7173_WriteModuleState( \
                        &currentModule->Config.AD7173, \
                        currentModuleRuntime, \
                        isPowered );
        break;
    default:
        // Not implemented yet
        break;
    }
    
    return result;
}

static bool ADC_InitHardware( \
                            const AInModule* boardConfig, \
                            const AInArray* moduleChannels )
{
    bool result = false;
     
    switch(boardConfig->Type)
    {
    case AIn_MC12bADC:
        result = MC12b_InitHardware( \
                                    &boardConfig->Config.MC12b, \
                                    moduleChannels );
        break;
    case AIn_AD7609:
        result = AD7609_InitHardware( \
                                    &boardConfig->Config.AD7609, \
                                    moduleChannels );
        break;
    case AIn_AD7173:
        result = AD7173_InitHardware( \
                                    &boardConfig->Config.AD7173, \
                                    moduleChannels );
        break;
    default:
        // Not implemented yet
        break;
    }
    
    return result;
}

/**
 * Extracts channel information for the specified module
 * @param moduleChannels [out] Static channel data
 * @param moduleChannelRuntime [out] Runtime channel data
 * @param moduleId The module to search for
 * @param boardConfig The board to extract information from
 * @param runtimeConfig The runtime structure to extract information from
 */
static void GetModuleChannelRuntimeData( \
                            AInArray* moduleChannels, \
                            AInRuntimeArray* moduleChannelRuntime, \
                            uint8_t moduleId )
{
    moduleChannels->Size = 0;
    moduleChannelRuntime->Size = 0;
    size_t i;
    for ( i=0; i< pBoardConfig->AInChannels.Size; ++i ){
        
        if( pBoardConfig->AInChannels.Data[i].DataModule != moduleId ){
            continue;
        }
        
        moduleChannels->Data[moduleChannels->Size] = pBoardConfig->AInChannels.Data[i];
        moduleChannels->Size += 1;
        
        moduleChannelRuntime->Data[moduleChannelRuntime->Size] = pBoardRuntimeConfig->AInChannels.Data[i];
        moduleChannelRuntime->Size += 1;
    }
}
