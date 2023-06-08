#include "SCPIADC.h"

// General
#include <stdlib.h>
#include <string.h>

// Harmony
#include "system_config.h"
#include "system_definitions.h"

// Project
#include "Util/StringFormatters.h"
#include "Util/Logger.h"
#include "state/data/BoardData.h"
#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "HAL/ADC.h"
#include "../HAL/NVM/DaqifiSettings.h"

scpi_result_t SCPI_ADCVoltageGet(scpi_t * context)
{
    int channel;
    AInSample *pAInLatest; 
    uint32_t *pAInLatestSize;
    AInArray * pBoardConfigAInChannels = BoardConfig_Get(                   \
                            BOARDCONFIG_AIN_CHANNELS,                       \
                            0 ); 
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIMECONFIG_AIN_CHANNELS);   
    
    if (SCPI_ParamInt32(context, &channel, FALSE))
    {
        // Get single
        volatile double val = 0;
        size_t index = ADC_FindChannelIndex((uint8_t)channel);
        if (index > pBoardConfigAInChannels->Size)
        {
            return SCPI_RES_ERR;
        }
        
        if (!pRuntimeAInChannels->Data[index].IsEnabled)
        {
            SCPI_ResultDouble(context, 0.0);
            return SCPI_RES_OK;
        }
        pAInLatest = BoardData_Get(                                         \
                BOARDDATA_AIN_LATEST,                                       \
                index );  
        
        val = ADC_ConvertToVoltage(pAInLatest);
        SCPI_ResultDouble(context, val);
    }
    else
    {
        // Get all
        size_t i=0;
        
        pAInLatestSize = BoardData_Get(                                     \
                BOARDDATA_AIN_LATEST_SIZE,                                  \
                0 );   
        
        for (i=0; i<*pAInLatestSize; ++i)
        {
            pAInLatest = BoardData_Get(                                     \
                BOARDDATA_AIN_LATEST,                                       \
                i ); 
            
            if (!pRuntimeAInChannels->Data[i].IsEnabled ||                  \
                pAInLatest->Timestamp < 1)
            {
                SCPI_ResultDouble(context, 0.0);
            }
            else
            {
                SCPI_ResultDouble(context, pAInLatest->Value);
            }
        }
    }
    
    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCChanEnableSet(scpi_t * context)
{
    int param1, param2;
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIMECONFIG_AIN_CHANNELS);   
    AInArray * pBoardConfigAInChannels = BoardConfig_Get(                   \
                            BOARDCONFIG_AIN_CHANNELS,                       \
                            0 ); 
    
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }

    if (SCPI_ParamInt32(context, &param2, FALSE))
    {
        size_t channelIndex = ADC_FindChannelIndex((uint8_t)param1);
        AInRuntimeConfig* channelRuntimeConfig =                            \
                        &pRuntimeAInChannels->Data[channelIndex];
        AInChannel* channel = &pBoardConfigAInChannels->Data[channelIndex];
        const AInModule* module = ADC_FindModule(                           \
                        channel->DataModule);
        
        // Single channel
        if (channelIndex > pBoardConfigAInChannels->Size)
        {
            return SCPI_RES_ERR;
        }
        if (module->Type == AIn_MC12bADC) // If the current module is the internal ADC then check to see if the channel is public
        {
            if (channel->Config.MC12b.IsPublic)
            {
                channelRuntimeConfig->IsEnabled = (param2 > 0);  // If public, allow allow configuration.
            }
            else
            {
                return SCPI_RES_ERR;    //  If private, return error
            }
        }
        else
        {
            channelRuntimeConfig->IsEnabled = (param2 > 0);  // If not the internal ADC, allow configuration.
        }
    }
    else
    {
        // Channel mask
        size_t index=0;
        for (index=0; index<pBoardConfigAInChannels->Size; ++index)
        {
            size_t channelIndex = ADC_FindChannelIndex((uint8_t)index);
            AInRuntimeConfig* channelRuntimeConfig =                        \
                        &pRuntimeAInChannels->Data[channelIndex];
            AInChannel* channel = &pBoardConfigAInChannels->Data[channelIndex];
            const AInModule* module = ADC_FindModule(                        \
                        channel->DataModule);
            bool value = (bool)((param1 & (1 << index)) > 0);
            
            // TODO: Perhaps add some sort of feedback if the user is attempting to edit a value beyond their  - this is fairly tricky to implement however
            if (module->Type == AIn_MC12bADC) // If the current module is the internal ADC then check to see if the channel is public
            {
                if (channel->Config.MC12b.IsPublic)
                {
                    channelRuntimeConfig->IsEnabled = value;  // If public, allow allow configuration.
                }
            }
            else
            {
                channelRuntimeConfig->IsEnabled = value;  // If not the internal ADC, allow configuration.
            }  
        }
    }
    
    if (ADC_WriteChannelStateAll())
    {
        return SCPI_RES_OK;
    }
    else
    {
        return SCPI_RES_ERR;
    }
}

scpi_result_t SCPI_ADCChanEnableGet(scpi_t * context)
{
    int param1;
    AInArray * pBoardConfigAInChannels = BoardConfig_Get(                   \
                            BOARDCONFIG_AIN_CHANNELS,                       \
                            0 ); 
    
    tBoardConfig * pBoardConfig = BoardConfig_Get(                          \
                            BOARDCONFIG_VARIANT,                            \
                            0 ); 
    
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIMECONFIG_AIN_CHANNELS);   
    
    if (SCPI_ParamInt32(context, &param1, FALSE))
    {
        // Single channel
        size_t index = ADC_FindChannelIndex((uint8_t)param1);
        // TODO: This function should be able to read which version of the board we are using and assign the ADC channels associated that version
        if (index > pBoardConfigAInChannels->Size)
        {
            return SCPI_RES_ERR;
        }
        
        if (pRuntimeAInChannels->Data[index].IsEnabled)
        {
            SCPI_ResultInt32(context, 1);
        }
        else
        {
            SCPI_ResultInt32(context, 0);
        }
    }
    else
    {
        uint32_t mask = 0;
        size_t i=0;
        // TODO: This function should be able to read which version of the board we are using and report the ADC channels associated that version
        for (i=0; i<pBoardConfig->AInModules.Data[0].Size; ++i)
        {
            if (pRuntimeAInChannels->Data[i].IsEnabled)
            {
                mask |= (1 << i);
            }
        }
        SCPI_ResultInt32(context, mask);
    }
    
    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCChanSingleEndSet(scpi_t * context)
{
    uint32_t *pAInLatestSize; 
    int param1, param2;
    AInArray * pBoardConfigAInChannels = BoardConfig_Get(                   \
                            BOARDCONFIG_AIN_CHANNELS,                       \
                            0 );
    
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIMECONFIG_AIN_CHANNELS);   
    
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
    
    if (SCPI_ParamInt32(context, &param2, FALSE))
    {
        // Single channel
        size_t index = ADC_FindChannelIndex((uint8_t)param1);
        if (index > pBoardConfigAInChannels->Size)
        {
            return SCPI_RES_ERR;
        }
        
        pRuntimeAInChannels->Data[index].IsDifferential = (param2 == 0);
    }
    else
    {
        pAInLatestSize = BoardData_Get(                                     \
                BOARDDATA_AIN_LATEST_SIZE,                                  \
                0 ); 
        
        size_t i=0;
        for (i=0; i<*pAInLatestSize; ++i)
        {
            pRuntimeAInChannels->Data[i].IsDifferential =               \
                        (param2 & (1 << i)) == 0;
        }
    }
    
    if (ADC_WriteChannelStateAll())
    {
        return SCPI_RES_OK;
    }
    else
    {
        return SCPI_RES_ERR;
    }
}

scpi_result_t SCPI_ADCChanSingleEndGet(scpi_t * context)
{
    int param1;
    uint32_t *pAInLatestSize; 
    AInArray * pBoardConfigAInChannels = BoardConfig_Get(                   \
                            BOARDCONFIG_AIN_CHANNELS,                       \
                            0 );
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIMECONFIG_AIN_CHANNELS);   
    if (SCPI_ParamInt32(context, &param1, FALSE))
    {
        // Single channel
        size_t index = ADC_FindChannelIndex((uint8_t)param1);
        if (index > pBoardConfigAInChannels->Size)
        {
            return SCPI_RES_ERR;
        }
        
        if (pRuntimeAInChannels->Data[index].IsDifferential)
        {
            SCPI_ResultInt32(context, 0);
        }
        else
        {
            SCPI_ResultInt32(context, 1);
        }
    }
    else
    {
        uint32_t mask = 0;
        size_t i=0;
        
        pAInLatestSize = BoardData_Get(                                     \
                BOARDDATA_AIN_LATEST_SIZE,                                  \
                0 ); 
        for (i=0; i<*pAInLatestSize; ++i)
        {
            if (!pRuntimeAInChannels->Data[i].IsDifferential)
            {
                mask |= (1 << i);
            }
        }
    }
    
    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCChanRangeSet(scpi_t * context)
{
    int param1;
    if (SCPI_ParamInt32(context, &param1, FALSE));
    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCChanRangeGet(scpi_t * context)
{
    int param1;
    if (SCPI_ParamInt32(context, &param1, FALSE));
    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCChanCalmSet(scpi_t * context)
{
    int param1;
    double param2;
    AInArray * pBoardConfigAInChannels = BoardConfig_Get(                   \
                            BOARDCONFIG_AIN_CHANNELS,                       \
                            0 );
    
    AInRuntimeArray * pRunTimeAInChannels = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIMECONFIG_AIN_CHANNELS);
    
    
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
    
    if (!SCPI_ParamDouble(context, &param2, TRUE))
    {
        return SCPI_RES_ERR;
    }
    
    size_t index = ADC_FindChannelIndex((uint8_t)param1);
    if (index > pBoardConfigAInChannels->Size)
    {
        return SCPI_RES_ERR;
    }

    pRunTimeAInChannels->Data[index].CalM = param2;
    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCChanCalbSet(scpi_t * context)
{
    int param1;
    double param2;
    AInArray * pBoardConfigAInChannels = BoardConfig_Get(                   \
                            BOARDCONFIG_AIN_CHANNELS,                       \
                            0 );
    
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIMECONFIG_AIN_CHANNELS);  
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
    
    if (!SCPI_ParamDouble(context, &param2, TRUE))
    {
        return SCPI_RES_ERR;
    }
    
    size_t index = ADC_FindChannelIndex((uint8_t)param1);
    if (index > pBoardConfigAInChannels->Size)
    {
        return SCPI_RES_ERR;
    }

    pRuntimeAInChannels->Data[index].CalB = param2;
    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCChanCalmGet(scpi_t * context)
{
    int param1;
    AInArray * pBoardConfigAInChannels = BoardConfig_Get(                   \
                            BOARDCONFIG_AIN_CHANNELS,                       \
                            0 );
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIMECONFIG_AIN_CHANNELS);  
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
        
    size_t index = ADC_FindChannelIndex((uint8_t)param1);
    if (index > pBoardConfigAInChannels->Size)
    {
        return SCPI_RES_ERR;
    }

    SCPI_ResultDouble(context, pRuntimeAInChannels->Data[index].CalM);
    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCChanCalbGet(scpi_t * context)
{
    int param1;
    AInArray * pBoardConfigAInChannels = BoardConfig_Get(                   \
                            BOARDCONFIG_AIN_CHANNELS,                       \
                            0 );
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIMECONFIG_AIN_CHANNELS);  
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
        
    size_t index = ADC_FindChannelIndex((uint8_t)param1);
    if (index > pBoardConfigAInChannels->Size)
    {
        return SCPI_RES_ERR;
    }

    SCPI_ResultDouble(context, pRuntimeAInChannels->Data[index].CalB);
    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCCalSave(scpi_t * context)
{
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIMECONFIG_AIN_CHANNELS);  
    if(SaveADCCalSettings(                                                  \
                        DaqifiSettings_UserAInCalParams,                    \
                        pRuntimeAInChannels))
    {
        return SCPI_RES_OK;
    }
    else
    {
        return SCPI_RES_ERR;
    } 
}

scpi_result_t SCPI_ADCCalFSave(scpi_t * context)
{
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIMECONFIG_AIN_CHANNELS);  
    if(SaveADCCalSettings(                                                  \
                        DaqifiSettings_FactAInCalParams,                    \
                        pRuntimeAInChannels))
    {
        return SCPI_RES_OK;
    }
    else
    {
        return SCPI_RES_ERR;
    } 
}

scpi_result_t SCPI_ADCCalLoad(scpi_t * context)
{ 
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIMECONFIG_AIN_CHANNELS); 
    if(LoadADCCalSettings(                                                  \
                        DaqifiSettings_UserAInCalParams,                    \
                        pRuntimeAInChannels))
    {
        return SCPI_RES_OK;
    }
    else
    {
        return SCPI_RES_ERR;
    } 
}

scpi_result_t SCPI_ADCCalFLoad(scpi_t * context)
{
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIMECONFIG_AIN_CHANNELS); 
    if(LoadADCCalSettings(                                                  \
                        DaqifiSettings_FactAInCalParams,                    \
                        pRuntimeAInChannels))
    {
        return SCPI_RES_OK;
    }
    else
    {
        return SCPI_RES_ERR;
    } 
}

scpi_result_t SCPI_ADCUseCalSet(scpi_t * context)
{
    int param1;
    DaqifiSettings tmpTopLevelSettings;
    AInRuntimeArray * pRuntimeAInChannels = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIMECONFIG_AIN_CHANNELS); 
        
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
        
     //  Load existing settings
    if(!LoadNvmSettings(DaqifiSettings_TopLevelSettings, &tmpTopLevelSettings)) return SCPI_RES_ERR;
    
    //  Update calVals setting
    tmpTopLevelSettings.settings.topLevelSettings.calVals = param1;
    
    //  Store to NVM
    if(!SaveNvmSettings(&tmpTopLevelSettings)) return SCPI_RES_ERR;

        //  Update runtime values
    switch (param1)
    {
        case 0:
            if(!LoadADCCalSettings(                                         \
                        DaqifiSettings_FactAInCalParams,                    \
                        pRuntimeAInChannels))
            {
                return SCPI_RES_ERR;
            }
            break;
        case 1:
            if(!LoadADCCalSettings(                                         \
                        DaqifiSettings_UserAInCalParams,                    \
                        pRuntimeAInChannels))
            {
                return SCPI_RES_ERR;
            }
            break;
        default:
            return SCPI_RES_ERR;
            break;
    }
    return SCPI_RES_OK;
}

scpi_result_t SCPI_ADCUseCalGet(scpi_t * context)
{
    DaqifiSettings tmpTopLevelSettings;

    if(LoadNvmSettings(DaqifiSettings_TopLevelSettings, &tmpTopLevelSettings))
    {
        SCPI_ResultInt32(context, tmpTopLevelSettings.settings.topLevelSettings.calVals);
        return SCPI_RES_OK;
    }
    else
    {
        return SCPI_RES_ERR;
    } 
}