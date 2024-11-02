/*******************************************************************************
  Company:
    Microchip Technology Inc.

  File Name:
    bootloader.c
    
  Summary:
    Interface for the Bootloader library.

  Description:
    This file contains the interface definition for the Bootloader library.
*******************************************************************************/

//DOM-IGNORE-BEGIN
/*******************************************************************************
Copyright (c) 2014 released Microchip Technology Inc.  All rights reserved.

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
// Section: Include Files
// *****************************************************************************
// *****************************************************************************
#include <sys/attribs.h>
#include <sys/kmem.h>

#include "bootloader/src/bootloader.h"
#include "peripheral/nvm/plib_nvm.h"
#include "peripheral/int/plib_int.h"
#include "system/devcon/sys_devcon.h"
#include "system/reset/sys_reset.h"

#if (BOOTLOADER_LIVE_UPDATE_SWITCHER == 1) || (BOOTLOADER_LIVE_UPDATE_STATE_SAVE == 1)

/* Structure to validate the Flash id and its checksum
 * Note: The order of the members should not be changed
 */
typedef struct flash_id
{
    uint32_t checksum_start;
    uint32_t flash_id;
    uint32_t checksum_end;
    uint32_t dummy;
} T_FLASH_ID;

#define LOWER_FLASH_ID_READ   ((T_FLASH_ID *)KVA0_TO_KVA1(LOWER_FLASH_ID_BASE_ADDRESS))
#define UPPER_FLASH_ID_READ   ((T_FLASH_ID *)KVA0_TO_KVA1(UPPER_FLASH_ID_BASE_ADDRESS))

#endif

#if(SYS_FS_MAX_FILES > 0)
uint8_t fileBuffer[512] __attribute__((coherent, aligned(16)));
#endif

BOOTLOADER_DATA bootloaderData __attribute__((coherent, aligned(16)));
BOOTLOADER_BUFFER data_buff __attribute__((coherent, aligned(16)));
#if defined(BOOTLOADER_LEGACY)
BOOTLOADER_STATES (*BootloaderTriggerCheck)(void) = NULL; // Start as NULL
#endif

void Bootloader_BufferEventHandler(DATASTREAM_BUFFER_EVENT buffEvent,
                            DATASTREAM_BUFFER_HANDLE hBufferEvent,
                            uint16_t context );

extern void APP_NVMQuadWordWrite(void* address, uint32_t* data);

#if(BOOTLOADER_LIVE_UPDATE_STATE_SAVE == 1)
static volatile uint32_t opp_flash_id;
/********************************************************************
* Function:     Get_Flash_Id()
*
* Precondition:
*
* Input:        None.
*
* Output:       returns the flash id of opposite panel
*
* Side Effects:
*
* Overview:     
********************************************************************/
uint32_t Get_Flash_Id(void)
{
    T_FLASH_ID *upper_flash_id = UPPER_FLASH_ID_READ;
    return (upper_flash_id->flash_id);
}

/********************************************************************
* Function:     Update_Flash_Id()
*
* Precondition:
*
* Input:        None.
*
* Output:
*
* Side Effects:
*
* Overview:     The function will increment and program the opposite panel flash id
*
********************************************************************/
void Update_Flash_Id(void)
{
    T_FLASH_ID flash_id = { 0 };
    /* Increment opposite flash id by 2 to be ahead of the current running panels flash ID*/
    flash_id.flash_id = opp_flash_id + 2;
    flash_id.checksum_start = FLASH_ID_CHECKSUM_START;
    flash_id.checksum_end = FLASH_ID_CHECKSUM_END;

    APP_NVMQuadWordWrite((void *)(KVA0_TO_KVA1(LOWER_FLASH_ID_BASE_ADDRESS)), (uint32_t *)&flash_id);
}
#endif

#if(BOOTLOADER_LIVE_UPDATE_SWITCHER == 1)
/********************************************************************
* Function:     SwitchFlashPanels()
*
* Precondition:
*
* Input:        None.
*
* Output:
*
* Side Effects:
*
* Overview:     The function will swap the flash panels in a safe
 *              place.
*
*
* Note:         This is placed in the .cache_init section in order
 *              to have it in boot flash rather than program flash.
********************************************************************/
void __longcall__ __attribute__ ((section (".cache_init"))) SwapFlashPanels()
{
    PLIB_NVM_MemoryModifyInhibit(NVM_ID_0);
    PLIB_NVM_FlashWriteKeySequence(NVM_ID_0, NVM_PROGRAM_UNLOCK_KEY1);
    PLIB_NVM_FlashWriteKeySequence(NVM_ID_0, NVM_PROGRAM_UNLOCK_KEY2);
    PLIB_NVM_ProgramFlashBank2LowerRegion(NVM_ID_0);
}
#endif

#if(BOOTLOADER_LIVE_UPDATE_STATE_SAVE != 1)
/********************************************************************
* Function:     Enter_Application()
*
* Precondition:
*
* Input:        None.
*
* Output:
*
* Side Effects: No return from here.
*
* Overview:     The function will program the checksum for the respective
 *              flash panel and jumps to application programmed in it.
*
*
* Note:
********************************************************************/
static void Enter_Application(void)
{
    void (*fptr)(void);

    /* Set default to APP_RESET_ADDRESS */
    fptr = (void (*)(void))APP_RESET_ADDRESS;

#if(BOOTLOADER_LIVE_UPDATE_SWITCHER == 1)
    T_FLASH_ID *lower_flash_id = LOWER_FLASH_ID_READ;
    T_FLASH_ID *upper_flash_id = UPPER_FLASH_ID_READ;

    SYS_DEVCON_InstructionCacheFlush();

    /* Application has been programmed first time into flash panel 1 by the bootloader */
    if( *(uint32_t *)LOWER_FLASH_ID_READ == FLASH_ID_CHECKSUM_CLR &&
        *(uint32_t *)UPPER_FLASH_ID_READ == FLASH_ID_CHECKSUM_CLR)
    {
        /* Program Checksum and initial ID's for both panels*/
        T_FLASH_ID low_flash_id = { 0 };
        T_FLASH_ID up_flash_id = { 0 };
 
        low_flash_id.flash_id = 1;
        low_flash_id.checksum_start = FLASH_ID_CHECKSUM_START;
        low_flash_id.checksum_end = FLASH_ID_CHECKSUM_END;

        up_flash_id.flash_id = 0;
        up_flash_id.checksum_start = FLASH_ID_CHECKSUM_START;
        up_flash_id.checksum_end = FLASH_ID_CHECKSUM_END;
        
        APP_NVMQuadWordWrite((void *)LOWER_FLASH_ID_BASE_ADDRESS, (uint32_t *)&low_flash_id);
        APP_NVMQuadWordWrite((void *)UPPER_FLASH_ID_BASE_ADDRESS, (uint32_t *)&up_flash_id);
    }
    /* If both the panels have proper checksum*/
    else if((lower_flash_id->checksum_start == FLASH_ID_CHECKSUM_START) &&
            (lower_flash_id->checksum_end == FLASH_ID_CHECKSUM_END) &&
            (upper_flash_id->checksum_start == FLASH_ID_CHECKSUM_START) &&
            (upper_flash_id->checksum_end == FLASH_ID_CHECKSUM_END))
    {
        if(upper_flash_id->flash_id > lower_flash_id->flash_id)
        {
            SwapFlashPanels();
        }
    }
    /* Fallback Cases when either of Panels checksum is corrupted*/
    else if((upper_flash_id->checksum_start == FLASH_ID_CHECKSUM_START) &&
            (upper_flash_id->checksum_end == FLASH_ID_CHECKSUM_END))
    {
        SwapFlashPanels();
    }
#endif // BOOTLOADER_LIVE_UPDATE_SWITCHER == 1
    /* Disable Global Interrupts and Jump to Application*/
    PLIB_INT_Disable(INT_ID_0);
    if (bootloaderData.StartAppFunc != NULL)
        bootloaderData.StartAppFunc();
    fptr();
}
#endif // BOOTLOADER_LIVE_UPDATE_STATE_SAVE != 1

/**
 * Static table used for the table_driven implementation.
 *****************************************************************************/
static const uint16_t crc_table[16] =
{
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
    0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef
};

/********************************************************************
* Function:     CalculateCrc()
*
* Precondition:
*
* Input:        Data pointer and data length
*
* Output:       CRC.
*
* Side Effects: None.
*
* Overview:     Calculates CRC for the given data and len
*
*
* Note:         None.
********************************************************************/
uint32_t APP_CalculateCrc(uint8_t *data, uint32_t len)
{
    uint32_t i;
    uint16_t crc = 0;
    
    while(len--)
    {
        i = (crc >> 12) ^ (*data >> 4);
        crc = crc_table[i & 0x0F] ^ (crc << 4);
        i = (crc >> 12) ^ (*data >> 0);
        crc = crc_table[i & 0x0F] ^ (crc << 4);
        data++;
    }

    return (crc & 0xFFFF);
}
/******************************************************************************
  Function:
    SYS_MODULE_OBJ Bootloader_Initialize(const SYS_MODULE_INDEX   moduleIndex,
                              const SYS_MODULE_INIT    * const moduleInit)

  Summary:
    Initializes primitive data structures for the general features
    of the primitive layer.

  Description:
    Initializes external and internal data structure for the general
    features of the primitive layer.

    This function must be called at system initialization.

  Remarks:
    None.
*/
void Bootloader_Initialize ( const BOOTLOADER_INIT *drvBootloaderInit )
{
    /* Place the App state machine in it's initial state. */
    bootloaderData.currentState = BOOTLOADER_CHECK_FOR_TRIGGER;
    bootloaderData.cmdBufferLength = 0;
    bootloaderData.streamHandle = DRV_HANDLE_INVALID;
    bootloaderData.datastreamStatus = DRV_CLIENT_STATUS_ERROR;
    bootloaderData.usrBufferEventComplete = false;

    bootloaderData.data = &data_buff;
    bootloaderData.type = drvBootloaderInit->drvType;
#ifdef BOOTLOADER_LEGACY
    BootloaderTriggerCheck = drvBootloaderInit->drvTrigger;
#endif
    
    bootloaderData.FlashEraseFunc = (BOOTLOADER_CALLBACK)NULL;
    bootloaderData.StartAppFunc = (BOOTLOADER_CALLBACK)NULL;
    bootloaderData.BlankCheckFunc = (BOOTLOADER_CALLBACK)NULL;
    bootloaderData.ProgramCompleteFunc = (BOOTLOADER_CALLBACK)NULL;
    bootloaderData.ForceBootloadFunc = (BOOTLOADER_CALLBACK)NULL;
    bootloaderData.softReset = (SYS_RESET_ReasonGet() & RESET_REASON_SOFTWARE) == RESET_REASON_SOFTWARE;
#if(BOOTLOADER_LIVE_UPDATE_STATE_SAVE != 1)
    SYS_RESET_ReasonClear(RESET_REASON_SOFTWARE);
    /* Delay to allow the internal pullups to stabilize */
    _CP0_SET_COUNT(0);
    while (_CP0_GET_COUNT() < SYS_CLK_FREQ / 5000);
#endif
}

void BOOTLOADER_FlashEraseRegister(BOOTLOADER_CALLBACK newFunc)
{
    bootloaderData.FlashEraseFunc = newFunc;
}

void BOOTLOADER_StartAppRegister(BOOTLOADER_CALLBACK newFunc)
{
    bootloaderData.StartAppFunc = newFunc;
}

void BOOTLOADER_BlankCheckRegister(BOOTLOADER_CALLBACK newFunc)
{
    bootloaderData.BlankCheckFunc = newFunc;
}

void BOOTLOADER_ProgramCompleteRegister(BOOTLOADER_CALLBACK newFunc)
{
    bootloaderData.ProgramCompleteFunc = newFunc;
}

void BOOTLOADER_ForceBootloadRegister(BOOTLOADER_CALLBACK newFunc)
{
    bootloaderData.ForceBootloadFunc = newFunc;
}

// *****************************************************************************
/* Function:
    void Bootloader_Tasks (SYS_MODULE_INDEX index);

  Summary:
    Maintains the Bootloader module state machine. It manages the Bootloader Module object list
    items and responds to Bootloader Module primitive events.

*/
void Bootloader_Tasks ()
{
    size_t BuffLen=0;
    uint16_t crc;
    unsigned int i;

    /* Check the application state*/
    switch ( bootloaderData.currentState )
    {
        case BOOTLOADER_CHECK_FOR_TRIGGER:
        {
            bool forceBootloadMode = false;
#if defined(BOOTLOADER_LEGACY)
            if(BootloaderTriggerCheck != NULL)
            {
                 bootloaderData.currentState = BootloaderTriggerCheck(); //Perform Trig Check
                 forceBootloadMode = bootloaderData.currentState != BOOTLOADER_ENTER_APPLICATION;
            }
            else
#else
            if (bootloaderData.ForceBootloadFunc != NULL)
            {
                forceBootloadMode = (1 == bootloaderData.ForceBootloadFunc());
            }
#endif
            if(forceBootloadMode)
            {
                /* Override any soft reset from the bootloader, so we will do
                 one when bootloader mode is done. */
                bootloaderData.softReset = false;
                bootloaderData.currentState = BOOTLOADER_OPEN_DATASTREAM;
            }
            else
            {
#if (BOOTLOADER_LIVE_UPDATE_STATE_SAVE == 1)
                /* Wait for trigger to enter program mode */
                bootloaderData.currentState = BOOTLOADER_CHECK_FOR_TRIGGER;
#else
                /* User reset address is not erased. Start program. */
                bootloaderData.currentState = BOOTLOADER_CLOSE_DATASTREAM;
#endif
            }
            break;
        }

        case BOOTLOADER_OPEN_DATASTREAM:
        {
            
            bootloaderData.streamHandle = DATASTREAM_Open(
                    DRV_IO_INTENT_READWRITE | DRV_IO_INTENT_NONBLOCKING);

            if (bootloaderData.streamHandle != DRV_HANDLE_INVALID )
            {
                
                if((bootloaderData.type != TYPE_USB_HOST) && (bootloaderData.type != TYPE_SD_CARD))
                {
                DATASTREAM_BufferEventHandlerSet(bootloaderData.streamHandle,
                        Bootloader_BufferEventHandler, APP_USR_CONTEXT);
                bootloaderData.currentState = BOOTLOADER_GET_COMMAND;
                }
                else
                {
                /* Host layer was opened successfully. Enable operation
                * and then wait for operation to be enabled  */
                bootloaderData.currentState = BOOTLOADER_WAIT_FOR_HOST_ENABLE;
                }
#if(BOOTLOADER_LIVE_UPDATE_STATE_SAVE == 1)
                /* Get the Opposite panel flash ID to update after programming*/
                opp_flash_id = Get_Flash_Id();
#endif
            }
            break;
        }

        case BOOTLOADER_PROCESS_COMMAND:
        {
            Bootloader_ProcessBuffer(&bootloaderData);
            break;
        }

        case BOOTLOADER_GET_COMMAND:
        {
            /* Get the datastream driver status */
            bootloaderData.datastreamStatus = DATASTREAM_ClientStatus( bootloaderData.streamHandle );
            /* Check if client is ready or not */
            if ( bootloaderData.datastreamStatus == DRV_CLIENT_STATUS_READY )
            {
                bootloaderData.bufferSize = 512;
                
                 DATASTREAM_Data_Read( &(bootloaderData.datastreamBufferHandle),
                        bootloaderData.data->buffers.buff1, bootloaderData.bufferSize);

                if ( bootloaderData.datastreamBufferHandle == DRV_HANDLE_INVALID )
                {
                    /* Set the app state to invalid */
                    bootloaderData.currentState = BOOTLOADER_ERROR;
                }
                else
                {
                    /* Set the App. state to wait for done */
                    bootloaderData.prevState    = BOOTLOADER_GET_COMMAND;
                    bootloaderData.currentState = BOOTLOADER_WAIT_FOR_DONE;
                }
            }
            break;
        }

        case BOOTLOADER_WAIT_FOR_DONE:
        {
            /* check if the datastream buffer event is complete or not */
            if (bootloaderData.usrBufferEventComplete)
            {
                bootloaderData.usrBufferEventComplete = false;
                
                /* Get the next App. State */
                switch (bootloaderData.prevState)
                {
                    case BOOTLOADER_GET_COMMAND:
                        bootloaderData.currentState = BOOTLOADER_PROCESS_COMMAND;
                        break;
                    case BOOTLOADER_SEND_RESPONSE:
                    default:
                        bootloaderData.currentState = BOOTLOADER_GET_COMMAND;
                        break;
                }
            }
            break;
        }

    case BOOTLOADER_WAIT_FOR_NVM:
       if (PLIB_NVM_FlashWriteCycleHasCompleted(NVM_ID_0))
       {
         
         bootloaderData.currentState = BOOTLOADER_SEND_RESPONSE;
         PLIB_NVM_MemoryModifyInhibit(NVM_ID_0);
       }
       break;

    case BOOTLOADER_SEND_RESPONSE:
        {
            if(bootloaderData.bufferSize)
            {
                /* Calculate the CRC of the response*/
                crc = APP_CalculateCrc(bootloaderData.data->buffers.buff1, bootloaderData.bufferSize);
                bootloaderData.data->buffers.buff1[bootloaderData.bufferSize++] = (uint8_t)crc;
                bootloaderData.data->buffers.buff1[bootloaderData.bufferSize++] = (crc>>8);

                bootloaderData.data->buffers.buff2[BuffLen++] = SOH;

                for (i = 0; i < bootloaderData.bufferSize; i++)
                {
                    if ((bootloaderData.data->buffers.buff1[i] == EOT) || (bootloaderData.data->buffers.buff1[i] == SOH)
                        || (bootloaderData.data->buffers.buff1[i] == DLE))
                    {
                        bootloaderData.data->buffers.buff2[BuffLen++] = DLE;
                    }
                    bootloaderData.data->buffers.buff2[BuffLen++] = bootloaderData.data->buffers.buff1[i];
                }

                bootloaderData.data->buffers.buff2[BuffLen++] = EOT;
                bootloaderData.bufferSize = 0;

                DATASTREAM_Data_Write( &(bootloaderData.datastreamBufferHandle),
                        bootloaderData.data->buffers.buff2, BuffLen);

                if ( bootloaderData.datastreamBufferHandle == DRV_HANDLE_INVALID )
                {
                    bootloaderData.currentState = BOOTLOADER_ERROR;
                }
                else
                {
                    bootloaderData.prevState = BOOTLOADER_SEND_RESPONSE;
                    bootloaderData.currentState = BOOTLOADER_WAIT_FOR_DONE;
                }
            }
            break;
        }

        case BOOTLOADER_WAIT_FOR_HOST_ENABLE:

            /* Check if the host operation has been enabled */
            if(DATASTREAM_ClientStatus(bootloaderData.streamHandle) == DRV_CLIENT_STATUS_READY)
            {
                /* This means host operation is enabled. We can
                 * move on to the next state */
                 DATASTREAM_BufferEventHandlerSet((DRV_HANDLE)bootloaderData.streamHandle, NULL, 0);
                 bootloaderData.currentState = BOOTLOADER_WAIT_FOR_DEVICE_ATTACH;
            }
            break;

        case BOOTLOADER_WAIT_FOR_DEVICE_ATTACH:
            /* Wait for device attach. The state machine will move
             * to the next state when the attach event
             * is received.  */
            break;

#if(SYS_FS_MAX_FILES > 0)
        case BOOTLOADER_DEVICE_CONNECTED:

            /* Turn on LED to indicate connection. */
            //BSP_LEDOn(BSP_LED_2);
            /* Device was connected. We can try opening the file */
            bootloaderData.currentState = BOOTLOADER_OPEN_FILE;
            break;

        case BOOTLOADER_OPEN_FILE:

            /* Try opening the file for reading */
            bootloaderData.fileHandle = SYS_FS_FileOpen("/mnt/myDrive1/" BOOTLOADER_IMAGE_FILE_NAME, (SYS_FS_FILE_OPEN_READ));
            if(bootloaderData.fileHandle == SYS_FS_HANDLE_INVALID)
            {
                /* Could not open the file. Error out*/
                bootloaderData.currentState = BOOTLOADER_ERROR;
            }
            else
            {
                /* File opened successfully. Read file */
                APP_FlashErase();
                while (!PLIB_NVM_FlashWriteCycleHasCompleted(NVM_ID_0));
                APP_NVMClearError();
                PLIB_NVM_MemoryModifyInhibit(NVM_ID_0);
                bootloaderData.currentState = BOOTLOADER_READ_FILE;

            }
            break;

        case BOOTLOADER_READ_FILE:

            /* Try reading the file */
            BuffLen = DATASTREAM_Data_Read(NULL, (void*)fileBuffer, 512);
            if (BuffLen <= 0)
            {
                SYS_FS_FileClose(bootloaderData.fileHandle);
                bootloaderData.currentState = BOOTLOADER_CLOSE_DATASTREAM;
                SYS_RESET_SoftwareReset();
            }
            else
            {
                memcpy(&bootloaderData.data->buffer[bootloaderData.cmdBufferLength], fileBuffer, BuffLen);
                
                /* Process the buffer that we read. */
                bootloaderData.bufferSize = BuffLen;
                Bootloader_ProcessBuffer(&bootloaderData);
            }

            break;
#endif
        case BOOTLOADER_CLOSE_DATASTREAM:
            DATASTREAM_Close();
#if(BOOTLOADER_LIVE_UPDATE_STATE_SAVE == 1)
            Update_Flash_Id();
#endif
            if (bootloaderData.ProgramCompleteFunc != NULL)
                bootloaderData.ProgramCompleteFunc();
        case BOOTLOADER_ENTER_APPLICATION:
#if(BOOTLOADER_LIVE_UPDATE_STATE_SAVE != 1)
            Enter_Application();
#endif
            break;

        case BOOTLOADER_ERROR:
            /* The application comes here when the demo
             * has failed. Switch on the LED 9.*/
            //BSP_LEDOn(BSP_LED_3);
            //BSP_LEDOn(BSP_LED_2);
            //BSP_LEDOn(BSP_LED_1);
            break;

        default:
            bootloaderData.currentState = BOOTLOADER_ERROR;
            break;
    }

#if((DRV_USBFS_HOST_SUPPORT == false) && \
    (DRV_USBHS_HOST_SUPPORT == false) && \
    !defined(DRV_SDCARD_INSTANCES_NUMBER) && \
    !defined(DRV_SDHC_INSTANCES_NUMBER))
    /* Maintain Device Drivers */
    DATASTREAM_Tasks();
    #endif

}

#if defined(BOOTLOADER_LEGACY)
BOOTLOADER_STATES BootloaderFlashTriggerCheck(void)
{
#if defined(BOOTLOADER_FLASH_TRIGGER_ADDRESS)
     //Check that the last program flash location is all 0's
     if (*(unsigned int *)BOOTLOADER_FLASH_TRIGGER_ADDRESS != 0xFFFFFFFF)
     {
         return(BOOTLOADER_ENTER_APPLICATION);
          
     }
     else
#endif
     {
         return(BOOTLOADER_OPEN_DATASTREAM);
     }
}

BOOTLOADER_STATES BootloaderButtonTriggerCheck(void)
{
#if defined(BTL_SWITCH)
     if (BSP_SWITCH_STATE_PRESSED == BSP_SwitchStateGet(BTL_SWITCH))
     {
          return(BOOTLOADER_OPEN_DATASTREAM);
     }
     else
#endif    
     {
          return(BOOTLOADER_CHECK_FOR_TRIGGER);
     }
}

bool __attribute__((weak)) BootloaderProgramExistsCheck(void)
{
    return (0xFFFFFFFF == *(unsigned int *)APP_RESET_ADDRESS);
}

#endif
