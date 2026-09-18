/*******************************************************************************
  MPLAB Harmony Bootloader Source File
  
  Company:
    Microchip Technology Inc.
  
  File Name:
    nvm.c

  Summary:
    This file contains the source code for handling NVM controllers.

  Description:
    This file contains the source code for the NVM handling functions for PIC32MX
	and MZ devices.
 *******************************************************************************/

// DOM-IGNORE-BEGIN
/*******************************************************************************
Copyright (c) 2013-2014 released Microchip Technology Inc.  All rights reserved.

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

#include "nvm.h"
#include "system_config.h"
#include "peripheral/nvm/plib_nvm.h"
#include "system/devcon/sys_devcon.h"
#include "peripheral/int/plib_int.h"
#include <sys/kmem.h>

typedef struct
{
    uint8_t RecDataLen;
    uint32_t Address;
    uint8_t RecType;
    uint8_t* Data;
    uint8_t CheckSum;
    uint32_t ExtSegAddress;
    uint32_t ExtLinAddress;
}T_HEX_RECORD;

void APP_NVMOperation(uint32_t nvmop)
{
    
    uint32_t processorStatus;
    
    processorStatus = PLIB_INT_GetStateAndDisable( INT_ID_0 );
    
    // Disable flash write/erase operations
    PLIB_NVM_MemoryModifyInhibit(NVM_ID_0);

    PLIB_NVM_MemoryOperationSelect(NVM_ID_0, nvmop);

    // Allow memory modifications
    PLIB_NVM_MemoryModifyEnable(NVM_ID_0);

    /* Unlock the Flash */
    PLIB_NVM_FlashWriteKeySequence(NVM_ID_0, 0);
    PLIB_NVM_FlashWriteKeySequence(NVM_ID_0, NVM_PROGRAM_UNLOCK_KEY1);
    PLIB_NVM_FlashWriteKeySequence(NVM_ID_0, NVM_PROGRAM_UNLOCK_KEY2);

    PLIB_NVM_FlashWriteStart(NVM_ID_0);
    
    while (!PLIB_NVM_FlashWriteCycleHasCompleted(NVM_ID_0));
    
    PLIB_INT_SetState(INT_ID_0, processorStatus);

}

bool _BTL_IsPageBlank(unsigned int address)
{
    unsigned int *addr = (unsigned int *)(address & (~NVM_PAGE_SIZE + 1));
    unsigned int *endaddr = (unsigned int *)((unsigned int)addr + NVM_PAGE_SIZE);
    bool result = true;
    
    while (result && (addr < endaddr))
    {
        result = 0xFFFFFFFF == *addr;
        addr++;
    }
    
    return result;
}

void APP_FlashErase( void )
{
#if (USE_PAGE_ERASE)
    unsigned int flashAddr = APP_FLASH_BASE_ADDRESS;

    while (flashAddr < APP_FLASH_END_ADDRESS)
    {
        if (!_BTL_IsPageBlank(flashAddr))
        {
            PLIB_NVM_FlashAddressToModify(NVM_ID_0, KVA_TO_PA(flashAddr));
            APP_NVMOperation(PAGE_ERASE_OPERATION);
        }
        flashAddr += NVM_PAGE_SIZE;
    }
#else
    #if defined(BOOTLOADER_LIVE_UPDATE_STATE_SAVE)
    APP_NVMOperation(UPPER_FLASH_REGION_ERASE_OPERATION);
    #else
    /* #909: erase ONLY the lower program-flash panel, never the whole PFM.
     *
     * This used to be FLASH_ERASE_OPERATION (NVMOP = 0b0111, "erase all of
     * program Flash memory"). That wiped 0x1D000000-0x1D1FFFFF, and the
     * application keeps its four 16 KB NVM settings pages -- TopLevel, WiFi
     * credentials, factory ADC calibration and user ADC calibration -- at
     * 0x9D1E0000-0x9D1EFFFF (firmware/src/services/daqifi_settings.h:
     * RESERVED_SETTINGS_ADDR = KSEG0_PROGRAM_MEM_BASE_END - 128 KB). So every
     * in-app customer update destroyed the device's settings and calibration;
     * that is the root cause of #908 and of the "my WiFi credentials /
     * precision reset after updating" reports.
     *
     * NVMOP = 0b0101 erases only the LOWER mapped region of program Flash,
     * 0x1D000000-0x1D0FFFFF on this 2 MB part, leaving the upper region --
     * and therefore the settings pages -- untouched.
     * [X] FRM DS60001193B "Flash Memory with Support for Live Update",
     *     Register 52-1 (NVMCON) bits 3-0:
     *       0111 = "erase all of program Flash memory"
     *       0110 = "erases only the upper mapped region of program Flash"
     *       0101 = "erases only the lower mapped region of program Flash"
     *     and Figure 52-3, which gives the 2 MB geometry: lower mapped region
     *     0x1D000000-0x1D0FFFFF, upper mapped region 0x1D100000-0x1D1FFFFF.
     *
     * Three preconditions, all satisfied here:
     *
     *  1. Execution location. DS60001193B section 52.11.2: "When erasing the
     *     entire PFM area, code must be executing from BFM. When erasing a
     *     single upper or lower PFM bank, code must either be executing from
     *     BFM or from the PFM bank that is not being erased." This bootloader
     *     links entirely into boot flash (btl_mz.ld puts kseg1_boot_mem at
     *     0xBFC00000 and its kseg0_program_mem at 0x9FC01000; the shipped
     *     usb_bootloader.X.production.hex occupies only 0x1FC00000-0x1FC0FFF3
     *     and nothing in PFM), so it runs from BFM and satisfies the stricter
     *     whole-PFM rule as well. The panel erase cannot erase the bootloader.
     *
     *  2. Write protection. The same section: a region erase "will only
     *     succeed if no pages are write-protected in the bank being erased",
     *     which is strictly WEAKER than the whole-PFM erase's "PFM write
     *     protection must be completely disabled". NVMPWP is 0 out of reset
     *     and neither image ever writes it, so nothing changes here.
     *
     *  3. The application must fit in the lower panel. It does, with room to
     *     spare -- but do not trust the figure in this comment, trust the
     *     gate. For scale only: a STANDALONE build of main measured 2026-09-08
     *     reported Total kseg0_program_mem used = 0xB8BBC (756,668 B), 72% of
     *     the 1 MB panel. A bootloader-linked build differs slightly (it
     *     starts 0x480 higher), and any figure written down here goes stale
     *     the moment someone adds a feature. So this is ENFORCED rather than
     *     asserted: tools/release/cut_release.sh measures the ACTUAL release
     *     build and fails the release if its .map's kseg0_program_mem usage
     *     reaches 0x100000, or if any record in the release hex lands at or
     *     above 0x1D100000.
     *
     * Panel swapping does not change any of this. The erase targets the
     * MAPPED region, i.e. the address range above, whichever physical bank
     * NVMCON.PFSWAP has mapped there; and in any case nothing in either image
     * ever writes PFSWAP (NVM_ProgramFlashSwapBank is never called), so it
     * stays at its power-on default.
     *
     * Regression test: test_909_nvm_survives_inapp_update.py in
     * daqifi-python-test-suite drives a real update over
     * FirmwareUpdateService and asserts precision, both calibration
     * coefficients and the WiFi credentials survive it, with firmware_crc32
     * required to CHANGE so a no-op update cannot pass. It reflashes the
     * board, so it is excluded from the release gate and run deliberately.
     *
     * Deliberately NOT using the USE_PAGE_ERASE path as the fix: that loop is
     * O(pages) with a blank check per page, and #532 was a bootloader
     * watchdog/USB-servicing timeout during programming. The panel erase keeps
     * this a single bulk operation and so does not reopen that risk. Page
     * erase remains the documented fallback if the panel erase ever proves
     * wrong on silicon.
     */
    APP_NVMOperation(LOWER_FLASH_REGION_ERASE_OPERATION);
    #endif
#endif
}

void APP_NVMWordWrite(void* address, uint32_t data)
{
     unsigned int addr = KVA_TO_PA((unsigned int) address);
#if defined(BOOTLOADER_LIVE_UPDATE_STATE_SAVE)
    // Ensure we write to the other program flash
    if (addr < 0x1FC00000)
    {
         addr += 0x100000;
    }
#endif
    PLIB_NVM_FlashAddressToModify(NVM_ID_0, addr);

    PLIB_NVM_FlashProvideData(NVM_ID_0, data);

    APP_NVMOperation(WORD_PROGRAM_OPERATION);
}

void APP_NVMRowWrite(void* address, void* data)
{
    unsigned int addr = KVA_TO_PA((unsigned int) address);
    #if defined(BOOTLOADER_LIVE_UPDATE_STATE_SAVE)
    // Ensure we write to the other program flash    
    if (addr < 0x1FC00000)
    {
        addr += 0x100000;
    }
    #endif

    PLIB_NVM_FlashAddressToModify(NVM_ID_0, addr);

    PLIB_NVM_DataBlockSourceAddress(NVM_ID_0, KVA_TO_PA((unsigned int)data));

    APP_NVMOperation(ROW_PROGRAM_OPERATION);
}

void APP_NVMQuadWordWrite(void* address, uint32_t* data)
{
 #if (USE_QUAD_WORD_WRITE)
    unsigned int addr = KVA_TO_PA((unsigned int) address);

    #if defined(BOOTLOADER_LIVE_UPDATE_STATE_SAVE)
    // Ensure we write to the other program flash
    if (addr < 0x1FC00000)
    {
        addr += 0x100000;
    }
    #endif

    if (PLIB_NVM_ExistsProvideQuadData(NVM_ID_0))
    {
        PLIB_NVM_FlashAddressToModify(NVM_ID_0, addr);

        PLIB_NVM_FlashProvideQuadData(NVM_ID_0, data);

        APP_NVMOperation(QUAD_WORD_PROGRAM_OPERATION);
    }
#endif
}

void APP_NVMClearError(void)
{
    APP_NVMOperation(NO_OPERATION);
}

char APP_ProgramHexRecord(uint8_t* HexRecord, int32_t totalLen)
{
    static T_HEX_RECORD HexRecordSt;
    uint8_t Checksum = 0;
    uint32_t i;
    uint32_t WrData;

#if (USE_QUAD_WORD_WRITE)
    uint32_t QuadData[4];   /* For Quad-write operations */
#endif
    void* ProgAddress;
    uint32_t nextRecStartPt = 0;

    while(totalLen>=5) // A hex record must be atleast 5 bytes. (1 Data Len byte + 1 rec type byte+ 2 address bytes + 1 crc)
    {
        HexRecord = &HexRecord[nextRecStartPt];
        HexRecordSt.RecDataLen = HexRecord[0];
        HexRecordSt.RecType = HexRecord[3];
        HexRecordSt.Data = &HexRecord[4];

        //Determine next record starting point.
        nextRecStartPt = HexRecordSt.RecDataLen + 5;

        // Decrement total hex record length by length of current record.
        totalLen = totalLen - nextRecStartPt;

        // Hex Record checksum check.
        Checksum = 0;
        for(i = 0; i < HexRecordSt.RecDataLen + 5; i++)
        {
            Checksum += HexRecord[i];
        }

        if(Checksum != 0)
        {
            return HEX_REC_CRC_ERROR;
        }
        else
        {
            // Hex record checksum OK.
            switch(HexRecordSt.RecType)
            {
                case DATA_RECORD:  //Record Type 00, data record.
                    HexRecordSt.Address = (HexRecord[1]<<8) + HexRecord[2];

                    // Derive the address.
                    HexRecordSt.Address = HexRecordSt.Address + HexRecordSt.ExtLinAddress + HexRecordSt.ExtSegAddress;

                    while(HexRecordSt.RecDataLen) // Loop till all bytes are done.
                    {
                        // Convert the Physical address to Virtual address.
                        ProgAddress = PA_TO_KVA0(HexRecordSt.Address);

                        // Make sure we are not writing boot area and device configuration bits.
                        if(((ProgAddress >= (void *)APP_FLASH_BASE_ADDRESS) && (ProgAddress <= (void *)APP_FLASH_END_ADDRESS)))
                        {
#if (USE_QUAD_WORD_WRITE)
                            /* Determine if we can do this with a quad-word write */
                            if (PLIB_NVM_ExistsProvideQuadData(NVM_ID_0) &&
                                    (HexRecordSt.RecDataLen >= 16) &&
                                    (0 == ((unsigned int)ProgAddress & 0xF)))
                            {
                                memcpy(QuadData, HexRecordSt.Data, 16); // Copy 4 words of data in.
                                APP_NVMQuadWordWrite(ProgAddress, QuadData);
                                // Increment the address.
                                HexRecordSt.Address += 16;
                                // Increment the data pointer.
                                HexRecordSt.Data += 16;
                                // Decrement data len.
                                if(HexRecordSt.RecDataLen > 15)
                                {
                                    HexRecordSt.RecDataLen -= 16;
                                }
                                else
                                {
                                    HexRecordSt.RecDataLen = 0;
                                }
                            }
                            else
#endif
                            {
                                if(HexRecordSt.RecDataLen < 4)
                                {
                                    // Sometimes record data length will not be in multiples of 4. Appending 0xFF will make sure that..
                                    // we don't write junk data in such cases.
                                    WrData = 0xFFFFFFFF;
                                    memcpy(&WrData, HexRecordSt.Data, HexRecordSt.RecDataLen);
                                }
                                else
                                {
                                    memcpy(&WrData, HexRecordSt.Data, 4);
                                }
                                // Write the data into flash.
                                APP_NVMWordWrite(ProgAddress, WrData);
                                // Increment the address.
                                HexRecordSt.Address += 4;
                                // Increment the data pointer.
                                HexRecordSt.Data += 4;
                                // Decrement data len.
                                if(HexRecordSt.RecDataLen > 3)
                                {
                                    HexRecordSt.RecDataLen -= 4;
                                }
                                else
                                {
                                    HexRecordSt.RecDataLen = 0;
                                }
                            }
                            while (!PLIB_NVM_FlashWriteCycleHasCompleted(NVM_ID_0));
                            if (PLIB_NVM_WriteOperationHasTerminated(NVM_ID_0))
                            {                                
                                APP_NVMClearError();
                                return HEX_REC_PGM_ERROR;
                            }
                                // Assert on error. This must be caught during debug phase.
    //                            ASSERT(Result==0);
                        }
                        else    // Out of boundaries. Adjust and move on.
                        {
                            // Increment the address.
                            HexRecordSt.Address += 4;
                            // Increment the data pointer.
                            HexRecordSt.Data += 4;
                            // Decrement data len.
                            if(HexRecordSt.RecDataLen > 3)
                            {
                                HexRecordSt.RecDataLen -= 4;
                            }
                            else
                            {
                                HexRecordSt.RecDataLen = 0;
                            }
                        }

                    }
                    break;

                case EXT_SEG_ADRS_RECORD:  // Record Type 02, defines 4th to 19th bits of the data address.
                    HexRecordSt.ExtSegAddress = (HexRecordSt.Data[0]<<12) + (HexRecordSt.Data[1]<<4);
                    
                    // Reset linear address.
                    HexRecordSt.ExtLinAddress = 0;
                    break;

                case EXT_LIN_ADRS_RECORD:   // Record Type 04, defines 16th to 31st bits of the data address.
                    HexRecordSt.ExtLinAddress = (HexRecordSt.Data[0]<<24) + (HexRecordSt.Data[1]<<16);

                    // Reset segment address.
                    HexRecordSt.ExtSegAddress = 0;
                    break;

                case END_OF_FILE_RECORD:  //Record Type 01, defines the end of file record.
                    HexRecordSt.ExtSegAddress = 0;
                    HexRecordSt.ExtLinAddress = 0;
                    break;
                default:
                    HexRecordSt.ExtSegAddress = 0;
                    HexRecordSt.ExtLinAddress = 0;
                    break;
            }

        }
    }//while(1)


    if ( (HexRecordSt.RecType == DATA_RECORD) || (HexRecordSt.RecType == EXT_SEG_ADRS_RECORD)
            || (HexRecordSt.RecType == EXT_LIN_ADRS_RECORD) || (HexRecordSt.RecType == END_OF_FILE_RECORD) )
    {
        return HEX_REC_NORMAL;
    }
    else
    {
        return HEX_REC_UNKNOW_TYPE;
    }

}
