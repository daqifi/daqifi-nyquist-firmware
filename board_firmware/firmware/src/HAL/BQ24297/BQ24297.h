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
    enum inLim_t{ILim_100, ILim_150, ILim_500, ILim_900, ILim_1000, ILim_1500, ILim_2000, ILim_3000} inLim;
    
    // From power-on configuration 0x01
    bool otg;
    bool chg;
    
    // From status register 0x08
    enum vBusStat_t{VBUS_UNKNOWN, VBUS_USB, VBUS_CHARGER, VBUS_OTG} vBusStat;
    enum chgStat_t{CHG_STAT_NOCHARGE, CHG_STAT_PRECHARGE, CHG_STAT_FASTCHARGE, CHG_STAT_CHARGED} chgStat;
    bool dpm;
    bool pg;
    bool therm;
    bool vsys;
    
    // From fault register 0x09
    bool watchdog_fault;
    bool otg_fault;
    enum chgFault_t{CHG_FAULT_NORMAL, CHG_FAULT_INPUTFAULT, CHG_FAULT_THERMAL, CHG_FAULT_TIMER} chgFault;
    bool bat_fault;
    enum ntcFault_t{NTC_FAULT_NORMAL, NTC_FAULT_HOT, NTC_FAULT_COLD, NTC_FAULT_HOTCOLD} ntcFault;
} BQ24297_STATUS;
    
 typedef struct sBQ24297Config{
	PORTS_CHANNEL SDA_Ch;
	PORTS_BIT_POS SDA_Bit;
	PORTS_CHANNEL SCL_Ch; 
	PORTS_BIT_POS SCL_Bit;
	PORTS_CHANNEL OTG_Ch; 
	PORTS_BIT_POS OTG_Bit; 
	PORTS_CHANNEL CE_Ch; 
	PORTS_BIT_POS CE_Bit; 
	PORTS_CHANNEL INT_Ch; 
	PORTS_BIT_POS INT_Bit; 
	PORTS_CHANNEL STAT_Ch; 
	PORTS_BIT_POS STAT_Bit;
    SYS_MODULE_INDEX I2C_Index;
    unsigned char I2C_Address;
 } sBQ24297Config;

  typedef struct sBQ24297Data{
	unsigned char INT_Val;
	unsigned char STAT_Val;
    bool chargeAllowed;
    BQ24297_STATUS status;
    DRV_HANDLE I2C_Handle;
 } sBQ24297Data;
 
   typedef struct sBQ24297WriteVars{

	unsigned char OTG_Val;	// Input type selection (Low for USB port, High for ac-dc adapter)
    
 } sBQ24297WriteVars;
 
    /**
     * Initializes hardware
     */
    void BQ24297_InitHardware(sBQ24297Config config, sBQ24297WriteVars write, sBQ24297Data *data);
 
    /**
     * Sets the default variable values via I2C
     */
    void BQ24297_InitSettings(sBQ24297Config config, sBQ24297WriteVars write, sBQ24297Data *data);
    
    /**
    * Reads data from BQ24297.
    * TODO:
    * @param config
    * @param data 
    * @return
    */
    uint8_t BQ24297_Read_I2C(sBQ24297Config config, sBQ24297WriteVars write, sBQ24297Data data, uint8_t reg);
    
    /**
    * Writes data to the the BQ24297.
    * TODO:
    * @param config
    * @param write
    * @return
    */
    void BQ24297_Write_I2C(sBQ24297Config config, sBQ24297WriteVars write, sBQ24297Data data, uint8_t reg, uint8_t txData);
    
    void BQ24297_ChargeEnable(sBQ24297Config config, sBQ24297Data *data, sBQ24297WriteVars *write, bool chargeEnable, bool pONBattPresent);

    void BQ24297_UpdateStatus(sBQ24297Config config, sBQ24297WriteVars write, sBQ24297Data *data);

    
#ifdef	__cplusplus
}
#endif
#endif /* BQ24297_H */