/* 
 * File:   DAC7718.h
 * Author: Chris Lange
 *
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
    
     /**
     * Configuration for a single SPI Port
     */
    typedef struct sSPIConfig
    {
        SPI_MODULE_ID spiID;
        uint32_t baud;
        SPI_BAUD_RATE_CLOCK clock;                                   //Selects the type of clock is used by the Baud Rate Generator.
        CLK_BUSES_PERIPHERAL busClk_id;

        SPI_CLOCK_POLARITY clockPolarity;                    //Enables clock polarity.
        SPI_COMMUNICATION_WIDTH busWidth;                   //Selects the data width for the SPI communication.

        SPI_INPUT_SAMPLING_PHASE inSamplePhase;        //Selects the SPI data input sample phase.

        SPI_OUTPUT_DATA_PHASE outDataPhase; //Selects serial output data change.
  
    } DAC7718_SPIConfig;
    
        
    /**
     * Configuration for a single DAC7718
     */
    typedef struct sDAC7718Config
    {
        uint8_t id;
                
        PORTS_CHANNEL RST_Ch;
        PORTS_BIT_POS RST_Bit;
        PORTS_CHANNEL CS_Ch;
        PORTS_BIT_POS CS_Bit;
        
        uint8_t DAC_Range;
        
        DAC7718_SPIConfig SPI;

    } DAC7718Config;

    /**
     * Initializes the internal GPIO data structures
     */
    void DAC7718_InitGlobal();
    
        /**
     * Creates a new configuration for the specified set of inputs and returns the id
     * @param channel
     * @param pin
     * @param mode
     * @return 
     */
    uint8_t DAC7718_NewConfig(DAC7718Config *newDAC7718Config);
    
    /**
     * Gets a handle to the config object with the specified id
     * @return 
     */
    DAC7718Config* DAC7718_GetConfig(uint8_t id);
    
    /**
    * Initializes the DAC7718.
    * TODO:
    * @param id Driver instance ID
    * @param range Range setting
    * @return
    */
    void DAC7718_Init(uint8_t id, uint8_t range);

    /**
    * Reads/Writes to a register in the DAC7718.
    * TODO:
    * @param id Driver instance ID
    * @param RW Read/Write Bit (W=0, R=1)
    * @param reg Register to read/write to DAC7718
    * @param data to write to DAC7718
    * @return
    */
    uint32_t DAC7718_ReadWriteReg(uint8_t id, uint8_t RW, uint8_t reg, uint32_t data);

    /**
    * Updates latches with values written to the DAC7718.
    * @param id Driver instance ID 
    * TODO:

    * @return
    */
    void DAC7718_UpdateLatch(uint8_t id);


#ifdef	__cplusplus
}
#endif

#endif	/* DAC7718_H */

