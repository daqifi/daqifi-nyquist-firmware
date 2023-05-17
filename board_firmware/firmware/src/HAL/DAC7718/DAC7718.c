/* 
 * @file   DAC7718.c
 * @brief This file manages the DAC7718 module
 * 
 */

#include "DAC7718.h"

//! Max number of configuration to DAC7718 module
#define MAX_DAC7718_CONFIG 1

//! Buffer with DAC7718 configurations
static tDAC7718Config m_DAC7718Config[MAX_DAC7718_CONFIG];
//! Number of configuration 
static uint8_t m_DAC7718ConfigCount;

/*!
* Resets the DAC7718.  Must be called after DAC7718_Init
* @param id Driver instance ID
*/
static void DAC7718_Reset(uint8_t id); 

/*!
* Sets the SPI parameters and opens the SPI port
* @param id Driver instance ID
*/
static void DAC7718_Apply_SPI_Config(uint8_t id); 

void DAC7718_InitGlobal( void )
{
    memset(m_DAC7718Config, 0, MAX_DAC7718_CONFIG * sizeof(tDAC7718Config));
    m_DAC7718ConfigCount = 0;
}

uint8_t DAC7718_NewConfig(tDAC7718Config *newDAC7718Config)
{
    uint8_t id = m_DAC7718ConfigCount;
    ++m_DAC7718ConfigCount;
    
    memcpy(&m_DAC7718Config[id], newDAC7718Config, sizeof(tDAC7718Config));

    return id;
}

tDAC7718Config* DAC7718_GetConfig(uint8_t id)
{
    return &m_DAC7718Config[id];
}

void DAC7718_Init(uint8_t id, uint8_t range)
{
    tDAC7718Config* config = DAC7718_GetConfig(id);
    
    DAC7718_Apply_SPI_Config(id);
    
	// Initialize all associated ports to known value
    PLIB_PORTS_PinWrite(PORTS_ID_0, config->CS_Ch , config->CS_Bit, true);
    PLIB_PORTS_PinWrite(PORTS_ID_0, config->RST_Ch , config->RST_Bit, true);


	// Set port directions
    // NOTE: Directions are set in MHC


	// Send reset to DACXX18
	DAC7718_Reset(id);

	// Set gain=4xVREF
	DAC7718_ReadWriteReg(id, 0, 0, 0b100000011000);
}

uint32_t DAC7718_ReadWriteReg(                                              \
                        uint8_t id,                                         \
                        uint8_t RW,                                         \
                        uint8_t Reg,                                        \
                        uint32_t Data)
{
	uint32_t Com=0;
	uint8_t x = 0;

	if(RW>1) return(0);		// Return 0 if improper R/W mode selected
	if(Reg>31) return(0);		// Return 0 if improper channel selected
	if(Data>4095) return(0);

    tDAC7718Config* config = DAC7718_GetConfig(id);
    
    DAC7718_Apply_SPI_Config(id);
    //Enable CS_ADC
    PLIB_PORTS_PinWrite(PORTS_ID_0, config->CS_Ch , config->CS_Bit, false);

	// Build 24 bit command (12 bit version)
	Com=(RW&0b1);
	Com=Com<<7;
	Com=Com|(Reg&0b11111);
	Com=Com<<12;
	Com=Com|(Data&0b111111111111);	// Shift DataWidth bits
	Com=Com<<4;
	
	// Write 24 bit command (MSB first)
	for(x=0;x<3;x++){
        while(PLIB_SPI_IsBusy(config->SPI.spiID));
		PLIB_SPI_BufferWrite(config->SPI.spiID,(Com&0x00FF0000)>>16);
		Com=Com<<8;
		PLIB_SPI_BufferRead(config->SPI.spiID);	// Clear buffer
	}
    while(PLIB_SPI_IsBusy(config->SPI.spiID));
    //Disable CS_ADC
	PLIB_PORTS_PinWrite(PORTS_ID_0, config->CS_Ch , config->CS_Bit, true);	

	// Read data if user has signaled to do so with RW bit (MSB first)
	if (RW==1){
        // Send NOP command to get data out without changing any settings
		Com=0b000000001000000110100000;
        //Enable CS_ADC
		PLIB_PORTS_PinWrite(                                                \
                        PORTS_ID_0,                                         \
                        config->CS_Ch ,                                     \
                        config->CS_Bit,                                     \
                        false);	
		Data=0;
		for(x=0;x<3;x++){
            while(PLIB_SPI_IsBusy(config->SPI.spiID));
			Data = Data << 8;
			PLIB_SPI_BufferWrite(config->SPI.spiID,(Com&0x00FF0000)>>16);
			Com=Com<<8;
			Data |= (PLIB_SPI_BufferRead(config->SPI.spiID));
		}
		while(PLIB_SPI_IsBusy(config->SPI.spiID));
        //Disable CS_ADC
		PLIB_PORTS_PinWrite(                                                \
                        PORTS_ID_0,                                         \
                        config->CS_Ch ,                                     \
                        config->CS_Bit,                                     \
                        true);	
	}
    // Only keep data bits
	Data=(Data&0b000000001111111111110000)>>4;	
	return (Data);
}

void DAC7718_UpdateLatch(uint8_t id)
{
	// Set gain=4xVREF, and LD to update the latches
	DAC7718_ReadWriteReg(id, 0, 0, 0b110000011000);
}

/*!
* Resets the DAC7718.  Must be called after DAC7718_Init
* @param id Driver instance ID
*/
static void DAC7718_Reset(uint8_t id)
{
    tDAC7718Config* config = DAC7718_GetConfig(id);
    
	// Reset DAC7718 by pulling RST line high for > 100ns
	// this process requires CS line be high (if it is not already).
    PLIB_PORTS_PinWrite(                                                    \
                        PORTS_ID_0,                                         \
                        config->CS_Ch ,                                     \
                        config->CS_Bit,                                     \
                        true);
    PLIB_PORTS_PinWrite(PORTS_ID_0,                                         \
                        config->RST_Ch ,                                    \
                        config->RST_Bit,                                    \
                        false);
    asm("nop");
	asm("nop");
	asm("nop");
	asm("nop");
	asm("nop");
    asm("nop");
	asm("nop");
	asm("nop");
	asm("nop");
	asm("nop");
    asm("nop");
	asm("nop");
	asm("nop");
	asm("nop");
	asm("nop");
    PLIB_PORTS_PinWrite(PORTS_ID_0, config->RST_Ch , config->RST_Bit, true);
}

/*!
* Sets the SPI parameters and opens the SPI port
* @param id Driver instance ID
*/
static void DAC7718_Apply_SPI_Config(uint8_t id){
    tDAC7718Config* config = DAC7718_GetConfig(id);
    
    //Disable SPI
    PLIB_SPI_Disable(config->SPI.spiID);
    // Optional: Clear SPI interrupts and status flag.
    //clear SPI buffer
    PLIB_SPI_BufferClear (config->SPI.spiID);
    // Configure General SPI Options
    PLIB_SPI_StopInIdleDisable(config->SPI.spiID);
    PLIB_SPI_PinEnable(config->SPI.spiID, SPI_PIN_DATA_OUT|SPI_PIN_DATA_IN);
    PLIB_SPI_CommunicationWidthSelect(                                      \
                        config->SPI.spiID,                                  \
                        config->SPI.busWidth);
    PLIB_SPI_InputSamplePhaseSelect(                                        \
                        config->SPI.spiID,                                  \
                        config->SPI.inSamplePhase);
    PLIB_SPI_ClockPolaritySelect(                                           \
                        config->SPI.spiID,                                  \
                        config->SPI.clockPolarity);
    PLIB_SPI_OutputDataPhaseSelect(                                         \
                        config->SPI.spiID,                                  \
                        config->SPI.outDataPhase);
    
    PLIB_SPI_BaudRateClockSelect (config->SPI.spiID, config->SPI.clock);
    PLIB_SPI_BaudRateSet(                                                   \
                    config->SPI.spiID,                                      \
                    SYS_CLK_PeripheralFrequencyGet(config->SPI.busClk_id),  \
                    config->SPI.baud);
    PLIB_SPI_MasterEnable(config->SPI.spiID);
    PLIB_SPI_FramedCommunicationDisable(config->SPI.spiID);
    // Optional: Enable interrupts.
    // Enable the module
    PLIB_SPI_Enable(config->SPI.spiID);
    while(PLIB_SPI_IsBusy(config->SPI.spiID));
}