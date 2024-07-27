/* ************************************************************************** */
/** Descriptive File Name

  @Company
    Company Name

  @File Name
    filename.c

  @Summary
    Brief description of the file.

  @Description
    Describe the purpose of this file.
 */
/* ************************************************************************** */

#include "TimerApi.h"


void TimerApi_Initialize(uint8_t index) {
    switch (index) {
        case 2:
            TMR2_Initialize();
            break;
        case 3:
            TMR3_Initialize();
            break;
        case 4:
            TMR4_Initialize();
            break;
        case 6:
            TMR6_Initialize();
            break;
        default:
            break;
    }
}

void TimerApi_Start(uint8_t index) {
    switch (index) {
        case 2:
            TMR2_Start();
            break;
        case 3:
            TMR3_Start();
            break;
        case 4:
            TMR4_Start();
            break;
        case 6:
            TMR6_Start();
            break;
        default:
            break;
    }
}

void TimerApi_Stop(uint8_t index) {
    switch (index) {
        case 2:
            TMR2_Stop();
            break;
        case 3:
            TMR3_Stop();
            break;
        case 4:
            TMR4_Stop();
            break;
        case 6:
            TMR6_Stop();
            break;
        default:
            break;
    }
}

void TimerApi_PeriodSet(uint8_t index, uint16_t period) {
    switch (index) {
        case 2:
            TMR2_PeriodSet(period);
            break;
        case 3:
            TMR3_PeriodSet(period);
            break;
        case 4:
            TMR4_PeriodSet(period);
            break;
        case 6:
            TMR6_PeriodSet(period);
            break;
        default:
            break;
    }
}

uint16_t TimerApi_PeriodGet(uint8_t index) {
    uint16_t ret = 0;
    switch (index) {
        case 2:
            ret = TMR2_PeriodGet();
            break;
        case 3:
            ret = TMR3_PeriodGet();
            break;
        case 4:
            ret = TMR4_PeriodGet();
            break;
        case 6:
            ret = TMR6_PeriodGet();
            break;
        default:
            break;
    }
    return ret;
}

uint16_t TimerApi_CounterGet(uint8_t index) {
    uint16_t ret = 0;
    switch (index) {
        case 2:
            ret = TMR2_CounterGet();
            break;
        case 3:
            ret = TMR3_CounterGet();
            break;
        case 4:
            ret = TMR4_CounterGet();
            break;
        case 6:
            ret = TMR6_CounterGet();
            break;
        default:
            break;
    }
    return ret;
}

uint32_t TimerApi_FrequencyGet(uint8_t index) {
    uint32_t ret = 0;
    switch (index) {
        case 2:
            ret = TMR2_FrequencyGet();
            break;
        case 3:
            ret = TMR3_FrequencyGet();
            break;
        case 4:
            ret = TMR4_FrequencyGet();
            break;
        case 6:
            ret = TMR6_FrequencyGet();
            break;
        default:
            break;
    }
    return ret;
}

void TimerApi_InterruptEnable(uint8_t index) {
    switch (index) {
        case 2:
            TMR2_InterruptEnable();
            break;
        case 3:
            TMR3_InterruptEnable();
            break;
        case 4:            
            TMR4_InterruptEnable();
            break;
        case 6:            
            TMR6_InterruptEnable();
            break;
        default:
            break;
    }
}

void TimerApi_InterruptDisable(uint8_t index) {
    switch (index) {
        case 2:
            TMR2_InterruptDisable();
            break;
        case 3:
            TMR3_InterruptDisable();
            break;
        case 4:         
            TMR4_InterruptDisable();
            break;
        case 6:
            TMR6_InterruptDisable();
            break;
        default:
            break;
    }
}

void TimerApi_CallbackRegister(uint8_t index, TMR_CALLBACK callback_fn, uintptr_t context) {
    switch (index) {
        case 2:
            TMR2_CallbackRegister(callback_fn, context);
            break;
        case 3:
            TMR3_CallbackRegister(callback_fn, context);
            break;
        case 4:
            TMR4_CallbackRegister(callback_fn,context);
            break;
        case 6:
            TMR6_CallbackRegister(callback_fn,context);
            break;
        default:
            break;
    }
}