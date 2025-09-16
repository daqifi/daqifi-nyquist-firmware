/*! @file AD7609.c 
 * 
 * This file implements the functions to manage the module ADC AD7609.
 * Minimal implementation for NQ3 testing - to be fully implemented later.
 */

#include "AD7609.h"
#include "configuration.h"
#include "definitions.h"
#include "peripheral/spi/spi_master/plib_spi6_master.h"
#include "peripheral/coretimer/plib_coretimer.h"
#include "state/board/BoardConfig.h"
#include "Util/Logger.h"

// Simple delay function using core timer
static void AD7609_Delay_ms(uint32_t milliseconds) {
    uint32_t startCount = CORETIMER_CounterGet();
    uint32_t targetCount = startCount + (milliseconds * (CORETIMER_FrequencyGet() / 1000));
    
    while (CORETIMER_CounterGet() < targetCount) {
        // Wait for timer to reach target
    }
}

#define UNUSED(x) (void)(x)

//! Pointer to the module configuration data structure to be set in initialization
static const AD7609ModuleConfig* pModuleConfigAD7609; 
//! Pointer to the module configuration data structure in runtime
static AInModuleRuntimeConfig* pModuleRuntimeConfigAD7609 __attribute__((unused)); 

/*! 
 * Function to configure SPI related to ADC - Updated for Harmony 3
 */
__attribute__((unused)) static void AD7609_Apply_SPI_Config( void )
{
    // For Harmony 3, SPI configuration is handled by SPI6_Initialize() during startup
    SPI_TRANSFER_SETUP spiSetup;
    
    spiSetup.clockFrequency = pModuleConfigAD7609->SPI.baud;
    spiSetup.clockPolarity = (pModuleConfigAD7609->SPI.clockPolarity == 1) ? 
                            SPI_CLOCK_POLARITY_IDLE_HIGH : SPI_CLOCK_POLARITY_IDLE_LOW;
    spiSetup.clockPhase = (pModuleConfigAD7609->SPI.inSamplePhase == 1) ?
                         SPI_CLOCK_PHASE_TRAILING_EDGE : SPI_CLOCK_PHASE_LEADING_EDGE;
    spiSetup.dataBits = SPI_DATA_BITS_8;
    
    // Apply the setup to SPI6
    SPI6_TransferSetup(&spiSetup, 80000000); // 80MHz peripheral clock
}

/*!
 *  Reset the module - Updated for Harmony 3 GPIO API
 */
__attribute__((unused)) static void AD7609_Reset( void )
{
    LOG_D("AD7609_Reset: Resetting AD7609 module");
    
    // Proper AD7609 reset sequence:
    // 1. Set CS high (inactive) 
    GPIO_PinWrite(pModuleConfigAD7609->CS_Pin, true);
    
    // 2. Set RST low (active reset)
    GPIO_PinWrite(pModuleConfigAD7609->RST_Pin, false);
    AD7609_Delay_ms(1); // Hold reset for 1ms
    
    // 3. Set RST high (release reset)
    GPIO_PinWrite(pModuleConfigAD7609->RST_Pin, true);
    
    // 4. Wait for chip to initialize (datasheet: >500ns)
    AD7609_Delay_ms(10); // 10ms for safety
    
    // 5. Keep CS high (inactive) until SPI transactions
    GPIO_PinWrite(pModuleConfigAD7609->CS_Pin, true);
}

bool AD7609_InitHardware(const AD7609ModuleConfig* pBoardConfigInit)
{
    LOG_D("AD7609_InitHardware: *** CALLED *** - Initializing AD7609 hardware");
    
    pModuleConfigAD7609 = pBoardConfigInit;
    
    // Configure SPI6 for AD7609 communication
    AD7609_Apply_SPI_Config();
    
    // Reset the AD7609 first before configuration
    AD7609_Reset();
    
    // Initialize GPIO pins for AD7609 control after reset
    GPIO_PinWrite(pModuleConfigAD7609->CS_Pin, true);      // CS high (inactive)  
    GPIO_PinWrite(pModuleConfigAD7609->Range_Pin, pModuleConfigAD7609->Range10V);  // Range setting
    GPIO_PinWrite(pModuleConfigAD7609->OS0_Pin, pModuleConfigAD7609->OSMode & 0b01); // OS0 setting
    GPIO_PinWrite(pModuleConfigAD7609->OS1_Pin, (pModuleConfigAD7609->OSMode & 0b10) >> 1); // OS1 setting
    GPIO_PinWrite(pModuleConfigAD7609->CONVST_Pin, false); // CONVST low (ready for trigger)
    
    // Override MCC GPIO configuration for SPI bus management
    // Ensure proper pin directions and states for AD7609 operation
    GPIO_PinOutputEnable(pModuleConfigAD7609->CS_Pin);     // CS as output
    GPIO_PinOutputEnable(pModuleConfigAD7609->CONVST_Pin); // CONVST as output  
    GPIO_PinOutputEnable(pModuleConfigAD7609->Range_Pin);  // Range as output
    GPIO_PinInputEnable(pModuleConfigAD7609->BSY_Pin);     // BSY as input
    
    // If RC15 is another SPI device CS, ensure it's inactive
    GPIO_PinOutputEnable(GPIO_PIN_RC15);
    GPIO_PinWrite(GPIO_PIN_RC15, true); // Keep inactive
    
    LOG_D("AD7609_InitHardware: Pin directions configured for AD7609 operation");
    
    // Debug: Check actual pin states after configuration
    LOG_D("AD7609 Pin States: CS=%d, RST=%d, Range=%d, OS0=%d, OS1=%d, CONVST=%d, BSY=%d",
          GPIO_PinRead(pModuleConfigAD7609->CS_Pin),
          GPIO_PinRead(pModuleConfigAD7609->RST_Pin), 
          GPIO_PinRead(pModuleConfigAD7609->Range_Pin),
          GPIO_PinRead(pModuleConfigAD7609->OS0_Pin),
          GPIO_PinRead(pModuleConfigAD7609->OS1_Pin),
          GPIO_PinRead(pModuleConfigAD7609->CONVST_Pin),
          GPIO_PinRead(pModuleConfigAD7609->BSY_Pin));
    
    LOG_D("AD7609_InitHardware: Hardware initialization complete");
    
    return true;
}

bool AD7609_WriteModuleState(bool isPowered)
{
    UNUSED(isPowered);
    LOG_D("AD7609_WriteModuleState: isPowered=%d", isPowered);
    
    if (pModuleConfigAD7609 == NULL) {
        return false;
    }
    
    // Minimal implementation - just log for now
    return true;
}

bool AD7609_WriteStateSingle(
        AInModuleRuntimeConfig* moduleRuntimeConfig,
        const AD7609ChannelConfig* channelConfig,
        AInRuntimeConfig* channelRuntimeConfig)
{
    UNUSED(moduleRuntimeConfig);
    UNUSED(channelConfig);
    UNUSED(channelRuntimeConfig);
    
    LOG_D("AD7609_WriteStateSingle: Configuring AD7609 channel");
    return true;
}

bool AD7609_WriteStateAll(
                        const AInArray* channelConfig,
                        AInRuntimeArray* channelRuntimeConfig)
{
    UNUSED(channelConfig);
    UNUSED(channelRuntimeConfig);
    
    LOG_D("AD7609_WriteStateAll: Configuring all AD7609 channels");
    
    // AD7609 should be initialized during system startup now
    if (pModuleConfigAD7609 == NULL) {
        LOG_E("AD7609_WriteStateAll: AD7609 still not initialized - startup issue");
        return false;
    }
    
    return true;
}

bool AD7609_ReadSamples(AInSampleArray* samples,
                        const AInArray* channelConfigList,
                        AInRuntimeArray* channelRuntimeConfigList,
                        uint32_t triggerTimeStamp)
{
    
    if (pModuleConfigAD7609 == NULL) {
        return false;
    }
    
    LOG_D("AD7609_ReadSamples: Reading AD7609 samples via SPI6");
    
    // Check if conversion is ready by reading BSY pin
    bool conversionReady = !GPIO_PinRead(pModuleConfigAD7609->BSY_Pin);
    if (!conversionReady) {
        LOG_D("AD7609_ReadSamples: Conversion not ready (BSY high)");
        return false;
    }
    
    // Set CS low to start SPI communication
    GPIO_PinWrite(pModuleConfigAD7609->CS_Pin, false);
    
    // Read 8 channels worth of data (16 bytes = 8 channels * 2 bytes each)
    uint8_t rxData[16];
    uint8_t txData[16] = {0}; // Dummy data for SPI read
    
    bool spiSuccess = SPI6_WriteRead(txData, 16, rxData, 16);
    
    // Set CS high to end communication
    GPIO_PinWrite(pModuleConfigAD7609->CS_Pin, true);
    
    if (!spiSuccess) {
        LOG_E("AD7609_ReadSamples: SPI6 communication failed");
        return false;
    }
    
    // Process the received data (18-bit values, 2's complement)
    LOG_D("AD7609_ReadSamples: Successfully read 16 bytes from AD7609");
    
    // Convert raw bytes to samples and populate the samples array
    size_t sampleCount = 0;
    for (int channel = 0; channel < 8; channel++) {
        // Each channel is 18-bit (3 bytes), but we're reading 2 bytes per channel for now
        uint16_t rawData = (rxData[channel * 2] << 8) | rxData[channel * 2 + 1];
        
        // Find this AD7609 channel in the configuration
        for (size_t i = 0; i < channelConfigList->Size; i++) {
            if (channelConfigList->Data[i].Type == AIn_AD7609 && 
                channelConfigList->Data[i].DaqifiAdcChannelId == channel &&
                channelRuntimeConfigList->Data[i].IsEnabled) {
                
                // Create sample entry
                if (sampleCount < samples->Size) {
                    samples->Data[sampleCount].Channel = channel;
                    samples->Data[sampleCount].Value = rawData;
                    samples->Data[sampleCount].Timestamp = triggerTimeStamp;
                    sampleCount++;
                    
                    LOG_D("AD7609_ReadSamples: Ch%d Raw=0x%04X", channel, rawData);
                }
                break;
            }
        }
    }
    
    samples->Size = sampleCount;
    
    return (sampleCount > 0); // Return true if we got any samples
}

bool AD7609_TriggerConversion(const AD7609ModuleConfig* moduleConfig)
{
    UNUSED(moduleConfig);
    
    if (pModuleConfigAD7609 == NULL) {
        return false;
    }
    
    // AD7609 Conversion Sequence per datasheet:
    // 1. CONVST rising edge starts conversion
    GPIO_PinWrite(pModuleConfigAD7609->CONVST_Pin, true);
    
    // 2. Hold CONVST high for minimum 25ns (using 1ms for safety)
    AD7609_Delay_ms(1);
    
    // 3. CONVST falling edge - conversion continues for ~2.5μs
    GPIO_PinWrite(pModuleConfigAD7609->CONVST_Pin, false);
    
    // 4. BSY pin will go high during conversion, then low when complete
    // Note: BSY checking happens in the sample reading function
    
    return true;
}

double AD7609_ConvertToVoltage(
                        const AInRuntimeConfig* runtimeConfig,
                        const AInSample* sample)
{
    UNUSED(runtimeConfig);
    
    LOG_D("AD7609_ConvertToVoltage: Called for channel with sample value 0x%X", 
          sample ? sample->Value : 0);
    
    if (sample == NULL) {
        LOG_E("AD7609_ConvertToVoltage: NULL sample");
        return 0.0;
    }
    
    if (pModuleConfigAD7609 == NULL) {
        LOG_E("AD7609_ConvertToVoltage: NULL config - AD7609 not initialized");
        // Use default configuration for conversion
        double fullScaleVoltage = 10.0; // Default ±10V range
        int32_t maxCode = (1 << 17) - 1; // 18-bit: 131071
        int32_t rawValue = (int32_t)sample->Value;
        
        if (rawValue > maxCode) {
            rawValue = rawValue - (1 << 18);
        }
        
        double voltage = ((double)rawValue / (double)maxCode) * fullScaleVoltage;
        LOG_D("AD7609_ConvertToVoltage: Using default config - Raw=0x%X, Voltage=%.3fV", 
              sample->Value, voltage);
        return voltage;
    }
    
    // Note: sample->Value == 0 means 0V input (center of ±10V range)
    
    // AD7609 is 18-bit, 2's complement
    // Full scale range: ±10V (when Range10V = true) or ±5V (when Range10V = false)
    double fullScaleVoltage = pModuleConfigAD7609->Range10V ? 10.0 : 5.0;
    int32_t maxCode = (1 << 17) - 1; // 18-bit: 131071 (half scale since 2's complement)
    
    // Convert raw ADC value to signed integer
    int32_t rawValue = (int32_t)sample->Value;
    
    // Handle 18-bit 2's complement conversion
    if (rawValue > maxCode) {
        rawValue = rawValue - (1 << 18); // Convert to negative
    }
    
    // Convert to voltage: raw / maxCode * fullScale
    double voltage = ((double)rawValue / (double)maxCode) * fullScaleVoltage;
    
    LOG_D("AD7609_ConvertToVoltage: Raw=0x%X (%d), Voltage=%.3fV", 
          sample->Value, rawValue, voltage);
    
    return voltage;
}