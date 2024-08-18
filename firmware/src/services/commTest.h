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

#ifndef _COMMTEST_H    /* Guard against multiple inclusion */
#define _COMMTEST_H


/* ************************************************************************** */
/* ************************************************************************** */
/* Section: Included Files                                                    */
/* ************************************************************************** */
/* ************************************************************************** */

/* This section lists the other files that are included in this file.
 */

/* TODO:  Include other files here if needed. */


/* Provide C++ Compatibility */
#ifdef __cplusplus
extern "C" {
#endif


    typedef struct
    {
        uint16_t TestData_len;

        union{
            uint32_t stats[4];
            struct{
                uint32_t USBOverflow;
                uint32_t TCPOverflow;
                uint32_t DIOSampleListOverflow;
                uint32_t AinSampleListOverflow;
            };
        };
    }COMMTEST;

    extern COMMTEST commTest;
    bool CommTest_FillTestData(uint8_t*, uint16_t);

    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _EXAMPLE_FILE_NAME_H */

/* *****************************************************************************
 End of File
 */
