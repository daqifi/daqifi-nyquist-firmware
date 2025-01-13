/* 
 * File:   ArrayWrapper.h
 * Author: Daniel
 *
 * Created on September 12, 2016, 2:28 PM
 */

#ifndef ARRAYWRAPPER_H
#define	ARRAYWRAPPER_H

#include "stddef.h"
#include "stdint.h"

#ifdef	__cplusplus
extern "C" {
#endif

    #define ARRAYWRAPPERDEF(ARRAYNAME, ARRAYTYPE, ARRAYMAXSIZE) typedef struct \
    {\
        size_t Size;\
        ARRAYTYPE Data[ARRAYMAXSIZE];\
    } ARRAYNAME;\

    ARRAYWRAPPERDEF(uint8_array, uint8_t, 128);

    ARRAYWRAPPERDEF(ulong_array, uint8_t, 16);
    
    ARRAYWRAPPERDEF(NanopbFlagsArray, uint8_t, 65);
    
#ifdef	__cplusplus
}
#endif

#endif	/* ARRAYWRAPPER_H */

