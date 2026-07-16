#define LOG_LVL LOG_LEVEL_DEBUG
#define LOG_MODULE LOG_MODULE_GENERAL
/*! @file DIO.c
 *
 * This file implements the functions to manage the digital input/output
 */

#include "DIO.h"

#include "configuration.h"
#include "definitions.h"
#include "FreeRTOS.h"
#include "task.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "state/board/BoardConfig.h"
#include "TimerApi/TimerApi.h"
#include "OcmpApi/OcmpApi.h"
#include "services/streaming.h"
#include "Util/Logger.h"
#include "DioProbe.h"
//! Pointer to the board configuration. It must be set in the initialization
static tBoardConfig *gpBoardConfig;
//! Pointer to the runtime board configuration. It must be set in the initialization
static tBoardRuntimeConfig *gpRuntimeBoardConfig;

static void WriteGpioPin(GPIO_PORT port, uint32_t bitPos, uint32_t value) {
    uint32_t pin = 1 << bitPos;
    if (value == 1)
        GPIO_PortSet(port, pin);
    else
        GPIO_PortClear(port, pin);

}

static void SetGpioDir(GPIO_PORT port, uint32_t bitPos, bool isInput) {
    uint32_t pin = 1 << bitPos ;
    if (isInput)
        GPIO_PortInputEnable(port, pin);
    else
        GPIO_PortOutputEnable(port, pin);

}

bool DIO_ProbeActivatePair(uint8_t channel) {
    if (gpBoardConfig == NULL) return false;
    if (channel >= gpBoardConfig->DIOChannels.Size) return false;

    const DIOConfig* dio = &gpBoardConfig->DIOChannels.Data[channel];

    // Drive data pin LOW *before* flipping to output — guarantees a
    // known initial level on the external-header pin.
    WriteGpioPin(dio->DataChannel, dio->DataBitPos, 0);
    SetGpioDir(dio->DataChannel, dio->DataBitPos, 0);

    // Activate the external-driver enable. Polarity per DIOConfig.
    WriteGpioPin(dio->EnableChannel, dio->EnableBitPos, !dio->EnableInverted);
    SetGpioDir(dio->EnableChannel, dio->EnableBitPos, 0);
    return true;
}

void DIO_ProbeReleasePair(uint8_t channel) {
    if (gpBoardConfig == NULL) return;
    if (channel >= gpBoardConfig->DIOChannels.Size) return;

    const DIOConfig* dio = &gpBoardConfig->DIOChannels.Data[channel];

    // Park outputs at inactive levels *before* flipping direction
    // to input — avoids a spurious edge on the external header.
    WriteGpioPin(dio->DataChannel, dio->DataBitPos, 0);
    WriteGpioPin(dio->EnableChannel, dio->EnableBitPos, dio->EnableInverted);

    // Return both pins to input / high-Z. DIO_WriteStateSingle will
    // re-apply the runtime-configured state on the next streaming tick.
    SetGpioDir(dio->DataChannel, dio->DataBitPos, 1);
    SetGpioDir(dio->EnableChannel, dio->EnableBitPos, 1);
}

/* ---------------------------------------------------------------------
 * DIO channel ownership registry (#664 shared foundation)
 * ---------------------------------------------------------------------
 * gDioOwnedMask is the fast-path bitmask consulted per streaming tick and
 * in the read filter; gDioOwner[] carries the owner id for error naming.
 * Written from task context (SCPI / peripheral HAL) under a critical
 * section; the mask is read from the streaming task and the DIO write
 * paths (a single aligned 16-bit load is atomic on PIC32MZ). The DIO
 * debug probe (DioProbe) is a separate, higher-priority owner class with
 * its own tear-safe mask — DIO_ChannelBlocked ORs the two.
 * --------------------------------------------------------------------- */
static volatile uint16_t gDioOwnedMask = 0;
static volatile uint8_t  gDioOwner[MAX_DIO_CHANNEL] = {0};

bool DIO_ClaimChannel(uint8_t channel, DioChannelOwner_t owner) {
    /* Bound against the actual board channel count, not just the gDioOwner
     * array size — a channel index in [DIOChannels.Size, MAX_DIO_CHANNEL) is a
     * valid array slot but not a real board pin, so claiming it would register
     * ownership of a non-existent channel. Size is always <= MAX_DIO_CHANNEL by
     * board-config construction, so this is also a strict array-bounds guard.
     * Matches the DIO_WriteStateSingle guard added in this PR. */
    if (gpBoardConfig == NULL ||
        channel >= gpBoardConfig->DIOChannels.Size ||
        owner == DIO_OWNER_NONE) {
        return false;
    }
    bool ok = false;
    taskENTER_CRITICAL();
    /* Test the probe claim, PWM-active state, AND the peripheral owner together
     * inside the critical section. PWM does not take a registry entry (it uses
     * the IsPwmActive flag), so a claim that ignored IsPwmActive could steal a
     * pin a concurrent PWM enable just activated on the other SCPI interface,
     * then overwrite its PPS mux — a silent cross-feature corruption. Reading
     * IsPwmActive here (paired with DIO_PWMWriteStateSingle refusing to program
     * a registry-owned pin) makes SPI-vs-PWM arbitration race-free both ways. */
    if (!DioProbe_IsChannelOwned(channel) && !DIO_IsPwmActive(channel)) {
        DioChannelOwner_t cur = (DioChannelOwner_t)gDioOwner[channel];
        if (cur == DIO_OWNER_NONE || cur == owner) {
            gDioOwner[channel] = (uint8_t)owner;
            gDioOwnedMask |= (uint16_t)(1u << channel);
            ok = true;
        }
    }
    taskEXIT_CRITICAL();
    return ok;
}

void DIO_ReleaseChannel(uint8_t channel, DioChannelOwner_t owner) {
    if (channel >= MAX_DIO_CHANNEL) {
        return;
    }
    taskENTER_CRITICAL();
    if ((DioChannelOwner_t)gDioOwner[channel] == owner) {
        gDioOwner[channel] = (uint8_t)DIO_OWNER_NONE;
        gDioOwnedMask &= (uint16_t)~(1u << channel);
    }
    taskEXIT_CRITICAL();
}

DioChannelOwner_t DIO_GetChannelOwner(uint8_t channel) {
    if (channel >= MAX_DIO_CHANNEL) {
        return DIO_OWNER_NONE;
    }
    return (DioChannelOwner_t)gDioOwner[channel];
}

const char* DIO_ChannelOwnerName(DioChannelOwner_t owner) {
    switch (owner) {
        case DIO_OWNER_SPI:     return "SPI";
        case DIO_OWNER_I2C:     return "I2C";
        case DIO_OWNER_UART:    return "UART";
        case DIO_OWNER_ONEWIRE: return "1-Wire";
        case DIO_OWNER_IC:      return "InputCapture";
        case DIO_OWNER_CLOCK:   return "Clock";
        default:                return "none";
    }
}

bool DIO_ChannelBlocked(uint8_t channel) {
    if (channel >= MAX_DIO_CHANNEL) {
        return false;
    }
    if (DioProbe_IsChannelOwned(channel)) {
        return true;
    }
    return (gDioOwnedMask & (uint16_t)(1u << channel)) != 0;
}

const char* DIO_ChannelBlockedReason(uint8_t channel) {
    if (channel >= MAX_DIO_CHANNEL) {
        return NULL;
    }
    if (DioProbe_IsChannelOwned(channel)) {
        return "DIO probe";
    }
    DioChannelOwner_t owner = (DioChannelOwner_t)gDioOwner[channel];
    return (owner == DIO_OWNER_NONE) ? NULL : DIO_ChannelOwnerName(owner);
}

bool DIO_IsPwmActive(uint8_t channel) {
    if (gpRuntimeBoardConfig == NULL ||
        channel >= gpRuntimeBoardConfig->DIOChannels.Size) {
        return false;
    }
    return gpRuntimeBoardConfig->DIOChannels.Data[channel].IsPwmActive;
}

/* ---------------------------------------------------------------------
 * Peripheral pin electrical setup (#664 shared foundation)
 * See the header for the SN74LVC2G241 buffer / 100K read-path model.
 * --------------------------------------------------------------------- */
bool DIO_SetChannelPeripheralOutput(uint8_t channel) {
    if (gpBoardConfig == NULL || channel >= gpBoardConfig->DIOChannels.Size) {
        return false;
    }
    const DIOConfig* dio = &gpBoardConfig->DIOChannels.Data[channel];
    /* Data pin -> output (driven by the mapped peripheral or DIO_DriveChannel). */
    SetGpioDir(dio->DataChannel, dio->DataBitPos, 0);
    /* External buffer enabled: it drives the terminal at +5V_D. */
    WriteGpioPin(dio->EnableChannel, dio->EnableBitPos, !dio->EnableInverted);
    SetGpioDir(dio->EnableChannel, dio->EnableBitPos, 0);
    return true;
}

bool DIO_SetChannelPeripheralInput(uint8_t channel) {
    if (gpBoardConfig == NULL || channel >= gpBoardConfig->DIOChannels.Size) {
        return false;
    }
    const DIOConfig* dio = &gpBoardConfig->DIOChannels.Data[channel];
    /* Data pin -> input: reads the terminal through the 100K series R. */
    SetGpioDir(dio->DataChannel, dio->DataBitPos, 1);
    /* External buffer disabled: terminal high-Z so the slave can drive it. */
    WriteGpioPin(dio->EnableChannel, dio->EnableBitPos, dio->EnableInverted);
    SetGpioDir(dio->EnableChannel, dio->EnableBitPos, 0);
    return true;
}

bool DIO_DriveChannel(uint8_t channel, bool level) {
    if (gpBoardConfig == NULL || channel >= gpBoardConfig->DIOChannels.Size) {
        return false;
    }
    const DIOConfig* dio = &gpBoardConfig->DIOChannels.Data[channel];
    WriteGpioPin(dio->DataChannel, dio->DataBitPos, level ? 1 : 0);
    return true;
}

void DIO_RestoreChannel(uint8_t channel) {
    /* Re-apply the runtime-configured state. The caller must have released
     * its claim first, else DIO_WriteStateSingle short-circuits on the
     * still-blocked channel. */
    (void)DIO_WriteStateSingle(channel);
}

bool DIO_InitHardware(const tBoardConfig *pInitBoardConfiguration,
        const tBoardRuntimeConfig *pInitBoardRuntimeConfig) {
    bool enableInverted;
    GPIO_PORT enableChannel;
    uint8_t enableBitPos;
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
        enableBitPos = gpBoardConfig->DIOChannels.Data[ i ].EnableBitPos;

        // Disable all channels by default
        if (enableInverted) {
            WriteGpioPin(enableChannel, enableBitPos,1);
        } else {
            WriteGpioPin(enableChannel, enableBitPos,0);
        }

        SetGpioDir(enableChannel, enableBitPos,0);
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
    /* Bounds-guard the array indexing below. DIO_ChannelBlocked only rejects
     * indices >= MAX_DIO_CHANNEL, so a caller passing a value in
     * [DIOChannels.Size, MAX_DIO_CHANNEL) (e.g. channel 15 under the 15-entry
     * DIO_TIMING_TEST config) would otherwise read past the table. */
    if (gpBoardConfig == NULL || dataIndex >= gpBoardConfig->DIOChannels.Size) {
        return false;
    }
    /* Probe or a peripheral (SPI/I2C/UART/...) owns this channel and
     * drives its TRIS/LAT exclusively. Skipping here is the primary
     * isolation point; for the probe, scope-signal purity depends on it,
     * and for a peripheral it prevents the DIO path stomping the bus. */
    if (DIO_ChannelBlocked(dataIndex)) {
        return true;
    }
    bool enableInverted = gpBoardConfig->DIOChannels.Data[ dataIndex ].EnableInverted;
    GPIO_PORT enableChannel = gpBoardConfig->DIOChannels.Data[ dataIndex ]. EnableChannel;
    uint8_t enableBitPos = gpBoardConfig->DIOChannels.Data[ dataIndex ].EnableBitPos;

    GPIO_PORT dataChannel = gpBoardConfig->DIOChannels.Data[ dataIndex ].DataChannel;
    uint8_t dataBitPos = gpBoardConfig->DIOChannels.Data[ dataIndex ].DataBitPos;

    bool value = gpRuntimeBoardConfig->DIOChannels.Data[ dataIndex ].Value;
    bool isPwmRunning = gpRuntimeBoardConfig->DIOChannels.Data[ dataIndex ].IsPwmActive;
    if (isPwmRunning) {
        return 1;
    }
    if (gpRuntimeBoardConfig->DIOChannels.Data[ dataIndex ].IsInput) {
        // Set driver disabled - this value will be the value of
        // EnableInverted config parameter
        WriteGpioPin(enableChannel, enableBitPos, enableInverted);
        // Set data pin direction as input
        SetGpioDir(dataChannel, dataBitPos,1);
    } else {
        // Set driver enabled - this value will be the inverse of
        // EnableInverted config parameter
        WriteGpioPin(enableChannel, enableBitPos, !enableInverted);
        // Set driver value
        WriteGpioPin(dataChannel, dataBitPos, value);
        // Set data pin direction as output
        SetGpioDir(dataChannel, dataBitPos,0);
    }

    return true;
}

bool DIO_ReadSampleByMask(DIOSample* sample, uint32_t mask) {
    /* Filter out probe-owned channels (their toggle waveform is a debug
     * artifact, not user data) and peripheral-claimed channels (SPI/I2C/
     * UART bus lines, not DIO logic levels) — including either would
     * mislead the consumer reading DIO samples. */
    mask = DioProbe_FilterReadMask(mask) & ~(uint32_t)gDioOwnedMask;
    sample->Mask = mask;
    sample->Values = 0;
    // Set module trigger timestamp
    sample->Timestamp = TimerApi_CounterGet(gpBoardConfig->StreamingConfig.TSTimerIndex);

    size_t dataIndex = 0;
    for (dataIndex = 0; dataIndex < gpRuntimeBoardConfig->DIOChannels.Size; ++dataIndex) {
        GPIO_PORT dataChannel = gpBoardConfig->DIOChannels.Data[ dataIndex ].DataChannel;
        uint8_t dataBitPos = gpBoardConfig->DIOChannels.Data[ dataIndex ].DataBitPos;
        if (mask & (1 << dataIndex)) {
            uint8_t val;
            if (GPIO_PortRead(dataChannel) & (1 << dataBitPos)) {
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
    if (DIO_ChannelBlocked(dataIndex)) {
        return false;
    }
    bool enableInverted = gpBoardConfig->DIOChannels.Data[ dataIndex ].EnableInverted;
    GPIO_PORT enableChannel = gpBoardConfig->DIOChannels.Data[ dataIndex ]. EnableChannel;
    uint8_t enableBitPos = gpBoardConfig->DIOChannels.Data[ dataIndex ].EnableBitPos;

    bool isPwmSupported = gpBoardConfig->DIOChannels.Data[ dataIndex ].IsPwmCapable;
    uint16_t pwmPPSPinNo = gpBoardConfig->DIOChannels.Data[ dataIndex ].PwmRemapPin;
    uint8_t pwmModId = gpBoardConfig->DIOChannels.Data[ dataIndex ].PwmOcmpId;
    bool pwmState = gpRuntimeBoardConfig->DIOChannels.Data[ dataIndex ].IsPwmActive;

    if (isPwmSupported != 1) {
        return false;
    }
    if (pwmState) {
        OcmpApi_Enable(pwmModId, pwmPPSPinNo);
        WriteGpioPin(enableChannel, enableBitPos, !enableInverted);
    } else {
        OcmpApi_Disable(pwmModId, pwmPPSPinNo);
        WriteGpioPin(enableChannel, enableBitPos, enableInverted);
    }
    return true;
}

bool DIO_PWMDutyCycleSetSingle(uint8_t dataIndex) {
    if (DIO_ChannelBlocked(dataIndex)) {
        return false;
    }
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
    if (DIO_ChannelBlocked(dataIndex)) {
        return false;
    }

    const uint16_t tim3PreScalers[8] = {1, 2, 4, 8, 16, 32, 64, 256};
    uint32_t timerClock = TIMER_CLOCK_FRQ;//TimerApi_FrequencyGet(3);
    uint32_t pwmFrequency = gpRuntimeBoardConfig->DIOChannels.Data[ dataIndex ].PwmFrequency;
    // #671 defense-in-depth: never divide the shared PWM timebase by zero — a
    // MIPS integer div-by-zero traps to a general exception (device reset). The
    // SCPI boundary now range-checks the channel index (SCPIDIO.c), so a valid
    // channel's frequency is always > 0 here; this guards any other caller.
    if (pwmFrequency == 0) {
        return false;
    }
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
        /* Skip probe- or peripheral-owned channels at the loop level to
         * avoid the call overhead. DIO_WriteStateSingle also guards
         * internally. */
        if (DIO_ChannelBlocked((uint8_t)i)) {
            continue;
        }
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
            if (!DIOSampleList_PushBack(
                    streamingSamples,
                    (const DIOSample*) &streamingSample)) {
                Streaming_IncrDioDropped();
                LOG_E_SESSION(LOG_SESSION_DIO_DROP, "DIO: sample queue full");
            }
        }
    }

}
