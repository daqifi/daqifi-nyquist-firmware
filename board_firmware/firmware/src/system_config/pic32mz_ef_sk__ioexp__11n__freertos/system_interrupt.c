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


 
void IntHandlerDrvI2CMasterInstance0(void)
{
    DRV_I2C_Tasks(sysObj.drvI2C0);
}


void IntHandlerDrvI2CErrorInstance0(void)
{
    SYS_ASSERT(false, "I2C Driver Instance 0 Error");
}


void IntHandlerChangeNotification_PortA(void)
{
    int * boardDataInISR = BoardData_Get(                                      \
                            BOARDDATA_IN_ISR,                               \
                            0 );
    tPowerData * pPowerData = BoardData_Get(                                \
                            BOARDATA_POWER_DATA,                            \
                            0 ); 
    ++(*boardDataInISR);
    
    // Read port to clear mismatch condition on BATT_MAN_INT
    if(PLIB_PORTS_PinGet(PORTS_ID_0, BATT_MAN_INT_PORT, BATT_MAN_INT_PIN)) 
    {
        // Battery management interrupt has occurred
        // Set intFlag so we can handle it in the main power task
        pPowerData->BQ24297Data.intFlag = true;   
    }
    PLIB_INT_SourceFlagClear(INT_ID_0,INT_SOURCE_CHANGE_NOTICE_A);
    --(*boardDataInISR);
}
void IntHandlerChangeNotification_PortB(void)
{
    int * boardDataInISR = BoardData_Get(                                      \
                            BOARDDATA_IN_ISR,                               \
                            0 );
    ++(*boardDataInISR);
    
    // Read port to clear mismatch condition
    if(PLIB_PORTS_PinGet(PORTS_ID_0, AD7609_BUSY_PORT, AD7609_BUSY_PIN)) 
    {
        // AD7609 interrupt has occurred
    }
    PLIB_INT_SourceFlagClear(INT_ID_0,INT_SOURCE_CHANGE_NOTICE_B);
    --(*boardDataInISR);
}
void IntHandlerChangeNotification_PortF(void)
{
    int * boardDataInISR = BoardData_Get(                                      \
                            BOARDDATA_IN_ISR,                               \
                            0 );
    
    ++(*boardDataInISR);
    if(PLIB_PORTS_PinGet(PORTS_ID_0, PORT_CHANNEL_F, PORTS_BIT_POS_2)) // Read port to clear mismatch condition
    {
        // SDI6 signal caused interrupt
    }
    PLIB_INT_SourceFlagClear(INT_ID_0,INT_SOURCE_CHANGE_NOTICE_F);
    --(*boardDataInISR);
}


void IntHandlerSysDmaInstance0(void)
{  
    int * boardDataInISR = BoardData_Get(                                      \
                            BOARDDATA_IN_ISR,                               \
                            0 );
    ++(*boardDataInISR);
    SYS_DMA_Tasks(sysObj.sysDma, DMA_CHANNEL_0);
    --(*boardDataInISR);
}

void IntHandlerSysDmaInstance1(void)
{
    int * boardDataInISR = BoardData_Get(                                      \
                            BOARDDATA_IN_ISR,                               \
                            0 );
    ++(*boardDataInISR);
    SYS_DMA_Tasks(sysObj.sysDma, DMA_CHANNEL_1);
    --(*boardDataInISR);
}


void IntHandlerSysRtcc (void)
{
    int * boardDataInISR = BoardData_Get(                                      \
                            BOARDDATA_IN_ISR,                               \
                            0 );
    ++(*boardDataInISR);
    SYS_RTCC_Tasks(sysObj.sysRtcc);
    --(*boardDataInISR);
}
void IntHandlerExternalInterruptInstance0(void)
{
    int * boardDataInISR = BoardData_Get(                                      \
                            BOARDDATA_IN_ISR,                               \
                            0 );
    ++(*boardDataInISR);
    PLIB_INT_SourceFlagClear(INT_ID_0, INT_SOURCE_EXTERNAL_4);
    WDRV_WINC1500_ISR();
    --(*boardDataInISR);
}
 

void IntHandlerDrvTmrInstance0(void)
{
    int * boardDataInISR = BoardData_Get(                                      \
                            BOARDDATA_IN_ISR,                               \
                            0 );
    ++(*boardDataInISR);
    DRV_TMR_Tasks(sysObj.drvTmr0);
    --(*boardDataInISR);
}
void IntHandlerDrvTmrInstance1(void)
{
    int * boardDataInISR = BoardData_Get(                                      \
                            BOARDDATA_IN_ISR,                               \
                            0 );
    ++(*boardDataInISR);
    DRV_TMR_Tasks(sysObj.drvTmr1);
    --(*boardDataInISR);
}
void IntHandlerDrvTmrInstance2(void)
{
    int * boardDataInISR = BoardData_Get(                                      \
                            BOARDDATA_IN_ISR,                               \
                            0 );
    ++(*boardDataInISR);
    DRV_TMR_Tasks(sysObj.drvTmr2);
    --(*boardDataInISR);
}
 
void IntHandlerSPIRxInstance0(void)
{
    int * boardDataInISR = BoardData_Get(                                      \
                            BOARDDATA_IN_ISR,                               \
                            0 );
    ++(*boardDataInISR);
    DRV_SPI_Tasks(sysObj.spiObjectIdx0);
    --(*boardDataInISR);
}
void IntHandlerSPITxInstance0(void)
{
    int * boardDataInISR = BoardData_Get(                                      \
                            BOARDDATA_IN_ISR,                               \
                            0 );
    ++(*boardDataInISR);
    DRV_SPI_Tasks(sysObj.spiObjectIdx0);
    --(*boardDataInISR);
}
void IntHandlerSPIFaultInstance0(void)
{
    int * boardDataInISR = BoardData_Get(                                      \
                            BOARDDATA_IN_ISR,                               \
                            0 );
    ++(*boardDataInISR);
    DRV_SPI_Tasks(sysObj.spiObjectIdx0);
    --(*boardDataInISR);
}
void IntHandlerSPIRxInstance1(void)
{
    int * boardDataInISR = BoardData_Get(                                      \
                            BOARDDATA_IN_ISR,                               \
                            0 );
    ++(*boardDataInISR);
    DRV_SPI_Tasks(sysObj.spiObjectIdx1);
    --(*boardDataInISR);
}
void IntHandlerSPITxInstance1(void)
{
    int * boardDataInISR = BoardData_Get(                                      \
                            BOARDDATA_IN_ISR,                               \
                            0 );
    ++(*boardDataInISR);
    DRV_SPI_Tasks(sysObj.spiObjectIdx1);
    --(*boardDataInISR);
}
void IntHandlerSPIFaultInstance1(void)
{
    int * boardDataInISR = BoardData_Get(                                      \
                            BOARDDATA_IN_ISR,                               \
                            0 );
    ++(*boardDataInISR);
    DRV_SPI_Tasks(sysObj.spiObjectIdx1);
    --(*boardDataInISR);
}

void IntHandlerDrvNvm (void)
{
    int * boardDataInISR = BoardData_Get(                                      \
                            BOARDDATA_IN_ISR,                               \
                            0 );
    ++(*boardDataInISR);
    DRV_NVM_Tasks(sysObj.drvNvm);
    --(*boardDataInISR);

}

void IntHandlerUSBInstance0(void)
{
    int * boardDataInISR = BoardData_Get(                                      \
                            BOARDDATA_IN_ISR,                               \
                            0 );
    ++(*boardDataInISR);
    DRV_USBHS_Tasks_ISR(sysObj.drvUSBObject);
    --(*boardDataInISR);
}

void IntHandlerUSBInstance0_USBDMA ( void )
{
    int * boardDataInISR = BoardData_Get(                                      \
                            BOARDDATA_IN_ISR,                               \
                            0 );
    ++(*boardDataInISR);
    DRV_USBHS_Tasks_ISR_USBDMA(sysObj.drvUSBObject);
    --(*boardDataInISR);
}

void IntHandlerDrvAdcEOS(void)
{
    int * boardDataInISR = BoardData_Get(                                      \
                            BOARDDATA_IN_ISR,                               \
                            0 );
    ++(*boardDataInISR);
    
    // Scanning of ADC channels is complete - so read data

    // Clear EOS interrupt flag in INT reg
    PLIB_INT_SourceFlagClear(INT_ID_0, INT_SOURCE_ADC_END_OF_SCAN);

    
    // Apparently something else might share this interrupt, so check to make sure the scan is complete!
    if(PLIB_ADCHS_AnalogInputScanIsComplete(ADCHS_ID_0))
    {
        uint32_t dummyADCCON2 = ADCCON2;    // Clear Scan Complete Interrupt Flag (the only way to do this is to read from ADCCON2)
        UNUSED(dummyADCCON2);
        
        // Tell the app to read the results
        /*const AInModule* module = ADC_FindModule(&g_BoardConfig.AInModules, AIn_MC12bADC);
        ADC_ConversionComplete(module);*/
        _ADC_Defer_Interrupt();
    }
    
    --(*boardDataInISR);
}

void _ISR_DefaultInterrupt(void) 
{
    SYS_DEBUG_BreakPoint();
}
/*******************************************************************************
 End of File
*/
