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

