/*******************************************************************************
  System Initialization File

  File Name:
    initialization.c

  Summary:
    This file contains source code necessary to initialize the system.

  Description:
    This file contains source code necessary to initialize the system.  It
    implements the "SYS_Initialize" function, defines the configuration bits,
    and allocates any necessary global system resources,
 *******************************************************************************/

// DOM-IGNORE-BEGIN
/*******************************************************************************
* Copyright (C) 2025 Microchip Technology Inc. and its subsidiaries.
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
// DOM-IGNORE-END

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************
#include "configuration.h"
#include "definitions.h"
#include "device.h"
#include "clock_config.h"   /* #487: single 200/252 MHz toggle (DAQIFI_SYSCLK_*) */


// ****************************************************************************
// ****************************************************************************
// Section: Configuration Bits
// ****************************************************************************
// ****************************************************************************

/*** DEVCFG0 ***/
#pragma config DEBUG =      OFF
#pragma config JTAGEN =     OFF
#pragma config ICESEL =     ICS_PGx1
#pragma config TRCEN =      OFF
#pragma config BOOTISA =    MIPS32
#pragma config FECCCON =    OFF_UNLOCKED
#pragma config FSLEEP =     OFF
#pragma config DBGPER =     PG_ALL
#pragma config SMCLR =      MCLR_NORM
#pragma config SOSCGAIN =   GAIN_LEVEL_3
#pragma config SOSCBOOST =  OFF
#pragma config POSCGAIN =   GAIN_LEVEL_3
#pragma config POSCBOOST =  OFF
#pragma config EJTAGBEN =   NORMAL
#pragma config CP =         OFF

/*** DEVCFG1 ***/
#pragma config FNOSC =      SPLL
#pragma config DMTINTV =    WIN_127_128
#pragma config FSOSCEN =    OFF
#pragma config IESO =       OFF
#pragma config POSCMOD =    EC
#pragma config OSCIOFNC =   OFF
#pragma config FCKSM =      CSECMD
#pragma config WDTPS =      PS1048576
#pragma config WDTSPGM =    STOP
#pragma config FWDTEN =     OFF
#pragma config WINDIS =     NORMAL
#pragma config FWDTWINSZ =  WINSZ_25
#pragma config DMTCNT =     DMT9
#pragma config FDMTEN =     OFF

/*** DEVCFG2 ***/
#pragma config FPLLIDIV =   DIV_3
#pragma config FPLLRNG =    RANGE_5_10_MHZ
#pragma config FPLLICLK =   PLL_POSC
/* #487: FPLLMULT selects SYSCLK via the DAQIFI_SYSCLK_252 toggle in
 * clock_config.h (pragma can't take a macro). 8 MHz PLL input (FPLLIDIV DIV_3),
 * /2 output (FPLLODIV) are the same for both points; only the multiplier moves. */
#if DAQIFI_SYSCLK_252
#pragma config FPLLMULT =   MUL_63    // 8 MHz x63 = 504 MHz VCO / 2 = 252 MHz SYSCLK
#else
#pragma config FPLLMULT =   MUL_50    // 8 MHz x50 = 400 MHz VCO / 2 = 200 MHz SYSCLK
#endif
#pragma config FPLLODIV =   DIV_2
#pragma config UPLLFSEL =   FREQ_24MHZ

/*** DEVCFG3 ***/
#pragma config USERID =     0xffff
#pragma config FMIIEN =     OFF
#pragma config FETHIO =     OFF
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// WARNING: DO NOT ALLOW MCC TO OVERWRITE - Custom configuration lock logic
// !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
// One-way configuration lock bits
// When ICSP logging is enabled, these must be OFF to allow runtime PPS configuration
// When disabled, these are ON for security to prevent unauthorized reconfiguration
// Note: Define ENABLE_ICSP_REALTIME_LOG=1 in project settings to enable debug logging
// REJECT MCC MERGE ATTEMPTS TO REPLACE THIS CONDITIONAL LOGIC!
#if defined(ENABLE_ICSP_REALTIME_LOG) && (ENABLE_ICSP_REALTIME_LOG == 1)
    #pragma config PGL1WAY =    OFF  // Allow multiple Permission Group Lock reconfigurations
    #pragma config PMDL1WAY =   OFF  // Allow multiple Peripheral Module Disable reconfigurations
    #pragma config IOL1WAY =    OFF  // Allow multiple I/O (PPS) lock reconfigurations
    #warning "Config lock bits disabled for ICSP logging - DO NOT RELEASE TO PRODUCTION"
#else
    // #664/#665: user digital-sensor peripherals on the DIO terminal need
    // RUNTIME reconfiguration of PPS (IOLOCK) and peripheral power (PMDLOCK) —
    // remap SDO1/SDI1, power SPI1, etc. — which the one-way locks would freeze
    // after the plib init. We don't rely on these locks as a security boundary,
    // so this is just: relax the two the feature needs and leave the unrelated
    // PGL1WAY (permission-group lock) at its default. Which subsystem may drive
    // each pin is gated by the DIO ownership registry (HAL/DIO.c).
    #pragma config PGL1WAY =    ON   // unrelated to PPS/PMD — left at default
    #pragma config PMDL1WAY =   OFF  // Allow multiple Peripheral Module Disable reconfigurations
    #pragma config IOL1WAY =    OFF  // Allow multiple I/O lock reconfigurations
#endif
#pragma config FUSBIDIO =   OFF

/*** BF1SEQ0 ***/

#pragma config TSEQ =       0xffff
#pragma config CSEQ =       0xffff





// *****************************************************************************
// *****************************************************************************
// Section: Driver Initialization Data
// *****************************************************************************
// *****************************************************************************
/* Following MISRA-C rules are deviated in the below code block */
/* MISRA C-2012 Rule 7.2 - Deviation record ID - H3_MISRAC_2012_R_7_2_DR_1 */
/* MISRA C-2012 Rule 11.1 - Deviation record ID - H3_MISRAC_2012_R_11_1_DR_1 */
/* MISRA C-2012 Rule 11.3 - Deviation record ID - H3_MISRAC_2012_R_11_3_DR_1 */
/* MISRA C-2012 Rule 11.8 - Deviation record ID - H3_MISRAC_2012_R_11_8_DR_1 */
// <editor-fold defaultstate="collapsed" desc="DRV_SDSPI Instance 0 Initialization Data">

/* SDSPI Client Objects Pool */
static DRV_SDSPI_CLIENT_OBJ drvSDSPI0ClientObjPool[DRV_SDSPI_CLIENTS_NUMBER_IDX0];

/* SDSPI Transfer Objects Pool */
static DRV_SDSPI_BUFFER_OBJ drvSDSPI0TransferObjPool[DRV_SDSPI_QUEUE_SIZE_IDX0];


/* SDSPI Driver Initialization Data */
static const DRV_SDSPI_INIT drvSDSPI0InitData =
{
    .spiDrvIndex            = 0,

    /* SDSPI Number of clients */
    .numClients             = DRV_SDSPI_CLIENTS_NUMBER_IDX0,

    /* SDSPI Client Objects Pool */
    .clientObjPool          = (uintptr_t)&drvSDSPI0ClientObjPool[0],

    /* SDSPI Transfer Objects Pool */
    .bufferObjPool          = (uintptr_t)&drvSDSPI0TransferObjPool[0],

    /* SDSPI Transfer Objects Queue Size */
    .bufferObjPoolSize      = DRV_SDSPI_QUEUE_SIZE_IDX0,

    .chipSelectPin          = DRV_SDSPI_CHIP_SELECT_PIN_IDX0,

    .sdcardSpeedHz          = DRV_SDSPI_SPEED_HZ_IDX0,

    .pollingIntervalMs      = DRV_SDSPI_POLLING_INTERVAL_MS_IDX0,

    .writeProtectPin        = SYS_PORT_PIN_NONE,

    .isFsEnabled            = true,

};
// </editor-fold>

// <editor-fold defaultstate="collapsed" desc="DRV_I2C Instance 0 Initialization Data">

/* I2C Client Objects Pool */
static DRV_I2C_CLIENT_OBJ drvI2C0ClientObjPool[DRV_I2C_CLIENTS_NUMBER_IDX0];

/* I2C PLib Interface Initialization */
static const DRV_I2C_PLIB_INTERFACE drvI2C0PLibAPI = {

    /* I2C PLib Transfer Read Add function */
    .read_t = (DRV_I2C_PLIB_READ)I2C5_Read,

    /* I2C PLib Transfer Write Add function */
    .write_t = (DRV_I2C_PLIB_WRITE)I2C5_Write,


    /* I2C PLib Transfer Write Read Add function */
    .writeRead = (DRV_I2C_PLIB_WRITE_READ)I2C5_WriteRead,

    /*I2C PLib Transfer Abort function */
    .transferAbort = (DRV_I2C_PLIB_TRANSFER_ABORT)I2C5_TransferAbort,

    /* I2C PLib Transfer Status function */
    .errorGet = (DRV_I2C_PLIB_ERROR_GET)I2C5_ErrorGet,

    /* I2C PLib Transfer Setup function */
    .transferSetup = (DRV_I2C_PLIB_TRANSFER_SETUP)I2C5_TransferSetup,

    /* I2C PLib Callback Register */
    .callbackRegister = (DRV_I2C_PLIB_CALLBACK_REGISTER)I2C5_CallbackRegister,
};


/* I2C Driver Initialization Data */
static const DRV_I2C_INIT drvI2C0InitData =
{
    /* I2C PLib API */
    .i2cPlib = &drvI2C0PLibAPI,

    /* I2C Number of clients */
    .numClients = DRV_I2C_CLIENTS_NUMBER_IDX0,

    /* I2C Client Objects Pool */
    .clientObjPool = (uintptr_t)&drvI2C0ClientObjPool[0],

    /* I2C Clock Speed */
    .clockSpeed = DRV_I2C_CLOCK_SPEED_IDX0,
};
// </editor-fold>

static const WDRV_WINC_SPI_CFG wdrvWincSpiInitData =
{
    .drvIndex           = DRV_SPI_INDEX_0,
    .chipSelect         = SYS_PORT_PIN_RK4  //Do Not Let the code generator change this
};

static const WDRV_WINC_SYS_INIT wdrvWincInitData = {
    .pSPICfg    = &wdrvWincSpiInitData,
    .intSrc     = GPIO_PIN_RD11
};

// <editor-fold defaultstate="collapsed" desc="DRV_SPI Instance 0 Initialization Data">

/* SPI Client Objects Pool */
static DRV_SPI_CLIENT_OBJ drvSPI0ClientObjPool[DRV_SPI_CLIENTS_NUMBER_IDX0];

/* SPI Transfer Objects Pool */
static DRV_SPI_TRANSFER_OBJ drvSPI0TransferObjPool[DRV_SPI_QUEUE_SIZE_IDX0];

/* SPI PLIB Interface Initialization */
static const DRV_SPI_PLIB_INTERFACE drvSPI0PlibAPI = {

    /* SPI PLIB Setup */
    .setup = (DRV_SPI_PLIB_SETUP)SPI4_TransferSetup,

    /* SPI PLIB WriteRead function */
    .writeRead = (DRV_SPI_PLIB_WRITE_READ)SPI4_WriteRead,

    /* SPI PLIB Transfer Status function */
    .isTransmitterBusy = (DRV_SPI_PLIB_TRANSMITTER_IS_BUSY)SPI4_IsTransmitterBusy,

    /* SPI PLIB Callback Register */
    .callbackRegister = (DRV_SPI_PLIB_CALLBACK_REGISTER)SPI4_CallbackRegister,
};

static const uint32_t drvSPI0remapDataBits[]= { 0x00000000, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0x00000400, 0x00000800 };
static const uint32_t drvSPI0remapClockPolarity[] = { 0x00000000, 0x00000040 };
static const uint32_t drvSPI0remapClockPhase[] = { 0x00000000, 0x00000100 };

static const DRV_SPI_INTERRUPT_SOURCES drvSPI0InterruptSources =
{
    /* Peripheral has more than one interrupt vectors */
    .isSingleIntSrc                        = false,

    /* Peripheral interrupt lines */
    .intSources.multi.spiTxReadyInt      = -1,
    .intSources.multi.spiTxCompleteInt   = (int32_t)_SPI4_TX_VECTOR,
    .intSources.multi.spiRxInt           = (int32_t)_SPI4_RX_VECTOR,
    /* DMA Tx interrupt line */
    .intSources.multi.dmaTxChannelInt      = (int32_t)_DMA0_VECTOR,
    /* DMA Rx interrupt line */
    .intSources.multi.dmaRxChannelInt      = (int32_t)_DMA1_VECTOR,
};

/* SPI Driver Initialization Data */
static const DRV_SPI_INIT drvSPI0InitData =
{
    /* SPI PLIB API */
    .spiPlib = &drvSPI0PlibAPI,

    .remapDataBits = drvSPI0remapDataBits,

    .remapClockPolarity = drvSPI0remapClockPolarity,

    .remapClockPhase = drvSPI0remapClockPhase,

    /* SPI Number of clients */
    .numClients = DRV_SPI_CLIENTS_NUMBER_IDX0,

    /* SPI Client Objects Pool */
    .clientObjPool = (uintptr_t)&drvSPI0ClientObjPool[0],

    /* DMA Channel for Transmit */
    .dmaChannelTransmit = DRV_SPI_XMIT_DMA_CH_IDX0,

    /* DMA Channel for Receive */
    .dmaChannelReceive  = DRV_SPI_RCV_DMA_CH_IDX0,

    /* SPI Transmit Register */
    .spiTransmitAddress =  (void *)&(SPI4BUF),

    /* SPI Receive Register */
    .spiReceiveAddress  = (void *)&(SPI4BUF),

    /* SPI Queue Size */
    .transferObjPoolSize = DRV_SPI_QUEUE_SIZE_IDX0,

    /* SPI Transfer Objects Pool */
    .transferObjPool = (uintptr_t)&drvSPI0TransferObjPool[0],

    /* SPI interrupt sources (SPI peripheral and DMA) */
    .interruptSources = &drvSPI0InterruptSources,
};
// </editor-fold>
// <editor-fold defaultstate="collapsed" desc="DRV_SPI Instance 1 Initialization Data">

/* SPI Client Objects Pool */
static DRV_SPI_CLIENT_OBJ drvSPI1ClientObjPool[DRV_SPI_CLIENTS_NUMBER_IDX1];

/* SPI Transfer Objects Pool */
static DRV_SPI_TRANSFER_OBJ drvSPI1TransferObjPool[DRV_SPI_QUEUE_SIZE_IDX1];

/* SPI PLIB Interface Initialization */
static const DRV_SPI_PLIB_INTERFACE drvSPI1PlibAPI = {

    /* SPI PLIB Setup */
    .setup = (DRV_SPI_PLIB_SETUP)SPI6_TransferSetup,

    /* SPI PLIB WriteRead function */
    .writeRead = (DRV_SPI_PLIB_WRITE_READ)SPI6_WriteRead,

    /* SPI PLIB Transfer Status function */
    .isTransmitterBusy = (DRV_SPI_PLIB_TRANSMITTER_IS_BUSY)SPI6_IsTransmitterBusy,

    /* SPI PLIB Callback Register */
    .callbackRegister = (DRV_SPI_PLIB_CALLBACK_REGISTER)SPI6_CallbackRegister,
};

static const uint32_t drvSPI1remapDataBits[]= { 0x00000000, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0x00000400, 0x00000800 };
static const uint32_t drvSPI1remapClockPolarity[] = { 0x00000000, 0x00000040 };
static const uint32_t drvSPI1remapClockPhase[] = { 0x00000000, 0x00000100 };

static const DRV_SPI_INTERRUPT_SOURCES drvSPI1InterruptSources =
{
    /* Peripheral has more than one interrupt vectors */
    .isSingleIntSrc                        = false,

    /* Peripheral interrupt lines */
    .intSources.multi.spiTxReadyInt      = -1,
    .intSources.multi.spiTxCompleteInt   = (int32_t)_SPI6_TX_VECTOR,
    .intSources.multi.spiRxInt           = (int32_t)_SPI6_RX_VECTOR,
    /* DMA Tx interrupt line */
    .intSources.multi.dmaTxChannelInt      = (int32_t)_DMA2_VECTOR,
    /* DMA Rx interrupt line */
    .intSources.multi.dmaRxChannelInt      = (int32_t)_DMA3_VECTOR,
};

/* SPI Driver Initialization Data */
static const DRV_SPI_INIT drvSPI1InitData =
{
    /* SPI PLIB API */
    .spiPlib = &drvSPI1PlibAPI,

    .remapDataBits = drvSPI1remapDataBits,

    .remapClockPolarity = drvSPI1remapClockPolarity,

    .remapClockPhase = drvSPI1remapClockPhase,

    /* SPI Number of clients */
    .numClients = DRV_SPI_CLIENTS_NUMBER_IDX1,

    /* SPI Client Objects Pool */
    .clientObjPool = (uintptr_t)&drvSPI1ClientObjPool[0],

    /* DMA Channel for Transmit */
    .dmaChannelTransmit = DRV_SPI_XMIT_DMA_CH_IDX1,

    /* DMA Channel for Receive */
    .dmaChannelReceive  = DRV_SPI_RCV_DMA_CH_IDX1,

    /* SPI Transmit Register */
    .spiTransmitAddress =  (void *)&(SPI6BUF),

    /* SPI Receive Register */
    .spiReceiveAddress  = (void *)&(SPI6BUF),

    /* SPI Queue Size */
    .transferObjPoolSize = DRV_SPI_QUEUE_SIZE_IDX1,

    /* SPI Transfer Objects Pool */
    .transferObjPool = (uintptr_t)&drvSPI1TransferObjPool[0],

    /* SPI interrupt sources (SPI peripheral and DMA) */
    .interruptSources = &drvSPI1InterruptSources,
};
// </editor-fold>
// <editor-fold defaultstate="collapsed" desc="DRV_SPI Instance 2 Initialization Data">

/* SPI Client Objects Pool */
static DRV_SPI_CLIENT_OBJ drvSPI2ClientObjPool[DRV_SPI_CLIENTS_NUMBER_IDX2];

/* SPI Transfer Objects Pool */
static DRV_SPI_TRANSFER_OBJ drvSPI2TransferObjPool[DRV_SPI_QUEUE_SIZE_IDX2];

/* SPI PLIB Interface Initialization */
static const DRV_SPI_PLIB_INTERFACE drvSPI2PlibAPI = {

    /* SPI PLIB Setup */
    .setup = (DRV_SPI_PLIB_SETUP)SPI2_TransferSetup,

    /* SPI PLIB WriteRead function */
    .writeRead = (DRV_SPI_PLIB_WRITE_READ)SPI2_WriteRead,

    /* SPI PLIB Transfer Status function */
    .isTransmitterBusy = (DRV_SPI_PLIB_TRANSMITTER_IS_BUSY)SPI2_IsTransmitterBusy,

    /* SPI PLIB Callback Register */
    .callbackRegister = (DRV_SPI_PLIB_CALLBACK_REGISTER)SPI2_CallbackRegister,
};

static const uint32_t drvSPI2remapDataBits[]= { 0x00000000, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0x00000400, 0x00000800 };
static const uint32_t drvSPI2remapClockPolarity[] = { 0x00000000, 0x00000040 };
static const uint32_t drvSPI2remapClockPhase[] = { 0x00000000, 0x00000100 };

static const DRV_SPI_INTERRUPT_SOURCES drvSPI2InterruptSources =
{
    /* Peripheral has more than one interrupt vectors */
    .isSingleIntSrc                        = false,

    /* Peripheral interrupt lines */
    .intSources.multi.spiTxReadyInt      = -1,
    .intSources.multi.spiTxCompleteInt   = (int32_t)_SPI2_TX_VECTOR,
    .intSources.multi.spiRxInt           = (int32_t)_SPI2_RX_VECTOR,
};

/* SPI Driver Initialization Data */
static const DRV_SPI_INIT drvSPI2InitData =
{
    /* SPI PLIB API */
    .spiPlib = &drvSPI2PlibAPI,

    .remapDataBits = drvSPI2remapDataBits,

    .remapClockPolarity = drvSPI2remapClockPolarity,

    .remapClockPhase = drvSPI2remapClockPhase,

    /* SPI Number of clients */
    .numClients = DRV_SPI_CLIENTS_NUMBER_IDX2,

    /* SPI Client Objects Pool */
    .clientObjPool = (uintptr_t)&drvSPI2ClientObjPool[0],

    /* DMA Channel for Transmit */
    .dmaChannelTransmit = SYS_DMA_CHANNEL_NONE,

    /* DMA Channel for Receive */
    .dmaChannelReceive  = SYS_DMA_CHANNEL_NONE,

    /* SPI Queue Size */
    .transferObjPoolSize = DRV_SPI_QUEUE_SIZE_IDX2,

    /* SPI Transfer Objects Pool */
    .transferObjPool = (uintptr_t)&drvSPI2TransferObjPool[0],

    /* SPI interrupt sources (SPI peripheral and DMA) */
    .interruptSources = &drvSPI2InterruptSources,
};
// </editor-fold>



// *****************************************************************************
// *****************************************************************************
// Section: System Data
// *****************************************************************************
// *****************************************************************************
/* Structure to hold the object handles for the modules in the system. */
SYSTEM_OBJECTS sysObj;

// *****************************************************************************
// *****************************************************************************
// Section: Library/Stack Initialization Data
// *****************************************************************************
// *****************************************************************************
/******************************************************
 * USB Driver Initialization
 ******************************************************/

static const DRV_USBHS_INIT drvUSBInit =
{
    /* Interrupt Source for USB module */
    .interruptSource = INT_SOURCE_USB,

    /* Interrupt Source for USB module */
    .interruptSourceUSBDma = INT_SOURCE_USB_DMA,
    /* System module initialization */
    .moduleInit = {0},

    /* USB Controller to operate as USB Device */
    .operationMode = DRV_USBHS_OPMODE_DEVICE,

    /* Enable High Speed Operation */
    .operationSpeed = USB_SPEED_HIGH,
    
    /* Stop in idle */
    .stopInIdle = true,

    /* Suspend in sleep */
    .suspendInSleep = false,

    /* Identifies peripheral (PLIB-level) ID */
    .usbID = USBHS_ID_0,

};


// <editor-fold defaultstate="collapsed" desc="File System Initialization Data">


const SYS_FS_MEDIA_MOUNT_DATA sysfsMountTable[SYS_FS_VOLUME_NUMBER] =
{
    {NULL}
};

static const SYS_FS_FUNCTIONS FatFsFunctions =
{
    .mount             = FATFS_mount,
    .unmount           = FATFS_unmount,
    .open              = FATFS_open,
    .read_t              = FATFS_read,
    .close             = FATFS_close,
    .seek              = FATFS_lseek,
    .fstat             = FATFS_stat,
    .getlabel          = FATFS_getlabel,
    .currWD            = FATFS_getcwd,
    .getstrn           = FATFS_gets,
    .openDir           = FATFS_opendir,
    .readDir           = FATFS_readdir,
    .closeDir          = FATFS_closedir,
    .chdir             = FATFS_chdir,
    .chdrive           = FATFS_chdrive,
    .write_t             = FATFS_write,
    .tell              = FATFS_tell,
    .eof               = FATFS_eof,
    .size              = FATFS_size,
    .mkdir             = FATFS_mkdir,
    .remove_t            = FATFS_unlink,
    .setlabel          = FATFS_setlabel,
    .truncate          = FATFS_truncate,
    .chmode            = FATFS_chmod,
    .chtime            = FATFS_utime,
    .rename_t            = FATFS_rename,
    .sync              = FATFS_sync,
    .putchr            = FATFS_putc,
    .putstrn           = FATFS_puts,
    .formattedprint    = FATFS_printf,
    .testerror         = FATFS_error,
    .formatDisk        = (FORMAT_DISK)FATFS_mkfs,
    .partitionDisk     = FATFS_fdisk,
    .getCluster        = FATFS_getclusters
};




static const SYS_FS_REGISTRATION_TABLE sysFSInit [ SYS_FS_MAX_FILE_SYSTEM_TYPE ] =
{
    {
        .nativeFileSystemType = FAT,
        .nativeFileSystemFunctions = &FatFsFunctions
    }
};
// </editor-fold>



// *****************************************************************************
// *****************************************************************************
// Section: System Initialization
// *****************************************************************************
// *****************************************************************************
// <editor-fold defaultstate="collapsed" desc="SYS_TIME Initialization Data">

static const SYS_TIME_PLIB_INTERFACE sysTimePlibAPI = {
    .timerCallbackSet = (SYS_TIME_PLIB_CALLBACK_REGISTER)CORETIMER_CallbackSet,
    .timerStart = (SYS_TIME_PLIB_START)CORETIMER_Start,
    .timerStop = (SYS_TIME_PLIB_STOP)CORETIMER_Stop ,
    .timerFrequencyGet = (SYS_TIME_PLIB_FREQUENCY_GET)CORETIMER_FrequencyGet,
    .timerPeriodSet = (SYS_TIME_PLIB_PERIOD_SET)NULL,
    .timerCompareSet = (SYS_TIME_PLIB_COMPARE_SET)CORETIMER_CompareSet,
    .timerCounterGet = (SYS_TIME_PLIB_COUNTER_GET)CORETIMER_CounterGet,
};

static const SYS_TIME_INIT sysTimeInitData =
{
    .timePlib = &sysTimePlibAPI,
    .hwTimerIntNum = 0,
};

// </editor-fold>



// *****************************************************************************
// *****************************************************************************
// Section: Local initialization functions
// *****************************************************************************
// *****************************************************************************

/* MISRAC 2012 deviation block end */

/*******************************************************************************
  Function:
    void SYS_Initialize ( void *data )

  Summary:
    Initializes the board, services, drivers, application and other modules.

  Remarks:
 */

/* ===================================================================== #741
 * Boot-time System PLL switch.
 *
 * WHY THIS EXISTS. FPLLMULT lives in DEVCFG2, a device Configuration Word.
 * Our USB bootloader refuses to program config words on purpose
 * (bootloader/.../nvm.c:244 "Make sure we are not writing boot area and device
 * configuration bits"), and DS80000663 Rev R erratum 45 says run-time
 * self-programming of them is not functional with no workaround. So a field
 * firmware update can never change the DEVCFG default, and every device
 * manufactured with the MUL_50 bootloader hex boots at 200 MHz forever (#716).
 *
 * But the DEVCFG value is only the POWER-ON DEFAULT. SPLLCON is an ordinary
 * runtime R/W register that POR-loads from DEVCFG2 (DS60001320G Register 8-3),
 * so software can override it. The datasheet in fact PRESCRIBES this for
 * 252 MHz parts (DS60001320G p.165):
 *
 *   "Devices that support 252 MHz operation should be configured for
 *    SYSCLK <= 200 MHz operation. Adjust the dividers of the PBCLKs, and then
 *    increase the SYSCLK to the desired speed."
 *
 * Boot-at-200-then-raise is the vendor's recommended bring-up, not a
 * workaround. Running it here also removes the transient this file documents
 * above (buses at 126 MHz during crt0), because a 200 MHz part's /2 reset
 * default is 100 MHz — in spec.
 *
 * WHY IT IS SAFE TO ATTEMPT. Every field device is already fused to permit it:
 * the bootloader hex sets FCKSM = CSECMD (clock switching enabled, FSCM
 * disabled) alongside the MUL_50 that causes the problem. Nothing here writes
 * flash, so the worst case is a boot that never reaches the application — and
 * the bootloader lives in boot flash and always runs first at reset, so button
 * entry still recovers the unit exactly as it does after any bad app update.
 *
 * SEQUENCE. FRM DS60001250B 42.3.7 Note 2 forbids changing the multiplier
 * while running from the affected PLL, so this is the documented two-step:
 * switch to FRC, rewrite SPLLCON, switch back. 42.3.7.2's recommended code
 * sequence is followed exactly, including the back-to-back key writes and an
 * immediate relock. Interrupts and DMA are required to be off during the
 * unlock; that holds by construction here — __builtin_enable_interrupts() is
 * not called until the end of SYS_Initialize, and no DMA is configured yet.
 *
 * 42.3.7.3: "If the new clock source does not start, or is not present, the
 * OSWEN bit remains set", so every wait below is BOUNDED. A failed raise
 * reverts the multiplier and returns to the PLL that demonstrably worked at
 * reset. If even that fails the part keeps running on FRC — slow, but alive,
 * bootloader intact, and #716's runtime clock derivation reports the real
 * frequency and clock_ok=false rather than lying about it.
 *
 * WHAT ACTUALLY PROTECTS THIS (read before changing it). The load-bearing
 * guard is the pre-switch validation in DAQIFI_ApplyTargetPll(): the only
 * multiplier ever written is the compile-time target, and it is written only
 * after proving, from the LIVE PLL input fields, that it yields exactly
 * DAQIFI_SYSCLK_HZ. The revert is belt-and-braces for a lock failure and is
 * deliberately NOT load-bearing — see the measured envelope below.
 *
 * MEASURED FREQUENCY ENVELOPE (E, bench 7E2898F46200E8A7, deliberate fault
 * injection observed via Windows device state):
 *   x1   -> VCO 8 MHz    -> SYSCLK ~4 MHz   : PLL LOCKS. The part runs but is
 *                                             too slow to meet USB enumeration
 *                                             timing — Windows reports "Device
 *                                             Descriptor Request Failed"
 *                                             (VID_0000): D+ pulled up, no
 *                                             descriptor returned. So the USB
 *                                             floor sits well above 4 MHz.
 *   x128 -> VCO 1024 MHz -> SYSCLK ~512 MHz : locks; CPU dies immediately
 *                                             (device absent from USB).
 *   x50 / x63 (200 / 252 MHz)               : fully functional.
 *
 * The 4 MHz result is not just empirical: 60 MHz is the DOCUMENTED minimum
 * SYSCLK with the USB module enabled (DS60001320H OS51 / MOS51, Tables 37-18
 * and 39-5 — "60 ... 252 MHz, USB module enabled"). 4 MHz is 15x below that
 * floor, so the enumeration failure is spec-predicted, not a surprise.
 *
 * The lesson for anyone extending this: a lock FAILURE is not the realistic
 * hazard, because this PLL locks across a very wide range. Writing the WRONG
 * multiplier is, and the validation is what prevents it. Runtime frequency
 * scaling would additionally have to stay above the 60 MHz USB floor, AND
 * recompute every PBCLK-derived BRG (consumer list atop clock_config.h), AND
 * carry its own fitted streaming caps — note the two-step switch necessarily
 * transits FRC at 8 MHz, i.e. below that USB floor, so a runtime switch would
 * likely drop the CDC connection every time. Evaluated and declined; a
 * boot-time selected point is the viable shape if it is ever wanted.
 */
#define DAQIFI_NOSC_FRC     0x0u   /* OSCCON NOSC: 000 = FRC (non-PLL) */
#define DAQIFI_NOSC_SPLL    0x1u   /* OSCCON NOSC: 001 = System PLL */
/* Generous: a PLL lock is microseconds, and this loop runs at 8 MHz FRC in the
 * worst case. Big enough never to trip on a healthy part, small enough that a
 * dead clock source cannot hang the boot. */
#define DAQIFI_OSW_TIMEOUT  2000000u
/* Separate symbol for the raise-to-target switch purely so a bench build can
 * shrink it and force the revert path to run. Production value is identical. */
#ifndef DAQIFI_OSW_SPLL_TIMEOUT
#define DAQIFI_OSW_SPLL_TIMEOUT  DAQIFI_OSW_TIMEOUT
#endif

/* One clock switch, per the FRM's recommended sequence.
 *
 * REGISTERS AND BITS (FRM DS60001250B 42.3.7.2 "Oscillator Switching Sequence";
 * register/bit definitions DS60001320H Register 8-1 OSCCON, Register 8-3
 * SPLLCON):
 *   SYSKEY<31:0>            0xAA996655 then 0x556699AA unlocks; any non-key
 *                           value (0x33333333) relocks. The two key writes
 *                           must be BACK-TO-BACK with no intervening
 *                           peripheral access (42.3.7.3).
 *   OSCCON<10:8> NOSC<2:0>  requested source; 000 = FRC, 001 = SPLL.
 *   OSCCON<14:12> COSC<2:0> current source, same encoding. NOSC == COSC is
 *                           defined as redundant: hardware clears OSWEN and
 *                           aborts (42.3.7.2), which is the only software way
 *                           to cancel a stuck switch.
 *   OSCCON<0>    OSWEN      1 initiates; hardware clears it on success, having
 *                           first waited for PLL lock when the new source uses
 *                           the PLL. "If the new clock source does not start,
 *                           or is not present, the OSWEN bit remains set"
 *                           (42.3.7.3) — hence the bounded wait.
 *
 * NO PLIB PATH EXISTS for this: the generated clock PLIB exposes only
 * CLK_Initialize() (plib_clk.h), which writes PMD bits, and nothing under
 * config/default/peripheral touches OSCCON or SPLLCON. CLAUDE.md's preference
 * order therefore lands on SFR bitfield accessors (used here for OSCCON and
 * SPLLCON) with raw writes only for SYSKEY, which has no bitfields — and only
 * after reading the FRM, cited above.
 *
 * @param nosc     NOSC value for the requested source.
 * @param timeout  bound on the OSWEN poll, in iterations. Parameterised rather
 *                 than fixed so a bench build can force a timeout and exercise
 *                 the revert path, which is otherwise unreachable (the PLL
 *                 locks over a very wide range — see the envelope above).
 * @return true if OSWEN cleared, i.e. the switch completed.
 */
static bool DAQIFI_ClockSwitch(uint32_t nosc, uint32_t timeout)
{
    uint32_t guard;

    /* Unlock: the two key writes MUST be back-to-back with no intervening
     * peripheral access (42.3.7.3). Nothing is inserted between them. */
    SYSKEY = 0x00000000U;
    SYSKEY = 0xAA996655U;
    SYSKEY = 0x556699AAU;

    OSCCONbits.NOSC = nosc;
    OSCCONbits.OSWEN = 1;

    /* "The system will not relock automatically. Perform the relock sequence as
     * soon as possible after the clock switch." The wait below is deliberately
     * outside the unlocked window. */
    SYSKEY = 0x33333333U;

    for (guard = 0u; guard < timeout; ++guard) {
        if (OSCCONbits.OSWEN == 0u) {
            return true;
        }
    }
    return false;
}

/* Write PLLMULT. Caller MUST already be off the PLL: DS60001320G Register 8-3
 * Note 2 says writes are not allowed while SPLL is the clock source. */
static void DAQIFI_SetPllMult(uint32_t multField)
{
    SYSKEY = 0x00000000U;
    SYSKEY = 0xAA996655U;
    SYSKEY = 0x556699AAU;
    SPLLCONbits.PLLMULT = multField;
    SYSKEY = 0x33333333U;
}

static void DAQIFI_ApplyTargetPll(void)
{
    const uint32_t target = (uint32_t)DAQIFI_PLLMULT_FIELD;
    uint32_t saved;
    uint32_t inHz;
    uint32_t idiv;
    uint32_t odivField;
    uint32_t odiv;
    uint32_t wouldBe;

    /* Already what this image wants AND actually running from the PLL — the
     * normal case on a PICkit-programmed unit whose DEVCFG matches the build.
     * Do nothing at all.
     *
     * COSC is checked too, not just the multiplier: SPLLCON can hold the target
     * while the part runs from FRC or POSC directly, in which case skipping
     * would leave initialisation to continue with peripheral constants that
     * assume the PLL frequency. Falling through instead lets the switch below
     * put the part onto the PLL (Qodo #742). */
    if ((SPLLCONbits.PLLMULT == target) && (OSCCONbits.COSC == DAQIFI_NOSC_SPLL)) {
        return;
    }

    /* Only switch when the outcome can be PROVEN to equal what every constant
     * in this build assumes. If the PLL is wired differently than expected
     * (unexpected input select, reserved PLLODIV), leave the hardware alone:
     * #716's runtime derivation will still report the truth, whereas a blind
     * switch could land somewhere neither correct nor detected. */
    inHz = (SPLLCONbits.PLLICLK != 0u) ? (uint32_t)DAQIFI_FRC_HZ
                                       : (uint32_t)DAQIFI_POSC_HZ;
    /* Register 8-3: with PLLICLK = FRC the input divider is ignored and the
     * PLL uses divide-by-1. */
    idiv = (SPLLCONbits.PLLICLK != 0u) ? 1u
                                       : ((uint32_t)SPLLCONbits.PLLIDIV + 1u);
    odivField = (uint32_t)SPLLCONbits.PLLODIV;
    if ((odivField < 1u) || (odivField > 5u)) {   /* 000 and 11x are Reserved */
        return;
    }
    odiv = 1u << odivField;
    wouldBe = (uint32_t)(((uint64_t)inHz * (target + 1u)) / idiv / odiv);
    if (wouldBe != (uint32_t)DAQIFI_SYSCLK_HZ) {
        return;
    }

    saved = (uint32_t)SPLLCONbits.PLLMULT;

    /* Step 1: off the PLL, so SPLLCON becomes writable. If this fails we are
     * still on the original PLL — a perfectly serviceable state. */
    if (!DAQIFI_ClockSwitch(DAQIFI_NOSC_FRC, DAQIFI_OSW_TIMEOUT)) {
        return;
    }

    /* Step 2: retune. Only PLLMULT moves; PLLIDIV / PLLODIV / PLLRANGE /
     * PLLICLK keep the values DEVCFG2 loaded, so the PLL input frequency and
     * therefore the PLLRANGE selection stay valid. */
    DAQIFI_SetPllMult(target);

    /* Step 3: back onto the PLL. Hardware waits for lock before clearing
     * OSWEN (42.3.7.3), so success here means the PLL is locked at the new
     * multiplier. */
    if (!DAQIFI_ClockSwitch(DAQIFI_NOSC_SPLL, DAQIFI_OSW_SPLL_TIMEOUT)) {
        /* Did not lock, so per 42.3.7.3 OSWEN is STILL SET and the switch is
         * still pending. Cancel it before touching SPLLCON again.
         *
         * UNTESTED, and labelled as such. Two fault-injection attempts
         * could not produce a genuine lock failure — the PLL locked both
         * times, just at a useless frequency (see the envelope above), and
         * OSWEN cleared normally, so this branch never executed. The cancel
         * below is FRM-documented behaviour, not something demonstrated on
         * hardware here.
         *
         * The documented way out is a REDUNDANT switch: 42.3.7.2 says when
         * NOSC equals COSC the hardware treats it as redundant, "the OSWEN
         * bit is cleared automatically and the clock switch is aborted".
         * COSC is still FRC here (the switch never completed), so asking for
         * FRC again is exactly that no-op, and it is the only software-visible
         * way to clear a stuck OSWEN — FSCM's OSWEN clear needs the Fail-Safe
         * Clock Monitor, which FCKSM = CSECMD leaves disabled. */
        (void)DAQIFI_ClockSwitch(DAQIFI_NOSC_FRC, DAQIFI_OSW_TIMEOUT);

        /* Now SPLLCON is writable again: restore the multiplier the part
         * booted with, which demonstrably locked at reset, and go back to it. */
        DAQIFI_SetPllMult(saved);
        (void)DAQIFI_ClockSwitch(DAQIFI_NOSC_SPLL, DAQIFI_OSW_TIMEOUT);
    }
}

void SYS_Initialize ( void* data )
{

    /* MISRAC 2012 deviation block start */
    /* MISRA C-2012 Rule 2.2 deviated in this file.  Deviation record ID -  H3_MISRAC_2012_R_2_2_DR_1 */

    /* Start out with interrupts disabled before configuring any modules */
    (void)__builtin_disable_interrupts();

  
    CLK_Initialize();

    /* #487: PBCLK divider /2 -> /3.  At 252 MHz SYSCLK the reset-default /2
     * puts every peripheral bus at 126 MHz, over the 100 MHz spec.  /3 = 84 MHz.
     * Harmony CLK_Initialize() only sets PMD, not PBxDIV, so these run at the
     * reset default /2 unless set here.  PB7 (CPU) is left at its /1 default so
     * the core runs at the full 252 MHz.  PBxDIV writes need the system unlock.
     *
     * KNOWN transient (Qodo #584, accepted): between reset and this point the
     * buses run at the /2 default = 126 MHz during crt0 (.data copy / .bss zero,
     * ~sub-ms). Accepted, not fixed: the only peripheral clocked during crt0 is
     * flash (PBCLK5), whose access timing is governed by PFMWS in SYSCLK cycles
     * (reset default = max 7 = most tolerant), and no I2C/SPI/UART/timer/ADC is
     * running yet. Empirically boots + runs clean. The spec-clean fix (set PBxDIV
     * in an XC32 _on_reset() hook before crt0) is deferred: it adds a new boot
     * path element (brick risk) disproportionate to a bounded, benign transient.
     * If revisited, keep these writes as the fallback. */
    SYSKEY = 0x00000000U;
    SYSKEY = 0xAA996655U;
    SYSKEY = 0x556699AAU;
    PB1DIVbits.PBDIV = DAQIFI_PBDIV;   /* system / INT controller / DMA */
    PB2DIVbits.PBDIV = DAQIFI_PBDIV;   /* I2C5, UART4, SPI2/4/6 */
    PB3DIVbits.PBDIV = DAQIFI_PBDIV;   /* timers (FreeRTOS tick, streaming), OC/IC, ADC ctrl */
    PB4DIVbits.PBDIV = DAQIFI_PBDIV;   /* PORTx */
    PB5DIVbits.PBDIV = DAQIFI_PBDIV;   /* flash controller, crypto, USB regs */
    SYSKEY = 0x33333333U;

    /* Configure Prefetch, Wait States and ECC */
    PRECONbits.PREFEN = 3;
    /* #487: flash wait states.  At 200 MHz this was 3 (errata #38: >184 MHz w/ ECC).
     * At 252 MHz set conservatively high (5) for the first bring-up so the CPU can
     * never fault on a flash read; extra wait states are hidden by prefetch + I-cache.
     * TODO(#487): tune down to the DS60001320H Table 7-1 / §39 value after boot-verify. */
    PRECONbits.PFMWS = DAQIFI_PFMWS;
    CFGCONbits.ECCCON = 3;

    /* #741: raise (or lower) the PLL to match this build. Deliberately AFTER
     * the PBxDIV and flash-wait-state writes above: the datasheet's 252 MHz
     * note says to set the PBCLK dividers first and increase SYSCLK last, and
     * PFMWS must already cover the higher speed before the core runs at it. */
    DAQIFI_ApplyTargetPll();



	GPIO_Initialize();


    OCMP8_Initialize();

    OCMP6_Initialize();

    OCMP7_Initialize();

	SPI4_Initialize();

	SPI6_Initialize();

    OCMP1_Initialize();

    OCMP4_Initialize();

    OCMP3_Initialize();

    NVM_Initialize();

    TMR6_Initialize();

    CORETIMER_Initialize();

    ADCHS_Initialize();

    TMR4_Initialize();


    TMR2_Initialize();

    TMR3_Initialize();

	SPI2_Initialize();

    DMAC_Initialize();

    I2C5_Initialize();


    /* MISRAC 2012 deviation block start */
    /* Following MISRA-C rules deviated in this block  */
    /* MISRA C-2012 Rule 11.3 - Deviation record ID - H3_MISRAC_2012_R_11_3_DR_1 */
    /* MISRA C-2012 Rule 11.8 - Deviation record ID - H3_MISRAC_2012_R_11_8_DR_1 */

    /* Initialize SDSPI0 Driver Instance */
    sysObj.drvSDSPI0 = DRV_SDSPI_Initialize(DRV_SDSPI_INDEX_0, (SYS_MODULE_INIT *)&drvSDSPI0InitData);

    /* Initialize I2C0 Driver Instance */
    sysObj.drvI2C0 = DRV_I2C_Initialize(DRV_I2C_INDEX_0, (SYS_MODULE_INIT *)&drvI2C0InitData);

    /* Initialize SPI0 Driver Instance (must be before WINC, as WINC depends on SPI0) */
    sysObj.drvSPI0 = DRV_SPI_Initialize(DRV_SPI_INDEX_0, (SYS_MODULE_INIT *)&drvSPI0InitData);

    /* Initialize the WINC Driver */
    sysObj.drvWifiWinc = WDRV_WINC_Initialize(0, (SYS_MODULE_INIT*)&wdrvWincInitData);

    /* Initialize SPI1 Driver Instance */
    sysObj.drvSPI1 = DRV_SPI_Initialize(DRV_SPI_INDEX_1, (SYS_MODULE_INIT *)&drvSPI1InitData);

    /* Initialize SPI2 Driver Instance */
    sysObj.drvSPI2 = DRV_SPI_Initialize(DRV_SPI_INDEX_2, (SYS_MODULE_INIT *)&drvSPI2InitData);


    /* MISRA C-2012 Rule 11.3, 11.8 deviated below. Deviation record ID -  
    H3_MISRAC_2012_R_11_3_DR_1 & H3_MISRAC_2012_R_11_8_DR_1*/
        
    sysObj.sysTime = SYS_TIME_Initialize(SYS_TIME_INDEX_0, (SYS_MODULE_INIT *)&sysTimeInitData);
    
    /* MISRAC 2012 deviation block end */


    /* Initialize the USB device layer */
    sysObj.usbDevObject0 = USB_DEVICE_Initialize (USB_DEVICE_INDEX_0 , ( SYS_MODULE_INIT* ) & usbDevInitData);


    CRYPT_WCCB_Initialize();
    /* Initialize USB Driver */ 
    sysObj.drvUSBHSObject = DRV_USBHS_Initialize(DRV_USBHS_INDEX_0, (SYS_MODULE_INIT *) &drvUSBInit);    

    /*** File System Service Initialization Code ***/
    (void) SYS_FS_Initialize( (const void *) sysFSInit );


    /* MISRAC 2012 deviation block end */
    APP_FREERTOS_Initialize();


    EVIC_Initialize();

	/* Enable global interrupts */
    (void)__builtin_enable_interrupts();



    /* MISRAC 2012 deviation block end */
}

/*******************************************************************************
 End of File
*/
