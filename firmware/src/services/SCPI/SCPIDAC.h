#pragma once

#include "SCPIInterface.h"

#ifdef	__cplusplus
extern "C" {
#endif

    /**
     * Sets an analog output voltage on one or more DAC channels
     *   SOURce:VOLTage:LEVel ${CH},${VOLTAGE} - Sets the voltage on a specific channel
     *   SOURce:VOLTage:LEVel ${VOLTAGE} - Sets the voltage on all enabled channels
     * 
     * @param context
     * @return 
     */
    scpi_result_t SCPI_DACVoltageSet(scpi_t * context);
    
    /**
     * Gets the analog output voltage on one or more DAC channels
     *   SOURce:VOLTage:LEVel? ${CH} - Gets the voltage on a specific channel
     *   SOURce:VOLTage:LEVel? - Gets the voltage on all channels
     * 
     * @param context
     * @return 
     */
    scpi_result_t SCPI_DACVoltageGet(scpi_t * context);

    /**
     * Sets the enabled flag on one or more DAC channels
     *   ENAble:SOURce:DC ${CH} (0|1) - Sets the enabled flag on a channel to either true (1) or false (0)
     * @param context
     * @return 
     */
    scpi_result_t SCPI_DACChanEnableSet(scpi_t * context);
    
    /**
     * Gets the enabled flag on one or more DAC channels
     *   ENAble:SOURce:DC? ${CH} - Gets the enabled flag on a channel
     *   ENAble:SOURce:DC? - Gets the enabled flag on all channels
     * @param context
     * @return 
     */
    scpi_result_t SCPI_DACChanEnableGet(scpi_t * context);
    
    /**
     * Sets the range on a single DAC channel
     *   CONFigure:DAC:RANGe ${CH} ${Enum} - Sets the range on a channel
     * @param context
     * @return 
     */
    scpi_result_t SCPI_DACChanRangeSet(scpi_t * context);
    
    /**
     * Gets the range on one or more DAC channels
     *   CONFigure:DAC:RANGe? ${CH} - Gets the range on a channel
     *   CONFigure:DAC:RANGe? - Gets the range on all channels
     * @param context
     * @return 
     */
    scpi_result_t SCPI_DACChanRangeGet(scpi_t * context);
    
    /**
     * Sets the m calibration value on a single DAC channel
     *   CONFigure:DAC:chanCALM ${CH} ${Double} - Sets the m (slope) calibration value on a channel
     * @param context
     * @return 
     */
    scpi_result_t SCPI_DACChanCalmSet(scpi_t * context);
    
    /**
     * Sets the b calibration value on a single DAC channel
     *   CONFigure:DAC:chanCALB ${CH} ${Double} - Sets the b (intercept) calibration value on a channel
     * @param context
     * @return 
     */            
    scpi_result_t SCPI_DACChanCalbSet(scpi_t * context);
    
    /**
     * Gets the m calibration value on a single DAC channel
     *   CONFigure:DAC:chanCALM? ${CH} - Gets the m (slope) calibration value for a channel
     * @param context
     * @return 
     */              
    scpi_result_t SCPI_DACChanCalmGet(scpi_t * context);
    
    /**
     * Gets the b calibration value on a single DAC channel
     *   CONFigure:DAC:chanCALB? ${CH} - Gets the b (intercept) calibration value for a channel
     * @param context
     * @return 
     */     
    scpi_result_t SCPI_DACChanCalbGet(scpi_t * context);
    
    /**
     * Saves the current DAC calibration values for all channels
     *   CONFigure:DAC:SAVEcal - Saves the current m (slope) and b (intercept) calibration values for all channels
     * @param context
     * @return 
     */ 
    scpi_result_t SCPI_DACCalSave(scpi_t * context);

    /**
     * Saves the current DAC calibration values for all channels to the factory calibration NVM location
     *   CONFigure:DAC:SAVEFcal - Saves the current m (slope) and b (intercept) calibration values as factory values for all channels
     * @param context
     * @return 
     */ 
    scpi_result_t SCPI_DACCalFSave(scpi_t * context);
    
    /**
     * Loads the (user) DAC calibration values for all channels
     *   CONFigure:DAC:LOADcal - Loads the user m (slope) and b (intercept) calibration values for all channels
     * @param context
     * @return 
     */     
    scpi_result_t SCPI_DACCalLoad(scpi_t * context);

    /**
     * Loads the (factory) DAC calibration values for all channels
     *   CONFigure:DAC:LOADFcal - Loads the factory m (slope) and b (intercept) calibration values for all channels
     * @param context
     * @return 
     */  
    scpi_result_t SCPI_DACCalFLoad(scpi_t * context);

    /**
     * Sets the global DAC calibration setting to use either factory (0) or user (1) calibration values - this also stores the setting in NVM
     *   CONFigure:DAC:USECal - Sets the global factory (0) or user (1) calibration values and stores the preference in NVM
     * @param context
     * @return 
     */      
    scpi_result_t SCPI_DACUseCalSet(scpi_t * context);

    /**
     * Gets the global DAC calibration setting - factory (0) or user (1)
     *   CONFigure:DAC:USECal? - Returns the global DAC calibration setting
     * @param context
     * @return 
     */        
    scpi_result_t SCPI_DACUseCalGet(scpi_t * context);

    /**
     * Updates all DAC output latches to reflect the current values
     *   CONFigure:DAC:UPDATE - Updates all DAC channel outputs with their current values
     * @param context
     * @return 
     */        
    scpi_result_t SCPI_DACUpdate(scpi_t * context);

#ifdef	__cplusplus
}
#endif