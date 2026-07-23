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
    .extern  CHANGE_NOTICE_B_Handler

    .section   .vector_119,code, keep
    .equ     __vector_dispatch_119, IntVectorCHANGE_NOTICE_B_Handler
    .global  __vector_dispatch_119
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorCHANGE_NOTICE_B_Handler

IntVectorCHANGE_NOTICE_B_Handler:
    portSAVE_CONTEXT
    la    s6,  CHANGE_NOTICE_B_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorCHANGE_NOTICE_B_Handler
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
    .extern  DMA2_Handler

    .section   .vector_136,code, keep
    .equ     __vector_dispatch_136, IntVectorDMA2_Handler
    .global  __vector_dispatch_136
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorDMA2_Handler

IntVectorDMA2_Handler:
    portSAVE_CONTEXT
    la    s6,  DMA2_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorDMA2_Handler
    .extern  DMA3_Handler

    .section   .vector_137,code, keep
    .equ     __vector_dispatch_137, IntVectorDMA3_Handler
    .global  __vector_dispatch_137
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorDMA3_Handler

IntVectorDMA3_Handler:
    portSAVE_CONTEXT
    la    s6,  DMA3_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorDMA3_Handler
    .extern  SPI2_RX_Handler

    .section   .vector_143,code, keep
    .equ     __vector_dispatch_143, IntVectorSPI2_RX_Handler
    .global  __vector_dispatch_143
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorSPI2_RX_Handler

IntVectorSPI2_RX_Handler:
    portSAVE_CONTEXT
    la    s6,  SPI2_RX_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorSPI2_RX_Handler
    .extern  SPI2_TX_Handler

    .section   .vector_144,code, keep
    .equ     __vector_dispatch_144, IntVectorSPI2_TX_Handler
    .global  __vector_dispatch_144
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorSPI2_TX_Handler

IntVectorSPI2_TX_Handler:
    portSAVE_CONTEXT
    la    s6,  SPI2_TX_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorSPI2_TX_Handler
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
    .extern  SPI6_RX_Handler

    .section   .vector_186,code, keep
    .equ     __vector_dispatch_186, IntVectorSPI6_RX_Handler
    .global  __vector_dispatch_186
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorSPI6_RX_Handler

IntVectorSPI6_RX_Handler:
    portSAVE_CONTEXT
    la    s6,  SPI6_RX_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorSPI6_RX_Handler
    .extern  SPI6_TX_Handler

    .section   .vector_187,code, keep
    .equ     __vector_dispatch_187, IntVectorSPI6_TX_Handler
    .global  __vector_dispatch_187
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorSPI6_TX_Handler

IntVectorSPI6_TX_Handler:
    portSAVE_CONTEXT
    la    s6,  SPI6_TX_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorSPI6_TX_Handler
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

/* #670: ADCHS digital-comparator trip vectors 46-51 (DC1-DC6). */
    .section   .vector_46,code, keep
    .equ     __vector_dispatch_46, IntVectorADC_DC1_Handler
    .global  __vector_dispatch_46
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_DC1_Handler
IntVectorADC_DC1_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_DC1_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_DC1_Handler

    .section   .vector_47,code, keep
    .equ     __vector_dispatch_47, IntVectorADC_DC2_Handler
    .global  __vector_dispatch_47
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_DC2_Handler
IntVectorADC_DC2_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_DC2_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_DC2_Handler

    .section   .vector_48,code, keep
    .equ     __vector_dispatch_48, IntVectorADC_DC3_Handler
    .global  __vector_dispatch_48
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_DC3_Handler
IntVectorADC_DC3_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_DC3_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_DC3_Handler

    .section   .vector_49,code, keep
    .equ     __vector_dispatch_49, IntVectorADC_DC4_Handler
    .global  __vector_dispatch_49
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_DC4_Handler
IntVectorADC_DC4_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_DC4_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_DC4_Handler

    .section   .vector_50,code, keep
    .equ     __vector_dispatch_50, IntVectorADC_DC5_Handler
    .global  __vector_dispatch_50
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_DC5_Handler
IntVectorADC_DC5_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_DC5_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_DC5_Handler

    .section   .vector_51,code, keep
    .equ     __vector_dispatch_51, IntVectorADC_DC6_Handler
    .global  __vector_dispatch_51
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorADC_DC6_Handler
IntVectorADC_DC6_Handler:
    portSAVE_CONTEXT
    la    s6,  ADC_DC6_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorADC_DC6_Handler


/* #667/#666: edge-event + input-capture interrupt vectors. Each block is
 * self-contained (section/.equ/.ent/body all match its vector number) — the
 * prior union-merge interleave desynchronized them (INT2 vec13 ran the IC2
 * handler -> INT2 IFS never cleared -> infinite ISR -> CDC wedge). Regenerated
 * clean 2026-07-22.
 * Edge: INT1=8, INT2=13, INT3=18, INT4=23, TMR8=36, TMR9=40.
 * IC:   IC1=6, IC2=11, IC3=16, IC4=21, IC5=26, IC6=30, IC7=34, IC8=38, IC9=42. */

    .extern  Edge_INT1_Handler
    .section   .vector_8,code, keep
    .equ     __vector_dispatch_8, IntVectorEdge_INT1_Handler
    .global  __vector_dispatch_8
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorEdge_INT1_Handler
IntVectorEdge_INT1_Handler:
    portSAVE_CONTEXT
    la    s6,  Edge_INT1_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorEdge_INT1_Handler

    .extern  Edge_INT2_Handler
    .section   .vector_13,code, keep
    .equ     __vector_dispatch_13, IntVectorEdge_INT2_Handler
    .global  __vector_dispatch_13
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorEdge_INT2_Handler
IntVectorEdge_INT2_Handler:
    portSAVE_CONTEXT
    la    s6,  Edge_INT2_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorEdge_INT2_Handler

    .extern  Edge_INT3_Handler
    .section   .vector_18,code, keep
    .equ     __vector_dispatch_18, IntVectorEdge_INT3_Handler
    .global  __vector_dispatch_18
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorEdge_INT3_Handler
IntVectorEdge_INT3_Handler:
    portSAVE_CONTEXT
    la    s6,  Edge_INT3_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorEdge_INT3_Handler

    .extern  Edge_INT4_Handler
    .section   .vector_23,code, keep
    .equ     __vector_dispatch_23, IntVectorEdge_INT4_Handler
    .global  __vector_dispatch_23
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorEdge_INT4_Handler
IntVectorEdge_INT4_Handler:
    portSAVE_CONTEXT
    la    s6,  Edge_INT4_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorEdge_INT4_Handler

    .extern  Edge_TMR8_Handler
    .section   .vector_36,code, keep
    .equ     __vector_dispatch_36, IntVectorEdge_TMR8_Handler
    .global  __vector_dispatch_36
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorEdge_TMR8_Handler
IntVectorEdge_TMR8_Handler:
    portSAVE_CONTEXT
    la    s6,  Edge_TMR8_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorEdge_TMR8_Handler

    .extern  Edge_TMR9_Handler
    .section   .vector_40,code, keep
    .equ     __vector_dispatch_40, IntVectorEdge_TMR9_Handler
    .global  __vector_dispatch_40
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorEdge_TMR9_Handler
IntVectorEdge_TMR9_Handler:
    portSAVE_CONTEXT
    la    s6,  Edge_TMR9_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorEdge_TMR9_Handler

    .extern  IC1_Capture_Handler
    .section   .vector_6,code, keep
    .equ     __vector_dispatch_6, IntVectorIC1_Capture_Handler
    .global  __vector_dispatch_6
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorIC1_Capture_Handler
IntVectorIC1_Capture_Handler:
    portSAVE_CONTEXT
    la    s6,  IC1_Capture_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorIC1_Capture_Handler

    .extern  IC2_Capture_Handler
    .section   .vector_11,code, keep
    .equ     __vector_dispatch_11, IntVectorIC2_Capture_Handler
    .global  __vector_dispatch_11
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorIC2_Capture_Handler
IntVectorIC2_Capture_Handler:
    portSAVE_CONTEXT
    la    s6,  IC2_Capture_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorIC2_Capture_Handler

    .extern  IC3_Capture_Handler
    .section   .vector_16,code, keep
    .equ     __vector_dispatch_16, IntVectorIC3_Capture_Handler
    .global  __vector_dispatch_16
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorIC3_Capture_Handler
IntVectorIC3_Capture_Handler:
    portSAVE_CONTEXT
    la    s6,  IC3_Capture_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorIC3_Capture_Handler

    .extern  IC4_Capture_Handler
    .section   .vector_21,code, keep
    .equ     __vector_dispatch_21, IntVectorIC4_Capture_Handler
    .global  __vector_dispatch_21
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorIC4_Capture_Handler
IntVectorIC4_Capture_Handler:
    portSAVE_CONTEXT
    la    s6,  IC4_Capture_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorIC4_Capture_Handler

    .extern  IC5_Capture_Handler
    .section   .vector_26,code, keep
    .equ     __vector_dispatch_26, IntVectorIC5_Capture_Handler
    .global  __vector_dispatch_26
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorIC5_Capture_Handler
IntVectorIC5_Capture_Handler:
    portSAVE_CONTEXT
    la    s6,  IC5_Capture_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorIC5_Capture_Handler

    .extern  IC6_Capture_Handler
    .section   .vector_30,code, keep
    .equ     __vector_dispatch_30, IntVectorIC6_Capture_Handler
    .global  __vector_dispatch_30
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorIC6_Capture_Handler
IntVectorIC6_Capture_Handler:
    portSAVE_CONTEXT
    la    s6,  IC6_Capture_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorIC6_Capture_Handler

    .extern  IC7_Capture_Handler
    .section   .vector_34,code, keep
    .equ     __vector_dispatch_34, IntVectorIC7_Capture_Handler
    .global  __vector_dispatch_34
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorIC7_Capture_Handler
IntVectorIC7_Capture_Handler:
    portSAVE_CONTEXT
    la    s6,  IC7_Capture_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorIC7_Capture_Handler

    .extern  IC8_Capture_Handler
    .section   .vector_38,code, keep
    .equ     __vector_dispatch_38, IntVectorIC8_Capture_Handler
    .global  __vector_dispatch_38
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorIC8_Capture_Handler
IntVectorIC8_Capture_Handler:
    portSAVE_CONTEXT
    la    s6,  IC8_Capture_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorIC8_Capture_Handler

    .extern  IC9_Capture_Handler
    .section   .vector_42,code, keep
    .equ     __vector_dispatch_42, IntVectorIC9_Capture_Handler
    .global  __vector_dispatch_42
    .set     nomicromips
    .set     noreorder
    .set     nomips16
    .set     noat
    .ent  IntVectorIC9_Capture_Handler
IntVectorIC9_Capture_Handler:
    portSAVE_CONTEXT
    la    s6,  IC9_Capture_Handler
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end   IntVectorIC9_Capture_Handler


