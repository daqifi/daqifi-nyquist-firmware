/*******************************************************************************
  System Interrupts File

  Company:
    Microchip Technology Inc.

  File Name:
    interrupt_a.S

  Summary:
    Raw ISR definitions.

  Description:
    This file maps all the interrupt vectors to their corresponding
    implementations. If a particular module interrupt is used, then its ISR
    definition can be found in corresponding PLIB source file. If a module
    interrupt is not used, then its ISR implementation is mapped to dummy
    handler.
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

#include <xc.h>
#include "ISR_Support.h"

    .extern  CORE_TIMER_Handler

    .section   .vector_0,code, keep
    .equ     __vector_dispatch_0, IntVectorCORE_TIMER_Handler
    .global  __vector_dispatch_0
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorCORE_TIMER_Handler

IntVectorCORE_TIMER_Handler:
    portSAVE_CONTEXT
    la    s6,  CORE_TIMER_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorCORE_TIMER_Handler
    .extern  OUTPUT_COMPARE_1_Handler

    .section   .vector_7,code, keep
    .equ     __vector_dispatch_7, IntVectorOUTPUT_COMPARE_1_Handler
    .global  __vector_dispatch_7
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorOUTPUT_COMPARE_1_Handler

IntVectorOUTPUT_COMPARE_1_Handler:
    portSAVE_CONTEXT
    la    s6,  OUTPUT_COMPARE_1_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorOUTPUT_COMPARE_1_Handler
    .extern  TIMER_2_Handler

    .section   .vector_9,code, keep
    .equ     __vector_dispatch_9, IntVectorTIMER_2_Handler
    .global  __vector_dispatch_9
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorTIMER_2_Handler

IntVectorTIMER_2_Handler:
    portSAVE_CONTEXT
    la    s6,  TIMER_2_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorTIMER_2_Handler
    .extern  TIMER_3_Handler

    .section   .vector_14,code, keep
    .equ     __vector_dispatch_14, IntVectorTIMER_3_Handler
    .global  __vector_dispatch_14
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorTIMER_3_Handler

IntVectorTIMER_3_Handler:
    portSAVE_CONTEXT
    la    s6,  TIMER_3_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorTIMER_3_Handler
    .extern  TIMER_5_Handler

    .section   .vector_24,code, keep
    .equ     __vector_dispatch_24, IntVectorTIMER_5_Handler
    .global  __vector_dispatch_24
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorTIMER_5_Handler

IntVectorTIMER_5_Handler:
    portSAVE_CONTEXT
    la    s6,  TIMER_5_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorTIMER_5_Handler
    .extern  TIMER_6_Handler

    .section   .vector_28,code, keep
    .equ     __vector_dispatch_28, IntVectorTIMER_6_Handler
    .global  __vector_dispatch_28
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorTIMER_6_Handler

IntVectorTIMER_6_Handler:
    portSAVE_CONTEXT
    la    s6,  TIMER_6_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorTIMER_6_Handler
    .extern  TIMER_7_Handler

    .section   .vector_32,code, keep
    .equ     __vector_dispatch_32, IntVectorTIMER_7_Handler
    .global  __vector_dispatch_32
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorTIMER_7_Handler

IntVectorTIMER_7_Handler:
    portSAVE_CONTEXT
    la    s6,  TIMER_7_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorTIMER_7_Handler
    .extern  ADC_DATA0_Handler

    .section   .vector_59,code, keep
    .equ     __vector_dispatch_59, IntVectorADC_DATA0_Handler
    .global  __vector_dispatch_59
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_DATA0_Handler

IntVectorADC_DATA0_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_DATA0_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_DATA0_Handler
    .extern  ADC_DATA1_Handler

    .section   .vector_60,code, keep
    .equ     __vector_dispatch_60, IntVectorADC_DATA1_Handler
    .global  __vector_dispatch_60
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_DATA1_Handler

IntVectorADC_DATA1_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_DATA1_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_DATA1_Handler
    .extern  ADC_DATA2_Handler

    .section   .vector_61,code, keep
    .equ     __vector_dispatch_61, IntVectorADC_DATA2_Handler
    .global  __vector_dispatch_61
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_DATA2_Handler

IntVectorADC_DATA2_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_DATA2_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_DATA2_Handler
    .extern  ADC_DATA3_Handler

    .section   .vector_62,code, keep
    .equ     __vector_dispatch_62, IntVectorADC_DATA3_Handler
    .global  __vector_dispatch_62
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_DATA3_Handler

IntVectorADC_DATA3_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_DATA3_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_DATA3_Handler
    .extern  ADC_DATA4_Handler

    .section   .vector_63,code, keep
    .equ     __vector_dispatch_63, IntVectorADC_DATA4_Handler
    .global  __vector_dispatch_63
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_DATA4_Handler

IntVectorADC_DATA4_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_DATA4_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_DATA4_Handler
    .extern  ADC_DATA5_Handler

    .section   .vector_64,code, keep
    .equ     __vector_dispatch_64, IntVectorADC_DATA5_Handler
    .global  __vector_dispatch_64
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_DATA5_Handler

IntVectorADC_DATA5_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_DATA5_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_DATA5_Handler
    .extern  ADC_DATA6_Handler

    .section   .vector_65,code, keep
    .equ     __vector_dispatch_65, IntVectorADC_DATA6_Handler
    .global  __vector_dispatch_65
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_DATA6_Handler

IntVectorADC_DATA6_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_DATA6_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_DATA6_Handler
    .extern  ADC_DATA7_Handler

    .section   .vector_66,code, keep
    .equ     __vector_dispatch_66, IntVectorADC_DATA7_Handler
    .global  __vector_dispatch_66
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_DATA7_Handler

IntVectorADC_DATA7_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_DATA7_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_DATA7_Handler
    .extern  ADC_DATA8_Handler

    .section   .vector_67,code, keep
    .equ     __vector_dispatch_67, IntVectorADC_DATA8_Handler
    .global  __vector_dispatch_67
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_DATA8_Handler

IntVectorADC_DATA8_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_DATA8_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_DATA8_Handler
    .extern  ADC_DATA11_Handler

    .section   .vector_70,code, keep
    .equ     __vector_dispatch_70, IntVectorADC_DATA11_Handler
    .global  __vector_dispatch_70
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_DATA11_Handler

IntVectorADC_DATA11_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_DATA11_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_DATA11_Handler
    .extern  ADC_DATA24_Handler

    .section   .vector_83,code, keep
    .equ     __vector_dispatch_83, IntVectorADC_DATA24_Handler
    .global  __vector_dispatch_83
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_DATA24_Handler

IntVectorADC_DATA24_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_DATA24_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_DATA24_Handler
    .extern  ADC_DATA25_Handler

    .section   .vector_84,code, keep
    .equ     __vector_dispatch_84, IntVectorADC_DATA25_Handler
    .global  __vector_dispatch_84
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_DATA25_Handler

IntVectorADC_DATA25_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_DATA25_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_DATA25_Handler
    .extern  ADC_DATA26_Handler

    .section   .vector_85,code, keep
    .equ     __vector_dispatch_85, IntVectorADC_DATA26_Handler
    .global  __vector_dispatch_85
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_DATA26_Handler

IntVectorADC_DATA26_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_DATA26_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_DATA26_Handler
    .extern  ADC_DATA27_Handler

    .section   .vector_86,code, keep
    .equ     __vector_dispatch_86, IntVectorADC_DATA27_Handler
    .global  __vector_dispatch_86
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_DATA27_Handler

IntVectorADC_DATA27_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_DATA27_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_DATA27_Handler
    .extern  ADC_DATA38_Handler

    .section   .vector_97,code, keep
    .equ     __vector_dispatch_97, IntVectorADC_DATA38_Handler
    .global  __vector_dispatch_97
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_DATA38_Handler

IntVectorADC_DATA38_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_DATA38_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_DATA38_Handler
    .extern  ADC_DATA39_Handler

    .section   .vector_98,code, keep
    .equ     __vector_dispatch_98, IntVectorADC_DATA39_Handler
    .global  __vector_dispatch_98
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_DATA39_Handler

IntVectorADC_DATA39_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_DATA39_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_DATA39_Handler
    .extern  CHANGE_NOTICE_A_Handler

    .section   .vector_118,code, keep
    .equ     __vector_dispatch_118, IntVectorCHANGE_NOTICE_A_Handler
    .global  __vector_dispatch_118
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorCHANGE_NOTICE_A_Handler

IntVectorCHANGE_NOTICE_A_Handler:
    portSAVE_CONTEXT
    la    s6,  CHANGE_NOTICE_A_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorCHANGE_NOTICE_A_Handler
    .extern  CHANGE_NOTICE_D_Handler

    .section   .vector_121,code, keep
    .equ     __vector_dispatch_121, IntVectorCHANGE_NOTICE_D_Handler
    .global  __vector_dispatch_121
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorCHANGE_NOTICE_D_Handler

IntVectorCHANGE_NOTICE_D_Handler:
    portSAVE_CONTEXT
    la    s6,  CHANGE_NOTICE_D_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorCHANGE_NOTICE_D_Handler
    .extern  USB_Handler

    .section   .vector_132,code, keep
    .equ     __vector_dispatch_132, IntVectorUSB_Handler
    .global  __vector_dispatch_132
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorUSB_Handler

IntVectorUSB_Handler:
    portSAVE_CONTEXT
    la    s6,  USB_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorUSB_Handler
    .extern  USB_DMA_Handler

    .section   .vector_133,code, keep
    .equ     __vector_dispatch_133, IntVectorUSB_DMA_Handler
    .global  __vector_dispatch_133
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorUSB_DMA_Handler

IntVectorUSB_DMA_Handler:
    portSAVE_CONTEXT
    la    s6,  USB_DMA_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorUSB_DMA_Handler
    .extern  DMA0_Handler

    .section   .vector_134,code, keep
    .equ     __vector_dispatch_134, IntVectorDMA0_Handler
    .global  __vector_dispatch_134
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorDMA0_Handler

IntVectorDMA0_Handler:
    portSAVE_CONTEXT
    la    s6,  DMA0_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorDMA0_Handler
    .extern  DMA1_Handler

    .section   .vector_135,code, keep
    .equ     __vector_dispatch_135, IntVectorDMA1_Handler
    .global  __vector_dispatch_135
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorDMA1_Handler

IntVectorDMA1_Handler:
    portSAVE_CONTEXT
    la    s6,  DMA1_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorDMA1_Handler
    .extern  SPI4_RX_Handler

    .section   .vector_164,code, keep
    .equ     __vector_dispatch_164, IntVectorSPI4_RX_Handler
    .global  __vector_dispatch_164
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorSPI4_RX_Handler

IntVectorSPI4_RX_Handler:
    portSAVE_CONTEXT
    la    s6,  SPI4_RX_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorSPI4_RX_Handler
    .extern  SPI4_TX_Handler

    .section   .vector_165,code, keep
    .equ     __vector_dispatch_165, IntVectorSPI4_TX_Handler
    .global  __vector_dispatch_165
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorSPI4_TX_Handler

IntVectorSPI4_TX_Handler:
    portSAVE_CONTEXT
    la    s6,  SPI4_TX_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorSPI4_TX_Handler
    .extern  FLASH_CONTROL_Handler

    .section   .vector_167,code, keep
    .equ     __vector_dispatch_167, IntVectorFLASH_CONTROL_Handler
    .global  __vector_dispatch_167
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorFLASH_CONTROL_Handler

IntVectorFLASH_CONTROL_Handler:
    portSAVE_CONTEXT
    la    s6,  FLASH_CONTROL_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorFLASH_CONTROL_Handler
    .extern  I2C5_BUS_Handler

    .section   .vector_182,code, keep
    .equ     __vector_dispatch_182, IntVectorI2C5_BUS_Handler
    .global  __vector_dispatch_182
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorI2C5_BUS_Handler

IntVectorI2C5_BUS_Handler:
    portSAVE_CONTEXT
    la    s6,  I2C5_BUS_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorI2C5_BUS_Handler
    .extern  I2C5_MASTER_Handler

    .section   .vector_184,code, keep
    .equ     __vector_dispatch_184, IntVectorI2C5_MASTER_Handler
    .global  __vector_dispatch_184
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorI2C5_MASTER_Handler

IntVectorI2C5_MASTER_Handler:
    portSAVE_CONTEXT
    la    s6,  I2C5_MASTER_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorI2C5_MASTER_Handler
    .extern  ADC_EOS_Handler

    .section   .vector_192,code, keep
    .equ     __vector_dispatch_192, IntVectorADC_EOS_Handler
    .global  __vector_dispatch_192
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_EOS_Handler

IntVectorADC_EOS_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_EOS_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_EOS_Handler

