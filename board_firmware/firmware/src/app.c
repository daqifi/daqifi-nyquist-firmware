/*******************************************************************************
  File Name: app.c
 
  Description: This file initialice all modules and manage the tasks of the 
               system

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

#include <sys/attribs.h>
#include "streaming.h"
#include "HAL/DIO.h"
#include "HAL/ADC.h"
#include "HAL/Power/PowerApi.h"
#include "HAL/Wifi/WifiApi.h"
#include "HAL/NVM/DaqifiSettings.h"
#include "TCPServer/TcpServer.h"
#include "UsbCdc/UsbCdc.h"
#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "state/data/BoardData.h"
#include "commTest.h"

const char BOARD_HARDWARE_REV[16] = "2.0.0";
const char BOARD_FIRMWARE_REV[16] = "1.0.3";
#define BOARD_VARIANT       1

#define UNUSED(x) (void)(x)

/*! Declare force bootloader RAM flag location (FORCE_BOOTLOADER_FLAG_ADDR 
 *  defined in preprocessor directives as 0x8007FFFC) 0x8007FFFC is the last 
 * word of RAM - this value must match what is defined in the bootloader 
 *  project as well!
 */
extern volatile uint32_t force_bootloader_flag __attribute__((              \
                        persistent,                                         \
                        coherent,                                           \
                        address(FORCE_BOOTLOADER_FLAG_ADDR)));

//! Pointer to board data information 
static tBoardData * pBoardData;
//! Pointer to board configuration 
static tBoardConfig * pBoardConfig;
//! Pointer to board configuration in runtime
static tBoardRuntimeConfig * pBoardRuntimeConfig; 

/*!
 * @function runTimeStatsTimer89_init
 * @brief This routine initializes timer 8/9, which is a 32-bit hardware timer
 * to generate tick every 2.56us. The timer is used for the following 
 * purposes:
 * 1. FreeRTOS run time statistic counter. The freeRTOS run time statistics 
 * time base needs to have a higher resolution than the 1ms tick interrupt. 
 * Otherwise the statistics may be too inaccurate to be truly useful. The 
 * timer 8/9 tick is set to run ~400 time faster than the tick interrupt 
 * because the faster the time base the more accurate the statistics will be. 
 * However, the downside is the timer value will also overflow faster(~3 hours). 
 * This becomes a problem because there is no overflow protection in the FreeRTOS 
 * library function, so count values are only valid until the timer overflows. 
 * As a workaround, the timer 8/9 is programmed to interrupt every 60 mins 
 * to reset runtime counter for every task. 
 * 2. General purpose microseconds counter. Refer to osGetSystemTimeUs()
 * in os_port_freertos.c                                  
*/
void runTimeStatsTimer89_init(void)
{
    PLIB_TMR_Stop(TMR_ID_8);
    //timer is clocked by PBCLK3 at 100Mhz
    PLIB_TMR_ClockSourceSelect(TMR_ID_8, TMR_CLOCK_SOURCE_PERIPHERAL_CLOCK);
    //prescaler = 256 to generate 2.56us/tick
    PLIB_TMR_PrescaleSelect(TMR_ID_8, TMR_PRESCALE_VALUE_256);
    PLIB_TMR_Mode32BitEnable(TMR_ID_8);
    PLIB_TMR_Counter32BitClear(TMR_ID_8);
    // interrupt every ~ 1 hour. <-Do NOT change the interrupt frequency as
    // it will affect other applications that are using the timer, 
    // e.g Recorder library. 
    PLIB_TMR_Period32BitSet(TMR_ID_8, 0x50000000); 
   
    // Setup Interrupt  
    PLIB_INT_VectorPrioritySet(                                             \
                        INT_ID_0,                                           \
                        INT_VECTOR_T9,                                      \
                        INT_PRIORITY_LEVEL3);
    PLIB_INT_VectorSubPrioritySet(                                          \
                        INT_ID_0,                                           \
                        INT_VECTOR_T9,                                      \
                        INT_SUBPRIORITY_LEVEL3);
    //disable interrupt
    PLIB_INT_SourceDisable(INT_ID_0, INT_SOURCE_TIMER_9);
    PLIB_TMR_Start(TMR_ID_8);
}

/*! @function runTimeStatsTimer89_counter
 * @brief  This routine returns tick counter for FreeRTOS run time stats. 
 * @return The 32bit timer
 */
unsigned long runTimeStatsTimer89_counter(void)
{
    // The 32-bit timer will overflow every 10995 secs.
    return (TMR9 <<16 | TMR8); 
}

/*! @function void APP_Initialize(void)
 * @brief This function manage the system and modules initialization
 */
void APP_Initialize(void)
{
    DaqifiSettings tmpTopLevelSettings;    
    DaqifiSettings tmpWifiSettings;
    
    pBoardData = BoardData_Get(                                             \
                            BOARDDATA_ALL_DATA,                             \
                            0);
    
    pBoardConfig = BoardConfig_Get(                                         \
                            BOARDCONFIG_ALL_CONFIG,                         \
                            0);
    
    pBoardRuntimeConfig = BoardRunTimeConfig_Get(                           \
                            BOARDRUNTIMECONFIG_ALL_CONFIG);
    
    // Reset force bootloader flag
	force_bootloader_flag = 0;  
    
    // Initialize the variable to 0s
    memset(&tmpTopLevelSettings, 0, sizeof(tmpTopLevelSettings));
    tmpTopLevelSettings.type = DaqifiSettings_TopLevelSettings;
    
    // Try to load TopLevelSettings from NVM - if this fails, store default 
    // settings to NVM (first run after a program)
    if(!LoadNvmSettings(                                                    \
                        DaqifiSettings_TopLevelSettings,                    \
                        &tmpTopLevelSettings))
    {
        // Get board variant and cal param type from TopLevelSettings NVM 
        // variable
        LoadFactorySettings(                                                \
                        DaqifiSettings_TopLevelSettings,                    \
                        &tmpTopLevelSettings);
        strcpy(                                                             \
            tmpTopLevelSettings.settings.topLevelSettings.boardHardwareRev, \
                        BOARD_HARDWARE_REV);
        strcpy(                                                             \
            tmpTopLevelSettings.settings.topLevelSettings.boardFirmwareRev, \
                        BOARD_FIRMWARE_REV);
        tmpTopLevelSettings.settings.topLevelSettings.boardVariant =        \
                        BOARD_VARIANT;
        SaveNvmSettings(&tmpTopLevelSettings);
    }
    
    // Load board config structures with the correct board variant values
    InitBoardConfig(&tmpTopLevelSettings.settings.topLevelSettings);
    InitBoardRuntimeConfig(                                                 \
                tmpTopLevelSettings.settings.topLevelSettings.boardVariant);
    InitializeBoardData(pBoardData);
    
    // Try to load WiFiSettings from NVM - if this fails, store default 
    // settings to NVM (first run after a program)

    // Initialize the variable to 0s
    memset(&tmpWifiSettings, 0, sizeof(tmpWifiSettings));
    tmpWifiSettings.type = DaqifiSettings_Wifi;
    
    if(!LoadNvmSettings(DaqifiSettings_Wifi, &tmpWifiSettings))
    {
        // Get board wifi settings from Wifi NVM variable
        LoadFactorySettings(DaqifiSettings_Wifi, &tmpWifiSettings);
        SaveNvmSettings(&tmpWifiSettings);
    }
    // Move temp variable to global variables
    memcpy(             &pBoardRuntimeConfig->wifiSettings.settings.wifi,   \
                        &tmpWifiSettings.settings.wifi,                     \
                        sizeof(WifiSettings));
    memcpy(             &pBoardData->wifiSettings.settings.wifi,            \
                        &tmpWifiSettings.settings.wifi,                     \
                        sizeof(WifiSettings));
        
    // Load factory calibration parameters - if they are not initialized, 
    // store them (first run after a program)
    if(!LoadADCCalSettings(                                                 \
                        DaqifiSettings_FactAInCalParams,                    \
                        &pBoardRuntimeConfig->AInChannels)) 
    {
        SaveADCCalSettings(                                                 \
                        DaqifiSettings_FactAInCalParams,                    \
                        &pBoardRuntimeConfig->AInChannels);
    }
    // If calVals has been set to 1 (user cal params), overwrite with user 
    // calibration parameters
    if(tmpTopLevelSettings.settings.topLevelSettings.calVals)
    {
        LoadADCCalSettings(                                                 \
                        DaqifiSettings_UserAInCalParams,                    \
                        &pBoardRuntimeConfig->AInChannels);
    }
 	// Power initialization - enables 3.3V rail by default - other power 
    // functions are in power task
    Power_Init(             &pBoardConfig->PowerConfig,                     \
                            &pBoardData->PowerData,                         \
                            &pBoardRuntimeConfig->PowerWriteVars);
    
    UI_Init( &pBoardConfig->UIConfig,                                       \
             &pBoardData->UIReadVars,                                       \
             &pBoardData->PowerData );
    
    // Init DIO Hardware
    DIO_InitHardware(pBoardConfig, pBoardRuntimeConfig);
    
	// Write initial values
    DIO_WriteStateAll( );
   
    Streaming_Init(     &pBoardConfig->StreamingConfig,                     \
                        &pBoardRuntimeConfig->StreamingConfig);
    Streaming_UpdateState();   
    
    ADC_Init(                                                               \
                pBoardConfig,                                               \
                pBoardRuntimeConfig,                                        \
                pBoardData );
}

/*! This function manage the application tasks. 
 */
void APP_Tasks(void)
{   
    ADC_Tasks();
    
    Streaming_Tasks( pBoardRuntimeConfig, pBoardData);
    
    // Don't do anything else until the board powers on
    if (pBoardData->PowerData.powerState < POWERED_UP)
    {
        return;
    }

    WifiTasks();

}

