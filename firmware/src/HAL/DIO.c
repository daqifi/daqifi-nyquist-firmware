/*! @file DIO.c 
 * 
 * This file implements the functions to manage the digital input/output
 */

#include "DIO.h"

#include "configuration.h"
#include "definitions.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "state/board/BoardConfig.h"
#include "TimerApi/TimerApi.h"
#include "OcmpApi/OcmpApi.h"
//! Pointer to the board configuration. It must be set in the initialization
static tBoardConfig *gpBoardConfig;
//! Pointer to the runtime board configuration. It must be set in the initialization
static tBoardRuntimeConfig *gpRuntimeBoardConfig;

static void WriteGpioPin(GPIO_PORT port, uint32_t mask, uint32_t value) {
    uint32_t pin = 1 << mask;
    if (value == 1)
        GPIO_PortSet(port, pin);
    else
        GPIO_PortClear(port, pin);

}

static void SetGpioDir(GPIO_PORT port, uint32_t mask, bool isInput) {
    uint32_t pin = 1 << mask ;
    if (isInput)
        GPIO_PortInputEnable(port, pin);
    else
        GPIO_PortOutputEnable(port, pin);

}

bool DIO_InitHardware(const tBoardConfig *pInitBoardConfiguration,
        const tBoardRuntimeConfig *pInitBoardRuntimeConfig) {
    bool enableInverted;
    GPIO_PORT enableChannel;
    GPIO_PIN enablePin;
    size_t channelsSize;
    uint8_t i = 0;

    gpBoardConfig = (tBoardConfig *) pInitBoardConfiguration;
    gpRuntimeBoardConfig = (tBoardRuntimeConfig *) pInitBoardRuntimeConfig;
    channelsSize = gpBoardConfig->DIOChannels.Size;

    // Initial condition should be handled by the pin manager
    // We can be sure by running the code here

    for (i = 0; i < channelsSize; ++i) {
        enableInverted = gpBoardConfig->DIOChannels.Data[ i ].EnableInverted;
        enableChannel = gpBoardConfig->DIOChannels.Data[ i ]. EnableChannel;
        enablePin = gpBoardConfig->DIOChannels.Data[ i ].EnablePin;

        // Disable all channels by default        
        if (enableInverted) {
            WriteGpioPin(enableChannel, enablePin,1);
        } else {
            WriteGpioPin(enableChannel, enablePin,0);
        }

        SetGpioDir(enableChannel, enablePin,0);
    }
    return true;
}

bool DIO_WriteStateAll(void) {
    size_t dataIndex;
    bool result = true;
    for (dataIndex = 0;
            dataIndex < gpRuntimeBoardConfig->DIOChannels.Size;
            ++dataIndex) {
        result &= DIO_WriteStateSingle(dataIndex);
    }

    return result;
}

bool DIO_WriteStateSingle(uint8_t dataIndex) {
    bool enableInverted = gpBoardConfig->DIOChannels.Data[ dataIndex ].EnableInverted;
    GPIO_PORT enableChannel = gpBoardConfig->DIOChannels.Data[ dataIndex ]. EnableChannel;
    GPIO_PIN enablePin = gpBoardConfig->DIOChannels.Data[ dataIndex ].EnablePin;

    GPIO_PORT dataChannel = gpBoardConfig->DIOChannels.Data[ dataIndex ].DataChannel;
    GPIO_PIN dataPin = gpBoardConfig->DIOChannels.Data[ dataIndex ].DataPin;

    bool value = gpRuntimeBoardConfig->DIOChannels.Data[ dataIndex ].Value;
    bool isPwmRunning = gpRuntimeBoardConfig->DIOChannels.Data[ dataIndex ].IsPwmActive;
    if (isPwmRunning) {
        return 1;
    }
    if (gpRuntimeBoardConfig->DIOChannels.Data[ dataIndex ].IsInput) {
        // Set driver disabled - this value will be the value of 
        // EnableInverted config parameter
        WriteGpioPin(enableChannel, enablePin, enableInverted);
        // Set data pin direction as input
        SetGpioDir(dataChannel, dataPin,1);
    } else {
        // Set driver enabled - this value will be the inverse of 
        // EnableInverted config parameter
        WriteGpioPin(enableChannel, enablePin, !enableInverted);
        // Set driver value
        WriteGpioPin(dataChannel, dataPin, value);
        // Set data pin direction as output
        SetGpioDir(dataChannel, dataPin,0);
    }

    return true;
}

bool DIO_ReadSampleByMask(DIOSample* sample, uint32_t mask) {
    sample->Mask = mask;
    sample->Values = 0;
    // Set module trigger timestamp
    sample->Timestamp = TimerApi_CounterGet(gpBoardConfig->StreamingConfig.TSTimerIndex);

    size_t dataIndex = 0;
    for (dataIndex = 0; dataIndex < gpRuntimeBoardConfig->DIOChannels.Size; ++dataIndex) {
        GPIO_PORT dataChannel = gpBoardConfig->DIOChannels.Data[ dataIndex ].DataChannel;
        GPIO_PIN dataPin = gpBoardConfig->DIOChannels.Data[ dataIndex ].DataPin;
        if (mask & (1 << dataIndex)) {
            uint8_t val;
            if (GPIO_PortRead(dataChannel)& (1 << dataPin)) {
                val = 1;
            } else {
                val = 0;
            }
            sample->Values |= (val << dataIndex);
        }
    }

    return true;
}

bool DIO_PWMWriteStateSingle(uint8_t dataIndex) {
    bool enableInverted = gpBoardConfig->DIOChannels.Data[ dataIndex ].EnableInverted;
    GPIO_PORT enableChannel = gpBoardConfig->DIOChannels.Data[ dataIndex ]. EnableChannel;
    GPIO_PIN enablePin = gpBoardConfig->DIOChannels.Data[ dataIndex ].EnablePin;

    bool isPwmSupported = gpBoardConfig->DIOChannels.Data[ dataIndex ].IsPwmCapable;
    uint16_t pwmPPSPinNo = gpBoardConfig->DIOChannels.Data[ dataIndex ].PwmRemapPin;
    uint8_t pwmModId = gpBoardConfig->DIOChannels.Data[ dataIndex ].PwmOcmpId;
    bool pwmState = gpRuntimeBoardConfig->DIOChannels.Data[ dataIndex ].IsPwmActive;

    if (isPwmSupported != 1) {
        return false;
    }
    if (pwmState) {
        OcmpApi_Enable(pwmModId, pwmPPSPinNo);
        WriteGpioPin(enableChannel, enablePin, !enableInverted);
    } else {
        OcmpApi_Disable(pwmModId, pwmPPSPinNo);
        WriteGpioPin(enableChannel, enablePin, enableInverted);
    }
    return true;
}

bool DIO_PWMDutyCycleSetSingle(uint8_t dataIndex) {
    uint8_t pwmDriverInstance = gpBoardConfig->DIOChannels.Data[ dataIndex ].PwmOcmpId;
    uint32_t timerFreq = TimerApi_FrequencyGet(3);
    uint16_t pwmDutyCycle = gpRuntimeBoardConfig->DIOChannels.Data[ dataIndex ].PwmDutyCycle;
    uint32_t pwmFrequency = gpRuntimeBoardConfig->DIOChannels.Data[ dataIndex ].PwmFrequency;
    if (pwmFrequency == 0 || pwmDutyCycle == 0) {
        return false;
    }
    uint16_t period = (timerFreq / pwmFrequency)*(pwmDutyCycle / 100.00);
    OcmpApi_CompareValueSet(pwmDriverInstance, period);
    return true;
}

bool DIO_PWMFrequencySet(uint8_t dataIndex) {

    const uint16_t tim3PreScalers[8] = {1, 2, 4, 8, 16, 32, 64, 256};
    uint32_t timerClock = TIMER_CLOCK_FRQ;//TimerApi_FrequencyGet(3);
    uint32_t pwmFrequency = gpRuntimeBoardConfig->DIOChannels.Data[ dataIndex ].PwmFrequency;
    uint32_t timer3ScaledClock = timerClock;
    uint64_t temp;
    uint8_t preScalerIndex = (sizeof (tim3PreScalers) / sizeof (tim3PreScalers[0])) - 1;
    uint16_t period = 2000;
    temp = period*pwmFrequency; //2000 is kept as the ideal minimum period value
    for (; preScalerIndex > 0; preScalerIndex--) {
        timer3ScaledClock = timerClock / tim3PreScalers[preScalerIndex];
        if (timer3ScaledClock > temp) {
            break;
        }
    }
    timer3ScaledClock = timerClock / tim3PreScalers[preScalerIndex];
    period = timer3ScaledClock / pwmFrequency;
    TimerApi_Stop(3);
    TimerApi_PreScalerSet(3, preScalerIndex);
    TimerApi_PeriodSet(3, period);
    TimerApi_Start(3);

    return true;
}

void DIO_StreamingTrigger(DIOSample* latest, DIOSampleList* streamingSamples) {
    //    // For debugging streaming frequency only!
    //    runtimeConfig->Data[0].Value = !runtimeConfig->Data[0].Value;

    DIORuntimeArray* DIOChruntimeConfig;
    DIOChruntimeConfig = &gpRuntimeBoardConfig->DIOChannels;

    size_t i = 0;

    // Write DIO values
    for (i = 0; i < DIOChruntimeConfig->Size; ++i) {
        DIO_WriteStateSingle(i);
    }

    // Read DIO values
    if (DIO_ReadSampleByMask(latest, 0xFFFF)) {
        // If streaming and the DIO is globally enabled, push the values onto the list
        if (gpRuntimeBoardConfig->DIOGlobalEnable &&
                gpRuntimeBoardConfig->StreamingConfig.IsEnabled) {
            DIOSample streamingSample;
            streamingSample.Mask = 0xFFFF;
            streamingSample.Values = latest->Values;
            streamingSample.Timestamp = latest->Timestamp;
            DIOSampleList_PushBack(
                    streamingSamples,
                    (const DIOSample*) &streamingSample);
        }
    }

}
