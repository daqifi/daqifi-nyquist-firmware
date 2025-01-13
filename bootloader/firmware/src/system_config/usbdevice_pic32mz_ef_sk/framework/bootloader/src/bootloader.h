/*******************************************************************************
 Module for Microchip Bootloader Library

  Company:
    Microchip Technology Inc.

  File Name:
    bootloader.h

  Summary:
    The header file joins all header files used in the Bootloader Library
    and contains compile options and defaults.

  Description:
    This header file includes all the header files required to use the
    Microchip Bootloader Library. Library features and options defined
    in the Bootloader Library configurations will be included in each build.
*******************************************************************************/

// DOM-IGNORE-BEGIN
/*******************************************************************************
Copyright (c) 2015 released Microchip Technology Inc.  All rights reserved.

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

#ifndef _BOOTLOADER_H
#define _BOOTLOADER_H

#ifdef __cplusplus
    extern "C" {
#endif

////////////////////////////// INCLUDES //////////////////////////////
#include "system_config.h"
#include <stdbool.h>
#include "datastream.h"
#include "driver/driver_common.h"
#include "system/fs/sys_fs.h"

#define SOH 01
#define EOT 04
#define DLE 16

// *****************************************************************************
/* PC Host Commands

  Summary:
    PC Host Commands enumeration.

  Description:
    This enumeration defines the valid PC Host Commands. These commands
    determine the behavior of the transmit/response at various times.
*/
typedef enum
{
    READ_BOOT_INFO = 1,
    ERASE_FLASH,
    PROGRAM_FLASH,
    READ_CRC,
    JMP_TO_APP

}T_COMMANDS;
// DOM-IGNORE-END

#define MAJOR_VERSION 4    /* Bootloader Major Version Shown From a Read Version on PC */
#define MINOR_VERSION 1    /* Bootloader Minor Version Shown From a Read Version on PC */

static const uint8_t BootInfo[2] =
{
    MINOR_VERSION,
    MAJOR_VERSION
};

#define BOOTLOADER_BUFFER_SIZE 512

typedef union
{
    uint8_t buffer[BOOTLOADER_BUFFER_SIZE + BOOTLOADER_BUFFER_SIZE];
    struct
    {
        uint8_t buff1[BOOTLOADER_BUFFER_SIZE];
        uint8_t buff2[BOOTLOADER_BUFFER_SIZE];
    }buffers;

} BOOTLOADER_BUFFER;

// DOM-IGNORE-BEGIN
// *****************************************************************************
/* Application states

  Summary:
    BOOTLOADER states enumeration.

  Description:
    This enumeration defines the valid application states.  These states
    determine the behavior of the application at various times.
*/

typedef enum
{
   /* Application's state machine's initial state. */
   BOOTLOADER_STATE_INIT=0,

   /* Check to see if we need to force the bootloader */
   BOOTLOADER_CHECK_FOR_TRIGGER,

    /* If we need to program, then open the datastream. */
    BOOTLOADER_OPEN_DATASTREAM,

    /* The application gets a command from the host application. */
    BOOTLOADER_GET_COMMAND,

    /* The application processes the command from the host application. */
    BOOTLOADER_PROCESS_COMMAND,

    /* The application waits for the NVM operation to complete. */
    BOOTLOADER_WAIT_FOR_NVM,

    /* The application sends data back to the user. */
    BOOTLOADER_SEND_RESPONSE,

    /* The application waits in this state for the driver to finish
       sending/receiving the message. */
    BOOTLOADER_WAIT_FOR_DONE,
           
    BOOTLOADER_CLOSE_DATASTREAM,

    /* The application enters the user application. */
    BOOTLOADER_ENTER_APPLICATION,
           
    BOOTLOADER_RESET,

    BOOTLOADER_OPEN_FILE,

    BOOTLOADER_READ_FILE,

    BOOTLOADER_WAIT_FOR_DEVICE_ATTACH,

    BOOTLOADER_WAIT_FOR_HOST_ENABLE,

    BOOTLOADER_DEVICE_CONNECTED,

    BOOTLOADER_UNMOUNT_DISK,

    BOOTLOADER_SWITCH_APPLICATION,

    /* This state indicates an error has occurred. */
    BOOTLOADER_ERROR,

} BOOTLOADER_STATES;
// DOM-IGNORE-END

// *****************************************************************************
/* Bootloader Type

  Summary:
    A structure used to initialize the bootloader defining the different bootloader.

  Description:
    This structure holds the bootloader types.

  Remarks:
    None.
 */
typedef enum
{
    TYPE_I2C,
    TYPE_USART,
    TYPE_USB_HOST,
    TYPE_USB_DEVICE,
    TYPE_ETHERNET_UDP_PULL,
    TYPE_SD_CARD
} BOOTLOADER_TYPE;

// *****************************************************************************
/* Bootloader Initialization Type

  Summary:
    A structure used to initialize the bootloader defining the different bootloader.

  Description:
    This structure holds the bootloader types.

  Remarks:
    None.
 */
typedef struct
{
    BOOTLOADER_TYPE drvType;
    BOOTLOADER_STATES (*drvTrigger)(void);
} BOOTLOADER_INIT;

// *****************************************************************************
/* Bootloader General Callback Function Pointer

  Summary:
    Pointer to a Bootloader callback function data type .

  Description:
    This data type defines a pointer to a Bootloader library callback function.

  Remarks:
    This is used for callback on the following events:
     * ERASE - Erase any additional areas (i.e. SQI, EBI, SPI memories)
     * PROGRAM_COMPLETE - No more data coming
     * START_APP - Anything that needs to be done prior to starting application
     * BLANK_CHECK - Check external devices for erasure
     * FORCE_BOOTLOAD - Any checks the bootloader wants to do before launching application
  
 Return values:
     * 1 = Operation complete - Bootloader can move to next state
     * 0 = Operation incomplete - Please check again
     * -1 = Operation error - Bootloader needs to abort operations
*/

typedef int ( *BOOTLOADER_CALLBACK ) (void);

// *****************************************************************************
/* Bootloader Data to Program Callback Function Pointer

  Summary:
    Pointer to a Bootloader callback function data type .

  Description:
    This data type defines a pointer to a Bootloader data callback function.
    This would be used to program data for an area not recognized by the
    bootloader as being in progam Flash (i.e. SPI Flash).

  Remarks:
    This is used for callback on the following events:
     * DATA_PROGRAM - Data to be programmed
     * DATA_VERIFY  - Compute the CRC for the given area
  
 Return values:
     * 1 = Operation complete - Bootloader can move to next state
     * 0 = Operation incomplete - Please check again
     * -1 = Operation error - Bootloader needs to abort operations
*/

typedef int ( *BOOTLOADER_DATA_CALLBACK ) (uint32_t address, uint32_t *data, uint32_t size);

// DOM-IGNORE-BEGIN
// *****************************************************************************
/* Application Data

  Summary:
    Holds application data.

  Description:
    This structure holds the application's data.

  Remarks:
    Application strings and buffers are be defined outside this structure.
 */

typedef struct
{
    /* Application current state */
    BOOTLOADER_STATES currentState;

    /* Application previous state */
    BOOTLOADER_STATES prevState;

    /* Datastream buffer size */
    uint16_t bufferSize;

    /* Command buffer length */
    int cmdBufferLength;

    /* Stream handle */
    DRV_HANDLE streamHandle;

    /* Handle returned by USART for buffer submitted */
    DRV_HANDLE  datastreamBufferHandle;

        /* Datastream status */
    DRV_CLIENT_STATUS datastreamStatus;
    
    /* Flag to indicate the user message is been processed */
    bool usrBufferEventComplete;

    /* The application's current state */
      /* USB Host Layer Handle */
    uintptr_t hostHandle;

    /* SYS_FS File handle for 1st file */
    SYS_FS_HANDLE fileHandle;
    
    /* Application data buffer */
    BOOTLOADER_BUFFER *data;
 
    /* Device configured */
    bool deviceConfigured;

    /* HID data received flag*/
    bool DataTransferred;

    /* Type of Bootloader Datastream */
    uint8_t type;
    
    /* Callback for Flash Erase function */
    BOOTLOADER_CALLBACK FlashEraseFunc;
    
    /* Callback for Start App function */
    BOOTLOADER_CALLBACK StartAppFunc;
    
    /* Callback for Blank Check function */
    BOOTLOADER_CALLBACK BlankCheckFunc;
    
    /* Callback for Programming Complete function */
    BOOTLOADER_CALLBACK ProgramCompleteFunc;
    
    /* Callback for Force Bootloader function */
    BOOTLOADER_CALLBACK ForceBootloadFunc;
    
    /* Determine if we are in the bootloader as a result of a software reset. */
    bool softReset;
    
} BOOTLOADER_DATA;
// DOM-IGNORE-END
// *****************************************************************************
/*  Bootloader Client Status

  Summary:
    Enumerated data type that identifies the  Bootloader module client status.

  Description:
    This enumeration defines the possible status of the Bootloader module client.
    It is returned by the () function.

  Remarks:
    None.
*/

typedef enum
{
     /* Client is closed or the specified handle is invalid */
    BOOTLOADER_CLIENT_STATUS_CLOSED
            /*DOM-IGNORE-BEGIN*/ = 0 /*DOM-IGNORE-END*/,

    /* Client is ready */
    BOOTLOADER_CLIENT_STATUS_READY
            /*DOM-IGNORE-BEGIN*/ = 1 /*DOM-IGNORE-END*/,
            
    BOOTLOADER_CLIENT_STATUS_WRITEFAILURE,

    BOOTLOADER_CLIENT_STATUS_WRITESUCCESS

} BOOTLOADER_CLIENT_STATUS;

// *****************************************************************************
/*
  Function:
    void Bootloader_Initialize (const SYS_MODULE_INDEX   moduleIndex,
                                const SYS_MODULE_INIT    * const moduleInit);

  Summary:
    Initializes the Bootloader Library.

  Description:
    This function is used to initialize the Bootloader Library.

  Precondition:
    None.

  Parameters:
    None.

  Returns:
    If successful, returns a valid handle to a device layer object.
    Otherwise, it returns SYS_MODULE_OBJ_INVALID.

  Remarks:
    This routine must be called before other Bootloader Library functions.

*/
// *****************************************************************************
void  Bootloader_Initialize( const BOOTLOADER_INIT *drvBootloaderInit);

// *****************************************************************************
/* Function:
    void Bootloader_Tasks (SYS_MODULE_INDEX index);

  Summary:
    Maintains the Bootloader module state machine. It manages the Bootloader 
    module object list items and responds to Bootloader module primitive events.

  Description:
    This function maintains the Bootloader module state machine and manages the 
    Bootloader Module object list items and responds to Bootloader Module events.
    This function should be called from the SYS_Tasks function.

  Precondition:
    None.

  Parameters:
    index      - Object index for the specified module instance.

  Returns:
    None.

  Example:
    <code>

    while (true)
    {
        Bootloader_Tasks ();

        // Do other tasks
    }
    </code>

  Remarks:
    This function is normally not called directly by an application.
*/
void Bootloader_Tasks ();

// DOM-IGNORE-BEGIN
uint32_t APP_CalculateCrc(uint8_t *data, uint32_t len);

// *****************************************************************************
/* Function:
    void Bootloader_ProcessBuffer( BOOTLOADER_DATA *handle )

  Summary: 
    Processes the input datastream received from the PC/media. 
    
  Description:
    This function processes the input datastream received from the PC/media.

  Precondition:
    None.

  Parameters: handle to the bootloader data

  Returns:
    None.

  Remarks:
    This routine is normally not called directly by an application.
*/
void Bootloader_ProcessBuffer( BOOTLOADER_DATA *handle );

// *****************************************************************************
/* Function:
    void Bootloader_BufferEventHandler(BOOTLOADER_DATA *handle, DATASTREAM_BUFFER_EVENT buffEvent,
                            DATASTREAM_BUFFER_HANDLE hBufferEvent,
                            uintptr_t context )

  Summary:
    A handler for a buffer event, which manages the Bootloader Module object list
    items and responds to Bootloader module events.

  Description:
    This function specifies the buffer event handler and manages the Bootloader 
    module object list items and responds to Bootloader Module events.  
    This function should be called from the SYS_Tasks function.

  Precondition:
     None.

  Parameters:
    BOOTLOADER_DATA *handle - handle to the bootloader data
   *DATASTREAM_BUFFER_EVENT buffEvent - buffer datastream event
    DATASTREAM_BUFFER_HANDLE hBufferEvent - handle to the datastream buffer
    uintptr_t context - context of the buffer

   Returns:
    None.

  Example:
    <code>

    while (true)
    {
        Bootloader_Tasks ();

        // Do other tasks
    }
    </code>

  Remarks:
    This routine is normally not called directly by an application.
*/

// *****************************************************************************
/* Function:
    NVM_BufferEventHandler(BOOTLOADER_DATA *handle, DATASTREAM_BUFFER_EVENT buffEvent,
                            DATASTREAM_BUFFER_HANDLE hBufferEvent,
                            uintptr_t context );

  Summary: 
    A similar function to Bootloader_BufferEventHandler used for NVM memory.

  Description:
    This function is similar to the Bootloader_BufferEventHandler function, which is 
    used for NVM memory.

  Precondition:
    None.

  Parameters: 
    A handle to the bootloader data.

  Returns:
    None.

  Remarks:
    This routine is normally not called directly by an application.
*/
void NVM_BufferEventHandler(BOOTLOADER_DATA *handle, DATASTREAM_BUFFER_EVENT buffEvent,
                            DATASTREAM_BUFFER_HANDLE hBufferEvent,
                            uintptr_t context );

// *****************************************************************************
/* Function:
    void Bootloader_ProcessBuffer( BOOTLOADER_DATA *handle )

  Summary: 
    Overloaded Function Pointer functions used to check for a trigger.

  Description:
    These are the overloaded Function Pointer functions used to check for a trigger.

  Precondition:
     None.

  Parameters: 
    A handle to the bootloader data.

  Returns:
    None.

  Remarks:
    This routine is normally not called directly by an application.
*/

/* */
#if defined(BOOTLOADER_LEGACY)
BOOTLOADER_STATES BootloaderButtonTriggerCheck(void);
BOOTLOADER_STATES BootloaderFlashTriggerCheck(void);
bool BootloaderProgramExistsCheck(void);
#endif

void BOOTLOADER_FlashEraseRegister(BOOTLOADER_CALLBACK newFunc);
void BOOTLOADER_StartAppRegister(BOOTLOADER_CALLBACK newFunc);
void BOOTLOADER_BlankCheckRegister(BOOTLOADER_CALLBACK newFunc);
void BOOTLOADER_ProgramCompleteRegister(BOOTLOADER_CALLBACK newFunc);
void BOOTLOADER_ForceBootloadRegister(BOOTLOADER_CALLBACK newFunc);

#ifdef  __cplusplus
}
#endif

#endif
// DOM-IGNORE-END
