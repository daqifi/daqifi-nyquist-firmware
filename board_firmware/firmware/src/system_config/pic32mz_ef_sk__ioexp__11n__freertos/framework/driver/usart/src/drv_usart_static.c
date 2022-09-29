/*******************************************************************************
  USART Driver Static implementation

  Company:
    Microchip Technology Inc.

  File Name:
    drv_usart_static.c

  Summary:
    Source code for the USART driver static implementation.

  Description:
    The USART device driver provides a simple interface to manage the USART
    modules on Microchip microcontrollers. This file contains static implementation
    for the USART driver.

  Remarks:
    Static interfaces incorporate the driver instance number within the names
    of the routines, eliminating the need for an object ID or object handle.

    Static single-open interfaces also eliminate the need for the open handle.
*******************************************************************************/

//DOM-IGNORE-BEGIN
/*******************************************************************************
Copyright (c) 2015 released Microchip Technology Inc.  All rights reserved.

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
//DOM-IGNORE-END

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************

#include "system_config.h"
#include "system_definitions.h"


// *****************************************************************************
// *****************************************************************************
// Section: Global Data
// *****************************************************************************
// *****************************************************************************

/* This is the driver static object . */
DRV_USART_OBJ  gDrvUSART0Obj ;

// *****************************************************************************
// *****************************************************************************
// Section: Instance 0 static driver functions
// *****************************************************************************
// *****************************************************************************

SYS_MODULE_OBJ DRV_USART0_Initialize(void)
{
    uint32_t clockSource;

    DRV_USART_OBJ *dObj = (DRV_USART_OBJ*)NULL;
    dObj = &gDrvUSART0Obj;

    /* Disable the USART module to configure it*/
    PLIB_USART_Disable (USART_ID_4);

    /* Create the hardware instance mutex. */
    if(OSAL_MUTEX_Create(&(dObj->mutexDriverInstance)) != OSAL_RESULT_TRUE)
    {
        return SYS_MODULE_OBJ_INVALID;
    }

    /* Initialize the USART based on configuration settings */
    PLIB_USART_InitializeModeGeneral(USART_ID_4,
            false,  /*Auto baud*/
            false,  /*LoopBack mode*/
            false,  /*Auto wakeup on start*/
            false,  /*IRDA mode*/
            false);  /*Stop In Idle mode*/

    /* Set the line control mode */
    PLIB_USART_LineControlModeSelect(USART_ID_4, DRV_USART_LINE_CONTROL_8NONE1);

    /* We set the receive interrupt mode to receive an interrupt whenever FIFO
       is not empty */
    PLIB_USART_InitializeOperation(USART_ID_4,
            USART_RECEIVE_FIFO_ONE_CHAR,
            USART_TRANSMIT_FIFO_IDLE,
            USART_ENABLE_TX_RX_USED);

    /* Get the USART clock source value*/
    clockSource = SYS_CLK_PeripheralFrequencyGet ( CLK_BUS_PERIPHERAL_2 );

    /* Set the baud rate and enable the USART */
    PLIB_USART_BaudSetAndEnable(USART_ID_4,
            clockSource,
            921600);  /*Desired Baud rate value*/

    /* Return the driver instance value*/
    return (SYS_MODULE_OBJ)DRV_USART_INDEX_0;
}

void  DRV_USART0_Deinitialize(void)
{
    DRV_USART_OBJ *dObj = (DRV_USART_OBJ*)NULL;

    dObj = &gDrvUSART0Obj;

    /* Deallocate all mutexes */
    if(OSAL_MUTEX_Delete(&(dObj->mutexDriverInstance)) != OSAL_RESULT_TRUE)
    {
        SYS_DEBUG_MESSAGE(SYS_ERROR_DEBUG, "\r\nUSART Driver: Mutex Delete Failed");
        return;
    }
    /* Disable USART module */
    PLIB_USART_Disable (USART_ID_4);

}


SYS_STATUS DRV_USART0_Status(void)
{
    /* Return the status as ready always */
    return SYS_STATUS_READY;
}

DRV_HANDLE DRV_USART0_Open(const SYS_MODULE_INDEX index, const DRV_IO_INTENT ioIntent)
{

    /* Return the driver instance value*/
    return ((DRV_HANDLE)DRV_USART_INDEX_0 );
}

void DRV_USART0_Close(void)
{
    return;
}

DRV_USART_CLIENT_STATUS DRV_USART0_ClientStatus(void)
{
    /* Return the status as ready always*/
    return DRV_USART_CLIENT_STATUS_READY;
}

DRV_USART_TRANSFER_STATUS DRV_USART0_TransferStatus( void )
{
    DRV_USART_TRANSFER_STATUS result = 0;

    /* Check if RX data available */
    if(PLIB_USART_ReceiverDataIsAvailable(USART_ID_4))
    {
        result|= DRV_USART_TRANSFER_STATUS_RECEIVER_DATA_PRESENT;
    }
    else
    {
        result|= DRV_USART_TRANSFER_STATUS_RECEIVER_EMPTY;
    }

    /* Check if TX Buffer is empty */
    if(PLIB_USART_TransmitterIsEmpty(USART_ID_4))
    {
        result|= DRV_USART_TRANSFER_STATUS_TRANSMIT_EMPTY;
    }

    /* Check if the TX buffer is full */
    if(PLIB_USART_TransmitterBufferIsFull(USART_ID_4))
    {
        result|= DRV_USART_TRANSFER_STATUS_TRANSMIT_FULL;
    }

    return(result);
}

DRV_USART_ERROR DRV_USART0_ErrorGet(void)
{
    DRV_USART_ERROR error;
    error = gDrvUSART0Obj.error;

    /* Clear the error before returning */
    gDrvUSART0Obj.error = DRV_USART_ERROR_NONE;

    /* Return the error*/
    return(error);
}


void _DRV_USART0_ErrorConditionClear()
{
    uint8_t dummyData = 0u;
    /* RX length = (FIFO level + RX register) */
    uint8_t RXlength = _DRV_USART_RX_DEPTH;

    /* If it's a overrun error then clear it to flush FIFO */
    if(USART_ERROR_RECEIVER_OVERRUN & PLIB_USART_ErrorsGet(USART_ID_4))
    {
        PLIB_USART_ReceiverOverrunErrorClear(USART_ID_4);
    }

    /* Read existing error bytes from FIFO to clear parity and framing error flags*/
    while( (USART_ERROR_PARITY | USART_ERROR_FRAMING) & PLIB_USART_ErrorsGet(USART_ID_4) )
    {
        dummyData = PLIB_USART_ReceiverByteReceive(USART_ID_4);
        RXlength--;

        /* Try to flush error bytes for one full FIFO and exit instead of
         * blocking here if more error bytes are received*/
        if(0u == RXlength)
        {
            break;
        }
    }

    /* Ignore the warning */
    (void)dummyData;

    /* Clear error interrupt flag */
    SYS_INT_SourceStatusClear(INT_SOURCE_USART_4_ERROR);

    /* Clear up the receive interrupt flag so that RX interrupt is not
     * triggered for error bytes*/
    SYS_INT_SourceStatusClear(INT_SOURCE_USART_4_RECEIVE);
}



DRV_USART_BAUD_SET_RESULT DRV_USART0_BaudSet(uint32_t baud)
{
    uint32_t clockSource;
    int32_t brgValueLow=0;
    int32_t brgValueHigh=0;
    DRV_USART_BAUD_SET_RESULT retVal = DRV_USART_BAUD_SET_SUCCESS;
#if defined (PLIB_USART_ExistsModuleBusyStatus)
    bool isEnabled = false;
#endif

    /* Making this function thread safe */
    if(OSAL_MUTEX_Lock(&(gDrvUSART0Obj.mutexDriverInstance), OSAL_WAIT_FOREVER) == OSAL_RESULT_TRUE)
    {
        /* We were able to take the mutex */
    }
    else
    {
        /* The mutex timed out */
        SYS_DEBUG_MESSAGE(SYS_ERROR_DEBUG, "\r\nUSART Driver: Hardware instance mutex time out in DRV_USART_BaudSet() function");
        return(DRV_USART_BAUD_SET_ERROR);
    }

    /* Get the USART clock source value*/
    clockSource = SYS_CLK_PeripheralFrequencyGet ( CLK_BUS_PERIPHERAL_2 );

    /* Calculate low and high baud values */
    brgValueLow  = ( (clockSource/baud) >> 4 ) - 1;
    brgValueHigh = ( (clockSource/baud) >> 2 ) - 1;

#if defined (PLIB_USART_ExistsModuleBusyStatus)
        isEnabled = PLIB_USART_ModuleIsBusy (USART_ID_4);
        if (isEnabled)
        {
            PLIB_USART_Disable (USART_ID_4);
            while (PLIB_USART_ModuleIsBusy (USART_ID_4));
        }
#endif

    /* Check if the baud value can be set with high baud settings */
    if ((brgValueHigh >= 0) && (brgValueHigh <= UINT16_MAX))
    {
        PLIB_USART_BaudRateHighEnable(USART_ID_4);
        PLIB_USART_BaudRateHighSet(USART_ID_4,clockSource,baud);
    }

    /* Check if the baud value can be set with low baud settings */
    else if ((brgValueLow >= 0) && (brgValueLow <= UINT16_MAX))
    {
        PLIB_USART_BaudRateHighDisable(USART_ID_4);
        PLIB_USART_BaudRateSet(USART_ID_4, clockSource, baud);
    }
    else
    {
            retVal = DRV_USART_BAUD_SET_ERROR;
    }

#if defined (PLIB_USART_ExistsModuleBusyStatus)
    if (isEnabled)
    {
        PLIB_USART_Enable (USART_ID_4);
    }
#endif
    /* Unlock Mutex */
    OSAL_MUTEX_Unlock(&(gDrvUSART0Obj.mutexDriverInstance));

    return retVal;
}


DRV_USART_LINE_CONTROL_SET_RESULT DRV_USART0_LineControlSet(DRV_USART_LINE_CONTROL lineControlMode)
{
#if defined (PLIB_USART_ExistsModuleBusyStatus)
    bool isEnabled = false;
#endif
    /* Making this function thread safe */
    if(OSAL_MUTEX_Lock(&(gDrvUSART0Obj.mutexDriverInstance), OSAL_WAIT_FOREVER) == OSAL_RESULT_TRUE)
    {
        /* We were able to take the mutex */
    }
    else
    {
        SYS_DEBUG_MESSAGE(SYS_ERROR_DEBUG, "\r\nUSART Driver: Hardware Instance Mutex time out in DRV_USART_LineControlSet() function");
        return DRV_USART_LINE_CONTROL_SET_ERROR;
    }
#if defined (PLIB_USART_ExistsModuleBusyStatus)
        isEnabled = PLIB_USART_ModuleIsBusy (USART_ID_4);
        if (isEnabled)
        {
            PLIB_USART_Disable (USART_ID_4);
            while (PLIB_USART_ModuleIsBusy (USART_ID_4));
        }
#endif

    /* Set the Line Control Mode */
    PLIB_USART_LineControlModeSelect(USART_ID_4, lineControlMode);

#if defined (PLIB_USART_ExistsModuleBusyStatus)
        if (isEnabled)
        {
            PLIB_USART_Enable (USART_ID_4);
        }
#endif
    OSAL_MUTEX_Unlock(&(gDrvUSART0Obj.mutexDriverInstance));

    /* Return success */
    return(DRV_USART_LINE_CONTROL_SET_SUCCESS);
}

/*******************************************************************************
 End of File
*/
