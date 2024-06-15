#ifndef _CIRCULAR_BUFFER_H    /* Guard against multiple inclusion */
#define _CIRCULAR_BUFFER_H


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
typedef struct s_CircularBuf
{
    uint8_t*    insertPtr;
    uint8_t*    removePtr;
    uint16_t    totalBytes;
    uint8_t*    buf_ptr;
    uint16_t    buf_size;
    int        (*process_callback)(uint8_t*, uint16_t);
}CircularBuf;


void     CircularBuf_Init(CircularBuf*, int (*fp)(uint8_t*,uint16_t), uint16_t);
uint16_t CircularBuf_AddBytes(CircularBuf*, uint8_t*, uint16_t);
uint16_t CircularBuf_NumBytesAvailable(CircularBuf*);
uint16_t CircularBuf_NumBytesFree(CircularBuf*);
uint16_t CircularBuf_ProcessBytes(CircularBuf*,uint8_t*, uint16_t,int*);
    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _EXAMPLE_FILE_NAME_H */

/* *****************************************************************************
 End of File
 */
