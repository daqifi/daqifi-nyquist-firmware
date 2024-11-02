/*******************************************************************************
  MPLAB Harmony System Configuration Header

  File Name:
    system_config.h

  Summary:
    Build-time configuration header for the system defined by this MPLAB Harmony
    project.

  Description:
    An MPLAB Project may have multiple configurations.  This file defines the
    build-time options for a single configuration.

  Remarks:
    This configuration header must not define any prototypes or data
    definitions (or include any files that do).  It only provides macro
    definitions for build-time configuration options that are not instantiated
    until used by another MPLAB Harmony module or application.

    Created with MPLAB Harmony Version 2.06
*******************************************************************************/

// DOM-IGNORE-BEGIN
/*******************************************************************************
Copyright (c) 2013-2015 released Microchip Technology Inc.  All rights reserved.

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
// DOM-IGNORE-END

#ifndef _SYSTEM_CONFIG_H
#define _SYSTEM_CONFIG_H

// *****************************************************************************
// *****************************************************************************
// Section: Included Files
// *****************************************************************************
// *****************************************************************************
/*  This section Includes other configuration headers necessary to completely
    define this configuration.
*/


// DOM-IGNORE-BEGIN
#ifdef __cplusplus  // Provide C++ Compatibility

extern "C" {

#endif
// DOM-IGNORE-END

// *****************************************************************************
// *****************************************************************************
// Section: System Service Configuration
// *****************************************************************************
// *****************************************************************************
// *****************************************************************************
/* Common System Service Configuration Options
*/
#define SYS_VERSION_STR           "2.06"
#define SYS_VERSION               20600

// *****************************************************************************
/* Clock System Service Configuration Options
*/
#define SYS_CLK_FREQ                        50000000ul
#define SYS_CLK_BUS_PERIPHERAL_1            25000000ul
#define SYS_CLK_BUS_PERIPHERAL_2            25000000ul
#define SYS_CLK_BUS_PERIPHERAL_3            25000000ul
#define SYS_CLK_BUS_PERIPHERAL_4            25000000ul
#define SYS_CLK_BUS_PERIPHERAL_5            25000000ul
#define SYS_CLK_BUS_PERIPHERAL_7            50000000ul
#define SYS_CLK_BUS_PERIPHERAL_8            25000000ul
#define SYS_CLK_CONFIG_PRIMARY_XTAL         24000000ul
#define SYS_CLK_CONFIG_SECONDARY_XTAL       0ul
#define SYS_CLK_CONFIG_FREQ_ERROR_LIMIT     10
#define SYS_CLK_WAIT_FOR_SWITCH             true
#define SYS_CLK_ON_WAIT                     OSC_ON_WAIT_IDLE 
   
/*** Ports System Service Configuration ***/
#define SYS_PORT_A_ANSEL        0x3F03
#define SYS_PORT_A_TRIS         0xBFDF
#define SYS_PORT_A_LAT          0x0020
#define SYS_PORT_A_ODC          0x0000
#define SYS_PORT_A_CNPU         0x8000
#define SYS_PORT_A_CNPD         0x0020
#define SYS_PORT_A_CNEN         0x0000

#define SYS_PORT_B_ANSEL        0x3DF0
#define SYS_PORT_B_TRIS         0xBDFD
#define SYS_PORT_B_LAT          0x0000
#define SYS_PORT_B_ODC          0x0000
#define SYS_PORT_B_CNPU         0x0000
#define SYS_PORT_B_CNPD         0x0008
#define SYS_PORT_B_CNEN         0x8000

#define SYS_PORT_C_ANSEL        0x4FF1
#define SYS_PORT_C_TRIS         0x5FF7
#define SYS_PORT_C_LAT          0x8000
#define SYS_PORT_C_ODC          0x0000
#define SYS_PORT_C_CNPU         0x0000
#define SYS_PORT_C_CNPD         0x0000
#define SYS_PORT_C_CNEN         0x0000

#define SYS_PORT_D_ANSEL        0x4100
#define SYS_PORT_D_TRIS         0xDD7A
#define SYS_PORT_D_LAT          0x2280
#define SYS_PORT_D_ODC          0x0000
#define SYS_PORT_D_CNPU         0x0230
#define SYS_PORT_D_CNPD         0x0000
#define SYS_PORT_D_CNEN         0x0000

#define SYS_PORT_E_ANSEL        0xFF00
#define SYS_PORT_E_TRIS         0xFF7A
#define SYS_PORT_E_LAT          0x0005
#define SYS_PORT_E_ODC          0x0000
#define SYS_PORT_E_CNPU         0x0000
#define SYS_PORT_E_CNPD         0x0085
#define SYS_PORT_E_CNEN         0x0000

#define SYS_PORT_F_ANSEL        0xFEC0
#define SYS_PORT_F_TRIS         0xFFFF
#define SYS_PORT_F_LAT          0x0000
#define SYS_PORT_F_ODC          0x0000
#define SYS_PORT_F_CNPU         0x0000
#define SYS_PORT_F_CNPD         0x0004
#define SYS_PORT_F_CNEN         0x0000

#define SYS_PORT_G_ANSEL        0x0E3C
#define SYS_PORT_G_TRIS         0x2FBF
#define SYS_PORT_G_LAT          0x0000
#define SYS_PORT_G_ODC          0x0000
#define SYS_PORT_G_CNPU         0x0080
#define SYS_PORT_G_CNPD         0x8000
#define SYS_PORT_G_CNEN         0x0000

#define SYS_PORT_H_ANSEL        0x0063
#define SYS_PORT_H_TRIS         0x1E43
#define SYS_PORT_H_LAT          0x803C
#define SYS_PORT_H_ODC          0x8000
#define SYS_PORT_H_CNPU         0x0000
#define SYS_PORT_H_CNPD         0x0000
#define SYS_PORT_H_CNEN         0x0000

#define SYS_PORT_J_ANSEL        0x0000
#define SYS_PORT_J_TRIS         0x4B4A
#define SYS_PORT_J_LAT          0x0414
#define SYS_PORT_J_ODC          0x0000
#define SYS_PORT_J_CNPU         0x0000
#define SYS_PORT_J_CNPD         0x1480
#define SYS_PORT_J_CNEN         0x0000

#define SYS_PORT_K_ANSEL        0xFF00
#define SYS_PORT_K_TRIS         0xFF00
#define SYS_PORT_K_LAT          0x0031
#define SYS_PORT_K_ODC          0x0000
#define SYS_PORT_K_CNPU         0x0001
#define SYS_PORT_K_CNPD         0x0000
#define SYS_PORT_K_CNEN         0x0000


/*** Interrupt System Service Configuration ***/
#define SYS_INT                     true
/*** Timer System Service Configuration ***/
#define SYS_TMR_POWER_STATE             SYS_MODULE_POWER_RUN_FULL
#define SYS_TMR_DRIVER_INDEX            DRV_TMR_INDEX_0
#define SYS_TMR_MAX_CLIENT_OBJECTS      5
#define SYS_TMR_FREQUENCY               1000
#define SYS_TMR_FREQUENCY_TOLERANCE     10
#define SYS_TMR_UNIT_RESOLUTION         10000
#define SYS_TMR_CLIENT_TOLERANCE        10
#define SYS_TMR_INTERRUPT_NOTIFICATION  false

// *****************************************************************************
// *****************************************************************************
// Section: Driver Configuration
// *****************************************************************************
// *****************************************************************************
/*** Timer Driver Configuration ***/
#define DRV_TMR_INTERRUPT_MODE             true
#define DRV_TMR_INSTANCES_NUMBER           1
#define DRV_TMR_CLIENTS_NUMBER             1

/*** Timer Driver 0 Configuration ***/
#define DRV_TMR_PERIPHERAL_ID_IDX0          TMR_ID_2
#define DRV_TMR_INTERRUPT_SOURCE_IDX0       INT_SOURCE_TIMER_2
#define DRV_TMR_INTERRUPT_VECTOR_IDX0       INT_VECTOR_T2
#define DRV_TMR_ISR_VECTOR_IDX0             _TIMER_2_VECTOR
#define DRV_TMR_INTERRUPT_PRIORITY_IDX0     INT_PRIORITY_LEVEL4
#define DRV_TMR_INTERRUPT_SUB_PRIORITY_IDX0 INT_SUBPRIORITY_LEVEL0
#define DRV_TMR_CLOCK_SOURCE_IDX0           DRV_TMR_CLKSOURCE_INTERNAL
#define DRV_TMR_PRESCALE_IDX0               TMR_PRESCALE_VALUE_256
#define DRV_TMR_OPERATION_MODE_IDX0         DRV_TMR_OPERATION_MODE_16_BIT
#define DRV_TMR_ASYNC_WRITE_ENABLE_IDX0     false
#define DRV_TMR_POWER_STATE_IDX0            SYS_MODULE_POWER_RUN_FULL


 
// *****************************************************************************
// *****************************************************************************
// Section: Middleware & Other Library Configuration
// *****************************************************************************
// *****************************************************************************
// *****************************************************************************
// Section: Bootloader Configuration
// *****************************************************************************
#ifdef DRV_SDCARD_INSTANCES_NUMBER
#define BTL_TRIGGER_SWITCH    BSP_SWITCH_S1
#else
#define BTL_TRIGGER_SWITCH    BSP_SWITCH_1
#endif
#define BTL_LED               BSP_LED_1


/* APP_FLASH_BASE_ADDRESS and APP_FLASH_END_ADDRESS reserves program Flash for the application*/
/* Rule:
    1)The memory regions kseg0_program_mem, kseg0_boot_mem, exception_mem and
    kseg1_boot_mem of the application linker script must fall with in APP_FLASH_BASE_ADDRESS
    and APP_FLASH_END_ADDRESS

    2)The base address and end address must align on boundaries according to the flash page size */
#define APP_FLASH_BASE_ADDRESS          (0x9D000000)

#define APP_FLASH_END_ADDRESS           (0x9D000000 + 0x200000 - 1)

/* Address of  the Flash from where the application starts executing */
/* Rule: Set APP_FLASH_BASE_ADDRESS to _RESET_ADDR value of application linker script*/
#define APP_RESET_ADDRESS               (APP_FLASH_BASE_ADDRESS)

// *****************************************************************************
// Section: Bootloader NVM Driver Configuration
// *****************************************************************************
/* NVM Driver Flash Memory row and page size in bytes */
#define USE_PAGE_ERASE  0
#define USE_QUAD_WORD_WRITE 1


/*** USB Driver Configuration ***/


/* Enables Device Support */
#define DRV_USBHS_DEVICE_SUPPORT      true

/* Disable Host Support */
#define DRV_USBHS_HOST_SUPPORT      false

/* Maximum USB driver instances */
#define DRV_USBHS_INSTANCES_NUMBER    1

/* Interrupt mode enabled */
#define DRV_USBHS_INTERRUPT_MODE      true


/* Number of Endpoints used */
#define DRV_USBHS_ENDPOINTS_NUMBER    2




/*** USB Device Stack Configuration ***/










/* The USB Device Layer will not initialize the USB Driver */
#define USB_DEVICE_DRIVER_INITIALIZE_EXPLICIT

/* Maximum device layer instances */
#define USB_DEVICE_INSTANCES_NUMBER     1

/* EP0 size in bytes */
#define USB_DEVICE_EP0_BUFFER_SIZE      64









/* Maximum instances of HID function driver */
#define USB_DEVICE_HID_INSTANCES_NUMBER     1










/* HID Transfer Queue Size for both read and
   write. Applicable to all instances of the
   function driver */
#define USB_DEVICE_HID_QUEUE_DEPTH_COMBINED 2





// *****************************************************************************
// *****************************************************************************
// Section: Application Configuration
// *****************************************************************************
// *****************************************************************************
/*** Application Defined Pins ***/

/*** Functions for DIO_EN_10 pin ***/
#define DIO_EN_10_PORT PORT_CHANNEL_G
#define DIO_EN_10_PIN PORTS_BIT_POS_15
#define DIO_EN_10_PIN_MASK (0x1 << 15)

/*** Functions for DIO_EN_14 pin ***/
#define DIO_EN_14_PORT PORT_CHANNEL_A
#define DIO_EN_14_PIN PORTS_BIT_POS_5
#define DIO_EN_14_PIN_MASK (0x1 << 5)

/*** Functions for DIO_14 pin ***/
#define DIO_14_PORT PORT_CHANNEL_E
#define DIO_14_PIN PORTS_BIT_POS_5
#define DIO_14_PIN_MASK (0x1 << 5)

/*** Functions for DIO_13 pin ***/
#define DIO_13_PORT PORT_CHANNEL_E
#define DIO_13_PIN PORTS_BIT_POS_6
#define DIO_13_PIN_MASK (0x1 << 6)

/*** Functions for DIO_EN_13 pin ***/
#define DIO_EN_13_PORT PORT_CHANNEL_E
#define DIO_EN_13_PIN PORTS_BIT_POS_7
#define DIO_EN_13_PIN_MASK (0x1 << 7)

/*** Functions for DIO_15 pin ***/
#define DIO_15_PORT PORT_CHANNEL_C
#define DIO_15_PIN PORTS_BIT_POS_1
#define DIO_15_PIN_MASK (0x1 << 1)

/*** Functions for DIO_EN_15 pin ***/
#define DIO_EN_15_PORT PORT_CHANNEL_J
#define DIO_EN_15_PIN PORTS_BIT_POS_12
#define DIO_EN_15_PIN_MASK (0x1 << 12)

/*** Functions for DIO_EN_11 pin ***/
#define DIO_EN_11_PORT PORT_CHANNEL_J
#define DIO_EN_11_PIN PORTS_BIT_POS_10
#define DIO_EN_11_PIN_MASK (0x1 << 10)

/*** Functions for DIO_11 pin ***/
#define DIO_11_PORT PORT_CHANNEL_C
#define DIO_11_PIN PORTS_BIT_POS_2
#define DIO_11_PIN_MASK (0x1 << 2)

/*** Functions for LED_WHITE pin ***/
#define LED_WHITE_PORT PORT_CHANNEL_C
#define LED_WHITE_PIN PORTS_BIT_POS_3
#define LED_WHITE_PIN_MASK (0x1 << 3)

/*** Functions for DACXX18_CS pin ***/
#define DACXX18_CS_PORT PORT_CHANNEL_K
#define DACXX18_CS_PIN PORTS_BIT_POS_0
#define DACXX18_CS_PIN_MASK (0x1 << 0)

/*** Functions for DACXX18_CLR_RST pin ***/
#define DACXX18_CLR_RST_PORT PORT_CHANNEL_J
#define DACXX18_CLR_RST_PIN PORTS_BIT_POS_13
#define DACXX18_CLR_RST_PIN_MASK (0x1 << 13)

/*** Functions for BUTTON pin ***/
#define BUTTON_PORT PORT_CHANNEL_J
#define BUTTON_PIN PORTS_BIT_POS_14
#define BUTTON_PIN_MASK (0x1 << 14)

/*** Functions for PWR_VREF_EN pin ***/
#define PWR_VREF_EN_PORT PORT_CHANNEL_J
#define PWR_VREF_EN_PIN PORTS_BIT_POS_15
#define PWR_VREF_EN_PIN_MASK (0x1 << 15)

/*** Functions for AD7609_BUSY pin ***/
#define AD7609_BUSY_PORT PORT_CHANNEL_B
#define AD7609_BUSY_PIN PORTS_BIT_POS_3
#define AD7609_BUSY_PIN_MASK (0x1 << 3)

/*** Functions for AD7609_FDATA pin ***/
#define AD7609_FDATA_PORT PORT_CHANNEL_B
#define AD7609_FDATA_PIN PORTS_BIT_POS_2
#define AD7609_FDATA_PIN_MASK (0x1 << 2)

/*** Functions for AD7609_CS pin ***/
#define AD7609_CS_PORT PORT_CHANNEL_H
#define AD7609_CS_PIN PORTS_BIT_POS_2
#define AD7609_CS_PIN_MASK (0x1 << 2)

/*** Functions for AD7609_RESET pin ***/
#define AD7609_RESET_PORT PORT_CHANNEL_H
#define AD7609_RESET_PIN PORTS_BIT_POS_3
#define AD7609_RESET_PIN_MASK (0x1 << 3)

/*** Functions for AD7609_CONVST pin ***/
#define AD7609_CONVST_PORT PORT_CHANNEL_B
#define AD7609_CONVST_PIN PORTS_BIT_POS_9
#define AD7609_CONVST_PIN_MASK (0x1 << 9)

/*** Functions for AD7609_RANGE pin ***/
#define AD7609_RANGE_PORT PORT_CHANNEL_K
#define AD7609_RANGE_PIN PORTS_BIT_POS_1
#define AD7609_RANGE_PIN_MASK (0x1 << 1)

/*** Functions for AD7609_STBY pin ***/
#define AD7609_STBY_PORT PORT_CHANNEL_K
#define AD7609_STBY_PIN PORTS_BIT_POS_2
#define AD7609_STBY_PIN_MASK (0x1 << 2)

/*** Functions for AD7609_OS_1 pin ***/
#define AD7609_OS_1_PORT PORT_CHANNEL_K
#define AD7609_OS_1_PIN PORTS_BIT_POS_3
#define AD7609_OS_1_PIN_MASK (0x1 << 3)

/*** Functions for LED_BLUE pin ***/
#define LED_BLUE_PORT PORT_CHANNEL_B
#define LED_BLUE_PIN PORTS_BIT_POS_14
#define LED_BLUE_PIN_MASK (0x1 << 14)

/*** Functions for WIFI_WAKE pin ***/
#define WIFI_WAKE_PORT PORT_CHANNEL_H
#define WIFI_WAKE_PIN PORTS_BIT_POS_4
#define WIFI_WAKE_PIN_MASK (0x1 << 4)

/*** Functions for AD7609_OS_0 pin ***/
#define AD7609_OS_0_PORT PORT_CHANNEL_H
#define AD7609_OS_0_PIN PORTS_BIT_POS_7
#define AD7609_OS_0_PIN_MASK (0x1 << 7)

/*** Functions for TEMP_CS pin ***/
#define TEMP_CS_PORT PORT_CHANNEL_C
#define TEMP_CS_PIN PORTS_BIT_POS_15
#define TEMP_CS_PIN_MASK (0x1 << 15)

/*** Functions for WIFI_RESET pin ***/
#define WIFI_RESET_PORT PORT_CHANNEL_H
#define WIFI_RESET_PIN PORTS_BIT_POS_8
#define WIFI_RESET_PIN_MASK (0x1 << 8)

/*** Functions for USB_DP_MON pin ***/
#define USB_DP_MON_PORT PORT_CHANNEL_H
#define USB_DP_MON_PIN PORTS_BIT_POS_9
#define USB_DP_MON_PIN_MASK (0x1 << 9)

/*** Functions for USB_DN_MON pin ***/
#define USB_DN_MON_PORT PORT_CHANNEL_H
#define USB_DN_MON_PIN PORTS_BIT_POS_10
#define USB_DN_MON_PIN_MASK (0x1 << 10)

/*** Functions for BATT_MAN_STAT pin ***/
#define BATT_MAN_STAT_PORT PORT_CHANNEL_H
#define BATT_MAN_STAT_PIN PORTS_BIT_POS_11
#define BATT_MAN_STAT_PIN_MASK (0x1 << 11)

/*** Functions for BATT_MAN_INT pin ***/
#define BATT_MAN_INT_PORT PORT_CHANNEL_A
#define BATT_MAN_INT_PIN PORTS_BIT_POS_4
#define BATT_MAN_INT_PIN_MASK (0x1 << 4)

/*** Functions for WIFI_CS pin ***/
#define WIFI_CS_PORT PORT_CHANNEL_K
#define WIFI_CS_PIN PORTS_BIT_POS_4
#define WIFI_CS_PIN_MASK (0x1 << 4)

/*** Functions for BATT_MAN_OTG pin ***/
#define BATT_MAN_OTG_PORT PORT_CHANNEL_K
#define BATT_MAN_OTG_PIN PORTS_BIT_POS_5
#define BATT_MAN_OTG_PIN_MASK (0x1 << 5)

/*** Functions for PWR_5_5V_CUR_LIM pin ***/
#define PWR_5_5V_CUR_LIM_PORT PORT_CHANNEL_K
#define PWR_5_5V_CUR_LIM_PIN PORTS_BIT_POS_6
#define PWR_5_5V_CUR_LIM_PIN_MASK (0x1 << 6)

/*** Functions for I2C_EN1 pin ***/
#define I2C_EN1_PORT PORT_CHANNEL_A
#define I2C_EN1_PIN PORTS_BIT_POS_14
#define I2C_EN1_PIN_MASK (0x1 << 14)

/*** Functions for SD_CS pin ***/
#define SD_CS_PORT PORT_CHANNEL_D
#define SD_CS_PIN PORTS_BIT_POS_9
#define SD_CS_PIN_MASK (0x1 << 9)

/*** Functions for PWR_3_3V_EN pin ***/
#define PWR_3_3V_EN_PORT PORT_CHANNEL_H
#define PWR_3_3V_EN_PIN PORTS_BIT_POS_12
#define PWR_3_3V_EN_PIN_MASK (0x1 << 12)

/*** Functions for WIFI_EN pin ***/
#define WIFI_EN_PORT PORT_CHANNEL_H
#define WIFI_EN_PIN PORTS_BIT_POS_13
#define WIFI_EN_PIN_MASK (0x1 << 13)

/*** Functions for I2C_EN2 pin ***/
#define I2C_EN2_PORT PORT_CHANNEL_H
#define I2C_EN2_PIN PORTS_BIT_POS_14
#define I2C_EN2_PIN_MASK (0x1 << 14)

/*** Functions for PWR_12V_EN pin ***/
#define PWR_12V_EN_PORT PORT_CHANNEL_H
#define PWR_12V_EN_PIN PORTS_BIT_POS_15
#define PWR_12V_EN_PIN_MASK (0x1 << 15)

/*** Functions for PWR_5V_EN pin ***/
#define PWR_5V_EN_PORT PORT_CHANNEL_D
#define PWR_5V_EN_PIN PORTS_BIT_POS_0
#define PWR_5V_EN_PIN_MASK (0x1 << 0)

/*** Functions for DIO_0 pin ***/
#define DIO_0_PORT PORT_CHANNEL_D
#define DIO_0_PIN PORTS_BIT_POS_1
#define DIO_0_PIN_MASK (0x1 << 1)

/*** Functions for DIO_EN_0 pin ***/
#define DIO_EN_0_PORT PORT_CHANNEL_D
#define DIO_EN_0_PIN PORTS_BIT_POS_2
#define DIO_EN_0_PIN_MASK (0x1 << 2)

/*** Functions for DIO_2 pin ***/
#define DIO_2_PORT PORT_CHANNEL_D
#define DIO_2_PIN PORTS_BIT_POS_3
#define DIO_2_PIN_MASK (0x1 << 3)

/*** Functions for DIO_3 pin ***/
#define DIO_3_PORT PORT_CHANNEL_D
#define DIO_3_PIN PORTS_BIT_POS_12
#define DIO_3_PIN_MASK (0x1 << 12)

/*** Functions for DIO_EN_2 pin ***/
#define DIO_EN_2_PORT PORT_CHANNEL_D
#define DIO_EN_2_PIN PORTS_BIT_POS_13
#define DIO_EN_2_PIN_MASK (0x1 << 13)

/*** Functions for DIO_EN_3 pin ***/
#define DIO_EN_3_PORT PORT_CHANNEL_J
#define DIO_EN_3_PIN PORTS_BIT_POS_0
#define DIO_EN_3_PIN_MASK (0x1 << 0)

/*** Functions for DIO_EN_1 pin ***/
#define DIO_EN_1_PORT PORT_CHANNEL_J
#define DIO_EN_1_PIN PORTS_BIT_POS_2
#define DIO_EN_1_PIN_MASK (0x1 << 2)

/*** Functions for DIO_1 pin ***/
#define DIO_1_PORT PORT_CHANNEL_J
#define DIO_1_PIN PORTS_BIT_POS_3
#define DIO_1_PIN_MASK (0x1 << 3)

/*** Functions for DIO_EN_4 pin ***/
#define DIO_EN_4_PORT PORT_CHANNEL_D
#define DIO_EN_4_PIN PORTS_BIT_POS_7
#define DIO_EN_4_PIN_MASK (0x1 << 7)

/*** Functions for DIO_4 pin ***/
#define DIO_4_PORT PORT_CHANNEL_F
#define DIO_4_PIN PORTS_BIT_POS_0
#define DIO_4_PIN_MASK (0x1 << 0)

/*** Functions for DIO_5 pin ***/
#define DIO_5_PORT PORT_CHANNEL_F
#define DIO_5_PIN PORTS_BIT_POS_1
#define DIO_5_PIN_MASK (0x1 << 1)

/*** Functions for DIO_EN_5 pin ***/
#define DIO_EN_5_PORT PORT_CHANNEL_K
#define DIO_EN_5_PIN PORTS_BIT_POS_7
#define DIO_EN_5_PIN_MASK (0x1 << 7)

/*** Functions for DIO_7 pin ***/
#define DIO_7_PORT PORT_CHANNEL_G
#define DIO_7_PIN PORTS_BIT_POS_1
#define DIO_7_PIN_MASK (0x1 << 1)

/*** Functions for DIO_6 pin ***/
#define DIO_6_PORT PORT_CHANNEL_G
#define DIO_6_PIN PORTS_BIT_POS_0
#define DIO_6_PIN_MASK (0x1 << 0)

/*** Functions for DIO_EN_6 pin ***/
#define DIO_EN_6_PORT PORT_CHANNEL_J
#define DIO_EN_6_PIN PORTS_BIT_POS_4
#define DIO_EN_6_PIN_MASK (0x1 << 4)

/*** Functions for DIO_EN_7 pin ***/
#define DIO_EN_7_PORT PORT_CHANNEL_J
#define DIO_EN_7_PIN PORTS_BIT_POS_5
#define DIO_EN_7_PIN_MASK (0x1 << 5)

/*** Functions for DIO_8 pin ***/
#define DIO_8_PORT PORT_CHANNEL_J
#define DIO_8_PIN PORTS_BIT_POS_6
#define DIO_8_PIN_MASK (0x1 << 6)

/*** Functions for DIO_EN_8 pin ***/
#define DIO_EN_8_PORT PORT_CHANNEL_J
#define DIO_EN_8_PIN PORTS_BIT_POS_7
#define DIO_EN_8_PIN_MASK (0x1 << 7)

/*** Functions for DIO_EN_9 pin ***/
#define DIO_EN_9_PORT PORT_CHANNEL_E
#define DIO_EN_9_PIN PORTS_BIT_POS_0
#define DIO_EN_9_PIN_MASK (0x1 << 0)

/*** Functions for DIO_9 pin ***/
#define DIO_9_PORT PORT_CHANNEL_E
#define DIO_9_PIN PORTS_BIT_POS_1
#define DIO_9_PIN_MASK (0x1 << 1)

/*** Functions for DIO_EN_12 pin ***/
#define DIO_EN_12_PORT PORT_CHANNEL_E
#define DIO_EN_12_PIN PORTS_BIT_POS_2
#define DIO_EN_12_PIN_MASK (0x1 << 2)

/*** Functions for DIO_12 pin ***/
#define DIO_12_PORT PORT_CHANNEL_E
#define DIO_12_PIN PORTS_BIT_POS_3
#define DIO_12_PIN_MASK (0x1 << 3)

/*** Functions for DIO_10 pin ***/
#define DIO_10_PORT PORT_CHANNEL_E
#define DIO_10_PIN PORTS_BIT_POS_4
#define DIO_10_PIN_MASK (0x1 << 4)


/*** Application Instance 0 Configuration ***/

//DOM-IGNORE-BEGIN
#ifdef __cplusplus
}
#endif
//DOM-IGNORE-END

#endif // _SYSTEM_CONFIG_H
/*******************************************************************************
 End of File
*/
