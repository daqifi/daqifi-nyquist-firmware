/*! @file AD7609.c
 *
 * @brief AD7609 8-Channel, 18-bit ADC Driver
 *
 * This file implements the hardware abstraction layer for the Analog Devices AD7609
 * simultaneous sampling, 18-bit ADC. The driver supports serial data interface mode
 * with manual chip select control via Harmony DRV_SPI.
 *
 * Key Features:
 * - 8 differential input channels
 * - 18-bit resolution with 2's complement output
 * - Configurable ±5V or ±10V input range per module
 * - SPI interface at 10 MHz
 * - Manual conversion triggering via CONVST pin
 * - Busy signal monitoring for conversion complete
 */

#include "AD7609.h"
#include "configuration.h"
#include "definitions.h"
#include "driver/spi/drv_spi.h"
#include "peripheral/coretimer/plib_coretimer.h"
#include "state/board/BoardConfig.h"
#include "state/runtime/BoardRuntimeConfig.h"
#include "HAL/ADC.h"
#include "Util/Logger.h"
#include "FreeRTOS.h"
#include "task.h"

// Simple delay function using core timer (wrap-around safe)
static void AD7609_Delay_ms(uint32_t milliseconds) {
    uint32_t startCount = CORETIMER_CounterGet();
    uint32_t ticks = (milliseconds * (CORETIMER_FrequencyGet() / 1000U));

    // Use modular arithmetic to handle 32-bit timer wrap-around correctly
    // Subtraction of unsigned values wraps correctly
    while ((uint32_t)(CORETIMER_CounterGet() - startCount) < ticks) {
        // Wait for elapsed ticks
    }
}

#define UNUSED(x) (void)(x)

//! Pointer to the module configuration data structure to be set in initialization
static const AD7609ModuleConfig* pModuleConfigAD7609;
//! Pointer to the module configuration data structure in runtime
static AInModuleRuntimeConfig* pModuleRuntimeConfigAD7609 __attribute__((unused));

// DRV_SPI handle for AD7609 communication
static DRV_HANDLE spi_handle = DRV_HANDLE_INVALID;

// AD7609 BSY interrupt handling
static TaskHandle_t gAD7609_TaskHandle = NULL;

// DMA-coherent SPI buffers (must be global for cache coherency)
static __attribute__((coherent)) uint8_t gAD7609_txBuffer[18];
static __attribute__((coherent)) uint8_t gAD7609_rxBuffer[18];

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

        // Call ADC layer to handle data acquisition and storage
        // This separates hardware driver from data management
        ADC_HandleAD7609Interrupt();
    }
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

    // Open DRV_SPI driver for SPI6 (DRV_SPI_INDEX_1 maps to SPI6)
    spi_handle = DRV_SPI_Open(DRV_SPI_INDEX_1, DRV_IO_INTENT_READWRITE);
    if (spi_handle == DRV_HANDLE_INVALID) {
        LOG_E("AD7609_InitHardware: Failed to open SPI driver");
        return false;
    }

    // Initialize GPIO pins for AD7609 control after reset
    GPIO_PinWrite(pModuleConfigAD7609->CS_Pin, true);      // CS high (inactive)
    // Per datasheet: Range pin HIGH=±20V, LOW=±10V
    // But hardware may be wired differently - use inverted logic to match SCPI implementation
    GPIO_PinWrite(pModuleConfigAD7609->Range_Pin, !pModuleConfigAD7609->Range10V);  // Range setting
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
    // AD7609 BSY goes LOW (falling edge) when conversion is complete
    GPIO_PinInterruptCallbackRegister(GPIO_PIN_RB3, AD7609_BSY_InterruptCallback, 0);
    GPIO_PinIntEnable(GPIO_PIN_RB3, GPIO_INTERRUPT_ON_FALLING_EDGE);

    return true;
}

bool AD7609_WriteModuleState(bool isPowered)
{
    UNUSED(isPowered);

    if (pModuleConfigAD7609 == NULL) {
        return false;
    }

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

    return true;
}

bool AD7609_WriteStateAll(
                        const AInArray* channelConfig,
                        AInRuntimeArray* channelRuntimeConfig)
{
    UNUSED(channelConfig);
    UNUSED(channelRuntimeConfig);

    if (pModuleConfigAD7609 == NULL) {
        LOG_E("AD7609_WriteStateAll: AD7609 not initialized");
        return false;
    }

    return true;
}

bool AD7609_ReadSamples(AInSampleArray* samples,
                        const AInArray* channelConfigList,
                        AInRuntimeArray* channelRuntimeConfigList,
                        uint32_t triggerTimeStamp)
{
    if (pModuleConfigAD7609 == NULL || spi_handle == DRV_HANDLE_INVALID) {
        return false;
    }

    // Check if conversion is ready by reading BSY pin (BSY low = ready)
    if (GPIO_PinRead(pModuleConfigAD7609->BSY_Pin)) {
        return false; // Chip not ready
    }

    // Clear DMA buffers (global, coherent for DMA access)
    memset(gAD7609_txBuffer, 0, sizeof(gAD7609_txBuffer));
    memset(gAD7609_rxBuffer, 0, sizeof(gAD7609_rxBuffer));

    // Assert CS (active low)
    GPIO_PinWrite(pModuleConfigAD7609->CS_Pin, false);

    // Use DRV_SPI async API (driver is in async mode for WiFi compatibility)
    DRV_SPI_TRANSFER_HANDLE transferHandle;
    DRV_SPI_WriteReadTransferAdd(spi_handle, gAD7609_txBuffer, 18, gAD7609_rxBuffer, 18, &transferHandle);

    if (transferHandle == DRV_SPI_TRANSFER_HANDLE_INVALID) {
        LOG_E("AD7609_ReadSamples: Failed to queue SPI transfer");
        GPIO_PinWrite(pModuleConfigAD7609->CS_Pin, true);  // Deassert CS
        return false;
    }

    // Poll for completion (blocking wait)
    DRV_SPI_TRANSFER_EVENT event;
    do {
        event = DRV_SPI_TransferStatusGet(transferHandle);
    } while (event == DRV_SPI_TRANSFER_EVENT_PENDING);

    // Deassert CS (inactive high)
    GPIO_PinWrite(pModuleConfigAD7609->CS_Pin, true);

    if (event != DRV_SPI_TRANSFER_EVENT_COMPLETE) {
        LOG_E("AD7609_ReadSamples: SPI transfer failed with event %d", event);
        return false;
    }

    // AD7609 sends data in serial mode: 18 bits per channel, 8 channels sequentially
    // Total: 144 bits (18 bytes) in continuous stream, MSB first per channel
    // We need to extract each 18-bit value from the bit stream

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

            // Calculate bit position in the 144-bit stream
            uint16_t bitPosition = hwChannel * 18;  // Each channel is 18 bits
            uint8_t byteIndex = bitPosition / 8;     // Starting byte
            uint8_t bitOffset = bitPosition % 8;     // Bit offset within byte

            // Extract 18-bit value spanning up to 3 bytes
            uint32_t tmpData;

            if (bitOffset == 0) {
                // Aligned on byte boundary (bits 0-17 from 3 bytes)
                tmpData = ((uint32_t)gAD7609_rxBuffer[byteIndex] << 10) |
                          ((uint32_t)gAD7609_rxBuffer[byteIndex + 1] << 2) |
                          ((uint32_t)gAD7609_rxBuffer[byteIndex + 2] >> 6);
            } else if (bitOffset <= 6) {
                // Spans 3 bytes
                tmpData = ((uint32_t)(gAD7609_rxBuffer[byteIndex] & (0xFF >> bitOffset)) << (10 + bitOffset)) |
                          ((uint32_t)gAD7609_rxBuffer[byteIndex + 1] << (2 + bitOffset)) |
                          ((uint32_t)gAD7609_rxBuffer[byteIndex + 2] >> (6 - bitOffset));
            } else {
                // bitOffset = 7, spans 3 bytes
                tmpData = ((uint32_t)(gAD7609_rxBuffer[byteIndex] & 0x01) << 17) |
                          ((uint32_t)gAD7609_rxBuffer[byteIndex + 1] << 9) |
                          ((uint32_t)gAD7609_rxBuffer[byteIndex + 2] << 1) |
                          ((uint32_t)gAD7609_rxBuffer[byteIndex + 3] >> 7);
            }

            // Mask to 18 bits
            tmpData &= 0x3FFFF;

            // Convert from 18-bit 2's complement to signed 32-bit
            if (tmpData & 0x20000) {  // Check bit 17 (sign bit)
                tmpData |= 0xFFFC0000;  // Sign extend
            }

            // Create sample entry with DAQiFi channel ID
            if (sampleCount < samples->Size) {
                samples->Data[sampleCount].Channel = channelConfigList->Data[i].DaqifiAdcChannelId;
                samples->Data[sampleCount].Value = tmpData;
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

    // Generate deterministic CONVST pulse using core timer
    // AD7609 requires minimum 25ns pulse, use 200ns for safety margin
    const uint32_t coreTimerFreq = CORETIMER_FrequencyGet();
    const uint32_t ticksForPulse = (coreTimerFreq + 4999999U) / 5000000U; // 200ns = 1/(5MHz)

    uint32_t startCount = CORETIMER_CounterGet();
    GPIO_PinWrite(pModuleConfigAD7609->CONVST_Pin, true);

    // Wait for pulse duration using wrap-safe arithmetic
    while ((uint32_t)(CORETIMER_CounterGet() - startCount) < ticksForPulse) {
        // Busy-wait for 200ns pulse width
    }

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
    const int32_t maxCode = (1 << 17) - 1; // 131071 (half scale for signed 18-bit)

    // Convert raw ADC value to signed integer
    int32_t rawValue = (int32_t)sample->Value;

    // Handle 18-bit 2's complement conversion by checking sign bit
    // If bit 17 is set, the value is negative and needs sign extension
    if (rawValue & (1 << 17)) {  // Check sign bit (bit 17)
        rawValue |= ~((1 << 18) - 1);  // Sign extend from 18 bits to 32 bits
    }

    // Convert to voltage: raw / maxCode * fullScale
    double voltage = ((double)rawValue / (double)maxCode) * fullScaleVoltage;
    return voltage;
}