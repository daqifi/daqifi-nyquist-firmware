/*******************************************************************************
  Output Compare OCMP7 Peripheral Library (PLIB)

  Company:
    Microchip Technology Inc.

  File Name:
    plib_ocmp7.c

  Summary:
    OCMP7 Source File

  Description:
    None

*******************************************************************************/

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
#include "plib_ocmp7.h"
#include "interrupts.h"

// *****************************************************************************

// *****************************************************************************
// Section: OCMP7 Implementation
// *****************************************************************************
// *****************************************************************************

// *****************************************************************************


void OCMP7_Initialize (void)
{
    /*Setup OC7CON        */
    /*OCM         = 6        */
    /*OCTSEL       = 1        */
    /*OC32         = 0        */
    /*SIDL         = false    */

    OC7CON = 0xe;

    OC7R = 0;
    OC7RS = 0;

}

void OCMP7_Enable (void)
{
    OC7CONSET = _OC7CON_ON_MASK;
}

void OCMP7_Disable (void)
{
    OC7CONCLR = _OC7CON_ON_MASK;
}



uint16_t OCMP7_CompareValueGet (void)
{
    return (uint16_t)OC7R;
}

void OCMP7_CompareSecondaryValueSet (uint16_t value)
{
    OC7RS = value;
}

uint16_t OCMP7_CompareSecondaryValueGet (void)
{
    return (uint16_t)OC7RS;
}

