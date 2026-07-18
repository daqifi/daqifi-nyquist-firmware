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

    /**
     * Enable/disable onboard diagnostic (monitoring) channel READS during
     * streaming.
     *   CONFigure:ADC:OBDiag <0|1>
     *     0 = EOS task skips monitoring-channel reads during streaming
     *         (SYST:INFo? rail values go stale with an age banner)
     *     1 = monitoring channels refresh every scan (default)
     * NOTE (#541 D-B): this setting also determines whether the monitoring
     * channels are INCLUDED in the session's shared-scan list (dynamic
     * ADCCSS, built at stream start) — OBDiag=1 adds 7 scan slots, which
     * lowers the in-spec scan-rate bound and therefore the advertised
     * frequency cap (visible in CONF:CAP). T1 results no longer depend on
     * the scan at all (read directly via ARDY in the deferred task); a
     * T1-only OBDiag=0 session arms no scan.
     * Rejected while streaming is active (returns EXECUTION_ERROR).
     */
    scpi_result_t SCPI_ADCOnboardDiagSet(scpi_t * context);
    scpi_result_t SCPI_ADCOnboardDiagGet(scpi_t * context);

    /**
     * #328 phase 1 — ADC acquisition-time (SAMC) runtime control.
     *   CONFigure:ADC:SAMC:DEDicated <0-1023>  — sample time for modules 0-4
     *   CONFigure:ADC:SAMC:DEDicated?          — current value
     *   CONFigure:ADC:SAMC:SHARed <0-1023>     — sample time for MODULE7
     *   CONFigure:ADC:SAMC:SHARed?             — current value
     * Actual acquisition time = (SAMC+2) TAD. At the boot clock config the
     * shared-module TAD7 is 100 ns: TCLK = 10 ns (PBCLK3 100 MHz), TQ =
     * (CONCLKDIV+1)*TCLK = 50 ns, TAD = 2*ADCDIV*TQ (DS60001320H Reg
     * 28-2/28-3; silicon-verified — docs/ADC_HW_SEMANTICS.md). Boot SAMC is
     * 100 for BOTH dedicated and shared (ADCCON2 = 0x00642001 -> SAMC=0x64).
     * Rejected while streaming is active (returns EXECUTION_ERROR).
     */
    scpi_result_t SCPI_ADCSamcDedicatedSet(scpi_t * context);
    scpi_result_t SCPI_ADCSamcDedicatedGet(scpi_t * context);
    scpi_result_t SCPI_ADCSamcSharedSet(scpi_t * context);
    scpi_result_t SCPI_ADCSamcSharedGet(scpi_t * context);

    /**
     * #670 — hardware analog threshold alarms via the ADCHS digital comparators.
     *   CONFigure:ADC:THREshold <ch>,<mode 0-4>,<lo>,<hi>  — mode 0=off, 1=below,
     *       2=above, 3=inside window, 4=outside window; lo/hi are raw 12-bit codes.
     *   CONFigure:ADC:THREshold? <ch>   — mode,lo,hi,tripCount,latched
     *   CONFigure:ADC:THREshold:CLEar [<ch>]  — clear latch+counter (no arg = all)
     * MC12b (NQ1/NQ2) channels with AN input < 32 only; rejected while streaming.
     */
    scpi_result_t SCPI_ADCThresholdSet(scpi_t * context);
    scpi_result_t SCPI_ADCThresholdGet(scpi_t * context);
    scpi_result_t SCPI_ADCThresholdClear(scpi_t * context);
    scpi_result_t SCPI_ADCThresholdDbg(scpi_t * context);  /* TEMP #670 bring-up */

#ifdef	__cplusplus
}
#endif


