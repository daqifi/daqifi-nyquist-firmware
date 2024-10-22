/*******************************************************************************
  TMR Peripheral Library Interface Source File

  Company
    Microchip Technology Inc.

  File Name
    plib_tmr6.c

  Summary
    TMR6 peripheral library source file.

  Description
    This file implements the interface to the TMR peripheral library.  This
    library provides access to and control of the associated peripheral
    instance.

*******************************************************************************/

// DOM-IGNORE-BEGIN
/*******************************************************************************
* Copyright (C) 2019 Microchip Technology Inc. and its subsidiaries.
*
* Subject to your compliance with these terms, you may use Microchip software
* and any derivatives exclusively with Microchip products. It is your
* responsibility to comply with third party license terms applicable to your
* use of third party software (including open source software) that may
* accompany Microchip software.
*
* THIS SOFTWARE IS SUPPLIED BY MICROCHIP "AS IS". NO WARRANTIES, WHETHER
* EXPRESS, IMPLIED OR STATUTORY, APPLY TO THIS SOFTWARE, INCLUDING ANY IMPLIED
* WARRANTIES OF NON-INFRINGEMENT, MERCHANTABILITY, AND FITNESS FOR A
* PARTICULAR PURPOSE.
*
* IN NO EVENT WILL MICROCHIP BE LIABLE FOR ANY INDIRECT, SPECIAL, PUNITIVE,
* INCIDENTAL OR CONSEQUENTIAL LOSS, DAMAGE, COST OR EXPENSE OF ANY KIND
* WHATSOEVER RELATED TO THE SOFTWARE, HOWEVER CAUSED, EVEN IF MICROCHIP HAS
* BEEN ADVISED OF THE POSSIBILITY OR THE DAMAGES ARE FORESEEABLE. TO THE
* FULLEST EXTENT ALLOWED BY LAW, MICROCHIP'S TOTAL LIABILITY ON ALL CLAIMS IN
* ANY WAY RELATED TO THIS SOFTWARE WILL NOT EXCEED THE AMOUNT OF FEES, IF ANY,
* THAT YOU HAVE PAID DIRECTLY TO MICROCHIP FOR THIS SOFTWARE.
*******************************************************************************/
// DOM-IGNORE-END


// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************

#include "device.h"
#include "plib_tmr6.h"
#include "interrupts.h"


volatile static TMR_TIMER_OBJECT tmr6Obj;


void TMR6_Initialize(void)
{
    /* Disable Timer */
    T6CONCLR = _T6CON_ON_MASK;

    /*
    SIDL = 0
    TCKPS =1
    T32   = 1
    TCS = 0
    */
    T6CONSET = 0x18;

    /* Clear counter */
    TMR6 = 0x0;

    /*Set period */
    PR6 = 14999U;

    /* Enable TMR Interrupt of odd numbered timer in 32-bit mode */
    IEC1SET = _IEC1_T7IE_MASK;

}


void TMR6_Start(void)
{
    T6CONSET = _T6CON_ON_MASK;
}


void TMR6_Stop (void)
{
    T6CONCLR = _T6CON_ON_MASK;
}

void TMR6_PeriodSet(uint32_t period)
{
    PR6  = period;
}

uint32_t TMR6_PeriodGet(void)
{
    return PR6;
}

uint32_t TMR6_CounterGet(void)
{
    return (TMR6);
}


uint32_t TMR6_FrequencyGet(void)
{
    return (50000000);
}


void __attribute__((used)) TIMER_7_InterruptHandler (void)
{
    uint32_t status  = 0U;
    status = IFS1bits.T7IF;
    IFS1CLR = _IFS1_T7IF_MASK;

    if((tmr6Obj.callback_fn != NULL))
    {
        uintptr_t context = tmr6Obj.context;
        tmr6Obj.callback_fn(status, context);
    }
}


void TMR6_InterruptEnable(void)
{
    IEC1SET = _IEC1_T7IE_MASK;
}


void TMR6_InterruptDisable(void)
{
    IEC1CLR = _IEC1_T7IE_MASK;
}


void TMR6_CallbackRegister( TMR_CALLBACK callback_fn, uintptr_t context )
{
    /* Save callback_fn and context in local memory */
    tmr6Obj.callback_fn = callback_fn;
    tmr6Obj.context = context;
}
