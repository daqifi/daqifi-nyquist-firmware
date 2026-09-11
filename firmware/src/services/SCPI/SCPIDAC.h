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
     * Updates all DAC output latches to reflect the current values
     *   CONFigure:DAC:UPDATE - Updates all DAC channel outputs with their current values
     * @param context
     * @return
     */
    scpi_result_t SCPI_DACUpdate(scpi_t * context);

#ifdef	__cplusplus
}
#endif