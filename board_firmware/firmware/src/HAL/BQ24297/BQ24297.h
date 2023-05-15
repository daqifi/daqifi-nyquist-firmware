/* 
 * File:   BQ24297.h
 * Author: Chris Lange
 *
 * Created on March 12, 2017, 6:45 PM
 */

#ifndef BQ24297_H
#define BQ24297_H

#include "system_config.h"
#include "system_definitions.h"

#ifdef	__cplusplus
extern "C" {
#endif
    

typedef struct
{
    // From control register 0x00
    bool hiZ;
    enum inLim_t{
        ILim_100, 
        ILim_150, 
        ILim_500, 
        ILim_900, 
        ILim_1000, 
        ILim_1500, 
        ILim_2000, 
        ILim_3000} 
    inLim;
    
    // From power-on configuration 0x01
    bool otg;
    bool chg;
    
    // From charger current control register 0x02
    uint8_t ichg;
    
    // From operation control register 0x07
    bool iinDet_Read;
    
    // From status register 0x08
    enum vBusStat_t{
        VBUS_UNKNOWN, 
        VBUS_USB,
        VBUS_CHARGER, 
        VBUS_OTG} 
    vBusStat;
    enum chgStat_t{
        CHG_STAT_NOCHARGE, 
        CHG_STAT_PRECHARGE, 
        CHG_STAT_FASTCHARGE, 
        CHG_STAT_CHARGED} 
    chgStat;
    bool dpmStat;
    bool pgStat;
    bool thermStat;
    bool vsysStat;
    
    // From fault register 0x09
    bool watchdog_fault;
    bool otg_fault;
    enum chgFault_t{
        CHG_FAULT_NORMAL, 
        CHG_FAULT_INPUTFAULT, 
        CHG_FAULT_THERMAL, 
        CHG_FAULT_TIMER} 
    chgFault;
    bool bat_fault;
    enum ntcFault_t{
        NTC_FAULT_NORMAL, 
        NTC_FAULT_HOT, 
        NTC_FAULT_COLD, 
        NTC_FAULT_HOTCOLD} 
    ntcFault;
    
    // Inferred battery status from registers
    bool batPresent;
} BQ24297_STATUS;
 
/*! @struct sBQ24297Config
 * @brief Data structure for the BQ24297 configuration
 * @typedef tBQ24297Config
 * @brief Data type associated to the structure sBQ24297Config
 */
 typedef struct sBQ24297Config{
    //! SDA channel port
	PORTS_CHANNEL SDA_Ch;
    //! SDA bit possition 
	PORTS_BIT_POS SDA_Bit;
    //! SCL channel port
	PORTS_CHANNEL SCL_Ch; 
    //! SCL bit possition
	PORTS_BIT_POS SCL_Bit;
    //! OTG channel port
	PORTS_CHANNEL OTG_Ch; 
    //! OTG bit possition
	PORTS_BIT_POS OTG_Bit;
    //! chip enable channel port
	PORTS_CHANNEL CE_Ch; 
    //! Chip enable bit possition
	PORTS_BIT_POS CE_Bit; 
    //! Interruption channel port
	PORTS_CHANNEL INT_Ch; 
    //! Interruption channel bit possition
	PORTS_BIT_POS INT_Bit; 
    //! Status port
	PORTS_CHANNEL STAT_Ch;
    //! Status bit possition
	PORTS_BIT_POS STAT_Bit;
    //! I2C index
    SYS_MODULE_INDEX I2C_Index;
    //! I2C address
    unsigned char I2C_Address;
 } tBQ24297Config;

 /*! @struct sBQ24297Data
 * @brief Data structure for the BQ24297 
 * @typedef tBQ24297Data
 * @brief Data type associated to the structure sBQ24297Data
 */
  typedef struct sBQ24297Data{
	unsigned char INT_Val;
	unsigned char STAT_Val;
    volatile bool intFlag;
    bool chargeAllowed;
    bool initComplete;
    BQ24297_STATUS status;
    DRV_HANDLE I2C_Handle;
 } tBQ24297Data;
 
 /*! @struct sBQ24297WriteVars
 * @brief Data structure for the BQ24297 write variables
 * @typedef tBQ24297WriteVars
 * @brief Data type associated to the structure sBQ24297WriteVars
 */
   typedef struct sBQ24297WriteVars{
    //! Input type selection (Low for USB port, High for ac-dc adapter)
	unsigned char OTG_Val;	
    //! USB port input current limit selection when SEL = Low. (Low = 100 mA, High = 500 mA)
	unsigned char CE_Val;	
 } tBQ24297WriteVars;
 
/*!
 * Initializes hardware
 * @param[in] pConfigInit Pointer to initial configuration of BQ23297 module
 * @param[in] pWriteInit Pointer to initial write variables data structure
 * @param[in] pDataInit Pointer to data structure of BQ24297 module
 */
void BQ24297_InitHardware(                                                  \
                    tBQ24297Config *pConfigInit,                            \
                    tBQ24297WriteVars *pWriteInit,                          \
                    tBQ24297Data *pDataInit );

/*!
 * Sets the default variable values via I2C
 */
void BQ24297_InitSettings( void );  
      
/*! 
 * Function to update status 
 */
void BQ24297_UpdateStatus();

/*!
 * Enable charge functions and save it in register
 */
void BQ24297_ChargeEnable(bool chargeEnable);
    
/*!
 * Force DPDM detection, using REG07
 */
void BQ24297_ForceDPDM( void );
    
/*!
 * Autosetting current limit
 */
void BQ24297_AutoSetILim( void );


    
#ifdef	__cplusplus
}
#endif
#endif /* BQ24297_H */