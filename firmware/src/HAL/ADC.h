/*! @file ADC.h 
 * 
 * This file implements the interface to functions to manage the module ADC. 
 */

#pragma once

#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "state/data/BoardData.h"
#include "ADC/MC12bADC.h"
#ifdef	__cplusplus
extern "C" {
#endif

/*! This function is used for initializing ADC firmware layer, setting the 
 * board config and board runtime config data structure pointers.
 * @param[in] pInitBoardConfig Pointer to the BoardConfig data structure
 * which will be used the next times any function of ADC layer is called.
 * @param[in] pInitBoardRuntimeConfig Pointer to the BoardRuntimeConfig
 * data structure which will be used the next times any function of ADC
 * layer is called.
 * @param[in] pInitBoardData Pointer to the BoardData data structure, which
 * will be used when needed the next times any function of ADC layer is
 * called */
void ADC_Init(                                                              
                    const tBoardConfig * pBoardConfigADCInit,               
                    const tBoardRuntimeConfig* pBoardRuntimeConfigADCInit,  
                    const tBoardData* pBoardDataADCInit);

 
/*! Sets the state for all ADC channels
 */
bool ADC_WriteChannelStateAll( void );

/*! Triggers a conversion for the specified module
 * @param[in] module Hardware information for the module to trigger
 * return true on success, false otherwise
 */
bool ADC_TriggerConversion(const AInModule* module,MC12b_adcType_t adcChannelType);
    
/*! Locates the first module of the given type
 * @param moduleType The module to locate
 * @return A const pointer to a module
 */
const AInModule* ADC_FindModule( AInType moduleType);
    
/*! Interrupt callback when an ADC conversion has completed
 * the function must check which conversion has completed.
 * @param[in] moduleID Module number for which an ADC conversion
 *                     has been completed.
 */
void ADC_ConversionComplete(const AInModule* module);
    
/*! Performs periodic tasks for ADC
 */
void ADC_Tasks( void );
    
/*! Locates an index for the given channel id
 * @param[in] channelId The Channel id to search for
 * @return The index of the channel
 */
size_t ADC_FindChannelIndex(uint8_t channelId);
    
/*! Indicates whether data is valid for the given channel
 * @param[in] sample The sample to check
 * @return True if the sample is valid, false otherwise
 */
bool ADC_IsDataValid(const AInSample* sample);
    
/*! Converts the given sample to a voltage
 * NOTE: This is NOT safe to call in an ISR
 * @param sample The sample to convert
 * @return The converted voltage
 */
double ADC_ConvertToVoltage(const AInSample* sample);

bool ADC_ReadADCSampleFromISR(uint32_t value,uint8_t bufferIndex);

/*! Function to be called from the ISR for deferring the ADC interrupt */
void ADC_EOSInterruptCB( void );
    
#ifdef	__cplusplus
}
#endif


