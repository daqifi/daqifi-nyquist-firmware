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
    typedef enum
     {  
        /* Powered down */
        POWER_DOWN = -1,
        /* 3.3V rail enabled. Ready to check initial status */
        MICRO_ON = 0,
        /* Board fully powered. Monitor for any changes/faults */ 
        POWERED_UP,
        /* Board partially powered. External power disabled */         
        POWERED_UP_EXT_DOWN,
     } POWER_STATE;
     
    typedef enum
     {      
        /* Board fully powered. */ 
        DO_POWER_UP,
        /* Board partially powered. External power disabled. */         
        DO_POWER_UP_EXT_DOWN,
        /* Power down. */
        DO_POWER_DOWN,
        /* No change.  This is the default status which allows power task to handle board power state.*/
        NO_CHANGE,
     } POWER_STATE_REQUEST;
     
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
       
       sBQ24297Config BQ24297Config;

    } sPowerConfig;

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
       
       sBQ24297Data BQ24297Data;

    } sPowerData;
    
    typedef struct sPowerWriteVars{

       unsigned char EN_Vref_Val;
       unsigned char EN_3_3V_Val;
       unsigned char EN_5_10V_Val;
       unsigned char EN_5V_ADC_Val;
       unsigned char EN_12V_Val;
       sBQ24297WriteVars BQ24297WriteVars;

    } sPowerWriteVars;
    
    void Power_Init( \
            sPowerConfig *pInitConfig, \
            sPowerData *pInitData, \
            sPowerWriteVars *pInitVars );
    void Power_Tasks( void );
    void Power_USB_Sleep_Update( bool sleep );
    
#ifdef	__cplusplus
}
#endif

#endif	/* POWERAPI_H */

