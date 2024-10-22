/*******************************************************************************
  System Definitions

  File Name:
    definitions.h

  Summary:
    project system definitions.

  Description:
    This file contains the system-wide prototypes and definitions for a project.

 *******************************************************************************/

//DOM-IGNORE-BEGIN
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
//DOM-IGNORE-END

#ifndef DEFINITIONS_H
#define DEFINITIONS_H

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "crypto/crypto.h"
#include "peripheral/ocmp/plib_ocmp8.h"
#include "peripheral/ocmp/plib_ocmp6.h"
#include "peripheral/ocmp/plib_ocmp7.h"
#include "peripheral/spi/spi_master/plib_spi4_master.h"
#include "usb/usb_chapter_9.h"
#include "usb/usb_device.h"
#include "peripheral/ocmp/plib_ocmp1.h"
#include "peripheral/ocmp/plib_ocmp4.h"
#include "peripheral/rcon/plib_rcon.h"
#include "driver/sdspi/drv_sdspi.h"
#include "driver/i2c/drv_i2c.h"
#include "peripheral/ocmp/plib_ocmp3.h"
#include "peripheral/nvm/plib_nvm.h"
#include "system/time/sys_time.h"
#include "usb/usb_device_cdc.h"
#include "usb/usb_cdc.h"
#include "peripheral/coretimer/plib_coretimer.h"
#include "peripheral/tmr/plib_tmr6.h"
#include "peripheral/adchs/plib_adchs.h"
#include "peripheral/tmr/plib_tmr4.h"
#include "peripheral/tmr/plib_tmr2.h"
#include "peripheral/tmr/plib_tmr3.h"
#include "driver/spi/drv_spi.h"
#include "system/int/sys_int.h"
#include "system/ports/sys_ports.h"
#include "system/cache/sys_cache.h"
#include "system/dma/sys_dma.h"
#include "osal/osal.h"
#include "system/debug/sys_debug.h"
#include "driver/winc/include/wdrv_winc_api.h"
#include "peripheral/clk/plib_clk.h"
#include "peripheral/gpio/plib_gpio.h"
#include "peripheral/cache/plib_cache.h"
#include "peripheral/evic/plib_evic.h"
#include "peripheral/dmac/plib_dmac.h"
#include "driver/usb/usbhs/drv_usbhs.h"
#include "peripheral/power/plib_power.h"
#include "wolfssl/wolfcrypt/port/pic32/crypt_wolfcryptcb.h"
#include "system/fs/sys_fs.h"
#include "system/fs/sys_fs_media_manager.h"
#include "system/fs/sys_fs_fat_interface.h"
#include "system/fs/fat_fs/file_system/ff.h"
#include "system/fs/fat_fs/file_system/ffconf.h"
#include "system/fs/fat_fs/hardware_access/diskio.h"
#include "peripheral/i2c/master/plib_i2c5_master.h"
#include "FreeRTOS.h"
#include "task.h"
#include "app_freertos.h"



// DOM-IGNORE-BEGIN
#ifdef __cplusplus  // Provide C++ Compatibility

extern "C" {

#endif
// DOM-IGNORE-END

/* Device Information */
#define DEVICE_NAME			 "PIC32MZ2048EFM144"
#define DEVICE_ARCH			 "MIPS"
#define DEVICE_FAMILY		 "PIC32MZEF"
#define DEVICE_SERIES		 "PIC32MZ"

/* CPU clock frequency */
#define CPU_CLOCK_FREQUENCY 200000000

// *****************************************************************************
// *****************************************************************************
// Section: System Functions
// *****************************************************************************
// *****************************************************************************

// *****************************************************************************
/* System Initialization Function

  Function:
    void SYS_Initialize( void *data )

  Summary:
    Function that initializes all modules in the system.

  Description:
    This function initializes all modules in the system, including any drivers,
    services, middleware, and applications.

  Precondition:
    None.

  Parameters:
    data            - Pointer to the data structure containing any data
                      necessary to initialize the module. This pointer may
                      be null if no data is required and default initialization
                      is to be used.

  Returns:
    None.

  Example:
    <code>
    SYS_Initialize ( NULL );

    while ( true )
    {
        SYS_Tasks ( );
    }
    </code>

  Remarks:
    This function will only be called once, after system reset.
*/

void SYS_Initialize( void *data );

// *****************************************************************************
/* System Tasks Function

Function:
    void SYS_Tasks ( void );

Summary:
    Function that performs all polled system tasks.

Description:
    This function performs all polled system tasks by calling the state machine
    "tasks" functions for all polled modules in the system, including drivers,
    services, middleware and applications.

Precondition:
    The SYS_Initialize function must have been called and completed.

Parameters:
    None.

Returns:
    None.

Example:
    <code>
    SYS_Initialize ( NULL );

    while ( true )
    {
        SYS_Tasks ( );
    }
    </code>

Remarks:
    If the module is interrupt driven, the system will call this routine from
    an interrupt context.
*/

void SYS_Tasks ( void );

// *****************************************************************************
// *****************************************************************************
// Section: Type Definitions
// *****************************************************************************
// *****************************************************************************

// *****************************************************************************
/* System Objects

Summary:
    Structure holding the system's object handles

Description:
    This structure contains the object handles for all objects in the
    MPLAB Harmony project's system configuration.

Remarks:
    These handles are returned from the "Initialize" functions for each module
    and must be passed into the "Tasks" function for each module.
*/

typedef struct
{
    /* SDSPI0 Driver Object */
    SYS_MODULE_OBJ drvSDSPI0;

    /* I2C0 Driver Object */
    SYS_MODULE_OBJ drvI2C0;

    SYS_MODULE_OBJ  usbDevObject0;

    SYS_MODULE_OBJ  sysTime;
    SYS_MODULE_OBJ  drvWifiWinc;
    /* SPI0 Driver Object */
    SYS_MODULE_OBJ drvSPI0;

    SYS_MODULE_OBJ  drvUSBHSObject;


} SYSTEM_OBJECTS;

// *****************************************************************************
// *****************************************************************************
// Section: extern declarations
// *****************************************************************************
// *****************************************************************************

extern const USB_DEVICE_INIT usbDevInitData; 



extern SYSTEM_OBJECTS sysObj;

//DOM-IGNORE-BEGIN
#ifdef __cplusplus
}
#endif
//DOM-IGNORE-END

#endif /* DEFINITIONS_H */
/*******************************************************************************
 End of File
*/

