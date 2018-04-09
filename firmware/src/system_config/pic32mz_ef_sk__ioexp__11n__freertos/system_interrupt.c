/*******************************************************************************
 System Interrupts File

  File Name:
    system_interrupt.c

  Summary:
    Raw ISR definitions.

  Description:
    This file contains a definitions of the raw ISRs required to support the
    interrupt sub-system.

  Summary:
    This file contains source code for the interrupt vector functions in the
    system.

  Description:
    This file contains source code for the interrupt vector functions in the
    system.  It implements the system and part specific vector "stub" functions
    from which the individual "Tasks" functions are called for any modules
    executing interrupt-driven in the MPLAB Harmony system.

  Remarks:
    This file requires access to the systemObjects global data structure that
    contains the object handles to all MPLAB Harmony module objects executing
    interrupt-driven in the system.  These handles are passed into the individual
    module "Tasks" functions to identify the instance of the module to maintain.
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

SOFTWARE AND DOCUMENTATION ARE PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND,
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

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************

#include "system/common/sys_common.h"
#include "app.h"
#include "system_definitions.h"

#include "HAL/ADC.h"

#define UNUSED(x) (void)(x)

// *****************************************************************************
// *****************************************************************************
// Section: System Interrupt Vector Functions
// *****************************************************************************
// *****************************************************************************
//         void IntHandlerChangeNotification_PortB(void)
//{
//    
//    PLIB_INT_SourceFlagClear(INT_ID_0,INT_SOURCE_CHANGE_NOTICE_B);
//}
//         void IntHandlerChangeNotification_PortF(void)
//{
//    
//    PLIB_INT_SourceFlagClear(INT_ID_0,INT_SOURCE_CHANGE_NOTICE_F);
//}


void IntHandlerSysDmaInstance0(void)
{          
    SYS_DMA_Tasks(sysObj.sysDma, DMA_CHANNEL_0);
}

void IntHandlerSysDmaInstance1(void)
{          
    SYS_DMA_Tasks(sysObj.sysDma, DMA_CHANNEL_1);
}


//void __ISR(_RTCC_VECTOR, ipl1AUTO) _IntHandlerSysRtcc (void)
//{
//    SYS_RTCC_Tasks(sysObj.sysRtcc);
//}
void IntHandlerExternalInterruptInstance0(void)
{
    PLIB_INT_SourceFlagClear(INT_ID_0, INT_SOURCE_EXTERNAL_4);
    WDRV_MRF24WN_ISR();
}
 

void IntHandlerDrvTmrInstance0(void)
{
    DRV_TMR_Tasks(sysObj.drvTmr0);
}
void IntHandlerDrvTmrInstance1(void)
{
    DRV_TMR_Tasks(sysObj.drvTmr1);
}
void IntHandlerDrvTmrInstance2(void)
{
    DRV_TMR_Tasks(sysObj.drvTmr2);
}
 
void IntHandlerSPIRxInstance0(void)
{
    DRV_SPI_Tasks(sysObj.spiObjectIdx0);
}
void IntHandlerSPITxInstance0(void)
{
    DRV_SPI_Tasks(sysObj.spiObjectIdx0);
}
void IntHandlerSPIFaultInstance0(void)
{
    DRV_SPI_Tasks(sysObj.spiObjectIdx0);
}
//void IntHandlerSPIRxInstance1(void)
//{
//    DRV_SPI_Tasks(sysObj.spiObjectIdx1);
//}
//void IntHandlerSPITxInstance1(void)
//{
//    DRV_SPI_Tasks(sysObj.spiObjectIdx1);
//}
//void IntHandlerSPIFaultInstance1(void)
//{
//    DRV_SPI_Tasks(sysObj.spiObjectIdx1);
//}

void IntHandlerDrvNvm (void)

{
    DRV_NVM_Tasks(sysObj.drvNvm);

}

void IntHandlerUSBInstance0(void)

{
    DRV_USBHS_Tasks_ISR(sysObj.drvUSBObject);
}

void IntHandlerUSBInstance0_USBDMA ( void )
{
    DRV_USBHS_Tasks_ISR_USBDMA(sysObj.drvUSBObject);
}

void IntHandlerDrvAdcEOS(void)
{
 //   ++g_BoardData.InISR;
    
    // Scanning of ADC channels is complete - so read data

    // Clear EOS interrupt flag in INT reg
    PLIB_INT_SourceFlagClear(INT_ID_0, INT_SOURCE_ADC_END_OF_SCAN);
    
    uint32_t dummyADCCON2 = ADCCON2;    // Clear Scan Complete Interrupt Flag (the only way to do this is to read from ADCCON2)
    UNUSED(dummyADCCON2);
    
    g_BoardData.PowerData.MCP73871Data.chargeAllowed = true;
    MCP73871_ChargeEnable(g_BoardConfig.PowerConfig.MCP73871Config,
            &g_BoardData.PowerData.MCP73871Data,
            &g_BoardRuntimeConfig.PowerWriteVars.MCP73871WriteVars,
            true, g_BoardData.PowerData.pONBattPresent);
    
    // Tell the app to read the results
    const AInModule* module = ADC_FindModule(&g_BoardConfig.AInModules, AIn_MC12bADC);
    ADC_ConversionComplete(module);
    
//    --g_BoardData.InISR;
}

void _ISR_DefaultInterrupt(void) 
{
    SYS_DEBUG_BreakPoint();
}
/*******************************************************************************
 End of File
*/
