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

    /* #564: tri-state. BATT_CHARGE_UNKNOWN (-1) until the VBATT ADC produces a
     * valid reading (it reads a stale 0 at cold boot); 0..100 once known. Signed
     * so UNKNOWN is distinguishable from a real 0% — naked `chargePct < THRESH`
     * comparisons must check known-first (chargePct >= 0). */
    int16_t chargePct;
    /* volatile: written by app_PowerAndUITask (pri 7), read across loop
     * iterations by app_WifiTask (pri 2) and app_SDCardTask (pri 5).
     * Function-call compiler-barriers in those loops currently force
     * re-loads each iteration at -O3, so this is defense-in-depth
     * against future inlining or refactoring. See #410. */
    volatile POWER_STATE powerState;
    volatile POWER_STATE_REQUEST requestedPowerState;
    EXT_POWER_SOURCE externalPowerSource;

    // Variables below are meant to be updated externally
    bool USBSleep;
    bool battLow;
    bool shutdownNotified;  /* Set by UI task after LED warning shown */
    double battVoltage;
    /* #564: false until the VBATT ADC has produced a real reading (>0.1V).
     * At cold boot the ADC may not be powered/sampled yet, so battVoltage
     * (and the chargePct derived from it) read a stale 0 — which must NOT be
     * treated as a dead battery.  chargePct-based demotions are gated on this;
     * the BQ24297 vsysStat (I2C, valid before the ADC) is the authoritative
     * critical-battery signal until the voltage measurement validates. */
    bool battVoltageValid;
    bool pONBattPresent;
    bool autoExtPowerEnabled;  /* Auto-manage external power based on battery level (default: true) */
    /* #454: Auto-transition STANDBY → POWERED_UP whenever VBUS (USB) is
     * present.  Default false (opt-in).  Loaded from NVM at boot
     * (TopLevelSettings.autoPowerOnUsb); persisted via SYST:POW:AUTOOn:SAVE. */
    bool autoPowerOnUsb;
    /* #454 tracking: have we already auto-promoted during the current
     * VBUS session?  Set when we issue the auto DO_POWER_UP request,
     * cleared when VBUS goes away.  Prevents re-promote after the user
     * manually returns to STANDBY (POW:STAT 0) while USB stays plugged
     * in. */
    bool autoPromotedThisVbusSession;

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


