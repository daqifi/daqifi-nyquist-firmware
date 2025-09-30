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
#include "state/runtime/BoardRuntimeConfig.h"
#include "Util/Logger.h"
#include "FreeRTOS.h"
#include "task.h"

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

// AD7609 BSY interrupt handling
static TaskHandle_t gAD7609_TaskHandle = NULL;

// Accessor function for task handle (used by tasks.c)
void* AD7609_GetTaskHandle(void) {
    return &gAD7609_TaskHandle;
}

// AD7609 BSY pin interrupt callback (called from GPIO ISR)
void AD7609_BSY_InterruptCallback(GPIO_PIN pin, uintptr_t context) {
    // Verify this is the BSY pin (RB3)
    if (pin == GPIO_PIN_RB3 && gAD7609_TaskHandle != NULL) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        // Notify deferred task that conversion is complete
        vTaskNotifyGiveFromISR(gAD7609_TaskHandle, &xHigherPriorityTaskWoken);
        portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
    }
}

// AD7609 deferred interrupt task (handles SPI read after BSY interrupt)
void AD7609_DeferredInterruptTask(void) {
    const TickType_t xBlockTime = portMAX_DELAY;
    
    while (1) {
        // Wait for BSY pin interrupt to signal conversion complete
        ulTaskNotifyTake(pdFALSE, xBlockTime);
        
        // TODO: Read AD7609 SPI data here
        // This will be called when BSY goes low (conversion complete)
    }
} 

/*! 
 * Function to configure SPI related to ADC - Updated for Harmony 3
 */
static void AD7609_Apply_SPI_Config( void )
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
static void AD7609_Reset( void )
{
    AD7609_Delay_ms(100); // Delay reset for 100ms to ensure power on is completed properly 
    
    // Proper AD7609 reset sequence (from working commit):
    // 1. Set CS high (inactive) 
    GPIO_PinWrite(pModuleConfigAD7609->CS_Pin, true);
    
    // 2. Set RST high (active reset)
    GPIO_PinWrite(pModuleConfigAD7609->RST_Pin, true);
    AD7609_Delay_ms(1); // Hold reset for 1ms
    
    // 3. Set RST low (release reset)
    GPIO_PinWrite(pModuleConfigAD7609->RST_Pin, false);
    
    // 4. Wait for chip to initialize (datasheet: >500ns)
    AD7609_Delay_ms(1); // 10ms for safety
}

bool AD7609_InitHardware(const AD7609ModuleConfig* pBoardConfigInit)
{
    // Initialize AD7609 hardware after power-up
    
    pModuleConfigAD7609 = pBoardConfigInit;
    
    // Configure SPI6 for AD7609 communication
    AD7609_Apply_SPI_Config();
      
    // Initialize GPIO pins for AD7609 control after reset
    GPIO_PinWrite(pModuleConfigAD7609->CS_Pin, true);      // CS high (inactive)  
    GPIO_PinWrite(pModuleConfigAD7609->Range_Pin, pModuleConfigAD7609->Range10V);  // Range setting
    GPIO_PinWrite(pModuleConfigAD7609->OS0_Pin, pModuleConfigAD7609->OSMode & 0b01); // OS0 setting
    GPIO_PinWrite(pModuleConfigAD7609->OS1_Pin, (pModuleConfigAD7609->OSMode & 0b10) >> 1); // OS1 setting
    GPIO_PinWrite(pModuleConfigAD7609->CONVST_Pin, false); // CONVST low (ready for trigger)
    GPIO_PinWrite(pModuleConfigAD7609->STBY_Pin, true); // STBY high (normal operation, active low)
    GPIO_PinWrite(pModuleConfigAD7609->RST_Pin, true); // RST low
    
    // Override MCC GPIO configuration for SPI bus management
    // Ensure proper pin directions and states for AD7609 operation
    GPIO_PinOutputEnable(pModuleConfigAD7609->CS_Pin);      // CS as output
    GPIO_PinOutputEnable(pModuleConfigAD7609->Range_Pin);   // Range as output
    GPIO_PinOutputEnable(pModuleConfigAD7609->OS0_Pin);   // OS0 as output
    GPIO_PinOutputEnable(pModuleConfigAD7609->OS1_Pin);   // OS1 as output
    GPIO_PinOutputEnable(pModuleConfigAD7609->CONVST_Pin);  // CONVST as output 
    GPIO_PinOutputEnable(pModuleConfigAD7609->STBY_Pin);    // STBY as output
    GPIO_PinOutputEnable(pModuleConfigAD7609->RST_Pin);    // RST as output
    
    GPIO_PinInputEnable(pModuleConfigAD7609->BSY_Pin);      // BSY as input
    
    // If RC15 is another SPI device CS, ensure it's inactive
    GPIO_PinOutputEnable(GPIO_PIN_RC15);
    GPIO_PinWrite(GPIO_PIN_RC15, true); // Keep inactive
    
    // Reset the AD7609
    AD7609_Reset();

    // Register BSY pin interrupt callback (falling edge on conversion complete)
    // Note: Task creation is handled in config/default/tasks.c
    GPIO_PinInterruptCallbackRegister(GPIO_PIN_RB3, AD7609_BSY_InterruptCallback, 0);
    GPIO_PinInterruptEnable(GPIO_PIN_RB3);

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
    uint8_t x = 0;
    uint8_t SPIData = 0;
    uint16_t Data16[9]; // 9 channels like original working implementation
    
    if (pModuleConfigAD7609 == NULL) {
        return false;
    }
    
    // Apply SPI configuration (like original)
    AD7609_Apply_SPI_Config();
    
    // Check if conversion is ready by reading BSY pin (BSY low = ready)
    // Note: BSY check bypassed - using interrupt-driven approach instead
    
    // Set CS low to start SPI communication
    GPIO_PinWrite(pModuleConfigAD7609->CS_Pin, false);
    
    // No delay needed - datasheet doesn't require settling time
    
    // Read 9 channels using byte-by-byte SPI (matches original working implementation)
    
    for (x = 0; x < 9; x++) {
        // Read high byte first
        uint8_t txByte = 0x00;
        while (SPI6_IsBusy()) { /* Wait */ }
        bool success1 = SPI6_WriteRead(&txByte, 1, &SPIData, 1);
        while (SPI6_IsBusy()) { /* Wait */ }
        if (!success1) {
            LOG_E("AD7609_ReadSamples: SPI failed on channel %d high byte", x);
            GPIO_PinWrite(pModuleConfigAD7609->CS_Pin, true);
            return false;
        }
        uint8_t highByte = SPIData;
        
        // Read low byte (no delay needed - hardware handles timing)
        while (SPI6_IsBusy()) { /* Wait */ }
        bool success2 = SPI6_WriteRead(&txByte, 1, &SPIData, 1);
        while (SPI6_IsBusy()) { /* Wait */ }
        if (!success2) {
            LOG_E("AD7609_ReadSamples: SPI failed on channel %d low byte", x);
            GPIO_PinWrite(pModuleConfigAD7609->CS_Pin, true);
            return false;
        }
        uint8_t lowByte = SPIData;
        
        // Combine high and low bytes
        Data16[x] = (highByte << 8) | lowByte;
    }
    
    // Set CS high to end communication
    GPIO_PinWrite(pModuleConfigAD7609->CS_Pin, true);
    
    // Convert raw bytes to samples and populate the samples array
    // Use channel mapping from configuration (ChannelNumber → hardware channel)
    size_t sampleCount = 0;
    for (size_t i = 0; i < channelConfigList->Size; i++) {
        if (channelConfigList->Data[i].Type == AIn_AD7609 &&
            channelRuntimeConfigList->Data[i].IsEnabled) {

            // Get the hardware channel number from config
            uint8_t hwChannel = channelConfigList->Data[i].Config.AD7609.ChannelNumber;

            // Validate hardware channel is in range
            if (hwChannel >= 8) {
                continue; // Skip invalid channels
            }

            // Get raw data from the hardware channel
            uint16_t rawData = Data16[hwChannel];

            // Create sample entry with DAQiFi channel ID
            if (sampleCount < samples->Size) {
                samples->Data[sampleCount].Channel = channelConfigList->Data[i].DaqifiAdcChannelId;
                samples->Data[sampleCount].Value = rawData;
                samples->Data[sampleCount].Timestamp = triggerTimeStamp;
                sampleCount++;
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
        LOG_E("AD7609_TriggerConversion: pModuleConfigAD7609 is NULL");
        return false;
    }
    
    // Check if the AD7609 is busy
    bool busy = GPIO_PinRead(pModuleConfigAD7609->BSY_Pin);
    if (busy) {
        return false; // Skip conversion if still busy
    }
    
    GPIO_PinWrite(pModuleConfigAD7609->CONVST_Pin, true);
    
    // PIC32MZ2048 nop is ~56ns at full 200MHz.  AD7609 requires 25ns pulse, so 10x should be goodish
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");
    asm("nop");

    GPIO_PinWrite(pModuleConfigAD7609->CONVST_Pin, false);
    return true;
}

double AD7609_ConvertToVoltage(
                        const AInRuntimeConfig* runtimeConfig,
                        const AInSample* sample)
{
    UNUSED(runtimeConfig);

    if (sample == NULL) {
        LOG_E("AD7609_ConvertToVoltage: NULL sample");
        return 0.0;
    }

    // Get runtime range from module configuration using BoardRunTimeConfig_Get
    AInModRuntimeArray* pRuntimeModules = BoardRunTimeConfig_Get(BOARDRUNTIMECONFIG_AIN_MODULES);
    double fullScaleVoltage = 10.0; // Default ±10V range

    if (pRuntimeModules != NULL && pRuntimeModules->Size > AIn_AD7609) {
        // AD7609 module index is AIn_AD7609 (1)
        fullScaleVoltage = pRuntimeModules->Data[AIn_AD7609].Range;
    }

    // AD7609 is 18-bit, 2's complement
    int32_t maxCode = (1 << 17) - 1; // 18-bit: 131071 (half scale since 2's complement)

    // Convert raw ADC value to signed integer
    int32_t rawValue = (int32_t)sample->Value;

    // Handle 18-bit 2's complement conversion
    if (rawValue > maxCode) {
        rawValue = rawValue - (1 << 18); // Convert to negative
    }

    // Convert to voltage: raw / maxCode * fullScale
    double voltage = ((double)rawValue / (double)maxCode) * fullScaleVoltage;
    return voltage;
}