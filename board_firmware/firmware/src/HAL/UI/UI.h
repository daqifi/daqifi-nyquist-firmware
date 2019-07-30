/* ************************************************************************** */
/** UI.h

  @Company
    DAQifi

  @File Name
    UI.h

  @Summary
    This file contains user interface (LED and button) handling function headers.

  @Description
    Describe the purpose of this file.
 */
/* ************************************************************************** */

#ifndef _UI_H    /* Guard against multiple inclusion */
#define _UI_H


/* ************************************************************************** */
/* ************************************************************************** */
/* Section: Included Files                                                    */
/* ************************************************************************** */
/* ************************************************************************** */

/* This section lists the other files that are included in this file.
 */
#include "system_config.h"
#include "system_definitions.h"
#include <peripheral/peripheral.h>
#include "HAL/Power/PowerApi.h"


/* Provide C++ Compatibility */
#ifdef __cplusplus
extern "C" {
#endif

#define MAX_PATTERNS 12
#define MAX_SEQ_NUM 8
    
    
    typedef struct sLEDIndication {
        const bool patterns[MAX_PATTERNS][MAX_SEQ_NUM];
        const uint32_t period[MAX_PATTERNS];
    } sLEDIndication;
    
    typedef struct sUIConfig {
        PORTS_MODULE_ID LED1_Mod;
        PORTS_CHANNEL LED1_Ch;
        PORTS_BIT_POS LED1_Bit;
        PORTS_MODULE_ID LED2_Mod;
        PORTS_CHANNEL LED2_Ch;
        PORTS_BIT_POS LED2_Bit;
        PORTS_MODULE_ID button_Mod;
        PORTS_CHANNEL button_Ch;
        PORTS_BIT_POS button_Bit;
        sLEDIndication LED1_Ind;
        sLEDIndication LED2_Ind;

    } sUIConfig;
    
    typedef struct sUIReadVars {
        bool LED1;
        bool LED2;
        bool button;
    } sUIReadVars;
    
    typedef struct sUIWriteVars {
        bool LED1;
        bool LED2;
        bool enableLEDs;
    } sUIWriteVars;
    
    


    // *****************************************************************************
    // *****************************************************************************
    // Section: Interface Functions
    // *****************************************************************************
    // *****************************************************************************

    /*  A brief description of a section can be given directly below the section
        banner.
     */

    // *****************************************************************************
    /**
      @Function
        void ButtonTasks(g_BoardConfig.UIConfig, *g_BoardData.UIReadVars)

      @Summary
        Function reads the button and determines any necessary actions

      @Description
        Function should be called periodically (~100ms) from a FreeRTOS task

     */
    void Button_Tasks(sUIConfig config, sUIReadVars *UIReadVars, sPowerData *PowerData);
    
        /**
      @Function
        void LED_Tasks

      @Summary
        Function reads the board state and determines any necessary LED actions

      @Description
        Function should be called periodically (100ms) from a FreeRTOS task

     */
    void LED_Tasks(sUIConfig config, sPowerData *PowerData, sUIReadVars *UIReadVars, bool streamingFlag);

    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _EXAMPLE_FILE_NAME_H */

/* *****************************************************************************
 End of File
 */
