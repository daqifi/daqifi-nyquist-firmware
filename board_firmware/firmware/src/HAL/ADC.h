#pragma once

#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "state/data/BoardData.h"

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
    void ADC_Init( \
                    const BoardConfig *pInitBoardConfig, \
                    const BoardRuntimeConfig *pInitBoardRuntimeConfig, \
                    const BoardData *pInitBoardData );
    
    /**
     * Performs board initialization
     * @param boardConfig The hardware configuration
     * @param moduleChannels Hardware configuration for the individual channels
     */
   // bool ADC_InitHardware(const AInModule* boardConfig, const AInArray* moduleChannels);

    /**
     * Updates the state of a single module
     */
//    bool ADC_WriteModuleState(const BoardConfig* boardConfig, BoardRuntimeConfig* runtimeConfig, size_t moduleId, POWER_STATE powerState);
    
    /*! Sets the state for all ADC channels
     */
    bool ADC_WriteChannelStateAll( void );
    
    /**
     * Updates the state for a single ADC channel
     */
//    bool ADC_WriteChannelStateSingle(const BoardConfig* boardConfig, BoardRuntimeConfig* runtimeConfig, size_t channelId);
    
    /**
     * Populates the sample array using data in the board config
     * NOTE: This is intended to be non-blocking. Samples that are not ready will be skipped.
     * @param samples The array to populate
     * @param module The module to retrieve samples from
     * @param moduleRuntime Runtime data for the given module
     */
//    bool ADC_ReadSamples(AInSampleArray* samples, const AInModule* module, AInModuleRuntimeConfig* moduleRuntime);

    /**
     * Locates the first module of the given type
     * @param boardConfig The board config to search
     * @param moduleType The module to locate
     * @return A const pointer to a module
     */
    const AInModule* ADC_FindModule( AInType moduleType);
    
    /**
     * Retrieves the index of a module
     * @param boardConfig The board config to search
     * @param module The module to search for
     * @return The index of the module in the array
     */
//    uint8_t ADC_FindModuleIndex(const AInModArray* boardConfig, const AInModule* module);
    
    /**
     * Triggers a conversion for the specified module
     * @param module Hardware information for the module to trigger
     * return true on success, false otherwise
     */
    bool ADC_TriggerConversion( const AInModule* module );
    
    /**
     * Interrupt callback when an ADC conversion has completed
     * the function must check which conversion has completed.
     * @param moduleID Module number for which an ADC conversion has been completed.
     */
    void ADC_ConversionComplete( const AInModule* module );
    
    /**
     * Performs periodic tasks for ADC
     * @param boardConfig The board config to use
     * @param runtimeConfig The runtime config to use
     * @param powerState The power state of the board
     */
    void ADC_Tasks( void );
    
    /**
     * Locates an index for the given channel id
     * @param boardConfig The channel list to search
     * @param channelId The Channel id to search for
     * @return The index of the channel
     */
    size_t ADC_FindChannelIndex( uint8_t channelId );
    
    /**
     * Indicates whether data is valid for the given channel
     * @param sample The sample to check
     * @return True if the sample is valid, false otherwise
     */
//    bool ADC_IsDataValid(const AInSample* sample);
    
    /**
     * Converts the given sample to a voltage
     * NOTE: This is NOT safe to call in an ISR
     * @param sample The sample to convert
     * @return The converted voltage
     */
    double ADC_ConvertToVoltage(const AInSample* sample);
    
#ifdef	__cplusplus
}
#endif


