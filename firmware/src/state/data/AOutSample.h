#pragma once

#include <stdint.h>

#include "Util/ArrayWrapper.h"
#include "../board/AOutConfig.h"

#ifdef __cplusplus
extern "C" {
#endif

    /**
     * Contains analog output data for a DAC channel
     */
    typedef struct s_AOutSample {
        /**
         * #1034: the processor tick count this Voltage was last commanded
         * (SCPI_DACVoltageSet) or otherwise known good. 0 means "not known"
         * -- either never commanded since boot, or invalidated by
         * DAC_EnsureHardwareInitialized() (SCPIDAC.c) the moment the DAC7718
         * most recently (re)initialised. A reinit (e.g. after a power cycle)
         * resets every physical output to its hardware reset state, which
         * this project cannot determine from source alone, so the cache is
         * cleared rather than left claiming the pre-reinit voltage is still
         * current. Same Timestamp==0-means-unknown convention as AInSample
         * (AInSample.h). Readers: SCPI_DACVoltageGet (SCPIDAC.c).
         */
        uint32_t Timestamp;

        /**
         * The DAC channel that this sample corresponds to
         */
        uint8_t Channel;

        /**
         * The commanded voltage value for this channel
         */
        double Voltage;
    } AOutSample;

    // Define a storage class for analog output samples
    // MAX_AOUT_CHANNEL defined in AOutConfig.h
    ARRAYWRAPPERDEF(AOutSampleArray, AOutSample, MAX_AOUT_CHANNEL);

#ifdef __cplusplus
}
#endif
