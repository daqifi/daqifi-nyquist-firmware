/* ************************************************************************** */
/** Descriptive File Name

  @Company
    Company Name

  @File Name
    filename.h

  @Summary
    Brief description of the file.

  @Description
    Describe the purpose of this file.
 */
/* ************************************************************************** */

#ifndef _RUNTIMESTATS_H    /* Guard against multiple inclusion */
#define _RUNTIMESTATS_H


/* ************************************************************************** */
/* ************************************************************************** */
/* Section: Included Files                                                    */
/* ************************************************************************** */
/* ************************************************************************** */
#include <stdint.h>
#include <stdbool.h>
/* This section lists the other files that are included in this file.
 */

/* TODO:  Include other files here if needed. */


/* Provide C++ Compatibility */
#ifdef __cplusplus
extern "C" {
#endif


    /* ************************************************************************** */
    /* ************************************************************************** */
    /* Section: Constants                                                         */
    /* ************************************************************************** */
    /* ************************************************************************** */

    /*  A brief description of a section can be given directly below the section
        banner.
     */
    typedef struct s_RunTimeData
    {
        uint32_t NumBytesWrittenUsbCdc;
        uint32_t NumBytesStreamToUsbBuf;
        uint32_t NumBytesScpiToUsbBuf;
        uint32_t usbCdcTransferStartCount;
        uint32_t usbCdcTransferCompleteCount;
        
        struct{
            uint32_t startticks;
            uint32_t bytesWritten;
            float    dataRatePerSecond; //kbytes per second
            uint16_t bufsize;
            uint8_t  buf[1000];
            bool     restart;
        }StressTest_Usb;
    }RunTimeData;
    
    extern RunTimeData runTimeStats;
    
    void RunTimeStats_Initialize(void);
    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _EXAMPLE_FILE_NAME_H */

/* *****************************************************************************
 End of File
 */
