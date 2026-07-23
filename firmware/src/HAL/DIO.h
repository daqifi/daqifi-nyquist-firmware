/* 
 * File:   DIO.h
 * Author: Daniel
 * The DIO Driver using the new state functions
 */

#pragma once

#include "state/board/DIOConfig.h"
#include "state/runtime/DIORuntimeConfig.h"
#include "state/data/DIOSample.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "state/board/BoardConfig.h"

#ifdef	__cplusplus
extern "C" {
#endif
    
/*!
 * Performs board initialization
 * @param pInitBoardConfiguration Board configuration data structure
 * @param[in] pInitBoardRuntmeConfig Board runtime configuration data
 * structure
 */
bool DIO_InitHardware( const tBoardConfig *pInitBoardConfiguration,         
                        const tBoardRuntimeConfig *pInitBoardRuntimeConfig );

/*!
 * Sets the initial state for all DIO channelss
 */
bool DIO_WriteStateAll( void );
    
/*!
 * Updates the state for a single DIO channel
 * @param[in] Data channel index
 */
bool DIO_WriteStateSingle( uint8_t dataIndex );

/* ---------------------------------------------------------------------
 * Debug / probe overrides
 * ---------------------------------------------------------------------
 * These bypass the runtime DIOChannels.Data[].IsInput / .Value fields
 * and force a pin-pair state directly from the HAL. Intended for the
 * DIO debug probe framework (HAL/DioProbe.c) and any other debug tool
 * that needs to commandeer a DIO channel independently of the user's
 * runtime configuration. For normal user-facing DIO operations, use
 * DIO_WriteStateSingle / DIO_WriteStateAll instead — those respect the
 * runtime config.
 * --------------------------------------------------------------------- */

/*!
 * Force a DIO channel's data+enable pin pair into an active digital
 * output driven LOW, bypassing the runtime IsInput/Value config.
 * Drives BOTH the data pin and the paired external-driver enable
 * pin — data+enable must always be configured together, per DIOConfig
 * semantics (Nyquist DIO drives an external buffer IC with a per-
 * channel enable).
 *
 * @warning Callers take ownership of the pin pair. The runtime DIO
 *          config is not updated, so DIO_WriteStateSingle on the next
 *          streaming tick would re-apply the user's config and stomp
 *          this override. Use DIO_ProbeReleasePair when done, and/or
 *          arrange for DIO_StreamingTrigger to skip the channel (e.g.
 *          via DioProbe_IsChannelOwned).
 *
 * @param[in] channel  DIO channel index (0..15)
 * @return true on success, false if channel is out of range.
 */
bool DIO_ProbeActivatePair(uint8_t channel);

/*!
 * Release a DIO channel's data+enable pair after DIO_ProbeActivatePair.
 * Parks outputs at their inactive levels (data LOW, enable INACTIVE per
 * EnableInverted), then returns both pins to input / high-Z so the
 * normal DIO path (DIO_WriteStateSingle) can cleanly re-apply the
 * runtime-configured state — including the case where the user had
 * the channel configured as an input.
 *
 * @warning Pair only with DIO_ProbeActivatePair. Calling this on a
 *          channel under normal user control will transiently break
 *          its driver state until the next DIO_StreamingTrigger tick
 *          re-applies the runtime config.
 *
 * @param[in] channel  DIO channel index (0..15)
 */
void DIO_ProbeReleasePair(uint8_t channel);
    
/*!
 * Generates a sample based all enabled samples included in the mask
 * @param sample The sample to populate
 * @param mask Defines the channels that will be included
 */
bool DIO_ReadSampleByMask(DIOSample* sample, uint32_t mask);

/*!
 * Read the raw logic level of one DIO channel's data pin, bypassing the
 * owned-mask filter in DIO_ReadSampleByMask (so a peripheral-claimed pin can
 * still be sampled). Bounds-guarded.
 * @param channel DIO channel index
 * @param level   [out] true = high, false = low (untouched on failure)
 * @return true on success, false if out of range / not configured
 */
bool DIO_ReadChannelLevel(uint8_t channel, bool* level);

/*!
 * Performs periodic tasks for DIO
 * @param latest Storage for the latest values
 * @param streamingSamples Storage for the latest streaming values
 */
void DIO_StreamingTrigger( DIOSample* latest, DIOSampleList* streamingSamples );

/*!
 * Enable PWM on a single PWM channel
 * @param[in] Data channel index
 */
bool DIO_PWMWriteStateSingle( uint8_t dataIndex );
/*!
 * Set duty cycle of PWM channel
 * @param[in] Data channel index
 */
bool DIO_PWMDutyCycleSetSingle(uint8_t dataIndex);

/*!
 * Set frequency of the PWM timer
 * @param[in] Data channel index
 */
bool DIO_PWMFrequencySet(uint8_t dataIndex);

/* ---------------------------------------------------------------------
 * DIO channel ownership registry (#664 shared foundation)
 * ---------------------------------------------------------------------
 * A general claim/release layer so peripheral features built on the user
 * DIO terminal (SPI #665, I2C #15, UART #16, 1-Wire #669, ...) can reserve
 * the pins they drive. A claimed channel is skipped by the DIO streaming
 * write path and rejected by the SCPI DIO:PORt / PWM setters (which name
 * the owner). This generalizes the probe-specific DioProbe_IsChannelOwned
 * into a multi-owner registry; the DIO debug probe keeps its own tear-safe
 * fast path and is treated here as an additional, higher-priority owner
 * class (see DIO_ChannelBlocked / DIO_ChannelBlockedReason).
 * --------------------------------------------------------------------- */

/*! Peripheral that has claimed a DIO channel. */
typedef enum {
    DIO_OWNER_NONE = 0,   //!< unclaimed by a peripheral
    DIO_OWNER_SPI,        //!< user SPI1 master (#665)
    DIO_OWNER_I2C,        //!< user I2C hub (#15)
    DIO_OWNER_UART,       //!< user UART (#16)
    DIO_OWNER_ONEWIRE,    //!< user 1-Wire master (#669)
    DIO_OWNER_IC,         //!< input capture — DIO:MEASure (#666)
    DIO_OWNER_CLOCK,      //!< clock output (#668)
    DIO_OWNER_EDGE,       //!< edge events + pulse totalizers — DIO:EVENt/COUNter (#667).
                          //!< Distinct from DIO_OWNER_IC so IC and edge features are
                          //!< mutually exclusive on a shared pin (e.g. DIO5 = INT3 & IC3):
                          //!< a same-pin claim by the other family is rejected, not aliased.
} DioChannelOwner_t;

/*! Claim a DIO channel for a peripheral. Fails if the index is out of
 *  range, the DIO debug probe owns the channel, or it is already claimed
 *  by a *different* peripheral (idempotent for the same owner). Thread-safe
 *  (task-context critical section).
 *  @return true if the channel is now owned by @p owner. */
bool DIO_ClaimChannel(uint8_t channel, DioChannelOwner_t owner);

/*! Release a channel previously claimed via DIO_ClaimChannel. No-op unless
 *  the channel is currently owned by @p owner (a peripheral cannot release
 *  another peripheral's claim). */
void DIO_ReleaseChannel(uint8_t channel, DioChannelOwner_t owner);

/*! Current peripheral owner of a channel (DIO_OWNER_NONE if not claimed by
 *  a peripheral). Does not report DIO-probe ownership. */
DioChannelOwner_t DIO_GetChannelOwner(uint8_t channel);

/*! Human-readable owner name for SCPI error messages ("SPI", "I2C", ...). */
const char* DIO_ChannelOwnerName(DioChannelOwner_t owner);

/*! True if a channel is unavailable for normal DIO use — owned by the
 *  debug probe OR claimed by a peripheral. Hot-path helper used by the DIO
 *  write/read/streaming paths. */
bool DIO_ChannelBlocked(uint8_t channel);

/*! Why a channel is blocked, as a static string for error messages, or
 *  NULL if it is free for normal DIO use. */
const char* DIO_ChannelBlockedReason(uint8_t channel);

/*! True if PWM (output-compare) is currently active on a channel. Lets a
 *  peripheral reject a pin-claim rather than silently leaving the OC output
 *  driving the pad. */
bool DIO_IsPwmActive(uint8_t channel);

/* ---------------------------------------------------------------------
 * Peripheral pin electrical setup (#664 shared foundation)
 * ---------------------------------------------------------------------
 * The DIO terminal channel is one unidirectional SN74LVC2G241 buffer half:
 * enable(OE) active => the PIC pin drives the terminal at the +5V_D rail;
 * enable inactive => terminal high-Z and the PIC pin reads the terminal
 * through a 100K series resistor (input mode). These helpers apply that
 * model for a claimed channel so SPI/UART/I2C don't each re-derive the
 * buffer polarity. Call only on a channel already claimed by the caller.
 * --------------------------------------------------------------------- */

/*! Data pin -> output, external buffer enabled (drives the terminal). Used
 *  for peripheral outputs: SPI SCK/MOSI/CS, UART TX. The data pin's value
 *  is driven either by the mapped peripheral (SCK/MOSI) or by
 *  DIO_DriveChannel (a software CS). */
bool DIO_SetChannelPeripheralOutput(uint8_t channel);

/*! Data pin -> input, external buffer disabled (terminal high-Z; the pin
 *  reads the terminal through the 100K series resistor). Used for
 *  peripheral inputs: SPI MISO, UART RX. */
bool DIO_SetChannelPeripheralInput(uint8_t channel);

/*! Drive a peripheral-output channel's data pin (e.g. a software CS line).
 *  Only meaningful after DIO_SetChannelPeripheralOutput on @p channel. */
bool DIO_DriveChannel(uint8_t channel, bool level);

/*! Restore a channel to its runtime-configured DIO state. Call after
 *  releasing a peripheral claim so the normal DIO path resumes cleanly. */
void DIO_RestoreChannel(uint8_t channel);



#ifdef  DIO_TIMING_TEST
#define DIO_TIMING_TEST_INIT()    \
                                    ({  \
                                        uint32_t pin0=1<<(DIO_0_PIN & 15);  \
                                        uint32_t pin0En= 1<<(DIO_EN_0_PIN & 15);    \
                                        GPIO_PortClear(GPIO_PORT_D, pin0); \
                                        GPIO_PortSet(GPIO_PORT_D, pin0En); \
                                        GPIO_PortOutputEnable(GPIO_PORT_D, pin0);   \
                                        })
                                    
#define DIO_TIMING_TEST_WRITE_STATE(state) \
                                    ({\
                                        uint32_t pin0=1<<(DIO_0_PIN & 15);  \
                                        if(state)   \
                                            GPIO_PortSet(GPIO_PORT_D, pin0); \
                                        else    \
                                            GPIO_PortClear(GPIO_PORT_D, pin0); \
                                    })
#define DIO_TIMING_TEST_TOGGLE_STATE() \
                                    ({\
                                        volatile static bool state=true; \
                                        state=!state; \
                                        uint32_t pin0=1<<(DIO_0_PIN & 15);  \
                                        if(state)   \
                                            GPIO_PortSet(GPIO_PORT_D, pin0); \
                                        else    \
                                            GPIO_PortClear(GPIO_PORT_D, pin0); \
                                    })                                    
                   
#else
#define     DIO_TIMING_TEST_INIT()    ({})
#define     DIO_TIMING_TEST_WRITE_STATE(state)  ({})
#define     DIO_TIMING_TEST_TOGGLE_STATE()      ({})
#endif
    
#ifdef	__cplusplus
}
#endif

