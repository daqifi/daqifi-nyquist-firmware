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

#ifndef _TIMER_API_H    /* Guard against multiple inclusion */
#define _TIMER_API_H

#include "configuration.h"
#include "definitions.h"


/* Provide C++ Compatibility */
#ifdef __cplusplus
extern "C" {
#endif

void TimerApi_Initialize(uint8_t index);

void TimerApi_Start(uint8_t index);

void TimerApi_Stop(uint8_t index);

void TimerApi_PeriodSet(uint8_t index,uint16_t period);

uint16_t TimerApi_PeriodGet(uint8_t index);

uint16_t TimerApi_CounterGet(uint8_t index);

uint32_t TimerApi_FrequencyGet(uint8_t index);

void TimerApi_InterruptEnable(uint8_t index);

void TimerApi_InterruptDisable(uint8_t index);

void TimerApi_CallbackRegister( uint8_t index,TMR_CALLBACK callback_fn, uintptr_t context );

    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _EXAMPLE_FILE_NAME_H */

/* *****************************************************************************
 End of File
 */
