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

