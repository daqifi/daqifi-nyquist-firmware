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

void TimerApi_PeriodSet(uint8_t index, uint32_t period) {
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

uint32_t TimerApi_PeriodGet(uint8_t index) {
    uint32_t ret = 0;
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

uint32_t TimerApi_CounterGet(uint8_t index) {
    uint32_t ret = 0;
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

void TimerApi_PreScalerSet(uint8_t index, timerApi_presScale_t preScale) {
    switch (index) {
        case 2:
            T2CONbits.TCKPS = preScale;
            break;
        case 3:
            T3CONbits.TCKPS = preScale;
            break;
        case 4:
            T4CONbits.TCKPS = preScale;
            break;
        case 6:
            T6CONbits.TCKPS = preScale;
            break;
        default:
            break;
    }
}

uint16_t TimerApi_PreScalerGet(uint8_t index) {    
    uint8_t temp_prescaler = 0;
    uint16_t preScaler = 0;
    switch (index) {
        case 2:
            temp_prescaler = T2CONbits.TCKPS;
            break;
        case 3:
            temp_prescaler = T3CONbits.TCKPS;
            break;
        case 4:
            temp_prescaler = T4CONbits.TCKPS;
            break;
        case 6:
            temp_prescaler = T6CONbits.TCKPS;
            break;
        default:
            break;
    }
    if (temp_prescaler == 7u) {
        temp_prescaler++;
    }
    preScaler = (uint16_t) (0x01u << temp_prescaler);
    return preScaler;
}

uint32_t TimerApi_FrequencyGet(uint8_t index) {
    uint32_t ret = 0;
    switch (index) {
        case 2:
            ret=TIMER_CLOCK_FRQ/TimerApi_PreScalerGet(2);            
            break;
        case 3:
            ret=TIMER_CLOCK_FRQ/TimerApi_PreScalerGet(3); 
            break;
        case 4:
            ret=TIMER_CLOCK_FRQ/TimerApi_PreScalerGet(4); 
            break;
        case 6:
             ret=TIMER_CLOCK_FRQ/TimerApi_PreScalerGet(6); 
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
            //IFS0CLR = _IFS0_T2IF_MASK;
            break;
        case 3:
            TMR3_InterruptDisable();
            //IFS0CLR = _IFS0_T3IF_MASK;
            break;
        case 4:
            TMR4_InterruptDisable();
            //IFS0CLR = _IFS0_T5IF_MASK;
            break;
        case 6:
            TMR6_InterruptDisable();
            //IFS1CLR=_IFS1_T7IF_MASK;
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
            TMR4_CallbackRegister(callback_fn, context);
            break;
        case 6:
            TMR6_CallbackRegister(callback_fn, context);
            break;
        default:
            break;
    }
}