/*******************************************************************************
  System Configuration Header

  File Name:
    configuration.h

  Summary:
    Build-time configuration header for the system defined by this project.

  Description:
    An MPLAB Project may have multiple configurations.  This file defines the
    build-time options for a single configuration.

  Remarks:
    This configuration header must not define any prototypes or data
    definitions (or include any files that do).  It only provides macro
    definitions for build-time configuration options

*******************************************************************************/

// DOM-IGNORE-BEGIN
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
// DOM-IGNORE-END

#ifndef CONFIGURATION_H
#define CONFIGURATION_H

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************
/*  This section Includes other configuration headers necessary to completely
    define this configuration.
*/

#include "user.h"
#include "device.h"

// DOM-IGNORE-BEGIN
#ifdef __cplusplus  // Provide C++ Compatibility

extern "C" {

#endif
// DOM-IGNORE-END

// *****************************************************************************
// *****************************************************************************
// Section: System Configuration
// *****************************************************************************
// *****************************************************************************



// *****************************************************************************
// *****************************************************************************
// Section: System Service Configuration
// *****************************************************************************
// *****************************************************************************
/* TIME System Service Configuration Options */
#define SYS_TIME_INDEX_0                            (0)
#define SYS_TIME_MAX_TIMERS                         (5)
#define SYS_TIME_HW_COUNTER_WIDTH                   (32)
#define SYS_TIME_HW_COUNTER_PERIOD                  (4294967295U)
#define SYS_TIME_HW_COUNTER_HALF_PERIOD             (SYS_TIME_HW_COUNTER_PERIOD>>1)
#define SYS_TIME_CPU_CLOCK_FREQUENCY                (200000000)
#define SYS_TIME_COMPARE_UPDATE_EXECUTION_CYCLES    (620)


/* File System Service Configuration */

#define SYS_FS_MEDIA_NUMBER               (1U)
#define SYS_FS_VOLUME_NUMBER              (1U)

#define SYS_FS_AUTOMOUNT_ENABLE           false
#define SYS_FS_MAX_FILES                  (10U)
#define SYS_FS_MAX_FILE_SYSTEM_TYPE       (1U)
#define SYS_FS_MEDIA_MAX_BLOCK_SIZE       (512U)
#define SYS_FS_MEDIA_MANAGER_BUFFER_SIZE  (2048U)
#define SYS_FS_USE_LFN                    (1)
#define SYS_FS_FILE_NAME_LEN              (255U)
#define SYS_FS_CWD_STRING_LEN             (1024)

/* File System RTOS Configurations*/
#define SYS_FS_STACK_SIZE                 1024
#define SYS_FS_PRIORITY                   1

#define SYS_FS_FAT_VERSION                "v0.15"
#define SYS_FS_FAT_READONLY               false
#define SYS_FS_FAT_CODE_PAGE              437
#define SYS_FS_FAT_MAX_SS                 SYS_FS_MEDIA_MAX_BLOCK_SIZE
#define SYS_FS_FAT_ALIGNED_BUFFER_LEN     512








// *****************************************************************************
// *****************************************************************************
// Section: Driver Configuration
// *****************************************************************************
// *****************************************************************************
/* SDSPI Driver Instance 0 Configuration Options */
#define DRV_SDSPI_INDEX_0                       0
#define DRV_SDSPI_CLIENTS_NUMBER_IDX0           1
#define DRV_SDSPI_QUEUE_SIZE_IDX0               4
#define DRV_SDSPI_CHIP_SELECT_PIN_IDX0          SYS_PORT_PIN_RD9
#define DRV_SDSPI_SPEED_HZ_IDX0                 1000000
#define DRV_SDSPI_POLLING_INTERVAL_MS_IDX0      1000



/* SDSPI Driver Instance 0 RTOS Configurations*/
#define DRV_SDSPI_STACK_SIZE_IDX0               256
#define DRV_SDSPI_PRIORITY_IDX0                     2

/* I2C Driver Instance 0 Configuration Options */
#define DRV_I2C_INDEX_0                       0
#define DRV_I2C_CLIENTS_NUMBER_IDX0           1
#define DRV_I2C_CLOCK_SPEED_IDX0              50000

/* SDSPI Driver Common Configuration Options */
#define DRV_SDSPI_INSTANCES_NUMBER              (1U)

/* I2C Driver Common Configuration Options */
#define DRV_I2C_INSTANCES_NUMBER              (1U)


/* SPI Driver Common Configuration Options */
#define DRV_SPI_INSTANCES_NUMBER              (1U)

/*** WiFi WINC Driver Configuration ***/
#define WDRV_WINC_GPIO_SOURCE
#define WDRV_WINC_NETWORK_MODE_SOCKET
#define WDRV_WINC_DEVICE_WINC1500
#define WDRV_WINC_DEVICE_SPLIT_INIT
#define WDRV_WINC_DEVICE_ENTERPRISE_CONNECT
#define WDRV_WINC_DEVICE_EXT_CONNECT_PARAMS
#define WDRV_WINC_DEVICE_BSS_ROAMING
#define WDRV_WINC_DEVICE_FLEXIBLE_FLASH_MAP
#define WDRV_WINC_DEVICE_DYNAMIC_BYPASS_MODE
#define WDRV_WINC_DEVICE_WPA_SOFT_AP
#define WDRV_WINC_DEVICE_CONF_NTP_SERVER
#define WDRV_WINC_DEVICE_HOST_FILE_DOWNLOAD
#define WDRV_WINC_DEVICE_SOFT_AP_EXT
#define WDRV_WINC_DEVICE_MULTI_GAIN_TABLE
#define WDRV_WINC_DEVICE_URL_TYPE           unsigned char
#define WDRV_WINC_DEVICE_SCAN_STOP_ON_FIRST
#define WDRV_WINC_DEVICE_DEPRECATE_WEP
#define WDRV_WINC_DEVICE_OTA_SSL_OPTIONS
#define WDRV_WINC_DEVICE_OTA_STATUS_EXTENDED
#define WDRV_WINC_DEVICE_SCAN_SSID_LIST
#define WDRV_WINC_DEBUG_LEVEL               WDRV_WINC_DEBUG_TYPE_NONE
/*** WiFi WINC Driver RTOS Configuration ***/
#define DRV_WIFI_WINC_RTOS_STACK_SIZE           3000 //Daqifi: Modified Manually
#define DRV_WIFI_WINC_RTOS_TASK_PRIORITY        2

/* SPI Driver Instance 0 Configuration Options */
#define DRV_SPI_INDEX_0                       0
#define DRV_SPI_CLIENTS_NUMBER_IDX0           2
#define DRV_SPI_DMA_MODE
#define DRV_SPI_XMIT_DMA_CH_IDX0              SYS_DMA_CHANNEL_0
#define DRV_SPI_RCV_DMA_CH_IDX0               SYS_DMA_CHANNEL_1
#define DRV_SPI_QUEUE_SIZE_IDX0               8



// *****************************************************************************
// *****************************************************************************
// Section: Middleware & Other Library Configuration
// *****************************************************************************
// *****************************************************************************
/* Number of Endpoints used */
#define DRV_USBHS_ENDPOINTS_NUMBER                        4U

/* The USB Device Layer will not initialize the USB Driver */
#define USB_DEVICE_DRIVER_INITIALIZE_EXPLICIT

/* Maximum device layer instances */
#define USB_DEVICE_INSTANCES_NUMBER                         1U

/* EP0 size in bytes */
#define USB_DEVICE_EP0_BUFFER_SIZE                          64U


/* Maximum instances of CDC function driver */
#define USB_DEVICE_CDC_INSTANCES_NUMBER                     1U


/* CDC Transfer Queue Size for both read and
   write. Applicable to all instances of the
   function driver */
#define USB_DEVICE_CDC_QUEUE_DEPTH_COMBINED                 3U

/*** USB Driver Configuration ***/

/* Maximum USB driver instances */
#define DRV_USBHS_INSTANCES_NUMBER                        1U

/* Interrupt mode enabled */
#define DRV_USBHS_INTERRUPT_MODE                          true


/* Enables Device Support */
#define DRV_USBHS_DEVICE_SUPPORT                          true

/* Disable Host Support */
#define DRV_USBHS_HOST_SUPPORT                            false





/* Alignment for buffers that are submitted to USB Driver*/ 
#define USB_ALIGN  CACHE_ALIGN  __ALIGNED(16)



/*** wolfCrypt Library Configuration ***/
#define MICROCHIP_PIC32
#define MICROCHIP_MPLAB_HARMONY
#define MICROCHIP_MPLAB_HARMONY_3
#define HAVE_MCAPI
#define SIZEOF_LONG_LONG 8
#define WOLFSSL_USER_IO
#define NO_WRITEV
#define NO_FILESYSTEM
#define USE_FAST_MATH
#define NO_PWDBASED
#define HAVE_MCAPI
#define WOLF_CRYPTO_CB  // provide call-back support
#define WOLFCRYPT_ONLY
#if (__XC32_VERSION > 100000000)
#define WOLFSSL_HAVE_MIN
#define WOLFSSL_HAVE_MAX
#endif
#undef WOLFSSL_HAVE_MIN
#undef WOLFSSL_HAVE_MAX
// ---------- FUNCTIONAL CONFIGURATION START ----------
#define WOLFSSL_AES_SMALL_TABLES
#define NO_MD4
#define WOLFSSL_SHA224
#define WOLFSSL_AES_128
#define WOLFSSL_AES_192
#define WOLFSSL_AES_256
#define WOLFSSL_AES_DIRECT
#define HAVE_AES_DECRYPT
#define HAVE_AES_ECB
#define HAVE_AES_CBC
#define WOLFSSL_AES_COUNTER
#define WOLFSSL_AES_OFB
#define HAVE_AESGCM
#define HAVE_AESCCM
#define NO_RC4
#define NO_HC128
#define NO_RABBIT
#define HAVE_ECC
#define NO_DH
#define NO_DSA
#define FP_MAX_BITS 4096
#define USE_CERT_BUFFERS_2048
#define NO_DEV_RANDOM
#define HAVE_HASHDRBG
#define WC_NO_HARDEN
#define SINGLE_THREADED
#define NO_SIG_WRAPPER
#define NO_ERROR_STRINGS
#define NO_WOLFSSL_MEMORY
// ---------- FUNCTIONAL CONFIGURATION END ----------



// *****************************************************************************
// *****************************************************************************
// Section: Application Configuration
// *****************************************************************************
// *****************************************************************************


//DOM-IGNORE-BEGIN
#ifdef __cplusplus
}
#endif
//DOM-IGNORE-END

#endif // CONFIGURATION_H
/*******************************************************************************
 End of File
*/
