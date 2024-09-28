/*******************************************************************************
  EVIC PLIB Implementation

  Company:
    Microchip Technology Inc.

  File Name:
    plib_evic.c

  Summary:
    EVIC PLIB Source File

  Description:
    None

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

#include "device.h"
#include "plib_evic.h"
#include "interrupts.h"


// *****************************************************************************
// *****************************************************************************
// Section: IRQ Implementation
// *****************************************************************************
// *****************************************************************************

void EVIC_Initialize( void )
{
    INTCONSET = _INTCON_MVEC_MASK;

    /* Set up priority and subpriority of enabled interrupts */
    IPC0SET = 0x4U | 0x0U;  /* CORE_TIMER:  Priority 1 / Subpriority 0 */
    IPC1SET = 0x4000000U | 0x3000000U;  /* OUTPUT_COMPARE_1:  Priority 1 / Subpriority 3 */
    IPC2SET = 0x400U | 0x0U;  /* TIMER_2:  Priority 1 / Subpriority 0 */
    IPC3SET = 0x40000U | 0x0U;  /* TIMER_3:  Priority 1 / Subpriority 0 */
    IPC6SET = 0xcU | 0x3U;  /* TIMER_5:  Priority 3 / Subpriority 3 */
    IPC7SET = 0x4U | 0x0U;  /* TIMER_6:  Priority 1 / Subpriority 0 */
    IPC8SET = 0x4U | 0x0U;  /* TIMER_7:  Priority 1 / Subpriority 0 */
    IPC14SET = 0xc000000U | 0x0U;  /* ADC_DATA0:  Priority 3 / Subpriority 0 */
    IPC15SET = 0xcU | 0x0U;  /* ADC_DATA1:  Priority 3 / Subpriority 0 */
    IPC15SET = 0xc00U | 0x0U;  /* ADC_DATA2:  Priority 3 / Subpriority 0 */
    IPC15SET = 0xc0000U | 0x0U;  /* ADC_DATA3:  Priority 3 / Subpriority 0 */
    IPC15SET = 0xc000000U | 0x0U;  /* ADC_DATA4:  Priority 3 / Subpriority 0 */
    IPC16SET = 0x4U | 0x0U;  /* ADC_DATA5:  Priority 1 / Subpriority 0 */
    IPC16SET = 0x400U | 0x0U;  /* ADC_DATA6:  Priority 1 / Subpriority 0 */
    IPC16SET = 0x40000U | 0x0U;  /* ADC_DATA7:  Priority 1 / Subpriority 0 */
    IPC16SET = 0x4000000U | 0x0U;  /* ADC_DATA8:  Priority 1 / Subpriority 0 */
    IPC17SET = 0x40000U | 0x0U;  /* ADC_DATA11:  Priority 1 / Subpriority 0 */
    IPC20SET = 0x4000000U | 0x0U;  /* ADC_DATA24:  Priority 1 / Subpriority 0 */
    IPC21SET = 0x4U | 0x0U;  /* ADC_DATA25:  Priority 1 / Subpriority 0 */
    IPC21SET = 0x400U | 0x0U;  /* ADC_DATA26:  Priority 1 / Subpriority 0 */
    IPC21SET = 0x40000U | 0x0U;  /* ADC_DATA27:  Priority 1 / Subpriority 0 */
    IPC24SET = 0x400U | 0x0U;  /* ADC_DATA38:  Priority 1 / Subpriority 0 */
    IPC24SET = 0x40000U | 0x0U;  /* ADC_DATA39:  Priority 1 / Subpriority 0 */
    IPC29SET = 0x40000U | 0x0U;  /* CHANGE_NOTICE_A:  Priority 1 / Subpriority 0 */
    IPC30SET = 0xc00U | 0x0U;  /* CHANGE_NOTICE_D:  Priority 3 / Subpriority 0 */
    IPC33SET = 0x4U | 0x1U;  /* USB:  Priority 1 / Subpriority 1 */
    IPC33SET = 0x400U | 0x100U;  /* USB_DMA:  Priority 1 / Subpriority 1 */
    IPC33SET = 0x80000U | 0x0U;  /* DMA0:  Priority 2 / Subpriority 0 */
    IPC33SET = 0x8000000U | 0x0U;  /* DMA1:  Priority 2 / Subpriority 0 */
    IPC41SET = 0x8U | 0x0U;  /* SPI4_RX:  Priority 2 / Subpriority 0 */
    IPC41SET = 0x800U | 0x0U;  /* SPI4_TX:  Priority 2 / Subpriority 0 */
    IPC41SET = 0x4000000U | 0x0U;  /* FLASH_CONTROL:  Priority 1 / Subpriority 0 */
    IPC45SET = 0x40000U | 0x0U;  /* I2C5_BUS:  Priority 1 / Subpriority 0 */
    IPC46SET = 0x4U | 0x0U;  /* I2C5_MASTER:  Priority 1 / Subpriority 0 */
    IPC48SET = 0xcU | 0x0U;  /* ADC_EOS:  Priority 3 / Subpriority 0 */


}

void EVIC_SourceEnable( INT_SOURCE source )
{
    volatile uint32_t *IECx = (volatile uint32_t *) (&IEC0 + ((0x10U * (source / 32U)) / 4U));
    volatile uint32_t *IECxSET = (volatile uint32_t *)(IECx + 2U);

    *IECxSET = 1UL << (source & 0x1fU);
}

void EVIC_SourceDisable( INT_SOURCE source )
{
    volatile uint32_t *IECx = (volatile uint32_t *) (&IEC0 + ((0x10U * (source / 32U)) / 4U));
    volatile uint32_t *IECxCLR = (volatile uint32_t *)(IECx + 1U);

    *IECxCLR = 1UL << (source & 0x1fU);
}

bool EVIC_SourceIsEnabled( INT_SOURCE source )
{
    volatile uint32_t *IECx = (volatile uint32_t *) (&IEC0 + ((0x10U * (source / 32U)) / 4U));

    return (((*IECx >> (source & 0x1fU)) & 0x01U) != 0U);
}

bool EVIC_SourceStatusGet( INT_SOURCE source )
{
    volatile uint32_t *IFSx = (volatile uint32_t *)(&IFS0 + ((0x10U * (source / 32U)) / 4U));

    return (((*IFSx >> (source & 0x1fU)) & 0x1U) != 0U);
}

void EVIC_SourceStatusSet( INT_SOURCE source )
{
    volatile uint32_t *IFSx = (volatile uint32_t *) (&IFS0 + ((0x10U * (source / 32U)) / 4U));
    volatile uint32_t *IFSxSET = (volatile uint32_t *)(IFSx + 2U);

    *IFSxSET = 1UL << (source & 0x1fU);
}

void EVIC_SourceStatusClear( INT_SOURCE source )
{
    volatile uint32_t *IFSx = (volatile uint32_t *) (&IFS0 + ((0x10U * (source / 32U)) / 4U));
    volatile uint32_t *IFSxCLR = (volatile uint32_t *)(IFSx + 1U);

    *IFSxCLR = 1UL << (source & 0x1fU);
}

void EVIC_INT_Enable( void )
{
   (void) __builtin_enable_interrupts();
}

bool EVIC_INT_Disable( void )
{
    uint32_t processorStatus;

    /* Save the processor status and then Disable the global interrupt */
    processorStatus = ( uint32_t )__builtin_disable_interrupts();

    /* return the interrupt status */
    return ((processorStatus & 0x01U) != 0U);
}

void EVIC_INT_Restore( bool state )
{
    if (state)
    {
        /* restore the state of CP0 Status register before the disable occurred */
       (void) __builtin_enable_interrupts();
    }
}

bool EVIC_INT_SourceDisable( INT_SOURCE source )
{
    bool processorStatus;
    bool intSrcStatus;

    processorStatus = EVIC_INT_Disable();
    intSrcStatus = (EVIC_SourceIsEnabled(source) != 0U);
    EVIC_SourceDisable( source );
    EVIC_INT_Restore( processorStatus );

    /* return the source status */
    return intSrcStatus;
}

void EVIC_INT_SourceRestore( INT_SOURCE source, bool status )
{
    if( status ) {
       EVIC_SourceEnable( source );
    }

    return;
}


/* End of file */