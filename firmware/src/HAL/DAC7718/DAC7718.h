/* 
 * @file   DAC7718.h
 * Author: Chris Lange
 * @brief This file manages the DAC7718 module
 * Created on July 15, 2016, 4:08 PM
 */
#ifndef DAC7718_H
#define	DAC7718_H

#include "configuration.h"
#include "definitions.h"
#include "state/board/AOutConfig.h"

#ifdef	__cplusplus
extern "C" {
#endif

// DAC7718 Hardware Constants
#define DAC7718_NUM_CHANNELS    8       // 8 output channels (0-7)
#define DAC7718_RESOLUTION      4096    // 12-bit resolution (4096 levels)
#define DAC7718_MAX_VALUE       4095    // Maximum DAC code (4096 - 1)

// DAC7718 Protocol Constants
#define DAC7718_MAX_REGISTER    31      // Maximum register address (5-bit address)
#define DAC7718_TRANSFER_BYTES  3       // 24-bit SPI protocol (3 bytes)
#define DAC7718_REGISTER_OFFSET 8       // DAC-0 register starts at address 8
#define DAC7718_READBACK_SHIFT  4       // Readback data is in bits 15:4

// Forward declaration - using board configuration structure from AOutConfig.h
typedef DAC7718ModuleConfig tDAC7718Config;

/*!
 * Initializes the internal GPIO data structures
 */
void DAC7718_InitGlobal( void );
    
/*!
 * Creates a new configuration for the specified DAC7718 module and 
 * returns the id
 * @param newDAC7718Config Pointer to DAC7718 configuration structure
 * @return Configuration ID
 */
uint8_t DAC7718_NewConfig(const tDAC7718Config *newDAC7718Config);
    
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
* @param Reg Register to read/write to DAC7718
* @param Data Data to write to DAC7718 (12-bit value)
* @return Read data on success, UINT32_MAX on error
*/
uint32_t DAC7718_ReadWriteReg(uint8_t id, uint8_t RW, uint8_t Reg, uint16_t Data); 

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

