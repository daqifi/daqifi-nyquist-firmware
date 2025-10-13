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

// Simple delay function using core timer (wrap-around safe, overflow-safe)
static void DAC7718_Delay_us(uint32_t microseconds) {
    uint32_t startCount = CORETIMER_CounterGet();
    // Use 64-bit arithmetic to prevent overflow for large microsecond values
    uint32_t ticks = (uint32_t)(((uint64_t)microseconds * CORETIMER_FrequencyGet()) / 1000000U);

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

uint32_t DAC7718_ReadWriteReg(uint8_t id, uint8_t RW, uint8_t Reg, uint16_t Data)
{
    uint32_t Com;
    uint32_t rdData = 0;
    uint8_t x;

    // Validate inputs
    if (RW > 1U) {
        return UINT32_MAX;
    }
    if (Reg > 31U) {
        return UINT32_MAX;
    }

    tDAC7718Config* config = DAC7718_GetConfig(id);
    if (config == NULL) {
        LOG_E("DAC7718_ReadWriteReg: invalid config id=%u", id);
        return UINT32_MAX;
    }

    // Assert CS (active low)
    GPIO_PinWrite(config->CS_Pin, false);

    // Build 24-bit command
    Com  = (uint32_t)(RW & 0x1U);
    Com <<= 7;
    Com |=  (uint32_t)(Reg & 0x1FU);
    Com <<= 12;
    Com |=  (uint32_t)(Data & 0x0FFFU);
    Com <<= 4;

    // Transmit 24-bit command (MSB first) with timeout protection
    for (x = 0U; x < 3U; x++) {
        uint32_t timeout = DAC7718_SPI_TIMEOUT;
        while (((SPI2STAT & _SPI2STAT_SPITBE_MASK) == 0U) && (--timeout > 0U)) { }
        if (timeout == 0U) {
            LOG_E("DAC7718_ReadWriteReg: TX buffer timeout on byte %u", x);
            GPIO_PinWrite(config->CS_Pin, true);
            return UINT32_MAX;
        }
        SPI2BUF = (uint8_t)((Com & 0x00FF0000UL) >> 16);
        Com <<= 8;

        timeout = DAC7718_SPI_TIMEOUT;
        while (((SPI2STAT & _SPI2STAT_SPIRBE_MASK) != 0U) && (--timeout > 0U)) { }
        if (timeout == 0U) {
            LOG_E("DAC7718_ReadWriteReg: RX buffer timeout on byte %u", x);
            GPIO_PinWrite(config->CS_Pin, true);
            return UINT32_MAX;
        }
        (void)SPI2BUF; // clear
    }

    // Wait for shift register to complete
    uint32_t timeout = DAC7718_SPI_TIMEOUT;
    while ((SPI2STATbits.SPIBUSY == 1) && (--timeout > 0U)) { }
    if (timeout == 0U) {
        LOG_E("DAC7718_ReadWriteReg: SPI busy timeout");
        GPIO_PinWrite(config->CS_Pin, true);
        return UINT32_MAX;
    }
    GPIO_PinWrite(config->CS_Pin, true);

    // Readback if requested
    if (RW == 1U) {
        Com = 0b000000001000000110100000U; // NOP to clock out data

        spi_txData[0] = (uint8_t)((Com >> 16) & 0xFFU);
        spi_txData[1] = (uint8_t)((Com >> 8)  & 0xFFU);
        spi_txData[2] = (uint8_t)( Com        & 0xFFU);

        GPIO_PinWrite(config->CS_Pin, false);
        for (uint8_t i = 0U; i < 3U; i++) {
            uint32_t to = DAC7718_SPI_TIMEOUT;
            while (((SPI2STAT & _SPI2STAT_SPITBE_MASK) == 0U) && (--to > 0U)) { }
            if (to == 0U) {
                LOG_E("DAC7718_ReadWriteReg: NOP TX timeout on byte %u", i);
                GPIO_PinWrite(config->CS_Pin, true);
                return UINT32_MAX;
            }
            SPI2BUF = spi_txData[i];

            to = DAC7718_SPI_TIMEOUT;
            while (((SPI2STAT & _SPI2STAT_SPIRBE_MASK) != 0U) && (--to > 0U)) { }
            if (to == 0U) {
                LOG_E("DAC7718_ReadWriteReg: NOP RX timeout on byte %u", i);
                GPIO_PinWrite(config->CS_Pin, true);
                return UINT32_MAX;
            }
            spi_rxData[i] = (uint8_t)SPI2BUF;
        }

        timeout = DAC7718_SPI_TIMEOUT;
        while ((SPI2STATbits.SPIBUSY == 1) && (--timeout > 0U)) { }
        if (timeout == 0U) {
            LOG_E("DAC7718_ReadWriteReg: NOP SPI busy timeout");
            GPIO_PinWrite(config->CS_Pin, true);
            return UINT32_MAX;
        }
        GPIO_PinWrite(config->CS_Pin, true);

        // Reconstruct received data
        rdData = (spi_rxData[0] << 16) | (spi_rxData[1] << 8) | spi_rxData[2];
    }

    // Only keep data bits (12-bit data is in bits 15:4)
    rdData = (rdData & 0b000000001111111111110000U) >> 4;
    return rdData;
}

void DAC7718_UpdateLatch(uint8_t id)
{
	// Write to configuration register with LD bit set to update all DAC outputs
	DAC7718_ReadWriteReg(id, 0, 0, 0b110000011000);
}

// SPI2 configuration is handled by MCC-generated initialization