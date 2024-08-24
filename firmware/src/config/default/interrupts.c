/*******************************************************************************
 System Interrupts File

  Company:
    Microchip Technology Inc.

  File Name:
    interrupt.c

  Summary:
    Interrupt vectors mapping

  Description:
    This file maps all the interrupt vectors to their corresponding
    implementations. If a particular module interrupt is used, then its ISR
    definition can be found in corresponding PLIB source file. If a module
    interrupt is not used, then its ISR implementation is mapped to dummy
    handler.
 *******************************************************************************/

// DOM-IGNORE-BEGIN
/*******************************************************************************
* Copyright (C) 2018 Microchip Technology Inc. and its subsidiaries.
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
#include "configuration.h"
#include "interrupts.h"
#include "definitions.h"



// *****************************************************************************
// *****************************************************************************
// Section: System Interrupt Vector Functions
// *****************************************************************************
// *****************************************************************************


/* All the handlers are defined here.  Each will call its PLIB-specific function. */
// *****************************************************************************
// *****************************************************************************
// Section: System Interrupt Vector declarations
// *****************************************************************************
// *****************************************************************************
void CORE_TIMER_Handler (void);
void OUTPUT_COMPARE_1_Handler (void);
void TIMER_2_Handler (void);
void TIMER_3_Handler (void);
void TIMER_4_Handler (void);
void TIMER_5_Handler (void);
void TIMER_6_Handler (void);
void TIMER_7_Handler (void);
void ADC_DATA0_Handler (void);
void ADC_DATA1_Handler (void);
void ADC_DATA2_Handler (void);
void ADC_DATA3_Handler (void);
void ADC_DATA4_Handler (void);
void ADC_DATA5_Handler (void);
void ADC_DATA6_Handler (void);
void ADC_DATA7_Handler (void);
void ADC_DATA8_Handler (void);
void ADC_DATA11_Handler (void);
void ADC_DATA19_Handler (void);
void ADC_DATA24_Handler (void);
void ADC_DATA26_Handler (void);
void ADC_DATA29_Handler (void);
void ADC_DATA30_Handler (void);
void ADC_DATA31_Handler (void);
void ADC_DATA38_Handler (void);
void ADC_DATA41_Handler (void);
void ADC_DATA42_Handler (void);
void ADC_DATA44_Handler (void);
void CHANGE_NOTICE_A_Handler (void);
void CHANGE_NOTICE_D_Handler (void);
void USB_Handler (void);
void USB_DMA_Handler (void);
void DMA0_Handler (void);
void DMA1_Handler (void);
void SPI4_RX_Handler (void);
void SPI4_TX_Handler (void);
void FLASH_CONTROL_Handler (void);
void I2C5_BUS_Handler (void);
void I2C5_MASTER_Handler (void);


// *****************************************************************************
// *****************************************************************************
// Section: System Interrupt Vector definitions
// *****************************************************************************
// *****************************************************************************
void CORE_TIMER_Handler (void)
{
    CORE_TIMER_InterruptHandler();
}



void OUTPUT_COMPARE_1_Handler (void)
{
    OUTPUT_COMPARE_1_InterruptHandler();
}

void TIMER_2_Handler (void)
{
    TIMER_2_InterruptHandler();
}

void TIMER_3_Handler (void)
{
    TIMER_3_InterruptHandler();
}

void TIMER_4_Handler (void)
{
}

void TIMER_5_Handler (void)
{
    TIMER_5_InterruptHandler();
}

void TIMER_6_Handler (void)
{
}

void TIMER_7_Handler (void)
{
    TIMER_7_InterruptHandler();
}

void ADC_DATA0_Handler (void)
{
    ADC_DATA0_InterruptHandler();
}

void ADC_DATA1_Handler (void)
{
    ADC_DATA1_InterruptHandler();
}

void ADC_DATA2_Handler (void)
{
    ADC_DATA2_InterruptHandler();
}

void ADC_DATA3_Handler (void)
{
    ADC_DATA3_InterruptHandler();
}

void ADC_DATA4_Handler (void)
{
    ADC_DATA4_InterruptHandler();
}

void ADC_DATA5_Handler (void)
{
    ADC_DATA5_InterruptHandler();
}

void ADC_DATA6_Handler (void)
{
    ADC_DATA6_InterruptHandler();
}

void ADC_DATA7_Handler (void)
{
    ADC_DATA7_InterruptHandler();
}

void ADC_DATA8_Handler (void)
{
    ADC_DATA8_InterruptHandler();
}

void ADC_DATA11_Handler (void)
{
    ADC_DATA11_InterruptHandler();
}

void ADC_DATA19_Handler (void)
{
    ADC_DATA19_InterruptHandler();
}

void ADC_DATA24_Handler (void)
{
    ADC_DATA24_InterruptHandler();
}

void ADC_DATA26_Handler (void)
{
    ADC_DATA26_InterruptHandler();
}

void ADC_DATA29_Handler (void)
{
    ADC_DATA29_InterruptHandler();
}

void ADC_DATA30_Handler (void)
{
    ADC_DATA30_InterruptHandler();
}

void ADC_DATA31_Handler (void)
{
    ADC_DATA31_InterruptHandler();
}

void ADC_DATA38_Handler (void)
{
    ADC_DATA38_InterruptHandler();
}

void ADC_DATA41_Handler (void)
{
    ADC_DATA41_InterruptHandler();
}

void ADC_DATA42_Handler (void)
{
    ADC_DATA42_InterruptHandler();
}

void ADC_DATA44_Handler (void)
{
    ADC_DATA44_InterruptHandler();
}

void CHANGE_NOTICE_A_Handler (void)
{
    CHANGE_NOTICE_A_InterruptHandler();
}

void CHANGE_NOTICE_D_Handler (void)
{
    CHANGE_NOTICE_D_InterruptHandler();
}

void USB_Handler (void)
{
    DRV_USBHS_InterruptHandler();
}

void USB_DMA_Handler (void)
{
    DRV_USBHS_DMAInterruptHandler();
}

void DMA0_Handler (void)
{
    DMA0_InterruptHandler();
}

void DMA1_Handler (void)
{
    DMA1_InterruptHandler();
}

void SPI4_RX_Handler (void)
{
    SPI4_RX_InterruptHandler();
}

void SPI4_TX_Handler (void)
{
    SPI4_TX_InterruptHandler();
}

void FLASH_CONTROL_Handler (void)
{
    NVM_InterruptHandler();
}

void I2C5_BUS_Handler (void)
{
    I2C5_BUS_InterruptHandler();
}

void I2C5_MASTER_Handler (void)
{
    I2C5_MASTER_InterruptHandler();
}





/*******************************************************************************
 End of File
*/
