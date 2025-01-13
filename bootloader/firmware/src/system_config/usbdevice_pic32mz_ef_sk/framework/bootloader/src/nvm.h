/*******************************************************************************
  System Definitions

  File Name:
    nvm.h

  Summary:
 Flash Memory function definitions.

  Description:
    This file contains the definitions needed for PLIB usage of the Flash 
    Controller on MX and MZ parts.
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

#ifndef _NVM_H
#define _NVM_H

#ifdef __cplusplus
    extern "C" {
#endif

#include <stdint.h>

#define DATA_RECORD           0
#define END_OF_FILE_RECORD    1
#define EXT_SEG_ADRS_RECORD   2
#define EXT_LIN_ADRS_RECORD   4
#define START_LIN_ADRS_RECORD 5

typedef enum
{
    // indicates that the CRC value between the calculated value and the
    // value received from data stream did not match
    HEX_REC_CRC_ERROR   = -10,

    // programming error
    HEX_REC_PGM_ERROR   = -5,

    // An unspecified hex record tyype is received
    HEX_REC_UNKNOW_TYPE  = -1,

    // the record type is a valid hex record
    HEX_REC_NORMAL    = 0,


} HEX_RECORD_STATUS;

char APP_ProgramHexRecord(uint8_t* HexRecord, int32_t totalLen);
void APP_FlashErase(void);
void APP_NVMClearError(void);

/* NVM Flash Memory programming
 * key 1*/

#define NVM_PROGRAM_UNLOCK_KEY1     0xAA996655

/* NVM Driver Flash Memory programming
 * key 1*/

#define NVM_PROGRAM_UNLOCK_KEY2     0x556699AA

#ifdef	__cplusplus
}
#endif

#endif
