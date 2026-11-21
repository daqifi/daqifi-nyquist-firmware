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
#include "configuration.h"
#include "definitions.h"
#include "HAL/Power/PowerApi.h"


/* Provide C++ Compatibility */
#ifdef __cplusplus
extern "C" {
#endif

#define MAX_PATTERNS 12
#define MAX_SEQ_NUM 8
    
/*! @struct sLEDIndication
 * @brief Data structure for LED indication
 * @typedef tLEDIndication
 * @brief Data type associated to the structure sLEDIndication
 */    
typedef struct sLEDIndication {
    const bool patterns[MAX_PATTERNS][MAX_SEQ_NUM];
    const uint32_t period[MAX_PATTERNS];
} tLEDIndication;

/*! @struct sUIConfig
 * @brief Data structure for user interface configuration
 * @typedef tUIConfig
 * @brief Data type associated to the structure sUIConfig
 */  
typedef struct sUIConfig {
   
    GPIO_PIN LED1_Pin;
   
    GPIO_PIN LED2_Pin;
   
    GPIO_PIN button_Pin;
    
    tLEDIndication LED1_Ind;
    tLEDIndication LED2_Ind;
} tUIConfig;

/*! @struct sUIReadVars
 * @brief Data structure for user interface read variables
 * @typedef tUIReadVars
 * @brief Data type associated to the structure sUIReadVars
 */ 
typedef struct sUIReadVars {
    bool LED1;
    bool LED2;
    bool button;
} tUIReadVars;

/*! @struct sUIWriteVars
 * @brief Data structure for user interface write variables
 * @typedef tUIWriteVars
 * @brief Data type associated to the structure sUIWriteVars
 */ 
typedef struct sUIWriteVars {
    bool LED1;
    bool LED2;
} tUIWriteVars;
    
/*! Function for initializing UI layer, in which button and LED tasks 
 * are included
 * @param[in] pConfigInit Pointer to the configuration data structure
 * @param[in] pReadVarsInit Pointer to the Read Variables data structure
 * @param[in] pPowerDataInit Pointer to the Power Data data stucture
 */
void UI_Init(                                                               
                tUIConfig *pConfigInit,                                     
                tUIReadVars *pReadVarsInit,                                 
                tPowerData *pPowerDataInit ); 
    
/*!
* @Function
*  void ButtonTasks(g_BoardConfig.UIConfig, *g_BoardData.UIReadVars)
*  @Summary
*   Function reads the button and determines any necessary actions
*  @Description
*   Function should be called periodically (~100ms) from a FreeRTOS task
*/
void Button_Tasks( void );
    
/*!
* @Function void LED_Tasks
* @Summary Function reads the board state and determines any necessary 
*          LED actions
* @Description Function should be called periodically (100ms) from a 
*          FreeRTOS task
* @param[in] streamingFlag  
*/
void LED_Tasks( bool streamingFlag);

/*!
* @Function void UI_SetIdentify
* @Summary Turns the identify pattern on or off
* @Description While on, LED_Tasks overrides every other LED pattern and
*          flashes both LEDs rapidly for ~1 s, then holds them off for ~1 s,
*          repeating, so an operator can see which physical board a host is
*          connected to. Driven by SYSTem:IDENtify. Not persisted; a reboot
*          turns it off.
* @param[in] enable true to start identifying, false to stop
*/
void UI_SetIdentify(bool enable);

/*!
* @Function bool UI_GetIdentify
* @Summary Returns whether the identify pattern is currently on
*/
bool UI_GetIdentify(void);

    /* Provide C++ Compatibility */
#ifdef __cplusplus
}
#endif

#endif /* _EXAMPLE_FILE_NAME_H */