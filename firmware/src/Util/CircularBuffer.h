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
    volatile uint32_t    totalBytes;  // volatile: single-producer (AddBytes +=)
                                      // single-consumer (ProcessBytes -=). RMW is
                                      // safe: only one writer per direction.
    uint8_t*    buf_ptr;
    uint32_t    buf_size;
    int        (*process_callback)(uint8_t*, uint32_t);
    bool        _ownsMemory;  // true if buf_ptr was allocated by CircularBuf_Init
}CircularBuf_t;


void     CircularBuf_Init(CircularBuf_t*, int (*fp)(uint8_t*,uint32_t), uint32_t);
void     CircularBuf_InitExternal(CircularBuf_t*, int (*fp)(uint8_t*,uint32_t), uint8_t* buf, uint32_t size);
void     CircularBuf_Deinit(CircularBuf_t*);
bool     CircularBuf_Resize(CircularBuf_t*, uint32_t newSize);
uint32_t CircularBuf_AddBytes(CircularBuf_t*, uint8_t*, uint32_t);
uint32_t CircularBuf_NumBytesAvailable(CircularBuf_t*);
uint32_t CircularBuf_NumBytesFree(CircularBuf_t*);
uint32_t CircularBuf_ProcessBytes(CircularBuf_t*,uint8_t*, uint32_t,int*);
void CircularBuf_Reset(CircularBuf_t* cirbuf);
    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _EXAMPLE_FILE_NAME_H */

/* *****************************************************************************
 End of File
 */
