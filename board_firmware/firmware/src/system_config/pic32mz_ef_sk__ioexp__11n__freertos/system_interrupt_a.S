/*******************************************************************************
 System Interrupt Source File

  File Name:
    sys_interrupt_a.S

  Summary:
    Raw ISR definitions.

  Description:
    This file contains a definitions of the raw ISRs required to support the 
    interrupt sub-system.
*******************************************************************************/

// DOM-IGNORE-BEGIN
/*******************************************************************************
Copyright (c) 2011-2014 released Microchip Technology Inc.  All rights reserved.

Microchip licenses to you the right to use, modify, copy and distribute
Software only when embedded on a Microchip microcontroller or digital signal
controller that is integrated into your product or third party product
(pursuant to the sublicense terms in the accompanying license agreement).

You should refer to the license agreement accompanying this Software for
additional information regarding your rights and obligations.

SOFTWARE AND DOCUMENTATION ARE PROVIDED AS IS WITHOUT WARRANTY OF ANY KIND,
EITHER EXPRESS OR IMPLIED, INCLUDING WITHOUT LIMITATION, ANY WARRANTY OF
MERCHANTABILITY, TITLE, NON-INFRINGEMENT AND FITNESS FOR A PARTICULAR PURPOSE.
IN NO EVENT SHALL MICROCHIP OR ITS LICENSORS BE LIABLE OR OBLIGATED UNDER
CONTRACT, NEGLIGENCE, STRICT LIABILITY, CONTRIBUTION, BREACH OF WARRANTY, OR
OTHER LEGAL EQUITABLE THEORY ANY DIRECT OR INDIRECT DAMAGES OR EXPENSES
INCLUDING BUT NOT LIMITED TO ANY INCIDENTAL, SPECIAL, INDIRECT, PUNITIVE OR
CONSEQUENTIAL DAMAGES, LOST PROFITS OR LOST DATA, COST OF PROCUREMENT OF
SUBSTITUTE GOODS, TECHNOLOGY, SERVICES, OR ANY CLAIMS BY THIRD PARTIES
(INCLUDING BUT NOT LIMITED TO ANY DEFENSE THEREOF), OR OTHER SIMILAR COSTS.
 *******************************************************************************/
// DOM-IGNORE-END
/*
*********************************************************************************************************
*                                           INCLUDES
*********************************************************************************************************
*/
#include <xc.h>



#include "ISR_Support.h"

 /* Port A Change Notice Interrupt */
   .extern  IntHandlerChangeNotification_PortA

   .section	.vector_118,code, keep
   .equ     __vector_dispatch_118, IntVectorChangeNotification_PortA
   .global  __vector_dispatch_118
   .set     nomicromips
   .set     noreorder
   .set     nomips16
   .set     noat
   .ent  IntVectorChangeNotification_PortA

IntVectorChangeNotification_PortA:
    portSAVE_CONTEXT
    la    s6,  IntHandlerChangeNotification_PortA
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end	IntVectorChangeNotification_PortA


 
 /* Port B Change Notice Interrupt */
   .extern  IntHandlerChangeNotification_PortB

   .section	.vector_119,code, keep
   .equ     __vector_dispatch_119, IntVectorChangeNotification_PortB
   .global  __vector_dispatch_119
   .set     nomicromips
   .set     noreorder
   .set     nomips16
   .set     noat
   .ent  IntVectorChangeNotification_PortB

IntVectorChangeNotification_PortB:
    portSAVE_CONTEXT
    la    s6,  IntHandlerChangeNotification_PortB
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end	IntVectorChangeNotification_PortB


 



 /* Port F Change Notice Interrupt */
   .extern  IntHandlerChangeNotification_PortF

   .section	.vector_123,code, keep
   .equ     __vector_dispatch_123, IntVectorChangeNotification_PortF
   .global  __vector_dispatch_123
   .set     nomicromips
   .set     noreorder
   .set     nomips16
   .set     noat
   .ent  IntVectorChangeNotification_PortF

IntVectorChangeNotification_PortF:
    portSAVE_CONTEXT
    la    s6,  IntHandlerChangeNotification_PortF
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end	IntVectorChangeNotification_PortF


 






/* External Interrupt Instance 0 Interrupt */
   .extern  IntHandlerExternalInterruptInstance0

   .section	.vector_23,code, keep
   .equ     __vector_dispatch_23, IntVectorExternalInterruptInstance0
   .global  __vector_dispatch_23
   .set     nomicromips
   .set     noreorder
   .set     nomips16
   .set     noat
   .ent  IntVectorExternalInterruptInstance0

IntVectorExternalInterruptInstance0:
    portSAVE_CONTEXT
    la    s6,  IntHandlerExternalInterruptInstance0
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end	IntVectorExternalInterruptInstance0


 

/* TMR Instance 0 Interrupt */
   .extern  IntHandlerDrvTmrInstance0

   .section	.vector_9,code, keep
   .equ     __vector_dispatch_9, IntVectorDrvTmrInstance0
   .global  __vector_dispatch_9
   .set     nomicromips
   .set     noreorder
   .set     nomips16
   .set     noat
   .ent  IntVectorDrvTmrInstance0

IntVectorDrvTmrInstance0:
    portSAVE_CONTEXT
    la    s6,  IntHandlerDrvTmrInstance0
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end	IntVectorDrvTmrInstance0


/* TMR Instance 1 Interrupt */
   .extern  IntHandlerDrvTmrInstance1

   .section	.vector_24,code, keep
   .equ     __vector_dispatch_24, IntVectorDrvTmrInstance1
   .global  __vector_dispatch_24
   .set     nomicromips
   .set     noreorder
   .set     nomips16
   .set     noat
   .ent  IntVectorDrvTmrInstance1

IntVectorDrvTmrInstance1:
    portSAVE_CONTEXT
    la    s6,  IntHandlerDrvTmrInstance1
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end	IntVectorDrvTmrInstance1


/* TMR Instance 2 Interrupt */
   .extern  IntHandlerDrvTmrInstance2

   .section	.vector_32,code, keep
   .equ     __vector_dispatch_32, IntVectorDrvTmrInstance2
   .global  __vector_dispatch_32
   .set     nomicromips
   .set     noreorder
   .set     nomips16
   .set     noat
   .ent  IntVectorDrvTmrInstance2

IntVectorDrvTmrInstance2:
    portSAVE_CONTEXT
    la    s6,  IntHandlerDrvTmrInstance2
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end	IntVectorDrvTmrInstance2




/* SPI Instance 0 Interrupt */
   .extern  IntHandlerSPIRxInstance0

   .section	.vector_164,code, keep
   .equ     __vector_dispatch_164, IntVectorSPIRxInstance0
   .global  __vector_dispatch_164
   .set     nomicromips
   .set     noreorder
   .set     nomips16
   .set     noat
   .ent  IntVectorSPIRxInstance0

IntVectorSPIRxInstance0:
    portSAVE_CONTEXT
    la    s6,  IntHandlerSPIRxInstance0
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end	IntVectorSPIRxInstance0


   .extern  IntHandlerSPITxInstance0

   .section	.vector_165,code, keep
   .equ     __vector_dispatch_165, IntVectorSPITxInstance0
   .global  __vector_dispatch_165
   .set     nomicromips
   .set     noreorder
   .set     nomips16
   .set     noat
   .ent  IntVectorSPITxInstance0

IntVectorSPITxInstance0:
    portSAVE_CONTEXT
    la    s6,  IntHandlerSPITxInstance0
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end	IntVectorSPITxInstance0


   .extern  IntHandlerSPIFaultInstance0

   .section	.vector_163,code, keep
   .equ     __vector_dispatch_163, IntVectorSPIFaultInstance0
   .global  __vector_dispatch_163
   .set     nomicromips
   .set     noreorder
   .set     nomips16
   .set     noat
   .ent  IntVectorSPIFaultInstance0

IntVectorSPIFaultInstance0:
    portSAVE_CONTEXT
    la    s6,  IntHandlerSPIFaultInstance0
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end	IntVectorSPIFaultInstance0


/* SPI Instance 1 Interrupt */
   .extern  IntHandlerSPIRxInstance1

   .section	.vector_186,code, keep
   .equ     __vector_dispatch_186, IntVectorSPIRxInstance1
   .global  __vector_dispatch_186
   .set     nomicromips
   .set     noreorder
   .set     nomips16
   .set     noat
   .ent  IntVectorSPIRxInstance1

IntVectorSPIRxInstance1:
    portSAVE_CONTEXT
    la    s6,  IntHandlerSPIRxInstance1
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end	IntVectorSPIRxInstance1


   .extern  IntHandlerSPITxInstance1

   .section	.vector_187,code, keep
   .equ     __vector_dispatch_187, IntVectorSPITxInstance1
   .global  __vector_dispatch_187
   .set     nomicromips
   .set     noreorder
   .set     nomips16
   .set     noat
   .ent  IntVectorSPITxInstance1

IntVectorSPITxInstance1:
    portSAVE_CONTEXT
    la    s6,  IntHandlerSPITxInstance1
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end	IntVectorSPITxInstance1


   .extern  IntHandlerSPIFaultInstance1

   .section	.vector_185,code, keep
   .equ     __vector_dispatch_185, IntVectorSPIFaultInstance1
   .global  __vector_dispatch_185
   .set     nomicromips
   .set     noreorder
   .set     nomips16
   .set     noat
   .ent  IntVectorSPIFaultInstance1

IntVectorSPIFaultInstance1:
    portSAVE_CONTEXT
    la    s6,  IntHandlerSPIFaultInstance1
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end	IntVectorSPIFaultInstance1


/* NVM Interrupt */
   .extern  IntHandlerDrvNvm

   .section	.vector_167,code, keep
   .equ     __vector_dispatch_167, IntVectorDrvNvm
   .global  __vector_dispatch_167
   .set     nomicromips
   .set     noreorder
   .set     nomips16
   .set     noat
   .ent  IntVectorDrvNvm

IntVectorDrvNvm:
    portSAVE_CONTEXT
    la    s6,  IntHandlerDrvNvm
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end	IntVectorDrvNvm



/* Sytem DMA Instance 0 Interrupt */
   .extern  IntHandlerSysDmaInstance0

   .section	.vector_134,code, keep
   .equ     __vector_dispatch_134, IntVectorSysDmaInstance0
   .global  __vector_dispatch_134
   .set     nomicromips
   .set     noreorder
   .set     nomips16
   .set     noat
   .ent  IntVectorSysDmaInstance0

IntVectorSysDmaInstance0:
    portSAVE_CONTEXT
    la    s6,  IntHandlerSysDmaInstance0
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end	IntVectorSysDmaInstance0


/* Sytem DMA Instance 1 Interrupt */
   .extern  IntHandlerSysDmaInstance1

   .section	.vector_135,code, keep
   .equ     __vector_dispatch_135, IntVectorSysDmaInstance1
   .global  __vector_dispatch_135
   .set     nomicromips
   .set     noreorder
   .set     nomips16
   .set     noat
   .ent  IntVectorSysDmaInstance1

    IntVectorSysDmaInstance1:
    portSAVE_CONTEXT
    la    s6,  IntHandlerSysDmaInstance1
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end	IntVectorSysDmaInstance1
    


    /* I2C Instance 0 Interrupt */

   .extern  IntHandlerDrvI2CMasterInstance0

   .section	.vector_184,code, keep
   .equ     __vector_dispatch_184, IntVectorDrvI2CMasterInstance0
   .global  __vector_dispatch_184
   .set     nomicromips
   .set     noreorder
   .set     nomips16
   .set     noat
   .ent  IntVectorDrvI2CMasterInstance0

IntVectorDrvI2CMasterInstance0:
    portSAVE_CONTEXT
    la    s6,  IntHandlerDrvI2CMasterInstance0
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end	IntVectorDrvI2CMasterInstance0


   .extern  IntHandlerDrvI2CErrorInstance0

   .section	.vector_182,code, keep
   .equ     __vector_dispatch_182, IntVectorDrvI2CErrorInstance0
   .global  __vector_dispatch_182
   .set     nomicromips
   .set     noreorder
   .set     nomips16
   .set     noat
   .ent  IntVectorDrvI2CErrorInstance0

IntVectorDrvI2CErrorInstance0:
    portSAVE_CONTEXT
    la    s6,  IntHandlerDrvI2CErrorInstance0
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end	IntVectorDrvI2CErrorInstance0
    
    

/* USB Device Interrupt */
   .extern  IntHandlerUSBInstance0

   .section	.vector_132,code, keep
   .equ     __vector_dispatch_132, IntVectorUSBInstance0
   .global  __vector_dispatch_132
   .set     nomicromips
   .set     noreorder
   .set     nomips16
   .set     noat
   .ent  IntVectorUSBInstance0

IntVectorUSBInstance0:
    portSAVE_CONTEXT
    la    s6,  IntHandlerUSBInstance0
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end	IntVectorUSBInstance0


   .extern  IntHandlerUSBInstance0_USBDMA

   .section	.vector_133,code, keep
   .equ     __vector_dispatch_133, IntVectorUSBInstance0_USBDMA
   .global  __vector_dispatch_133
   .set     nomicromips
   .set     noreorder
   .set     nomips16
   .set     noat
   .ent  IntVectorUSBInstance0_USBDMA

IntVectorUSBInstance0_USBDMA:
    portSAVE_CONTEXT
    la    s6,  IntHandlerUSBInstance0_USBDMA
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end	IntVectorUSBInstance0_USBDMA
    
/* ADC EOS Interrupt */
   .extern  IntHandlerDrvAdcEOS

   .section	.vector_192,code, keep
   .equ     __vector_dispatch_192, IntVectorDrvAdcEOS
   .global  __vector_dispatch_192
   .set     nomicromips
   .set     noreorder
   .set     nomips16
   .set     noat
   .ent  IntVectorDrvAdcEOS

IntVectorDrvAdcEOS:
    portSAVE_CONTEXT
    la    s6,  IntHandlerDrvAdcEOS
    jalr  s6
    nop
    portRESTORE_CONTEXT
    .end	IntVectorDrvAdcEOS

/* RTCC Interrupt */
//  Commenting out because compiler complains past version 2.10
//   .extern  IntHandlerSysRtcc

//   .section	.vector_166,code, keep
//   .equ     __vector_dispatch_166, IntHandlerSysRtcc
//   .global  __vector_dispatch_166
//   .set     nomicromips
//   .set     noreorder
//   .set     nomips16
//   .set     noat
//   .ent  IntHandlerSysRtcc

//IntHandlerSysRtcc:
//    portSAVE_CONTEXT
//    la    s6,  IntHandlerSysRtcc
//    jalr  s6
//    nop
//    portRESTORE_CONTEXT
//    .end	IntHandlerSysRtcc
/*******************************************************************************
 End of File
 */

