#pragma once

#include "SCPIInterface.h"

#ifdef	__cplusplus
extern "C" {
#endif

    /**
     * Measures an ADC voltage on one or more channels
     *   MEASure:VOLTage:DC? ${CH}- Measures the voltage on one (enabled) channel
     *   MEASure:VOLTage:DC- Measures the voltage on all enabled channels
     * 
     * MEASure:INT:ADC? and MEASure:EXT:ADC? both map to this same function
     * 
     * @param context
     * @return 
     */
    scpi_result_t SCPI_ADCVoltageGet(scpi_t * context);
    
    /**
     * Sets the enabled flag on one or more channels
     *   ENAble:VOLTage:DC ${CH} (0|1)- Sets the enabled flag on a channel to either true (1) or false (0)
     * @param context
     * @return 
     */
    scpi_result_t SCPI_ADCChanEnableSet(scpi_t * context);
    
    /**
     * Gets the enabled flag on one or more channels
     *   ENAble:VOLTage:DC? ${CH}: Gets the enabled flag on a channel
     *   ENAble:VOLTage:DC?- Gets the enabled flag on all channels
     * @param context
     * @return 
     */
    scpi_result_t SCPI_ADCChanEnableGet(scpi_t * context);
    
    /**
     * Sets the single-ended flag on one or more channels
     *   CONFigure:ADC:SINGleend ${CH} (0|1)- Sets the single-ended flag on a channel to either true (1) or false (0)
     * @param context
     * @return 
     */
    scpi_result_t SCPI_ADCChanSingleEndSet(scpi_t * context);
    
    /**
     * Gets the streaming flag on one or more channels
     *   CONFigure:ADC:SINGleend? ${CH}: Gets the single-ended flag on a channel
     *   CONFigure:ADC:SINGleend?- Gets the single-ended flag on all channels
     * @param context
     * @return 
     */
    scpi_result_t SCPI_ADCChanSingleEndGet(scpi_t * context);
    
    /**
     * Sets the range on a single channel
     *   CONFigure:ADC:RANGe ${CH} ${Enum}- Sets the range on a channel to either true (1) or false (0)
     * @param context
     * @return 
     */
    scpi_result_t SCPI_ADCChanRangeSet(scpi_t * context);
    
    /**
     * Gets the range on one or more channels
     *   CONFigure:ADC:RANGe"? ${CH}: Gets the range flag on a channel
     *   CONFigure:ADC:RANGe"?- Gets the range on all channels
     * @param context
     * @return 
     */
    scpi_result_t SCPI_ADCChanRangeGet(scpi_t * context);
    
    /**
     * Sets the m calibration value on a single channel
     *   CONFigure:ADC:chanCALM ${CH} ${Double} - Sets the m (slope) calibration value on a channel to any value
     * @param context
     * @return 
     */
    scpi_result_t SCPI_ADCChanCalmSet(scpi_t * context);
    
    /**
     * Sets the b calibration value on a single channel
     *   CONFigure:ADC:chanCALB ${CH} ${Double} - Sets the b (intercept) calibration value on a channel to any value
     * @param context
     * @return 
     */            
    scpi_result_t SCPI_ADCChanCalbSet(scpi_t * context);
    
    /**
     * Gets the m calibration value on a single channel
     *   CONFigure:ADC:chanCALM? ${CH} - Gets the m (slope) calibration value for a channel
     * @param context
     * @return 
     */              
    scpi_result_t SCPI_ADCChanCalmGet(scpi_t * context);
    
    /**
     * Gets the b calibration value on a single channel
     *   CONFigure:ADC:chanCALB? ${CH} - Gets the b (intercept) calibration value for a channel
     * @param context
     * @return 
     */     
    scpi_result_t SCPI_ADCChanCalbGet(scpi_t * context);
    
    /**
     * Saves the current ADC calibration values for all channels
     *   CONFigure:ADC:SAVEcal - Saves the current m (slope) and b (intercept) calibration values for all channels
     * @param context
     * @return 
     */ 
    scpi_result_t SCPI_ADCCalSave(scpi_t * context);

    /**
     * Saves the current ADC calibration values for all channels to the factory calibration NVM location
     *   CONFigure:ADC:SAVEFcal - Saves the current m (slope) and b (intercept) calibration values as factory values for all channels
     * @param context
     * @return 
     */ 
    scpi_result_t SCPI_ADCCalFSave(scpi_t * context);
    
    /**
     * Loads the (user) ADC calibration values for all channels
     *   CONFigure:ADC:LOADcal - Loads the user m (slope) and b (intercept) calibration values for all channels
     * @param context
     * @return 
     */     
    scpi_result_t SCPI_ADCCalLoad(scpi_t * context);

    /**
     * Loads the (factory) ADC calibration values for all channels
     *   CONFigure:ADC:LOADFcal - Loads the factory m (slope) and b (intercept) calibration values for all channels
     * @param context
     * @return 
     */  
    scpi_result_t SCPI_ADCCalFLoad(scpi_t * context);

    /**
     * Sets the global ADC calibration setting to use either factory (0) or user (1) calibration values - this also stores the setting in NVM
     *   CONFigure:ADC:USECal - Sets the global factory (0) or user (1) calibration values and stores the preference in NVM
     * @param context
     * @return 
     */      
    scpi_result_t SCPI_ADCUseCalSet(scpi_t * context);

    /**
     * Gets the global ADC calibration setting - factory (0) or user (1)
     *   CONFigure:ADC:USECal? - Returns the global ADC calibration values for all channels
     * @param context
     * @return 
     */        
    scpi_result_t SCPI_ADCUseCalGet(scpi_t * context);

#ifdef	__cplusplus
}
#endif


