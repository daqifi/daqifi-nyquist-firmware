#include "SCPIDIO.h"

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
#include "HAL/DIO.h"
#include "../../HAL/TimerApi/TimerApi.h"
/**
 * Sets the GPIO direction for a single pin
 * @param id The id of the pin to change
 * @param isInput Indicates whether the value is an input
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
static scpi_result_t SCPI_GPIOSingleDirectionSet(uint8_t id, bool isInput);

/**
 * Sets the GPIO direction for all pins
 * @param mask A mask where each bit corresponds to the pin with the given id (BIT(1) == PIN(1))
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
static scpi_result_t SCPI_GPIOMultiDirectionSet(uint32_t mask);

/**
 * Gets the direction of a single GPIO pin
 * @param id The id of the pin
 * @param result [out] The direction of the pin
 * @return A boolean indicating whether the pin is an output (true) or input (false)
 */
static scpi_result_t SCPI_GPIOSingleDirectionGet(uint8_t id, bool* result);

/**
 * Gets the direction of all GPIO pins
 * @param mask [out] A mask where each bit corresponds to the pin with the given id (BIT(1) == PIN(1))
 * @return A value indicating successfullness of the operation
 */
static scpi_result_t SCPI_GPIOMultiDirectionGet(uint32_t* mask);

/**
 * Sets the GPIO value for a single (output) pin
 * @param id The id of the pin to change
 * @param value The new value of the pin
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
static scpi_result_t SCPI_GPIOSingleStateSet(uint8_t id, bool value);

/**
 * Sets the value (high/low) for all pins
 * @param mask A mask where each bit corresponds to the pin with the given id (BIT(1) == PIN(1))
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
static scpi_result_t SCPI_GPIOMultiStateSet(uint32_t mask);

/**
 * Gets the value of a single GPIO pin
 * @param id The id of the pin
 * @param result [out] The direction of the pin
 * @return A boolean indicating whether the pin is an output (true) or input (false)
 */
static scpi_result_t SCPI_GPIOSingleStateGet(uint8_t id, bool* result);

/**
 * Gets the value (high/low) of all GPIO pins
 * @return A mask where each bit corresponds to the pin with the given id (BIT(1) == PIN(1))
 */
static scpi_result_t SCPI_GPIOMultiStateGet(uint32_t* result);

/**
 * Enables the 
 * @param mask A mask where each bit corresponds to the pin with the given id (BIT(1) == PIN(1))
 * @return SCPI_RES_OK on success SCPI_RES_ERR on error
 */
static scpi_result_t SCPI_PWMSingleStateSet(uint8_t id, bool value);

scpi_result_t SCPI_GPIODirectionSet(scpi_t * context)
{
    int param1, param2;
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
    
    // TODO: Validate channel
    if (!SCPI_ParamInt32(context, &param2, FALSE))
    {
        return SCPI_GPIOMultiDirectionSet((uint32_t)~param1); // Interpret the input as a bit mask (invert because 1=output but we use isInput as the test)
    }
    else
    {
        return SCPI_GPIOSingleDirectionSet((uint8_t)param1, !(bool)param2); // Interpret the input as a bit/direction pair (invert because 1=output but we use isInput as the test)
    }
}

scpi_result_t SCPI_GPIODirectionGet(scpi_t * context)
{
    scpi_result_t retCode = SCPI_RES_OK;
    
    int param1;
    int converted = 0;
    if (!SCPI_ParamInt32(context, &param1, FALSE))
    {
        uint32_t result = 0;
        if (SCPI_GPIOMultiDirectionGet(&result) == SCPI_RES_ERR)
        {
            retCode = SCPI_RES_ERR;
        }
        else
        {
            converted = (int)result;
        }
    }
    else
    {
        bool result = false;
        if (SCPI_GPIOSingleDirectionGet((uint8_t)param1, &result) == SCPI_RES_ERR)
        {
            retCode = SCPI_RES_ERR;
        }
        else
        {
            converted = (int)result;
        }
    }

    if (retCode == SCPI_RES_OK)
    {
        SCPI_ResultInt32(context, converted);
    }
    
    return retCode;
}

scpi_result_t SCPI_GPIOStateSet(scpi_t * context)
{
    int param1, param2;
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
    
    // TODO: Validate channel
    if (!SCPI_ParamInt32(context, &param2, FALSE))
    {
        return SCPI_GPIOMultiStateSet((uint32_t)param1); // Interpret the input as a bit mask
    }
    else
    {
        return SCPI_GPIOSingleStateSet((uint8_t)param1, (bool)param2); // Interpret the input as a bit/direction pair
    }
}

scpi_result_t SCPI_GPIOStateGet(scpi_t * context)
{
    scpi_result_t retCode = SCPI_RES_OK;
    
    int param1;
    int converted = 0;
    if (!SCPI_ParamInt32(context, &param1, FALSE))
    {
        uint32_t result = 0;
        if (SCPI_GPIOMultiStateGet(&result) == SCPI_RES_ERR)
        {
            retCode = SCPI_RES_ERR;
        }
        else
        {
            converted = (int)result;
        }
    }
    else
    {
        bool result = false;
        if (SCPI_GPIOSingleStateGet((uint8_t)param1, &result) == SCPI_RES_ERR)
        {
            retCode = SCPI_RES_ERR;
        }
        else
        {
            converted = (int)result;
        }
    }

    if (retCode == SCPI_RES_OK)
    {
        SCPI_ResultInt32(context, converted);
    }
    
    return retCode;
}

scpi_result_t SCPI_GPIOEnableSet(scpi_t * context)
{
    bool * pRunTimeDIOGlobalEnable = BoardRunTimeConfig_Get(                
                        BOARDRUNTIMECONFIG_DIO_GLOBAL_ENABLE);
    bool enable = false; 
    int param1;
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
    enable = param1>0;
    memcpy(pRunTimeDIOGlobalEnable, &enable, sizeof(bool));
    return SCPI_RES_OK;
}

scpi_result_t SCPI_GPIOEnableGet(scpi_t * context)
{
    bool * pRunTimeDIOGlobalEnable = BoardRunTimeConfig_Get(                
                        BOARDRUNTIMECONFIG_DIO_GLOBAL_ENABLE);
    SCPI_ResultInt32(context, *pRunTimeDIOGlobalEnable);
    return SCPI_RES_OK;
}


scpi_result_t SCPI_PWMChannelEnableSet (scpi_t * context){
    int param1, param2;
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
    
    if (!SCPI_ParamInt32(context, &param2, TRUE))
    {
        return SCPI_RES_ERR;
    }
    
    return SCPI_PWMSingleStateSet((uint8_t)param1, (bool)param2);
    return SCPI_RES_OK;
   
}
scpi_result_t SCPI_PWMChannelEnableGet(scpi_t * context){
    int param1;
    DIORuntimeArray * pRunTimeDIOChannels = BoardRunTimeConfig_Get(         
                        BOARDRUNTIMECONFIG_DIO_CHANNELS);
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
    
    if(param1>=pRunTimeDIOChannels->Size){
        return SCPI_RES_ERR;
    }
    uint32_t pwmEnable=pRunTimeDIOChannels->Data[param1].IsPwmActive;
    SCPI_ResultUInt32(context,pwmEnable);
    return SCPI_RES_OK;
}
scpi_result_t SCPI_PWMChannelFrequencySet(scpi_t * context){
    uint32_t param1,param2;
    int i;
    uint32_t timerClock=TIMER_CLOCK_FRQ;
    if (!SCPI_ParamUInt32(context, &param1, FALSE))
    {
        return SCPI_RES_ERR;
    }
    if (!SCPI_ParamUInt32(context, &param2, TRUE))
    {
        return SCPI_RES_ERR;
    }
    if(param2>timerClock || param2<=0)
    {
        return SCPI_RES_ERR;
    }
    DIORuntimeArray * pRunTimeDIOChannels = BoardRunTimeConfig_Get(         
                        BOARDRUNTIMECONFIG_DIO_CHANNELS);
    //only timer 3 is driving all the pwm so, channel independent frequency cannot be generated
    for(i=0;i<pRunTimeDIOChannels->Size;i++){
        pRunTimeDIOChannels->Data[i].PwmFrequency=param2;
    }
    //updating frequency for one channel means updating frequency of all the channels
    DIO_PWMFrequencySet(param1);
    //update the duty cycle period register of all the channels based on the new frequency
    for(i=0;i<pRunTimeDIOChannels->Size;i++){
        DIO_PWMDutyCycleSetSingle(i);
    }   
    return SCPI_RES_OK;
}
scpi_result_t SCPI_PWMChannelFrequencyGet(scpi_t * context){
    int param1;
    DIORuntimeArray * pRunTimeDIOChannels = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIMECONFIG_DIO_CHANNELS);
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
    
    if(param1>=pRunTimeDIOChannels->Size){
        return SCPI_RES_ERR;
    }
    uint32_t freq=pRunTimeDIOChannels->Data[param1].PwmFrequency;
    SCPI_ResultUInt32(context,freq);
    return SCPI_RES_OK;
}
scpi_result_t SCPI_PWMChannelDUTYSet(scpi_t * context){
    
    uint32_t param1, param2;
    DIORuntimeArray * pRunTimeDIOChannels;
    if (!SCPI_ParamUInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
    
    if (!SCPI_ParamUInt32(context, &param2, TRUE))
    {
        return SCPI_RES_ERR;
    }
    pRunTimeDIOChannels = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_DIO_CHANNELS);
    if(param1>pRunTimeDIOChannels->Size){
        return SCPI_RES_ERR;
    }
    if(param2>100){
        return SCPI_RES_ERR;
    }
    
    pRunTimeDIOChannels->Data[param1].PwmDutyCycle=param2;    
    DIO_PWMDutyCycleSetSingle(param1);
    return SCPI_RES_OK;
}
scpi_result_t SCPI_PWMChannelDUTYGet(scpi_t * context){
    int param1;
    DIORuntimeArray * pRunTimeDIOChannels = BoardRunTimeConfig_Get(         \
                        BOARDRUNTIMECONFIG_DIO_CHANNELS);
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }
    
    if(param1>=pRunTimeDIOChannels->Size){
        return SCPI_RES_ERR;
    }
    uint32_t duty=pRunTimeDIOChannels->Data[param1].PwmDutyCycle;
    SCPI_ResultUInt32(context,duty);
    return SCPI_RES_OK;

}

////////
// Internal Implementation
////////

static scpi_result_t SCPI_GPIOSingleDirectionSet(uint8_t id, bool isInput)
{
    DIORuntimeArray * pRunTimeDIOChannels = BoardRunTimeConfig_Get(         
                        BOARDRUNTIMECONFIG_DIO_CHANNELS);
    
    if ( id > pRunTimeDIOChannels->Size)
    {
        return SCPI_RES_ERR;
    }
    
    pRunTimeDIOChannels->Data[id].IsInput = isInput;
    if (!DIO_WriteStateSingle(id))
    {
        return SCPI_RES_ERR;
    }
    
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GPIOMultiDirectionSet(uint32_t mask)
{
    size_t i = 0;
    scpi_result_t result = SCPI_RES_OK;
    
    DIORuntimeArray * pRunTimeDIOChannels = BoardRunTimeConfig_Get(         
                        BOARDRUNTIMECONFIG_DIO_CHANNELS);
    
    // Obviously, this breaks down if we have more than 32 channels
    for (i=0; i<pRunTimeDIOChannels->Size; ++i) 
    {
        bool isInput = (mask & (1 << i));
        if (SCPI_GPIOSingleDirectionSet(i, isInput) == SCPI_RES_ERR)
        {
            result = SCPI_RES_ERR;
        }
    }
    
    return result;
}

static scpi_result_t SCPI_GPIOSingleDirectionGet(uint8_t id, bool* result)
{
    DIORuntimeArray * pRunTimeDIOChannels = BoardRunTimeConfig_Get(         
                        BOARDRUNTIMECONFIG_DIO_CHANNELS);
    
    if ( id > pRunTimeDIOChannels->Size)
    {
        return SCPI_RES_ERR;
    }
    
    (*result)=!pRunTimeDIOChannels->Data[id].IsInput;

    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GPIOMultiDirectionGet(uint32_t* mask)
{
    DIORuntimeArray * pRunTimeDIOChannels = BoardRunTimeConfig_Get(         
                        BOARDRUNTIMECONFIG_DIO_CHANNELS);
    
    scpi_result_t result = SCPI_RES_OK;
    (*mask) = 0;
    int i;
    // Obviously, this breaks down if we have more than 32 channels
    for (i=0; i<pRunTimeDIOChannels->Size; ++i) 
    {
        bool channelResult;
        if (SCPI_GPIOSingleDirectionGet(i, &channelResult) == SCPI_RES_ERR)
        {
            result = SCPI_RES_ERR;
            continue;
        }
        
        if (channelResult)
        {
            (*mask) |= (1 << i);
        }
        
    }
    
    return result;
}

static scpi_result_t SCPI_GPIOSingleStateSet(uint8_t id, bool value)
{
    DIORuntimeArray * pRunTimeDIOChannels = BoardRunTimeConfig_Get(         
                        BOARDRUNTIMECONFIG_DIO_CHANNELS);
    
    if ( id > pRunTimeDIOChannels->Size)
    {
        return SCPI_RES_ERR;
    }
    
    pRunTimeDIOChannels->Data[id].Value = value;
    if (!DIO_WriteStateSingle(id))
    {
        return SCPI_RES_ERR;
    }
    
    return SCPI_RES_OK;
}
 

static scpi_result_t SCPI_GPIOMultiStateSet(uint32_t mask)
{
    size_t i = 0;
    scpi_result_t result = SCPI_RES_OK;
    
    DIORuntimeArray * pRunTimeDIOChannels = BoardRunTimeConfig_Get(         
                        BOARDRUNTIMECONFIG_DIO_CHANNELS);
    
    // Obviously, this breaks down if we have more than 32 channels
    for (i=0; i<pRunTimeDIOChannels->Size; ++i) 
    {
        bool value = (mask & (1 << i));
        if (SCPI_GPIOSingleStateSet(i, value) == SCPI_RES_ERR)
        {
            result = SCPI_RES_ERR;
        }
    }
    
    return result;
}

static scpi_result_t SCPI_GPIOSingleStateGet(uint8_t id, bool* result)
{
    DIORuntimeArray * pRunTimeDIOChannels = BoardRunTimeConfig_Get(         
                        BOARDRUNTIMECONFIG_DIO_CHANNELS);
    
    if ( id > pRunTimeDIOChannels->Size)
    {
        return SCPI_RES_ERR;
    }
    
    DIOSample sample;
    uint32_t mask = (1 << id);
    if (!DIO_ReadSampleByMask(&sample,mask))
    {
        return SCPI_RES_ERR;
    }
    
    (*result)=(mask & sample.Values) != 0;
    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GPIOMultiStateGet(uint32_t* result)
{
    (*result) = 0;
    uint32_t channelMask = 0xFFFFFFFF;
    DIOSample sample;
    
    if (!DIO_ReadSampleByMask(&sample, channelMask))
    {
        return SCPI_RES_ERR;
    }
    
    (*result)=sample.Values;
    return SCPI_RES_OK;
}


static scpi_result_t SCPI_PWMSingleStateSet(uint8_t id, bool value)
{
    DIORuntimeArray * pRunTimeDIOChannels = BoardRunTimeConfig_Get(         
                        BOARDRUNTIMECONFIG_DIO_CHANNELS);
    
    if ( id > pRunTimeDIOChannels->Size)
    {
        return SCPI_RES_ERR;
    }
    if(value){
        pRunTimeDIOChannels->Data[id].Value=1;
        pRunTimeDIOChannels->Data[id].IsPwmActive=1;
    }
    else{
        pRunTimeDIOChannels->Data[id].Value=0;
        pRunTimeDIOChannels->Data[id].IsPwmActive=0;
    }
    if (!DIO_PWMWriteStateSingle(id))
    {
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

