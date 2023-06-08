/* 
 * File:   PowerApi.h
 * Author: Daniel
 *
 * Created on January 11, 2017, 7:06 PM
 */

#ifndef POWERAPI_H
#define	POWERAPI_H

#include "system_config.h"
#include "system_definitions.h"
#include "HAL/BQ24297/BQ24297.h"

#ifdef	__cplusplus
extern "C" {
#endif

/*! @enum POWER_STATE
 * @brief Enumeration with the possible power states 
 */
typedef enum
 {  
    /* Powered down */
    POWERED_DOWN = 0,
    /* Power was just applied */
    FRESH_BOOT,
    /* 3.3V rail enabled. Ready to check initial status */
    MICRO_ON,
    /* Board fully powered. Monitor for any changes/faults */ 
    POWERED_UP,
    /* Board partially powered. External power disabled */         
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

    PORTS_CHANNEL EN_Vref_Ch;
    PORTS_BIT_POS EN_Vref_Bit;
    PORTS_CHANNEL EN_3_3V_Ch; 
    PORTS_BIT_POS EN_3_3V_Bit; 
    PORTS_CHANNEL EN_5_10V_Ch; 
    PORTS_BIT_POS EN_5_10V_Bit; 
    PORTS_CHANNEL EN_5V_ADC_Ch; 
    PORTS_BIT_POS EN_5V_ADC_Bit; 
    PORTS_CHANNEL EN_12V_Ch; 
    PORTS_BIT_POS EN_12V_Bit; 
    PORTS_CHANNEL USB_Dp_Ch; 
    PORTS_BIT_POS USB_Dp_Bit; 
    PORTS_CHANNEL USB_Dn_Ch; 
    PORTS_BIT_POS USB_Dn_Bit;

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
    bool powerDnAllowed;
    double battVoltage;
    bool pONBattPresent;

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
   unsigned char EN_5V_ADC_Val;
   unsigned char EN_12V_Val;
   tBQ24297WriteVars BQ24297WriteVars;
} tPowerWriteVars;

/*! Initialice power 
 * @param[in] pInitConfig Pointer to power configuration
 * @param[in] pInitData   Pointer to power data 
 * @param[in] pInitVars   Pointer to write variables 
 */
 void Power_Init(                                                           \
                        tPowerConfig *pInitConfig,                          \
                        tPowerData *pInitData,                              \
                        tPowerWriteVars *pInitVars); 
 
/*! This function manages the power task
 */
void Power_Tasks( void );

/*! Function to update USB sleep mode
 * @param[in] sleep Boolean to indicate if sleep mode is enable
 */
void Power_USB_Sleep_Update( bool sleep ); 
 
    
#ifdef	__cplusplus
}
#endif

#endif	/* POWERAPI_H */


