/* 
 * @file   DAC7718.h
 * Author: Chris Lange
 * @brief This file manages the DAC7718 module
 * Created on July 15, 2016, 4:08 PM
 */
#ifndef DAC7718_H
#define	DAC7718_H

#include "system_config.h"
#include "system_definitions.h"
#include <peripheral/peripheral.h>

#ifdef	__cplusplus
extern "C" {
#endif
    
/*! @struct sDAC7718Config
 * @brief Data structure for a single SPI Port configuration
 * @typedef tDAC7718Config
 * @brief Data type associated to the structure sDAC7718Config
 */
typedef struct sSPIConfig
{
    //! SPI module ID
    SPI_MODULE_ID spiID;
    //! Baudrate
    uint32_t baud;
    //! Selects the type of clock is used by the Baud Rate Generator.
    SPI_BAUD_RATE_CLOCK clock;  
    //! Bus clock id
    CLK_BUSES_PERIPHERAL busClk_id;
    //! Enables clock polarity.
    SPI_CLOCK_POLARITY clockPolarity;
    //! Selects the data width for the SPI communication.
    SPI_COMMUNICATION_WIDTH busWidth;                   
    //! Selects the SPI data input sample phase.
    SPI_INPUT_SAMPLING_PHASE inSamplePhase;        
    //! Selects serial output data change.
    SPI_OUTPUT_DATA_PHASE outDataPhase; 

} tDAC7718_SPIConfig;
    
/*! @struct sDAC7718Config
 * @brief Data structure for a single DAC7718 configuration
 * @typedef tDAC7718Config
 * @brief Data type associated to the structure sDAC7718Config
 */
typedef struct sDAC7718Config
{
    //! Configuration ID
    uint8_t id;
    //! Reset port
    PORTS_CHANNEL RST_Ch;
    //! Reset bit possition
    PORTS_BIT_POS RST_Bit;
    //! CS port
    PORTS_CHANNEL CS_Ch;
    //! CS bit possition
    PORTS_BIT_POS CS_Bit;
    //! DAC range
    uint8_t DAC_Range;
    //! SPI configuration
    tDAC7718_SPIConfig SPI;

} tDAC7718Config;

/*!
 * Initializes the internal GPIO data structures
 */
void DAC7718_InitGlobal( void );
    
/*!
 * Creates a new configuration for the specified set of inputs and 
 * returns the id
 * @param channel
 * @param pin
 * @param mode
 * @return 
 */
uint8_t DAC7718_NewConfig(tDAC7718Config *newDAC7718Config);
    
/*!
 * Gets a handle to the config object with the specified id
 * @param id Configuration id
 * @return 
 */
tDAC7718Config* DAC7718_GetConfig(uint8_t id);

/*1
* Initializes the DAC7718.
* @param id Driver instance ID
* @param range Range setting
* @return
*/
void DAC7718_Init(uint8_t id, uint8_t range);

/*!
* Reads/Writes to a register in the DAC7718.
* @param id Driver instance ID
* @param RW Read/Write Bit (W=0, R=1)
* @param reg Register to read/write to DAC7718
* @param data to write to DAC7718
* @return
*/
uint32_t DAC7718_ReadWriteReg(                                              \
                        uint8_t id,                                         \
                        uint8_t RW,                                         \
                        uint8_t Reg,                                        \
                        uint32_t Data); 

/*!
* Updates latches with values written to the DAC7718.
* @param id Driver instance ID 
* @return
*/
void DAC7718_UpdateLatch(uint8_t id);


#ifdef	__cplusplus
}
#endif

#endif	/* DAC7718_H */

