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
#define SYS_CLK_FREQ                        200000000ul
#define SYS_CLK_BUS_PERIPHERAL_1            100000000ul
#define SYS_CLK_BUS_PERIPHERAL_2            100000000ul
#define SYS_CLK_BUS_PERIPHERAL_3            100000000ul
#define SYS_CLK_BUS_PERIPHERAL_4            100000000ul
#define SYS_CLK_BUS_PERIPHERAL_5            100000000ul
#define SYS_CLK_BUS_PERIPHERAL_7            200000000ul
#define SYS_CLK_BUS_PERIPHERAL_8            100000000ul
#define SYS_CLK_CONFIG_PRIMARY_XTAL         24000000ul
#define SYS_CLK_CONFIG_SECONDARY_XTAL       32768ul
#define SYS_CLK_CONFIG_FREQ_ERROR_LIMIT     10
#define SYS_CLK_WAIT_FOR_SWITCH             true
#define SYS_CLK_ON_WAIT                     OSC_ON_WAIT_IDLE 

#define SYS_CLK_DIV_PWR_SAVE    2       // Keep this for changing clock speeds   
/*** Ports System Service Configuration ***/
#define SYS_PORT_A_ANSEL        0x3F03
#define SYS_PORT_A_TRIS         0xBFDF
#define SYS_PORT_A_LAT          0x0020
#define SYS_PORT_A_ODC          0x0000
#define SYS_PORT_A_CNPU         0x8000
#define SYS_PORT_A_CNPD         0x0020
#define SYS_PORT_A_CNEN         0x0010

#define SYS_PORT_B_ANSEL        0x3DF0
#define SYS_PORT_B_TRIS         0xBDFD
#define SYS_PORT_B_LAT          0x0000
#define SYS_PORT_B_ODC          0x0000
#define SYS_PORT_B_CNPU         0x0000
#define SYS_PORT_B_CNPD         0x0008
#define SYS_PORT_B_CNEN         0x8008

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
#define SYS_PORT_F_CNEN         0x0004

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


/*** Console System Service Configuration ***/

#define SYS_CONSOLE_OVERRIDE_STDIO
#define SYS_CONSOLE_DEVICE_MAX_INSTANCES        2
#define SYS_CONSOLE_INSTANCES_NUMBER            1
#define SYS_CONSOLE_BUFFER_DMA_READY        __attribute__((coherent)) __attribute__((aligned(16)))

// DM 8/23/2016: APPIO isn't fully implemented. Doing this allows it to compile though
#define SYS_CONSOLE_APPIO_RD_QUEUE_DEPTH 1 
#define SYS_CONSOLE_APPIO_WR_QUEUE_DEPTH 128
    
#ifndef SYS_CONSOLE_PRINT
int LogMessage(const char* format, ...);
#define SYS_CONSOLE_MESSAGE(...) LogMessage(__VA_ARGS__) 
#define SYS_CONSOLE_PRINT(...) LogMessage(__VA_ARGS__)
#define SYS_DEBUG_MESSAGE(level, ...) LogMessage(__VA_ARGS__)
#define SYS_DEBUG_PRINT(level, ...) LogMessage(__VA_ARGS__)
#endif    

// DM 8/23/2016: END
/*** Debug System Service Configuration ***/
#define SYS_DEBUG_ENABLE
#define DEBUG_PRINT_BUFFER_SIZE       512
#define SYS_DEBUG_BUFFER_DMA_READY        __attribute__((coherent)) __attribute__((aligned(16)))
#define SYS_DEBUG_USE_CONSOLE

/*** File System Service Configuration ***/

#define SYS_FS_MEDIA_NUMBER         	1

#define SYS_FS_VOLUME_NUMBER		5

#define SYS_FS_AUTOMOUNT_ENABLE		false
#define SYS_FS_MAX_FILES	    	25
#define SYS_FS_MAX_FILE_SYSTEM_TYPE 	1
#define SYS_FS_MEDIA_MAX_BLOCK_SIZE  	2048
#define SYS_FS_MEDIA_MANAGER_BUFFER_SIZE 2048
#define SYS_FS_FILE_NAME_LEN 255
#define SYS_FS_CWD_STRING_LEN 1024


#define SYS_FS_MEDIA_TYPE_IDX0 				
#define SYS_FS_TYPE_IDX0 					










/*** Interrupt System Service Configuration ***/
#define SYS_INT                     true
// *****************************************************************************
/* Random System Service Configuration Options
*/

#define SYS_RANDOM_CRYPTO_SEED_SIZE  55

/*** Timer System Service Configuration ***/
#define SYS_TMR_POWER_STATE             SYS_MODULE_POWER_RUN_FULL
#define SYS_TMR_DRIVER_INDEX            DRV_TMR_INDEX_0
#define SYS_TMR_MAX_CLIENT_OBJECTS      5
#define SYS_TMR_FREQUENCY               1000
#define SYS_TMR_FREQUENCY_TOLERANCE     10
#define SYS_TMR_UNIT_RESOLUTION         10000
#define SYS_TMR_CLIENT_TOLERANCE        10
#define SYS_TMR_INTERRUPT_NOTIFICATION  true

// *****************************************************************************
// *****************************************************************************
// Section: Driver Configuration
// *****************************************************************************
// *****************************************************************************
#define DRV_FLASH_DRIVER_MODE_STATIC 
// *****************************************************************************
/* I2C Driver Configuration Options
*/
#define DRV_I2C_INTERRUPT_MODE                    		true
#define DRV_I2C_CLIENTS_NUMBER                    		1
#define DRV_I2C_INSTANCES_NUMBER                  		1

#define DRV_I2C_PERIPHERAL_ID_IDX0                		I2C_ID_5
#define DRV_I2C_OPERATION_MODE_IDX0               		DRV_I2C_MODE_MASTER
#define DRV_SCL_PORT_IDX0                               PORT_CHANNEL_F
#define DRV_SCL_PIN_POSITION_IDX0                       PORTS_BIT_POS_5
#define DRV_SDA_PORT_IDX0                               PORT_CHANNEL_F
#define DRV_SDA_PIN_POSITION_IDX0                       PORTS_BIT_POS_4
#define DRV_I2C_BIT_BANG_IDX0                           false
#define DRV_I2C_STOP_IN_IDLE_IDX0                       false
#define DRV_I2C_SMBus_SPECIFICATION_IDX0			    false
#define DRV_I2C_BAUD_RATE_IDX0                    		50000
#define DRV_I2C_BRG_CLOCK_IDX0	                  		100000000
#define DRV_I2C_SLEW_RATE_CONTROL_IDX0      			false
#define DRV_I2C_MASTER_INT_SRC_IDX0               		INT_SOURCE_I2C_5_MASTER
#define DRV_I2C_SLAVE_INT_SRC_IDX0                		
#define DRV_I2C_ERR_MZ_INT_SRC_IDX0               		INT_SOURCE_I2C_5_BUS
#define DRV_I2C_MASTER_INT_VECTOR_IDX0            		INT_VECTOR_I2C5_MASTER
#define DRV_I2C_MASTER_ISR_VECTOR_IDX0                  _I2C5_MASTER_VECTOR
#define DRV_I2C_MASTER_INT_PRIORITY_IDX0          		INT_PRIORITY_LEVEL1
#define DRV_I2C_MASTER_INT_SUB_PRIORITY_IDX0      		INT_SUBPRIORITY_LEVEL0
#define DRV_I2C_SLAVE_INT_VECTOR_IDX0             		INT_VECTOR_I2C5_SLAVE
#define DRV_I2C_SLAVE_ISR_VECTOR_IDX0			  	    _I2C5_SLAVE_VECTOR
#define DRV_I2C_SLAVE_INT_PRIORITY_IDX0           		
#define DRV_I2C_SLAVE_INT_SUB_PRIORITY_IDX0       		
#define DRV_I2C_ERR_INT_VECTOR_IDX0               		INT_VECTOR_I2C5_BUS
#define DRV_I2C_ERR_ISR_VECTOR_IDX0                     _I2C5_BUS_VECTOR
#define DRV_I2C_ERR_INT_PRIORITY_IDX0             		INT_PRIORITY_LEVEL1
#define DRV_I2C_ERR_INT_SUB_PRIORITY_IDX0         		INT_SUBPRIORITY_LEVEL0
#define DRV_I2C_POWER_STATE_IDX0                  		SYS_MODULE_POWER_RUN_FULL
#define DRV_I2C_INTERRUPT_MODE                    		true


/*** NVM Driver Configuration ***/
#define DRV_NVM_INSTANCES_NUMBER     	1
#define DRV_NVM_CLIENTS_NUMBER        	2
#define DRV_NVM_BUFFER_OBJECT_NUMBER  	5
#define DRV_NVM_INTERRUPT_MODE        	true
#define DRV_NVM_INTERRUPT_SOURCE      	INT_SOURCE_FLASH_CONTROL

#define DRV_NVM_MEDIA_SIZE              64
#define DRV_NVM_MEDIA_START_ADDRESS     0x9D020000

#define DRV_NVM_ERASE_WRITE_ENABLE


#define DRV_NVM_SYS_FS_REGISTER


#define DRV_OC_DRIVER_MODE_STATIC 

/*** SDCARD Driver Configuration ***/
#define DRV_SDCARD_INSTANCES_NUMBER     1
#define DRV_SDCARD_CLIENTS_NUMBER       1
#define DRV_SDCARD_INDEX_MAX            1
#define DRV_SDCARD_INDEX                DRV_SDCARD_INDEX_0
#define DRV_SDCARD_QUEUE_POOL_SIZE      10
#define DRV_SDCARD_SPI_DRV_INSTANCE     0

#define DRV_SDCARD_SYS_FS_REGISTER




/*** SPI Driver Configuration ***/
#define DRV_SPI_NUMBER_OF_MODULES		6
/*** Driver Compilation and static configuration options. ***/
/*** Select SPI compilation units.***/
#define DRV_SPI_POLLED 				0
#define DRV_SPI_ISR 				1
#define DRV_SPI_MASTER 				1
#define DRV_SPI_SLAVE 				0
#define DRV_SPI_RM 					0
#define DRV_SPI_EBM 				1
#define DRV_SPI_8BIT 				1
#define DRV_SPI_16BIT 				0
#define DRV_SPI_32BIT 				0
#define DRV_SPI_DMA 				1

/*** SPI Driver Static Allocation Options ***/
#define DRV_SPI_INSTANCES_NUMBER 		2
#define DRV_SPI_CLIENTS_NUMBER 			2
#define DRV_SPI_ELEMENTS_PER_QUEUE 		10
/*** SPI Driver DMA Options ***/
#define DRV_SPI_DMA_TXFER_SIZE 			512
#define DRV_SPI_DMA_DUMMY_BUFFER_SIZE 	512
/* SPI Driver Instance 0 Configuration */
#define DRV_SPI_SPI_ID_IDX0 				SPI_ID_4
#define DRV_SPI_TASK_MODE_IDX0 				DRV_SPI_TASK_MODE_ISR
#define DRV_SPI_SPI_MODE_IDX0				DRV_SPI_MODE_MASTER
#define DRV_SPI_ALLOW_IDLE_RUN_IDX0			false
#define DRV_SPI_SPI_PROTOCOL_TYPE_IDX0 		DRV_SPI_PROTOCOL_TYPE_STANDARD
#define DRV_SPI_COMM_WIDTH_IDX0 			SPI_COMMUNICATION_WIDTH_8BITS
#define DRV_SPI_CLOCK_SOURCE_IDX0 		    SPI_BAUD_RATE_PBCLK_CLOCK
#define DRV_SPI_SPI_CLOCK_IDX0 				CLK_BUS_PERIPHERAL_2
#define DRV_SPI_BAUD_RATE_IDX0 				20000000
#define DRV_SPI_BUFFER_TYPE_IDX0 			DRV_SPI_BUFFER_TYPE_ENHANCED
#define DRV_SPI_CLOCK_MODE_IDX0 			DRV_SPI_CLOCK_MODE_IDLE_LOW_EDGE_FALL
#define DRV_SPI_INPUT_PHASE_IDX0 			SPI_INPUT_SAMPLING_PHASE_AT_END
#define DRV_SPI_TRANSMIT_DUMMY_BYTE_VALUE_IDX0      0x00

#define DRV_SPI_TX_INT_SOURCE_IDX0 			INT_SOURCE_SPI_4_TRANSMIT
#define DRV_SPI_RX_INT_SOURCE_IDX0 			INT_SOURCE_SPI_4_RECEIVE
#define DRV_SPI_ERROR_INT_SOURCE_IDX0 		INT_SOURCE_SPI_4_ERROR
#define DRV_SPI_TX_INT_VECTOR_IDX0			INT_VECTOR_SPI4_TX
#define DRV_SPI_RX_INT_VECTOR_IDX0			INT_VECTOR_SPI4_RX
#define DRV_DRV_SPI_ERROR_INT_VECTOR_IDX0	INT_VECTOR_SPI4_FAULT
#define DRV_SPI_TX_INT_PRIORITY_IDX0 		INT_PRIORITY_LEVEL3
#define DRV_SPI_TX_INT_SUB_PRIORITY_IDX0 	INT_SUBPRIORITY_LEVEL0
#define DRV_SPI_RX_INT_PRIORITY_IDX0 		INT_PRIORITY_LEVEL3
#define DRV_SPI_RX_INT_SUB_PRIORITY_IDX0 	INT_SUBPRIORITY_LEVEL0
#define DRV_SPI_ERROR_INT_PRIORITY_IDX0 	INT_PRIORITY_LEVEL3
#define DRV_SPI_ERROR_INT_SUB_PRIORITY_IDX0 INT_SUBPRIORITY_LEVEL0
#define DRV_SPI_QUEUE_SIZE_IDX0 			10
#define DRV_SPI_RESERVED_JOB_IDX0 			1
#define DRV_SPI_TX_DMA_CHANNEL_IDX0 		DMA_CHANNEL_1
#define DRV_SPI_TX_DMA_THRESHOLD_IDX0 		16
#define DRV_SPI_RX_DMA_CHANNEL_IDX0 		DMA_CHANNEL_0
#define DRV_SPI_RX_DMA_THRESHOLD_IDX0 		16
/* SPI Driver Instance 1 Configuration */
#define DRV_SPI_SPI_ID_IDX1 				SPI_ID_6
#define DRV_SPI_TASK_MODE_IDX1 				DRV_SPI_TASK_MODE_ISR
#define DRV_SPI_SPI_MODE_IDX1				DRV_SPI_MODE_MASTER
#define DRV_SPI_ALLOW_IDLE_RUN_IDX1			false
#define DRV_SPI_SPI_PROTOCOL_TYPE_IDX1 		DRV_SPI_PROTOCOL_TYPE_STANDARD
#define DRV_SPI_COMM_WIDTH_IDX1 			SPI_COMMUNICATION_WIDTH_8BITS
#define DRV_SPI_CLOCK_SOURCE_IDX1 		    SPI_BAUD_RATE_PBCLK_CLOCK
#define DRV_SPI_SPI_CLOCK_IDX1 				CLK_BUS_PERIPHERAL_2
#define DRV_SPI_BAUD_RATE_IDX1 				1000000
#define DRV_SPI_BUFFER_TYPE_IDX1 			DRV_SPI_BUFFER_TYPE_ENHANCED
#define DRV_SPI_CLOCK_MODE_IDX1 			DRV_SPI_CLOCK_MODE_IDLE_LOW_EDGE_RISE
#define DRV_SPI_INPUT_PHASE_IDX1 			SPI_INPUT_SAMPLING_PHASE_IN_MIDDLE
#define DRV_SPI_TRANSMIT_DUMMY_BYTE_VALUE_IDX1      0xFF

#define DRV_SPI_TX_INT_SOURCE_IDX1 			INT_SOURCE_SPI_6_TRANSMIT
#define DRV_SPI_RX_INT_SOURCE_IDX1 			INT_SOURCE_SPI_6_RECEIVE
#define DRV_SPI_ERROR_INT_SOURCE_IDX1 		INT_SOURCE_SPI_6_ERROR
#define DRV_SPI_TX_INT_VECTOR_IDX1			INT_VECTOR_SPI6_TX
#define DRV_SPI_RX_INT_VECTOR_IDX1			INT_VECTOR_SPI6_RX
#define DRV_DRV_SPI_ERROR_INT_VECTOR_IDX1	INT_VECTOR_SPI6_FAULT
#define DRV_SPI_TX_INT_PRIORITY_IDX1 		INT_PRIORITY_LEVEL1
#define DRV_SPI_TX_INT_SUB_PRIORITY_IDX1 	INT_SUBPRIORITY_LEVEL0
#define DRV_SPI_RX_INT_PRIORITY_IDX1 		INT_PRIORITY_LEVEL1
#define DRV_SPI_RX_INT_SUB_PRIORITY_IDX1 	INT_SUBPRIORITY_LEVEL0
#define DRV_SPI_ERROR_INT_PRIORITY_IDX1 	INT_PRIORITY_LEVEL1
#define DRV_SPI_ERROR_INT_SUB_PRIORITY_IDX1 INT_SUBPRIORITY_LEVEL0
#define DRV_SPI_QUEUE_SIZE_IDX1 			10
#define DRV_SPI_RESERVED_JOB_IDX1 			1
/*** Timer Driver Configuration ***/
#define DRV_TMR_INTERRUPT_MODE             true
#define DRV_TMR_INSTANCES_NUMBER           4
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

#define DRV_TMR_PERIPHERAL_ID_IDX1          TMR_ID_4
#define DRV_TMR_INTERRUPT_SOURCE_IDX1       INT_SOURCE_TIMER_5
#define DRV_TMR_INTERRUPT_VECTOR_IDX1       INT_VECTOR_T5
#define DRV_TMR_ISR_VECTOR_IDX1             _TIMER_5_VECTOR
#define DRV_TMR_INTERRUPT_PRIORITY_IDX1     INT_PRIORITY_LEVEL3
#define DRV_TMR_INTERRUPT_SUB_PRIORITY_IDX1 INT_SUBPRIORITY_LEVEL0
#define DRV_TMR_CLOCK_SOURCE_IDX1           DRV_TMR_CLKSOURCE_INTERNAL
#define DRV_TMR_PRESCALE_IDX1               TMR_PRESCALE_VALUE_2
#define DRV_TMR_OPERATION_MODE_IDX1         DRV_TMR_OPERATION_MODE_32_BIT

#define DRV_TMR_ASYNC_WRITE_ENABLE_IDX1     false
#define DRV_TMR_POWER_STATE_IDX1            SYS_MODULE_POWER_RUN_FULL
/*** Timer Driver 2 Configuration ***/
#define DRV_TMR_PERIPHERAL_ID_IDX2          TMR_ID_6
#define DRV_TMR_INTERRUPT_SOURCE_IDX2       INT_SOURCE_TIMER_7
#define DRV_TMR_INTERRUPT_VECTOR_IDX2       INT_VECTOR_T7
#define DRV_TMR_ISR_VECTOR_IDX2             _TIMER_7_VECTOR
#define DRV_TMR_INTERRUPT_PRIORITY_IDX2     INT_PRIORITY_LEVEL1
#define DRV_TMR_INTERRUPT_SUB_PRIORITY_IDX2 INT_SUBPRIORITY_LEVEL0
#define DRV_TMR_CLOCK_SOURCE_IDX2           DRV_TMR_CLKSOURCE_INTERNAL
#define DRV_TMR_PRESCALE_IDX2               TMR_PRESCALE_VALUE_2
#define DRV_TMR_OPERATION_MODE_IDX2         DRV_TMR_OPERATION_MODE_32_BIT
#define DRV_TMR_ASYNC_WRITE_ENABLE_IDX2     false
#define DRV_TMR_POWER_STATE_IDX2            SYS_MODULE_POWER_RUN_FULL
/*** Timer Driver 3 Configuration ***/
#define DRV_TMR_PERIPHERAL_ID_IDX3          TMR_ID_3
#define DRV_TMR_INTERRUPT_SOURCE_IDX3       INT_SOURCE_TIMER_3
#define DRV_TMR_INTERRUPT_VECTOR_IDX3       INT_VECTOR_T3
#define DRV_TMR_ISR_VECTOR_IDX3             _TIMER_3_VECTOR
#define DRV_TMR_INTERRUPT_PRIORITY_IDX3     INT_PRIORITY_LEVEL4
#define DRV_TMR_INTERRUPT_SUB_PRIORITY_IDX3 INT_SUBPRIORITY_LEVEL0
#define DRV_TMR_CLOCK_SOURCE_IDX3           DRV_TMR_CLKSOURCE_INTERNAL
#define DRV_TMR_PRESCALE_IDX3               TMR_PRESCALE_VALUE_256
#define DRV_TMR_OPERATION_MODE_IDX3         DRV_TMR_OPERATION_MODE_16_BIT
#define DRV_TMR_ASYNC_WRITE_ENABLE_IDX3     false
#define DRV_TMR_POWER_STATE_IDX3            SYS_MODULE_POWER_RUN_FULL

 /*** Wi-Fi Driver Configuration ***/
#define WINC1500_INT_SOURCE INT_SOURCE_EXTERNAL_4
#define WINC1500_INT_VECTOR INT_VECTOR_INT4

#define WDRV_SPI_INDEX 0
#define WDRV_SPI_INSTANCE sysObj.spiObjectIdx0

#define WDRV_BOARD_TYPE WDRV_BD_TYPE_CUSTOM

#define WDRV_EXT_RTOS_TASK_SIZE 2048u
#define WDRV_EXT_RTOS_TASK_PRIORITY 2u

// I/O mappings for general control pins, including CHIP_EN, IRQN, RESET_N and SPI_SSN.
#define WDRV_CHIP_EN_PORT_CHANNEL   PORT_CHANNEL_H
#define WDRV_CHIP_EN_BIT_POS        13

#define WDRV_IRQN_PORT_CHANNEL      PORT_CHANNEL_D
#define WDRV_IRQN_BIT_POS           11

#define WDRV_RESET_N_PORT_CHANNEL   PORT_CHANNEL_H
#define WDRV_RESET_N_BIT_POS        8

#define WDRV_SPI_SSN_PORT_CHANNEL   PORT_CHANNEL_K
#define WDRV_SPI_SSN_BIT_POS        4

#define WDRV_DEFAULT_NETWORK_TYPE WDRV_NETWORK_TYPE_SOFT_AP
#define WDRV_DEFAULT_CHANNEL 6
#define WDRV_DEFAULT_SSID "DAQiFi"

#define WDRV_DEFAULT_SECURITY_MODE WDRV_SECURITY_OPEN
#define WDRV_DEFAULT_WEP_KEYS_40 "5AFB6C8E77" // default WEP40 key
#define WDRV_DEFAULT_WEP_KEYS_104 "90E96780C739409DA50034FCAA" // default WEP104 key
#define WDRV_DEFAULT_PSK_PHRASE "Microchip 802.11 Secret PSK Password" // default WPA-PSK or WPA2-PSK passphrase
#define WDRV_DEFAULT_WPS_PIN "12390212" // default WPS PIN

#define WDRV_DEFAULT_POWER_SAVE WDRV_FUNC_DISABLED

// *****************************************************************************
// *****************************************************************************
// Section: Middleware & Other Library Configuration
// *****************************************************************************
// *****************************************************************************
/*** Crypto Library Configuration ***/

#define WC_NO_HARDEN
#define MICROCHIP_MPLAB_HARMONY
#define HAVE_MCAPI
#define MICROCHIP_PIC32
#define NO_CERTS
#define NO_PWDBASED
#define NO_OLD_TLS
#define NO_AES
#define NO_ASN
#define NO_RSA

/* MPLAB Harmony Net Presentation Layer Definitions*/
#define NET_PRES_NUM_INSTANCE 1
#define NET_PRES_NUM_SOCKETS 10

/*** OSAL Configuration ***/
#define OSAL_USE_RTOS          9


// *****************************************************************************
// *****************************************************************************
// Section: TCPIP Stack Configuration
// *****************************************************************************
// *****************************************************************************
#define TCPIP_STACK_USE_IPV4
#define TCPIP_STACK_USE_TCP
#define TCPIP_STACK_USE_UDP

#define TCPIP_STACK_TICK_RATE		        		5
#define TCPIP_STACK_SECURE_PORT_ENTRIES             10

#define TCPIP_STACK_ALIAS_INTERFACE_SUPPORT   false

#define TCPIP_PACKET_LOG_ENABLE     0

/* TCP/IP stack event notification */
#define TCPIP_STACK_USE_EVENT_NOTIFICATION
#define TCPIP_STACK_USER_NOTIFICATION   false
#define TCPIP_STACK_DOWN_OPERATION   true
#define TCPIP_STACK_IF_UP_DOWN_OPERATION   true
#define TCPIP_STACK_MAC_DOWN_OPERATION  true
#define TCPIP_STACK_INTERFACE_CHANGE_SIGNALING   false
#define TCPIP_STACK_CONFIGURATION_SAVE_RESTORE   true
/*** TCPIP Heap Configuration ***/

#define TCPIP_STACK_USE_INTERNAL_HEAP
#define TCPIP_STACK_DRAM_SIZE                       39250
#define TCPIP_STACK_DRAM_RUN_LIMIT                  2048

#define TCPIP_STACK_MALLOC_FUNC                     malloc

#define TCPIP_STACK_CALLOC_FUNC                     calloc

#define TCPIP_STACK_FREE_FUNC                       free


#define TCPIP_STACK_DRAM_DEBUG_ENABLE

#define TCPIP_STACK_HEAP_USE_FLAGS                   TCPIP_STACK_HEAP_FLAG_ALLOC_UNCACHED

#define TCPIP_STACK_HEAP_USAGE_CONFIG                TCPIP_STACK_HEAP_USE_DEFAULT

#define TCPIP_STACK_SUPPORTED_HEAPS                  1

/*** ARP Configuration ***/
#define TCPIP_ARP_CACHE_ENTRIES                 		5
#define TCPIP_ARP_CACHE_DELETE_OLD		        	true
#define TCPIP_ARP_CACHE_SOLVED_ENTRY_TMO			1200
#define TCPIP_ARP_CACHE_PENDING_ENTRY_TMO			60
#define TCPIP_ARP_CACHE_PENDING_RETRY_TMO			2
#define TCPIP_ARP_CACHE_PERMANENT_QUOTA		    		50
#define TCPIP_ARP_CACHE_PURGE_THRESHOLD		    		75
#define TCPIP_ARP_CACHE_PURGE_QUANTA		    		1
#define TCPIP_ARP_CACHE_ENTRY_RETRIES		    		3
#define TCPIP_ARP_GRATUITOUS_PROBE_COUNT			1
#define TCPIP_ARP_TASK_PROCESS_RATE		        	2
#define TCPIP_ARP_PRIMARY_CACHE_ONLY		        	true

/*** Berkeley API Configuration ***/
#define TCPIP_STACK_USE_BERKELEY_API
#define MAX_BSD_SOCKETS 					4
#define TCPIP_STACK_USE_BERKELEY_API
/*** DHCP Configuration ***/
#define TCPIP_STACK_USE_DHCP_CLIENT
#define TCPIP_DHCP_TIMEOUT                          2
#define TCPIP_DHCP_TASK_TICK_RATE                   200
#define TCPIP_DHCP_HOST_NAME_SIZE                   20
#define TCPIP_DHCP_CLIENT_CONNECT_PORT              68
#define TCPIP_DHCP_SERVER_LISTEN_PORT               67
#define TCPIP_DHCP_CLIENT_ENABLED                   true

/*** DHCP Server Configuration ***/
#define TCPIP_STACK_USE_DHCP_SERVER
#define TCPIP_DHCPS_TASK_PROCESS_RATE                               200
#define TCPIP_DHCPS_LEASE_ENTRIES_DEFAULT                           15
#define TCPIP_DHCPS_LEASE_SOLVED_ENTRY_TMO                          1200
#define TCPIP_DHCPS_LEASE_REMOVED_BEFORE_ACK                        5
#define TCPIP_DHCP_SERVER_DELETE_OLD_ENTRIES                        true
#define TCPIP_DHCPS_LEASE_DURATION	TCPIP_DHCPS_LEASE_SOLVED_ENTRY_TMO

/*** DHCP Server Instance 0 Configuration ***/
#define TCPIP_DHCPS_DEFAULT_IP_ADDRESS_RANGE_START_IDX0             "192.168.1.100"

#define TCPIP_DHCPS_DEFAULT_SERVER_IP_ADDRESS_IDX0                  "192.168.1.1"

#define TCPIP_DHCPS_DEFAULT_SERVER_NETMASK_ADDRESS_IDX0             "255.255.255.0"

#define TCPIP_DHCPS_DEFAULT_SERVER_GATEWAY_ADDRESS_IDX0             "192.168.1.1"

#define TCPIP_DHCPS_DEFAULT_SERVER_PRIMARY_DNS_ADDRESS_IDX0         "192.168.1.1"

#define TCPIP_DHCPS_DEFAULT_SERVER_SECONDARY_DNS_ADDRESS_IDX0       "192.168.1.1"

#define TCPIP_DHCP_SERVER_INTERFACE_INDEX_IDX0                      0

#define TCPIP_DHCP_SERVER_POOL_ENABLED_IDX0                         true



/*** DNS Client Configuration ***/
#define TCPIP_STACK_USE_DNS
#define TCPIP_DNS_CLIENT_SERVER_TMO					60
#define TCPIP_DNS_CLIENT_TASK_PROCESS_RATE			200
#define TCPIP_DNS_CLIENT_CACHE_ENTRIES				5
#define TCPIP_DNS_CLIENT_CACHE_ENTRY_TMO			0
#define TCPIP_DNS_CLIENT_CACHE_PER_IPV4_ADDRESS		5
#define TCPIP_DNS_CLIENT_CACHE_PER_IPV6_ADDRESS		1
#define TCPIP_DNS_CLIENT_ADDRESS_TYPE			    IP_ADDRESS_TYPE_IPV4
#define TCPIP_DNS_CLIENT_CACHE_DEFAULT_TTL_VAL		1200
#define TCPIP_DNS_CLIENT_CACHE_UNSOLVED_ENTRY_TMO	10
#define TCPIP_DNS_CLIENT_LOOKUP_RETRY_TMO			5
#define TCPIP_DNS_CLIENT_MAX_HOSTNAME_LEN			32
#define TCPIP_DNS_CLIENT_MAX_SELECT_INTERFACES		4
#define TCPIP_DNS_CLIENT_DELETE_OLD_ENTRIES			true
#define TCPIP_DNS_CLIENT_USER_NOTIFICATION   false

/*** DNS Server Configuration ***/
#define TCPIP_STACK_USE_DNS_SERVER
#define TCPIP_DNSS_HOST_NAME_LEN		    		64
#define TCPIP_DNSS_REPLY_BOARD_ADDR				true
#define TCPIP_DNSS_CACHE_PER_IPV4_ADDRESS			2
#define TCPIP_DNSS_CACHE_PER_IPV6_ADDRESS			
#define TCPIP_DNSS_TTL_TIME							600
#define TCPIP_DNSS_TASK_PROCESS_RATE			    33
#define TCPIP_DNSS_DELETE_OLD_LEASE				true

/***Maximum DNS server Cache entries. It is the sum of TCPIP_DNSS_CACHE_PER_IPV4_ADDRESS and TCPIP_DNSS_CACHE_PER_IPV6_ADDRESS.***/
#define TCPIP_DNSS_CACHE_MAX_SERVER_ENTRIES     (TCPIP_DNSS_CACHE_PER_IPV4_ADDRESS+TCPIP_DNSS_CACHE_PER_IPV6_ADDRESS)


/*** HTTP Configuration ***/
#define TCPIP_STACK_USE_HTTP_SERVER
#define TCPIP_HTTP_MAX_HEADER_LEN		    		15
#define TCPIP_HTTP_CACHE_LEN		        		"600"
#define TCPIP_HTTP_TIMEOUT		            		45
#define TCPIP_HTTP_MAX_CONNECTIONS		    		4
#define TCPIP_HTTP_DEFAULT_FILE		        		"index.htm"
#define TCPIP_HTTPS_DEFAULT_FILE	        		"index.htm"
#define TCPIP_HTTP_DEFAULT_LEN		        		10
#define TCPIP_HTTP_MAX_DATA_LEN		        		100
#define TCPIP_HTTP_MIN_CALLBACK_FREE				16
#define TCPIP_HTTP_SKT_TX_BUFF_SIZE		    		0
#define TCPIP_HTTP_SKT_RX_BUFF_SIZE		    		0
#define TCPIP_HTTP_CONFIG_FLAGS		        		1
#define TCPIP_HTTP_USE_POST
#define TCPIP_HTTP_USE_COOKIES
#define TCPIP_HTTP_USE_BASE64_DECODE
#define TCPIP_HTTP_USE_AUTHENTICATION
#define TCPIP_HTTP_TASK_RATE					33
#define TCPIP_HTTP_MALLOC_FUNC                     0
#define TCPIP_HTTP_FREE_FUNC                        0

/*** ICMPv4 Server Configuration ***/
#define TCPIP_STACK_USE_ICMP_SERVER
#define TCPIP_ICMP_ECHO_ALLOW_BROADCASTS    false

/*** ICMPv4 Client Configuration ***/
#define TCPIP_STACK_USE_ICMP_CLIENT
#define TCPIP_ICMP_CLIENT_USER_NOTIFICATION   true
#define TCPIP_ICMP_ECHO_REQUEST_TIMEOUT       500
#define TCPIP_ICMP_TASK_TICK_RATE             33


/*** NBNS Configuration ***/
#define TCPIP_STACK_USE_NBNS
#define TCPIP_NBNS_TASK_TICK_RATE   110







/*** TCP Configuration ***/
#define TCPIP_TCP_MAX_SEG_SIZE_TX		        	1460
#define TCPIP_TCP_SOCKET_DEFAULT_TX_SIZE			512
#define TCPIP_TCP_SOCKET_DEFAULT_RX_SIZE			512
#define TCPIP_TCP_DYNAMIC_OPTIONS             			true
#define TCPIP_TCP_START_TIMEOUT_VAL		        	1000
#define TCPIP_TCP_DELAYED_ACK_TIMEOUT		    		100
#define TCPIP_TCP_FIN_WAIT_2_TIMEOUT		    		5000
#define TCPIP_TCP_KEEP_ALIVE_TIMEOUT		    		10000
#define TCPIP_TCP_CLOSE_WAIT_TIMEOUT		    		200
#define TCPIP_TCP_MAX_RETRIES		            		5
#define TCPIP_TCP_MAX_UNACKED_KEEP_ALIVES			6
#define TCPIP_TCP_MAX_SYN_RETRIES		        	2
#define TCPIP_TCP_AUTO_TRANSMIT_TIMEOUT_VAL			40
#define TCPIP_TCP_WINDOW_UPDATE_TIMEOUT_VAL			200
#define TCPIP_TCP_MAX_SOCKETS		                10
#define TCPIP_TCP_TASK_TICK_RATE		        	5
#define TCPIP_TCP_MSL_TIMEOUT		        	    0
#define TCPIP_TCP_QUIET_TIME		        	    0
#define TCPIP_TCP_COMMANDS   false

/*** announce Configuration ***/
#define TCPIP_STACK_USE_ANNOUNCE
#define TCPIP_ANNOUNCE_MAX_PAYLOAD 	512
#define TCPIP_ANNOUNCE_TASK_RATE    333
#define TCPIP_ANNOUNCE_NETWORK_DIRECTED_BCAST             			false




/*** UDP Configuration ***/
#define TCPIP_UDP_MAX_SOCKETS		                	10
#define TCPIP_UDP_SOCKET_DEFAULT_TX_SIZE		    	512
#define TCPIP_UDP_SOCKET_DEFAULT_TX_QUEUE_LIMIT    	 	3
#define TCPIP_UDP_SOCKET_DEFAULT_RX_QUEUE_LIMIT			5
#define TCPIP_UDP_USE_POOL_BUFFERS   false
#define TCPIP_UDP_USE_TX_CHECKSUM             			true
#define TCPIP_UDP_USE_RX_CHECKSUM             			true
#define TCPIP_UDP_COMMANDS   false




/*** IPv4 Configuration ***/

/*** Network Configuration Index 0 ***/
#define TCPIP_NETWORK_DEFAULT_INTERFACE_NAME_IDX0		"WINC1500"
#define TCPIP_IF_WINC1500
#define TCPIP_NETWORK_DEFAULT_HOST_NAME_IDX0				"NYQUIST"
#define TCPIP_NETWORK_DEFAULT_MAC_ADDR_IDX0				0
#define TCPIP_NETWORK_DEFAULT_IP_ADDRESS_IDX0			"0.0.0.0"
#define TCPIP_NETWORK_DEFAULT_IP_MASK_IDX0				"255.255.255.0"
#define TCPIP_NETWORK_DEFAULT_GATEWAY_IDX0				"192.168.1.1"
#define TCPIP_NETWORK_DEFAULT_DNS_IDX0					"192.168.1.1"
#define TCPIP_NETWORK_DEFAULT_SECOND_DNS_IDX0			"0.0.0.0"
#define TCPIP_NETWORK_DEFAULT_POWER_MODE_IDX0			"full"
#define TCPIP_NETWORK_DEFAULT_INTERFACE_FLAGS_IDX0			\
													TCPIP_NETWORK_CONFIG_DHCP_CLIENT_ON |\
													TCPIP_NETWORK_CONFIG_DNS_CLIENT_ON |\
													TCPIP_NETWORK_CONFIG_IP_STATIC
#define TCPIP_NETWORK_DEFAULT_MAC_DRIVER_IDX0			WDRV_WINC1500_MACObject
#define TCPIP_NETWORK_DEFAULT_IPV6_ADDRESS_IDX0			0
#define TCPIP_NETWORK_DEFAULT_IPV6_PREFIX_LENGTH_IDX0	0
#define TCPIP_NETWORK_DEFAULT_IPV6_GATEWAY_IDX0			0
/*** TCPIP SYS FS Wrapper ***/
#define SYS_FS_MAX_PATH						80
#define LOCAL_WEBSITE_PATH_FS				"/mnt/mchpSite1"
#define LOCAL_WEBSITE_PATH					"/mnt/mchpSite1/"
#define SYS_FS_DRIVE						"FLASH"
#define SYS_FS_NVM_VOL						"/dev/nvma1"
#define SYS_FS_FATFS_STRING					"FATFS"
#define SYS_FS_MPFS_STRING					"MPFS2"

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
#define DRV_USBHS_ENDPOINTS_NUMBER    3




/*** USB Device Stack Configuration ***/










/* The USB Device Layer will not initialize the USB Driver */
#define USB_DEVICE_DRIVER_INITIALIZE_EXPLICIT

/* Maximum device layer instances */
#define USB_DEVICE_INSTANCES_NUMBER     1

/* EP0 size in bytes */
#define USB_DEVICE_EP0_BUFFER_SIZE      64










/* Maximum instances of CDC function driver */
#define USB_DEVICE_CDC_INSTANCES_NUMBER     1










/* CDC Transfer Queue Size for both read and
   write. Applicable to all instances of the
   function driver */
#define USB_DEVICE_CDC_QUEUE_DEPTH_COMBINED 3





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

/**

#define DBG_DIO_0_TRIS         g_BoardRuntimeConfig.DIOChannels.Data[0].IsInput = false;\
                               g_BoardRuntimeConfig.DIOChannels.Data[0].IsReadOnly = false;
#define DBG_DIO_0_SET(x)       g_BoardRuntimeConfig.DIOChannels.Data[0].Value = x;\
                               DIO_WriteStateSingle(&g_BoardConfig.DIOChannels.Data[0], &g_BoardRuntimeConfig.DIOChannels.Data[0]);
#define DBG_DIO_0_TOG()        g_BoardRuntimeConfig.DIOChannels.Data[0].Value = !g_BoardRuntimeConfig.DIOChannels.Data[0].Value;\
                               DIO_WriteStateSingle(&g_BoardConfig.DIOChannels.Data[0], &g_BoardRuntimeConfig.DIOChannels.Data[0]);


#define DBG_DIO_1_TRIS         g_BoardRuntimeConfig.DIOChannels.Data[1].IsInput = false;\
                               g_BoardRuntimeConfig.DIOChannels.Data[1].IsReadOnly = false;
#define DBG_DIO_1_SET(x)       g_BoardRuntimeConfig.DIOChannels.Data[1].Value = x;\
                               DIO_WriteStateSingle(&g_BoardConfig.DIOChannels.Data[1], &g_BoardRuntimeConfig.DIOChannels.Data[1]);
#define DBG_DIO_1_TOG()        g_BoardRuntimeConfig.DIOChannels.Data[1].Value = !g_BoardRuntimeConfig.DIOChannels.Data[1].Value;\
                               DIO_WriteStateSingle(&g_BoardConfig.DIOChannels.Data[1], &g_BoardRuntimeConfig.DIOChannels.Data[1]);


#define DBG_DIO_2_TRIS         g_BoardRuntimeConfig.DIOChannels.Data[2].IsInput = false;\
                               g_BoardRuntimeConfig.DIOChannels.Data[2].IsReadOnly = false;
#define DBG_DIO_2_SET(x)       g_BoardRuntimeConfig.DIOChannels.Data[2].Value = x;\
                               DIO_WriteStateSingle(&g_BoardConfig.DIOChannels.Data[2], &g_BoardRuntimeConfig.DIOChannels.Data[2]);
#define DBG_DIO_2_TOG()        g_BoardRuntimeConfig.DIOChannels.Data[2].Value = !g_BoardRuntimeConfig.DIOChannels.Data[2].Value;\
                               DIO_WriteStateSingle(&g_BoardConfig.DIOChannels.Data[2], &g_BoardRuntimeConfig.DIOChannels.Data[2]);

#define DBG_DIO_3_TRIS         g_BoardRuntimeConfig.DIOChannels.Data[3].IsInput = false;\
                               g_BoardRuntimeConfig.DIOChannels.Data[3].IsReadOnly = false;
#define DBG_DIO_3_SET(x)       g_BoardRuntimeConfig.DIOChannels.Data[3].Value = x;\
                               DIO_WriteStateSingle(&g_BoardConfig.DIOChannels.Data[3], &g_BoardRuntimeConfig.DIOChannels.Data[3]);
#define DBG_DIO_3_TOG()        g_BoardRuntimeConfig.DIOChannels.Data[3].Value = !g_BoardRuntimeConfig.DIOChannels.Data[3].Value;\
                               DIO_WriteStateSingle(&g_BoardConfig.DIOChannels.Data[3], &g_BoardRuntimeConfig.DIOChannels.Data[3]);

#define DBG_DIO_4_TRIS         g_BoardRuntimeConfig.DIOChannels.Data[4].IsInput = false;\
                               g_BoardRuntimeConfig.DIOChannels.Data[4].IsReadOnly = false;
#define DBG_DIO_4_SET(x)       g_BoardRuntimeConfig.DIOChannels.Data[4].Value = x;\
                               DIO_WriteStateSingle(&g_BoardConfig.DIOChannels.Data[4], &g_BoardRuntimeConfig.DIOChannels.Data[4]);
#define DBG_DIO_4_TOG()        g_BoardRuntimeConfig.DIOChannels.Data[4].Value = !g_BoardRuntimeConfig.DIOChannels.Data[4].Value;\
                               DIO_WriteStateSingle(&g_BoardConfig.DIOChannels.Data[4], &g_BoardRuntimeConfig.DIOChannels.Data[4]);

#define DBG_DIO_5_TRIS         g_BoardRuntimeConfig.DIOChannels.Data[5].IsInput = false;\
                               g_BoardRuntimeConfig.DIOChannels.Data[5].IsReadOnly = false;
#define DBG_DIO_5_SET(x)       g_BoardRuntimeConfig.DIOChannels.Data[5].Value = x;\
                               DIO_WriteStateSingle(&g_BoardConfig.DIOChannels.Data[5], &g_BoardRuntimeConfig.DIOChannels.Data[5]);
#define DBG_DIO_5_TOG()        g_BoardRuntimeConfig.DIOChannels.Data[5].Value = !g_BoardRuntimeConfig.DIOChannels.Data[5].Value;\
                               DIO_WriteStateSingle(&g_BoardConfig.DIOChannels.Data[5], &g_BoardRuntimeConfig.DIOChannels.Data[5]);
**/

//DOM-IGNORE-BEGIN
#ifdef __cplusplus
}
#endif
//DOM-IGNORE-END

#endif // _SYSTEM_CONFIG_H
/*******************************************************************************
 End of File
*/
