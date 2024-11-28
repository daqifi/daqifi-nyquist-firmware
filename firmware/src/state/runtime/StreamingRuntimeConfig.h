#pragma once

#include "configuration.h"
#include "definitions.h"

#ifdef	__cplusplus
extern "C" {
#endif

    typedef enum eStreamingEncoding
    {
        Streaming_ProtoBuffer = 0,
        Streaming_Json = 1,        
    } StreamingEncoding;
    
    /**
     * Contains the board configuration for the streaming timer
     */
    typedef struct sStreamingRuntimeConfig
    {
        /**
         * Indicates whether the board is streaming
         */
        bool IsEnabled;
        
        /**
         * Indicates whether the alarm has been registered
         * TODO: This probably belongs in 'Data'
         */
        bool Running;
        
        /**
         * The base clock divider
         */
        uint32_t ClockPeriod;
        /**
         *Base Frequency of adc Sampling and streaming
         */
        uint64_t Frequency;
        /**
         *Divider for channel Scanning frequency. 
         * This is used to divide the Frequency of streaming to get
         * the frequency of channel scanning trigger
         */
        uint64_t ChannelScanFreqDiv;
        /**
         * The base clock divider for the timestamp timer
         */
        uint32_t TSClockPeriod;
        
//        /**
//         * The number of times through the loop before we do a system read
//         * TODO: Every module (and eventually channel) needs one of these
//         */
//        volatile uint32_t StreamCountTrigger;
//        
//        /**
//         * The current iteration in the streaming loop
//         * TODO: This probably belongs in 'Data'
//         */
//        volatile uint32_t StreamCount;
//        
//        /**
//         * The maximum value of stream count
//         * TODO: This probably belongs in 'Data'
//         */
//        volatile uint32_t MaxStreamCount;
        
        /**
         * The type of encoding to use
         */
        StreamingEncoding Encoding;
        
    } StreamingRuntimeConfig;


#ifdef	__cplusplus
}
#endif


