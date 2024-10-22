/* 
 * File:   BQ24297.h
 * Author: Chris Lange
 *
 * Created on March 12, 2017, 6:45 PM
 */

#ifndef BQ24297_H
#define BQ24297_H

#include "configuration.h"
#include "definitions.h"

#ifdef	__cplusplus
extern "C" {
#endif
    
/*! @enum eILimit
 * @brief Enumeration for input limit 
 */
enum eILimit{ 
    //! Limit 100
    ILim_100, 
    //! Limit 150
    ILim_150, 
    //! Limit 500
    ILim_500, 
    //! Limit 900
    ILim_900, 
    //! Limit 1000
    ILim_1000, 
    //! Limit 1500
    ILim_1500, 
    //! Limit 2000
    ILim_2000,
    //! Limit 3000 
    ILim_3000
};

/*! @enum eBusStatus
 * @brief Enumeration for bus status
 */
enum eBusStatus{
    //!Bus unknown
    VBUS_UNKNOWN,
    //! Bus USB
    VBUS_USB,
    //! Bus charger
    VBUS_CHARGER, 
    //! Bus OTG module
    VBUS_OTG
};

/*! @enum eChargeStatus
 * @brief Enumeration for charge status
 */
enum eChargeStatus{
    //! No charge
    CHG_STAT_NOCHARGE,
    //! Precharge
    CHG_STAT_PRECHARGE, 
    //! Fast charge
    CHG_STAT_FASTCHARGE,
    //! Stat charged
    CHG_STAT_CHARGED
};

/*! @enum eChargeStatus
 * @brief Enumeration for charge fault
 */
enum eChargeFault{
    //! No fail
    CHG_FAULT_NORMAL, 
    //! Fault in input
    CHG_FAULT_INPUTFAULT, 
    //! Thermal error
    CHG_FAULT_THERMAL,
    //! Timer error
    CHG_FAULT_TIMER
};

/*! @enum eNTCFault
 * @brief Enumeration for NTC fault
 */
enum eNTCFault{
    //! NTC works normal
    NTC_FAULT_NORMAL,
    //! Hot error
    NTC_FAULT_HOT, 
    //! Cold error
    NTC_FAULT_COLD, 
    //! Hot cold fault
    NTC_FAULT_HOTCOLD
}eNTCFault;

typedef struct
{
    //! From control register 0x00
    bool hiZ;
    //! Input limit
    enum eILimit inLim;
    //! From power-on configuration 0x01
    bool otg;
    bool chg;
    //! From charger current control register 0x02
    uint8_t ichg;
    // From operation control register 0x07
    bool iinDet_Read;
    // From status register 0x08
    //! Bus status
    enum eBusStatus vBusStat;
    //! Charge status
    enum eChargeStatus chgStat;
    bool dpmStat;
    bool pgStat;
    bool thermStat;
    bool vsysStat;
    // From fault register 0x09
    bool watchdog_fault;
    bool otg_fault;
    //! Charge error
    enum eChargeFault chgFault;
    bool bat_fault;
    //! NTC Fault code
    enum eNTCFault ntcFault;
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
	GPIO_PORT SDA_Ch;
    //! SDA bit possition 
	GPIO_PIN SDA_Bit;
    //! SCL channel port
	GPIO_PORT SCL_Ch; 
    //! SCL bit possition
	GPIO_PIN SCL_Bit;
    //! OTG channel port
	GPIO_PORT OTG_Ch; 
    //! OTG bit possition
	GPIO_PIN OTG_Bit;
    //! chip enable channel port
	GPIO_PORT CE_Ch; 
    //! Chip enable bit possition
	GPIO_PIN CE_Bit; 
    //! Interruption channel port
	GPIO_PORT INT_Ch; 
    //! Interruption channel bit possition
	GPIO_PIN INT_Bit; 
    //! Status port
	GPIO_PORT STAT_Ch;
    //! Status bit possition
	GPIO_PIN STAT_Bit;
    //! I2C index
    SYS_MODULE_INDEX I2C_Index;
    //! I2C address
    uint16_t I2C_Address;
 } tBQ24297Config;

 /*! @struct sBQ24297Data
 * @brief Data structure for the BQ24297 
 * @typedef tBQ24297Data
 * @brief Data type associated to the structure sBQ24297Data
 */
  typedef struct sBQ24297Data{
    //! Interruption value
	unsigned char INT_Val;
    //! Status value
	unsigned char STAT_Val;
    //! Interruption falg
    volatile bool intFlag;
    //! Indicate if charge is allowed
    bool chargeAllowed;
    //! Initialitaion completed
    bool initComplete;
    //! Current status of the module
    BQ24297_STATUS status;
    //I2C handler to this module
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
void BQ24297_Config_Settings( void );  
      
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