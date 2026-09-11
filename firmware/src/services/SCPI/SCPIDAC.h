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