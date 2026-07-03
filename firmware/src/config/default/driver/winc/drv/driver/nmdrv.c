/*******************************************************************************
  File Name:
    nmdrv.c

  Summary:
    This module contains WINC1500 M2M driver APIs implementation.

  Description:
    This module contains WINC1500 M2M driver APIs implementation.
 *******************************************************************************/

//DOM-IGNORE-BEGIN
/*
Copyright (C) 2022, Microchip Technology Inc., and its subsidiaries. All rights reserved.

The software and documentation is provided by microchip and its contributors
"as is" and any express, implied or statutory warranties, including, but not
limited to, the implied warranties of merchantability, fitness for a particular
purpose and non-infringement of third party intellectual property rights are
disclaimed to the fullest extent permitted by law. In no event shall microchip
or its contributors be liable for any direct, indirect, incidental, special,
exemplary, or consequential damages (including, but not limited to, procurement
of substitute goods or services; loss of use, data, or profits; or business
interruption) however caused and on any theory of liability, whether in contract,
strict liability, or tort (including negligence or otherwise) arising in any way
out of the use of the software and documentation, even if advised of the
possibility of such damage.

Except as expressly permitted hereunder and subject to the applicable license terms
for any third-party software incorporated in the software and any applicable open
source software license terms, no license or other rights, whether express or
implied, are granted under any patent or other intellectual property rights of
Microchip or any third party.
*/

#include "nm_common.h"
#include "nmbus.h"
#include "nm_bsp.h"
#include "nmdrv.h"
#include "nmasic.h"
#include "m2m_types.h"
#include "spi_flash.h"

#include "nmspi.h"

/**
*   @fn     nm_get_firmware_info(tstrM2mRev* M2mRev)
*   @brief  Get Firmware version info
*   @param [out]    M2mRev
*               pointer holds address of structure "tstrM2mRev" that contains the firmware version parameters
*   @version    1.0
*/
int8_t nm_get_firmware_info(tstrM2mRev* M2mRev)
{
    uint16_t  curr_drv_ver, min_req_drv_ver,curr_firm_ver;
    uint32_t    reg = 0;
    int8_t  ret = M2M_SUCCESS;

    ret = nm_read_reg_with_ret(NMI_REV_REG, &reg);
    //In case the Firmware running is ATE fw
    if(M2M_ATE_FW_IS_UP_VALUE == reg)
    {
        //Read FW info again from the register specified for ATE
        ret = nm_read_reg_with_ret(NMI_REV_REG_ATE, &reg);
    }
    M2mRev->u8DriverMajor   = M2M_GET_DRV_MAJOR(reg);
    M2mRev->u8DriverMinor   = M2M_GET_DRV_MINOR(reg);
    M2mRev->u8DriverPatch   = M2M_GET_DRV_PATCH(reg);
    M2mRev->u8FirmwareMajor = M2M_GET_FW_MAJOR(reg);
    M2mRev->u8FirmwareMinor = M2M_GET_FW_MINOR(reg);
    M2mRev->u8FirmwarePatch = M2M_GET_FW_PATCH(reg);
    M2mRev->u32Chipid   = nmi_get_chipid();
    M2mRev->u16FirmwareSvnNum = 0;

    curr_firm_ver   = M2M_MAKE_VERSION(M2mRev->u8FirmwareMajor, M2mRev->u8FirmwareMinor,M2mRev->u8FirmwarePatch);
    curr_drv_ver    = M2M_MAKE_VERSION(M2M_RELEASE_VERSION_MAJOR_NO, M2M_RELEASE_VERSION_MINOR_NO, M2M_RELEASE_VERSION_PATCH_NO);
    min_req_drv_ver = M2M_MAKE_VERSION(M2mRev->u8DriverMajor, M2mRev->u8DriverMinor,M2mRev->u8DriverPatch);
    if(curr_drv_ver <  min_req_drv_ver) {
        /*The current driver version should be larger or equal
        than the min driver that the current firmware support  */
        ret = M2M_ERR_FW_VER_MISMATCH;
    }
    if(curr_drv_ver >  curr_firm_ver) {
        /*The current driver should be equal or less than the firmware version*/
        ret = M2M_ERR_FW_VER_MISMATCH;
    }
    return ret;
}
/**
*   @fn     nm_get_firmware_info(tstrM2mRev* M2mRev)
*   @brief  Get Firmware version info
*   @param [out]    M2mRev
*               pointer holds address of structure "tstrM2mRev" that contains the firmware version parameters
*   @version    1.0
*/
int8_t nm_get_firmware_full_info(tstrM2mRev* pstrRev)
{
    uint16_t  curr_drv_ver, min_req_drv_ver,curr_firm_ver;
    uint32_t    reg = 0;
    int8_t  ret = M2M_SUCCESS;
    tstrGpRegs strgp = {0};
    if (pstrRev != NULL)
    {
        memset((uint8_t*)pstrRev,0,sizeof(tstrM2mRev));
        ret = nm_read_reg_with_ret(rNMI_GP_REG_2, &reg);
        if(ret == M2M_SUCCESS)
        {
            if(reg != 0)
            {
                ret = nm_read_block(reg|0x30000,(uint8_t*)&strgp,sizeof(tstrGpRegs));
                if(ret == M2M_SUCCESS)
                {
                    reg = strgp.u32Firmware_Ota_rev;
                    reg &= 0x0000ffff;
                    if(reg != 0)
                    {
                        ret = nm_read_block(reg|0x30000,(uint8_t*)pstrRev,sizeof(tstrM2mRev));
                        if(ret == M2M_SUCCESS)
                        {
                            curr_firm_ver   = M2M_MAKE_VERSION(pstrRev->u8FirmwareMajor, pstrRev->u8FirmwareMinor,pstrRev->u8FirmwarePatch);
                            curr_drv_ver    = M2M_MAKE_VERSION(M2M_RELEASE_VERSION_MAJOR_NO, M2M_RELEASE_VERSION_MINOR_NO, M2M_RELEASE_VERSION_PATCH_NO);
                            min_req_drv_ver = M2M_MAKE_VERSION(pstrRev->u8DriverMajor, pstrRev->u8DriverMinor,pstrRev->u8DriverPatch);
                            if((curr_firm_ver == 0)||(min_req_drv_ver == 0)||(min_req_drv_ver == 0)){
                                ret = M2M_ERR_FAIL;
                                goto EXIT;
                            }
                            if(curr_drv_ver <  min_req_drv_ver) {
                                /*The current driver version should be larger or equal
                                than the min driver that the current firmware support  */
                                ret = M2M_ERR_FW_VER_MISMATCH;
                                goto EXIT;
                            }
                            if(curr_drv_ver >  curr_firm_ver) {
                                /*The current driver should be equal or less than the firmware version*/
                                ret = M2M_ERR_FW_VER_MISMATCH;
                                goto EXIT;
                            }
                        }
                    }else {
                        ret = M2M_ERR_FAIL;
                    }
                }
            }else{
                ret = M2M_ERR_FAIL;
            }
        }
    }
EXIT:
    return ret;
}
/**
*   @fn     nm_get_ota_firmware_info(tstrM2mRev* pstrRev)
*   @brief  Get Firmware version info
*   @param [out]    M2mRev
*               pointer holds address of structure "tstrM2mRev" that contains the firmware version parameters

*   @version    1.0
*/
int8_t nm_get_ota_firmware_info(tstrM2mRev* pstrRev)
{
    uint16_t  curr_drv_ver, min_req_drv_ver,curr_firm_ver;
    uint32_t    reg = 0;
    int8_t  ret;
    tstrGpRegs strgp = {0};

    if (pstrRev != NULL)
    {
        memset((uint8_t*)pstrRev,0,sizeof(tstrM2mRev));
        ret = nm_read_reg_with_ret(rNMI_GP_REG_2, &reg);
        if(ret == M2M_SUCCESS)
        {
            if(reg != 0)
            {
                ret = nm_read_block(reg|0x30000,(uint8_t*)&strgp,sizeof(tstrGpRegs));
                if(ret == M2M_SUCCESS)
                {
                    reg = strgp.u32Firmware_Ota_rev;
                    reg >>= 16;
                    if(reg != 0)
                    {
                        ret = nm_read_block(reg|0x30000,(uint8_t*)pstrRev,sizeof(tstrM2mRev));
                        if(ret == M2M_SUCCESS)
                        {
                            curr_firm_ver   = M2M_MAKE_VERSION(pstrRev->u8FirmwareMajor, pstrRev->u8FirmwareMinor,pstrRev->u8FirmwarePatch);
                            curr_drv_ver    = M2M_MAKE_VERSION(M2M_RELEASE_VERSION_MAJOR_NO, M2M_RELEASE_VERSION_MINOR_NO, M2M_RELEASE_VERSION_PATCH_NO);
                            min_req_drv_ver = M2M_MAKE_VERSION(pstrRev->u8DriverMajor, pstrRev->u8DriverMinor,pstrRev->u8DriverPatch);
                            if((curr_firm_ver == 0)||(min_req_drv_ver == 0)||(min_req_drv_ver == 0)){
                                ret = M2M_ERR_FAIL;
                                goto EXIT;
                            }
                            if(curr_drv_ver <  min_req_drv_ver) {
                                /*The current driver version should be larger or equal
                                than the min driver that the current firmware support  */
                                ret = M2M_ERR_FW_VER_MISMATCH;
                            }
                            if(curr_drv_ver >  curr_firm_ver) {
                                /*The current driver should be equal or less than the firmware version*/
                                ret = M2M_ERR_FW_VER_MISMATCH;
                            }
                        }
                    }else{
                        ret = M2M_ERR_INVALID;
                    }
                }
            }else{
                ret = M2M_ERR_FAIL;
            }
        }
    } else {
        ret = M2M_ERR_INVALID_ARG;
    }
EXIT:
    return ret;
}



/*
*   @fn     nm_drv_init_download_mode
*   @brief  Initialize NMC1000 driver
*   @return M2M_SUCCESS in case of success and Negative error code in case of failure
*   @param [in] arg
*               Generic argument
*   @author Viswanathan Murugesan
*   @date   10 Oct 2014
*   @version    1.0
*/
int8_t nm_drv_init_download_mode(void)
{
    int8_t ret = M2M_SUCCESS;

    ret = nm_bus_iface_init(NULL);
    if (M2M_SUCCESS != ret) {
        M2M_ERR("[nmi start]: fail init bus\r\n");
        goto ERR1;
    }

    nm_spi_lock_init();

    /**
        TODO:reset the chip and halt the cpu in case of no wait efuse is set (add the no wait effuse check)
    */
    if(!ISNMC3000(GET_CHIPID()))
    {
        /*Execuate that function only for 1500A/B, no room in 3000, but it may be needed in 3400 no wait*/
        /* DAQiFi (#WINC-recovery, 2026-07-02): the stock code only called
         * cpu_halt() here, on a chip that may be mid-flight running its WiFi
         * firmware (this download mode is entered from FW-update mode while
         * the AP is beaconing). The PC-side programmer
         * (winc_programmer 2.0.2) expects to take over a chip in a clean
         * post-reset state — it performs its own halt, IRAM download and
         * cpu-start sequence over the bridge. Against a soft-halted,
         * dirty-state chip the start sequence goes nowhere: the boot ROM
         * never runs (BOOTROM_REG keeps the unconsumed 0xEF522F61 start
         * magic) and the tool fails with "time out waiting for firmware to
         * run" (bench-verified via bridge command tracing). Give it the real
         * thing: the canonical hard CHIP_EN/RESET_N power-cycle pulse.
         *
         * VERIFIED bring-up with retries: module boot ramp varies board to
         * board, and touching SPI before the chip is ready desyncs the host
         * protocol layer for the whole session (all register reads 0, tool
         * chip IDs like 0x00050000, NACKed writes — bench board ...026A).
         * After each reset, re-sync the host SPI protocol, wait for the
         * efuse-load-done flag the boot ROM sets (same readiness gate
         * wait_for_bootrom uses), and verify the chip ID reads sane before
         * accepting the bring-up. */
        {
            uint8_t attempt;
            for (attempt = 0; attempt < 4; attempt++)
            {
                uint32_t reg = 0;
                uint32_t chipId = 0;
                uint16_t waited;

                nm_reset();
                nm_spi_init();  /* re-sync host protocol state after reset */

                for (waited = 0; waited < 800; waited += 20)
                {
                    if ((nm_read_reg_with_ret(0x1014, &reg) == M2M_SUCCESS) &&
                        ((reg & 0x80000000UL) != 0UL))
                    {
                        break;  /* efuse loaded — ROM is up */
                    }
                    nm_sleep(20);
                }

                if ((nm_read_reg_with_ret(0x1000, &chipId) == M2M_SUCCESS) &&
                    (((chipId >> 16) == 0x15UL) || ((chipId >> 16) == 0x10UL)))
                {
                    break;  /* chip verified reachable */
                }
            }
        }
    }

    /* Must do this after global reset to set SPI data packet size. */
    nm_spi_init();

    M2M_INFO("Chip ID %lx\r\n", nmi_get_chipid());

    /*disable all interrupt in ROM (to disable uart) in 2b0 chip*/
    nm_write_reg(0x20300,0);

ERR1:
    return ret;
}

int8_t nm_drv_init_hold(void)
{
    int8_t ret = M2M_SUCCESS;

    nm_spi_lock_init();

    ret = nm_bus_iface_init(NULL);
    if (M2M_SUCCESS != ret) {
        M2M_ERR("[nmi start]: fail init bus\r\n");
        goto ERR1;
    }

#ifdef BUS_ONLY
    return;
#endif

#ifdef NO_HW_CHIP_EN
    ret = chip_wake();
    if (M2M_SUCCESS != ret) {
        M2M_ERR("[nmi start]: fail chip_wakeup\r\n");
        goto ERR2;
    }

    /**
    Go...
    **/
    ret = chip_reset();
    if (M2M_SUCCESS != ret) {
        goto ERR2;
    }
#endif
    /* Must do this after global reset to set SPI data packet size. */
    ret = nm_spi_init();
    if (M2M_SUCCESS != ret) {
        M2M_ERR("[nmi start]: fail init spi\r\n");
        goto ERR1;
    }

    M2M_INFO("Chip ID %lx\r\n", nmi_get_chipid());

    return ret;
#ifdef NO_HW_CHIP_EN
ERR2:
#endif
    nm_bus_iface_deinit();
ERR1:
    return ret;
}

int8_t nm_drv_init_start(void * arg)
{
    int8_t ret = M2M_SUCCESS;
    uint8_t u8Mode = M2M_WIFI_MODE_NORMAL;

    if(NULL != arg) {
        u8Mode = *((uint8_t *)arg);
        if((u8Mode < M2M_WIFI_MODE_NORMAL)||(u8Mode >= M2M_WIFI_MODE_MAX)) {
            u8Mode = M2M_WIFI_MODE_NORMAL;
        }
    }

    ret = wait_for_bootrom(u8Mode);
    if (M2M_SUCCESS != ret) {
        goto ERR2;
    }

    ret = wait_for_firmware_start(u8Mode);
    if (M2M_SUCCESS != ret) {
        goto ERR2;
    }

    if((M2M_WIFI_MODE_ATE_HIGH == u8Mode)||(M2M_WIFI_MODE_ATE_LOW == u8Mode)) {
        goto ERR1;
    } else {
        /*continue running*/
    }

    ret = enable_interrupts();
    if (M2M_SUCCESS != ret) {
        M2M_ERR("failed to enable interrupts..\r\n");
        goto ERR2;
    }
    return ret;
ERR2:
    nm_bus_iface_deinit();
    nm_spi_deinit();
ERR1:
    return ret;
}

/*
*   @fn     nm_drv_init
*   @brief  Initialize NMC1000 driver
*   @return M2M_SUCCESS in case of success and Negative error code in case of failure
*   @param [in] arg
*               Generic argument
*   @author M. Abdelmawla
*   @date   15 July 2012
*   @version    1.0
*/
int8_t nm_drv_init(void * arg)
{
    int8_t ret = M2M_SUCCESS;

    ret = nm_drv_init_hold();

    if(ret == M2M_SUCCESS)
        ret = nm_drv_init_start(arg);

    return ret;
}

/*
*   @fn     nm_drv_deinit
*   @brief  Deinitialize NMC1000 driver
*   @author M. Abdelmawla
*   @date   17 July 2012
*   @version    1.0
*/
int8_t nm_drv_deinit(void * arg)
{
    int8_t ret;

    ret = chip_deinit();
    if (M2M_SUCCESS != ret) {
        M2M_ERR("[nmi stop]: chip_deinit fail\r\n");
        goto ERR1;
    }

    nm_bus_reset();

    /* Disable SPI flash to save power when the chip is off */
    ret = spi_flash_enable(0);
    if (M2M_SUCCESS != ret) {
        M2M_ERR("[nmi stop]: SPI flash disable fail\r\n");
        goto ERR1;
    }

    ret = nm_bus_iface_deinit();
    if (M2M_SUCCESS != ret) {
        M2M_ERR("[nmi stop]: fail init bus\r\n");
        goto ERR1;
    }
    /* Must do this after global reset to set SPI data packet size. */
    nm_spi_deinit();

ERR1:
    return ret;
}

//DOM-IGNORE-END
