/* 
 * File:   PowerApi.h
 * Author: Daniel
 *
 * Created on January 11, 2017, 7:06 PM
 */

#ifndef POWERAPI_H
#define	POWERAPI_H

#include "configuration.h"
#include "definitions.h"
#include "HAL/BQ24297/BQ24297.h"

#ifdef	__cplusplus
extern "C" {
#endif

/*! @enum POWER_STATE
 * @brief Enumeration with the possible power states 
 */
typedef enum
 {  
    /* Standby/Off state - MCU on if USB powered, off if battery powered */
    STANDBY = 0,
    /* Board fully powered. Monitor for any changes/faults */ 
    POWERED_UP,
    /* Board partially powered. External power disabled (low battery mode) */         
    POWERED_UP_EXT_DOWN,
 } POWER_STATE;
 
 /*! @enum POWER_STATE_REQUEST
 * @brief Enumeration with the possible power states request
 */
typedef enum
{      
    /* No change.  This is the default status which allows power task to handle board power state.*/
    NO_CHANGE,
    /* Board fully powered. */ 
    DO_POWER_UP,
    /* Board partially powered. External power disabled. */         
    DO_POWER_UP_EXT_DOWN,
    /* Power down. */
    DO_POWER_DOWN
} POWER_STATE_REQUEST;

 /*! @enum EXT_POWER_SOURCE
 * @brief Enumeration with the possible power source
 */
typedef enum
{      
    /* No power detected. */ 
    NO_EXT_POWER,
    /* Unknown source */         
    UNKNOWN_EXT_POWER,
    /* 2 amp external charger */
    CHARGER_1A_EXT_POWER,
    /* 2 amp external charger */
    CHARGER_2A_EXT_POWER,
    /* 100mA USB power */
    USB_100MA_EXT_POWER,
    /* 500mA USB power */
    USB_500MA_EXT_POWER,
} EXT_POWER_SOURCE;
 
/*! @struct sPowerConfig
 * @brief Power configuration 
 */
typedef struct sPowerConfig{

    GPIO_PORT EN_Vref_Ch;
    GPIO_PIN EN_Vref_Bit;
    GPIO_PORT EN_3_3V_Ch; 
    GPIO_PIN EN_3_3V_Bit; 
    GPIO_PORT EN_5_10V_Ch; 
    GPIO_PIN EN_5_10V_Bit; 
    GPIO_PORT EN_12V_Ch; 
    GPIO_PIN EN_12V_Bit; 
    GPIO_PORT USB_Dp_Ch; 
    GPIO_PIN USB_Dp_Bit; 
    GPIO_PORT USB_Dn_Ch; 
    GPIO_PIN USB_Dn_Bit;

   tBQ24297Config BQ24297Config;

} tPowerConfig;

/*! @struct sPowerData
 * @brief Power data 
 */
typedef struct sPowerData{

    uint8_t chargePct;
    POWER_STATE powerState;
    POWER_STATE_REQUEST requestedPowerState;
    EXT_POWER_SOURCE externalPowerSource;

    // Variables below are meant to be updated externally
    bool USBSleep;
    bool battLow;
    bool shutdownNotified;  /* Set by UI task after LED warning shown */
    double battVoltage;
    bool pONBattPresent;
    bool autoExtPowerEnabled;  /* Auto-manage external power based on battery level (default: true) */

    tBQ24297Data BQ24297Data;

} tPowerData;

/*! @struct sPowerWriteVars
 * @brief Power write variables 
 */
typedef struct sPowerWriteVars
{
   unsigned char EN_Vref_Val;
   unsigned char EN_3_3V_Val;
   unsigned char EN_5_10V_Val;
   unsigned char EN_12V_Val;
   tBQ24297WriteVars BQ24297WriteVars;
} tPowerWriteVars;

/*! Initialice power 
 * @param[in] pInitConfig Pointer to power configuration
 * @param[in] pInitData   Pointer to power data 
 * @param[in] pInitVars   Pointer to write variables 
 */
 void Power_Init(                                                           
                        tPowerConfig *pInitConfig,                          
                        tPowerData *pInitData,                              
                        tPowerWriteVars *pInitVars); 
 
/*! This function manages the power task
 */
void Power_Tasks( void );

/*! Function to update USB sleep mode
 * @param[in] sleep Boolean to indicate if sleep mode is enable
 */
void Power_USB_Sleep_Update( bool sleep ); 
/*!
 * Writes the state 
 */
void Power_Write( void );
 
    
#ifdef	__cplusplus
}
#endif

#endif	/* POWERAPI_H */


