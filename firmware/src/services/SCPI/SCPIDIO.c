#define LOG_LVL LOG_LEVEL_SCPI
#define LOG_MODULE LOG_MODULE_SCPI
#include "SCPIDIO.h"
#include "SCPIInterface.h"

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
#include "HAL/UserClock/UserClock.h"   // #668: REFCLKO clock outputs
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
static scpi_result_t SCPI_GPIOMultiDirectionSet(scpi_t * context, uint32_t mask);

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
static scpi_result_t SCPI_GPIOMultiStateSet(scpi_t * context, uint32_t mask);

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

// #671: validate a single-channel index at the SCPI boundary, on the FULL
// parsed value, BEFORE it is narrowed to uint8_t. The outer setters cast
// (uint8_t)param1 to call the inner single-channel helpers, so an index like
// 256 wraps to 0 and aliases a nonexistent channel onto a valid one — slipping
// past the inner id>=Size guard with no error. Reject out-of-range indices
// (including negatives) here, surfacing a specific SCPI_ERROR_DATA_OUT_OF_RANGE
// + LOG_E so the failure is discoverable via SYST:ERR? / SYST:LOG? (standing
// error-visibility rule) rather than a bare generic -200. DIO channel ids are
// dense 0 .. Size-1 (no monitoring gap), so the logged range is exact.
static bool DIO_SingleChannelIndexValid(scpi_t *context, int channel)
{
    DIORuntimeArray * pRunTimeDIOChannels =
            BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_DIO_CHANNELS);
    if ((channel >= 0) && ((size_t)channel < (size_t)pRunTimeDIOChannels->Size)) {
        return true;
    }
    LOG_E("DIO: channel %d out of range (valid 0..%u)",
          channel, (unsigned)(pRunTimeDIOChannels->Size - 1));
    SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
    return false;
}

/* If @p channel is owned by the DIO probe or claimed by a peripheral
 * (SPI/I2C/UART/...), push a SCPI execution error naming the owner and
 * return true. The caller should then return SCPI_RES_ERR. Returns false
 * if the channel is free for normal DIO/PWM control. (#664 ownership) */
static bool SCPI_DioChannelBlocked(scpi_t * context, int channel, const char * op)
{
    const char* owner = DIO_ChannelBlockedReason((uint8_t)channel);
    if (owner == NULL) {
        return false;
    }
    char msg[64];
    snprintf(msg, sizeof(msg), "%s: DIO ch %d in use by %s", op, channel, owner);
    SCPI_ExecutionError(context, msg);
    return true;
}

scpi_result_t SCPI_GPIODirectionSet(scpi_t * context)
{
    int param1, param2;
    if (!SCPI_ParamInt32(context, &param1, TRUE))
    {
        return SCPI_RES_ERR;
    }

    if (!SCPI_ParamInt32(context, &param2, FALSE))
    {
        return SCPI_GPIOMultiDirectionSet(context, (uint32_t)~param1); // Interpret the input as a bit mask (invert because 1=output but we use isInput as the test)
    }
    else
    {
        if (!DIO_SingleChannelIndexValid(context, param1)) {
            return SCPI_RES_ERR; // #671: reject before (uint8_t) truncation
        }
        if (SCPI_DioChannelBlocked(context, param1, "DIO:DIR")) {
            return SCPI_RES_ERR;
        }
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
        /* Reject a read of an owned pin the same way the setter dispatch does —
         * consistent with SCPI_GPIODirectionSet (see SCPI_GPIOStateGet). */
        if (!DIO_SingleChannelIndexValid(context, param1) ||  // #671: reject before truncation
            SCPI_DioChannelBlocked(context, param1, "DIO:DIR?") ||
            SCPI_GPIOSingleDirectionGet((uint8_t)param1, &result) == SCPI_RES_ERR)
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

    if (!SCPI_ParamInt32(context, &param2, FALSE))
    {
        return SCPI_GPIOMultiStateSet(context, (uint32_t)param1); // Interpret the input as a bit mask
    }
    else
    {
        if (!DIO_SingleChannelIndexValid(context, param1)) {
            return SCPI_RES_ERR; // #671: reject before (uint8_t) truncation
        }
        if (SCPI_DioChannelBlocked(context, param1, "DIO:STATE")) {
            return SCPI_RES_ERR;
        }
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
        /* Reject a read of an owned pin (SPI/probe/...) the same way the setter
         * dispatch does — an owned bus line has no meaningful DIO logic level,
         * and the read path strips it to 0, misreporting a driven-high pin
         * (e.g. an idle-high CS) as 0. Error + name the owner (a config problem,
         * per the SCPI data-visibility rule), consistent with SCPI_GPIOStateSet. */
        if (!DIO_SingleChannelIndexValid(context, param1) ||  // #671: reject before truncation
            SCPI_DioChannelBlocked(context, param1, "DIO:STATE?") ||
            SCPI_GPIOSingleStateGet((uint8_t)param1, &result) == SCPI_RES_ERR)
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

    if (!DIO_SingleChannelIndexValid(context, param1)) {
        return SCPI_RES_ERR; // #671: reject before (uint8_t) truncation
    }
    if (SCPI_DioChannelBlocked(context, param1, "DIO:PWM:ENA")) {
        return SCPI_RES_ERR;
    }

    return SCPI_PWMSingleStateSet((uint8_t)param1, (bool)param2);
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
    // #671: FREQ Set was the 7th single-channel callback and had NO index
    // bound at all — param1 flowed unbounded into DIO_PWMFrequencySet, which
    // reads Data[param1].PwmFrequency (OOB) and divides the timebase by it
    // (a device-resetting divide-by-zero on indices landing on a 0 dword).
    // Validate at the boundary like the other single-channel callbacks.
    if (param1 >= (uint32_t)pRunTimeDIOChannels->Size) {
        LOG_E("DIO:PWM:FREQ: channel %u out of range (valid 0..%u)",
              (unsigned)param1, (unsigned)(pRunTimeDIOChannels->Size - 1));
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_OUT_OF_RANGE);
        return SCPI_RES_ERR;
    }
    if (SCPI_DioChannelBlocked(context, param1, "DIO:PWM:FREQ")) {
        return SCPI_RES_ERR;
    }
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
    if(param1>=pRunTimeDIOChannels->Size){
        return SCPI_RES_ERR;
    }
    if(param2>100){
        return SCPI_RES_ERR;
    }
    if (SCPI_DioChannelBlocked(context, param1, "DIO:PWM:DUTY")) {
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
    
    if ( id >= pRunTimeDIOChannels->Size)
    {
        return SCPI_RES_ERR;
    }
    /* Pure writer: ownership is enforced by both callers (see
     * SCPI_GPIOSingleStateSet) — a blocked channel never reaches here. */
    pRunTimeDIOChannels->Data[id].IsInput = isInput;
    if (!DIO_WriteStateSingle(id))
    {
        return SCPI_RES_ERR;
    }

    return SCPI_RES_OK;
}

static scpi_result_t SCPI_GPIOMultiDirectionSet(scpi_t * context, uint32_t mask)
{
    size_t i = 0;
    scpi_result_t result = SCPI_RES_OK;
    uint32_t blockedMask = 0;

    DIORuntimeArray * pRunTimeDIOChannels = BoardRunTimeConfig_Get(
                        BOARDRUNTIMECONFIG_DIO_CHANNELS);

    // Obviously, this breaks down if we have more than 32 channels
    for (i=0; i<pRunTimeDIOChannels->Size; ++i)
    {
        /* Skip probe/peripheral-owned pins (see SCPI_GPIOMultiStateSet) — never
         * stomp an owner's direction, and report the skip rather than silently
         * succeeding. */
        if (DIO_ChannelBlocked(i))
        {
            blockedMask |= (uint32_t)(1u << i);
            continue;
        }
        bool isInput = (mask & (1 << i));
        if (SCPI_GPIOSingleDirectionSet(i, isInput) == SCPI_RES_ERR)
        {
            result = SCPI_RES_ERR;
        }
    }

    if (blockedMask != 0)
    {
        /* Name the ACTUAL owner of the first skipped channel (registry has
         * SPI/I2C/UART/1-Wire/... + probe) rather than a fixed string. */
        uint8_t firstCh = (uint8_t)__builtin_ctz(blockedMask);
        const char* owner = DIO_ChannelBlockedReason(firstCh);
        char msg[112];
        snprintf(msg, sizeof(msg),
                 "DIO:DIR mask: skipped owned channels 0x%04X (ch %u in use by %s)",
                 (unsigned)blockedMask, (unsigned)firstCh,
                 owner ? owner : "peripheral/probe");
        SCPI_ExecutionError(context, msg);
        result = SCPI_RES_ERR;
    }

    return result;
}

static scpi_result_t SCPI_GPIOSingleDirectionGet(uint8_t id, bool* result)
{
    DIORuntimeArray * pRunTimeDIOChannels = BoardRunTimeConfig_Get(         
                        BOARDRUNTIMECONFIG_DIO_CHANNELS);
    
    if ( id >= pRunTimeDIOChannels->Size)
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

    if ( id >= pRunTimeDIOChannels->Size)
    {
        return SCPI_RES_ERR;
    }
    /* Pure writer: ownership is enforced by both callers — the single form
     * (SCPI_GPIOStateSet) rejects blocked channels upstream and names the
     * owner; the mask form (SCPI_GPIOMultiStateSet) skips them before calling
     * here. So a blocked channel never reaches this point. */
    pRunTimeDIOChannels->Data[id].Value = value;
    if (!DIO_WriteStateSingle(id))
    {
        return SCPI_RES_ERR;
    }

    return SCPI_RES_OK;
}
 

static scpi_result_t SCPI_GPIOMultiStateSet(scpi_t * context, uint32_t mask)
{
    size_t i = 0;
    scpi_result_t result = SCPI_RES_OK;
    uint32_t blockedMask = 0;

    DIORuntimeArray * pRunTimeDIOChannels = BoardRunTimeConfig_Get(
                        BOARDRUNTIMECONFIG_DIO_CHANNELS);

    // Obviously, this breaks down if we have more than 32 channels
    for (i=0; i<pRunTimeDIOChannels->Size; ++i)
    {
        /* The mask form bypasses the per-channel ownership guard the single
         * form applies upstream. Skip probe/peripheral-owned pins (SPI/#665) so
         * we never stomp their runtime Value, but record them — the command
         * must NOT silently succeed while ignoring claimed pins (contract:
         * DIO:PORt setters reject claimed channels). */
        if (DIO_ChannelBlocked(i))
        {
            blockedMask |= (uint32_t)(1u << i);
            continue;
        }
        bool value = (mask & (1 << i));
        if (SCPI_GPIOSingleStateSet(i, value) == SCPI_RES_ERR)
        {
            result = SCPI_RES_ERR;
        }
    }

    if (blockedMask != 0)
    {
        /* Push ONE execution error per command (not per channel) naming the
         * skipped pins — surfaces via SYST:ERR? like the single form, and can't
         * flood the log on a repeated mask poll. */
        /* Name the ACTUAL owner of the first skipped channel (registry has
         * SPI/I2C/UART/1-Wire/... + probe) rather than a fixed string;
         * blockedMask lists every skipped channel for follow-up. */
        uint8_t firstCh = (uint8_t)__builtin_ctz(blockedMask);
        const char* owner = DIO_ChannelBlockedReason(firstCh);
        char msg[112];
        snprintf(msg, sizeof(msg),
                 "DIO:STATE mask: skipped owned channels 0x%04X (ch %u in use by %s)",
                 (unsigned)blockedMask, (unsigned)firstCh,
                 owner ? owner : "peripheral/probe");
        SCPI_ExecutionError(context, msg);
        result = SCPI_RES_ERR;
    }

    return result;
}

static scpi_result_t SCPI_GPIOSingleStateGet(uint8_t id, bool* result)
{
    DIORuntimeArray * pRunTimeDIOChannels = BoardRunTimeConfig_Get(         
                        BOARDRUNTIMECONFIG_DIO_CHANNELS);
    
    if ( id >= pRunTimeDIOChannels->Size)
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
    
    if ( id >= pRunTimeDIOChannels->Size)
    {
        return SCPI_RES_ERR;
    }
    /* Snapshot so a blocked write rolls BOTH fields back to their pre-call
     * state (not just IsPwmActive). The whole transition — the transient
     * Value/IsPwmActive write, the hardware program, and any rollback — runs in
     * one critical section so a concurrent DIO_ClaimChannel on the other SCPI
     * interface (USB pri 7 can preempt WiFi pri 2 mid-sequence) can never
     * observe a transient IsPwmActive that this call later rolls back. Pairs
     * with the critical section in DIO_ClaimChannel to make SPI-vs-PWM
     * arbitration atomic in both directions (the fields are an RMW on a struct
     * that claim reads — the project atomicity rule requires the guard).
     * DIO_PWMWriteStateSingle is bounded register work (OCMP enable/disable +
     * PPS remap, no loops/delays), so the section stays short (~1-2 us). */
    taskENTER_CRITICAL();
    bool prevValue = pRunTimeDIOChannels->Data[id].Value;
    bool prevPwm   = pRunTimeDIOChannels->Data[id].IsPwmActive;
    pRunTimeDIOChannels->Data[id].Value       = value ? 1 : 0;
    pRunTimeDIOChannels->Data[id].IsPwmActive = value ? 1 : 0;
    bool applied = DIO_PWMWriteStateSingle(id);
    if (!applied) {
        /* Blocked — e.g. the pin was claimed by a peripheral (SPI) in a
         * concurrent cross-interface race, so DIO_PWMWriteStateSingle refused
         * to reprogram it. Roll BOTH runtime fields back: a lying IsPwmActive
         * would block the channel's DIO restore + later claims, and a stale
         * Value would drive an unintended static level once the block clears
         * and DIO_WriteStateSingle resumes for this channel. */
        pRunTimeDIOChannels->Data[id].Value       = prevValue;
        pRunTimeDIOChannels->Data[id].IsPwmActive = prevPwm;
    }
    taskEXIT_CRITICAL();
    return applied ? SCPI_RES_OK : SCPI_RES_ERR;
}

// *****************************************************************************
// Section: programmable clock outputs (#668, epic #664) — DIO:CLOCk:*
// REFCLKO on DIO 2/3/4/5/6/7/12/14/15; POSC source; achieved Hz reported.
// *****************************************************************************

scpi_result_t SCPI_DioClockConfig(scpi_t * context) {
    int32_t dio, hz;
    if (!SCPI_ParamInt32(context, &dio, TRUE) ||
        !SCPI_ParamInt32(context, &hz, TRUE)) {
        return SCPI_RES_ERR;
    }
    if (dio < 0 || dio > 15) {
        SCPI_ExecutionError(context, "DIO:CLOCK: channel out of range (0..15)");
        return SCPI_RES_ERR;
    }
    if (hz < 0) {
        SCPI_ExecutionError(context, "DIO:CLOCK: frequency must be > 0");
        return SCPI_RES_ERR;
    }
    const char* err = NULL;
    uint32_t actual = 0;
    if (!UserClock_Configure((uint8_t)dio, (uint32_t)hz, &actual, &err)) {
        SCPI_ExecutionError(context, (err != NULL) ? err : "DIO:CLOCK: config rejected");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, actual);   /* achieved Hz (quantized by the fractional divider) */
    return SCPI_RES_OK;
}

scpi_result_t SCPI_DioClockEnable(scpi_t * context) {
    int32_t dio, on;
    if (!SCPI_ParamInt32(context, &dio, TRUE) ||
        !SCPI_ParamInt32(context, &on, TRUE)) {
        return SCPI_RES_ERR;
    }
    if (dio < 0 || dio > 15) {
        SCPI_ExecutionError(context, "DIO:CLOCK: channel out of range (0..15)");
        return SCPI_RES_ERR;
    }
    const char* err = NULL;
    if (!UserClock_Enable((uint8_t)dio, on != 0, &err)) {
        SCPI_ExecutionError(context, (err != NULL) ? err : "DIO:CLOCK: enable failed");
        return SCPI_RES_ERR;
    }
    return SCPI_RES_OK;
}

scpi_result_t SCPI_DioClockGet(scpi_t * context) {
    int32_t dio;
    if (!SCPI_ParamInt32(context, &dio, TRUE)) {
        return SCPI_RES_ERR;
    }
    if (dio < 0 || dio > 15) {
        SCPI_ExecutionError(context, "DIO:CLOCK: channel out of range (0..15)");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, UserClock_GetActualHz((uint8_t)dio));   /* 0 = off */
    return SCPI_RES_OK;
}

