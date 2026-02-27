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
    bool chgEn;  // Charge enable bit (not charging status)
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
    // Accumulated (sticky) faults — OR'd on every REG09 read, cleared explicitly
    bool watchdog_faultAccum;
    bool otg_faultAccum;
    enum eChargeFault chgFaultAccum;
    bool bat_faultAccum;
    enum eNTCFault ntcFaultAccum;
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

/*! @enum eIINLIM_State
 * @brief State machine for managing IINLIM after VBUS detection
 */
typedef enum {
    IINLIM_STATE_IDLE = 0,       // No VBUS, waiting
    IINLIM_STATE_WAIT_DPDM,      // VBUS appeared, waiting for DPDM to finish
    IINLIM_STATE_WAIT_USB,       // DPDM done, IINLIM=500mA, waiting 2s for USB enumeration
    IINLIM_STATE_SETTLED          // Final IINLIM set (500mA or 2000mA)
} eIINLIM_State;

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
    //! Interruption flag
    volatile bool intFlag;
    //! Indicate if charge is allowed
    bool chargeAllowed;
    //! Initialization completed
    bool initComplete;
    //! IINLIM state machine
    eIINLIM_State iinlimState;
    TickType_t iinlimTimestamp;
    bool iinlimLastVbus;
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
 * @param[in] pConfigInit Pointer to initial configuration of BQ24297 module
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
 * Updates BQ24297 status by reading all registers via I2C.
 * Called at boot, and on-demand by ForceDPDM, DisableOTG,
 * IsBatteryPresent, and UpdateBatteryStatus.
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

/*!
 * Enable OTG boost mode
 * @note This will disable charging as OTG and charging are mutually exclusive
 */
void BQ24297_EnableOTG(void);

/*!
 * Disable OTG boost mode and enable charging if battery present
 * @param[in] enableCharging If true, enable charging after disabling OTG
 */
void BQ24297_DisableOTG(bool enableCharging);

/*!
 * Get current OTG mode status
 * @return true if OTG is enabled, false otherwise
 */
bool BQ24297_IsOTGEnabled(void);

/*!
 * Get current charge enable status
 * @return true if charging is enabled, false otherwise
 */
bool BQ24297_IsChargingEnabled(void);

/*!
 * Switch power mode based on external power availability
 * @param[in] externalPowerPresent true if USB/charger connected, false if battery only
 */
void BQ24297_SetPowerMode(bool externalPowerPresent);

/*!
 * Read a BQ24297 register via I2C
 * @param[in] reg Register address to read
 * @return Register value or 0xFF on error
 */
uint8_t BQ24297_Read_I2C(uint8_t reg);

/*!
 * Write to a BQ24297 register via I2C
 * @param[in] reg Register address to write
 * @param[in] data Data to write to register
 * @return true on success, false on error
 */
bool BQ24297_Write_I2C(uint8_t reg, uint8_t data);

/*!
 * Check if battery is present
 * Consolidated battery detection logic that checks:
 * - Thermistor status (COLD = open circuit = no battery)
 * - Thermistor HOT (stuck low) with voltage check
 * - Normal thermistor indicates battery present
 * @return true if battery is present, false otherwise
 */
bool BQ24297_IsBatteryPresent(void);

/*!
 * Set the input current limit (IINLIM) on BQ24297
 * Also clears HIZ mode (REG00 bit 7) to ensure current flows from VBUS.
 * @param iinlimCode IINLIM code 0-7 (eILimit enum):
 *   0=100mA, 1=150mA, 2=500mA, 3=900mA, 4=1A, 5=1.5A, 6=2A, 7=3A
 * @return true if write succeeded, false on out-of-range or I2C error
 */
bool BQ24297_SetIINLIM(uint8_t iinlimCode);

/*!
 * Manage IINLIM state machine - call periodically from Power_Tasks
 * Handles VBUS detection, DPDM wait, USB enumeration check, and
 * sets appropriate IINLIM (500mA for USB host, 2000mA for wall charger)
 * @param[in] vbusPresent true if VBUS is currently detected
 */
void BQ24297_ManageIINLIM(bool vbusPresent);

/*!
 * Update battery status and charging allowed flag
 * Updates batPresent and chargeAllowed flags based on:
 * - Battery presence detection
 * - Thermistor fault status
 * Disables charging if thermistor is stuck low (HOT) for safety
 */
void BQ24297_UpdateBatteryStatus(void);

/*!
 * Read REG09 with proper double-read protocol and accumulate faults.
 * First read captures+clears latched faults, second read gets current.
 * Both reads feed into accumulated fault tracking.
 * @param[out] latched  Raw latched REG09 value (NULL to skip)
 * @param[out] current  Raw current REG09 value (NULL to skip)
 * @return true if at least one read succeeded
 */
bool BQ24297_ReadFaultReg(uint8_t *latched, uint8_t *current);

/*!
 * Clear all accumulated (sticky) fault fields.
 * Call after handling faults or on demand via SCPI.
 */
void BQ24297_ClearAccumulatedFaults(void);

#ifdef	__cplusplus
}
#endif
#endif /* BQ24297_H */