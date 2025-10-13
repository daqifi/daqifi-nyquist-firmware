/* 
 * @file   DAC7718.c
 * @brief This file manages the DAC7718 module
 * 
 */

#include "DAC7718.h"
#include "peripheral/gpio/plib_gpio.h"
#include "peripheral/spi/spi_master/plib_spi2_master.h"
#include "peripheral/coretimer/plib_coretimer.h"
#include "Util/Logger.h"

// Simple delay function using core timer (wrap-around safe)
static void DAC7718_Delay_us(uint32_t microseconds) {
    uint32_t startCount = CORETIMER_CounterGet();
    uint32_t ticks = (microseconds * (CORETIMER_FrequencyGet() / 1000000U));

    // Use modular arithmetic to handle 32-bit timer wrap-around correctly
    while ((uint32_t)(CORETIMER_CounterGet() - startCount) < ticks) {
        // Wait for elapsed ticks
    }
}

//! Max number of configuration to DAC7718 module
#define MAX_DAC7718_CONFIG 1

//! SPI timeout in iterations (approximately 100k iterations = ~10ms at 200MHz)
#define DAC7718_SPI_TIMEOUT 100000

//! Buffer with DAC7718 configurations
static tDAC7718Config m_DAC7718Config[MAX_DAC7718_CONFIG];
//! Number of configuration 
static uint8_t m_DAC7718ConfigCount;

//! Static SPI buffers to avoid stack/cache issues
static uint8_t spi_txData[3] __attribute__((coherent, aligned(4)));
static uint8_t spi_rxData[3] __attribute__((coherent, aligned(4)));

/*!
* Resets the DAC7718.  Must be called after DAC7718_Init
* @param id Driver instance ID
*/
// static void DAC7718_Reset(uint8_t id);  // Not used - CLR tied to RST 

// SPI2 is configured by MCC - no separate configuration needed

void DAC7718_InitGlobal( void )
{
    memset(m_DAC7718Config, 0, MAX_DAC7718_CONFIG * sizeof(tDAC7718Config));
    m_DAC7718ConfigCount = 0;
}

uint8_t DAC7718_NewConfig(const tDAC7718Config *newDAC7718Config)
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
    
    if (config == NULL) {
        // Error - should not happen if NewConfig was called
        return;
    }
    
    // SPI2 is already initialized by MCC
    
	// Initialize GPIO pins - CS high, RST high for normal operation
    GPIO_PinWrite(config->CS_Pin, true);
    GPIO_PinWrite(config->RST_Pin, true);
    
	// Set GPIO pins as outputs
    GPIO_PinOutputEnable(config->CS_Pin);
    GPIO_PinOutputEnable(config->RST_Pin);

	// Hardware reset sequence: RST low pulse, then high
	// DAC7718 datasheet requires minimum 100ns reset pulse
	GPIO_PinWrite(config->RST_Pin, false);  // Assert reset
	DAC7718_Delay_us(1);  // 1us delay (10x minimum spec, guaranteed safe)
	GPIO_PinWrite(config->RST_Pin, true);   // De-assert reset
	
	// Configure DAC: GAIN-A=1, GAIN-B=1 for 4x gain (10V full scale)
	DAC7718_ReadWriteReg(id, 0, 0, 0b100000011000);
	
	// Update latch to apply configuration
	DAC7718_UpdateLatch(id);
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
	// Remove 4095 limit for configuration registers (they can be > 4095)
	// if(Data>4095) return(0);

    tDAC7718Config* config = DAC7718_GetConfig(id);
    
    // Assert CS (active low)
    GPIO_PinWrite(config->CS_Pin, false);

	// Build 24 bit command using original working logic
	Com = (RW & 0b1);
	Com = Com << 7;
	Com = Com | (Reg & 0b11111);
	Com = Com << 12;
	Com = Com | (Data & 0b111111111111);
	Com = Com << 4;
	
	// Transmit 24-bit command (MSB first) with timeout protection
	for(x=0; x<3; x++){
        uint32_t timeout;

        // Wait for transmit buffer empty
        timeout = DAC7718_SPI_TIMEOUT;
        while (((SPI2STAT & _SPI2STAT_SPITBE_MASK) == 0) && (--timeout > 0));
        if (timeout == 0) {
            LOG_E("DAC7718_ReadWriteReg: TX buffer timeout on byte %d", x);
            GPIO_PinWrite(config->CS_Pin, true);  // Deassert CS on error
            return 0;
        }

        // Send byte using original working method
		SPI2BUF = (Com & 0x00FF0000) >> 16;
		Com = Com << 8;

		// Wait for receive complete and clear buffer
        timeout = DAC7718_SPI_TIMEOUT;
		while (((SPI2STAT & _SPI2STAT_SPIRBE_MASK) != 0) && (--timeout > 0));
        if (timeout == 0) {
            LOG_E("DAC7718_ReadWriteReg: RX buffer timeout on byte %d", x);
            GPIO_PinWrite(config->CS_Pin, true);  // Deassert CS on error
            return 0;
        }
		(void)SPI2BUF;
	}

    // Wait for shift register to complete before deasserting CS
    // This ensures the last byte is fully transmitted
    uint32_t timeout = DAC7718_SPI_TIMEOUT;
    while ((SPI2STATbits.SPIBUSY == 1) && (--timeout > 0)) {
        // Busy-wait for completion
    }
    if (timeout == 0) {
        LOG_E("DAC7718_ReadWriteReg: SPI busy timeout");
        GPIO_PinWrite(config->CS_Pin, true);  // Deassert CS on error
        return 0;
    }

    // Deassert CS
	GPIO_PinWrite(config->CS_Pin, true);

	// Read data if user has signaled to do so with RW bit (MSB first)
	if (RW==1){
        // Send NOP command to get data out without changing any settings
		Com=0b000000001000000110100000;
        
        // Prepare NOP command bytes using static buffers
        spi_txData[0] = (Com >> 16) & 0xFF;
        spi_txData[1] = (Com >> 8) & 0xFF;
        spi_txData[2] = Com & 0xFF;
        
        // Assert CS
		GPIO_PinWrite(config->CS_Pin, false);
        
        // Send NOP and receive data using direct register access with timeout protection
        for (int i = 0; i < 3; i++) {
            uint32_t timeout;

            // Wait for transmit buffer to be ready
            timeout = DAC7718_SPI_TIMEOUT;
            while (((SPI2STAT & _SPI2STAT_SPITBE_MASK) == 0) && (--timeout > 0));
            if (timeout == 0) {
                LOG_E("DAC7718_ReadWriteReg: NOP TX timeout on byte %d", i);
                GPIO_PinWrite(config->CS_Pin, true);  // Deassert CS on error
                return 0;
            }

            // Send byte
            SPI2BUF = spi_txData[i];

            // Wait for receive buffer to have data
            timeout = DAC7718_SPI_TIMEOUT;
            while (((SPI2STAT & _SPI2STAT_SPIRBE_MASK) != 0) && (--timeout > 0));
            if (timeout == 0) {
                LOG_E("DAC7718_ReadWriteReg: NOP RX timeout on byte %d", i);
                GPIO_PinWrite(config->CS_Pin, true);  // Deassert CS on error
                return 0;
            }

            // Read response byte
            spi_rxData[i] = SPI2BUF;
        }

        // Wait for shift register to complete before deasserting CS
        // This ensures the last byte is fully received
        timeout = DAC7718_SPI_TIMEOUT;
        while ((SPI2STATbits.SPIBUSY == 1) && (--timeout > 0)) {
            // Busy-wait for completion
        }
        if (timeout == 0) {
            LOG_E("DAC7718_ReadWriteReg: NOP SPI busy timeout");
            GPIO_PinWrite(config->CS_Pin, true);  // Deassert CS on error
            return 0;
        }

        // Deassert CS
		GPIO_PinWrite(config->CS_Pin, true);
        
        // Reconstruct received data
        Data = (spi_rxData[0] << 16) | (spi_rxData[1] << 8) | spi_rxData[2];
	}
    
    // Only keep data bits (12-bit data is in bits 15:4)
	Data=(Data&0b000000001111111111110000)>>4;	
	return (Data);
}

void DAC7718_UpdateLatch(uint8_t id)
{
	// Write to configuration register with LD bit set to update all DAC outputs
	DAC7718_ReadWriteReg(id, 0, 0, 0b110000011000);
}

// SPI2 configuration is handled by MCC-generated initialization