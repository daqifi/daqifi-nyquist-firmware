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
