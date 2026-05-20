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
        Streaming_Csv=2,
    } StreamingEncoding;

    typedef enum eStreamingInterface
    {
        StreamingInterface_USB = 0,
        StreamingInterface_WiFi = 1,
        StreamingInterface_SD = 2,
        StreamingInterface_UsbAndSd = 3,  // USB+SD concurrent (WiFi excluded — SPI bus conflict with SD)
    } StreamingInterface;
    
    /**
     * Contains the board configuration for the streaming timer
     */
    typedef struct sStreamingRuntimeConfig
    {
        /**
         * Indicates whether the board is streaming.  Written by SCPI
         * start/stop paths, read from streaming_Task and the timer ISR.
         * volatile so -O3 cannot cache the read across loop iterations
         * or function-call boundaries — the #484 shutdown-race fix
         * relies on a second read mid-iteration observing a Stop that
         * landed after the top-of-loop check.  Same rationale as
         * Running below.  (PIC32MZ note: single-byte bool is byte-
         * atomic on the bus, no critical section needed for plain
         * load/store.)
         */
        volatile bool IsEnabled;
        
        /**
         * Indicates whether the streaming timer is actually ticking.
         * Written by streaming.c (Streaming_Start sets, Streaming_Stop
         * clears) and read by ADC.c:129 (EOS deferred task, pri 9),
         * SCPIInterface/SCPIADC (USB pri 7 / WiFi pri 2).  Marked
         * volatile so -O3 cannot cache reads across loop iterations
         * or function-call boundaries — per CLAUDE.md PIC32MZ
         * cross-context atomicity rules (32-bit RW atomic but
         * volatile needed for visibility).  Type widened to uint32_t per
         * the bus-native-width convention used by the other cross-context
         * shared flags (gQuesBits in streaming.c): single-instruction
         * load/store on PIC32MZ, no sub-word zero-extend overhead.
         */
        volatile uint32_t Running;
        
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

        /**
         * Which interface to stream data to (USB, WiFi, SD, or All)
         * Default: Single interface (the one that initiated streaming)
         * Multi-interface streaming (All) is available but not used by default
         */
        StreamingInterface ActiveInterface;

        /**
         * Voltage output precision for CSV and JSON encoders.
         * 0 = integer millivolts (backwards compatible, fast path)
         * 1-10 = volts with N decimal places (e.g. 4 → "1.2207")
         * Default: 4 (0.1mV resolution, preserves 12-bit ADC precision)
         * Higher values (7-10) reserved for future 32-bit ADC support.
         */
        uint8_t VoltagePrecision;

        /**
         * Enable onboard diagnostic channel scanning during streaming.
         * true (default): MODULE7 scans monitoring channels at divided rate.
         * false: Skip MODULE7 entirely for max dedicated channel throughput.
         * Controlled via CONF:ADC:OBDiag.  Runtime-only, resets on reboot.
         */
        bool OnboardDiagEnabled;

    } StreamingRuntimeConfig;

    /**
     * Dynamic memory configuration for streaming buffers.
     * Settings take effect at next StartStreamData.
     * Runtime-only (not persisted to NVM — reset on reboot to safe defaults).
     */
    typedef struct sMemoryConfig {
        uint32_t sdCircularBufSize;    // SD circular buffer (default 32768)
        uint32_t wifiCircularBufSize;  // WiFi circular buffer (default 14000)
        uint32_t usbCircularBufSize;   // USB circular buffer (default 16384)
        uint32_t encoderBufSize;       // Encoder buffer (default 8192, 0=auto)
        uint32_t samplePoolCount;      // Sample pool depth (default 700, 0=auto)
    } MemoryConfig;


#ifdef	__cplusplus
}
#endif


